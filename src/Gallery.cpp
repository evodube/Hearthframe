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
			bool          placementInfo = true;  // size / distance / frame line at the bottom while hanging
			bool          thumbFill = true;      // thumbnails fill their cards (cropped) instead of fitting inside
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
			bool     preview = false;  // frame menu picture (not part of the list)
		};

		struct Result
		{
			int                       gen = 0;
			int                       index = 0;
			bool                      full = false;
			bool                      preview = false;
			fs::path                  path;
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
				Key,
				Char      // typed character: button = UTF-16 code unit
			} kind;
			float    x = 0.0f, y = 0.0f;
			int      button = 0;
			ImGuiKey key = ImGuiKey_None;
			bool     down = false;
		};
		std::mutex            g_inputMtx;
		std::vector<InputMsg> g_input;

		std::vector<Item>         items;
		std::vector<int>          visible;  // indices into items that match the search, in order
		int                       viewing = -1;  // index into items
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
		int                                g_changePending = 0;   // frames left to wait for a Change frame swap
		std::optional<RE::ObjectRefHandle> g_replaceTarget;       // set while choosing a new picture for a frame
		int                                g_hangShape = 0;
		int                                g_hangStyle = 7;
		bool                               g_placementControls = false;
		std::atomic<bool>                  g_shiftDown{ false };
		std::atomic<bool>                  g_ctrlDown{ false };
		float                              g_pendingUiScale = -1.0f;  // applied between frames (fonts are rebuilt)
		std::string                        g_iniPath;
		char                               g_search[128]{};
		char                               g_renameBuf[256]{};
		std::string                        g_renameError;
		bool                               g_renameDone = false;
		ID3D11ShaderResourceView*          g_previewSrv = nullptr;  // frame menu picture
		fs::path                           g_previewPath;
		int                                g_previewW = 0, g_previewH = 0;
		bool                               g_previewLoading = false, g_previewMissing = false;
		std::atomic<bool>                  g_altDown{ false };

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

		fs::path FromUtf8Path(const std::string& s)
		{
			return fs::path(std::u8string(s.begin(), s.end()));
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
				if (job.gen != g_gen.load() && !job.preview) {
					continue;  // the list was refreshed while this was waiting
				}
				Result r;
				r.gen = job.gen;
				r.index = job.index;
				r.full = job.full;
				r.preview = job.preview;
				r.path = job.path;
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
				if (r.preview) {
					if (r.path == g_previewPath) {
						if (g_previewSrv) {
							g_previewSrv->Release();
							g_previewSrv = nullptr;
						}
						if (r.ok) {
							g_previewSrv = MakeSRV(r.px.data(), r.w, r.h);
							g_previewW = r.w;
							g_previewH = r.h;
						}
						g_previewLoading = false;
						g_previewMissing = !g_previewSrv;
					}
					continue;
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
				if (g_previewSrv) {
					g_previewSrv->Release();
					g_previewSrv = nullptr;
				}
				g_previewPath.clear();
				g_previewLoading = g_previewMissing = false;
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

		// Character a key types with the current Windows keyboard layout (0 if none). Input thread.
		wchar_t KeyToChar(std::uint32_t a_dik, bool a_shift, bool a_altGr)
		{
			const UINT sc = (a_dik & 0x7F) | ((a_dik & 0x80) ? 0xE000u : 0u);
			HKL        layout = ::GetKeyboardLayout(::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr));
			const UINT vk = ::MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK_EX, layout);
			if (vk == 0) {
				return 0;
			}
			BYTE state[256]{};
			if (a_shift) {
				state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
			}
			if (::GetKeyState(VK_CAPITAL) & 1) {
				state[VK_CAPITAL] = 0x01;
			}
			if (::GetKeyState(VK_NUMLOCK) & 1) {
				state[VK_NUMLOCK] = 0x01;
			}
			if (a_altGr) {
				state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_MENU] = state[VK_RMENU] = 0x80;
			}
			wchar_t buf[8]{};
			const int n = ::ToUnicodeEx(vk, sc, state, buf, 8, 0x4 /* don't change the keyboard state */, layout);
			return (n == 1 && buf[0] >= 0x20 && buf[0] != 0x7F) ? buf[0] : 0;
		}

		ImGuiKey KeyToImGui(std::uint32_t a_dik)
		{
			switch (a_dik) {
			case kDIK_Escape:
				return ImGuiKey_Escape;
			case kDIK_Left:
				return ImGuiKey_LeftArrow;
			case kDIK_Right:
				return ImGuiKey_RightArrow;
			case kDIK_Up:
				return ImGuiKey_UpArrow;
			case kDIK_Down:
				return ImGuiKey_DownArrow;
			case 0x0E:
				return ImGuiKey_Backspace;
			case 0xD3:
				return ImGuiKey_Delete;
			case 0x1C:
				return ImGuiKey_Enter;
			case 0x9C:
				return ImGuiKey_KeypadEnter;
			case 0xC7:
				return ImGuiKey_Home;
			case 0xCF:
				return ImGuiKey_End;
			case 0x0F:
				return ImGuiKey_Tab;
			case 0x1E:
				return ImGuiKey_A;
			case 0x2E:
				return ImGuiKey_C;
			case 0x2F:
				return ImGuiKey_V;
			case 0x2D:
				return ImGuiKey_X;
			case 0x2C:
				return ImGuiKey_Z;
			case 0x15:
				return ImGuiKey_Y;
			default:
				return ImGuiKey_None;
			}
		}

		// Holding Up/Down while hanging keeps moving the frame (first repeat after a short pause).
		float g_offsetRepeatAt = 0.0f;

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
					const bool keyboard = e->GetDevice() == RE::INPUT_DEVICE::kKeyboard;
					if (!down && !up) {
						// held: Up/Down keep moving the frame while hanging a picture
						if (keyboard && Wall::IsPlacing() && (code == kDIK_Up || code == kDIK_Down)) {
							const float held = btn->HeldDuration();
							if (held >= g_offsetRepeatAt) {
								const int dir = code == kDIK_Up ? +1 : -1;
								Wall::AdjustOffset(dir * (g_shiftDown ? 8 : 1));
								g_offsetRepeatAt = held + (g_shiftDown ? 0.10f : 0.05f);
							}
						}
						continue;
					}

					if (keyboard && (code == 0x2A || code == 0x36)) {
						g_shiftDown = down;  // left / right shift
						if (g_open) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = ImGuiMod_Shift;
							m.down = down;
							PushInput(m);
						}
					}
					if (keyboard && (code == 0x1D || code == 0x9D)) {
						g_ctrlDown = down;  // left / right ctrl
						if (g_open) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = ImGuiMod_Ctrl;
							m.down = down;
							PushInput(m);
						}
					}
					if (keyboard && (code == 0x38 || code == 0xB8)) {
						g_altDown = down;  // left / right alt (right alt = AltGr on many layouts)
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
								g_offsetRepeatAt = 0.35f;
							} else if (code == kDIK_Down) {
								Wall::AdjustOffset(g_shiftDown ? -8 : -1);  // closer / into the wall (Shift: 2 units)
								g_offsetRepeatAt = 0.35f;
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
						if (const ImGuiKey key = KeyToImGui(code); key != ImGuiKey_None) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = key;
							m.down = down;
							PushInput(m);
						}
						// typed text (search box, rename box); AltGr = Ctrl+Alt on many layouts
						const bool altGr = g_ctrlDown && g_altDown;
						if (down && (!g_ctrlDown || altGr)) {
							if (const wchar_t c = KeyToChar(code, g_shiftDown, altGr); c != 0) {
								InputMsg m{ InputMsg::Kind::Char };
								m.button = static_cast<int>(c);
								PushInput(m);
							}
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
				case InputMsg::Kind::Char:
					io.AddInputCharacterUTF16(static_cast<ImWchar16>(m.button));
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
			static bool lookChosen = false;
			if (!lookChosen) {
				lookChosen = true;
				for (int i = 0; i < Theme::StyleCount(); ++i) {
					if (std::strcmp(Theme::StyleName(i), "Ledger: Guild Card") == 0) {
						Theme::SetStyle(i);
					}
				}
				// parchment / leather textures for the look (the DX11 backend samples with WRAP, so they tile)
				Theme::SetTextureFactory([](int a_w, int a_h, const unsigned char* a_px) -> ImTextureID {
					return reinterpret_cast<ImTextureID>(MakeSRV(a_px, a_w, a_h));
				});
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

		// Previous / Next walk through the pictures that match the search.
		void Step(int delta)
		{
			const int n = static_cast<int>(visible.size());
			if (n == 0) {
				return;
			}
			const auto it = std::find(visible.begin(), visible.end(), viewing);
			const int  pos = it == visible.end() ? 0 : static_cast<int>(it - visible.begin());
			OpenViewer(visible[((pos + delta) % n + n) % n]);
		}

		std::string LowerUtf8(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return a_s;
		}

		void UpdateVisible()
		{
			visible.clear();
			const std::string needle = LowerUtf8(g_search);
			for (int i = 0; i < static_cast<int>(items.size()); ++i) {
				if (needle.empty() || LowerUtf8(items[i].name).find(needle) != std::string::npos) {
					visible.push_back(i);
				}
			}
		}

		// Renames a picture file (keeps its extension). Returns "" on success, else the reason.
		std::string RenamePicture(int a_item, std::string a_stem)
		{
			auto& it = items[a_item];
			while (!a_stem.empty() && (a_stem.back() == ' ' || a_stem.back() == '.')) {
				a_stem.pop_back();
			}
			while (!a_stem.empty() && a_stem.front() == ' ') {
				a_stem.erase(a_stem.begin());
			}
			if (a_stem.empty()) {
				return "Type a name.";
			}
			for (const char c : a_stem) {
				if (std::strchr("\\/:*?\"<>|", c) || static_cast<unsigned char>(c) < 0x20) {
					return "A name can't contain any of these: \\ / : * ? \" < > |";
				}
			}
			std::string upper = a_stem;
			std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
			static const char* reserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3" };
			for (const char* r : reserved) {
				if (upper == r) {
					return "Windows doesn't allow that name.";
				}
			}
			const fs::path from = it.path;
			const fs::path to = from.parent_path() / (FromUtf8Path(a_stem) += from.extension());
			if (to == from) {
				return {};  // unchanged
			}
			std::error_code ec;
			const bool sameFileOtherCase = _wcsicmp(to.c_str(), from.c_str()) == 0;
			if (!sameFileOtherCase && fs::exists(to, ec)) {
				return "There's already a picture called " + ToUtf8(to.filename()) + ".";
			}
			if (!::MoveFileExW(from.c_str(), to.c_str(), 0)) {
				const DWORD err = ::GetLastError();
				wchar_t*    msg = nullptr;
				::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
					reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
				std::string text = msg ? ToUtf8(fs::path(msg)) : fmt::format("error {}", err);
				if (msg) {
					::LocalFree(msg);
				}
				while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
					text.pop_back();
				}
				spdlog::warn("Rename {} -> {} failed: {}", ToUtf8(from), ToUtf8(to), text);
				return "Couldn't rename it: " + text;
			}
			spdlog::info("Renamed picture {} -> {}", ToUtf8(from), ToUtf8(to));
			it.path = to;
			it.name = ToUtf8(to.filename());
			Wall::PictureRenamed(from, to);
			return {};
		}

		// Frame menu preview: (re)load when the frame's picture changes.
		void UpdatePreview(const fs::path& a_picture)
		{
			if (a_picture == g_previewPath && (g_previewSrv || g_previewLoading || g_previewMissing)) {
				return;
			}
			if (g_previewSrv) {
				g_previewSrv->Release();
				g_previewSrv = nullptr;
			}
			g_previewPath = a_picture;
			g_previewW = g_previewH = 0;
			g_previewMissing = false;
			g_previewLoading = !a_picture.empty();
			if (g_previewLoading) {
				Job job{ g_gen.load(), -1, a_picture, kThumbMaxDim, false };
				job.preview = true;
				Submit(std::move(job), true);
			}
		}

		void DrawFrameMenu()
		{
			auto&          f = g_frameMenu;
			int            pct = static_cast<int>(std::lround(f.scale * 100.0f));
			Ui::FrameModel fm;
			fm.style = Frames::StyleName(f.shape, f.style);
			fm.shape = Frames::kShapes[f.shape].name;
			fm.picture = f.picture.c_str();
			fm.sizePct = &pct;
			fm.canMove = !f.picture.empty();
			UpdatePreview(f.picturePath);
			fm.preview = g_previewSrv ? reinterpret_cast<ImTextureID>(g_previewSrv) : 0;
			fm.previewW = g_previewW;
			fm.previewH = g_previewH;
			fm.previewLoading = g_previewLoading;
			fm.previewMissing = g_previewMissing;
			fm.canvasAspect = Frames::kShapes[Frames::ClampShape(f.shape)].canvasW / Frames::kShapes[Frames::ClampShape(f.shape)].canvasH;
			fm.styles = Frames::StyleNames(f.shape);
			fm.styleCount = Frames::StyleCount(f.shape);
			fm.currentStyle = f.style;
			int chosen = f.style;
			fm.chosenStyle = &chosen;
			if (g_changePending > 0) {
				--g_changePending;  // safety timeout (frames) in case the swap never reports back
			}
			fm.changing = g_changePending > 0;
			switch (Ui::DrawFrameMenu(fm)) {
			case Ui::FrameAction::ChangeFrame:
				f.style = chosen;  // shown right away; the new frame's info replaces this when it exists
				g_changePending = 180;
				Wall::ChangeFrame(f.ref, chosen);
				break;
			case Ui::FrameAction::Close:
				ApplyOpen(false);
				break;
			case Ui::FrameAction::SizeChanged:
				f.scale = pct / 100.0f;
				Wall::SetFrameScale(f.ref, f.scale);
				break;
			case Ui::FrameAction::Closer:
				Wall::NudgeFrame(f.ref, -0.25f);  // repeats while the button is held
				break;
			case Ui::FrameAction::Further:
				Wall::NudgeFrame(f.ref, +0.25f);
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
			gm.thumbFill = &cfg.thumbFill;
			static int uiPct = 100;
			if (g_pendingUiScale < 0.0f && !ImGui::IsAnyItemActive()) {
				uiPct = static_cast<int>(std::lround(cfg.uiScale * 100.0f));
			}
			gm.uiScalePct = &uiPct;
			UpdateVisible();
			gm.search = g_search;
			gm.searchSize = static_cast<int>(sizeof g_search);
			gm.totalPictures = static_cast<int>(items.size());
			gm.renameBuf = g_renameBuf;
			gm.renameSize = static_cast<int>(sizeof g_renameBuf);
			gm.renameError = g_renameError.c_str();
			gm.renameDone = g_renameDone;
			g_renameDone = false;
			gm.pictures.reserve(visible.size());
			for (const int idx : visible) {
				const auto& it = items[idx];
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
				const int idx = visible[i];
				Item&     it = items[idx];
				if (it.state == State::None) {
					it.state = State::Queued;
					Submit({ g_gen.load(), idx, it.path, kThumbMaxDim, false });
				}
			};
			{
				const auto vit = std::find(visible.begin(), visible.end(), viewing);
				gm.viewing = (viewing >= 0 && vit != visible.end()) ? static_cast<int>(vit - visible.begin()) : -1;
				if (viewing >= 0 && gm.viewing < 0) {  // the picture being viewed no longer matches: back to the grid
					viewing = -1;
					ReleaseFull();
				}
			}
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
			gm.stylesFor = [](int shape) { return std::pair<const char* const*, int>(Frames::StyleNames(shape), Frames::StyleCount(shape)); };
			gm.shapes = shapeNames.data();
			gm.shapeCount = Frames::kShapeCount;
			gm.hangStyle = &g_hangStyle;
			gm.hangShape = &g_hangShape;
			gm.bestShape = [](int w, int h) { return Wall::AutoShape(w, h); };
			gm.textureSize = [](int shape) { return Wall::TextureSize(shape); };

			const auto r = Ui::DrawGallery(gm);
			const int  item = (r.index >= 0 && r.index < static_cast<int>(visible.size())) ? visible[r.index] : -1;
			switch (r.action) {
			case Ui::GalleryAction::ThumbSize:
				::WritePrivateProfileStringA("General", "iThumbnailSize", std::to_string(cfg.thumbSize).c_str(), g_iniPath.c_str());
				spdlog::info("Thumbnail size {} saved", cfg.thumbSize);
				break;
			case Ui::GalleryAction::ThumbFill:
				::WritePrivateProfileStringA("General", "bThumbnailFill", cfg.thumbFill ? "1" : "0", g_iniPath.c_str());
				break;
			case Ui::GalleryAction::RenameOpened:
				g_renameError.clear();
				break;
			case Ui::GalleryAction::Rename:
				if (item >= 0) {
					g_renameError = RenamePicture(item, g_renameBuf);
					g_renameDone = g_renameError.empty();
				}
				break;
			case Ui::GalleryAction::Close:
				ApplyOpen(false);
				break;
			case Ui::GalleryAction::Refresh:
				g_needScan = true;
				break;
			case Ui::GalleryAction::Open:
				if (item >= 0) {
					OpenViewer(item);
				}
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
				if (item >= 0) {
					Wall::BeginPlacement(items[item].path, g_hangShape, g_hangStyle);
					ApplyOpen(false);
				}
				break;
			case Ui::GalleryAction::UiScale:
				RequestUiScale(uiPct / 100.0f);
				break;
			case Ui::GalleryAction::UseForFrame:
				if (item >= 0 && g_replaceTarget) {
					Wall::ChangePicture(*g_replaceTarget, items[item].path);
					ApplyOpen(false);
				}
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
		cfg.thumbSize = std::clamp(static_cast<int>(::GetPrivateProfileIntA("General", "iThumbnailSize", 200, ini.c_str())), 100, 320);
		cfg.thumbFill = ::GetPrivateProfileIntA("General", "bThumbnailFill", 1, ini.c_str()) != 0;
		::GetPrivateProfileStringA("General", "fFontSize", "18", buf, MAX_PATH, ini.c_str());
		cfg.fontSize = std::clamp(static_cast<float>(std::atof(buf)), 8.0f, 64.0f);
		::GetPrivateProfileStringA("General", "fUIScale", "1.0", buf, MAX_PATH, ini.c_str());
		cfg.uiScale = std::clamp(static_cast<float>(std::atof(buf)), 0.6f, 2.0f);
		::GetPrivateProfileStringA("General", "fMouseSpeed", "1.0", buf, MAX_PATH, ini.c_str());
		cfg.mouseSpeed = std::clamp(static_cast<float>(std::atof(buf)), 0.1f, 10.0f);
		auto& wall = Wall::GetSettings();
		wall.resolution = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iPictureResolution", 1024, ini.c_str())), 256, 4096);
		wall.defaultStyle = (std::max)(0, static_cast<int>(::GetPrivateProfileIntA("Frames", "iDefaultFrameStyle", 7, ini.c_str())));
		g_hangStyle = wall.defaultStyle;
		// fPlaceReach replaces the old fMaxPlaceDistance (default went from 400 to 2000)
		::GetPrivateProfileStringA("Frames", "fPlaceReach", "2000", buf, MAX_PATH, ini.c_str());
		wall.maxDistance = std::clamp(static_cast<float>(std::atof(buf)), 100.0f, 10000.0f);
		wall.cleanUp = ::GetPrivateProfileIntA("Frames", "bCleanUpTextures", 1, ini.c_str()) != 0;
		::GetPrivateProfileStringA("Frames", "fExamineReach", "600", buf, MAX_PATH, ini.c_str());
		wall.examineReach = std::clamp(static_cast<float>(std::atof(buf)), 100.0f, 5000.0f);
		auto note = [&](const char* a_key, bool a_default) { return ::GetPrivateProfileIntA("Notifications", a_key, a_default ? 1 : 0, ini.c_str()) != 0; };
		wall.notifications = note("bNotifications", true);
		wall.showSize = note("bShowSize", false);
		wall.showDistance = note("bShowDistance", false);
		wall.showFrameName = note("bShowFrameName", false);
		wall.showPlacementHelp = note("bShowPlacementHelp", true);
		wall.showConfirmations = note("bShowConfirmations", true);
		wall.showPlacementWarnings = note("bShowPlacementWarnings", true);
		wall.showErrors = note("bShowErrors", true);
		cfg.placementInfo = note("bShowPlacementInfo", true);
		spdlog::info("Notifications: on={} size={} distance={} frame={} help={} confirm={} warnings={} errors={} info line={}", wall.notifications,
			wall.showSize, wall.showDistance, wall.showFrameName, wall.showPlacementHelp, wall.showConfirmations, wall.showPlacementWarnings,
			wall.showErrors, cfg.placementInfo);
		spdlog::info("Frames: resolution {}  default style {}  reach {}", wall.resolution, Frames::StyleName(0, wall.defaultStyle), wall.maxDistance);
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
		if (auto activated = Wall::TakeActivatedFrame(); activated && !Wall::IsPlacing()) {
			if (!g_open && !g_wantOpen) {
				Ui::ResetFrameMenu();
				g_frameMenu = *activated;
				g_uiMode = UiMode::FrameMenu;
				g_wantOpen = true;
			} else if (g_open && g_uiMode == UiMode::FrameMenu) {
				g_frameMenu = *activated;  // after Change frame: the menu now controls the new frame
				g_changePending = 0;
			}
		}
		if (g_wantOpen != g_open) {
			ApplyOpen(g_wantOpen);
		}
		Wall::PlacementStatus placing;
		const bool            infoLine = !g_open && cfg.placementInfo && Wall::GetPlacementStatus(placing);
		if ((!g_open && !infoLine) || g_imguiFailed) {
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
		if (infoLine) {
			std::lock_guard lk(g_inputMtx);
			g_input.clear();  // the game has the controls while hanging; nothing goes to ImGui
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
		if (!infoLine) {
			DrainInput();
		}
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
		if (infoLine) {
			io.MouseDrawCursor = false;
			const auto values = fmt::format("{}  \xC2\xB7  {}  \xC2\xB7  Size {:.0f}%  \xC2\xB7  Distance {:+.2f}", placing.frame, placing.shape,
				placing.scale * 100.0f, placing.distance);
			Ui::DrawPlacementInfo(values.c_str(),
				"Click: hang   \xC2\xB7   Wheel: size   \xC2\xB7   Shift+Wheel: frame   \xC2\xB7   Up/Down (hold): distance   \xC2\xB7   Right-click / Esc: cancel");
		} else {
			DrawUI();
		}
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
