#include "Gallery.h"

#include "Frames.h"
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
		constexpr int kThumbMaxDim = 320;
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
		};

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
			int                       w = 0, h = 0, origW = 0, origH = 0;
			std::vector<std::uint8_t> px;
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
			FILE* f = nullptr;
			if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
				return false;
			}
			int      channels = 0;
			stbi_uc* px = stbi_load_from_file(f, &r.origW, &r.origH, &channels, 4);
			fclose(f);
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
				} catch (...) {
					r.ok = false;
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
								Wall::AdjustOffset(+1);  // away from the wall
							} else if (code == kDIK_Down) {
								Wall::AdjustOffset(-1);  // closer / into the wall
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
								InputMsg m{ InputMsg::Kind::Wheel };
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
				case InputMsg::Kind::Wheel:
					io.AddMouseWheelEvent(0.0f, m.y);
					break;
				case InputMsg::Kind::Key:
					io.AddKeyEvent(m.key, m.down);
					break;
				}
			}
		}

		bool InitImGui()
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			ImFontConfig fc;
			fc.SizePixels = cfg.fontSize;
			io.Fonts->AddFontDefault(&fc);
			ImGui::StyleColorsDark();
			if (!ImGui_ImplDX11_Init(g_dev, g_ctx)) {
				return false;
			}
			io.MouseDrawCursor = g_open;
			return true;
		}

		// ---------------------------------------------------------------- UI
		ImVec2 Fit(float w, float h, float maxW, float maxH)
		{
			if (w <= 0 || h <= 0) {
				return { maxW, maxH };
			}
			const float s = (std::min)(maxW / w, maxH / h);
			return { w * s, h * s };
		}

		void OpenViewer(int i)
		{
			viewing = i;
			ReleaseFull();
			Submit({ g_gen.load(), i, items[i].path, kFullMaxDim, true }, true);
		}

		void DrawGrid()
		{
			const float cell = static_cast<float>(cfg.thumbSize);
			const auto& style = ImGui::GetStyle();
			const float labelH = ImGui::GetTextLineHeight() + 2.0f;
			ImGui::BeginChild("grid");
			const float  availW = ImGui::GetContentRegionAvail().x;
			const int    cols = (std::max)(1, static_cast<int>(availW / (cell + style.ItemSpacing.x)));
			const ImVec2 cellSize(cell, cell + labelH);

			for (int i = 0; i < static_cast<int>(items.size()); ++i) {
				if (i % cols != 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(i);
				ImGui::BeginGroup();
				if (ImGui::IsRectVisible(cellSize)) {
					Item& it = items[i];
					if (it.state == State::None) {
						it.state = State::Queued;
						Submit({ g_gen.load(), i, it.path, kThumbMaxDim, false });
					}
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const bool   clicked = ImGui::InvisibleButton("##pic", ImVec2(cell, cell));
					const bool   hovered = ImGui::IsItemHovered();
					auto*        dl = ImGui::GetWindowDrawList();
					dl->AddRectFilled(p, p + ImVec2(cell, cell), IM_COL32(25, 25, 25, 255));
					if (it.srv) {
						const ImVec2 sz = Fit(static_cast<float>(it.w), static_cast<float>(it.h), cell, cell);
						const ImVec2 a = p + (ImVec2(cell, cell) - sz) * 0.5f;
						dl->AddImage(reinterpret_cast<ImTextureID>(it.srv), a, a + sz);
					} else {
						dl->AddText(p + ImVec2(8, 8), IM_COL32(160, 160, 160, 255), it.state == State::Failed ? "Can't read" : "Loading...");
					}
					if (hovered) {
						dl->AddRect(p, p + ImVec2(cell, cell), IM_COL32(255, 200, 80, 255), 0.0f, 0, 2.0f);
						ImGui::SetTooltip("%s\n%d x %d", it.name.c_str(), it.origW, it.origH);
					}
					dl->PushClipRect(p + ImVec2(0, cell), p + ImVec2(cell, cell + labelH), true);
					dl->AddText(p + ImVec2(2, cell + 1), IM_COL32(220, 220, 220, 255), it.name.c_str());
					dl->PopClipRect();
					ImGui::Dummy(ImVec2(cell, labelH));
					if (clicked) {
						OpenViewer(i);
					}
				} else {
					ImGui::Dummy(cellSize);
				}
				ImGui::EndGroup();
				ImGui::PopID();
			}
			if (items.empty()) {
				ImGui::TextWrapped("No pictures found. Put .png, .jpg, .bmp or .tga files in:\n%s", ToUtf8(cfg.folder).c_str());
			}
			ImGui::EndChild();
		}

		void Step(int delta)
		{
			const int n = static_cast<int>(items.size());
			if (n > 0) {
				OpenViewer(((viewing + delta) % n + n) % n);
			}
		}

		void DrawViewer()
		{
			if (ImGui::Button("< Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				viewing = -1;
				ReleaseFull();
				return;
			}
			ImGui::SameLine();
			if (ImGui::Button("Prev") || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
				Step(-1);
				return;
			}
			ImGui::SameLine();
			if (ImGui::Button("Next") || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
				Step(+1);
				return;
			}
			const Item& it = items[viewing];
			ImGui::SameLine();
			if (g_replaceTarget) {
				if (ImGui::Button("Use for this frame")) {
					Wall::ChangePicture(*g_replaceTarget, it.path);
					ApplyOpen(false);
					return;
				}
			} else if (ImGui::Button("Hang on wall...")) {
				g_hangShape = Wall::AutoShape(it.origW, it.origH);
				g_hangStyle = std::clamp(Wall::GetSettings().defaultStyle, 0, Frames::kStyleCount - 1);
				ImGui::OpenPopup("Hang on wall");
			}
			ImGui::SameLine();
			ImGui::Text("%s   %d x %d   (%d / %d)", it.name.c_str(), it.origW, it.origH, viewing + 1, static_cast<int>(items.size()));

			bool startHanging = false;
			ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Hang on wall", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted("Frame");
				ImGui::SetNextItemWidth(260.0f);
				ImGui::Combo("##style", &g_hangStyle, Frames::kStyles, Frames::kStyleCount);
				ImGui::TextUnformatted("Shape");
				ImGui::SetNextItemWidth(260.0f);
				if (ImGui::BeginCombo("##shape", Frames::kShapes[g_hangShape].name)) {
					for (int i = 0; i < Frames::kShapeCount; ++i) {
						const auto& sh = Frames::kShapes[i];
						const auto  label = fmt::format("{}{}", sh.name, i == Wall::AutoShape(it.origW, it.origH) ? "  (best fit)" : "");
						if (ImGui::Selectable(label.c_str(), i == g_hangShape)) {
							g_hangShape = i;
						}
					}
					ImGui::EndCombo();
				}
				const auto [tw, th] = Wall::TextureSize(g_hangShape);
				ImGui::TextDisabled("The picture is cropped to fill the frame (%d x %d texture).", tw, th);
				ImGui::Spacing();
				if (ImGui::Button("Hang it", ImVec2(120, 0))) {
					startHanging = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (startHanging) {
				Wall::BeginPlacement(it.path, g_hangShape, g_hangStyle);
				ApplyOpen(false);
				return;
			}

			const ImVec2              avail = ImGui::GetContentRegionAvail();
			ID3D11ShaderResourceView* tex = fullSrv ? fullSrv : it.srv;
			if (tex) {
				const int    tw = fullSrv ? fullW : it.w;
				const int    th = fullSrv ? fullH : it.h;
				const ImVec2 sz = Fit(static_cast<float>(tw), static_cast<float>(th), avail.x, avail.y);
				ImGui::SetCursorPos(ImGui::GetCursorPos() + (avail - sz) * 0.5f);
				ImGui::Image(reinterpret_cast<ImTextureID>(tex), sz);
			} else if (fullFailed) {
				ImGui::TextUnformatted("This picture could not be read.");
			} else {
				ImGui::TextUnformatted("Loading...");
			}
		}

		void DrawFrameMenu()
		{
			auto& io = ImGui::GetIO();
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ApplyOpen(false);
				return;
			}
			ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			bool open = true;
			if (ImGui::Begin("Framed picture", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
				const auto& f = g_frameMenu;
				ImGui::Text("%s frame, %s", Frames::kStyles[f.style], Frames::kShapes[f.shape].name);
				ImGui::Text("Picture: %s", f.picture.empty() ? "(none - placed some other way)" : f.picture.c_str());
				ImGui::Spacing();
				int pct = static_cast<int>(std::lround(g_frameMenu.scale * 100.0f));
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::SliderInt("Size", &pct, 30, 300, "%d%%")) {
					g_frameMenu.scale = pct / 100.0f;
					Wall::SetFrameScale(f.ref, g_frameMenu.scale);
				}
				ImGui::TextUnformatted("Distance from wall");
				if (ImGui::Button("Closer", ImVec2(97, 0))) {
					Wall::NudgeFrame(f.ref, -0.5f);
				}
				ImGui::SameLine(0.0f, 6.0f);
				if (ImGui::Button("Further", ImVec2(97, 0))) {
					Wall::NudgeFrame(f.ref, +0.5f);
				}
				const ImVec2 bs(200, 0);
				if (ImGui::Button("Change picture...", bs)) {
					g_replaceTarget = f.ref;
					g_uiMode = UiMode::Gallery;
					g_needScan = true;
					viewing = -1;
				}
				ImGui::BeginDisabled(f.picture.empty());
				if (ImGui::Button("Move", bs)) {
					Wall::BeginPlacement(f.picturePath, f.shape, f.style, f.ref, f.scale);
					open = false;
				}
				ImGui::EndDisabled();
				if (ImGui::Button("Take down", bs)) {
					Wall::RemoveFrame(f.ref);
					open = false;
				}
				if (ImGui::Button("Close", bs)) {
					open = false;
				}
			}
			ImGui::End();
			if (!open) {
				ApplyOpen(false);
			}
		}

		void DrawUI()
		{
			auto& io = ImGui::GetIO();
			if (g_uiMode == UiMode::FrameMenu) {
				DrawFrameMenu();
				return;
			}
			if (viewing < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ApplyOpen(false);
				return;
			}
			ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
			bool open = true;
			if (ImGui::Begin("Folder Gallery", &open)) {
				if (g_replaceTarget) {
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Choosing a new picture for the frame - open a picture and click \"Use for this frame\".");
				}
				if (viewing < 0) {
					if (ImGui::Button("Refresh")) {
						g_needScan = true;
					}
					ImGui::SameLine();
					ImGui::SetNextItemWidth(200.0f);
					ImGui::SliderInt("Size", &cfg.thumbSize, 100, kThumbMaxDim);
					ImGui::SameLine();
					ImGui::Text("%d picture(s) in %s", static_cast<int>(items.size()), ToUtf8(cfg.folder).c_str());
					DrawGrid();
				} else {
					DrawViewer();
				}
			}
			ImGui::End();
			if (!open) {
				ApplyOpen(false);
			}
		}
	}  // namespace

	void LoadSettings()
	{
		std::error_code ec;
		const auto      ini = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\FolderGallery.ini"), ec).string();

		char buf[MAX_PATH]{};
		::GetPrivateProfileStringA("General", "sFolder", "Data\\SKSE\\Plugins\\FolderGallery\\Pictures", buf, MAX_PATH, ini.c_str());
		cfg.folder = fs::absolute(fs::path(buf), ec);
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
		::GetPrivateProfileStringA("General", "fMouseSpeed", "1.0", buf, MAX_PATH, ini.c_str());
		cfg.mouseSpeed = std::clamp(static_cast<float>(std::atof(buf)), 0.1f, 10.0f);
		auto& wall = Wall::GetSettings();
		wall.resolution = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iPictureResolution", 1024, ini.c_str())), 256, 4096);
		wall.defaultStyle = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iDefaultFrameStyle", 2, ini.c_str())), 0, Frames::kStyleCount - 1);
		// fPlaceReach replaces the old fMaxPlaceDistance (default went from 400 to 2000)
		::GetPrivateProfileStringA("Frames", "fPlaceReach", "2000", buf, MAX_PATH, ini.c_str());
		wall.maxDistance = std::clamp(static_cast<float>(std::atof(buf)), 100.0f, 10000.0f);
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
