// Hearthframe - an in-game picture gallery and wall frames for Skyrim Special Edition
// Copyright (C) 2026 evodube
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program
// (LICENSE.txt). If not, see <https://www.gnu.org/licenses/>.

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
		bool directMouse = false; // [Debug] bDirectMouse: cursor reads the mouse right before drawing (0 = from the game's input)
	};

	void                 LoadSettings();
	const DebugSwitches& GetDebugSwitches();

	// Registers for the game's input events (keyboard + mouse go through DirectInput,
	// so the window never sees normal mouse messages). Call at kDataLoaded.
	bool InstallInputSink();
	// Plugin load: while a Hearthframe window is open, other mods and the game get no keyboard/mouse input.
	void PrepareInputBlocker();  // plugin load
	void InstallInputBlocker();  // first kPostLoadGame / kNewGame

	// Called from inside the game's IDXGISwapChain::Present, on the render thread.
	void OnPresent(IDXGISwapChain* a_swapChain);

	// __except filter: logs the exception code and the faulting module+offset, then handles it.
	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where);
}
