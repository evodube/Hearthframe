#include "Gallery.h"

#include "Dds.h"
#include "Frames.h"
#include "Ui.h"
#include "Wall.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_dx11.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

namespace fs = std::filesystem;

namespace Gallery
{
	namespace
	{
		constexpr int kThumbMaxDim = 480;  // sharp enough for 320 px thumbnails on a 4K screen
		constexpr int kFullMaxDim = 4096;
		constexpr int kTexturesPerFrame = 6;

		// DirectInput scan codes (what Skyrim's ButtonEvent::idCode holds for the keyboard)
		constexpr std::uint32_t kDIK_Escape = 0x01;
		constexpr std::uint32_t kDIK_Left = 0xCB;
		constexpr std::uint32_t kDIK_Right = 0xCD;
		constexpr std::uint32_t kDIK_Up = 0xC8;
		constexpr std::uint32_t kDIK_Down = 0xD0;

		// ---------------------------------------------------------------- settings
		struct Settings
		{
			fs::path      folder{ L"Data\\SKSE\\Plugins\\FolderGallery\\Pictures" };
			int           toggleKey = VK_F8;  // virtual-key code from the INI
			std::uint32_t toggleScan = 0x42;  // same key as a DirectInput scan code
			bool          recursive = false;
			int           thumbSize = 200;
			float         fontSize = 18.0f;
			float         mouseSpeed = 1.0f;
			float         uiScale = 1.0f;  // extra UI scale on top of the automatic screen-height scale
		} cfg;

		DebugSwitches dbg;
		std::atomic<bool> g_inputDisabled{ false };

		std::uint32_t VirtualKeyToDIK(int vk)
		{
			const UINT sc = ::MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
			if (sc == 0) {
				return 0;
			}
			// Extended keys come back as 0xE0xx; DirectInput marks them with the high bit instead.
			if ((sc & 0xFF00) == 0xE000 || (sc & 0xFF00) == 0xE100) {
				return (sc & 0x7F) | 0x80;
			}
			return sc & 0xFF;
		}

		// ---------------------------------------------------------------- state
		enum class State
		{
			None,
			Queued,
			Ready,
			Failed
		};

		struct Item
		{
			fs::path                  path;
			std::string               name;
			State                     state = State::None;
			ID3D11ShaderResourceView* srv = nullptr;
			int                       w = 0, h = 0;          // thumbnail texture size
			int                       origW = 0, origH = 0;  // size of the file on disk
			std::string               error;                 // why it couldn't be read
		};
		bool g_virtualFileHint = false;  // a listed picture couldn't be opened by name (MO2 virtual Data folder)

		struct Job
		{
			int      gen = 0;
			int      index = 0;
			fs::path path;
			int      maxDim = 0;
			bool     full = false;
		};

		struct Result
		{
			int                       gen = 0;
			int                       index = 0;
			bool                      full = false;
			bool                      ok = false;
			bool                      notFound = false;
			int                       w = 0, h = 0, origW = 0, origH = 0;
			std::vector<std::uint8_t> px;
			std::string               error;
		};

		// Heap-allocated and never freed so the detached worker can't touch destroyed objects at exit.
		struct Shared
		{
			std::mutex              mtx;
			std::condition_variable cv;
			std::deque<Job>         jobs;
			std::deque<Result>      results;
		};
		Shared&          S = *new Shared;
		std::atomic<int> g_gen{ 0 };

		// Input arrives on the game's input thread/callback; ImGui is only touched in Present.
		struct InputMsg
		{
			enum class Kind
			{
				MouseMove,
				MouseButton,
				Wheel,
				UiScale,  // Ctrl + wheel: x = steps
				Key
			} kind;
			float    x = 0.0f, y = 0.0f;
			int      button = 0;
			ImGuiKey key = ImGuiKey_None;
			bool     down = false;
		};
		std::mutex            g_inputMtx;
		std::vector<InputMsg> g_input;

		std::vector<Item>         items;
		int                       viewing = -1;
		ID3D11ShaderResourceView* fullSrv = nullptr;
		int                       fullW = 0, fullH = 0;
		bool                      fullFailed = false;

		enum class UiMode
		{
			Gallery,
			FrameMenu
		};
		UiMode                             g_uiMode = UiMode::Gallery;
		Wall::FrameInfo                    g_frameMenu;           // frame the player activated
		std::optional<RE::ObjectRefHandle> g_replaceTarget;       // set while choosing a new picture for a frame
		int                                g_hangShape = 0;
		int                                g_hangStyle = 2;
		bool                               g_placementControls = false;
		std::atomic<bool>                  g_shiftDown{ false };
		std::atomic<bool>                  g_ctrlDown{ false };
		float                              g_pendingUiScale = -1.0f;  // applied between frames (fonts are rebuilt)
		std::string                        g_iniPath;

