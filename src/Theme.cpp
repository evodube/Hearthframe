#include "Theme.h"

#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>

namespace Theme
{
	namespace
	{
		constexpr ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

		enum class Chrome
		{
			InsetDiamonds,   // thin inset line with diamond corners
			CornerBrackets,  // L-shaped brackets in the corners
			Rules,           // Skyrim-menu style: fading horizontal rules top and bottom
			DoubleLine,      // two inset lines
			Rounded,         // soft rounded outline
			Rivets,          // double line with rivets
		};

		enum class Title
		{
			SmallCaps,
			Upper,   // all caps, tracked
			Normal,
		};

		// Extra surface treatment (all off = the flat look)
		struct Fx
		{
			bool textured = false;   // parchment fibre + leather grain textures
			bool bevel = false;      // lit/shaded edges on frames, boxes and buttons
			bool shadows = false;    // soft drop shadows under panels and cards
			bool deckle = false;     // torn parchment edges
			int  frame = 0;          // 0 thin line, 1 metallic gold band with corner pieces, 2 stitched leather
			bool medallion = false;  // gold guild medallion before the title
			bool seal = false;       // wax seal on parchment panels
			bool gilt = false;       // small gilt frames around thumbnails
		};

		struct Look
		{
			const char* name;
			const char* blurb;
			bool        sans;  // font family
			Title       title;
			Chrome      chrome;
			float       rounding;       // buttons, chips, cards
			float       windowRounding;
			// window
			ImU32 winMid, winEdge, winOuter;
			ImU32 accent, accentDim, accentFaint;
			ImU32 text, textDim;
			ImU32 frameBg, frameHov, frameAct, grab, grabAct;
			ImU32 btnBg, btnHov, btnAct, btnText, btnBorder;
			ImU32 priBg, priHov, priAct, priText, priBorder;
			ImU32 boxBg, closeHov, closeX;
			// panel
			ImU32 panel, panelShade, panelBorder, ink, inkSoft, panelRule;
			ImU32 pFrame, pFrameHov, pFrameAct, pGrab, pGrabAct;
			ImU32 pBtnHov, pBtnAct, pSel, pSelHov, pSelText, pBorder;
			ImU32 pPriBg, pPriHov, pPriText;
			// misc
			ImU32 well, cardBorder, shadow, muted, viewerBg, warning;
			Fx    fx{};
		};

