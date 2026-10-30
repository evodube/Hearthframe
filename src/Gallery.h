#pragma once

struct IDXGISwapChain;

namespace Gallery
{
	struct DebugSwitches
	{
		bool hookPresent = true;  // [Debug] bHookPresent
		bool inputSink = true;    // [Debug] bInputSink
		bool blockInput = true;   // [Debug] bBlockInput: other mods/the game get no keys while a window is open
		bool logInput = false;    // [Debug] bLogInput: write every key the gallery receives to the log
	};

	void                 LoadSettings();
	const DebugSwitches& GetDebugSwitches();

	// Registers for the game's input events (keyboard + mouse go through DirectInput,
	// so the window never sees normal mouse messages). Call at kDataLoaded.
	bool InstallInputSink();
	// Plugin load: while a Hearthframe window is open, other mods and the game get no keyboard/mouse input.
	void InstallInputBlocker();

	// Called from inside the game's IDXGISwapChain::Present, on the render thread.
	void OnPresent(IDXGISwapChain* a_swapChain);

	// __except filter: logs the exception code and the faulting module+offset, then handles it.
	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where);
}
