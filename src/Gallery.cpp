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
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include <stb_image_write.h>

#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>

namespace fs = std::filesystem;

namespace Gallery
{
	namespace
	{
		constexpr int kThumbMaxDim = 480;  // sharp enough for 320 px thumbnails on a 4K screen
		constexpr int kFullMaxDim = 4096;
		constexpr int kTexturesPerFrame = 12;  // small thumbnails: a dozen uploads per frame cost next to nothing

		// DirectInput scan codes (what Skyrim's ButtonEvent::idCode holds for the keyboard)
		constexpr std::uint32_t kDIK_Escape = 0x01;
		constexpr std::uint32_t kDIK_Left = 0xCB;
		constexpr std::uint32_t kDIK_Right = 0xCD;
		constexpr std::uint32_t kDIK_Up = 0xC8;
		constexpr std::uint32_t kDIK_Down = 0xD0;

		// ---------------------------------------------------------------- settings
		struct Settings
		{
			fs::path      folder{ L"Data\\SKSE\\Plugins\\Hearthframe\\Pictures" };
			int           toggleKey = VK_F8;  // virtual-key code from the INI
			std::uint32_t toggleScan = 0x42;  // same key as a DirectInput scan code
			bool          recursive = false;
			int           thumbSize = 200;
			float         fontSize = 18.0f;
			float         mouseSpeed = 1.0f;
			float         uiScale = 1.0f;  // extra UI scale on top of the automatic screen-height scale
			bool          placementInfo = true;  // size / distance / frame line at the bottom while hanging
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
			int                       loadedDim = 0;         // long side the thumbnail was made at
			std::uint64_t             lastSeen = 0;          // frame the card was last on screen
		};
		std::uint64_t g_frame = 0;            // UI frames drawn
		constexpr int kMaxThumbs = 240;       // thumbnails kept in video memory; older off-screen ones are freed
		bool g_virtualFileHint = false;  // a listed picture couldn't be opened by name (MO2 virtual Data folder)

		struct Job
		{
			int      gen = 0;
			int      index = 0;
			fs::path path;
			int      maxDim = 0;
			bool     full = false;
			bool     preview = false;  // frame menu picture (not part of the list)
			bool     placed = false;   // Placed pictures window thumbnail
			bool     info = false;     // only read the picture's size (for the folder tabs)
		};

		struct Result
		{
			int                       gen = 0;
			int                       index = 0;
			bool                      full = false;
			bool                      preview = false;
			bool                      placed = false;
			bool                      info = false;
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
		int                                g_shapeFolder = 0;  // gallery folder tab: 0 all, 1 + frame shape
		int                                g_folderCounts[4]{};
		// Placed pictures window
		std::vector<Wall::PlacedInfo>      g_placed;
		bool                               g_placedLoading = false;
		struct ExtraThumb
		{
			ID3D11ShaderResourceView* srv = nullptr;
			int                       w = 0, h = 0;
			bool                      loading = false, missing = false;
		};
		std::map<fs::path, ExtraThumb>     g_placedThumbs;  // pictures not in the gallery list (other folders, renamed...)
		char                               g_renameBuf[256]{};
		std::string                        g_renameError;
		bool                               g_renameDone = false;
		ID3D11ShaderResourceView*          g_previewSrv = nullptr;  // frame menu picture
		fs::path                           g_previewPath;
		int                                g_previewW = 0, g_previewH = 0;
		bool                               g_previewLoading = false, g_previewMissing = false;
		std::atomic<bool>                  g_altDown{ false };
		// Settings window
		Ui::SettingsModel                  g_sm;
		std::string                        g_smKeyName, g_smOthers, g_smError;
		int                                g_smKeyVk = 0;          // new gallery key chosen in the window (0 = unchanged)
		std::atomic<bool>                  g_captureKey{ false };  // waiting for the "press a key" key
		std::atomic<std::uint32_t>         g_capturedDik{ 0 };     // set by the input sink (0xFFFFFFFF = cancelled)
		std::string                        g_smKeyWarning;
		HWND                               g_gameWindow = nullptr;
		std::chrono::steady_clock::time_point g_lastExplorer{};
		bool                               g_pendingTheme = false;  // rebuild fonts before the next frame

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

		// ---------------------------------------------------------------- fast decode (Windows Imaging Component)
		// Scales while decoding: JPEGs are decoded straight at 1/2, 1/4 or 1/8 size, so a thumbnail of a big
		// photo costs a fraction of a full decode. Worker threads only; returns false to fall back to stb.
		bool LoadWithWic(const fs::path& a_path, int a_maxDim, Result& r)
		{
			thread_local IWICImagingFactory* factory = [] {
				::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				IWICImagingFactory* f = nullptr;
				::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
				return f;
			}();
			if (!factory) {
				return false;
			}
			IWICBitmapDecoder*     decoder = nullptr;
			IWICBitmapFrameDecode* frame = nullptr;
			IWICBitmapScaler*      scaler = nullptr;
			IWICFormatConverter*   conv = nullptr;
			bool                   ok = false;
			do {
				if (FAILED(factory->CreateDecoderFromFilename(a_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
					FAILED(decoder->GetFrame(0, &frame))) {
					break;
				}
				UINT w = 0, h = 0;
				if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
					break;
				}
				r.origW = static_cast<int>(w);
				r.origH = static_cast<int>(h);
				UINT tw = w, th = h;
				const UINT longest = (std::max)(w, h);
				if (a_maxDim > 0 && longest > static_cast<UINT>(a_maxDim)) {
					const double s = static_cast<double>(a_maxDim) / longest;
					tw = (std::max)(1u, static_cast<UINT>(w * s));
					th = (std::max)(1u, static_cast<UINT>(h * s));
				}
				IWICBitmapSource* src = frame;
				if (tw != w || th != h) {
					if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant))) {
						break;
					}
					src = scaler;
				}
				if (FAILED(factory->CreateFormatConverter(&conv)) ||
					FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
					break;
				}
				r.w = static_cast<int>(tw);
				r.h = static_cast<int>(th);
				r.px.resize(static_cast<std::size_t>(tw) * th * 4);
				ok = SUCCEEDED(conv->CopyPixels(nullptr, tw * 4, static_cast<UINT>(r.px.size()), r.px.data()));
			} while (false);
			for (IUnknown* u : { static_cast<IUnknown*>(conv), static_cast<IUnknown*>(scaler), static_cast<IUnknown*>(frame), static_cast<IUnknown*>(decoder) }) {
				if (u) {
					u->Release();
				}
			}
			if (!ok) {
				r.px.clear();
			}
			return ok;
		}

		// ---------------------------------------------------------------- thumbnail cache on disk
		// Documents\My Games\Skyrim Special Edition\Hearthframe\ThumbCache\<picture key>_<size>.jpg/.png
		// The key covers the picture's path, size and time, so an edited or renamed picture gets a new one.
		fs::path g_cacheDir;