		// clang-format off
		const Look kLooks[] = {
			{ "Guild Ledger", "Charcoal and gold with parchment panels",
				false, Title::SmallCaps, Chrome::InsetDiamonds, 2.0f, 0.0f,
				C(40,38,36), C(22,21,20), C(10,10,10),
				C(206,174,116), C(128,104,66), C(92,76,52),
				C(230,218,194), C(168,154,128),
				C(22,21,20), C(52,47,40), C(66,58,46), C(176,146,94), C(206,174,116),
				C(46,41,34), C(76,64,46), C(98,80,54), C(230,218,194), C(128,104,66),
				C(122,96,56), C(150,120,72), C(170,138,84), C(250,240,218), C(206,174,116),
				C(24,23,22), C(70,58,40), C(230,218,194),
				C(224,208,170), C(150,120,74,110), C(150,122,80), C(56,38,22), C(112,88,60), C(150,122,80,160),
				C(205,186,144), C(196,175,130), C(186,164,118), C(92,66,38), C(70,48,26),
				C(92,66,38,45), C(92,66,38,80), C(92,66,38), C(108,78,46), C(244,232,205), C(112,88,60),
				C(92,66,38), C(112,82,48), C(244,232,205),
				C(34,28,22), C(112,88,60), C(90,66,36,70), C(170,150,120), C(16,15,14), C(140,40,30) },

			{ "Nordic Stone", "Cold slate and frost-silver, carved corner brackets",
				false, Title::SmallCaps, Chrome::CornerBrackets, 0.0f, 0.0f,
				C(52,58,64), C(28,31,35), C(12,14,16),
				C(196,214,226), C(118,134,148), C(78,88,98),
				C(226,232,238), C(150,162,172),
				C(24,27,31), C(48,54,61), C(60,68,77), C(150,170,186), C(196,214,226),
				C(42,47,53), C(62,70,79), C(78,88,99), C(226,232,238), C(110,124,138),
				C(78,102,124), C(96,124,150), C(110,140,168), C(240,246,250), C(170,196,216),
				C(30,34,38), C(64,74,84), C(226,232,238),
				C(64,71,79), C(20,24,28,90), C(104,118,132), C(232,238,243), C(160,174,186), C(130,146,160,140),
				C(44,50,56), C(54,61,68), C(62,70,78), C(176,196,212), C(206,222,234),
				C(255,255,255,18), C(255,255,255,34), C(150,176,198), C(170,196,216), C(22,28,34), C(130,146,160),
				C(78,102,124), C(96,124,150), C(240,246,250),
				C(26,29,33), C(110,124,138), C(0,0,0,90), C(150,162,172), C(14,16,18), C(232,128,110) },

			{ "Skyrim Menu", "Like the game's own menus: black, white text, fading rules",
				true, Title::Upper, Chrome::Rules, 0.0f, 0.0f,
				C(10,10,10,232), C(4,4,4,236), C(0,0,0,0),
				C(240,240,240), C(150,150,150), C(90,90,90),
				C(235,235,235), C(150,150,150),
				C(0,0,0,150), C(255,255,255,24), C(255,255,255,40), C(110,110,110), C(160,160,160),
				C(0,0,0,0), C(255,255,255,26), C(255,255,255,46), C(235,235,235), C(120,120,120),
				C(255,255,255,30), C(255,255,255,50), C(255,255,255,70), C(255,255,255), C(220,220,220),
				C(0,0,0,120), C(255,255,255,30), C(235,235,235),
				C(255,255,255,8), C(0,0,0,0), C(255,255,255,40), C(235,235,235), C(150,150,150), C(255,255,255,50),
				C(0,0,0,140), C(255,255,255,20), C(255,255,255,36), C(110,110,110), C(160,160,160),
				C(255,255,255,24), C(255,255,255,44), C(255,255,255,40), C(255,255,255,60), C(255,255,255), C(120,120,120),
				C(255,255,255,30), C(255,255,255,50), C(255,255,255),
				C(0,0,0,170), C(90,90,90), C(0,0,0,0), C(150,150,150), C(0,0,0,200), C(230,110,100) },

			{ "Daedric Ember", "Black iron and smouldering red, double-lined",
				false, Title::SmallCaps, Chrome::DoubleLine, 0.0f, 0.0f,
				C(40,18,16), C(14,7,6), C(0,0,0),
				C(232,112,58), C(140,58,32), C(92,38,24),
				C(238,214,198), C(168,128,112),
				C(18,9,8), C(54,24,18), C(70,30,22), C(200,90,46), C(240,128,70),
				C(40,18,15), C(70,28,20), C(92,36,24), C(238,214,198), C(120,50,30),
				C(150,52,26), C(182,66,32), C(206,80,40), C(255,236,222), C(232,112,58),
				C(22,10,9), C(80,30,20), C(238,214,198),
				C(46,22,19), C(0,0,0,110), C(110,44,28), C(238,214,198), C(176,128,110), C(140,58,32,160),
				C(28,13,11), C(52,22,17), C(66,28,20), C(214,98,50), C(240,128,70),
				C(232,112,58,30), C(232,112,58,56), C(150,52,26), C(182,66,32), C(255,236,222), C(120,50,30),
				C(150,52,26), C(182,66,32), C(255,236,222),
				C(18,8,7), C(110,44,28), C(0,0,0,110), C(170,120,100), C(8,4,4), C(255,140,100) },

			{ "Modern Glass", "Clean modern app look: rounded, translucent, teal accent",
				true, Title::Normal, Chrome::Rounded, 8.0f, 14.0f,
				C(30,36,46,240), C(20,24,32,240), C(0,0,0,0),
				C(94,214,200), C(70,140,136), C(60,72,88),
				C(232,238,244), C(146,158,172),
				C(255,255,255,14), C(255,255,255,26), C(255,255,255,36), C(94,214,200), C(140,236,224),
				C(255,255,255,16), C(255,255,255,30), C(255,255,255,44), C(232,238,244), C(255,255,255,30),
				C(52,170,158), C(68,192,178), C(40,146,136), C(255,255,255), C(52,170,158),
				C(255,255,255,12), C(255,255,255,30), C(232,238,244),
				C(255,255,255,10), C(0,0,0,0), C(255,255,255,22), C(232,238,244), C(146,158,172), C(255,255,255,26),
				C(255,255,255,14), C(255,255,255,24), C(255,255,255,34), C(94,214,200), C(140,236,224),
				C(255,255,255,20), C(255,255,255,34), C(52,170,158), C(68,192,178), C(255,255,255), C(255,255,255,40),
				C(52,170,158), C(68,192,178), C(255,255,255),
				C(10,12,16,200), C(255,255,255,30), C(0,0,0,70), C(146,158,172), C(10,12,16,230), C(255,128,120) },

			{ "Dwemer Brass", "Verdigris metal with brass trim and rivets",
				false, Title::SmallCaps, Chrome::Rivets, 1.0f, 0.0f,
				C(34,50,47), C(16,26,24), C(6,10,9),
				C(212,164,82), C(140,104,52), C(92,72,40),
				C(226,218,196), C(160,160,138),
				C(14,22,21), C(36,52,49), C(46,64,60), C(196,150,74), C(228,180,98),
				C(30,44,41), C(46,64,60), C(60,80,75), C(226,218,196), C(140,104,52),
				C(150,108,48), C(176,128,58), C(196,146,66), C(255,246,226), C(212,164,82),
				C(18,28,26), C(56,72,62), C(226,218,196),
				C(40,58,55), C(0,0,0,80), C(120,96,56), C(230,222,200), C(164,164,140), C(140,104,52,150),
				C(26,38,36), C(36,52,49), C(46,64,60), C(212,164,82), C(232,186,104),
				C(212,164,82,28), C(212,164,82,52), C(150,108,48), C(176,128,58), C(255,246,226), C(140,104,52),
				C(150,108,48), C(176,128,58), C(255,246,226),
				C(14,22,21), C(120,96,56), C(0,0,0,90), C(160,160,138), C(8,12,11), C(240,130,100) },

			{ "Scholar's Desk", "Light theme: warm paper, ink and red-brown accents",
				false, Title::SmallCaps, Chrome::DoubleLine, 2.0f, 0.0f,
				C(238,228,204), C(214,198,162), C(120,96,62),
				C(132,44,32), C(150,110,80), C(176,148,108),
				C(52,36,22), C(118,96,72),
				C(226,212,180), C(216,200,164), C(206,188,150), C(132,44,32), C(160,58,40),
				C(0,0,0,0), C(132,44,32,30), C(132,44,32,56), C(52,36,22), C(118,96,72),
				C(132,44,32), C(156,56,40), C(112,36,26), C(252,244,226), C(112,36,26),
				C(232,220,192), C(214,196,160), C(52,36,22),
				C(248,242,226), C(150,120,74,60), C(176,148,108), C(52,36,22), C(118,96,72), C(176,148,108,160),
				C(236,226,202), C(228,216,188), C(218,204,172), C(132,44,32), C(160,58,40),
				C(132,44,32,26), C(132,44,32,50), C(132,44,32), C(156,56,40), C(252,244,226), C(150,120,90),
				C(132,44,32), C(156,56,40), C(252,244,226),
				C(58,44,30), C(150,120,90), C(90,66,36,60), C(140,118,90), C(40,30,20), C(160,40,30) },
		};
		// clang-format on
		std::vector<Look> BuildLooks()
		{
			std::vector<Look> v(std::begin(kLooks), std::end(kLooks));
			auto              variant = [&](const char* a_name, const char* a_blurb, Fx a_fx, Chrome a_chrome) {
                Look l = kLooks[0];  // Guild Ledger colours
				l.name = a_name;
				l.blurb = a_blurb;
				l.fx = a_fx;
				l.chrome = a_chrome;
				v.push_back(l);
			};
			// A: textured + bevelled + shadows, thin gold line with diamonds
			variant("Ledger: Textured", "Parchment fibre, leather grain, lit gold edges and soft shadows",
				Fx{ true, true, true, false, 0, false, false, false }, Chrome::InsetDiamonds);
			// B: ornate metallic frame band, deckled parchment, medallion
			variant("Ledger: Gilded", "Metallic gold frame band with corner pieces, torn parchment edges, guild medallion",
				Fx{ true, true, true, true, 1, true, false, false }, Chrome::InsetDiamonds);
			// C: guild card - stitched leather, wax seal, medallion
			variant("Ledger: Guild Card", "Stitched leather cover, wax seal and medallion like a guild member card",
				Fx{ true, true, true, true, 2, true, true, false }, Chrome::InsetDiamonds);
			// D: gallery wall - thumbnails as gilt-framed paintings on parchment
			variant("Ledger: Gallery Wall", "Every thumbnail in a little gilt frame, raised cards, embossed buttons",
				Fx{ true, true, true, false, 0, false, false, true }, Chrome::InsetDiamonds);
			return v;
		}
		const std::vector<Look>& Looks()
		{
			static const std::vector<Look> looks = BuildLooks();
			return looks;
		}
		int kLookCount = static_cast<int>(Looks().size());

		int         g_look = 0;
		const Look* L = &Looks()[0];

		// ---- generated textures (luminance maps tinted with the palette; tile seamlessly)
		std::function<ImTextureID(int, int, const unsigned char*)> g_texFactory;
		ImTextureID g_texParch = 0, g_texGrain = 0;
		constexpr int kTex = 256;

		float Hash(int x, int y, int seed)
		{
			unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + static_cast<unsigned>(seed) * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
		}

		// Periodic value noise (period = cells) so the texture tiles.
		float Noise(float x, float y, int cells, int seed)
		{
			const int   x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
			const float fx = x - x0, fy = y - y0;
			auto        at = [&](int ix, int iy) { return Hash(((ix % cells) + cells) % cells, ((iy % cells) + cells) % cells, seed); };
			const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
			const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
			const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
			return a + (b - a) * sy;
		}

		float Fbm(float u, float v, int baseCells, int octaves, int seed)
		{
			float sum = 0, amp = 0.5f, norm = 0;
			int   cells = baseCells;
			for (int o = 0; o < octaves; ++o) {
				sum += amp * Noise(u * cells, v * cells, cells, seed + o * 17);
				norm += amp;
				amp *= 0.5f;
				cells *= 2;
			}
			return sum / norm;
		}