		void RequestUiScale(float a_scale)
		{
			g_pendingUiScale = std::clamp(std::round(a_scale * 20.0f) / 20.0f, 0.6f, 2.0f);
		}

		std::atomic<bool> g_open{ false };
		std::atomic<bool> g_wantOpen{ false };  // toggled by the input sink, applied in Present
		bool              g_needScan = false;
		bool              g_imguiReady = false;
		bool              g_imguiFailed = false;
		bool              g_workerStarted = false;

		IDXGISwapChain*      g_sc = nullptr;
		ID3D11Device*        g_dev = nullptr;
		ID3D11DeviceContext* g_ctx = nullptr;
		float                g_mouseX = 0.0f, g_mouseY = 0.0f;
		std::chrono::steady_clock::time_point g_lastFrame{};
		float                g_displayH = 1080.0f;

		std::string ToUtf8(const fs::path& p)
		{
			const auto s = p.u8string();
			return std::string(s.begin(), s.end());
		}

		// ---------------------------------------------------------------- image decoding (worker thread)
		std::vector<std::uint8_t> Downscale(const std::uint8_t* src, int sw, int sh, int dw, int dh)
		{
			std::vector<std::uint8_t> out(static_cast<std::size_t>(dw) * dh * 4);
			for (int y = 0; y < dh; ++y) {
				const int y0 = static_cast<int>(static_cast<std::int64_t>(y) * sh / dh);
				const int y1 = (std::max)(y0 + 1, static_cast<int>(static_cast<std::int64_t>(y + 1) * sh / dh));
				for (int x = 0; x < dw; ++x) {
					const int     x0 = static_cast<int>(static_cast<std::int64_t>(x) * sw / dw);
					const int     x1 = (std::max)(x0 + 1, static_cast<int>(static_cast<std::int64_t>(x + 1) * sw / dw));
					std::uint32_t acc[4]{};
					for (int yy = y0; yy < y1; ++yy) {
						const std::uint8_t* p = src + (static_cast<std::size_t>(yy) * sw + x0) * 4;
						for (int xx = x0; xx < x1; ++xx, p += 4) {
							acc[0] += p[0];
							acc[1] += p[1];
							acc[2] += p[2];
							acc[3] += p[3];
						}
					}
					const auto    cnt = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
					std::uint8_t* o = &out[(static_cast<std::size_t>(y) * dw + x) * 4];
					o[0] = static_cast<std::uint8_t>(acc[0] / cnt);
					o[1] = static_cast<std::uint8_t>(acc[1] / cnt);
					o[2] = static_cast<std::uint8_t>(acc[2] / cnt);
					o[3] = static_cast<std::uint8_t>(acc[3] / cnt);
				}
			}
			return out;
		}

		bool LoadPixels(const fs::path& path, int maxDim, Result& r)
		{
			stbi_uc* px = Dds::DecodePicture(path, r.origW, r.origH, r.error, r.notFound);
			if (!px) {
				return false;
			}
			r.w = r.origW;
			r.h = r.origH;
			const int longest = (std::max)(r.origW, r.origH);
			if (longest > maxDim) {
				const double s = static_cast<double>(maxDim) / longest;
				r.w = (std::max)(1, static_cast<int>(r.origW * s));
				r.h = (std::max)(1, static_cast<int>(r.origH * s));
				r.px = Downscale(px, r.origW, r.origH, r.w, r.h);
			} else {
				r.px.assign(px, px + static_cast<std::size_t>(r.origW) * r.origH * 4);
			}
			stbi_image_free(px);
			return true;
		}

		void WorkerMain()
		{
			for (;;) {
				Job job;
				{
					std::unique_lock lk(S.mtx);
					S.cv.wait(lk, [] { return !S.jobs.empty(); });
					job = std::move(S.jobs.front());
					S.jobs.pop_front();
				}
				if (job.gen != g_gen.load()) {
					continue;  // the list was refreshed while this was waiting
				}
				Result r;
				r.gen = job.gen;
				r.index = job.index;
				r.full = job.full;
				try {
					r.ok = LoadPixels(job.path, job.maxDim, r);
				} catch (const std::exception& e) {
					r.ok = false;
					r.error = e.what();
				}
				if (!r.ok) {
					spdlog::warn("Can't read {}: {}", ToUtf8(job.path), r.error);
				}
				std::lock_guard lk(S.mtx);
				S.results.push_back(std::move(r));
			}
		}

		void Submit(Job job, bool front = false)
		{
			{
				std::lock_guard lk(S.mtx);
				if (front) {
					S.jobs.push_front(std::move(job));
				} else {
					S.jobs.push_back(std::move(job));
				}
			}
			S.cv.notify_one();
		}