		std::uint64_t Hash64(std::string_view a_s)
		{
			std::uint64_t h = 1469598103934665603ull;
			for (const unsigned char c : a_s) {
				h = (h ^ c) * 1099511628211ull;
			}
			return h;
		}

		std::string CacheKey(const fs::path& a_path)
		{
			std::error_code ec;
			const auto      size = fs::file_size(a_path, ec);
			const auto      time = fs::last_write_time(a_path, ec).time_since_epoch().count();
			return fmt::format("{:016x}", Hash64(fmt::format("{}|{}|{}", ToUtf8(a_path), size, time)));
		}

		fs::path CacheFile(const std::string& a_key, int a_dim, bool a_png)
		{
			return g_cacheDir / fmt::format("{}_{}.{}", a_key, a_dim, a_png ? "png" : "jpg");
		}

		bool LoadFromCache(const std::string& a_key, int a_dim, Result& r)
		{
			if (g_cacheDir.empty()) {
				return false;
			}
			for (const bool png : { false, true }) {
				const auto f = CacheFile(a_key, a_dim, png);
				std::error_code ec;
				if (!fs::exists(f, ec)) {
					continue;
				}
				int   w = 0, h = 0, n = 0;
				auto* px = stbi_load(ToUtf8(f).c_str(), &w, &h, &n, 4);
				if (!px) {
					continue;
				}
				r.w = w;
				r.h = h;
				r.px.assign(px, px + static_cast<std::size_t>(w) * h * 4);
				stbi_image_free(px);
				// original size: stored in the file name of a tiny sidecar would be overkill; read the header
				return true;
			}
			return false;
		}

		void SaveToCache(const std::string& a_key, int a_dim, const Result& r)
		{
			if (g_cacheDir.empty() || r.px.empty()) {
				return;
			}
			bool alpha = false;
			for (std::size_t i = 3; i < r.px.size(); i += 4) {
				if (r.px[i] != 255) {
					alpha = true;
					break;
				}
			}
			const auto      f = CacheFile(a_key, a_dim, alpha);
			const auto      tmp = fs::path(f).concat(L".tmp");
			std::error_code ec;
			fs::create_directories(g_cacheDir, ec);
			const bool ok = alpha ? stbi_write_png(ToUtf8(tmp).c_str(), r.w, r.h, 4, r.px.data(), r.w * 4) != 0 :
			                        stbi_write_jpg(ToUtf8(tmp).c_str(), r.w, r.h, 4, r.px.data(), 90) != 0;
			if (ok) {
				fs::rename(tmp, f, ec);
			}
			if (!ok || ec) {
				fs::remove(tmp, ec);
			}
		}

		// Removes cached thumbnails of pictures that are no longer in the list (deleted, edited, renamed). Background.
		void CleanCache(std::unordered_set<std::string> a_keep)
		{
			if (g_cacheDir.empty()) {
				return;
			}
			std::thread([keep = std::move(a_keep)] {
				std::error_code ec;
				int             removed = 0;
				for (fs::directory_iterator it(g_cacheDir, ec), end; !ec && it != end; it.increment(ec)) {
					const auto name = ToUtf8(it->path().filename());
					if (name.size() < 16 || keep.contains(name.substr(0, 16))) {
						continue;
					}
					std::error_code e2;
					fs::remove(it->path(), e2);
					removed += e2 ? 0 : 1;
				}
				if (removed) {
					spdlog::info("Thumbnail cache: removed {} old file(s)", removed);
				}
			}).detach();
		}

