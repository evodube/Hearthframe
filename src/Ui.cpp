#include "Ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
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
				const float w = ImGui::CalcTextSize(a_label).x + S(14.0f) * 2.0f;
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
			if (m.thumbFill) {
				ImGui::SameLine(0.0f, S(12.0f));
				if (Theme::Chip("Fill", *m.thumbFill) && !*m.thumbFill) {
					*m.thumbFill = true;
					r.action = GalleryAction::ThumbFill;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Pictures fill their cards (edges trimmed in the grid only)");
				}
				ImGui::SameLine(0.0f, S(6.0f));
				if (Theme::Chip("Fit", !*m.thumbFill) && *m.thumbFill) {
					*m.thumbFill = false;
					r.action = GalleryAction::ThumbFill;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Whole pictures, with empty space around them");
				}
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
					ImGui::SetTooltip("Size of all Folder Gallery windows.\nCtrl + mouse wheel also works in any of them.");
				}
			}
			ImGui::Spacing();

			if (Theme::BeginParchment("##grid", ImVec2(0, 0), true)) {
				if (m.notice) {
					Theme::TextColoredWrapped(Theme::Warning(), m.notice);
					ImGui::Dummy(ImVec2(0, S(8.0f)));
				}
				if (m.pictures.empty() && m.search && m.search[0] && m.totalPictures > 0) {
					Theme::SectionLabel("No matches");
					ImGui::TextWrapped("No picture name contains \"%s\".", m.search);
				} else if (m.pictures.empty()) {
					Theme::SectionLabel("No pictures yet");
					std::string msg = "Put .png, .jpg, .bmp or .tga files in this folder, then press Refresh:\n";
					msg += m.folder;
					ImGui::TextWrapped("%s", msg.c_str());
				}
				const float cell = static_cast<float>(*m.thumbSize) * Theme::Scale();
				const float labelH = ImGui::GetTextLineHeight() + Theme::Italic()->FontSize + S(10.0f);
				const float gap = S(18.0f);
				const float availW = ImGui::GetContentRegionAvail().x;
				const int   cols = (std::max)(1, static_cast<int>((availW + gap) / (cell + gap)));
				auto*       dl = ImGui::GetWindowDrawList();
				for (int i = 0; i < static_cast<int>(m.pictures.size()); ++i) {
					auto& pic = m.pictures[i];
					if (i % cols != 0) {
						ImGui::SameLine(0.0f, gap);
					}
					ImGui::PushID(i);
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const ImVec2 cardSize(cell, cell + labelH);
					if (!ImGui::IsRectVisible(cardSize)) {
						ImGui::Dummy(cardSize);
						ImGui::PopID();
						continue;
					}
					if (!pic.thumb && !pic.failed && m.requestThumb) {
						m.requestThumb(i);
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
					// picture well
					const ImVec2 a = p, b = p + ImVec2(cell, cell);
					Theme::CardBack(dl, a, b, hovered);
					if (pic.thumb && m.thumbFill && *m.thumbFill && pic.thumbW > 0 && pic.thumbH > 0) {
						// crop to a square, centred
						const float  in = Theme::CardInset() * 0.5f;
						const float  ia = static_cast<float>(pic.thumbW) / pic.thumbH;
						ImVec2       uv0(0, 0), uv1(1, 1);
						if (ia > 1.0f) {
							uv0.x = (1.0f - 1.0f / ia) * 0.5f;
							uv1.x = 1.0f - uv0.x;
						} else {
							uv0.y = (1.0f - ia) * 0.5f;
							uv1.y = 1.0f - uv0.y;
						}
						dl->AddImage(pic.thumb, a + ImVec2(in, in), b - ImVec2(in, in), uv0, uv1);
					} else if (pic.thumb) {
						const ImVec2 sz = Fit(static_cast<float>(pic.thumbW), static_cast<float>(pic.thumbH), cell - 2 * Theme::CardInset(), cell - 2 * Theme::CardInset());
						const ImVec2 ia = a + (ImVec2(cell, cell) - sz) * 0.5f;
						dl->AddImage(pic.thumb, ia, ia + sz);
					} else {
						Centered(Theme::Italic(), Theme::Italic()->FontSize, Theme::Muted(), a, b, pic.failed ? "can't read" : "loading...");
					}
					Theme::CardFront(dl, a, b, hovered);
					// caption
					dl->PushClipRect(ImVec2(a.x, b.y), ImVec2(b.x, b.y + labelH), true);
					dl->AddText(ImVec2(a.x + S(2), b.y + S(6)), Theme::Ink(), pic.name);
					char dims[64];
					std::snprintf(dims, sizeof dims, "%d %s %d", pic.origW, kTimes, pic.origH);
					if (pic.origW > 0) {
						dl->AddText(Theme::Italic(), Theme::Italic()->FontSize, ImVec2(a.x + S(2), b.y + S(6) + ImGui::GetTextLineHeight()), Theme::InkSoft(), dims);
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
			}
			Theme::EndParchment();
			return r;
		}

		// ------------------------------------------------------------------ hang dialog
		void DrawHangDialog(GalleryModel& m, GalleryResult& r)
		{
			const auto& pic = m.pictures[m.viewing];
			const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // stays centred as its height changes
			ImGui::SetNextWindowSize(ImVec2(S(980.0f), 0));
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
					// long lists scroll so the dialog stays on screen
					const bool scroll = count > 12;
					if (scroll) {
						ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
						ImGui::BeginChild("##frames", ImVec2(0, S(260.0f)), ImGuiChildFlags_None, 0);
					}
					Flow flow;
					for (int i = 0; i < count; ++i) {
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
			if (Theme::BeginParchment("##change", ImVec2(0, 0), false, true)) {
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
			Theme::EndParchment();
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
		const bool visible = Theme::BeginWindow("Folder Gallery", nullptr);
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
			const Theme::Stat stats[] = { { "Pictures", pics }, { "On walls", walls } };
			const char*       subtitle = m.replaceMode ? "Choose a new picture for the frame" : m.folder;
			if (Theme::Header("Folder Gallery", subtitle, stats, 2)) {
				r.action = GalleryAction::Close;
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
			if (Theme::Button("Change picture", big, true)) {
				act = FrameAction::ChangePicture;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!m.canMove);
			if (Theme::Button("Move", big)) {
				act = FrameAction::Move;
			}
			ImGui::EndDisabled();
			if (Theme::Button("Take down", big)) {
				act = FrameAction::TakeDown;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(m.styleCount < 2);
			if (Theme::Button("Change frame", big)) {
				g_choosingFrame = true;
			}
			ImGui::EndDisabled();
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
}