		// ---------------------------------------------------------------- textures (render thread)
		ID3D11ShaderResourceView* MakeSRV(const std::uint8_t* px, int w, int h)
		{
			D3D11_TEXTURE2D_DESC d{};
			d.Width = static_cast<UINT>(w);
			d.Height = static_cast<UINT>(h);
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA sd{ px, static_cast<UINT>(w) * 4, 0 };

			ID3D11Texture2D* tex = nullptr;
			if (FAILED(g_dev->CreateTexture2D(&d, &sd, &tex)) || !tex) {
				return nullptr;
			}
			ID3D11ShaderResourceView* srv = nullptr;
			g_dev->CreateShaderResourceView(tex, nullptr, &srv);
			tex->Release();
			return srv;
		}

		void ReleaseFull()
		{
			if (fullSrv) {
				fullSrv->Release();
				fullSrv = nullptr;
			}
			fullW = fullH = 0;
			fullFailed = false;
		}

		void UploadReady()
		{
			for (int n = 0; n < kTexturesPerFrame; ++n) {
				Result r;
				{
					std::lock_guard lk(S.mtx);
					if (S.results.empty()) {
						return;
					}
					r = std::move(S.results.front());
					S.results.pop_front();
				}
				if (r.gen != g_gen.load() || r.index < 0 || r.index >= static_cast<int>(items.size())) {
					continue;
				}
				if (r.full) {
					if (r.index != viewing) {
						continue;
					}
					ReleaseFull();
					if (r.ok) {
						fullSrv = MakeSRV(r.px.data(), r.w, r.h);
						fullW = r.w;
						fullH = r.h;
					}
					fullFailed = (fullSrv == nullptr);
				} else {
					Item& it = items[r.index];
					it.origW = r.origW;
					it.origH = r.origH;
					it.error = r.error;
					if (r.notFound) {
						g_virtualFileHint = true;
					}
					if (r.ok) {
						it.srv = MakeSRV(r.px.data(), r.w, r.h);
						it.w = r.w;
						it.h = r.h;
					}
					it.state = it.srv ? State::Ready : State::Failed;
				}
			}
		}

		// ---------------------------------------------------------------- scanning
		bool IsPicture(const fs::path& p)
		{
			auto ext = p.extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".tga";
		}

		void Scan()
		{
			++g_gen;
			{
				std::lock_guard lk(S.mtx);
				S.jobs.clear();
				S.results.clear();
			}
			for (auto& it : items) {
				if (it.srv) {
					it.srv->Release();
				}
			}
			items.clear();
			g_virtualFileHint = false;
			viewing = -1;
			ReleaseFull();

			std::error_code ec;
			fs::create_directories(cfg.folder, ec);
			ec.clear();

			std::vector<std::pair<fs::file_time_type, fs::path>> found;
			auto consider = [&](const fs::directory_entry& e) {
				std::error_code e2;
				if (e.is_regular_file(e2) && IsPicture(e.path())) {
					found.emplace_back(e.last_write_time(e2), e.path());
				}
			};
			if (cfg.recursive) {
				for (fs::recursive_directory_iterator it(cfg.folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
					consider(*it);
				}
			} else {
				for (fs::directory_iterator it(cfg.folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
					consider(*it);
				}
			}

			std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });  // newest first
			items.reserve(found.size());
			for (auto& [time, path] : found) {
				Item it;
				it.path = path;
				it.name = ToUtf8(path.filename());
				items.push_back(std::move(it));
			}
			spdlog::info("Scanned {} -> {} picture(s)", ToUtf8(cfg.folder), items.size());
		}

		// ---------------------------------------------------------------- opening / closing (render thread)
		void ApplyOpen(bool open)
		{
			if (g_open == open) {
				return;
			}
			g_open = open;
			g_wantOpen = open;
			if (open && g_uiMode == UiMode::Gallery) {
				g_needScan = true;
			}
			if (!open) {
				g_uiMode = UiMode::Gallery;
				g_replaceTarget.reset();
			}

			using UEFlag = RE::ControlMap::UEFlag;
			constexpr auto blocked = static_cast<UEFlag>(
				static_cast<std::uint32_t>(UEFlag::kMovement) | static_cast<std::uint32_t>(UEFlag::kLooking) |
				static_cast<std::uint32_t>(UEFlag::kActivate) | static_cast<std::uint32_t>(UEFlag::kMenu) |
				static_cast<std::uint32_t>(UEFlag::kFighting) | static_cast<std::uint32_t>(UEFlag::kJumping) |
				static_cast<std::uint32_t>(UEFlag::kSneaking) | static_cast<std::uint32_t>(UEFlag::kWheelZoom) |
				static_cast<std::uint32_t>(UEFlag::kPOVSwitch));
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				cm->ToggleControls(blocked, !open);
			}
			if (g_imguiReady) {
				ImGui::GetIO().MouseDrawCursor = open;
			}
			spdlog::info("Gallery {}", open ? "opened" : "closed");
		}