		void MakeTextures()
		{
			if (!g_texFactory || g_texParch) {
				return;
			}
			std::vector<unsigned char> px(kTex * kTex * 4);
			// parchment: soft blotches + long fibres + fine grain, mean ~0.94
			for (int y = 0; y < kTex; ++y) {
				for (int x = 0; x < kTex; ++x) {
					const float u = x / static_cast<float>(kTex), v = y / static_cast<float>(kTex);
					const float blot = Fbm(u, v, 3, 4, 11);
					const float fibre = Fbm(u * 1.0f, v * 1.0f + 0.15f * Fbm(u, v, 4, 2, 5), 16, 3, 23);
					const float grain = Hash(x, y, 7);
					float       l = 0.80f + 0.16f * blot + 0.05f * (fibre > 0.62f ? (fibre - 0.62f) * 4.0f : 0.0f) + 0.035f * (grain - 0.5f);
					l = std::fmin(1.0f, std::fmax(0.0f, l));
					auto* o = &px[(y * kTex + x) * 4];
					o[0] = o[1] = o[2] = 255;
					o[3] = static_cast<unsigned char>((1.0f - l) * 255.0f);
				}
			}
			g_texParch = g_texFactory(kTex, kTex, px.data());
			// leather / dark stone grain: pebbled, mean ~0.9
			for (int y = 0; y < kTex; ++y) {
				for (int x = 0; x < kTex; ++x) {
					const float u = x / static_cast<float>(kTex), v = y / static_cast<float>(kTex);
					const float pebble = Fbm(u, v, 24, 2, 31);
					const float broad = Fbm(u, v, 2, 3, 41);
					const float grain = Hash(x, y, 9);
					float       l = 0.74f + 0.20f * pebble + 0.10f * broad + 0.05f * (grain - 0.5f);
					l = std::fmin(1.0f, std::fmax(0.0f, l));
					auto* o = &px[(y * kTex + x) * 4];
					o[0] = o[1] = o[2] = 255;
					o[3] = static_cast<unsigned char>((1.0f - l) * 255.0f);
				}
			}
			g_texGrain = g_texFactory(kTex, kTex, px.data());
		}

		void Tiled(ImDrawList* a_dl, ImTextureID a_tex, const ImVec2& a_a, const ImVec2& a_b, ImU32 a_tint, float a_rounding = 0.0f)
		{
			const float  tile = 256.0f * 1.25f;
			const ImVec2 uv1((a_b.x - a_a.x) / tile, (a_b.y - a_a.y) / tile);
			if (a_rounding > 0.0f) {
				a_dl->AddImageRounded(a_tex, a_a, a_b, ImVec2(0, 0), uv1, a_tint, a_rounding);
			} else {
				a_dl->AddImage(a_tex, a_a, a_b, ImVec2(0, 0), uv1, a_tint);
			}
		}

		float   g_s = 1.0f;
		float   g_base = 18.0f;
		ImFont* g_body = nullptr;
		ImFont* g_bold = nullptr;
		ImFont* g_italic = nullptr;
		int     g_parchmentDepth = 0;

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

		ImU32 WithAlpha(ImU32 a_col, int a_alpha)
		{
			return (a_col & 0x00FFFFFF) | (static_cast<ImU32>(a_alpha) << 24);
		}

		// Horizontal rule that fades out at both ends, with a small diamond in the middle.
		void FadingRule(ImDrawList* a_dl, float a_x0, float a_x1, float a_y, ImU32 a_col, float a_thick, bool a_diamond)
		{
			const float  mid = (a_x0 + a_x1) * 0.5f;
			const ImU32  clear = WithAlpha(a_col, 0);
			a_dl->AddRectFilledMultiColor(ImVec2(a_x0, a_y), ImVec2(mid, a_y + a_thick), clear, a_col, a_col, clear);
			a_dl->AddRectFilledMultiColor(ImVec2(mid, a_y), ImVec2(a_x1, a_y + a_thick), a_col, clear, clear, a_col);
			if (a_diamond) {
				Diamond(a_dl, ImVec2(mid, a_y + a_thick * 0.5f), a_thick * 3.5f, a_col);
			}
		}
	}

	int         StyleCount() { return kLookCount; }
	const char* StyleName(int a_i) { return Looks()[(a_i % kLookCount + kLookCount) % kLookCount].name; }
	const char* StyleBlurb(int a_i) { return Looks()[(a_i % kLookCount + kLookCount) % kLookCount].blurb; }
	void        SetTextureFactory(std::function<ImTextureID(int, int, const unsigned char*)> a_factory) { g_texFactory = std::move(a_factory); }
	int         CurrentStyle() { return g_look; }

	void SetStyle(int a_i)
	{
		g_look = (a_i % kLookCount + kLookCount) % kLookCount;
		L = &Looks()[g_look];
	}