		bool LoadPixels(const fs::path& path, int maxDim, Result& r)
		{
			if (LoadWithWic(path, maxDim, r)) {
				return true;
			}
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
			::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			for (;;) {
				Job job;
				{
					std::unique_lock lk(S.mtx);
					S.cv.wait(lk, [] { return !S.jobs.empty(); });
					job = std::move(S.jobs.front());
					S.jobs.pop_front();
				}
				if (job.gen != g_gen.load() && !job.preview && !job.placed) {
					continue;  // the list was refreshed while this was waiting
				}
				Result r;
				r.gen = job.gen;
				r.index = job.index;
				r.full = job.full;
				r.preview = job.preview;
				r.placed = job.placed;
				r.info = job.info;
				if (job.info) {
					int w = 0, h = 0, comp = 0;
					r.ok = stbi_info(ToUtf8(job.path).c_str(), &w, &h, &comp) != 0;
					r.origW = w;
					r.origH = h;
					std::lock_guard lk(S.mtx);
					S.results.push_back(std::move(r));
					continue;
				}
				r.path = job.path;
				try {
					const bool  cacheable = !job.full;
					std::string key;
					if (cacheable) {
						key = CacheKey(job.path);
						if (LoadFromCache(key, job.maxDim, r)) {
							int w = 0, h = 0, comp = 0;  // the card shows the original size
							if (stbi_info(ToUtf8(job.path).c_str(), &w, &h, &comp)) {
								r.origW = w;
								r.origH = h;
							}
							r.ok = true;
							std::lock_guard lk(S.mtx);
							S.results.push_back(std::move(r));
							continue;
						}
					}
					r.ok = LoadPixels(job.path, job.maxDim, r);
					if (r.ok && cacheable) {
						SaveToCache(key, job.maxDim, r);
					}
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
		ID3D11ShaderResourceView* MakeSRV(const std::uint8_t* px, int w, int h);

		// ---- frame previews for the hang dialog: Data\SKSE\Plugins\Hearthframe\FramePreviews\<Shape>_<Style>.png
		// (rendered from the meshes with the canvas cut out) + holes.txt (image size and cut-out per file)
		struct PreviewEntry
		{
			Ui::FramePreview          preview;
			ID3D11ShaderResourceView* srv = nullptr;
			bool                      tried = false;
		};
		std::map<std::string, PreviewEntry> g_framePreviews;
		bool                                g_framePreviewsRead = false;

		// "Painted Blue, Copper Trim" -> "Landscape_PaintedBlueCopperTrim" (same rule as tools/catalog.py file_stem)
		std::string PreviewStem(int a_shape, int a_style)
		{
			std::string out = std::string(Frames::kShapes[Frames::ClampShape(a_shape)].name) + "_";
			bool        wordStart = true;
			for (const char* c = Frames::StyleName(a_shape, a_style); *c; ++c) {
				const unsigned char ch = static_cast<unsigned char>(*c);
				if (std::isalpha(ch)) {
					out += static_cast<char>(wordStart ? std::toupper(ch) : std::tolower(ch));
					wordStart = false;
				} else {
					if (std::isdigit(ch)) {
						out += static_cast<char>(ch);
					}
					wordStart = true;
				}
			}
			return out;
		}

		Ui::FramePreview FramePreviewFor(int a_shape, int a_style)
		{
			std::error_code ec;
			const fs::path  dir = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\Hearthframe\\FramePreviews"), ec);
			if (!g_framePreviewsRead) {
				g_framePreviewsRead = true;
				std::ifstream in(dir / L"holes.txt");
				std::string   line;
				while (std::getline(in, line)) {
					std::istringstream ss(line);
					std::string        stem;
					Ui::FramePreview   f;
					if (ss >> stem >> f.w >> f.h >> f.hx0 >> f.hy0 >> f.hx1 >> f.hy1) {
						g_framePreviews[stem].preview = f;
					}
				}
				spdlog::info("Frame previews: {} listed in {}", g_framePreviews.size(), ToUtf8(dir));
			}
			const auto it = g_framePreviews.find(PreviewStem(a_shape, a_style));
			if (it == g_framePreviews.end()) {
				return {};
			}
			auto& e = it->second;
			if (!e.tried) {  // load on first use (render thread)
				e.tried = true;
				int   w = 0, h = 0, n = 0;
				auto* px = stbi_load(ToUtf8(dir / (it->first + ".png")).c_str(), &w, &h, &n, 4);
				if (px) {
					e.srv = MakeSRV(px, w, h);
					stbi_image_free(px);
				}
				if (!e.srv) {
					spdlog::warn("Frame preview {} could not be loaded", it->first);
				}
			}
			Ui::FramePreview out = e.preview;
			out.tex = e.srv ? reinterpret_cast<ImTextureID>(e.srv) : ImTextureID{};
			return out;
		}

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
			for (int n = 0; n < kTexturesPerFrame;) {
				Result r;
				{
					std::lock_guard lk(S.mtx);
					if (S.results.empty()) {
						return;
					}
					r = std::move(S.results.front());
					S.results.pop_front();
				}
				if (r.info) {
					if (r.gen == g_gen.load() && r.index >= 0 && r.index < static_cast<int>(items.size()) && r.ok && items[r.index].origW == 0) {
						items[r.index].origW = r.origW;
						items[r.index].origH = r.origH;
					}
					continue;  // no texture made: doesn't count
				}
				++n;
				if (r.placed) {
					if (auto it = g_placedThumbs.find(r.path); it != g_placedThumbs.end()) {
						if (r.ok && !it->second.srv) {
							it->second.srv = MakeSRV(r.px.data(), r.w, r.h);
							it->second.w = r.w;
							it->second.h = r.h;
						}
						it->second.loading = false;
						it->second.missing = !it->second.srv;
					}
					continue;
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
						if (it.srv) {
							it.srv->Release();  // replaced by a sharper one
						}
						it.srv = MakeSRV(r.px.data(), r.w, r.h);
						it.w = r.w;
						it.h = r.h;
						it.loadedDim = (std::max)(r.w, r.h);
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
				Job info{ g_gen.load(), static_cast<int>(items.size()), path, 0, false };  // size only, for the folder tabs
				info.info = true;
				Submit(std::move(info));
				items.push_back(std::move(it));
			}
			spdlog::info("Scanned {} -> {} picture(s)", ToUtf8(cfg.folder), items.size());
			{
				std::unordered_set<std::string> keep;
				for (const auto& it : items) {
					keep.insert(CacheKey(it.path));
				}
				CleanCache(std::move(keep));
			}
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
				for (auto& [path, t] : g_placedThumbs) {
					if (t.srv) {
						t.srv->Release();
					}
				}
				g_placedThumbs.clear();
				g_placed.clear();
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
			// the wheel stays with the camera when only the arrow keys resize
			static UEFlag blocked{};
			if (a_on) {
				blocked = static_cast<UEFlag>(
					static_cast<std::uint32_t>(UEFlag::kActivate) | static_cast<std::uint32_t>(UEFlag::kMenu) |
					static_cast<std::uint32_t>(UEFlag::kFighting) |
					(Wall::GetSettings().resizeWith == 1 ? 0u : static_cast<std::uint32_t>(UEFlag::kWheelZoom)));
			}
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				cm->ToggleControls(blocked, !a_on);  // off: re-enable exactly what was blocked
			}
		}

		fs::path DefaultPictureFolder()
		{
			PWSTR    docs = nullptr;
			fs::path out;
			if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
				out = fs::path(docs) / L"My Games" / L"Skyrim Special Edition" / L"Hearthframe" / L"Pictures";
				::CoTaskMemFree(docs);
			} else {
				std::error_code ec;
				out = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\Hearthframe\\Pictures"), ec);
			}
			return out;
		}

		std::string KeyName(std::uint32_t a_dik)
		{
			wchar_t   name[64]{};
			const LONG lparam = static_cast<LONG>(((a_dik & 0x7F) << 16) | ((a_dik & 0x80) ? (1 << 24) : 0));
			if (::GetKeyNameTextW(lparam, name, 64) > 0) {
				return ToUtf8(fs::path(name));
			}
			return fmt::format("Key 0x{:02X}", a_dik);
		}

		fs::path DataTexturesFolder()
		{
			std::error_code ec;
			return fs::absolute(fs::path(L"Data") / L"textures", ec).lexically_normal();
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
		float g_sizeRepeatAt = 0.0f;
		bool  ArrowsResize() { return Wall::GetSettings().resizeWith != 2; }
		bool  WheelResize() { return Wall::GetSettings().resizeWith != 1; }

		void PushInput(const InputMsg& m)
		{
			std::lock_guard lk(g_inputMtx);
			if (g_input.size() < 1024) {
				g_input.push_back(m);
			}
		}

		// ---------------------------------------------------------------- input (game input events)
		const char*                           g_inputVia = "sink";  // "blocker" or "sink", for the input log
		std::atomic<std::int64_t>             g_lastBlockerMs{ 0 };  // last time the game's input reached our blocker
		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

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
								const int dir = code == kDIK_Up ? -1 : +1;  // Up = towards the wall, Down = away
								Wall::AdjustOffset(dir * (g_shiftDown ? 8 : 1));
								g_offsetRepeatAt = held + (g_shiftDown ? 0.10f : 0.05f);
							}
						}
						// held: Left/Right keep resizing (Shift: keep changing the frame, slower)
						if (keyboard && Wall::IsPlacing() && ArrowsResize() && (code == kDIK_Left || code == kDIK_Right)) {
							const float held = btn->HeldDuration();
							if (held >= g_sizeRepeatAt) {
								const int dir = code == kDIK_Right ? +1 : -1;
								if (g_shiftDown) {
									Wall::CycleStyle(dir);
									g_sizeRepeatAt = held + 0.30f;
								} else {
									Wall::AdjustScale(dir);
									g_sizeRepeatAt = held + 0.08f;
								}
							}
						}
						continue;
					}

					if (g_captureKey && keyboard && down) {  // Settings: "press a key" for the gallery key
						// never Tab / Alt / Ctrl / Shift / Windows / Enter (Alt+Tab, shortcuts...); Esc cancels
						constexpr std::uint32_t unsafe[] = { 0x0F, 0x38, 0xB8, 0x1D, 0x9D, 0x2A, 0x36, 0xDB, 0xDC, 0x1C, 0x9C };
						if (code == kDIK_Escape) {
							g_capturedDik = 0xFFFFFFFFu;
							g_captureKey = false;
						} else if (std::find(std::begin(unsafe), std::end(unsafe), code) == std::end(unsafe) && !g_altDown && !g_ctrlDown) {
							g_capturedDik = code;
							g_captureKey = false;
						}
						if (code == 0x38 || code == 0xB8) {
							g_altDown = true;
						}
						if (code == 0x1D || code == 0x9D) {
							g_ctrlDown = true;
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
							} else if ((code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) && WheelResize()) {
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
								Wall::AdjustOffset(g_shiftDown ? -8 : -1);  // Up: towards / into the wall (Shift: 2 units)
								g_offsetRepeatAt = 0.35f;
							} else if (code == kDIK_Down) {
								Wall::AdjustOffset(g_shiftDown ? +8 : +1);  // Down: away from the wall (Shift: 2 units)
								g_offsetRepeatAt = 0.35f;
							} else if ((code == kDIK_Left || code == kDIK_Right) && ArrowsResize()) {
								const int dir = code == kDIK_Right ? +1 : -1;
								if (g_shiftDown) {
									Wall::CycleStyle(dir);  // Shift + Left/Right: frame
								} else {
									Wall::AdjustScale(dir);  // Left/Right: size
								}
								g_sizeRepeatAt = 0.35f;
							}
						}
						continue;
					}

					if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
						if (g_open && dbg.logInput) {
							const bool    altGr = g_ctrlDown && g_altDown;
							const wchar_t c = down ? KeyToChar(code, g_shiftDown, altGr) : 0;
							spdlog::info("[input] key {:02X} {} via {}  shift={} ctrl={} alt={}  char={}", code, down ? "down" : "up", g_inputVia,
								g_shiftDown.load(), g_ctrlDown.load(), g_altDown.load(),
								!down ? std::string("-") : (c == 0 ? std::string("none") : ((g_ctrlDown && !altGr) ? fmt::format("U+{:04X} (not typed: Ctrl held)", static_cast<int>(c)) : fmt::format("U+{:04X}", static_cast<int>(c)))));
						}
						if (down && code == cfg.toggleScan) {
							if (g_wantOpen) {
								g_wantOpen = false;
							} else {
								auto* ui = RE::UI::GetSingleton();
								auto* cm = RE::ControlMap::GetSingleton();
								using UEFlag = RE::ControlMap::UEFlag;
								// Something else has the player's controls (another mod's menu, dialogue...):
								// don't open on top of it. Placing a picture only turns off activate/menu keys.
								const bool othersBusy = !Wall::IsPlacing() && cm &&
														!cm->AreControlsEnabled(static_cast<UEFlag>(static_cast<std::uint32_t>(UEFlag::kMovement) | static_cast<std::uint32_t>(UEFlag::kLooking)));
								const bool talking = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
								if (othersBusy || talking) {
									spdlog::info("Gallery key ignored: {} is using the controls right now", talking ? "a conversation" : "another menu or mod");
								} else if (!(ui && ui->GameIsPaused())) {
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

		// While a Hearthframe window is open, other mods' hotkeys (minimap Ctrl+N, OAR Ctrl+O...) and the
		// game must not see the keys typed into it. The game hands each frame's input to every listener in
		// one call; we sit in front of that call, handle the input ourselves and pass on an empty list.
		using DispatchInput = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
		std::uintptr_t g_dispatchInput = 0;  // the game's function we replaced the call to

		bool g_inBlockerPass = false;  // the list going on was already handled by the blocker: the sink skips it

		void DispatchInputHook(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events)
		{
			const auto original = reinterpret_cast<DispatchInput*>(g_dispatchInput);
			g_lastBlockerMs = NowMs();
			if (g_open && a_events && *a_events && !g_inputDisabled) {
				g_inputVia = "blocker";
				SafeHandleInput(a_events);
				g_inputVia = "sink";
				// Key/button releases still go on, so nothing (movement, another mod's hold key, Shift...)
				// stays held after the window closes; presses - what hotkeys react to - stay with us.
				// The list goes in writable memory: mods further down the chain (SKSE Menu Framework...)
				// write their filtered list back into it - a read-only constant crashed the game.
				static RE::InputEvent* kept[1];
				kept[0] = nullptr;
				RE::InputEvent* last = nullptr;
				std::vector<RE::InputEvent*> releases;
				for (auto* e = *a_events; e; e = e->next) {
					if (auto* b = e->AsButtonEvent(); b && b->IsUp()) {
						releases.push_back(e);
					}
				}
				for (auto* e : releases) {  // relink only the releases (like other menu mods filter the list)
					if (last) {
						last->next = e;
					} else {
						kept[0] = e;
					}
					last = e;
				}
				if (last) {
					last->next = nullptr;
				}
				g_inBlockerPass = true;
				original(a_source, kept);  // nobody else sees the presses
				g_inBlockerPass = false;
				return;
			}
			original(a_source, a_events);
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!g_inputDisabled && !g_inBlockerPass) {
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
					if (dbg.logInput) {
						spdlog::info("[input] char U+{:04X} given to the window (text box focused: {})", m.button, io.WantTextInput);
					}
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
					if (std::strcmp(Theme::StyleName(i), "Modern") == 0) {
						Theme::SetStyle(i);
					}
				}
				// parchment / leather textures for the look (the DX11 backend samples with WRAP, so they tile)
				Theme::SetTextureFactory([](int a_w, int a_h, const unsigned char* a_px) -> ImTextureID {
					return reinterpret_cast<ImTextureID>(MakeSRV(a_px, a_w, a_h));
				});
			}
			// the modern look's fonts ship with the mod (Cinzel + Inter, SIL Open Font License); Georgia if missing
			std::error_code   fec;
			const std::string own = ToUtf8(fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\Hearthframe\\Fonts"), fec)) + "\\";
			const std::string display = own + "Cinzel-SemiBold.ttf", uiReg = own + "Inter-Regular.ttf", uiSemi = own + "Inter-SemiBold.ttf",
							  uiItal = own + "Inter-Italic.ttf";
			Theme::FontFiles files{ regular.c_str(), bold.c_str(), italic.c_str() };
			files.display = display.c_str();
			files.uiRegular = uiReg.c_str();
			files.uiSemiBold = uiSemi.c_str();
			files.uiItalic = uiItal.c_str();
			Theme::Setup(scale, cfg.fontSize, files);
			ImGui::GetIO().MouseDrawCursor = g_open;
			spdlog::info("UI: look {}, scale {:.2f}, font {} ({})", Theme::StyleName(Theme::CurrentStyle()), scale, cfg.fontSize,
				fs::exists(fs::path(uiReg), fec) ? "Inter + Cinzel" : "Georgia - Fonts folder missing");
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
			const auto& io = ImGui::GetIO();  // no need for more pixels than the screen has
			const int   cap = std::clamp(static_cast<int>((std::max)(io.DisplaySize.x, io.DisplaySize.y)), 1024, kFullMaxDim);
			Submit({ g_gen.load(), i, items[i].path, cap, true }, true);
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

		// Folder tab a picture belongs to: 1 + its best frame shape (0 = size not known yet: All only).
		int FolderOf(const Item& a_item)
		{
			return (a_item.origW > 0 && a_item.origH > 0) ? 1 + Wall::AutoShape(a_item.origW, a_item.origH) : 0;
		}

		void UpdateVisible()
		{
			visible.clear();
			std::fill(std::begin(g_folderCounts), std::end(g_folderCounts), 0);
			const std::string needle = LowerUtf8(g_search);
			for (int i = 0; i < static_cast<int>(items.size()); ++i) {
				if (!needle.empty() && LowerUtf8(items[i].name).find(needle) == std::string::npos) {
					continue;
				}
				const int folder = FolderOf(items[i]);
				++g_folderCounts[0];
				if (folder > 0 && folder < 4) {
					++g_folderCounts[folder];
				}
				if (g_shapeFolder == 0 || g_shapeFolder == folder) {
					visible.push_back(i);
				}
			}
		}

		// Thumbnail for the Placed pictures window: the gallery's own when the picture is listed, else loaded.
		const ExtraThumb* PlacedThumb(const fs::path& a_path, ExtraThumb& a_tmp)
		{
			for (auto& it : items) {
				if (it.path == a_path) {
					if (it.srv) {
						a_tmp = { it.srv, it.w, it.h, false, false };
						return &a_tmp;
					}
					break;
				}
			}
			auto [it, added] = g_placedThumbs.try_emplace(a_path);
			if (added) {
				it->second.loading = true;
				Job job{ g_gen.load(), -1, a_path, kThumbMaxDim, false };
				job.placed = true;
				Submit(std::move(job));
			}
			return &it->second;
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

		// ---------------------------------------------------------------- settings window
		void FillSettingsModel()
		{
			auto& w = Wall::GetSettings();
			auto& m = g_sm;
			std::snprintf(m.folder, sizeof m.folder, "%s", ToUtf8(cfg.folder).c_str());
			m.recursive = cfg.recursive;
			std::error_code ec;
			g_smKeyVk = 0;
			g_smKeyName = KeyName(cfg.toggleScan);
			g_smKeyWarning.clear();
			m.thumbSize = cfg.thumbSize;
			m.uiScalePct = static_cast<int>(std::lround(cfg.uiScale * 100.0f));
			m.fontSize = static_cast<int>(std::lround(cfg.fontSize));
			m.mouseSpeed = cfg.mouseSpeed;
			m.defaultStyle = std::clamp(w.defaultStyle, 0, Frames::StyleCount(0) - 1);
			m.resolution = w.resolution;
			m.placeReach = static_cast<int>(w.maxDistance);
			m.examineReach = static_cast<int>(w.examineReach);
			m.cleanUp = w.cleanUp;
			m.resizeWith = w.resizeWith;
			m.notifications = w.notifications;
			m.showSize = w.showSize;
			m.showDistance = w.showDistance;
			m.showFrameName = w.showFrameName;
			m.showPlacementHelp = w.showPlacementHelp;
			m.showConfirmations = w.showConfirmations;
			m.showPlacementWarnings = w.showPlacementWarnings;
			m.showErrors = w.showErrors;
			m.placementInfo = cfg.placementInfo;
			g_captureKey = false;
			g_capturedDik = 0;
		}

		void ResetSettingsTab(int a_tab)
		{
			auto& m = g_sm;
			if (a_tab == 0) {
				std::snprintf(m.folder, sizeof m.folder, "%s", ToUtf8(DefaultPictureFolder()).c_str());
				m.recursive = false;
				g_smKeyVk = VK_F8;
				g_smKeyName = KeyName(VirtualKeyToDIK(VK_F8));
				m.thumbSize = 200;
				m.uiScalePct = 100;
				m.fontSize = 18;
				m.mouseSpeed = 1.0f;
			} else if (a_tab == 1) {
				m.defaultStyle = 7;
				m.resolution = 1024;
				m.placeReach = 2000;
				m.examineReach = 600;
				m.cleanUp = true;
				m.resizeWith = 0;
			} else {
				m.notifications = true;
				m.showSize = m.showDistance = m.showFrameName = false;
				m.showPlacementHelp = m.showConfirmations = m.showPlacementWarnings = m.showErrors = true;
				m.placementInfo = true;
			}
		}

		void WriteIni(const char* a_section, const char* a_key, const std::string& a_value)
		{
			::WritePrivateProfileStringA(a_section, a_key, a_value.c_str(), g_iniPath.c_str());
		}

		void ApplySettings()
		{
			auto& m = g_sm;
			auto& w = Wall::GetSettings();
			// picture folder (empty / the default -> written as empty, so the default follows Documents)
			std::string folderText = m.folder;
			while (!folderText.empty() && folderText.back() == ' ') {
				folderText.pop_back();
			}
			std::error_code ec;
			const fs::path newFolder = folderText.empty() ? DefaultPictureFolder() : fs::absolute(FromUtf8Path(folderText), ec);
			const bool     isDefault = newFolder.lexically_normal() == DefaultPictureFolder().lexically_normal();
			const bool     rescan = newFolder != cfg.folder || m.recursive != cfg.recursive;
			cfg.folder = newFolder;
			cfg.recursive = m.recursive;
			::WritePrivateProfileStringW(L"General", L"sFolder", isDefault ? L"" : newFolder.c_str(), fs::path(g_iniPath).c_str());
			WriteIni("General", "bRecursive", m.recursive ? "1" : "0");
			if (g_smKeyVk > 0) {
				cfg.toggleKey = g_smKeyVk;
				cfg.toggleScan = VirtualKeyToDIK(g_smKeyVk);
				WriteIni("General", "iToggleKey", std::to_string(g_smKeyVk));
			}
			cfg.thumbSize = m.thumbSize;
			WriteIni("General", "iThumbnailSize", std::to_string(m.thumbSize));
			cfg.mouseSpeed = m.mouseSpeed;
			WriteIni("General", "fMouseSpeed", fmt::format("{:.2f}", m.mouseSpeed));
			if (std::lround(cfg.fontSize) != m.fontSize) {
				cfg.fontSize = static_cast<float>(m.fontSize);
				g_pendingTheme = true;  // fonts are rebuilt before the next frame
			}
			WriteIni("General", "fFontSize", std::to_string(m.fontSize));
			RequestUiScale(m.uiScalePct / 100.0f);  // applied (and saved) before the next frame
			// frames & hanging
			w.defaultStyle = m.defaultStyle;
			g_hangStyle = m.defaultStyle;
			w.resolution = m.resolution;
			w.maxDistance = static_cast<float>(m.placeReach);
			w.examineReach = static_cast<float>(m.examineReach);
			w.cleanUp = m.cleanUp;
			WriteIni("Frames", "iDefaultFrameStyle", std::to_string(m.defaultStyle));
			WriteIni("Frames", "iPictureResolution", std::to_string(m.resolution));
			WriteIni("Frames", "fPlaceReach", std::to_string(m.placeReach));
			WriteIni("Frames", "fExamineReach", std::to_string(m.examineReach));
			WriteIni("Frames", "bCleanUpTextures", m.cleanUp ? "1" : "0");
			w.resizeWith = std::clamp(m.resizeWith, 0, 2);
			WriteIni("Frames", "iResizeWith", std::to_string(w.resizeWith));
			// notifications
			w.notifications = m.notifications;
			w.showSize = m.showSize;
			w.showDistance = m.showDistance;
			w.showFrameName = m.showFrameName;
			w.showPlacementHelp = m.showPlacementHelp;
			w.showConfirmations = m.showConfirmations;
			w.showPlacementWarnings = m.showPlacementWarnings;
			w.showErrors = m.showErrors;
			cfg.placementInfo = m.placementInfo;
			auto b = [](bool v) { return std::string(v ? "1" : "0"); };
			WriteIni("Notifications", "bNotifications", b(m.notifications));
			WriteIni("Notifications", "bShowSize", b(m.showSize));
			WriteIni("Notifications", "bShowDistance", b(m.showDistance));
			WriteIni("Notifications", "bShowFrameName", b(m.showFrameName));
			WriteIni("Notifications", "bShowPlacementHelp", b(m.showPlacementHelp));
			WriteIni("Notifications", "bShowConfirmations", b(m.showConfirmations));
			WriteIni("Notifications", "bShowPlacementWarnings", b(m.showPlacementWarnings));
			WriteIni("Notifications", "bShowErrors", b(m.showErrors));
			WriteIni("Notifications", "bShowPlacementInfo", b(m.placementInfo));
			spdlog::info("Settings saved: folder {} (recursive {}), key {}", ToUtf8(cfg.folder), cfg.recursive, cfg.toggleKey);
			if (rescan) {
				g_needScan = true;
			}
		}

		// Called every gallery frame: the Settings window (when open) and what it asks for.
		void OpenFolderInExplorer(const fs::path& a_folder);

		void DrawSettingsWindow()
		{
			auto& m = g_sm;
			// results from the input sink / folder picker
			if (const auto dik = g_capturedDik.exchange(0); dik != 0) {
				if (dik != 0xFFFFFFFFu) {
					const UINT vk = ::MapVirtualKeyW((dik & 0x7F) | ((dik & 0x80) ? 0xE000u : 0u), MAPVK_VSC_TO_VK_EX);
					if (vk != 0 && VirtualKeyToDIK(static_cast<int>(vk)) == dik) {
						g_smKeyVk = static_cast<int>(vk);
						g_smKeyName = KeyName(dik);
						g_smKeyWarning.clear();
						if (auto* cm = RE::ControlMap::GetSingleton()) {  // also a game control? (allowed, but say so)
							const auto ev = cm->GetUserEventName(dik, RE::INPUT_DEVICE::kKeyboard);
							if (!ev.empty()) {
								g_smKeyWarning = fmt::format("Also the game's \"{}\" key - it will do both.", ev);
							}
						}
					} else {
						g_smKeyWarning = KeyName(dik) + " can't be used as the gallery key.";
					}
				}
			}
			if (g_captureKey && g_gameWindow && ::GetForegroundWindow() != g_gameWindow) {
				g_captureKey = false;  // the game lost focus (Alt+Tab...): stop waiting, so nothing gets caught by accident
			}

			m.waitingForKey = g_captureKey;
			m.keyName = g_smKeyName.c_str();
			m.keyWarning = g_smKeyWarning.empty() ? nullptr : g_smKeyWarning.c_str();
			m.landscapeStyles = Frames::StyleNames(0);
			m.landscapeCount = Frames::StyleCount(0);
			g_smOthers = fmt::format("Square: {}  \xC2\xB7  Portrait: {}", Frames::StyleName(1, m.defaultStyle), Frames::StyleName(2, m.defaultStyle));
			m.defaultStyleOthers = g_smOthers.c_str();
			m.iniPath = g_iniPath.c_str();
			m.error = g_smError.empty() ? nullptr : g_smError.c_str();

			const auto r = Ui::DrawSettings(m);
			switch (r.action) {
			case Ui::SettingsAction::Save:
				ApplySettings();
				g_captureKey = false;
				break;
			case Ui::SettingsAction::Cancel:
				g_captureKey = false;
				break;
			case Ui::SettingsAction::OpenPictures:
				OpenFolderInExplorer(cfg.folder);
				break;
			case Ui::SettingsAction::ChangeKey:
				g_capturedDik = 0;
				g_captureKey = true;
				break;
			case Ui::SettingsAction::ResetTab:
				ResetSettingsTab(r.index);
				break;
			default:
				break;
			}
		}

		void OpenFolderInExplorer(const fs::path& a_folder)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lastExplorer < std::chrono::seconds(3)) {
				return;  // one window per click burst
			}
			g_lastExplorer = now;
			std::error_code ec;
			fs::create_directories(a_folder, ec);
			::AllowSetForegroundWindow(ASFW_ANY);  // the game has the foreground: let Explorer come to the front
			std::thread([a_folder] {
				::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
				::ShellExecuteW(nullptr, L"open", a_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				::CoUninitialize();
			}).detach();
			spdlog::info("Opened {} in Explorer", ToUtf8(a_folder));
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
			                "Leave sFolder empty in Hearthframe.ini to use Documents\\My Games\\Skyrim Special Edition\\Hearthframe\\Pictures, where new pictures work right away." :
			                nullptr;
			gm.thumbSize = &cfg.thumbSize;
			static int uiPct = 100;
			if (g_pendingUiScale < 0.0f && !ImGui::IsAnyItemActive()) {
				uiPct = static_cast<int>(std::lround(cfg.uiScale * 100.0f));
			}
			gm.uiScalePct = &uiPct;
			UpdateVisible();
			gm.openFolderStyle = 2;  // "Open folder" button after the path
			gm.shapeFolder = &g_shapeFolder;
			for (int i = 0; i < 4; ++i) {
				gm.folderCounts[i] = g_folderCounts[i];
			}
			if (auto list = Wall::TakePlacedList()) {
				g_placed = std::move(*list);
				g_placedLoading = false;
			}
			gm.placedLoading = g_placedLoading;
			std::vector<std::string> frameNames;  // keep the strings alive for this frame
			frameNames.reserve(g_placed.size());
			for (const auto& pi : g_placed) {
				ExtraThumb        tmp;
				const ExtraThumb* th = pi.stray ? &tmp : PlacedThumb(pi.picture, tmp);
				Ui::PlacedPicture pp;
				pp.name = pi.name.c_str();
				if (pi.stray) {
					tmp = {};
					th = &tmp;
				}
				pp.thumb = th->srv ? reinterpret_cast<ImTextureID>(th->srv) : 0;
				pp.thumbW = th->w;
				pp.thumbH = th->h;
				pp.missing = th->missing;
				pp.frame = Frames::StyleName(pi.shape, pi.style);
				pp.shape = Frames::kShapes[Frames::ClampShape(pi.shape)].name;
				pp.canvasAspect = Frames::kShapes[Frames::ClampShape(pi.shape)].canvasW / Frames::kShapes[Frames::ClampShape(pi.shape)].canvasH;
				pp.sizePct = static_cast<int>(std::lround(pi.scale * 100.0f));
				pp.place = pi.place.c_str();
				pp.region = pi.region.c_str();
				pp.interior = pi.interior;
				pp.stray = pi.stray;
				gm.placed.push_back(pp);
			}
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
			gm.requestThumb = [](int i) {  // called for every card on screen
				const int idx = visible[i];
				Item&     it = items[idx];
				it.lastSeen = g_frame;
				const int want = std::clamp((static_cast<int>(cfg.thumbSize * Theme::Scale()) + 63) / 64 * 64, 128, kThumbMaxDim);
				const bool tooSoft = it.state == State::Ready && it.loadedDim > 0 && it.loadedDim < want * 3 / 4 &&
				                     (std::max)(it.origW, it.origH) > it.loadedDim;
				if (it.state == State::None || tooSoft) {
					it.state = State::Queued;
					Submit({ g_gen.load(), idx, it.path, want, false }, true);  // newest on screen first
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
			gm.framePreview = [](int shape, int style) { return FramePreviewFor(shape, style); };

			const int  folderBefore = g_shapeFolder;
			const auto r = Ui::DrawGallery(gm);
			DrawSettingsWindow();
			++g_frame;
			if (g_frame % 15 == 0) {  // drop queued thumbnails for cards scrolled away / in another folder tab
				std::lock_guard lk(S.mtx);
				for (auto j = S.jobs.begin(); j != S.jobs.end();) {
					const bool thumb = !j->full && !j->info && !j->preview && !j->placed && j->gen == g_gen.load() && j->index >= 0 &&
					                   j->index < static_cast<int>(items.size());
					if (thumb && items[j->index].lastSeen + 30 < g_frame && items[j->index].state == State::Queued) {
						items[j->index].state = State::None;
						j = S.jobs.erase(j);
					} else {
						++j;
					}
				}
			}
			{  // free thumbnails that have been off screen the longest, beyond the budget
				std::vector<int> loaded;
				for (int i = 0; i < static_cast<int>(items.size()); ++i) {
					if (items[i].srv) {
						loaded.push_back(i);
					}
				}
				if (static_cast<int>(loaded.size()) > kMaxThumbs) {
					std::sort(loaded.begin(), loaded.end(), [](int a, int b) { return items[a].lastSeen < items[b].lastSeen; });
					for (int k = 0; k < static_cast<int>(loaded.size()) - kMaxThumbs; ++k) {
						Item& it = items[loaded[k]];
						if (it.lastSeen + 2 >= g_frame || loaded[k] == viewing) {
							break;  // still on screen
						}
						it.srv->Release();
						it.srv = nullptr;
						it.state = State::None;  // loaded again when scrolled back into view
						it.loadedDim = 0;
					}
				}
			}
			if (g_shapeFolder != folderBefore) {
				::WritePrivateProfileStringA("General", "iGalleryFolder", std::to_string(g_shapeFolder).c_str(), g_iniPath.c_str());
				viewing = -1;
				ReleaseFull();
			}
			const int  item = (r.index >= 0 && r.index < static_cast<int>(visible.size())) ? visible[r.index] : -1;
			switch (r.action) {
			case Ui::GalleryAction::OpenSettings:
				FillSettingsModel();
				Ui::OpenSettings();
				break;
			case Ui::GalleryAction::OpenFolder:
				OpenFolderInExplorer(cfg.folder);
				break;
			case Ui::GalleryAction::OpenPlaced:
				g_placedLoading = true;
				Wall::RequestPlacedList();
				break;
			case Ui::GalleryAction::RemoveStrays:
				Wall::RemoveStrayFrames();
				g_placed.erase(std::remove_if(g_placed.begin(), g_placed.end(), [](const Wall::PlacedInfo& a_p) { return a_p.stray; }), g_placed.end());
				Wall::RequestPlacedList();
				break;
			case Ui::GalleryAction::DeletePlaced:
				if (r.index >= 0 && r.index < static_cast<int>(g_placed.size())) {
					Wall::RemoveFrame(g_placed[r.index].ref);
					g_placed.erase(g_placed.begin() + r.index);  // gone from the list right away
					Wall::RequestPlacedList();                   // and re-read once the game has removed it
				}
				break;
			case Ui::GalleryAction::ThumbSize:
				::WritePrivateProfileStringA("General", "iThumbnailSize", std::to_string(cfg.thumbSize).c_str(), g_iniPath.c_str());
				spdlog::info("Thumbnail size {} saved", cfg.thumbSize);
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
		const auto      ini = fs::absolute(fs::path(L"Data\\SKSE\\Plugins\\Hearthframe.ini"), ec).string();
		g_iniPath = ini;

		char buf[MAX_PATH]{};
		{
			wchar_t wbuf[1024]{};
			::GetPrivateProfileStringW(L"General", L"sFolder", L"", wbuf, 1024, fs::path(ini).c_str());
			// empty = Documents\...\Hearthframe\Pictures: outside Data on purpose, Mod Organizer's virtual Data
			// folder can't open files added while the game runs
			cfg.folder = wbuf[0] ? fs::absolute(fs::path(wbuf), ec) : DefaultPictureFolder();
		}
		g_cacheDir = DefaultPictureFolder().parent_path() / L"ThumbCache";
		cfg.toggleKey = static_cast<int>(::GetPrivateProfileIntA("General", "iToggleKey", VK_F8, ini.c_str()));
		cfg.toggleScan = VirtualKeyToDIK(cfg.toggleKey);
		if (cfg.toggleScan == 0) {
			spdlog::warn("iToggleKey={} is not a usable key, falling back to F8", cfg.toggleKey);
			cfg.toggleKey = VK_F8;
			cfg.toggleScan = 0x42;
		}
		cfg.recursive = ::GetPrivateProfileIntA("General", "bRecursive", 0, ini.c_str()) != 0;
		cfg.thumbSize = std::clamp(static_cast<int>(::GetPrivateProfileIntA("General", "iThumbnailSize", 200, ini.c_str())), 100, 320);
		g_shapeFolder = std::clamp(static_cast<int>(::GetPrivateProfileIntA("General", "iGalleryFolder", 0, ini.c_str())), 0, Frames::kShapeCount);
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
		wall.resizeWith = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Frames", "iResizeWith", 0, ini.c_str())), 0, 2);
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
		dbg.logInput = ::GetPrivateProfileIntA("Debug", "bLogInput", 0, ini.c_str()) != 0;
		dbg.blockInput = ::GetPrivateProfileIntA("Debug", "bBlockInput", 1, ini.c_str()) != 0;
		std::error_code ec2;
		spdlog::info("INI: {} ({})", ini, fs::exists(ini, ec2) ? "found" : "NOT FOUND - using defaults");
		spdlog::info("Debug: bHookPresent={} bInputSink={} bBlockInput={} bLogInput={}", dbg.hookPresent, dbg.inputSink, dbg.blockInput, dbg.logInput);
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

	// Which module a call target ends up in (follows the jumps other mods' trampolines use), for the log.
	std::uintptr_t FollowJumps(std::uintptr_t a_addr)  // no C++ objects here: __try needs that
	{
		__try {
			for (int hop = 0; hop < 6; ++hop) {
				const auto* b = reinterpret_cast<const std::uint8_t*>(a_addr);
				if (b[0] == 0xFF && b[1] == 0x25) {  // jmp [rip+disp32]
					const auto disp = *reinterpret_cast<const std::int32_t*>(b + 2);
					a_addr = *reinterpret_cast<const std::uintptr_t*>(a_addr + 6 + disp);
				} else if (b[0] == 0xE9) {  // jmp rel32
					a_addr = a_addr + 5 + *reinterpret_cast<const std::int32_t*>(b + 1);
				} else {
					break;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
		return a_addr;
	}

	std::string ModuleOf(std::uintptr_t a_addr)
	{
		a_addr = FollowJumps(a_addr);
		HMODULE mod = nullptr;
		wchar_t path[MAX_PATH]{};
		if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a_addr), &mod) &&
			::GetModuleFileNameW(mod, path, MAX_PATH)) {
			return ToUtf8(fs::path(path).filename());
		}
		return fmt::format("unknown ({:X})", a_addr);
	}

	void PrepareInputBlocker()
	{
		if (dbg.inputSink && dbg.blockInput) {
			SKSE::AllocTrampoline(14);  // reserved at plugin load; the hook itself goes in at kDataLoaded
		}
	}

	void InstallInputBlocker()
	{
		if (!dbg.inputSink || !dbg.blockInput) {
			spdlog::info("Input blocker off (bInputSink={} bBlockInput={})", dbg.inputSink, dbg.blockInput);
			return;
		}
		// BSInputDeviceManager::PollInputDevices -> the call that sends the input events to all listeners.
		// Installed at kDataLoaded, after every plugin has hooked this call too, so we are first in line:
		// while a Hearthframe window is open nobody else (OAR, MeridianUI, minimap...) sees the keys.
		REL::Relocation<std::uintptr_t> target{ REL::RelocationID(67315, 68617), REL::VariantOffset(0x7B, 0x7B, 0x81) };
		const auto* code = reinterpret_cast<const std::uint8_t*>(target.address());
		if (*code != 0xE8) {  // not a call: different game build or another mod rewrote it
			spdlog::warn("Input blocker: unexpected code at {:X} ({:02X}) - other mods' hotkeys can still fire while typing", target.address(), *code);
			return;
		}
		const std::uintptr_t next = target.address() + 5 + *reinterpret_cast<const std::int32_t*>(code + 1);
		g_dispatchInput = SKSE::GetTrampoline().write_call<5>(target.address(), &DispatchInputHook);
		spdlog::info("Input blocker installed in front of {} - keys typed in Hearthframe windows stay there", ModuleOf(next));
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
				// 2-3 decode threads below normal priority: the game's own threads always come first
				const unsigned cores = (std::max)(1u, std::thread::hardware_concurrency());
				const int      workers = std::clamp(static_cast<int>(cores / 4), 2, 3);
				for (int w = 0; w < workers; ++w) {
					std::thread(WorkerMain).detach();
				}
				spdlog::info("Picture loading: {} background thread(s), thumbnail cache {}", workers, ToUtf8(g_cacheDir));
			}
			g_displayH = static_cast<float>(desc.BufferDesc.Height);
			g_gameWindow = desc.OutputWindow;
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

		if (g_pendingTheme) {
			g_pendingTheme = false;
			ApplyTheme();
		}
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
			static const char* const hints[] = {
				"Click: hang   \xC2\xB7   Wheel or Left/Right: size   \xC2\xB7   Shift + Wheel or Left/Right: frame   \xC2\xB7   Up/Down: distance   \xC2\xB7   Right-click / Esc: cancel",
				"Click: hang   \xC2\xB7   Left/Right: size   \xC2\xB7   Shift+Left/Right: frame   \xC2\xB7   Up/Down: distance   \xC2\xB7   Right-click / Esc: cancel",
				"Click: hang   \xC2\xB7   Wheel: size   \xC2\xB7   Shift+Wheel: frame   \xC2\xB7   Up/Down: distance   \xC2\xB7   Right-click / Esc: cancel",
			};
			Ui::DrawPlacementInfo(values.c_str(), hints[std::clamp(Wall::GetSettings().resizeWith, 0, 2)]);
		} else {
			DrawUI();
		}
		if (dbg.logInput) {  // [Debug] bLogInput: what happens around typing
			static bool         lastText = false, lastFocus = true, staleLogged = false;
			static std::int64_t openedMs = 0;
			const bool          text = g_open && io.WantTextInput;
			if (text != lastText) {
				spdlog::info("[input] text box {}", text ? "focused - letters should type" : "not focused");
				lastText = text;
			}
			const bool focus = !g_gameWindow || ::GetForegroundWindow() == g_gameWindow;
			if (focus != lastFocus) {
				spdlog::info("[input] game window {} the keyboard focus", focus ? "has" : "LOST");
				lastFocus = focus;
			}
			const std::int64_t now = NowMs();
			if (!g_open) {
				openedMs = 0;
				staleLogged = false;
			} else {
				if (!openedMs) {
					openedMs = now;
					spdlog::info("[input] window open; input blocker {}", g_dispatchInput ? "active" : "NOT installed");
				}
				const std::int64_t last = g_lastBlockerMs;
				if (g_dispatchInput && now - (std::max)(last, openedMs) > 2000 && !staleLogged) {
					spdlog::warn("[input] the game has not sent any input for 2 s while the window is open - another mod is holding it back");
					staleLogged = true;
				} else if (now - last < 500 && staleLogged) {
					spdlog::info("[input] game input is arriving again");
					staleLogged = false;
				}
			}
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