		// While hanging a picture: the player can still walk and look around, but clicks and
		// activate/menu keys belong to us.
		void ApplyPlacementControls(bool a_on)
		{
			if (g_placementControls == a_on) {
				return;
			}
			g_placementControls = a_on;
			using UEFlag = RE::ControlMap::UEFlag;
			constexpr auto blocked = static_cast<UEFlag>(
				static_cast<std::uint32_t>(UEFlag::kActivate) | static_cast<std::uint32_t>(UEFlag::kMenu) |
				static_cast<std::uint32_t>(UEFlag::kFighting) | static_cast<std::uint32_t>(UEFlag::kWheelZoom));
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				cm->ToggleControls(blocked, !a_on);
			}
		}

		void PushInput(const InputMsg& m)
		{
			std::lock_guard lk(g_inputMtx);
			if (g_input.size() < 1024) {
				g_input.push_back(m);
			}
		}

		// ---------------------------------------------------------------- input (game input events)
		void HandleInput(RE::InputEvent* const* a_event)
		{
				if (!a_event) {
					return;
				}
				for (auto* e = *a_event; e; e = e->next) {
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
						if (g_open) {
							auto* mm = static_cast<RE::MouseMoveEvent*>(e);
							PushInput({ InputMsg::Kind::MouseMove, static_cast<float>(mm->mouseInputX), static_cast<float>(mm->mouseInputY) });
						}
						continue;
					}
					auto* btn = e->AsButtonEvent();
					if (!btn) {
						continue;
					}
					const auto code = btn->GetIDCode();
					const bool down = btn->IsDown();
					const bool up = btn->IsUp();
					if (!down && !up) {
						continue;  // held / repeating
					}

					if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard && (code == 0x2A || code == 0x36)) {
						g_shiftDown = down;  // left / right shift
					}
					if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard && (code == 0x1D || code == 0x9D)) {
						g_ctrlDown = down;  // left / right ctrl
					}
					// Activate on a hung picture: find it ourselves, so it works past the game's short
					// activation range and when the frame sits in the wall (its collision can't be hit).
					if (down && !g_open && !Wall::IsPlacing()) {
						auto* ue = RE::UserEvents::GetSingleton();
						auto* ui = RE::UI::GetSingleton();
						if (ue && btn->QUserEvent() == ue->activate && ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
							spdlog::info("Activate pressed");
							Wall::RequestExamine();
						}
					}
					if (Wall::IsPlacing()) {
						if (!down) {
							continue;
						}
						if (e->GetDevice() == RE::INPUT_DEVICE::kMouse) {
							using MouseKey = RE::BSWin32MouseDevice::Key;
							if (code == 0) {
								Wall::RequestPlace();  // left click
							} else if (code == 1) {
								Wall::CancelPlacement();  // right click
							} else if (code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) {
								const int dir = code == MouseKey::kWheelUp ? +1 : -1;
								if (g_shiftDown) {
									Wall::CycleStyle(dir);
								} else {
									Wall::AdjustScale(dir);
								}
							}
						} else if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
							if (code == kDIK_Escape || code == cfg.toggleScan) {
								Wall::CancelPlacement();
							} else if (code == kDIK_Up) {
								Wall::AdjustOffset(g_shiftDown ? +8 : +1);  // away from the wall (Shift: 2 units)
							} else if (code == kDIK_Down) {
								Wall::AdjustOffset(g_shiftDown ? -8 : -1);  // closer / into the wall (Shift: 2 units)
							}
						}
						continue;
					}

					if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
						if (down && code == cfg.toggleScan) {
							if (g_wantOpen) {
								g_wantOpen = false;
							} else {
								auto* ui = RE::UI::GetSingleton();
								if (!(ui && ui->GameIsPaused())) {
									// Looking at a hung picture: open its menu; otherwise the gallery.
									Wall::RequestExamine([] {
										g_uiMode = UiMode::Gallery;
										g_wantOpen = true;
									});
								}
							}
							continue;
						}
						if (!g_open) {
							continue;
						}
						ImGuiKey key = ImGuiKey_None;
						switch (code) {
						case kDIK_Escape:
							key = ImGuiKey_Escape;
							break;
						case kDIK_Left:
							key = ImGuiKey_LeftArrow;
							break;
						case kDIK_Right:
							key = ImGuiKey_RightArrow;
							break;
						default:
							break;
						}
						if (key != ImGuiKey_None) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = key;
							m.down = down;
							PushInput(m);
						}
					} else if (e->GetDevice() == RE::INPUT_DEVICE::kMouse && g_open) {
						using MouseKey = RE::BSWin32MouseDevice::Key;
						if (code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) {
							if (down) {
								InputMsg m{ g_ctrlDown ? InputMsg::Kind::UiScale : InputMsg::Kind::Wheel };
								m.x = code == MouseKey::kWheelUp ? 1.0f : -1.0f;
								m.y = code == MouseKey::kWheelUp ? 1.0f : -1.0f;
								PushInput(m);
							}
						} else if (code <= 4) {  // left, right, middle, X1, X2
							InputMsg m{ InputMsg::Kind::MouseButton };
							m.button = static_cast<int>(code);
							m.down = down;
							PushInput(m);
						}
					}
				}
		}

		void SafeHandleInput(RE::InputEvent* const* a_event)
		{
			__try {
				HandleInput(a_event);
			} __except (SehFilter(GetExceptionInformation(), "HandleInput")) {
				g_inputDisabled = true;
			}
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!g_inputDisabled) {
					SafeHandleInput(a_event);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void DrainInput()
		{
			std::vector<InputMsg> msgs;
			{
				std::lock_guard lk(g_inputMtx);
				msgs.swap(g_input);
			}
			auto& io = ImGui::GetIO();
			for (const auto& m : msgs) {
				switch (m.kind) {
				case InputMsg::Kind::MouseMove:
					g_mouseX = std::clamp(g_mouseX + m.x * cfg.mouseSpeed, 0.0f, io.DisplaySize.x - 1.0f);
					g_mouseY = std::clamp(g_mouseY + m.y * cfg.mouseSpeed, 0.0f, io.DisplaySize.y - 1.0f);
					io.AddMousePosEvent(g_mouseX, g_mouseY);
					break;
				case InputMsg::Kind::MouseButton:
					io.AddMouseButtonEvent(m.button, m.down);
					break;
				case InputMsg::Kind::UiScale:
					RequestUiScale(cfg.uiScale + 0.1f * m.x);
					break;
				case InputMsg::Kind::Wheel:
					io.AddMouseWheelEvent(0.0f, m.y);
					break;
				case InputMsg::Kind::Key:
					io.AddKeyEvent(m.key, m.down);
					break;
				}
			}
		}

		// Builds fonts + style for the current screen and interface size. Can run again between frames.
		void ApplyTheme()
		{
			// Georgia ships with Windows; falls back to ImGui's built-in font if missing.
			char windir[MAX_PATH]{};
			::GetWindowsDirectoryA(windir, MAX_PATH);
			const std::string fonts = std::string(windir) + "\\Fonts\\";
			const std::string regular = fonts + "georgia.ttf", bold = fonts + "georgiab.ttf", italic = fonts + "georgiai.ttf";
			const float       scale = (std::max)(1.0f, g_displayH / 1080.0f) * cfg.uiScale;
			if (g_imguiReady) {
				ImGui_ImplDX11_InvalidateDeviceObjects();  // font texture is recreated by the next NewFrame
			}
			Theme::Setup(scale, cfg.fontSize, { regular.c_str(), bold.c_str(), italic.c_str() });
			ImGui::GetIO().MouseDrawCursor = g_open;
			spdlog::info("UI: scale {:.2f}, font {} ({})", scale, cfg.fontSize, Theme::Body() && Theme::Body()->ConfigData ? "Georgia" : "default");
		}

		bool InitImGui()
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

			ApplyTheme();
			if (!ImGui_ImplDX11_Init(g_dev, g_ctx)) {
				return false;
			}
			io.MouseDrawCursor = g_open;
			return true;
		}

		// ---------------------------------------------------------------- UI
		void OpenViewer(int i)
		{
			viewing = i;
			ReleaseFull();
			Submit({ g_gen.load(), i, items[i].path, kFullMaxDim, true }, true);
		}

		void Step(int delta)
		{
			const int n = static_cast<int>(items.size());
			if (n > 0) {
				OpenViewer(((viewing + delta) % n + n) % n);
			}
		}

		void DrawFrameMenu()
		{
			auto&          f = g_frameMenu;
			int            pct = static_cast<int>(std::lround(f.scale * 100.0f));
			Ui::FrameModel fm;
			fm.style = Frames::kStyles[f.style];
			fm.shape = Frames::kShapes[f.shape].name;
			fm.picture = f.picture.c_str();
			fm.sizePct = &pct;
			fm.canMove = !f.picture.empty();
			switch (Ui::DrawFrameMenu(fm)) {
			case Ui::FrameAction::Close:
				ApplyOpen(false);
				break;
			case Ui::FrameAction::SizeChanged:
				f.scale = pct / 100.0f;
				Wall::SetFrameScale(f.ref, f.scale);
				break;
			case Ui::FrameAction::Closer:
				Wall::NudgeFrame(f.ref, -0.5f);
				break;
			case Ui::FrameAction::Further:
				Wall::NudgeFrame(f.ref, +0.5f);
				break;
			case Ui::FrameAction::ChangePicture:
				g_replaceTarget = f.ref;
				g_uiMode = UiMode::Gallery;
				g_needScan = true;
				viewing = -1;
				break;
			case Ui::FrameAction::Move:
				Wall::BeginPlacement(f.picturePath, f.shape, f.style, f.ref, f.scale);
				ApplyOpen(false);
				break;
			case Ui::FrameAction::TakeDown:
				Wall::RemoveFrame(f.ref);
				ApplyOpen(false);
				break;
			default:
				break;
			}
		}

		void DrawUI()
		{
			if (g_uiMode == UiMode::FrameMenu) {
				DrawFrameMenu();
				return;
			}
			const auto       folder = ToUtf8(cfg.folder);
			Ui::GalleryModel gm;
			gm.folder = folder.c_str();
			gm.framesOnWalls = Wall::FrameCount();
			gm.replaceMode = g_replaceTarget.has_value();
			gm.notice = g_virtualFileHint ?
			                "Some pictures can't be opened until the game restarts - this folder is inside Mod Organizer's virtual Data folder. "
			                "Leave sFolder empty in FolderGallery.ini to use Documents\\My Games\\Skyrim Special Edition\\FolderGallery\\Pictures, where new pictures work right away." :
			                nullptr;
			gm.thumbSize = &cfg.thumbSize;
			static int uiPct = 100;
			if (g_pendingUiScale < 0.0f && !ImGui::IsAnyItemActive()) {
				uiPct = static_cast<int>(std::lround(cfg.uiScale * 100.0f));
			}
			gm.uiScalePct = &uiPct;
			gm.pictures.reserve(items.size());
			for (const auto& it : items) {
				Ui::Picture p;
				p.name = it.name.c_str();
				p.thumb = it.srv ? reinterpret_cast<ImTextureID>(it.srv) : 0;
				p.thumbW = it.w;
				p.thumbH = it.h;
				p.origW = it.origW;
				p.origH = it.origH;
				p.failed = it.state == State::Failed;
				p.error = it.error.c_str();
				gm.pictures.push_back(p);
			}
			gm.requestThumb = [](int i) {
				Item& it = items[i];
				if (it.state == State::None) {
					it.state = State::Queued;
					Submit({ g_gen.load(), i, it.path, kThumbMaxDim, false });
				}
			};
			gm.viewing = viewing;
			gm.full = fullSrv ? reinterpret_cast<ImTextureID>(fullSrv) : 0;
			gm.fullW = fullW;
			gm.fullH = fullH;
			gm.fullFailed = fullFailed;
			static std::vector<const char*> shapeNames = [] {
				std::vector<const char*> v;
				for (const auto& sh : Frames::kShapes) {
					v.push_back(sh.name);
				}
				return v;
			}();
			gm.styles = Frames::kStyles;
			gm.styleCount = Frames::kStyleCount;
			gm.shapes = shapeNames.data();
			gm.shapeCount = Frames::kShapeCount;
			gm.hangStyle = &g_hangStyle;
			gm.hangShape = &g_hangShape;
			gm.bestShape = [](int w, int h) { return Wall::AutoShape(w, h); };
			gm.textureSize = [](int shape) { return Wall::TextureSize(shape); };

			const auto r = Ui::DrawGallery(gm);
			switch (r.action) {
			case Ui::GalleryAction::Close:
				ApplyOpen(false);
				break;
			case Ui::GalleryAction::Refresh:
				g_needScan = true;
				break;
			case Ui::GalleryAction::Open:
				OpenViewer(r.index);
				break;
			case Ui::GalleryAction::Back:
				viewing = -1;
				ReleaseFull();
				break;
			case Ui::GalleryAction::Prev:
				Step(-1);
				break;
			case Ui::GalleryAction::Next:
				Step(+1);
				break;
			case Ui::GalleryAction::Hang:
				Wall::BeginPlacement(items[r.index].path, g_hangShape, g_hangStyle);
				ApplyOpen(false);
				break;
			case Ui::GalleryAction::UiScale:
				RequestUiScale(uiPct / 100.0f);
				break;
			case Ui::GalleryAction::UseForFrame:
				Wall::ChangePicture(*g_replaceTarget, items[r.index].path);
				ApplyOpen(false);
				break;
			default:
				break;
			}
		}
	}  // namespace

	void LoadSettings()
	{
		std::error_code ec;
		const auto      ini = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\FolderGallery.ini"), ec).string();
		g_iniPath = ini;

		char buf[MAX_PATH]{};
		::GetPrivateProfileStringA("General", "sFolder", "", buf, MAX_PATH, ini.c_str());
		if (buf[0]) {
			cfg.folder = fs::absolute(fs::path(buf), ec);
		} else {
			// Outside Data on purpose: Mod Organizer's virtual Data folder can't open files added while the game runs.
			PWSTR docs = nullptr;
			if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
				cfg.folder = fs::path(docs) / L"My Games" / L"Skyrim Special Edition" / L"FolderGallery" / L"Pictures";
				::CoTaskMemFree(docs);
			} else {
				cfg.folder = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\FolderGallery\\Pictures"), ec);
			}
		}
		cfg.toggleKey = static_cast<int>(::GetPrivateProfileIntA("General", "iToggleKey", VK_F8, ini.c_str()));
		cfg.toggleScan = VirtualKeyToDIK(cfg.toggleKey);
		if (cfg.toggleScan == 0) {
			spdlog::warn("iToggleKey={} is not a usable key, falling back to F8", cfg.toggleKey);
			cfg.toggleKey = VK_F8;
			cfg.toggleScan = 0x42;
		}
		cfg.recursive = ::GetPrivateProfileIntA("General", "bRecursive", 0, ini.c_str()) != 0;
		cfg.thumbSize = std::clamp(static_cast<int>(::GetPrivateProfileIntA("General", "iThumbnailSize", 200, ini.c_str())), 100, kThumbMaxDim);
		::GetPrivateProfileStringA("General", "fFontSize", "18", buf, MAX_PATH, ini.c_str());
		cfg.fontSize = std::clamp(static_cast<float>(std::atof(buf)), 8.0f, 64.0f);
		::GetPrivateProfileStringA("General", "fUIScale", "1.0", buf, MAX_PATH, ini.c_str());
		cfg.uiScale = std::clamp(static_cast<float>(std::atof(buf)), 0.6f, 2.0f);
		::GetPrivateProfileStringA("General", "fMouseSpeed", "1.0", buf, MAX_PATH, ini.c_str());
		cfg.mouseSpeed = std::clamp(static_cast<float>(std::atof(buf)), 0.1f, 10.0f);
		auto& wall = Wall::GetSettings();
		wall.resolution = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iPictureResolution", 1024, ini.c_str())), 256, 4096);
		wall.defaultStyle = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iDefaultFrameStyle", 2, ini.c_str())), 0, Frames::kStyleCount - 1);
		g_hangStyle = wall.defaultStyle;
		// fPlaceReach replaces the old fMaxPlaceDistance (default went from 400 to 2000)
		::GetPrivateProfileStringA("Frames", "fPlaceReach", "2000", buf, MAX_PATH, ini.c_str());
		wall.maxDistance = std::clamp(static_cast<float>(std::atof(buf)), 100.0f, 10000.0f);
		wall.cleanUp = ::GetPrivateProfileIntA("Frames", "bCleanUpTextures", 1, ini.c_str()) != 0;
		::GetPrivateProfileStringA("Frames", "fExamineReach", "600", buf, MAX_PATH, ini.c_str());
		wall.examineReach = std::clamp(static_cast<float>(std::atof(buf)), 100.0f, 5000.0f);
		spdlog::info("Frames: resolution {}  default style {}  reach {}", wall.resolution, Frames::kStyles[wall.defaultStyle], wall.maxDistance);
		dbg.hookPresent = ::GetPrivateProfileIntA("Debug", "bHookPresent", 1, ini.c_str()) != 0;
		dbg.inputSink = ::GetPrivateProfileIntA("Debug", "bInputSink", 1, ini.c_str()) != 0;
		std::error_code ec2;
		spdlog::info("INI: {} ({})", ini, fs::exists(ini, ec2) ? "found" : "NOT FOUND - using defaults");
		spdlog::info("Debug: bHookPresent={} bInputSink={}", dbg.hookPresent, dbg.inputSink);
		spdlog::info("Folder: {}  key: {} (scan 0x{:02X})  recursive: {}  mouse speed: {}",
			ToUtf8(cfg.folder), cfg.toggleKey, cfg.toggleScan, cfg.recursive, cfg.mouseSpeed);
	}

	const DebugSwitches& GetDebugSwitches()
	{
		return dbg;
	}

	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where)
	{
		const auto* rec = a_info ? a_info->ExceptionRecord : nullptr;
		const auto  addr = rec ? reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress) : 0;
		HMODULE     mod = nullptr;
		wchar_t     modPath[MAX_PATH]{};
		if (addr && ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(addr), &mod)) {
			::GetModuleFileNameW(mod, modPath, MAX_PATH);
		}
		const auto modName = ToUtf8(fs::path(modPath).filename());
		const auto offset = mod ? addr - reinterpret_cast<std::uintptr_t>(mod) : addr;
		std::string detail;
		if (rec && rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
			detail = fmt::format(" ({} address 0x{:X})", rec->ExceptionInformation[0] ? "writing" : "reading", rec->ExceptionInformation[1]);
		}
		spdlog::critical("CRASH CAUGHT in {}: exception 0x{:08X} at {}+0x{:X}{} - this part is now switched off so the game keeps running",
			a_where, rec ? static_cast<std::uint32_t>(rec->ExceptionCode) : 0u, modName.empty() ? "?" : modName, offset, detail);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	bool InstallInputSink()
	{
		static InputSink sink;
		auto*            mgr = RE::BSInputDeviceManager::GetSingleton();
		spdlog::info("  BSInputDeviceManager = {}", static_cast<void*>(mgr));
		if (!mgr) {
			return false;
		}
		mgr->AddEventSink<RE::InputEvent*>(&sink);
		return true;
	}

	void OnPresent(IDXGISwapChain* sc)
	{
		if (!g_sc) {
			spdlog::info("OnPresent: attaching to swap chain {}", static_cast<void*>(sc));
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) {
				return;
			}
			if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))) || !g_dev) {
				return;
			}
			g_dev->GetImmediateContext(&g_ctx);
			g_sc = sc;
			if (!g_workerStarted) {
				g_workerStarted = true;
				std::thread(WorkerMain).detach();
			}
			g_displayH = static_cast<float>(desc.BufferDesc.Height);
			spdlog::info("Attached to the game's swap chain ({}x{})", desc.BufferDesc.Width, desc.BufferDesc.Height);
		}
		if (sc != g_sc) {
			return;
		}

		ApplyPlacementControls(Wall::IsPlacing());
		Wall::Tick();
		if (auto activated = Wall::TakeActivatedFrame(); activated && !g_open && !g_wantOpen && !Wall::IsPlacing()) {
			g_frameMenu = *activated;
			g_uiMode = UiMode::FrameMenu;
			g_wantOpen = true;
		}
		if (g_wantOpen != g_open) {
			ApplyOpen(g_wantOpen);
		}
		if (!g_open || g_imguiFailed) {
			if (!g_open) {
				std::lock_guard lk(g_inputMtx);
				g_input.clear();
			}
			return;
		}

		if (!g_imguiReady) {
			if (!InitImGui()) {
				g_imguiFailed = true;
				spdlog::error("ImGui failed to initialise");
				ApplyOpen(false);
				return;
			}
			g_imguiReady = true;
		}

		ID3D11Texture2D* back = nullptr;
		if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) {
			return;
		}
		D3D11_TEXTURE2D_DESC bd{};
		back->GetDesc(&bd);

		auto& io = ImGui::GetIO();
		const bool firstFrameOpen = io.DisplaySize.x <= 0.0f || g_needScan;
		io.DisplaySize = ImVec2(static_cast<float>(bd.Width), static_cast<float>(bd.Height));
		const auto now = std::chrono::steady_clock::now();
		float      dt = g_lastFrame.time_since_epoch().count() ? std::chrono::duration<float>(now - g_lastFrame).count() : 1.0f / 60.0f;
		g_lastFrame = now;
		io.DeltaTime = std::clamp(dt, 1.0f / 1000.0f, 0.5f);

		if (firstFrameOpen) {  // start the cursor in the middle of the screen
			g_mouseX = io.DisplaySize.x * 0.5f;
			g_mouseY = io.DisplaySize.y * 0.5f;
			io.AddMousePosEvent(g_mouseX, g_mouseY);
		}
		if (g_needScan) {
			g_needScan = false;
			Scan();
		}
		DrainInput();
		UploadReady();

		if (g_pendingUiScale > 0.0f) {
			if (std::fabs(g_pendingUiScale - cfg.uiScale) > 0.001f) {
				cfg.uiScale = g_pendingUiScale;
				ApplyTheme();
				::WritePrivateProfileStringA("General", "fUIScale", fmt::format("{:.2f}", cfg.uiScale).c_str(), g_iniPath.c_str());
				spdlog::info("Interface size {:.0f}%", cfg.uiScale * 100.0f);
			}
			g_pendingUiScale = -1.0f;
		}
		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
		DrawUI();
		ImGui::Render();

		ID3D11RenderTargetView* rtv = nullptr;
		g_dev->CreateRenderTargetView(back, nullptr, &rtv);
		back->Release();
		if (rtv) {
			g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			rtv->Release();
		}
	}
}