	void Setup(float a_scale, float a_baseSize, const FontFiles& a_files)
	{
		g_s = a_scale < 0.5f ? 0.5f : a_scale;
		g_base = a_baseSize;
		auto&       io = ImGui::GetIO();
		const float size = g_base * g_s;
		io.Fonts->Clear();  // Setup may run again when the interface size or style changes

		const char* reg = L->sans && a_files.sansRegular ? a_files.sansRegular : a_files.regular;
		const char* bold = L->sans && a_files.sansBold ? a_files.sansBold : a_files.bold;
		const char* ital = L->sans && a_files.sansItalic ? a_files.sansItalic : a_files.italic;
		g_body = LoadFont(reg, size);
		if (!g_body) {
			ImFontConfig fc;
			fc.SizePixels = size;
			g_body = io.Fonts->AddFontDefault(&fc);
		}
		g_bold = LoadFont(bold, size * 1.9f);  // big: used for titles and numbers (drawn smaller when needed)
		if (!g_bold) {
			g_bold = g_body;
		}
		g_italic = LoadFont(ital, size * 0.92f);
		if (!g_italic) {
			g_italic = g_body;
		}
		io.FontDefault = g_body;
		if (L->fx.textured) {
			MakeTextures();
		}

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
		st.WindowRounding = L->windowRounding;
		st.ChildRounding = L->rounding;
		st.FrameRounding = L->rounding;
		st.PopupRounding = L->rounding;
		st.ScrollbarRounding = L->rounding;
		st.GrabRounding = L->rounding;
		st.WindowMinSize = ImVec2(200, 120);
		st.ScaleAllSizes(g_s);

		auto* c = st.Colors;
		c[ImGuiCol_Text] = V(L->text);
		c[ImGuiCol_TextDisabled] = V(L->textDim);
		c[ImGuiCol_WindowBg] = V(L->winEdge);
		c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_PopupBg] = V(WithAlpha(L->winMid, 250));
		c[ImGuiCol_Border] = V(L->accentDim);
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = V(L->frameBg);
		c[ImGuiCol_FrameBgHovered] = V(L->frameHov);
		c[ImGuiCol_FrameBgActive] = V(L->frameAct);
		c[ImGuiCol_TitleBg] = V(L->winEdge);
		c[ImGuiCol_TitleBgActive] = V(L->winEdge);
		c[ImGuiCol_TitleBgCollapsed] = V(L->winEdge);
		c[ImGuiCol_MenuBarBg] = V(L->winMid);
		c[ImGuiCol_ScrollbarBg] = V(C(0, 0, 0, 30));
		c[ImGuiCol_ScrollbarGrab] = V(WithAlpha(L->accentDim, 170));
		c[ImGuiCol_ScrollbarGrabHovered] = V(WithAlpha(L->accentDim, 230));
		c[ImGuiCol_ScrollbarGrabActive] = V(L->accent);
		c[ImGuiCol_CheckMark] = V(L->accent);
		c[ImGuiCol_SliderGrab] = V(L->grab);
		c[ImGuiCol_SliderGrabActive] = V(L->grabAct);
		c[ImGuiCol_Button] = V(L->btnBg);
		c[ImGuiCol_ButtonHovered] = V(L->btnHov);
		c[ImGuiCol_ButtonActive] = V(L->btnAct);
		c[ImGuiCol_Header] = V(WithAlpha(L->accent, 50));
		c[ImGuiCol_HeaderHovered] = V(WithAlpha(L->accent, 90));
		c[ImGuiCol_HeaderActive] = V(WithAlpha(L->accent, 130));
		c[ImGuiCol_Separator] = V(L->accentFaint);
		c[ImGuiCol_SeparatorHovered] = V(L->accentDim);
		c[ImGuiCol_SeparatorActive] = V(L->accent);
		c[ImGuiCol_ResizeGrip] = V(WithAlpha(L->accentDim, 90));
		c[ImGuiCol_ResizeGripHovered] = V(WithAlpha(L->accentDim, 180));
		c[ImGuiCol_ResizeGripActive] = V(L->accent);
		c[ImGuiCol_Tab] = V(L->btnBg);
		c[ImGuiCol_TabHovered] = V(L->btnHov);
		c[ImGuiCol_TabActive] = V(L->btnAct);
		c[ImGuiCol_PlotLines] = V(L->accent);
		c[ImGuiCol_PlotHistogram] = V(L->accent);
		c[ImGuiCol_TextSelectedBg] = V(WithAlpha(L->accent, 90));
		c[ImGuiCol_DragDropTarget] = V(L->accent);
		c[ImGuiCol_NavHighlight] = V(L->accent);
		c[ImGuiCol_ModalWindowDimBg] = V(C(0, 0, 0, 150));
		ImGui::GetStyle() = st;
	}

	float   Scale() { return g_s; }
	float   S(float a_px) { return a_px * g_s; }
	ImFont* Body() { return g_body; }
	ImFont* Bold() { return g_bold; }
	ImFont* Italic() { return g_italic; }
	ImU32   Gold() { return L->accent; }
	ImU32   GoldDim() { return L->accentDim; }
	ImU32   Ink() { return L->ink; }
	ImU32   InkSoft() { return L->inkSoft; }
	ImU32   Cream() { return L->text; }
	ImU32   Well() { return L->well; }
	ImU32   CardBorder() { return L->cardBorder; }
	ImU32   CardShadow() { return L->shadow; }
	ImU32   Muted() { return L->muted; }
	ImU32   ViewerBg() { return L->viewerBg; }
	ImU32   Warning() { return L->warning; }
	float   Rounding() { return S(L->rounding); }

	namespace
	{
		ImU32 Mix(ImU32 a_a, ImU32 a_b, float a_t)
		{
			auto ch = [&](int sh) {
				const float x = ((a_a >> sh) & 0xFF) + (static_cast<float>((a_b >> sh) & 0xFF) - ((a_a >> sh) & 0xFF)) * a_t;
				return static_cast<ImU32>(std::fmin(255.0f, std::fmax(0.0f, x)));
			};
			return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
		}
		ImU32 Bright(ImU32 a_c, float a_t) { return Mix(a_c, IM_COL32(255, 248, 225, (a_c >> 24) & 0xFF), a_t); }
		ImU32 Dark(ImU32 a_c, float a_t) { return Mix(a_c, IM_COL32(0, 0, 0, (a_c >> 24) & 0xFF), a_t); }

		// Layered soft shadow, offset down-right.
		void SoftShadow(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, float a_r, float a_size, int a_alpha = 70)
		{
			const int   n = 7;
			const ImVec2 off(a_size * 0.25f, a_size * 0.45f);
			for (int i = n; i >= 1; --i) {
				const float g = a_size * i / n;
				const int   al = static_cast<int>(a_alpha * (1.0f - static_cast<float>(i - 1) / n) / n * 2.2f);
				a_dl->AddRectFilled(a_a + off - ImVec2(g, g) * 0.5f, a_b + off + ImVec2(g, g) * 0.5f, IM_COL32(0, 0, 0, al), a_r + g * 0.5f);
			}
		}

		// Raised (light top-left, dark bottom-right) or sunken edge.
		void Bevel(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, float a_t, bool a_sunken, int a_light = 60, int a_dark = 90)
		{
			const ImU32 hi = IM_COL32(255, 244, 214, a_light), lo = IM_COL32(0, 0, 0, a_dark);
			const ImU32 tl = a_sunken ? lo : hi, br = a_sunken ? hi : lo;
			const float h = a_t * 0.5f;
			a_dl->AddLine(ImVec2(a_a.x + h, a_b.y - h), ImVec2(a_a.x + h, a_a.y + h), tl, a_t);
			a_dl->AddLine(ImVec2(a_a.x + h, a_a.y + h), ImVec2(a_b.x - h, a_a.y + h), tl, a_t);
			a_dl->AddLine(ImVec2(a_b.x - h, a_a.y + h), ImVec2(a_b.x - h, a_b.y - h), br, a_t);
			a_dl->AddLine(ImVec2(a_b.x - h, a_b.y - h), ImVec2(a_a.x + h, a_b.y - h), br, a_t);
		}

		// Metallic gold band: nested lines dark -> bright -> dark, lit from the top-left.
		void GoldBand(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, float a_w)
		{
			const int   n = 6;
			const float step = a_w / n;
			const float prof[n] = { 0.10f, 0.55f, 1.00f, 0.75f, 0.35f, 0.05f };
			for (int i = 0; i < n; ++i) {
				const ImVec2 o(step * (i + 0.5f), step * (i + 0.5f));
				const ImU32  base = Mix(Dark(L->accentDim, 0.50f), Mix(L->accent, IM_COL32(255, 226, 150, 255), 0.25f), prof[i]);
				const ImU32  hi = Mix(base, IM_COL32(255, 230, 160, 255), 0.18f), lo = Dark(base, 0.30f);
				const ImVec2 a = a_a + o, b = a_b - o;
				a_dl->AddLine(ImVec2(a.x, b.y), a, hi, step + 0.6f);
				a_dl->AddLine(a, ImVec2(b.x, a.y), hi, step + 0.6f);
				a_dl->AddLine(ImVec2(b.x, a.y), b, lo, step + 0.6f);
				a_dl->AddLine(b, ImVec2(a.x, b.y), lo, step + 0.6f);
			}
		}

		void CornerPiece(ImDrawList* a_dl, const ImVec2& a_c, float a_r)
		{
			const ImVec2 a = a_c - ImVec2(a_r, a_r), b = a_c + ImVec2(a_r, a_r);
			a_dl->AddRectFilled(a, b, Dark(L->winEdge, 0.2f));
			GoldBand(a_dl, a, b, a_r * 0.45f);
			Diamond(a_dl, a_c, a_r * 0.42f, Bright(L->accent, 0.15f));
			Diamond(a_dl, a_c - ImVec2(a_r * 0.06f, a_r * 0.06f), a_r * 0.16f, IM_COL32(255, 246, 220, 200));
		}

		void Stitches(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, ImU32 a_col, float a_len, float a_gap, float a_t)
		{
			auto run = [&](ImVec2 p0, ImVec2 p1) {
				const ImVec2 d = p1 - p0;
				const float  len = std::sqrt(d.x * d.x + d.y * d.y);
				const ImVec2 u = d / len;
				for (float t = 0; t + a_len <= len; t += a_len + a_gap) {
					a_dl->AddLine(p0 + u * t + ImVec2(0.8f, 1.0f), p0 + u * (t + a_len) + ImVec2(0.8f, 1.0f), IM_COL32(0, 0, 0, 110), a_t);
					a_dl->AddLine(p0 + u * t, p0 + u * (t + a_len), a_col, a_t);
				}
			};
			run(a_a, ImVec2(a_b.x, a_a.y));
			run(ImVec2(a_b.x, a_a.y), a_b);
			run(a_b, ImVec2(a_a.x, a_b.y));
			run(ImVec2(a_a.x, a_b.y), a_a);
		}

		// Gold coin with a small picture-frame emblem.
		void Medallion(ImDrawList* a_dl, const ImVec2& a_c, float a_r)
		{
			a_dl->AddCircleFilled(a_c + ImVec2(a_r * 0.08f, a_r * 0.14f), a_r * 1.04f, IM_COL32(0, 0, 0, 110), 40);
			const int n = 8;
			for (int i = 0; i < n; ++i) {  // radial gradient, highlight towards top-left
				const float t = static_cast<float>(i) / (n - 1);
				const float r = a_r * (1.0f - t * 0.92f);
				a_dl->AddCircleFilled(a_c - ImVec2(a_r, a_r) * (0.18f * t), r, Mix(Dark(L->accentDim, 0.40f), Mix(L->accent, IM_COL32(255, 228, 155, 255), 0.35f), t), 40);
			}
			a_dl->AddCircle(a_c, a_r * 0.80f, Dark(L->accentDim, 0.3f), 40, a_r * 0.06f);
			a_dl->AddCircle(a_c, a_r * 0.98f, Dark(L->accentDim, 0.45f), 40, a_r * 0.05f);
			const ImVec2 e(a_r * 0.36f, a_r * 0.28f);
			a_dl->AddRect(a_c - e, a_c + e, Dark(L->accentDim, 0.55f), 0.0f, 0, a_r * 0.09f);
			a_dl->AddTriangleFilled(a_c + ImVec2(-e.x * 0.7f, e.y * 0.7f), a_c + ImVec2(-e.x * 0.05f, -e.y * 0.35f), a_c + ImVec2(e.x * 0.6f, e.y * 0.7f), Dark(L->accentDim, 0.45f));
		}

		// Red wax seal with an uneven rim.
		void WaxSeal(ImDrawList* a_dl, const ImVec2& a_c, float a_r)
		{
			const ImU32 wax = IM_COL32(140, 30, 26, 255);
			a_dl->AddCircleFilled(a_c + ImVec2(a_r * 0.1f, a_r * 0.16f), a_r * 1.05f, IM_COL32(40, 10, 5, 90), 32);
			a_dl->PathClear();
			for (int i = 0; i < 28; ++i) {
				const float ang = i / 28.0f * 6.2831853f;
				const float rr = a_r * (0.92f + 0.10f * Hash(i, 3, 77));
				a_dl->PathLineTo(a_c + ImVec2(std::cos(ang), std::sin(ang)) * rr);
			}
			a_dl->PathFillConvex(wax);
			a_dl->AddCircleFilled(a_c, a_r * 0.66f, Dark(wax, 0.18f), 32);
			a_dl->AddCircle(a_c, a_r * 0.66f, Bright(wax, 0.25f), 32, a_r * 0.06f);
			a_dl->AddCircleFilled(a_c - ImVec2(a_r * 0.35f, a_r * 0.38f), a_r * 0.16f, IM_COL32(255, 200, 190, 70), 16);
			const float k = a_r * 0.28f;  // emblem: a little frame
			a_dl->AddRect(a_c - ImVec2(k, k * 0.8f), a_c + ImVec2(k, k * 0.8f), Dark(wax, 0.45f), 0.0f, 0, a_r * 0.08f);
			a_dl->AddRect(a_c - ImVec2(k, k * 0.8f) - ImVec2(0.8f, 0.8f), a_c + ImVec2(k, k * 0.8f) - ImVec2(0.8f, 0.8f), Bright(wax, 0.2f), 0.0f, 0, a_r * 0.04f);
		}

		// Torn paper edge: notches along every side, cut with the colour behind the panel.
		void DeckleEdges(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, ImU32 a_behind, float a_depth)
		{
			auto side = [&](ImVec2 p0, ImVec2 p1, ImVec2 inward, int seed) {
				const ImVec2 d = p1 - p0;
				const float  len = std::sqrt(d.x * d.x + d.y * d.y);
				const ImVec2 u = d / len;
				const float  step = a_depth * 1.6f;
				const int    n = static_cast<int>(len / step);
				ImVec2       prev = p0;
				for (int i = 1; i <= n; ++i) {
					const float  t = std::fmin(len, i * step);
					const float  dep = a_depth * (0.25f + 0.75f * Hash(i, seed, 13));
					const ImVec2 q = p0 + u * t + inward * dep;
					const ImVec2 base0 = p0 + u * ((i - 1) * step), base1 = p0 + u * t;
					a_dl->AddQuadFilled(base0 - inward * 2.0f, base1 - inward * 2.0f, q, prev, a_behind);
					prev = q;
				}
			};
			side(a_a, ImVec2(a_b.x, a_a.y), ImVec2(0, 1), 1);
			side(ImVec2(a_b.x, a_a.y), a_b, ImVec2(-1, 0), 2);
			side(a_b, ImVec2(a_a.x, a_b.y), ImVec2(0, -1), 3);
			side(ImVec2(a_a.x, a_b.y), a_a, ImVec2(1, 0), 4);
		}

		// Background fill + light/depth for a filled box (buttons, chips, stat boxes).
		void RaisedFill(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, ImU32 a_col, float a_r, bool a_pressed)
		{
			const bool  clear = (a_col >> 24) < 8;
			const ImU32 top = clear ? IM_COL32(255, 245, 220, a_pressed ? 0 : 34) : (a_pressed ? Dark(a_col, 0.15f) : Bright(a_col, 0.24f));
			const ImU32 bot = clear ? IM_COL32(60, 40, 20, a_pressed ? 40 : 28) : (a_pressed ? Bright(a_col, 0.05f) : Dark(a_col, 0.32f));
			if (a_r > S(4.0f)) {
				a_dl->AddRectFilled(a_a, a_b, Mix(top, bot, 0.5f), a_r);
			} else {
				a_dl->AddRectFilledMultiColor(a_a, a_b, top, top, bot, bot);
			}
			if (!a_pressed) {
				a_dl->AddLine(ImVec2(a_a.x + 2, a_a.y + 1.5f), ImVec2(a_b.x - 2, a_a.y + 1.5f), IM_COL32(255, 248, 228, clear ? 60 : 70), 1.0f);
			}
		}
	}

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
		const float drop = (a_size - smallSize) * 0.80f;  // keep the small letters on the same baseline
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

	namespace
	{
		// A heading in the current look's style (small caps / tracked capitals / plain).
		void Heading(ImDrawList* a_dl, ImFont* a_font, float a_size, const ImVec2& a_pos, ImU32 a_col, const char* a_text)
		{
			switch (L->title) {
			case Title::SmallCaps:
				SmallCaps(a_dl, a_font, a_size, a_pos, a_col, a_text);
				break;
			case Title::Upper:
				{
					const float track = a_size * 0.14f;
					float       x = a_pos.x;
					char        ch[2] = { 0, 0 };
					for (const char* p = a_text; *p; ++p) {
						ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
						a_dl->AddText(a_font, a_size * 0.86f, ImVec2(x, a_pos.y + a_size * 0.08f), a_col, ch);
						x += a_font->CalcTextSizeA(a_size * 0.86f, FLT_MAX, 0.0f, ch).x + track;
					}
					break;
				}
			case Title::Normal:
				a_dl->AddText(a_font, a_size, a_pos, a_col, a_text);
				break;
			}
		}

		float HeadingWidth(ImFont* a_font, float a_size, const char* a_text)
		{
			switch (L->title) {
			case Title::SmallCaps:
				return SmallCapsWidth(a_font, a_size, a_text);
			case Title::Upper:
				{
					float w = 0.0f;
					char  ch[2] = { 0, 0 };
					for (const char* p = a_text; *p; ++p) {
						ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
						w += a_font->CalcTextSizeA(a_size * 0.86f, FLT_MAX, 0.0f, ch).x + a_size * 0.14f;
					}
					return w;
				}
			default:
				return a_font->CalcTextSizeA(a_size, FLT_MAX, 0.0f, a_text).x;
			}
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
		const float  wr = S(L->windowRounding);
		dl->PushClipRect(a, b, false);  // the window's own clip rect excludes its padding

		// background: vignette (lighter middle) or flat
		if (L->winMid != L->winEdge && wr <= 0.0f) {
			const ImU32  mid = L->winMid, edge = L->winEdge;
			const ImVec2 m = (a + b) * 0.5f;
			dl->AddRectFilledMultiColor(a, m, edge, edge, mid, edge);
			dl->AddRectFilledMultiColor(ImVec2(m.x, a.y), ImVec2(b.x, m.y), edge, edge, edge, mid);
			dl->AddRectFilledMultiColor(ImVec2(a.x, m.y), ImVec2(m.x, b.y), edge, mid, edge, edge);
			dl->AddRectFilledMultiColor(m, b, mid, edge, edge, edge);
		} else {
			dl->AddRectFilled(a, b, L->winMid, wr);
		}
		if (L->fx.textured && g_texGrain) {
			Tiled(dl, g_texGrain, a, b, IM_COL32(0, 0, 0, 255), wr);
		}
		if ((L->winOuter >> 24) != 0) {
			dl->AddRect(a, b, L->winOuter, wr, 0, S(2.0f));
		}
		if (L->fx.bevel) {
			Bevel(dl, a + ImVec2(1, 1), b - ImVec2(1, 1), S(1.5f), false, 40, 120);
		}
		if (L->fx.frame == 1) {  // metallic band with corner pieces
			const ImVec2 ba = a + ImVec2(S(5.0f), S(5.0f)), bb = b - ImVec2(S(5.0f), S(5.0f));
			dl->AddRect(ba - ImVec2(1, 1), bb + ImVec2(1, 1), IM_COL32(0, 0, 0, 160), 0.0f, 0, S(2.0f));
			GoldBand(dl, ba, bb, S(9.0f));
			dl->AddRect(ba + ImVec2(S(9.5f), S(9.5f)), bb - ImVec2(S(9.5f), S(9.5f)), IM_COL32(0, 0, 0, 120), 0.0f, 0, S(2.0f));
			const float r = S(15.0f), o = S(9.5f);
			CornerPiece(dl, ba + ImVec2(o, o), r);
			CornerPiece(dl, ImVec2(bb.x - o, ba.y + o), r);
			CornerPiece(dl, ImVec2(ba.x + o, bb.y - o), r);
			CornerPiece(dl, bb - ImVec2(o, o), r);
			dl->PopClipRect();
			return;
		}
		if (L->fx.frame == 2) {  // stitched leather
			const ImVec2 sa = a + ImVec2(S(11.0f), S(11.0f)), sb = b - ImVec2(S(11.0f), S(11.0f));
			Bevel(dl, a + ImVec2(S(5.0f), S(5.0f)), b - ImVec2(S(5.0f), S(5.0f)), S(1.2f), true, 30, 110);
			Stitches(dl, sa, sb, L->accentDim, S(9.0f), S(6.0f), S(2.0f));
			Bevel(dl, sa + ImVec2(S(7.0f), S(7.0f)), sb - ImVec2(S(7.0f), S(7.0f)), S(1.2f), false, 30, 110);
			dl->PopClipRect();
			return;
		}

		const float  inset = S(9.0f);
		const ImVec2 ia = a + ImVec2(inset, inset), ib = b - ImVec2(inset, inset);
		switch (L->chrome) {
		case Chrome::InsetDiamonds:
			{
				if (L->fx.bevel) {  // engraved line: dark groove + light lip
					dl->AddRect(ia + ImVec2(1, 1), ib + ImVec2(1, 1), IM_COL32(0, 0, 0, 150), 0.0f, 0, S(1.2f));
					dl->AddRect(ia, ib, Bright(L->accentFaint, 0.15f), 0.0f, 0, S(1.2f));
				} else {
					dl->AddRect(ia, ib, L->accentFaint, 0.0f, 0, S(1.2f));
				}
				const float r = S(6.0f);
				if (L->fx.bevel) {
					for (const ImVec2& c : { ia, ImVec2(ib.x, ia.y), ImVec2(ia.x, ib.y), ib }) {
						Diamond(dl, c + ImVec2(1.5f, 2.0f), r, IM_COL32(0, 0, 0, 140));
						Diamond(dl, c, r, L->accentDim);
						Diamond(dl, c - ImVec2(r * 0.15f, r * 0.15f), r * 0.55f, Bright(L->accent, 0.2f));
					}
					break;
				}
				Diamond(dl, ia, r, L->accentDim);
				Diamond(dl, ImVec2(ib.x, ia.y), r, L->accentDim);
				Diamond(dl, ImVec2(ia.x, ib.y), r, L->accentDim);
				Diamond(dl, ib, r, L->accentDim);
				break;
			}
		case Chrome::CornerBrackets:
			{
				dl->AddRect(ia, ib, L->accentFaint, 0.0f, 0, S(1.0f));
				const float k = S(34.0f), t = S(3.0f);
				const ImU32 col = L->accentDim;
				// top-left, top-right, bottom-left, bottom-right
				dl->AddRectFilled(ia, ia + ImVec2(k, t), col);
				dl->AddRectFilled(ia, ia + ImVec2(t, k), col);
				dl->AddRectFilled(ImVec2(ib.x - k, ia.y), ImVec2(ib.x, ia.y + t), col);
				dl->AddRectFilled(ImVec2(ib.x - t, ia.y), ImVec2(ib.x, ia.y + k), col);
				dl->AddRectFilled(ImVec2(ia.x, ib.y - t), ImVec2(ia.x + k, ib.y), col);
				dl->AddRectFilled(ImVec2(ia.x, ib.y - k), ImVec2(ia.x + t, ib.y), col);
				dl->AddRectFilled(ib - ImVec2(k, t), ib, col);
				dl->AddRectFilled(ib - ImVec2(t, k), ib, col);
				break;
			}
		case Chrome::Rules:
			FadingRule(dl, a.x + S(20.0f), b.x - S(20.0f), a.y + S(8.0f), L->accentDim, S(1.5f), true);
			FadingRule(dl, a.x + S(20.0f), b.x - S(20.0f), b.y - S(10.0f), L->accentDim, S(1.5f), true);
			break;
		case Chrome::DoubleLine:
			dl->AddRect(ia, ib, L->accentDim, 0.0f, 0, S(1.4f));
			dl->AddRect(ia + ImVec2(S(5.0f), S(5.0f)), ib - ImVec2(S(5.0f), S(5.0f)), L->accentFaint, 0.0f, 0, S(1.0f));
			break;
		case Chrome::Rounded:
			dl->AddRect(a + ImVec2(S(0.5f), S(0.5f)), b - ImVec2(S(0.5f), S(0.5f)), L->accentFaint, wr, 0, S(1.2f));
			break;
		case Chrome::Rivets:
			{
				dl->AddRect(ia, ib, L->accentDim, 0.0f, 0, S(2.0f));
				dl->AddRect(ia + ImVec2(S(6.0f), S(6.0f)), ib - ImVec2(S(6.0f), S(6.0f)), L->accentFaint, 0.0f, 0, S(1.0f));
				const float  rr = S(4.5f);
				const ImVec2 pts[] = { ia, ImVec2(ib.x, ia.y), ImVec2(ia.x, ib.y), ib, ImVec2((ia.x + ib.x) * 0.5f, ia.y),
					ImVec2((ia.x + ib.x) * 0.5f, ib.y), ImVec2(ia.x, (ia.y + ib.y) * 0.5f), ImVec2(ib.x, (ia.y + ib.y) * 0.5f) };
				for (const auto& p : pts) {
					dl->AddCircleFilled(p, rr, L->accentDim);
					dl->AddCircleFilled(p - ImVec2(rr * 0.3f, rr * 0.3f), rr * 0.45f, L->accent);
				}
				break;
			}
		}
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
		const float  r = S(L->rounding);

		// title + subtitle
		float tx = 0.0f;
		if (L->fx.medallion) {
			Medallion(dl, p + ImVec2(S(34.0f), S(34.0f)), S(32.0f));
			tx = S(80.0f);
		}
		if (L->fx.bevel) {  // engraved: dark copy below, faint highlight above
			Heading(dl, g_bold, S(34.0f), p + ImVec2(tx + S(4.0f), S(2.0f)) + ImVec2(S(1.0f), S(2.0f)), IM_COL32(0, 0, 0, 170), a_title);
			Heading(dl, g_bold, S(34.0f), p + ImVec2(tx + S(4.0f), S(2.0f)), L->accent, a_title);
		} else {
			Heading(dl, g_bold, S(34.0f), p + ImVec2(tx + S(4.0f), S(2.0f)), L->accent, a_title);
		}
		if (a_subtitle && *a_subtitle) {
			dl->AddText(g_italic, S(18.0f), p + ImVec2(tx + S(6.0f), S(46.0f)), L->textDim, a_subtitle);
		}
		if (L->fx.bevel) {  // gold rule under the header
			FadingRule(dl, p.x, p.x + w, p.y + h - S(4.0f), L->accentDim, S(1.2f), true);
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
			if (L->fx.shadows) {
				SoftShadow(dl, ca, cb, r, S(8.0f), 80);
			}
			dl->AddRectFilled(ca, cb, hov ? L->closeHov : L->boxBg, r);
			if (L->fx.bevel) {
				RaisedFill(dl, ca, cb, C(0, 0, 0, 0), r, ImGui::IsItemActive());
				Bevel(dl, ca, cb, S(1.5f), false, 45, 130);
			}
			dl->AddRect(ca, cb, hov ? L->accent : L->accentDim, r, 0, S(1.2f));
			const float  k = box * 0.26f;
			const ImVec2 cc = (ca + cb) * 0.5f;
			dl->AddLine(cc - ImVec2(k, k), cc + ImVec2(k, k), L->closeX, S(2.4f));
			dl->AddLine(cc + ImVec2(-k, k), cc + ImVec2(k, -k), L->closeX, S(2.4f));
			right -= box + S(18.0f);
		}

		// stat boxes, right to left
		for (int i = a_statCount - 1; i >= 0; --i) {
			const float  bw = S(130.0f), bh = box;
			const ImVec2 sa(right - bw, p.y + S(4.0f)), sb(right, p.y + S(4.0f) + bh);
			dl->AddRectFilled(sa, sb, L->fx.bevel ? Dark(L->boxBg, 0.25f) : L->boxBg, r);
			if (L->fx.bevel) {  // sunken counter
				dl->AddRectFilledMultiColor(sa, ImVec2(sb.x, sa.y + S(12.0f)), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
				Bevel(dl, sa, sb, S(1.5f), true, 40, 140);
			}
			dl->AddRect(sa, sb, L->accentDim, r, 0, S(1.2f));
			const float lw = HeadingWidth(g_body, S(13.0f), a_stats[i].label);
			Heading(dl, g_body, S(13.0f), ImVec2((sa.x + sb.x - lw) * 0.5f, sa.y + S(6.0f)), L->textDim, a_stats[i].label);
			const float vs = S(26.0f);
			const float vw = g_body->CalcTextSizeA(vs, FLT_MAX, 0.0f, a_stats[i].value).x;
			dl->AddText(g_body, vs, ImVec2((sa.x + sb.x - vw) * 0.5f, sa.y + S(22.0f)), L->text, a_stats[i].value);
			right -= bw + S(12.0f);
		}

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h));
		return closed;
	}

	// ------------------------------------------------------------------ content panel
	bool BeginParchment(const char* a_id, const ImVec2& a_size, bool a_scroll, bool a_autoHeight)
	{
		const bool fancy = L->fx.shadows || L->fx.textured || L->fx.deckle;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, fancy ? C(0, 0, 0, 0) : L->panel);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22.0f), S(18.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(L->windowRounding > 0.0f ? L->rounding + 2.0f : L->rounding));
		const bool visible = ImGui::BeginChild(a_id, a_size,
			ImGuiChildFlags_AlwaysUseWindowPadding | (a_autoHeight ? ImGuiChildFlags_AutoResizeY : 0),
			a_scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 a = ImGui::GetWindowPos();
		const ImVec2 b = a + ImGui::GetWindowSize();
		dl->PushClipRect(a, b, false);
		const float pr = S(L->windowRounding > 0.0f ? L->rounding + 2.0f : L->rounding);
		if (fancy) {
			dl->AddRectFilled(a, b, L->panel, pr);
			if (L->fx.textured && g_texParch) {
				Tiled(dl, g_texParch, a, b, Dark(L->panelBorder, 0.35f), pr);
			}
		}
		if ((L->panelShade >> 24) != 0) {  // aged / shaded edges
			const float e = S(26.0f);
			const ImU32 dark = L->panelShade, none = WithAlpha(L->panelShade, 0);
			dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + e), dark, dark, none, none);
			dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - e), b, none, none, dark, dark);
			dl->AddRectFilledMultiColor(a, ImVec2(a.x + e, b.y), dark, none, none, dark);
			dl->AddRectFilledMultiColor(ImVec2(b.x - e, a.y), b, none, dark, dark, none);
		}
		if (L->fx.bevel) {  // paper lit from above: bright top edge, darker bottom edge
			dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + S(5.0f)), IM_COL32(255, 250, 230, 70), IM_COL32(255, 250, 230, 70), IM_COL32(255, 250, 230, 0), IM_COL32(255, 250, 230, 0));
			dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - S(8.0f)), b, IM_COL32(60, 35, 15, 0), IM_COL32(60, 35, 15, 0), IM_COL32(60, 35, 15, 60), IM_COL32(60, 35, 15, 60));
		}
		if (L->fx.deckle) {
			DeckleEdges(dl, a, b, L->winMid, S(4.0f));
			dl->AddRect(a + ImVec2(S(6.0f), S(6.0f)), b - ImVec2(S(6.0f), S(6.0f)), WithAlpha(L->panelBorder, 110), 0.0f, 0, S(1.0f));
		} else {
			dl->AddRect(a, b, L->panelBorder, pr, 0, S(1.5f));
		}
		if (L->fx.seal && (b.y - a.y) > S(260.0f) && (b.x - a.x) > S(500.0f)) {
			WaxSeal(dl, ImVec2(b.x - S(36.0f), a.y + S(28.0f)), S(21.0f));
		}
		dl->PopClipRect();

		++g_parchmentDepth;
		ImGui::PushStyleColor(ImGuiCol_Text, L->ink);
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, L->inkSoft);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, L->pFrame);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, L->pFrameHov);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, L->pFrameAct);
		ImGui::PushStyleColor(ImGuiCol_Border, L->panelBorder);
		ImGui::PushStyleColor(ImGuiCol_SliderGrab, L->pGrab);
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, L->pGrabAct);
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, C(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, WithAlpha(L->inkSoft, 140));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, WithAlpha(L->inkSoft, 200));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, (L->panel >> 24) >= 200 ? L->panel : WithAlpha(L->winMid, 250));  // tooltips
		ImGui::PushStyleColor(ImGuiCol_Header, L->pBtnHov);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, L->pBtnAct);
		return visible;
	}

	void EndParchment()
	{
		ImGui::PopStyleColor(14);
		--g_parchmentDepth;
		ImGui::EndChild();
		if (L->fx.shadows) {  // on the parent's draw list, which renders under the child window
			SoftShadow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), S(L->rounding), S(14.0f), 130);
		}
	}

	void SectionLabel(const char* a_text)
	{
		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  w = ImGui::GetContentRegionAvail().x;
		const float  size = S(16.0f);
		const bool   parch = g_parchmentDepth > 0;
		Heading(dl, g_body, size, p, parch ? L->inkSoft : L->textDim, a_text);
		dl->AddLine(ImVec2(p.x, p.y + size + S(7.0f)), ImVec2(p.x + w, p.y + size + S(7.0f)), parch ? L->panelRule : L->accentFaint, S(1.0f));
		ImGui::Dummy(ImVec2(w, size + S(12.0f)));
	}

	// ------------------------------------------------------------------ controls
	namespace
	{
		// ImGui::Button with the current Button colours, but drawn with light and depth when the look asks for it.
		bool DepthButton(const char* a_label, const ImVec2& a_size, float a_r, bool a_parch)
		{
			if (!L->fx.bevel) {
				return ImGui::Button(a_label, a_size);
			}
			auto&       st = ImGui::GetStyle();
			const ImU32 base = ImGui::GetColorU32(ImGuiCol_Button), hov = ImGui::GetColorU32(ImGuiCol_ButtonHovered),
						act = ImGui::GetColorU32(ImGuiCol_ButtonActive), border = ImGui::GetColorU32(ImGuiCol_Border);
			auto* dl = ImGui::GetWindowDrawList();
			dl->ChannelsSplit(2);
			dl->ChannelsSetCurrent(1);
			ImGui::PushStyleColor(ImGuiCol_Button, C(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, C(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, C(0, 0, 0, 0));
			const bool pressed = ImGui::Button(a_label, a_size);
			ImGui::PopStyleColor(4);
			const bool   down = ImGui::IsItemActive() && ImGui::IsItemHovered();
			const ImU32  fill = down ? act : (ImGui::IsItemHovered() ? hov : base);
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			dl->ChannelsSetCurrent(0);
			if (L->fx.shadows && !down) {
				SoftShadow(dl, a, b, a_r, S(5.0f), a_parch ? 45 : 90);
			}
			if ((fill >> 24) >= 8) {
				dl->AddRectFilled(a, b, fill, a_r);
			}
			RaisedFill(dl, a, b, fill, a_r, down);
			Bevel(dl, a, b, S(1.2f), down, a_parch ? 90 : 50, a_parch ? 50 : 120);
			dl->AddRect(a, b, border, a_r, 0, st.FrameBorderSize);
			dl->ChannelsMerge();
			return pressed;
		}
	}

	bool Button(const char* a_label, const ImVec2& a_size, bool a_primary)
	{
		const bool parch = g_parchmentDepth > 0;
		ImVec2     size = a_size;
		if (size.y <= 0.0f) {
			size.y = S(46.0f);
		}
		int  colors = 0;
		auto push = [&](ImGuiCol a_idx, ImU32 a_col) { ImGui::PushStyleColor(a_idx, a_col); ++colors; };
		if (parch) {
			push(ImGuiCol_Button, a_primary ? L->pPriBg : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, a_primary ? L->pPriHov : L->pBtnHov);
			push(ImGuiCol_ButtonActive, a_primary ? L->pPriBg : L->pBtnAct);
			push(ImGuiCol_Text, a_primary ? L->pPriText : L->ink);
			push(ImGuiCol_Border, a_primary ? L->pPriBg : L->pBorder);
		} else {
			push(ImGuiCol_Button, a_primary ? L->priBg : L->btnBg);
			push(ImGuiCol_ButtonHovered, a_primary ? L->priHov : L->btnHov);
			push(ImGuiCol_ButtonActive, a_primary ? L->priAct : L->btnAct);
			push(ImGuiCol_Text, a_primary ? L->priText : L->btnText);
			push(ImGuiCol_Border, a_primary ? L->priBorder : L->btnBorder);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(20.0f), S(10.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.2f));
		const bool pressed = DepthButton(a_label, size, S(L->rounding), parch);
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
			push(ImGuiCol_Button, a_selected ? L->pSel : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, a_selected ? L->pSelHov : L->pBtnHov);
			push(ImGuiCol_ButtonActive, a_selected ? L->pSel : L->pBtnAct);
			push(ImGuiCol_Text, a_selected ? L->pSelText : L->ink);
			push(ImGuiCol_Border, a_selected ? L->pSel : L->pBorder);
		} else {
			push(ImGuiCol_Button, a_selected ? L->priBg : C(0, 0, 0, 0));
			push(ImGuiCol_ButtonHovered, L->btnHov);
			push(ImGuiCol_ButtonActive, L->btnAct);
			push(ImGuiCol_Text, a_selected ? L->priText : L->text);
			push(ImGuiCol_Border, a_selected ? L->priBorder : L->btnBorder);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(8.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.2f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(L->rounding > 4.0f ? 16.0f : L->rounding));
		const bool pressed = DepthButton(a_label, ImVec2(0, S(40.0f)), S(L->rounding > 4.0f ? 16.0f : L->rounding), parch);
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

	float CardInset() { return S(L->fx.gilt ? 13.0f : 4.0f); }
	void CardBack(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, bool a_hovered)
	{
		const float r = S(L->rounding);
		if (L->fx.shadows) {
			SoftShadow(a_dl, a_a, a_b, r, S(a_hovered ? 16.0f : 10.0f), a_hovered ? 120 : 90);
		} else {
			a_dl->AddRectFilled(a_a + ImVec2(S(3), S(4)), a_b + ImVec2(S(3), S(4)), L->shadow, r);
		}
		if (L->fx.gilt) {  // gilt frame + dark mat
			a_dl->AddRectFilled(a_a, a_b, Dark(L->accentDim, 0.3f));
			GoldBand(a_dl, a_a, a_b, S(8.0f));
			const ImVec2 ma = a_a + ImVec2(S(8.0f), S(8.0f)), mb = a_b - ImVec2(S(8.0f), S(8.0f));
			a_dl->AddRectFilled(ma, mb, L->well);
			Bevel(a_dl, ma, mb, S(2.0f), true, 30, 140);
			return;
		}
		a_dl->AddRectFilled(a_a, a_b, L->well, r);
		if (L->fx.bevel) {
			Bevel(a_dl, a_a, a_b, S(2.0f), true, 50, 120);
		}
	}
	void CardFront(ImDrawList* a_dl, const ImVec2& a_a, const ImVec2& a_b, bool a_hovered)
	{
		const float r = S(L->rounding);
		if (L->fx.gilt) {
			if (a_hovered) {
				a_dl->AddRect(a_a - ImVec2(S(2), S(2)), a_b + ImVec2(S(2), S(2)), Bright(L->accent, 0.3f), 0.0f, 0, S(2.5f));
				a_dl->AddRectFilled(a_a + ImVec2(S(8), S(8)), a_b - ImVec2(S(8), S(8)), IM_COL32(255, 240, 200, 18));
			}
			return;
		}
		if (L->fx.bevel && !a_hovered) {
			a_dl->AddRect(a_a, a_b, L->cardBorder, r, 0, S(1.2f));
			a_dl->AddLine(a_a + ImVec2(r, 1), ImVec2(a_b.x - r, a_a.y + 1), IM_COL32(255, 248, 225, 50), 1.0f);
			return;
		}
		a_dl->AddRect(a_a, a_b, a_hovered ? L->accent : L->cardBorder, r, 0, a_hovered ? S(3.0f) : S(1.2f));
	}
}
