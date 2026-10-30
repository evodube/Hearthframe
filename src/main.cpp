#include "Gallery.h"
#include "Wall.h"

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		// keep the last runs: Hearthframe.log (this run), Hearthframe.1.log, Hearthframe.2.log
		{
			const auto      dir = *path;
			std::error_code ec;
			std::filesystem::remove(dir / "Hearthframe.2.log", ec);
			std::filesystem::rename(dir / "Hearthframe.1.log", dir / "Hearthframe.2.log", ec);
			std::filesystem::rename(dir / "Hearthframe.log", dir / "Hearthframe.1.log", ec);
		}
		*path /= "Hearthframe.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	// ------------------------------------------------------------------ Present hook
	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	PresentFn         g_originalPresent = nullptr;
	std::atomic<bool> g_presentDisabled{ false };
	std::atomic<int>  g_presentCalls{ 0 };

	void SafeOnPresent(IDXGISwapChain* a_sc)
	{
		__try {
			Gallery::OnPresent(a_sc);
		} __except (Gallery::SehFilter(GetExceptionInformation(), "OnPresent")) {
			g_presentDisabled = true;
		}
	}

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_sc, UINT a_sync, UINT a_flags)
	{
		if (g_presentCalls.load(std::memory_order_relaxed) < 3) {
			const int n = ++g_presentCalls;
			spdlog::info("HookedPresent call #{} (swap chain {})", n, static_cast<void*>(a_sc));
		}
		if (!g_presentDisabled) {
			SafeOnPresent(a_sc);
		}
		return g_originalPresent(a_sc, a_sync, a_flags);
	}

	bool PatchPresentSlot(IDXGISwapChain* a_sc, const char* a_source)
	{
		auto** vtable = *reinterpret_cast<void***>(a_sc);
		spdlog::info("  {} swap chain {} vtable {} Present {}", a_source, static_cast<void*>(a_sc),
			static_cast<void*>(vtable), vtable[8]);
		if (vtable[8] == reinterpret_cast<void*>(&HookedPresent)) {
			spdlog::info("  already hooked");
			return true;
		}
		DWORD old = 0;
		if (!::VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
			spdlog::error("  VirtualProtect failed ({})", ::GetLastError());
			return false;
		}
		g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
		vtable[8] = reinterpret_cast<void*>(&HookedPresent);
		::VirtualProtect(&vtable[8], sizeof(void*), old, &old);
		return true;
	}

	// Preferred: the game's own swap chain, read from BSGraphics::Renderer.
	IDXGISwapChain* GetGameSwapChain()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		spdlog::info("  BSGraphics::Renderer = {}", static_cast<void*>(renderer));
		if (!renderer) {
			return nullptr;
		}
		auto* sc = renderer->data.renderWindows[0].swapChain;
		spdlog::info("  renderWindows[0].swapChain = {}", static_cast<void*>(sc));
		return sc;
	}

	IDXGISwapChain* SafeGetGameSwapChain()
	{
		__try {
			return GetGameSwapChain();
		} __except (Gallery::SehFilter(GetExceptionInformation(), "GetGameSwapChain")) {
			return nullptr;
		}
	}

	bool InstallPresentHook()
	{
		if (auto* sc = SafeGetGameSwapChain()) {
			return PatchPresentSlot(sc, "game");
		}
		// No dummy-swap-chain fallback: creating an extra swap chain breaks ENB Frame Generation
		// (its proxy is a singleton) and crashes the game on the next Present.
		spdlog::error("  game swap chain not available yet");
		return false;
	}

	bool SafeInstallPresentHook()
	{
		__try {
			return InstallPresentHook();
		} __except (Gallery::SehFilter(GetExceptionInformation(), "InstallPresentHook")) {
			return false;
		}
	}

	bool SafeInstallInputSink()
	{
		__try {
			return Gallery::InstallInputSink();
		} __except (Gallery::SehFilter(GetExceptionInformation(), "InstallInputSink")) {
			return false;
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		spdlog::info("SKSE message {}", a_msg->type);
		if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame || a_msg->type == SKSE::MessagingInterface::kNewGame) {
			// First save loaded / new game: later than every plugin that hooks input at the main menu
			// (Hearthsworn, OAR's menu...), so Hearthframe's blocker is first in line. Only the first time.
			static bool blockerDone = false;
			if (!blockerDone) {
				blockerDone = true;
				Gallery::InstallInputBlocker();
			}
			Wall::CancelPlacement(false);
			Wall::OnGameLoaded();
			return;
		}
		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) {
			return;
		}
		const auto& dbg = Gallery::GetDebugSwitches();

		if (dbg.hookPresent) {
			spdlog::info("Step 1/2: installing Present hook...");
			if (SafeInstallPresentHook()) {
				spdlog::info("Step 1/2: Present hook installed");
			} else {
				spdlog::error("Step 1/2: could not install the Present hook - the gallery window will not appear");
			}
		} else {
			spdlog::warn("Step 1/2: Present hook DISABLED by bHookPresent=0");
		}

		if (dbg.inputSink) {
			spdlog::info("Step 2/2: registering input sink...");
			if (SafeInstallInputSink()) {
				spdlog::info("Step 2/2: input sink registered");
			} else {
				spdlog::error("Step 2/2: could not register for input events - the toggle key will not work");
			}
		} else {
			spdlog::warn("Step 2/2: input sink DISABLED by bInputSink=0");
		}
		if (Wall::Init()) {
			spdlog::info("Wall frames ready");
		} else {
			spdlog::error("Some frame records are missing - is the new Hearthframe.esp installed and enabled?");
		}
		spdlog::info("Setup finished");
	}

	void LogEnvironment()
	{
		const auto ver = REL::Module::get().version();
		spdlog::info("Game version {} ({})", ver.string("."sv),
			REL::Module::IsAE() ? "AE" : (REL::Module::IsSE() ? "SE" : "other"));
		const auto lib = REL::Module::IsAE() ?
		                     fmt::format("Data\\SKSE\\Plugins\\versionlib-{}.bin", ver.string()) :
		                     fmt::format("Data\\SKSE\\Plugins\\version-{}.bin", ver.string());
		std::error_code ec;
		if (std::filesystem::exists(lib, ec)) {
			spdlog::info("Address Library found: {}", lib);
		} else {
			spdlog::critical("Address Library NOT FOUND: {} - install 'Address Library for SKSE Plugins' for this game version", lib);
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("Hearthframe {} loaded, SKSE {}",
		SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."sv),
		REL::Version::unpack(a_skse->SKSEVersion()).string("."sv));
	LogEnvironment();
	Gallery::LoadSettings();
	Gallery::PrepareInputBlocker();
	Wall::InstallSerialization();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with the SKSE messaging interface");
		return false;
	}
	spdlog::info("Waiting for kDataLoaded (main menu)");
	return true;
}
