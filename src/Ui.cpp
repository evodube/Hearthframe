#include "Ui.h"

#include <algorithm>
#include <cfloat>
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
			ImGui::SameLine(0.0f, S(24.0f));
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Thumbnails");
			ImGui::SameLine();
			Theme::SliderInt("##thumbs", m.thumbSize, 100, 320, "%d px", S(260.0f));
			ImGui::Spacing();

			if (Theme::BeginParchment("##grid", ImVec2(0, 0), true)) {
				if (m.notice) {
					Theme::TextColoredWrapped(IM_COL32(140, 40, 30, 255), m.notice);
					ImGui::Dummy(ImVec2(0, S(8.0f)));
				}
				if (m.pictures.empty()) {
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
					// picture well
					const ImVec2 a = p, b = p + ImVec2(cell, cell);
					dl->AddRectFilled(a + ImVec2(S(3), S(4)), b + ImVec2(S(3), S(4)), IM_COL32(90, 66, 36, 70), S(2.0f));  // shadow
					dl->AddRectFilled(a, b, IM_COL32(34, 28, 22, 255), S(2.0f));
					if (pic.thumb) {
						const ImVec2 sz = Fit(static_cast<float>(pic.thumbW), static_cast<float>(pic.thumbH), cell - S(8), cell - S(8));
						const ImVec2 ia = a + (ImVec2(cell, cell) - sz) * 0.5f;
						dl->AddImage(pic.thumb, ia, ia + sz);
					} else {
						Centered(Theme::Italic(), Theme::Italic()->FontSize, IM_COL32(170, 150, 120, 255), a, b, pic.failed ? "can't read" : "loading...");
					}
					dl->AddRect(a, b, hovered ? Theme::Gold() : IM_COL32(112, 88, 60, 255), S(2.0f), 0, hovered ? S(3.0f) : S(1.2f));
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
						if (pic.failed && pic.error && *pic.error) {
							ImGui::SetTooltip("%s\nCan't read: %s\nPress Refresh to try again.", pic.name, pic.error);
						} else {
							ImGui::SetTooltip("%s\n%d %s %d", pic.name, pic.origW, kTimes, pic.origH);
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
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(S(760.0f), 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
			const bool open = ImGui::BeginPopupModal("Hang on wall", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar();
			if (!open) {
				return;
			}
			Theme::DrawChrome();
			if (Theme::Header("Hang on Wall", pic.name)) {
				ImGui::CloseCurrentPopup();
			}
			const int best = m.bestShape ? m.bestShape(pic.origW, pic.origH) : 0;
			if (Theme::BeginParchment("##hang", ImVec2(0, 0), false, true)) {
				Theme::SectionLabel("Frame");
				{
					Flow flow;
					for (int i = 0; i < m.styleCount; ++i) {
						flow.Next(m.styles[i]);
						ImGui::PushID(i);
						if (Theme::Chip(m.styles[i], *m.hangStyle == i)) {
							*m.hangStyle = i;
						}
						ImGui::PopID();
					}
				}
				ImGui::Dummy(ImVec2(0, S(6.0f)));
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

		// ------------------------------------------------------------------ viewer
		GalleryResult DrawViewer(GalleryModel& m)
		{
			GalleryResult r;
			const auto&   pic = m.pictures[m.viewing];
			const ImVec2  nav(S(130.0f), 0);
			if (Theme::Button("Back", nav) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				r.action = GalleryAction::Back;
			}
			ImGui::SameLine();
			if (Theme::Button("Previous", nav) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
				r.action = GalleryAction::Prev;
			}
			ImGui::SameLine();
			if (Theme::Button("Next", nav) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
				r.action = GalleryAction::Next;
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
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(16, 15, 14, 255));
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
					Centered(Theme::Italic(), Theme::Italic()->FontSize, IM_COL32(170, 150, 120, 255), a, b,
						m.fullFailed ? "This picture could not be read." : "Loading...");
				}
			}
			ImGui::EndChild();
			return r;
		}
	}

	GalleryResult DrawGallery(GalleryModel& m)
	{
		auto& io = ImGui::GetIO();
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.72f, io.DisplaySize.y * 0.78f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(S(760.0f), S(520.0f)), io.DisplaySize);
		GalleryResult r;
		if (Theme::BeginWindow("Folder Gallery", nullptr)) {
			char pics[16], walls[16];
			std::snprintf(pics, sizeof pics, "%d", static_cast<int>(m.pictures.size()));
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
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
					r.action = GalleryAction::Close;
				}
				const auto g = DrawGrid(m);
				if (g.action != GalleryAction::None) {
					r = g;
				}
			}
		}
		Theme::EndWindow();
		return r;
	}

	FrameAction DrawFrameMenu(FrameModel& m)
	{
		FrameAction act = FrameAction::None;
		auto&       io = ImGui::GetIO();
		ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(S(640.0f), 0));
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
				ImGui::TextWrapped("%s", *m.picture ? m.picture : "(unknown - hung some other way)");
				ImGui::Dummy(ImVec2(0, S(6.0f)));
				Theme::SectionLabel("Size");
				if (Theme::SliderInt("##size", m.sizePct, 30, 300, "%d%%", ImGui::GetContentRegionAvail().x)) {
					act = FrameAction::SizeChanged;
				}
				ImGui::Dummy(ImVec2(0, S(6.0f)));
				Theme::SectionLabel("Distance from wall");
				const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
				if (Theme::Button("Closer", ImVec2(half, 0))) {
					act = FrameAction::Closer;
				}
				ImGui::SameLine();
				if (Theme::Button("Further", ImVec2(half, 0))) {
					act = FrameAction::Further;
				}
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
			if (Theme::Button("Close", big)) {
				act = FrameAction::Close;
			}
		}
		Theme::EndWindow();
		return act;
	}
}
