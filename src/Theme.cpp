#include "Theme.h"

#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Theme
{
	namespace
	{
		float   g_s = 1.0f;
		float   g_base = 18.0f;
		ImFont* g_body = nullptr;
		ImFont* g_bold = nullptr;
		ImFont* g_italic = nullptr;
		int     g_parchmentDepth = 0;

		constexpr ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

		// palette
		constexpr ImU32 kGold = C(206, 174, 116);
		constexpr ImU32 kGoldDim = C(128, 104, 66);
		constexpr ImU32 kGoldFaint = C(92, 76, 52);
		constexpr ImU32 kCream = C(230, 218, 194);
		constexpr ImU32 kCreamDim = C(168, 154, 128);
		constexpr ImU32 kCharcoal = C(29, 28, 27);
		constexpr ImU32 kCharcoalHi = C(44, 42, 39);
		constexpr ImU32 kParchment = C(224, 208, 170);
		constexpr ImU32 kParchmentDark = C(196, 174, 128);
		constexpr ImU32 kParchmentEdge = C(150, 122, 80);
		constexpr ImU32 kInk = C(56, 38, 22);
		constexpr ImU32 kInkSoft = C(112, 88, 60);

		ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

		bool FileExists(const char* a_path)
		{
			if (!a_path || !*a_path) {
				return false;
			}
			FILE* f = std::fopen(a_path, "rb");
			if (!f) {
				return false;
			}
			std::fclose(f);
			return true;
		}

		ImFont* LoadFont(const char* a_path, float a_size)
		{
			auto& io = ImGui::GetIO();
			if (FileExists(a_path)) {
				ImFontConfig fc;
				fc.OversampleH = 2;
				fc.OversampleV = 2;
				if (auto* f = io.Fonts->AddFontFromFileTTF(a_path, a_size, &fc)) {
					return f;
				}
			}
			return nullptr;
		}

		void Diamond(ImDrawList* a_dl, const ImVec2& a_c, float a_r, ImU32 a_col)
		{
			a_dl->AddQuadFilled(a_c + ImVec2(0, -a_r), a_c + ImVec2(a_r, 0), a_c + ImVec2(0, a_r), a_c + ImVec2(-a_r, 0), a_col);
		}
	}

	void Setup(float a_scale, float a_baseSize, const FontFiles& a_files)
	{
		g_s = a_scale < 0.5f ? 0.5f : a_scale;
		g_base = a_baseSize;
		auto& io = ImGui::GetIO();
		const float size = g_base * g_s;

		g_body = LoadFont(a_files.regular, size);
		if (!g_body) {
			ImFontConfig fc;
			fc.SizePixels = size;
			g_body = io.Fonts->AddFontDefault(&fc);
		}
		g_bold = LoadFont(a_files.bold, size * 1.9f);  // big: used for titles and numbers (drawn smaller when needed)
		if (!g_bold) {
			g_bold = g_body;
		}
		g_italic = LoadFont(a_files.italic, size * 0.92f);
		if (!g_italic) {
			g_italic = g_body;
		}
		io.FontDefault = g_body;

		ImGuiStyle st;
		st.WindowPadding = ImVec2(26, 22);
		st.FramePadding = ImVec2(14, 9);
		st.CellPadding = ImVec2(8, 6);
		st.ItemSpacing = ImVec2(12, 10);
		st.ItemInnerSpacing = ImVec2(10, 6);
		st.IndentSpacing = 20;
		st.ScrollbarSize = 16;
		st.GrabMinSize = 22;
		st.WindowBorderSize = 0;
		st.ChildBorderSize = 0;
		st.PopupBorderSize = 1;
		st.FrameBorderSize = 1;
		st.WindowRounding = 0;
		st.ChildRounding = 2;
		st.FrameRounding = 2;
		st.PopupRounding = 2;
		st.ScrollbarRounding = 2;
		st.GrabRounding = 2;
		st.WindowMinSize = ImVec2(200, 120);
		st.ScaleAllSizes(g_s);

		auto* c = st.Colors;
		c[ImGuiCol_Text] = V(kCream);
		c[ImGuiCol_TextDisabled] = V(kCreamDim);
		c[ImGuiCol_WindowBg] = V(kCharcoal);
		c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_PopupBg] = V(C(36, 34, 31, 250));
		c[ImGuiCol_Border] = V(kGoldDim);
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = V(C(22, 21, 20));
		c[ImGuiCol_FrameBgHovered] = V(C(52, 47, 40));
		c[ImGuiCol_FrameBgActive] = V(C(66, 58, 46));
		c[ImGuiCol_TitleBg] = V(kCharcoal);
		c[ImGuiCol_TitleBgActive] = V(kCharcoal);
		c[ImGuiCol_TitleBgCollapsed] = V(kCharcoal);
		c[ImGuiCol_MenuBarBg] = V(kCharcoalHi);
		c[ImGuiCol_ScrollbarBg] = V(C(0, 0, 0, 40));
		c[ImGuiCol_ScrollbarGrab] = V(C(128, 104, 66, 170));
		c[ImGuiCol_ScrollbarGrabHovered] = V(C(160, 132, 86, 220));
		c[ImGuiCol_ScrollbarGrabActive] = V(kGold);
		c[ImGuiCol_CheckMark] = V(kGold);
		c[ImGuiCol_SliderGrab] = V(C(176, 146, 94));
		c[ImGuiCol_SliderGrabActive] = V(kGold);
		c[ImGuiCol_Button] = V(C(52, 45, 35));
		c[ImGuiCol_ButtonHovered] = V(C(82, 68, 47));
		c[ImGuiCol_ButtonActive] = V(C(104, 85, 56));
		c[ImGuiCol_Header] = V(C(206, 174, 116, 50));
		c[ImGuiCol_HeaderHovered] = V(C(206, 174, 116, 90));
		c[ImGuiCol_HeaderActive] = V(C(206, 174, 116, 130));
		c[ImGuiCol_Separator] = V(kGoldFaint);
		c[ImGuiCol_SeparatorHovered] = V(kGoldDim);
		c[ImGuiCol_SeparatorActive] = V(kGold);
		c[ImGuiCol_ResizeGrip] = V(C(128, 104, 66, 90));
		c[ImGuiCol_ResizeGripHovered] = V(C(160, 132, 86, 180));
		c[ImGuiCol_ResizeGripActive] = V(kGold);
		c[ImGuiCol_Tab] = V(kCharcoalHi);
		c[ImGuiCol_TabHovered] = V(C(82, 68, 47));
		c[ImGuiCol_TabActive] = V(C(66, 56, 40));
		c[ImGuiCol_PlotLines] = V(kGold);
		c[ImGuiCol_PlotHistogram] = V(kGold);
		c[ImGuiCol_TextSelectedBg] = V(C(206, 174, 116, 90));
		c[ImGuiCol_DragDropTarget] = V(kGold);
		c[ImGuiCol_NavHighlight] = V(kGold);
		c[ImGuiCol_ModalWindowDimBg] = V(C(0, 0, 0, 150));
		ImGui::GetStyle() = st;
	}

	float   Scale() { return g_s; }
	float   S(float a_px) { return a_px * g_s; }
	ImFont* Body() { return g_body; }
	ImFont* Bold() { return g_bold; }
	ImFont* Italic() { return g_italic; }
	ImU32   Gold() { return kGold; }
	ImU32   GoldDim() { return kGoldDim; }
	ImU32   Ink() { return kInk; }
	ImU32   InkSoft() { return kInkSoft; }
	ImU32   Cream() { return kCream; }

	// ------------------------------------------------------------------ text
	float SmallCapsWidth(ImFont* a_font, float a_size, const char* a_text)
	{
		float       w = 0.0f;
		const float smallSize = a_size * 0.78f;
		const float track = a_size * 0.06f;
		char        ch[2] = { 0, 0 };
		for (const char* p = a_text; *p; ++p) {
			const bool lower = std::islower(static_cast<unsigned char>(*p)) != 0;
			ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
			w += a_font->CalcTextSizeA(lower ? smallSize : a_size, FLT_MAX, 0.0f, ch).x + track;
		}
		return w;
	}

	void SmallCaps(ImDrawList* a_dl, ImFont* a_font, float a_size, const ImVec2& a_pos, ImU32 a_col, const char* a_text)
	{
		const float smallSize = a_size * 0.78f;
		const float track = a_size * 0.06f;
		const float drop = (a_size - smallSize) * 0.80f;  // keep the smallSize letters on the same baseline
		float       x = a_pos.x;
		char        ch[2] = { 0, 0 };
		for (const char* p = a_text; *p; ++p) {
			const bool  lower = std::islower(static_cast<unsigned char>(*p)) != 0;
			const float sz = lower ? smallSize : a_size;
			ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
			a_dl->AddText(a_font, sz, ImVec2(x, a_pos.y + (lower ? drop : 0.0f)), a_col, ch);
			x += a_font->CalcTextSizeA(sz, FLT_MAX, 0.0f, ch).x + track;
		}
	}

	void TextItalic(const char* a_fmt, ...)
	{
		ImGui::PushFont(g_italic);
		va_list args;
		va_start(args, a_fmt);
		ImGui::TextV(a_fmt, args);
		va_end(args);
		ImGui::PopFont();
	}

	void TextColoredWrapped(ImU32 a_col, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_col);
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	// ------------------------------------------------------------------ window chrome
	bool BeginWindow(const char* a_id, bool* a_open, int a_extraFlags)
	{
		const bool visible = ImGui::Begin(a_id, a_open,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse | a_extraFlags);
		DrawChrome();
		return visible;
	}

	void DrawChrome()
	{
		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 a = ImGui::GetWindowPos();
		const ImVec2 b = a + ImGui::GetWindowSize();
		dl->PushClipRect(a, b, false);  // the window's own clip rect excludes its padding
		// soft vignette: lighter in the middle, darker at the edges
		const ImU32 mid = C(40, 38, 36), edge = C(22, 21, 20);
		const ImVec2 m = (a + b) * 0.5f;
		dl->AddRectFilledMultiColor(a, ImVec2(m.x, m.y), edge, edge, mid, edge);
		dl->AddRectFilledMultiColor(ImVec2(m.x, a.y), ImVec2(b.x, m.y), edge, edge, edge, mid);
		dl->AddRectFilledMultiColor(ImVec2(a.x, m.y), ImVec2(m.x, b.y), edge, mid, edge, edge);
		dl->AddRectFilledMultiColor(ImVec2(m.x, m.y), b, mid, edge, edge, edge);
		// outer edge + inset gold line with diamond corners
		dl->AddRect(a, b, C(10, 10, 10), 0.0f, 0, S(2.0f));
		const float  inset = S(9.0f);
		const ImVec2 ia = a + ImVec2(inset, inset), ib = b - ImVec2(inset, inset);
		dl->AddRect(ia, ib, kGoldFaint, 0.0f, 0, S(1.2f));
		const float r = S(6.0f);
		Diamond(dl, ia, r, kGoldDim);
		Diamond(dl, ImVec2(ib.x, ia.y), r, kGoldDim);
		Diamond(dl, ImVec2(ia.x, ib.y), r, kGoldDim);
		Diamond(dl, ib, r, kGoldDim);
		dl->PopClipRect();
	}

	void EndWindow()
	{
		ImGui::End();
	}

	bool Header(const char* a_title, const char* a_subtitle, const Stat* a_stats, int a_statCount, bool a_closeBox)
	{
		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  w = ImGui::GetContentRegionAvail().x;
		const float  h = S(76.0f);

		// title + subtitle
		SmallCaps(dl, g_bold, S(34.0f), p + ImVec2(S(4.0f), S(2.0f)), kGold, a_title);
		if (a_subtitle && *a_subtitle) {
			dl->AddText(g_italic, S(18.0f), p + ImVec2(S(6.0f), S(46.0f)), kCreamDim, a_subtitle);
		}

		// close box
		bool        closed = false;
		float       right = p.x + w;
		const float box = S(54.0f);
		if (a_closeBox) {
			const ImVec2 ca(right - box, p.y + S(4.0f)), cb(right, p.y + S(4.0f) + box);
			ImGui::SetCursorScreenPos(ca);
			closed = ImGui::InvisibleButton("##close", ImVec2(box, box));
			const bool hov = ImGui::IsItemHovered();
			dl->AddRectFilled(ca, cb, hov ? C(70, 58, 40) : C(36, 34, 31));
			dl->AddRect(ca, cb, hov ? kGold : kGoldDim, 0.0f, 0, S(1.2f));
			const float  k = box * 0.28f;
			const ImVec2 cc = (ca + cb) * 0.5f;
			dl->AddLine(cc - ImVec2(k, k), cc + ImVec2(k, k), kCream, S(2.4f));
			dl->AddLine(cc + ImVec2(-k, k), cc + ImVec2(k, -k), kCream, S(2.4f));
			right -= box + S(18.0f);
		}

		// stat boxes, right to left
		for (int i = a_statCount - 1; i >= 0; --i) {
			const float  bw = S(130.0f), bh = box;
			const ImVec2 sa(right - bw, p.y + S(4.0f)), sb(right, p.y + S(4.0f) + bh);
			dl->AddRectFilled(sa, sb, C(24, 23, 22));
			dl->AddRect(sa, sb, kGoldDim, 0.0f, 0, S(1.2f));
			const float lw = SmallCapsWidth(g_body, S(13.0f), a_stats[i].label);
			SmallCaps(dl, g_body, S(13.0f), ImVec2((sa.x + sb.x - lw) * 0.5f, sa.y + S(6.0f)), kCreamDim, a_stats[i].label);
			const float vs = S(26.0f);
			const float vw = g_body->CalcTextSizeA(vs, FLT_MAX, 0.0f, a_stats[i].value).x;
			dl->AddText(g_body, vs, ImVec2((sa.x + sb.x - vw) * 0.5f, sa.y + S(22.0f)), kCream, a_stats[i].value);
			right -= bw + S(12.0f);
		}

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h));
		return closed;
	}

	// ------------------------------------------------------------------ parchment
	bool BeginParchment(const char* a_id, const ImVec2& a_size, bool a_scroll, bool a_autoHeight)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, kParchment);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22.0f), S(18.0f)));
		const bool visible = ImGui::BeginChild(a_id, a_size,
			ImGuiChildFlags_AlwaysUseWindowPadding | (a_autoHeight ? ImGuiChildFlags_AutoResizeY : 0),
			a_scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 a = ImGui::GetWindowPos();
		const ImVec2 b = a + ImGui::GetWindowSize();
		dl->PushClipRect(a, b, false);
		// aged edges: darker towards the border
		const float  e = S(26.0f);
		const ImU32  dark = C(150, 120, 74, 110), none = C(150, 120, 74, 0);
		dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + e), dark, dark, none, none);
		dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - e), b, none, none, dark, dark);
		dl->AddRectFilledMultiColor(a, ImVec2(a.x + e, b.y), dark, none, none, dark);
		dl->AddRectFilledMultiColor(ImVec2(b.x - e, a.y), b, none, dark, dark, none);
		dl->AddRect(a, b, kParchmentEdge, S(2.0f), 0, S(1.5f));
		dl->PopClipRect();

		++g_parchmentDepth;
		ImGui::PushStyleColor(ImGuiCol_Text, kInk);
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, kInkSoft);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, C(205, 186, 144));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, C(196, 175, 130));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, C(186, 164, 118));
		ImGui::PushStyleColor(ImGuiCol_Border, kParchmentEdge);
		ImGui::PushStyleColor(ImGuiCol_SliderGrab, C(92, 66, 38));
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, C(70, 48, 26));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, C(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, C(120, 92, 58, 140));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, C(100, 74, 44, 200));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, kParchment);
		ImGui::PushStyleColor(ImGuiCol_Header, C(92, 66, 38, 50));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, C(92, 66, 38, 80));
		return visible;
	}

	void EndParchment()
	{
		ImGui::PopStyleColor(14);
		--g_parchmentDepth;
		ImGui::EndChild();
	}

	void SectionLabel(const char* a_text)
	{
		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  w = ImGui::GetContentRegionAvail().x;
		const float  size = S(16.0f);
		const bool   parch = g_parchmentDepth > 0;
		SmallCaps(dl, g_body, size, p, parch ? kInkSoft : kCreamDim, a_text);
		dl->AddLine(ImVec2(p.x, p.y + size + S(7.0f)), ImVec2(p.x + w, p.y + size + S(7.0f)), parch ? C(150, 122, 80, 160) : kGoldFaint, S(1.0f));
		ImGui::Dummy(ImVec2(w, size + S(12.0f)));
	}

	// ------------------------------------------------------------------ controls
	bool Button(const char* a_label, const ImVec2& a_size, bool a_primary)
	{
		const bool parch = g_parchmentDepth > 0;
		ImVec2     size = a_size;
		if (size.y <= 0.0f) {
			size.y = S(46.0f);
		}
		int colors = 0;
		auto push = [&](ImGuiCol a_idx, ImU32 a_col) { ImGui::PushStyleColor(a_idx, a_col); ++colors; };
		if (parch) {
			push(ImGuiCol_Button, a_primary ? C(92, 66, 38) : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, a_primary ? C(112, 82, 48) : C(92, 66, 38, 45));
			push(ImGuiCol_ButtonActive, a_primary ? C(70, 48, 26) : C(92, 66, 38, 80));
			push(ImGuiCol_Text, a_primary ? C(244, 232, 205) : kInk);
			push(ImGuiCol_Border, a_primary ? C(70, 48, 26) : kInkSoft);
		} else {
			push(ImGuiCol_Button, a_primary ? C(122, 96, 56) : C(46, 41, 34));
			push(ImGuiCol_ButtonHovered, a_primary ? C(150, 120, 72) : C(76, 64, 46));
			push(ImGuiCol_ButtonActive, a_primary ? C(170, 138, 84) : C(98, 80, 54));
			push(ImGuiCol_Text, a_primary ? C(250, 240, 218) : kCream);
			push(ImGuiCol_Border, a_primary ? kGold : kGoldDim);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(20.0f), S(10.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.2f));
		const bool pressed = ImGui::Button(a_label, size);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(colors);
		return pressed;
	}

	bool Chip(const char* a_label, bool a_selected)
	{
		const bool parch = g_parchmentDepth > 0;
		int        colors = 0;
		auto push = [&](ImGuiCol a_idx, ImU32 a_col) { ImGui::PushStyleColor(a_idx, a_col); ++colors; };
		if (parch) {
			push(ImGuiCol_Button, a_selected ? C(92, 66, 38) : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, a_selected ? C(108, 78, 46) : C(92, 66, 38, 45));
			push(ImGuiCol_ButtonActive, C(70, 48, 26));
			push(ImGuiCol_Text, a_selected ? C(244, 232, 205) : kInk);
			push(ImGuiCol_Border, a_selected ? C(70, 48, 26) : kInkSoft);
		} else {
			push(ImGuiCol_Button, a_selected ? C(122, 96, 56) : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, C(82, 68, 47));
			push(ImGuiCol_ButtonActive, C(104, 85, 56));
			push(ImGuiCol_Text, kCream);
			push(ImGuiCol_Border, a_selected ? kGold : kGoldDim);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(8.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.2f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(1.0f));
		const bool pressed = ImGui::Button(a_label, ImVec2(0, S(40.0f)));
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(colors);
		return pressed;
	}

	bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_format, float a_width)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(10.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, S(26.0f));
		ImGui::SetNextItemWidth(a_width);
		const bool changed = ImGui::SliderInt(a_label, a_v, a_min, a_max, a_format);
		ImGui::PopStyleVar(2);
		return changed;
	}
}
