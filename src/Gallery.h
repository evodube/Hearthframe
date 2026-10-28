#pragma once

struct IDXGISwapChain;

namespace Gallery
{
	void LoadSettings();
	// Registers for the game's input events (keyboard + mouse go through DirectInput,
	// so the window never sees normal mouse messages). Call after kInputLoaded/kDataLoaded.
	bool InstallInputSink();
	// Called from inside the game's IDXGISwapChain::Present, on the render thread.
	void OnPresent(IDXGISwapChain* a_swapChain);
}
