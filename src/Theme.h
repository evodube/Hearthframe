#pragma once

// Folder Gallery look: dark charcoal window with thin gold lines and diamond corners, gold serif
// small-caps headings, parchment panels with dark-brown text, outlined buttons and chips.
// Depends only on Dear ImGui (no game headers), so it can be previewed outside the game.

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

namespace Theme
{
	struct FontFiles
	{
		const char* regular = nullptr;  // e.g. C:\Windows\Fonts\georgia.ttf
		const char* bold = nullptr;
		const char* italic = nullptr;
	};

	// Call once after ImGui::CreateContext(). a_scale ~ screen height / 1080, a_baseSize in px at 1080p.
	void Setup(float a_scale, float a_baseSize, const FontFiles& a_files);
	float Scale();
	float S(float a_px);  // a_px at 1080p -> pixels on this screen

	ImFont* Body();
	ImFont* Bold();
	ImFont* Italic();

	// ---- colours (ImU32)
	ImU32 Gold();
	ImU32 GoldDim();
	ImU32 Ink();       // dark brown text on parchment
	ImU32 InkSoft();
	ImU32 Cream();     // light text on charcoal

	// ---- window chrome
	// Begins a borderless themed window (NoTitleBar). a_open may be null. Returns ImGui::Begin's result.
	bool BeginWindow(const char* a_id, bool* a_open, int a_extraFlags = 0);
	// Window background + gold chrome for the current window (used by BeginWindow and themed popups).
	void DrawChrome();
	void EndWindow();
	struct Stat
	{
		const char* label;
		const char* value;
	};
	// Gold small-caps title + italic subtitle on the left, stat boxes and a close box on the right.
	// Returns true when the close box was clicked.
	bool Header(const char* a_title, const char* a_subtitle, const Stat* a_stats = nullptr, int a_statCount = 0, bool a_closeBox = true);

	// ---- parchment panel (child window); text inside is dark ink
	bool BeginParchment(const char* a_id, const ImVec2& a_size, bool a_scroll = false, bool a_autoHeight = false);
	void EndParchment();
	void SectionLabel(const char* a_text);  // small-caps label with a rule under it (inside parchment)

	// ---- controls
	bool Button(const char* a_label, const ImVec2& a_size = ImVec2(0, 0), bool a_primary = false);
	bool Chip(const char* a_label, bool a_selected = false);  // outlined chip for parchment panels
	bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_format, float a_width);

	// Text helpers
	void SmallCaps(ImDrawList* a_dl, ImFont* a_font, float a_size, const ImVec2& a_pos, ImU32 a_col, const char* a_text);
	float SmallCapsWidth(ImFont* a_font, float a_size, const char* a_text);
	void TextItalic(const char* a_fmt, ...);
	void TextColoredWrapped(ImU32 a_col, const char* a_text);
}
