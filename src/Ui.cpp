#include "Ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Ui
{
	using Theme::S;

	namespace
	{
		constexpr const char* kTimes = "\xC3\x97";  // multiplication sign
		constexpr const char* kDot = "\xC2\xB7";    // middle dot

		ImVec2 Fit(float w, float h, float maxW, float maxH)
		{
			if (w <= 0 || h <= 0) {
				return { maxW, maxH };
			}
			const float s = (std::min)(maxW / w, maxH / h);
			return { w * s, h * s };
		}

		// Lays chips out left to right, wrapping when the next one would not fit.
		struct Flow
		{
			float right = 0.0f;
			bool  first = true;
			Flow() { right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }
			void Next(const char* a_label)
			{
				const auto& st = ImGui::GetStyle();
				const float w = Theme::ButtonLabelWidth(a_label) + S(14.0f) * 2.0f;
				if (!first) {
					ImGui::SameLine();
					if (ImGui::GetCursorScreenPos().x + w > right) {
						ImGui::NewLine();
					}
				}
				first = false;
				(void)st;
			}
		};

		// Keys like Esc / arrows act on the window only when no text box or dialog has them.
		bool KeysFree()
		{
			return !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		}

		// A file-folder tab: drawn folder icon + label. The selected one is parchment-coloured, joining the panel.
		bool FolderTab(const char* a_label, bool a_selected, float a_h, int a_icon = -1)
		{
			if (Theme::PlaqueTabs()) {
				return Theme::Tab(a_label, a_selected, a_h, a_icon);
			}
			const ImVec2 ts = ImGui::CalcTextSize(a_label);
			const float  icon = S(22.0f), padX = S(16.0f);
			const ImVec2 size(padX * 2 + icon + S(10.0f) + ts.x, a_h);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool   pressed = ImGui::InvisibleButton("##tab", size);
			const bool   hov = ImGui::IsItemHovered();
			auto*        dl = ImGui::GetWindowDrawList();
			const ImVec2 a = p, b = p + ImVec2(size.x, size.y + (a_selected ? S(10.0f) : 0.0f));  // selected runs into the panel
			const float  r = S(6.0f);
			const ImU32  bg = a_selected ? Theme::Parchment() : (hov ? Theme::TabHover() : Theme::TabIdle());
			dl->AddRectFilled(a, b, bg, r, ImDrawFlags_RoundCornersTop);
			dl->AddRect(a, b + ImVec2(0, a_selected ? 2.0f : 0.0f), a_selected ? Theme::GoldDim() : Theme::CardBorder(), r, ImDrawFlags_RoundCornersTop, S(1.2f));
			if (!a_selected) {
				dl->AddLine(ImVec2(a.x + 1, a.y + 1.5f), ImVec2(b.x - 1, a.y + 1.5f), IM_COL32(255, 245, 220, 40), 1.0f);
			}
			// folder glyph
			const ImU32  ink = a_selected ? Theme::Ink() : Theme::Cream();
			const ImVec2 f0(a.x + padX, a.y + (a_h - icon * 0.78f) * 0.5f);
			const ImVec2 f1 = f0 + ImVec2(icon, icon * 0.78f);
			dl->AddRectFilled(f0, ImVec2(f0.x + icon * 0.45f, f0.y + icon * 0.22f), ink, S(2.0f), ImDrawFlags_RoundCornersTop);
			dl->AddRectFilled(ImVec2(f0.x, f0.y + icon * 0.14f), f1, ink, S(2.5f));
			dl->AddRectFilled(ImVec2(f0.x + S(2), f0.y + icon * 0.30f), f1 - ImVec2(S(2), S(2)), a_selected ? Theme::Gold() : Theme::GoldDim(), S(2.0f));
			dl->AddText(ImVec2(f1.x + S(10.0f), a.y + (a_h - ts.y) * 0.5f), a_selected ? Theme::Ink() : Theme::Cream(), a_label);
			return pressed;
		}

		int  g_renameIndex = -1;
		bool g_openRename = false;
		bool g_renameFocus = false;

		void Centered(ImFont* a_font, float a_size, ImU32 a_col, const ImVec2& a_min, const ImVec2& a_max, const char* a_text)
		{
			const ImVec2 ts = a_font->CalcTextSizeA(a_size, FLT_MAX, 0.0f, a_text);
			ImGui::GetWindowDrawList()->AddText(a_font, a_size, (a_min + a_max - ts) * 0.5f, a_col, a_text);
		}

		// ------------------------------------------------------------------ grid
		GalleryResult DrawGrid(GalleryModel& m)
		{
			GalleryResult r;
			if (Theme::Button("Refresh", ImVec2(S(150.0f), 0))) {
				r.action = GalleryAction::Refresh;
			}
			ImGui::SameLine(0.0f, S(8.0f));
			if (Theme::Button("Settings", ImVec2(S(150.0f), 0))) {
				r.action = GalleryAction::OpenSettings;
			}
			if (m.search) {
				ImGui::SameLine(0.0f, S(24.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(12.0f)));
				ImGui::SetNextItemWidth(S(280.0f));
				ImGui::InputTextWithHint("##search", "Search pictures...", m.search, static_cast<size_t>(m.searchSize), ImGuiInputTextFlags_EscapeClearsAll);
				ImGui::PopStyleVar();
				if (m.search[0]) {
					ImGui::SameLine(0.0f, S(6.0f));
					if (Theme::Button("Clear", ImVec2(0, 0))) {
						m.search[0] = 0;
					}
				}
			}
			ImGui::SameLine(0.0f, S(24.0f));
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Thumbnails");
			ImGui::SameLine();
			Theme::SliderInt("##thumbs", m.thumbSize, 100, 320, "%d px", S(260.0f));
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				r.action = GalleryAction::ThumbSize;
			}
			if (m.uiScalePct) {
				ImGui::SameLine(0.0f, S(24.0f));
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("Interface");
				ImGui::SameLine();
				Theme::SliderInt("##uiscale", m.uiScalePct, 60, 200, "%d%%", S(220.0f));
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					r.action = GalleryAction::UiScale;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Size of all Hearthframe windows.\nCtrl + mouse wheel also works in any of them.");
				}
			}
			ImGui::Spacing();

			if (m.shapeFolder && m.shapes) {
				// folder tabs: All / one per shape
				const float tabH = S(46.0f);
				for (int f = 0; f <= m.shapeCount && f < 4; ++f) {
					char label[64];
					std::snprintf(label, sizeof label, "%s  %d", f == 0 ? "All" : m.shapes[f - 1], m.folderCounts[f]);
					if (f > 0) {
						ImGui::SameLine(0.0f, S(8.0f));
					}
					ImGui::PushID(500 + f);
					if (FolderTab(label, *m.shapeFolder == f, tabH, f)) {
						*m.shapeFolder = f;
					}
					ImGui::PopID();
				}
			}
			const int cardStyle = Theme::CardStyle();
			if (cardStyle == 1 || cardStyle == 2) {
				Theme::NextPanelDark();  // the cards bring their own mat: hang them on a dark wall
			}
			if (cardStyle) {
				ImGui::Dummy(ImVec2(0, S(6.0f)));  // plaque tabs sit apart from the panel
			}
			if (Theme::BeginParchment("##grid", ImVec2(0, -Theme::FooterHeight()), true, false, true)) {
				if (m.notice) {
					Theme::TextColoredWrapped(Theme::Warning(), m.notice);
					ImGui::Dummy(ImVec2(0, S(8.0f)));
				}
				if (m.pictures.empty() && m.shapeFolder && *m.shapeFolder > 0 && m.totalPictures > 0 && !(m.search && m.search[0])) {
					Theme::SectionLabel("Empty folder");
					ImGui::TextWrapped("No %s pictures in this folder.", m.shapes[*m.shapeFolder - 1]);
				} else if (m.pictures.empty() && m.search && m.search[0] && m.totalPictures > 0) {
					Theme::SectionLabel("No matches");
					ImGui::TextWrapped("No picture name contains \"%s\".", m.search);
				} else if (m.pictures.empty()) {
					Theme::SectionLabel("No pictures yet");
					std::string msg = "Put .png, .jpg, .bmp or .tga files in this folder, then press Refresh:\n";
					msg += m.folder;
					ImGui::TextWrapped("%s", msg.c_str());
				}
				const float cell = static_cast<float>(*m.thumbSize) * Theme::Scale();
				const float labelH = cardStyle ? Theme::PictureCardHeight(cell) - cell : ImGui::GetTextLineHeight() + Theme::Italic()->FontSize + S(10.0f);
				const float gap = cardStyle == 4 ? S(22.0f) : cardStyle ? S(26.0f) : S(18.0f);
				const float availW = ImGui::GetContentRegionAvail().x;
				const int   cols = (std::max)(1, static_cast<int>((availW + gap) / (cell + gap)));
				auto*       dl = ImGui::GetWindowDrawList();
				// modern look: centre the grid and space rows like columns
				const float indent = cardStyle == 4 ? (std::max)(0.0f, (availW - (cols * cell + (cols - 1) * gap)) * 0.5f) : 0.0f;
				if (cardStyle == 4) {
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
				}
				for (int i = 0; i < static_cast<int>(m.pictures.size()); ++i) {
					auto& pic = m.pictures[i];
					if (i % cols != 0) {
						ImGui::SameLine(0.0f, gap);
					} else if (indent > 0.0f) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
					}
					ImGui::PushID(i);
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const ImVec2 cardSize(cell, cell + labelH);
					if (!ImGui::IsRectVisible(cardSize)) {
						ImGui::Dummy(cardSize);
						ImGui::PopID();
						continue;
					}
					if (m.requestThumb && !pic.failed) {
						m.requestThumb(i);  // also tells the gallery the card is on screen
					}
					const bool clicked = ImGui::InvisibleButton("##pic", cardSize);
					const bool hovered = ImGui::IsItemHovered();
					if (m.renameBuf && ImGui::BeginPopupContextItem("##card")) {
						if (ImGui::Selectable("Open")) {
							r = { GalleryAction::Open, i };
						}
						if (ImGui::Selectable("Rename...")) {
							g_openRename = true;
							g_renameIndex = i;
						}
						ImGui::EndPopup();
					}
					if (cardStyle) {
						const float pw0 = static_cast<float>(pic.thumb ? pic.thumbW : pic.origW);
						const float ph0 = static_cast<float>(pic.thumb ? pic.thumbH : pic.origH);
						char detail[64] = "";
						if (pic.origW > 0) {
							std::snprintf(detail, sizeof detail, "%d %s %d", pic.origW, kTimes, pic.origH);
						}
						Theme::PictureCard(dl, p, cell, pic.thumb, pw0, ph0, pic.name, pic.failed ? "can't read" : "loading...", hovered, detail);
						if (hovered && !ImGui::IsPopupOpen("##card") && (cardStyle != 4 || pic.failed)) {  // modern cards show their name
							if (pic.failed && pic.error && *pic.error) {
								ImGui::SetTooltip("%s\nCan't read: %s\nPress Refresh to try again.", pic.name, pic.error);
							} else {
								ImGui::SetTooltip("%s\n%d %s %d\nRight-click to rename", pic.name, pic.origW, kTimes, pic.origH);
							}
						}
						if (clicked) {
							r = { GalleryAction::Open, i };
						}
						ImGui::PopID();
						if (cardStyle == 2 && i % cols == cols - 1) {
							ImGui::Dummy(ImVec2(0, S(8.0f)));  // room for the plaques hanging below the row
						}
						continue;
					}
					// card shaped like the picture (fitted inside the square slot, on the bottom), so no empty bars
					const float  in = Theme::CardInset();
					ImVec2       cardSz(cell, cell);
					const float  pw = static_cast<float>(pic.thumb ? pic.thumbW : pic.origW);
					const float  ph = static_cast<float>(pic.thumb ? pic.thumbH : pic.origH);
					if (pw > 0 && ph > 0) {
						const ImVec2 inner = Fit(pw, ph, cell - 2 * in, cell - 2 * in);
						cardSz = inner + ImVec2(2 * in, 2 * in);
					}
					const ImVec2 a = p + ImVec2((cell - cardSz.x) * 0.5f, cell - cardSz.y), b = a + cardSz;  // sits on its name
					Theme::CardBack(dl, a, b, hovered);
					if (pic.thumb) {
						dl->AddImage(pic.thumb, a + ImVec2(in, in), b - ImVec2(in, in));
					} else {
						Centered(Theme::Italic(), Theme::Italic()->FontSize, Theme::Muted(), a, b, pic.failed ? "can't read" : "loading...");
					}
					Theme::CardFront(dl, a, b, hovered);
					// caption
					const ImVec2 slotA = p, slotB = p + ImVec2(cell, cell);
					dl->PushClipRect(ImVec2(slotA.x, slotB.y), ImVec2(slotB.x, slotB.y + labelH), true);
					dl->AddText(ImVec2(slotA.x + S(2), slotB.y + S(6)), Theme::Ink(), pic.name);
					char dims[64];
					std::snprintf(dims, sizeof dims, "%d %s %d", pic.origW, kTimes, pic.origH);
					if (pic.origW > 0) {
						dl->AddText(Theme::Italic(), Theme::Italic()->FontSize, ImVec2(slotA.x + S(2), slotB.y + S(6) + ImGui::GetTextLineHeight()), Theme::InkSoft(), dims);
					}
					dl->PopClipRect();
					if (hovered) {
						if (ImGui::IsPopupOpen("##card")) {
						} else if (pic.failed && pic.error && *pic.error) {
							ImGui::SetTooltip("%s\nCan't read: %s\nPress Refresh to try again.", pic.name, pic.error);
						} else {
							ImGui::SetTooltip("%s\n%d %s %d\nRight-click to rename", pic.name, pic.origW, kTimes, pic.origH);
						}
					}
					if (clicked) {
						r = { GalleryAction::Open, i };
					}
					ImGui::PopID();
				}
				if (cardStyle == 4) {
					ImGui::PopStyleVar();
				}
			}
			Theme::EndParchment();
			if (Theme::FooterHeight() > 0.0f) {
				static const char* keys[] = { "LMB", "RMB", "Ctrl+Wheel", "Esc" };
				static const char* labels[] = { "Open", "Rename", "Interface size", "Close" };
				Theme::KeyHints(keys, labels, 4);
			}
			return r;
		}

		// ------------------------------------------------------------------ hang dialog
		// The picture inside a frame preview: picture cropped to fill the cut-out, the frame drawn over it.
		void FramedPicture(ImDrawList* a_dl, const FramePreview& a_f, const Picture& a_pic, const ImVec2& a_a, const ImVec2& a_b)
		{
			const ImVec2 box = a_b - a_a;
			const float  s = (std::min)(box.x / a_f.w, box.y / a_f.h);
			const ImVec2 sz(a_f.w * s, a_f.h * s);
			const ImVec2 o = a_a + (box - sz) * 0.5f;
			const ImVec2 h0 = o + ImVec2(a_f.hx0, a_f.hy0) * s - ImVec2(1, 1), h1 = o + ImVec2(a_f.hx1, a_f.hy1) * s + ImVec2(1, 1);
			if (a_pic.thumb) {
				const float pw = static_cast<float>(a_pic.thumbW), ph = static_cast<float>(a_pic.thumbH);
				ImVec2      uv0(0, 0), uv1(1, 1);
				const float pa = pw / ph, ha = (h1.x - h0.x) / (h1.y - h0.y);
				if (pa > ha) {
					const float k = ha / pa;
					uv0.x = (1 - k) * 0.5f;
					uv1.x = 1 - uv0.x;
				} else {
					const float k = pa / ha;
					uv0.y = (1 - k) * 0.5f;
					uv1.y = 1 - uv0.y;
				}
				a_dl->AddImage(a_pic.thumb, h0, h1, uv0, uv1);
			} else {
				a_dl->AddRectFilled(h0, h1, Theme::Well());
			}
			a_dl->AddRectFilledMultiColor(h0, ImVec2(h1.x, h0.y + (h1.y - h0.y) * 0.25f), IM_COL32(0, 0, 0, 70), IM_COL32(0, 0, 0, 70), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			a_dl->AddImage(a_f.tex, o, o + sz);
		}

		// Hang dialog: a big preview of the chosen frame with the picture in it, and every frame as a tile.
		void DrawFramePicker(GalleryModel& m, const Picture& a_pic, const char* const* a_names, int a_count)
		{
			auto*       dl = ImGui::GetWindowDrawList();
			const float h = S(430.0f);
			const float bigW = S(360.0f);
			// big preview
			{
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(bigW, h));
				const ImVec2 a = p, b = p + ImVec2(bigW, h);
				dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 60), S(10.0f));
				dl->AddRectFilledMultiColor(a + ImVec2(S(10), S(10)), ImVec2(b.x - S(10), a.y + h * 0.6f), IM_COL32(255, 230, 190, 14), IM_COL32(255, 230, 190, 14), IM_COL32(255, 230, 190, 0), IM_COL32(255, 230, 190, 0));
				const FramePreview f = m.framePreview(*m.hangShape, *m.hangStyle);
				if (f.tex) {
					FramedPicture(dl, f, a_pic, a + ImVec2(S(26.0f), S(26.0f)), ImVec2(b.x - S(26.0f), b.y - S(84.0f)));
				}
				const char*  nm = (*m.hangStyle >= 0 && *m.hangStyle < a_count) ? a_names[*m.hangStyle] : "";
				const ImVec2 ns = Theme::Body()->CalcTextSizeA(Theme::Body()->FontSize * 1.15f, FLT_MAX, 0.0f, nm);
				dl->AddText(Theme::Body(), Theme::Body()->FontSize * 1.15f, ImVec2((a.x + b.x - ns.x) * 0.5f, b.y - S(66.0f)), Theme::Cream(), nm);
				char idx[32];
				std::snprintf(idx, sizeof idx, "%d / %d", *m.hangStyle + 1, a_count);
				const ImVec2 is = Theme::Body()->CalcTextSizeA(Theme::Body()->FontSize * 0.85f, FLT_MAX, 0.0f, idx);
				dl->AddText(Theme::Body(), Theme::Body()->FontSize * 0.85f, ImVec2((a.x + b.x - is.x) * 0.5f, b.y - S(36.0f)), Theme::Muted(), idx);
			}
			ImGui::SameLine(0.0f, S(22.0f));
			// tiles
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
			ImGui::BeginChild("##framegrid", ImVec2(0, h), ImGuiChildFlags_None, 0);
			const float gap = S(14.0f);
			const float availW = ImGui::GetContentRegionAvail().x;
			const int   cols = (std::max)(1, static_cast<int>((availW + gap) / (S(150.0f) + gap)));
			const float tw = (availW - gap * (cols - 1)) / cols, th = tw * 0.80f + S(30.0f);
			auto*       gdl = ImGui::GetWindowDrawList();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
			for (int i = 0; i < a_count; ++i) {
				if (i % cols != 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(i);
				const ImVec2 p = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##tile", ImVec2(tw, th))) {
					*m.hangStyle = i;
				}
				const bool hov = ImGui::IsItemHovered(), sel = *m.hangStyle == i;
				if (ImGui::IsRectVisible(p, p + ImVec2(tw, th))) {
					const ImVec2 a = p, b = p + ImVec2(tw, tw * 0.80f);
					gdl->AddRectFilled(a, b, sel ? IM_COL32(255, 230, 180, 22) : (hov ? IM_COL32(255, 255, 255, 14) : IM_COL32(255, 255, 255, 6)), S(8.0f));
					const FramePreview f = m.framePreview(*m.hangShape, i);
					if (f.tex) {
						FramedPicture(gdl, f, a_pic, a + ImVec2(S(12.0f), S(12.0f)), b - ImVec2(S(12.0f), S(12.0f)));
					}
					if (sel) {
						gdl->AddRect(a - ImVec2(S(3), S(3)), b + ImVec2(S(3), S(3)), (Theme::Gold() & 0x00FFFFFF) | (60u << 24), S(10.0f), 0, S(4.0f));
					}
					gdl->AddRect(a, b, sel ? Theme::Gold() : (hov ? (Theme::Gold() & 0x00FFFFFF) | (150u << 24) : IM_COL32(255, 255, 255, 20)), S(8.0f), 0, sel ? S(1.6f) : S(1.0f));
					// name, shortened to fit
					std::string nm = a_names[i];
					ImFont*     font = Theme::Body();
					const float fs = font->FontSize * 0.82f;
					if (font->CalcTextSizeA(fs, FLT_MAX, 0.0f, nm.c_str()).x > tw) {
						while (nm.size() > 1 && font->CalcTextSizeA(fs, FLT_MAX, 0.0f, (nm + "...").c_str()).x > tw) {
							nm.pop_back();
						}
						nm += "...";
					}
					const ImVec2 ns = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, nm.c_str());
					gdl->AddText(font, fs, ImVec2(a.x + (tw - ns.x) * 0.5f, b.y + S(8.0f)), sel ? Theme::Gold() : (hov ? Theme::Cream() : Theme::Muted()), nm.c_str());
					if (hov && nm != a_names[i]) {
						ImGui::SetTooltip("%s", a_names[i]);
					}
				}
				ImGui::PopID();
			}
			ImGui::PopStyleVar();
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		void DrawHangDialog(GalleryModel& m, GalleryResult& r)
		{
			const auto& pic = m.pictures[m.viewing];
			const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // stays centred as its height changes
			ImGui::SetNextWindowSize(ImVec2(S(m.framePreview ? 1180.0f : 980.0f), 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
			const bool open = ImGui::BeginPopupModal("Hang on wall", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar();
			if (!open) {
				return;
			}
			Theme::DrawChrome();
			if (Theme::Header("Hang on Wall", pic.name) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ImGui::CloseCurrentPopup();
			}
			const int best = m.bestShape ? m.bestShape(pic.origW, pic.origH) : 0;
			if (Theme::BeginParchment("##hang", ImVec2(0, 0), false, true)) {
				Theme::SectionLabel("Shape");
				{
					Flow flow;
					for (int i = 0; i < m.shapeCount; ++i) {
						std::string label = m.shapes[i];
						if (i == best) {
							label += "  (best fit)";
						}
						flow.Next(label.c_str());
						ImGui::PushID(100 + i);
						if (Theme::Chip(label.c_str(), *m.hangShape == i)) {
							*m.hangShape = i;
						}
						ImGui::PopID();
					}
				}
				ImGui::Dummy(ImVec2(0, S(6.0f)));
				Theme::SectionLabel("Frame");
				if (m.stylesFor) {
					const auto [names, count] = m.stylesFor(*m.hangShape);
					*m.hangStyle = std::clamp(*m.hangStyle, 0, (std::max)(0, count - 1));
					const bool previews = m.framePreview && m.framePreview(*m.hangShape, 0).tex;
					if (previews) {
						DrawFramePicker(m, pic, names, count);
					}
					// long lists scroll so the dialog stays on screen
					const bool scroll = !previews && count > 12;
					if (scroll) {
						ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
						ImGui::BeginChild("##frames", ImVec2(0, S(260.0f)), ImGuiChildFlags_None, 0);
					}
					Flow flow;
					for (int i = 0; i < count && !previews; ++i) {
						flow.Next(names[i]);
						ImGui::PushID(i);
						if (Theme::Chip(names[i], *m.hangStyle == i)) {
							*m.hangStyle = i;
						}
						ImGui::PopID();
					}
					if (scroll) {
						ImGui::EndChild();
						ImGui::PopStyleColor();
					}
				}
				ImGui::Dummy(ImVec2(0, S(4.0f)));
				if (m.textureSize) {
					const auto [tw, th] = m.textureSize(*m.hangShape);
					Theme::TextItalic("The picture is cropped to fill the frame %s %d %s %d texture", kDot, tw, kTimes, th);
				}
			}
			Theme::EndParchment();
			ImGui::Dummy(ImVec2(0, S(6.0f)));
			const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (Theme::Button("Hang it", ImVec2(bw, S(52.0f)), true)) {
				r = { GalleryAction::Hang, m.viewing };
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (Theme::Button("Cancel", ImVec2(bw, S(52.0f)))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// ------------------------------------------------------------------ change frame (same shape)
		bool g_choosingFrame = false;  // the frame menu shows the frame list (docked low so the frame stays visible)

		void DrawChangeFrameBody(FrameModel& m, FrameAction& act)
		{
			std::string sub = std::string(m.shape) + " frames  " + kDot + "  the picture stays";
			if (Theme::Header("Change Frame", sub.c_str())) {
				g_choosingFrame = false;
			}
			const bool strip = m.framePreview && m.framePreview(0).tex;
			if (strip && Theme::BeginParchment("##change", ImVec2(0, 0), false, true)) {
				// one row of frames, each showing this picture; the wheel scrolls it sideways
				Picture pic;
				pic.thumb = m.preview;
				pic.thumbW = m.previewW;
				pic.thumbH = m.previewH;
				const float tw = S(168.0f), th = tw * 0.80f, rowH = th + S(34.0f);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(10.0f));
				ImGui::BeginChild("##strip", ImVec2(0, rowH + S(16.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
				if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
					ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * (tw + S(14.0f)));
				}
				static int scrolledTo = -1;  // bring the current frame into view when the strip opens
				if (scrolledTo != m.currentStyle) {
					ImGui::SetScrollX((std::max)(0.0f, m.currentStyle * (tw + S(14.0f)) - S(300.0f)));
					scrolledTo = m.currentStyle;
				}
				auto* dl = ImGui::GetWindowDrawList();
				ImGui::BeginDisabled(m.changing);
				for (int i = 0; i < m.styleCount; ++i) {
					if (i > 0) {
						ImGui::SameLine(0.0f, S(14.0f));
					}
					ImGui::PushID(i);
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const bool   cur = i == m.currentStyle;
					if (ImGui::InvisibleButton("##tile", ImVec2(tw, rowH)) && !cur) {
						if (m.chosenStyle) {
							*m.chosenStyle = i;
						}
						act = FrameAction::ChangeFrame;
					}
					const bool hov = ImGui::IsItemHovered();
					if (ImGui::IsRectVisible(p, p + ImVec2(tw, rowH))) {
						const ImVec2 a = p, b = p + ImVec2(tw, th);
						dl->AddRectFilled(a, b, cur ? IM_COL32(255, 230, 180, 22) : (hov ? IM_COL32(255, 255, 255, 14) : IM_COL32(255, 255, 255, 6)), S(8.0f));
						const FramePreview f = m.framePreview(i);
						if (f.tex) {
							FramedPicture(dl, f, pic, a + ImVec2(S(12.0f), S(12.0f)), b - ImVec2(S(12.0f), S(12.0f)));
						}
						if (cur) {
							dl->AddRect(a - ImVec2(S(3), S(3)), b + ImVec2(S(3), S(3)), (Theme::Gold() & 0x00FFFFFF) | (60u << 24), S(10.0f), 0, S(4.0f));
						}
						dl->AddRect(a, b, cur ? Theme::Gold() : (hov ? (Theme::Gold() & 0x00FFFFFF) | (150u << 24) : IM_COL32(255, 255, 255, 20)), S(8.0f), 0, cur ? S(1.6f) : S(1.0f));
						if (cur) {  // "current" badge
							const char*  tag = "CURRENT";
							ImFont*      f2 = Theme::Body();
							const float  fs = f2->FontSize * 0.62f;
							const ImVec2 ts = f2->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag);
							const ImVec2 ba(a.x + S(8.0f), a.y + S(8.0f)), bb = ba + ts + ImVec2(S(12.0f), S(6.0f));
							dl->AddRectFilled(ba, bb, Theme::Gold(), S(4.0f));
							dl->AddText(f2, fs, ba + ImVec2(S(6.0f), S(3.0f)), IM_COL32(26, 21, 14, 255), tag);
						}
						std::string nm = m.styles[i];
						ImFont*     font = Theme::Body();
						const float fs = font->FontSize * 0.82f;
						if (font->CalcTextSizeA(fs, FLT_MAX, 0.0f, nm.c_str()).x > tw) {
							while (nm.size() > 1 && font->CalcTextSizeA(fs, FLT_MAX, 0.0f, (nm + "...").c_str()).x > tw) {
								nm.pop_back();
							}
							nm += "...";
						}
						const ImVec2 ns = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, nm.c_str());
						dl->AddText(font, fs, ImVec2(a.x + (tw - ns.x) * 0.5f, b.y + S(8.0f)), cur ? Theme::Gold() : (hov ? Theme::Cream() : Theme::Muted()), nm.c_str());
						if (hov && nm != m.styles[i]) {
							ImGui::SetTooltip("%s", m.styles[i]);
						}
					}
					ImGui::PopID();
				}
				ImGui::EndDisabled();
				ImGui::EndChild();
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
			}
			if (strip) {
				Theme::EndParchment();
			}
			if (!strip && Theme::BeginParchment("##change", ImVec2(0, 0), false, true)) {
				Theme::SectionLabel(m.style);
				const bool scroll = m.styleCount > 12;
				if (scroll) {
					ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
					ImGui::BeginChild("##frames", ImVec2(0, S(210.0f)), ImGuiChildFlags_None, 0);
				}
				Flow flow;
				ImGui::BeginDisabled(m.changing);
				for (int i = 0; i < m.styleCount; ++i) {
					flow.Next(m.styles[i]);
					ImGui::PushID(i);
					if (Theme::Chip(m.styles[i], i == m.currentStyle) && i != m.currentStyle) {
						if (m.chosenStyle) {
							*m.chosenStyle = i;
						}
						act = FrameAction::ChangeFrame;
					}
					ImGui::PopID();
				}
				ImGui::EndDisabled();
				if (scroll) {
					ImGui::EndChild();
					ImGui::PopStyleColor();
				}
			}
			if (!strip) {
				Theme::EndParchment();
			}
			ImGui::Dummy(ImVec2(0, S(6.0f)));
			if (Theme::Button("Done", ImVec2(ImGui::GetContentRegionAvail().x, S(52.0f)), true)) {
				g_choosingFrame = false;
			}
		}

		// ------------------------------------------------------------------ viewer
		GalleryResult DrawViewer(GalleryModel& m)
		{
			GalleryResult r;
			const auto&   pic = m.pictures[m.viewing];
			const ImVec2  nav(S(130.0f), 0);
			const bool    keys = KeysFree();
			if (Theme::Button("Back", nav) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
				r.action = GalleryAction::Back;
			}
			ImGui::SameLine();
			if (Theme::Button("Previous", nav) || (keys && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))) {
				r.action = GalleryAction::Prev;
			}
			ImGui::SameLine();
			if (Theme::Button("Next", nav) || (keys && ImGui::IsKeyPressed(ImGuiKey_RightArrow))) {
				r.action = GalleryAction::Next;
			}
			if (m.renameBuf) {
				ImGui::SameLine(0.0f, S(24.0f));
				if (Theme::Button("Rename", nav)) {
					g_openRename = true;
					g_renameIndex = m.viewing;
				}
			}
			// primary action on the right
			const char* primary = m.replaceMode ? "Use for this frame" : "Hang on wall";
			const float pw = ImGui::CalcTextSize(primary).x + S(20.0f) * 2.0f + S(24.0f);
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - pw);
			if (Theme::Button(primary, ImVec2(pw, 0), true)) {
				r = { m.replaceMode ? GalleryAction::UseForFrame : GalleryAction::OpenHangDialog, m.viewing };
			}
			ImGui::Spacing();
			ImGui::TextUnformatted(pic.name);
			ImGui::SameLine();
			Theme::TextItalic("  %d %s %d   %s   %d of %d", pic.origW, kTimes, pic.origH, kDot, m.viewing + 1, static_cast<int>(m.pictures.size()));

			// picture on a dark inset
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ViewerBg());
			ImGui::BeginChild("##view", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleColor();
			{
				auto*        dl = ImGui::GetWindowDrawList();
				const ImVec2 a = ImGui::GetWindowPos(), b = a + ImGui::GetWindowSize();
				dl->AddRect(a, b, Theme::GoldDim(), 0.0f, 0, S(1.0f));
				const ImTextureID tex = m.full ? m.full : pic.thumb;
				if (tex) {
					const float  tw = static_cast<float>(m.full ? m.fullW : pic.thumbW);
					const float  th = static_cast<float>(m.full ? m.fullH : pic.thumbH);
					const ImVec2 sz = Fit(tw, th, b.x - a.x - S(24), b.y - a.y - S(24));
					const ImVec2 ia = (a + b - sz) * 0.5f;
					dl->AddImage(tex, ia, ia + sz);
				} else {
					Centered(Theme::Italic(), Theme::Italic()->FontSize, Theme::Muted(), a, b,
						m.fullFailed ? "This picture could not be read." : "Loading...");
				}
			}
			ImGui::EndChild();
			return r;
		}

		// ------------------------------------------------------------------ placed pictures
		void MapPin(ImDrawList* a_dl, const ImVec2& a_pos, float a_size, ImU32 a_col)
		{
			const float  r = a_size * 0.36f;
			const ImVec2 c(a_pos.x + a_size * 0.5f, a_pos.y + r + a_size * 0.04f);
			a_dl->AddTriangleFilled(ImVec2(c.x - r * 0.86f, c.y + r * 0.5f), ImVec2(c.x + r * 0.86f, c.y + r * 0.5f), ImVec2(c.x, a_pos.y + a_size), a_col);
			a_dl->AddCircleFilled(c, r, a_col, 20);
			a_dl->AddCircleFilled(c, r * 0.42f, Theme::Parchment(), 16);
		}

		int g_confirmDelete = -1;  // placed picture whose Delete was clicked once

		void DrawPlacedDialog(GalleryModel& m, GalleryResult& r)
		{
			const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2((std::min)(S(1180.0f), ImGui::GetIO().DisplaySize.x * 0.95f), 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
			const bool open = ImGui::BeginPopupModal("Placed pictures", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar();
			if (!open) {
				g_confirmDelete = -1;
				return;
			}
			Theme::DrawChrome();
			int strays = 0;
			for (const auto& pp : m.placed) {
				strays += pp.stray ? 1 : 0;
			}
			const int pictures = static_cast<int>(m.placed.size()) - strays;
			char      sub[96];
			std::snprintf(sub, sizeof sub, "%d picture%s on walls in this save", pictures, pictures == 1 ? "" : "s");
			if (Theme::Header("Placed Pictures", sub) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ImGui::CloseCurrentPopup();
			}
			static bool confirmStrays = false;
			if (strays > 0) {
				ImGui::AlignTextToFramePadding();
				Theme::TextItalic("%d stray frame%s without a picture (left over from an earlier session)", strays, strays == 1 ? "" : "s");
				const char* label = confirmStrays ? "Click again to remove them" : "Remove stray frames";
				const float bw = ImGui::CalcTextSize(label).x + S(48.0f);
				ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
				if (Theme::Button(label, ImVec2(bw, S(40.0f)), confirmStrays)) {
					if (confirmStrays) {
						r = { GalleryAction::RemoveStrays, -1 };
					}
					confirmStrays = !confirmStrays;
				}
				ImGui::Dummy(ImVec2(0, S(4.0f)));
			} else {
				confirmStrays = false;
			}
			const float listH = (std::min)(S(640.0f), ImGui::GetIO().DisplaySize.y * 0.66f);
			if (Theme::BeginParchment("##placed", ImVec2(0, listH), true)) {
				if (m.placedLoading) {
					Theme::TextItalic("Looking for your pictures...");
				} else if (m.placed.empty()) {
					Theme::SectionLabel("Nothing on the walls yet");
					ImGui::TextWrapped("Open a picture and use Hang on wall to put it up.");
				}
				auto*       dl = ImGui::GetWindowDrawList();
				const float cell = S(230.0f);
				const float lh = ImGui::GetTextLineHeight(), ih = Theme::Italic()->FontSize;
				const float textH = S(8.0f) + lh + S(2.0f) + ih + S(10.0f);
				const float btnH = S(40.0f);
				const float gap = S(22.0f);
				const float availW = ImGui::GetContentRegionAvail().x;
				const int   cols = (std::max)(1, static_cast<int>((availW + gap) / (cell + gap)));
				for (int i = 0; i < static_cast<int>(m.placed.size()); ++i) {
					const auto& pp = m.placed[i];
					if (i % cols != 0) {
						ImGui::SameLine(0.0f, gap);
					} else if (i > 0) {
						ImGui::Dummy(ImVec2(0, S(6.0f)));
					}
					ImGui::PushID(i);
					ImGui::BeginGroup();
					const ImVec2 p = ImGui::GetCursorScreenPos();
					// picture, shaped like its frame, sitting on its name
					const float  in = Theme::CardInset();
					const float  ar = pp.canvasAspect > 0.0f ? pp.canvasAspect : 1.0f;
					ImVec2       inner = Fit(ar, 1.0f, cell - 2 * in, cell - 2 * in);
					const ImVec2 cardSz = inner + ImVec2(2 * in, 2 * in);
					const ImVec2 a = p + ImVec2((cell - cardSz.x) * 0.5f, cell - cardSz.y), b = a + cardSz;
					Theme::CardBack(dl, a, b, false);
					if (pp.thumb && pp.thumbW > 0 && pp.thumbH > 0) {
						const float ia = static_cast<float>(pp.thumbW) / pp.thumbH;
						ImVec2      uv0(0, 0), uv1(1, 1);
						if (ia > ar) {
							uv0.x = (1.0f - ar / ia) * 0.5f;
							uv1.x = 1.0f - uv0.x;
						} else {
							uv0.y = (1.0f - ia / ar) * 0.5f;
							uv1.y = 1.0f - uv0.y;
						}
						dl->AddImage(pp.thumb, a + ImVec2(in, in), b - ImVec2(in, in), uv0, uv1);
					} else {
						Centered(Theme::Italic(), ih, Theme::Muted(), a, b, pp.stray ? "no picture" : (pp.missing ? "picture file not found" : "loading..."));
					}
					Theme::CardFront(dl, a, b, false);
					ImGui::Dummy(ImVec2(cell, cell));
					// name + where
					const ImVec2 t = ImGui::GetCursorScreenPos();
					dl->PushClipRect(t, t + ImVec2(cell, textH), true);
					dl->AddText(ImVec2(t.x + S(2.0f), t.y + S(6.0f)), pp.stray ? Theme::InkSoft() : Theme::Ink(), pp.stray ? "Stray frame (no picture)" : pp.name);
					const std::string where = std::string(pp.place) + (*pp.region && std::strcmp(pp.region, pp.place) != 0 ? std::string(", ") + pp.region : std::string()) +
					                          (pp.interior ? "  (interior)" : "  (exterior)");
					MapPin(dl, ImVec2(t.x + S(2.0f), t.y + S(8.0f) + lh + S(2.0f)), ih, Theme::InkSoft());
					dl->AddText(Theme::Italic(), ih, ImVec2(t.x + S(4.0f) + ih, t.y + S(8.0f) + lh), Theme::InkSoft(), where.c_str());
					dl->PopClipRect();
					ImGui::Dummy(ImVec2(cell, textH));
					if (ImGui::IsMouseHoveringRect(t, t + ImVec2(cell, textH))) {
						ImGui::SetTooltip("%s\n%s\n%s frame  %s  %s  %s  %d%%", pp.name, where.c_str(), pp.frame, kDot, pp.shape, kDot, pp.sizePct);
					}
					// delete, with a second click to confirm
					const bool confirming = g_confirmDelete == i;
					if (Theme::Button(confirming ? "Click again to delete" : "Delete", ImVec2(cell, btnH), confirming)) {
						if (confirming) {
							r = { GalleryAction::DeletePlaced, i };
							g_confirmDelete = -1;
						} else {
							g_confirmDelete = i;
						}
					}
					ImGui::EndGroup();
					ImGui::PopID();
				}
			}
			Theme::EndParchment();
			ImGui::Dummy(ImVec2(0, S(6.0f)));
			if (Theme::Button("Done", ImVec2(ImGui::GetContentRegionAvail().x, S(52.0f)), true)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// ------------------------------------------------------------------ rename
		void DrawRenameDialog(GalleryModel& m, GalleryResult& r)
		{
			if (!m.renameBuf) {
				return;
			}
			const int n = static_cast<int>(m.pictures.size());
			if (g_openRename && g_renameIndex >= 0 && g_renameIndex < n) {
				g_openRename = false;
				const std::string name = m.pictures[g_renameIndex].name;
				const auto        dot = name.find_last_of('.');
				const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
				std::snprintf(m.renameBuf, static_cast<size_t>(m.renameSize), "%s", stem.c_str());
				g_renameFocus = true;
				r = { GalleryAction::RenameOpened, g_renameIndex };
				ImGui::OpenPopup("Rename picture");
			}
			g_openRename = false;
			const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(S(760.0f), 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
			const bool open = ImGui::BeginPopupModal("Rename picture", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar();
			if (!open) {
				return;
			}
			if (g_renameIndex < 0 || g_renameIndex >= n || m.renameDone) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
			const std::string name = m.pictures[g_renameIndex].name;
			const auto        dot = name.find_last_of('.');
			const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
			Theme::DrawChrome();
			bool cancel = Theme::Header("Rename Picture", name.c_str());
			bool submit = false;
			if (Theme::BeginParchment("##rename", ImVec2(0, 0), false, true)) {
				Theme::SectionLabel("New name");
				const float extW = ImGui::CalcTextSize(ext.c_str()).x + S(12.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(12.0f)));
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - extW);
				if (g_renameFocus) {
					ImGui::SetKeyboardFocusHere();
					g_renameFocus = false;
				}
				submit = ImGui::InputText("##newname", m.renameBuf, static_cast<size_t>(m.renameSize), ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::PopStyleVar();
				ImGui::SameLine(0.0f, S(6.0f));
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(ext.c_str());
				ImGui::Dummy(ImVec2(0, S(4.0f)));
				if (m.renameError && *m.renameError) {
					Theme::TextColoredWrapped(Theme::Warning(), m.renameError);
				} else {
					Theme::TextItalic("Hung copies of this picture follow the new name.");
				}
			}
			Theme::EndParchment();
			ImGui::Dummy(ImVec2(0, S(6.0f)));
			const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (Theme::Button("Rename", ImVec2(bw, S(52.0f)), true)) {
				submit = true;
			}
			ImGui::SameLine();
			if (Theme::Button("Cancel", ImVec2(bw, S(52.0f))) || (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
				cancel = true;
			}
			if (submit) {
				r = { GalleryAction::Rename, g_renameIndex };
			}
			if (cancel) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	GalleryResult DrawGallery(GalleryModel& m)
	{
		auto& io = ImGui::GetIO();
		// The window follows the interface size: first open at a size made from it, and when it changes,
		// resize by the same factor around the window's centre.
		static float  lastScale = 0.0f;
		static ImVec2 lastSize, lastCenter;
		const float   scale = Theme::Scale();
		auto          clampToScreen = [&](ImVec2 a_size) {
            return ImVec2((std::min)(a_size.x, io.DisplaySize.x * 0.98f), (std::min)(a_size.y, io.DisplaySize.y * 0.98f));
		};
		if (lastScale <= 0.0f) {
			ImGui::SetNextWindowSize(clampToScreen(ImVec2(S(1380.0f), S(840.0f))), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		} else if (std::fabs(scale - lastScale) > 0.001f) {
			const ImVec2 size = clampToScreen(lastSize * (scale / lastScale));
			ImGui::SetNextWindowSize(size, ImGuiCond_Always);
			ImGui::SetNextWindowPos(lastCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		}
		ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(S(760.0f), io.DisplaySize.x), (std::min)(S(520.0f), io.DisplaySize.y)), io.DisplaySize);
		GalleryResult r;
		Theme::SetBackdrop();  // modern look: dim the game behind the gallery
		const bool visible = Theme::BeginWindow("Hearthframe", nullptr);
		lastScale = scale;
		lastSize = ImGui::GetWindowSize();
		lastCenter = ImGui::GetWindowPos() + lastSize * 0.5f;
		if (visible) {
			char pics[16], walls[16];
			if (m.search && m.search[0]) {
				std::snprintf(pics, sizeof pics, "%d / %d", static_cast<int>(m.pictures.size()), m.totalPictures);
			} else {
				std::snprintf(pics, sizeof pics, "%d", static_cast<int>(m.pictures.size()));
			}
			std::snprintf(walls, sizeof walls, "%d", m.framesOnWalls);
			const Theme::Stat stats[] = { { "Pictures", pics }, { "On walls", walls, "Show the pictures hanging in this save, and where" } };
			const char*       subtitle = m.replaceMode ? "Choose a new picture for the frame" : m.folder;
			int               clicked = -1;
			bool              openFolder = false;
			if (!m.replaceMode && m.openFolderStyle > 0) {
				Theme::SetSubtitleAction(m.openFolderStyle, &openFolder, "Open this folder in Windows Explorer");
			}
			if (Theme::Header("Hearthframe", subtitle, stats, 2, true, &clicked)) {
				r.action = GalleryAction::Close;
			}
			if (clicked == 1) {
				r.action = GalleryAction::OpenPlaced;
				ImGui::OpenPopup("Placed pictures");
			}
			if (openFolder) {
				r.action = GalleryAction::OpenFolder;
			}
			if (m.viewing >= 0 && m.viewing < static_cast<int>(m.pictures.size())) {
				const auto v = DrawViewer(m);
				if (v.action != GalleryAction::None) {
					r = v;
				}
				if (m.openHangDialog) {
					m.openHangDialog = false;
					ImGui::OpenPopup("Hang on wall");
				}
				if (r.action == GalleryAction::OpenHangDialog) {
					const auto& pic = m.pictures[m.viewing];
					if (m.bestShape && m.hangShape) {
						*m.hangShape = m.bestShape(pic.origW, pic.origH);  // preselect the best-fitting shape
					}
					ImGui::OpenPopup("Hang on wall");
				}
				DrawHangDialog(m, r);
			} else {
				if (KeysFree() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
					if (m.search && m.search[0]) {
						m.search[0] = 0;  // first Esc clears the search
					} else {
						r.action = GalleryAction::Close;
					}
				}
				const auto g = DrawGrid(m);
				if (g.action != GalleryAction::None) {
					r = g;
				}
			}
			DrawRenameDialog(m, r);
			DrawPlacedDialog(m, r);
		}
		Theme::EndWindow();
		return r;
	}

	void ResetFrameMenu() { g_choosingFrame = false; }

	FrameAction DrawFrameMenu(FrameModel& m)
	{
		FrameAction act = FrameAction::None;
		auto&       io = ImGui::GetIO();
		if (g_choosingFrame) {
			// bottom of the screen, so the frame being changed stays in view above it
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - S(40.0f)), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2((std::min)(S(980.0f), io.DisplaySize.x - S(40.0f)), 0));
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				g_choosingFrame = false;
			}
			if (Theme::BeginWindow("Change frame", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
				DrawChangeFrameBody(m, act);
			}
			Theme::EndWindow();
			return act;
		}
		ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(S(640.0f), 0));
		(void)io;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			act = FrameAction::Close;
		}
		if (Theme::BeginWindow("Framed picture", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
			std::string sub = std::string(m.style) + " frame  " + kDot + "  " + m.shape;
			if (Theme::Header("Framed Picture", sub.c_str())) {
				act = FrameAction::Close;
			}
			if (Theme::BeginParchment("##frame", ImVec2(0, 0), false, true)) {
				Theme::SectionLabel("Picture");
				if (*m.picture) {
					// preview cropped to the frame's shape, like it looks on the wall
					const float  maxH = S(220.0f), maxW = ImGui::GetContentRegionAvail().x;
					float        h = maxH, w = maxH * m.canvasAspect;
					if (w > maxW) {
						w = maxW;
						h = w / m.canvasAspect;
					}
					const ImVec2 pos = ImGui::GetCursorScreenPos() + ImVec2((maxW - w) * 0.5f, 0.0f);
					const ImVec2 a = pos, b = pos + ImVec2(w, h);
					auto*        dl = ImGui::GetWindowDrawList();
					Theme::CardBack(dl, a, b, false);
					const float in = Theme::CardInset();
					if (m.preview && m.previewW > 0 && m.previewH > 0) {
						const float ia = static_cast<float>(m.previewW) / m.previewH;
						const float ta = (w - 2 * in) / (h - 2 * in);
						ImVec2      uv0(0, 0), uv1(1, 1);
						if (ia > ta) {
							const float k = ta / ia;
							uv0.x = (1.0f - k) * 0.5f;
							uv1.x = 1.0f - uv0.x;
						} else {
							const float k = ia / ta;
							uv0.y = (1.0f - k) * 0.5f;
							uv1.y = 1.0f - uv0.y;
						}
						dl->AddImage(m.preview, a + ImVec2(in, in), b - ImVec2(in, in), uv0, uv1);
					} else {
						Centered(Theme::Italic(), Theme::Italic()->FontSize, Theme::Muted(), a, b,
							m.previewMissing ? "picture file not found" : (m.previewLoading ? "loading..." : "no preview"));
					}
					Theme::CardFront(dl, a, b, false);
					ImGui::Dummy(ImVec2(maxW, h + S(8.0f)));
				}
				ImGui::TextWrapped("%s", *m.picture ? m.picture : "(unknown - hung some other way)");
				ImGui::Dummy(ImVec2(0, S(6.0f)));
				Theme::SectionLabel("Size");
				if (Theme::SliderInt("##size", m.sizePct, 30, 300, "%d%%", ImGui::GetContentRegionAvail().x)) {
					act = FrameAction::SizeChanged;
				}
				ImGui::Dummy(ImVec2(0, S(6.0f)));
				Theme::SectionLabel("Distance from wall");
				const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
				ImGui::PushButtonRepeat(true);  // hold to keep moving
				if (Theme::Button("Closer", ImVec2(half, 0))) {
					act = FrameAction::Closer;
				}
				ImGui::SameLine();
				if (Theme::Button("Further", ImVec2(half, 0))) {
					act = FrameAction::Further;
				}
				ImGui::PopButtonRepeat();
			}
			Theme::EndParchment();
			ImGui::Dummy(ImVec2(0, S(6.0f)));
			const float  half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			const ImVec2 big(half, S(54.0f));
			// Change frame | Change picture
			// Move         | Take down
			ImGui::BeginDisabled(m.styleCount < 2);
			if (Theme::Button("Change frame", big)) {
				g_choosingFrame = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (Theme::Button("Change picture", big, true)) {
				act = FrameAction::ChangePicture;
			}
			ImGui::BeginDisabled(!m.canMove);
			if (Theme::Button("Move", big)) {
				act = FrameAction::Move;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (Theme::Button("Take down", big)) {
				act = FrameAction::TakeDown;
			}
		}
		Theme::EndWindow();
		return act;
	}

	void DrawPlacementInfo(const char* a_values, const char* a_keys)
	{
		auto*        dl = ImGui::GetForegroundDrawList();
		const auto&  io = ImGui::GetIO();
		ImFont*      body = Theme::Body();
		ImFont*      ital = Theme::Italic();
		const float  s1 = body->FontSize, s2 = ital->FontSize * 0.9f;
		const ImVec2 t1 = body->CalcTextSizeA(s1, FLT_MAX, 0.0f, a_values);
		const ImVec2 t2 = ital->CalcTextSizeA(s2, FLT_MAX, 0.0f, a_keys);
		const float  padX = S(22.0f), padY = S(10.0f), gap = S(4.0f);
		const float  w = (std::max)(t1.x, t2.x) + padX * 2, h = t1.y + t2.y + gap + padY * 2;
		const ImVec2 a(std::round((io.DisplaySize.x - w) * 0.5f), std::round(io.DisplaySize.y - h - S(70.0f)));
		const ImVec2 b = a + ImVec2(w, h);
		dl->AddRectFilled(a + ImVec2(S(2), S(3)), b + ImVec2(S(2), S(3)), IM_COL32(0, 0, 0, 90), S(4.0f));
		dl->AddRectFilled(a, b, IM_COL32(24, 22, 20, 210), S(4.0f));
		dl->AddRect(a, b, Theme::GoldDim(), S(4.0f), 0, S(1.2f));
		dl->AddText(body, s1, ImVec2(a.x + (w - t1.x) * 0.5f, a.y + padY), Theme::Cream(), a_values);
		dl->AddText(ital, s2, ImVec2(a.x + (w - t2.x) * 0.5f, a.y + padY + t1.y + gap), Theme::Muted(), a_keys);
	}

	void DrawPlacementHud(const PlacementHud& a_hud)
	{
		if (!Theme::IsModern()) {
			char values[256];
			std::snprintf(values, sizeof values, "%s  %s  %s  %s  Size %d%%  %s  Distance %+.2f", a_hud.frame, kDot, a_hud.shape, kDot, a_hud.sizePct, kDot, a_hud.distance);
			DrawPlacementInfo(values, "Click: hang   \xC2\xB7   Wheel: size   \xC2\xB7   Shift+Wheel: frame   \xC2\xB7   Up/Down: distance   \xC2\xB7   Right-click / Esc: cancel");
			return;
		}
		// smoked-glass bar: frame + shape on the left, size and distance as valueSz numbers, key caps below
		auto*       dl = ImGui::GetForegroundDrawList();
		const auto& io = ImGui::GetIO();
		ImFont*     body = Theme::Body();
		static const char* keysAll[] = { "LMB", "Wheel/Left/Right", "Shift+Wheel", "Up/Down", "RMB/Esc" };
		static const char* keysArrows[] = { "LMB", "Left/Right", "Shift+Left/Right", "Up/Down", "RMB/Esc" };
		static const char* keysWheel[] = { "LMB", "Wheel", "Shift+Wheel", "Up/Down", "RMB/Esc" };
		static const char* labels[] = { "Hang", "Size", "Frame", "Distance", "Cancel" };
		const char* const* keys = a_hud.resizeWith == 1 ? keysArrows : a_hud.resizeWith == 2 ? keysWheel : keysAll;
		const float        keysW = Theme::KeyHintsAt(dl, ImVec2(0, 0), keys, labels, 5, false);
		const float        padX = S(26.0f), padY = S(18.0f);
		const float        w = (std::max)(keysW, S(560.0f)) + padX * 2, h = S(128.0f);
		const ImVec2       a(std::round((io.DisplaySize.x - w) * 0.5f), std::round(io.DisplaySize.y - h - S(60.0f)));
		const ImVec2       b = a + ImVec2(w, h);
		const float        r = S(12.0f);
		for (int i = 6; i >= 1; --i) {  // soft shadow
			dl->AddRectFilled(a + ImVec2(0, S(6)) - ImVec2(S(3.0f * i), S(3.0f * i)), b + ImVec2(0, S(6)) + ImVec2(S(3.0f * i), S(3.0f * i)), IM_COL32(0, 0, 0, 14), r + S(3.0f * i));
		}
		dl->AddRectFilled(a, b, IM_COL32(14, 14, 16, 225), r);
		dl->AddRect(a, b, IM_COL32(255, 255, 255, 26), r, 0, S(1.0f));
		const float  mid = (a.x + b.x) * 0.5f, half = w * 0.3f;
		const ImU32  acc = Theme::Gold(), clear = Theme::Gold() & 0x00FFFFFF;
		dl->AddRectFilledMultiColor(ImVec2(mid - half, a.y), ImVec2(mid, a.y + S(1.5f)), clear, acc, acc, clear);
		dl->AddRectFilledMultiColor(ImVec2(mid, a.y), ImVec2(mid + half, a.y + S(1.5f)), acc, clear, clear, acc);
		// left: what is being hung
		const float  valueSz = body->FontSize * 1.25f, labelSz = body->FontSize * 0.78f;
		const ImVec2 lp(a.x + padX, a.y + padY);
		dl->AddText(body, labelSz, lp, Theme::Gold(), "HANGING");
		dl->AddText(body, valueSz, lp + ImVec2(0, labelSz + S(4.0f)), Theme::Cream(), a_hud.frame);
		const float fw = body->CalcTextSizeA(valueSz, FLT_MAX, 0.0f, a_hud.frame).x;
		dl->AddText(body, body->FontSize * 0.9f, lp + ImVec2(fw + S(12.0f), labelSz + S(4.0f) + (valueSz - body->FontSize * 0.9f) * 0.75f), Theme::Muted(), a_hud.shape);
		// right: size and distance
		char sz[32], ds[32];
		std::snprintf(sz, sizeof sz, "%d%%", a_hud.sizePct);
		std::snprintf(ds, sizeof ds, "%+.2f", a_hud.distance);
		float x = b.x - padX;
		for (int k = 1; k >= 0; --k) {
			const char*  v = k == 0 ? sz : ds;
			const char*  l = k == 0 ? "SIZE" : "DISTANCE";
			const float  vw = body->CalcTextSizeA(valueSz, FLT_MAX, 0.0f, v).x, lw = body->CalcTextSizeA(labelSz, FLT_MAX, 0.0f, l).x;
			const float  cw = (std::max)(vw, lw);
			dl->AddText(body, labelSz, ImVec2(x - lw, a.y + padY), Theme::Muted(), l);
			dl->AddText(body, valueSz, ImVec2(x - vw, a.y + padY + labelSz + S(4.0f)), Theme::Cream(), v);
			x -= cw + S(30.0f);
			if (k == 1) {
				dl->AddLine(ImVec2(x + S(15.0f), a.y + padY + S(2.0f)), ImVec2(x + S(15.0f), a.y + padY + labelSz + valueSz), IM_COL32(255, 255, 255, 26), S(1.0f));
			}
		}
		// hairline + key caps
		const float ly = a.y + padY + labelSz + valueSz + S(14.0f);
		dl->AddLine(ImVec2(a.x + padX, ly), ImVec2(b.x - padX, ly), IM_COL32(255, 255, 255, 16), S(1.0f));
		Theme::KeyHintsAt(dl, ImVec2(mid - keysW * 0.5f, ly + S(12.0f)), keys, labels, 5, true);
	}

	// ------------------------------------------------------------------ settings
	namespace
	{
		bool g_openSettings = false;
		int  g_settingsTab = 0;

		// label column + control column
		void Row(const char* a_label, const char* a_help = nullptr)
		{
			if (Theme::DescriptionPane()) {  // the whole row reports its help to the description pane
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const ImVec2 q(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, p.y + S(46.0f));
				if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(p, q)) {
					Theme::SetHoveredHelp(a_label, a_help);
					ImGui::GetWindowDrawList()->AddRectFilled(p - ImVec2(S(10.0f), S(3.0f)), q + ImVec2(S(4.0f), -S(3.0f)), IM_COL32(255, 255, 255, 8), S(6.0f));
				}
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_label);
			if (a_help && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", a_help);
			}
			ImGui::SameLine(S(300.0f));
		}

		bool Toggle(const char* a_id, bool* a_v)
		{
			if (Theme::ToggleSwitches()) {
				return Theme::Toggle(a_id, a_v);
			}
			ImGui::PushID(a_id);
			bool changed = false;
			if (Theme::Chip("On", *a_v) && !*a_v) {
				*a_v = true;
				changed = true;
			}
			ImGui::SameLine(0.0f, S(6.0f));
			if (Theme::Chip("Off", !*a_v) && *a_v) {
				*a_v = false;
				changed = true;
			}
			ImGui::PopID();
			return changed;
		}

		void Gap() { ImGui::Dummy(ImVec2(0, S(8.0f))); }
	}

	void OpenSettings(int a_tab)
	{
		g_openSettings = true;
		if (a_tab >= 0 && a_tab < 3) {
			g_settingsTab = a_tab;
		}
	}

	SettingsResult DrawSettings(SettingsModel& m)
	{
		SettingsResult r;
		if (g_openSettings) {
			g_openSettings = false;
			ImGui::OpenPopup("Hearthframe settings");
		}
		const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
		ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2((std::min)(S(Theme::DescriptionPane() ? 1300.0f : 1000.0f), ImGui::GetIO().DisplaySize.x * 0.95f), 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
		const bool open = ImGui::BeginPopupModal("Hearthframe settings", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar();
		if (!open) {
			return r;
		}
		Theme::DrawChrome();
		if (Theme::Header("Settings", "Saved to Hearthframe.ini") || (!m.waitingForKey && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
			r.action = SettingsAction::Cancel;
		}
		// tabs
		const char* tabs[] = { "Gallery", "Frames & hanging", "Notifications" };
		for (int t = 0; t < 3; ++t) {
			if (t > 0) {
				ImGui::SameLine(0.0f, S(8.0f));
			}
			ImGui::PushID(700 + t);
			if (FolderTab(tabs[t], g_settingsTab == t, S(46.0f), 4 + t)) {
				g_settingsTab = t;
			}
			ImGui::PopID();
		}
		const float settingsH = (std::min)(S(600.0f), ImGui::GetIO().DisplaySize.y * 0.6f);
		if (Theme::BeginParchment("##settings", ImVec2(Theme::DescriptionPane() ? -S(380.0f) : 0.0f, settingsH), true)) {
			const float ctl = ImGui::GetContentRegionAvail().x - S(300.0f);
			if (g_settingsTab == 0) {
				Theme::SectionLabel("Pictures");
				Row("Picture folder", "Where the gallery looks for pictures");
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.0f), S(10.0f)));
				ImGui::SetNextItemWidth(ctl - S(126.0f));
				ImGui::InputText("##folder", m.folder, sizeof m.folder);
				ImGui::PopStyleVar();
				ImGui::SameLine(0.0f, S(6.0f));
				if (Theme::Button("Open", ImVec2(S(120.0f), S(40.0f)))) {
					r.action = SettingsAction::OpenPictures;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Open the picture folder in Windows Explorer\n(type a new path in the box and press Save to change it)");
				}
				Row("Include subfolders", "Also show pictures in folders inside the picture folder");
				Toggle("recursive", &m.recursive);
				Gap();
				Theme::SectionLabel("Controls");
				Row("Gallery key", "Opens the gallery (or the menu of the picture you look at)");
				{
					char label[64];
					std::snprintf(label, sizeof label, "%s", m.waitingForKey ? "Press a key...  (Esc cancels)" : m.keyName);
					if (Theme::Button(label, ImVec2(S(m.waitingForKey ? 300.0f : 140.0f), S(40.0f)), m.waitingForKey) && !m.waitingForKey) {
						r.action = SettingsAction::ChangeKey;
					}
					if (m.keyWarning && *m.keyWarning) {
						ImGui::SameLine(0.0f, S(12.0f));
						ImGui::AlignTextToFramePadding();
						Theme::TextColoredWrapped(Theme::Warning(), m.keyWarning);
					}
				}
				Row("Mouse speed");
				Theme::SliderFloat("##mouse", &m.mouseSpeed, 0.2f, 5.0f, "%.1f x", ctl);
				Gap();
				Theme::SectionLabel("Look");
				Row("Thumbnail size");
				Theme::SliderInt("##thumb", &m.thumbSize, 100, 320, "%d px", ctl);
				Row("Interface size", "Size of all Hearthframe windows (Ctrl + wheel also works)");
				Theme::SliderInt("##ui", &m.uiScalePct, 60, 200, "%d%%", ctl);
				Row("Text size");
				Theme::SliderInt("##font", &m.fontSize, 12, 32, "%d px", ctl);
			} else if (g_settingsTab == 1) {
				Theme::SectionLabel("Hanging");
				Row("Default frame", "Preselected in the Hang on Wall window (same position in each shape's list)");
				{
					const char* name = (m.landscapeStyles && m.defaultStyle < m.landscapeCount) ? m.landscapeStyles[m.defaultStyle] : "?";
					Theme::SliderInt("##style", &m.defaultStyle, 0, (std::max)(0, m.landscapeCount - 1), name, ctl);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(300.0f));
					Theme::TextItalic("%s", m.defaultStyleOthers);
				}
				Row("Hang reach", "How far away the preview follows your aim");
				Theme::SliderInt("##reach", &m.placeReach, 500, 10000, "%d units", ctl);
				Row("Examine reach", "How far away F8 / Activate can open a hung picture's menu");
				Theme::SliderInt("##examine", &m.examineReach, 100, 5000, "%d units", ctl);
				Row("Resize with", "While hanging a picture: what changes its size and frame.\nArrows: Left / Right = size, Shift + Left / Right = frame.\nWheel: wheel = size, Shift + wheel = frame.\nWith \"Arrows only\" the wheel zooms the camera as usual.");
				{
					const char* opts[] = { "Wheel + arrows", "Arrows only", "Wheel only" };
					for (int k = 0; k < 3; ++k) {
						if (k) {
							ImGui::SameLine(0.0f, S(6.0f));
						}
						ImGui::PushID(820 + k);
						if (Theme::Chip(opts[k], m.resizeWith == k)) {
							m.resizeWith = k;
						}
						ImGui::PopID();
					}
				}
				Gap();
				Theme::SectionLabel("Pictures on walls");
				Row("Picture resolution", "Long side of the texture made for a hung picture. Applies to pictures you hang from now on.");
				{
					const int vals[] = { 512, 1024, 2048, 4096 };
					for (int k = 0; k < 4; ++k) {
						char l[16];
						std::snprintf(l, sizeof l, "%d", vals[k]);
						if (k) {
							ImGui::SameLine(0.0f, S(6.0f));
						}
						ImGui::PushID(800 + k);
						if (Theme::Chip(l, m.resolution == vals[k])) {
							m.resolution = vals[k];
						}
						ImGui::PopID();
					}
				}
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(300.0f));
				Theme::TextItalic("Higher is sharper up close; each step uses 4x the video memory per picture.");
				Row("Clean up unused textures", "Deletes converted pictures nothing in the current save uses");
				Toggle("cleanup", &m.cleanUp);
			} else {
				Theme::SectionLabel("Messages in the top-left corner");
				Row("All notifications", "Master switch");
				Toggle("all", &m.notifications);
				ImGui::BeginDisabled(!m.notifications);
				Row("Size while hanging", "\"Size: 110%\"");
				Toggle("size", &m.showSize);
				Row("Distance while hanging", "\"Distance from wall: +0.50\"");
				Toggle("dist", &m.showDistance);
				Row("Frame name while hanging", "\"Frame: Rustic Dark Oak\"");
				Toggle("frame", &m.showFrameName);
				Row("How-to when hanging starts");
				Toggle("help", &m.showPlacementHelp);
				Row("Confirmations", "Picture hung / taken down / hanging cancelled");
				Toggle("confirm", &m.showConfirmations);
				Row("Placement warnings", "Can't hang it here (floor, too far...)");
				Toggle("warn", &m.showPlacementWarnings);
				Row("Errors", "Something went wrong - best left on");
				Toggle("errors", &m.showErrors);
				ImGui::EndDisabled();
				Gap();
				Theme::SectionLabel("While hanging");
				Row("Info line at the bottom", "Frame, shape, size and distance, plus the keys");
				Toggle("info", &m.placementInfo);
			}
		}
		Theme::EndParchment();
		if (Theme::DescriptionPane()) {  // what the hovered setting does
			ImGui::SameLine(0.0f, S(28.0f));
			ImGui::BeginChild("##desc", ImVec2(0, settingsH));
			static const char* blurbs[] = { "Where your pictures come from, the key that opens Hearthframe and how its windows look.",
				"How pictures are hung on walls: the default frame, how far you can reach and how big the textures are.",
				"Which messages Hearthframe shows in the corner of the screen while you play." };
			static const char* titles[] = { "Gallery", "Frames & hanging", "Notifications" };
			Theme::DrawDescription(titles[g_settingsTab], blurbs[g_settingsTab]);
			ImGui::EndChild();
		}
		ImGui::Dummy(ImVec2(0, S(6.0f)));
		if (Theme::Button("Reset this tab", ImVec2(S(200.0f), S(52.0f)))) {
			r = { SettingsAction::ResetTab, g_settingsTab };
		}
		const float bw = S(180.0f);
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x);
		if (Theme::Button("Cancel", ImVec2(bw, S(52.0f)))) {
			r.action = SettingsAction::Cancel;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m.error != nullptr);
		if (Theme::Button("Save", ImVec2(bw, S(52.0f)), true)) {
			r.action = SettingsAction::Save;
		}
		ImGui::EndDisabled();
		if (m.error) {
			Theme::TextColoredWrapped(Theme::Warning(), m.error);
		} else {
			Theme::TextItalic("%s", m.iniPath);
		}
		if (r.action == SettingsAction::Save || r.action == SettingsAction::Cancel) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return r;
	}
}
