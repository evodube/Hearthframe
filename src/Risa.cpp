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

// Integration with Risa's All In One Menu (https://risas-all-in-one-menu.pages.dev/for-mod-authors/).
// Risa's launcher gets a "Hearthframe" button that opens the gallery. While Risa manages it, our own
// gallery key is switched off (in memory only); excluding Hearthframe in Risa switches it back on.
// Risa is never required: if its DLL isn't loaded, nothing here does anything.

#include "Risa.h"

#include "Gallery.h"

#if __has_include("RisaMenuIntegrationAPI.h")
#	include "RisaMenuIntegrationAPI.h"
#	define HEARTHFRAME_HAS_RISA 1
#endif

namespace Risa
{
#ifdef HEARTHFRAME_HAS_RISA
	namespace
	{
		namespace API = RisaMenuIntegrationAPI;

		constexpr const char* kMenuId = "Hearthframe.Gallery";  // never change: Risa keys its settings by this
		constexpr std::uint32_t kIconPicture = 0xF03E;           // Font Awesome "image"

		std::string g_keyLabel;  // Risa copies strings, but keep ours alive anyway

		bool OpenMenu(void*) { return Gallery::LauncherOpen(); }
		void CloseMenu(void*) { Gallery::LauncherClose(); }
		bool IsMenuOpen(void*) { return Gallery::IsOpen(); }
		void SetHotkeyEnabled(void*, bool a_enabled) { Gallery::SetNativeKeyEnabled(a_enabled); }
	}

	void Register()
	{
		const HMODULE module = ::GetModuleHandleW(L"RisaAllInOneMenu.dll");
		if (!module) {
			spdlog::info("Risa's All In One Menu not installed - the gallery key works on its own");
			return;
		}
		const auto getApi = reinterpret_cast<API::GetInterfaceFn>(::GetProcAddress(module, "RisaAllInOneMenu_GetIntegrationAPI"));
		if (!getApi) {
			spdlog::warn("Risa's All In One Menu: no RisaAllInOneMenu_GetIntegrationAPI export - not integrating");
			return;
		}
		const auto* base = getApi(API::kTypedInterfaceVersion);
		if (!base) {
			spdlog::warn("Risa's All In One Menu: interface v{} not available (older Risa?) - not integrating", API::kTypedInterfaceVersion);
			return;
		}
		const auto* api = reinterpret_cast<const API::InterfaceV5*>(base);

		g_keyLabel = Gallery::ToggleKeyName();
		API::MenuRegistrationV1 menu{};
		menu.menuId = kMenuId;
		menu.displayName = "Hearthframe";
		menu.iconCodepoint = kIconPicture;
		menu.nativeHotkeyLabel = g_keyLabel.c_str();
		menu.context = nullptr;
		menu.OpenMenu = OpenMenu;
		menu.CloseMenu = CloseMenu;
		menu.IsMenuOpen = IsMenuOpen;
		menu.SetHotkeyEnabled = SetHotkeyEnabled;

		const auto result = api->RegisterMenu(&menu);
		if (result == API::Registration_Success) {
			spdlog::info("Risa's All In One Menu: gallery added to the launcher (id {}, key {})", kMenuId, g_keyLabel);
		} else {
			spdlog::warn("Risa's All In One Menu: RegisterMenu failed (result {}) - the gallery key works on its own", static_cast<std::uint32_t>(result));
		}
	}
#else
	void Register()
	{
		spdlog::info("Built without Risa's All In One Menu support (RisaMenuIntegrationAPI.h missing)");
	}
#endif
}
