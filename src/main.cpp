#include "Gallery.h"

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "FolderGallery.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	PresentFn g_originalPresent = nullptr;

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_sc, UINT a_sync, UINT a_flags)
	{
		Gallery::OnPresent(a_sc);
		return g_originalPresent(a_sc, a_sync, a_flags);
	}

	// Builds a throwaway swap chain on a hidden window just to read the address of
	// IDXGISwapChain::Present out of the shared vtable, then patches that slot.
	// This needs no game addresses, so it is the same on every Skyrim version.
	bool InstallPresentHook()
	{
		const auto instance = ::GetModuleHandleW(nullptr);
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = ::DefWindowProcW;
		wc.hInstance = instance;
		wc.lpszClassName = L"FolderGalleryDummy";
		::RegisterClassExW(&wc);
		HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, instance, nullptr);

		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 1;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hwnd;
		sd.SampleDesc.Count = 1;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		ID3D11Device*        dev = nullptr;
		ID3D11DeviceContext* ctx = nullptr;
		IDXGISwapChain*      sc = nullptr;
		D3D_FEATURE_LEVEL    level{};
		const auto hr = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &sd, &sc, &dev, &level, &ctx);

		bool ok = false;
		if (SUCCEEDED(hr) && sc) {
			auto** vtable = *reinterpret_cast<void***>(sc);
			DWORD  old = 0;
			if (::VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &old)) {
				g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
				vtable[8] = reinterpret_cast<void*>(&HookedPresent);
				::VirtualProtect(&vtable[8], sizeof(void*), old, &old);
				ok = true;
			}
		} else {
			spdlog::error("D3D11CreateDeviceAndSwapChain failed (0x{:08X})", static_cast<std::uint32_t>(hr));
		}

		if (sc) sc->Release();
		if (ctx) ctx->Release();
		if (dev) dev->Release();
		if (hwnd) ::DestroyWindow(hwnd);
		::UnregisterClassW(wc.lpszClassName, instance);
		return ok;
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) {
			return;
		}
		if (InstallPresentHook()) {
			spdlog::info("Present hook installed");
		} else {
			spdlog::error("Could not install the Present hook - the gallery window will not appear");
		}
		if (Gallery::InstallInputSink()) {
			spdlog::info("Input sink registered");
		} else {
			spdlog::error("Could not register for input events - the toggle key will not work");
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("FolderGallery {} loaded", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));
	Gallery::LoadSettings();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with the SKSE messaging interface");
		return false;
	}
	return true;
}
