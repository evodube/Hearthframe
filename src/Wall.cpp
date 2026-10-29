#include "Wall.h"

#include "Dds.h"
#include "Frames.h"

namespace fs = std::filesystem;

namespace Wall
{
	namespace
	{
		constexpr std::string_view kPluginFile = "FolderGallery.esp"sv;
		constexpr std::uint32_t    kSerializationID = 'FGAL';
		constexpr std::uint32_t    kRecordFrames = 'FRMS';
		constexpr std::uint32_t    kRecordVersion = 1;
		constexpr std::uint32_t    kRecordPreview = 'PREV';
		constexpr std::uint32_t    kRecordRemovals = 'DELS';
		// Game path of the folder for hung pictures' textures ("textures\\...\\"), from Settings::placedFolder.
		std::mutex  g_dirMtx;
		std::string g_placedDir = "textures\\FolderGallery\\Placed\\";
		std::string PlacedDir()
		{
			std::lock_guard lk(g_dirMtx);
			return g_placedDir;
		}
		std::string GamePathFor(std::string a_relative)
		{
			while (!a_relative.empty() && (a_relative.back() == '\\' || a_relative.back() == '/')) {
				a_relative.pop_back();
			}
			while (!a_relative.empty() && (a_relative.front() == '\\' || a_relative.front() == '/')) {
				a_relative.erase(a_relative.begin());
			}
			std::replace(a_relative.begin(), a_relative.end(), '/', '\\');
			return "textures\\" + (a_relative.empty() ? std::string("FolderGallery\\Placed") : a_relative) + "\\";
		}
		constexpr float            kDefaultOffset = 0.3f;
		constexpr float            kMinOffset = -40.0f;  // wall collision often sits well in front of the visible wall
		constexpr float            kMaxOffset = 12.0f;
		constexpr float            kMinScale = 0.3f;
		constexpr float            kMaxScale = 3.0f;
		constexpr const char*      kFlatNormal = "textures\\FolderGallery\\canvas_flat_n.dds";

		Settings g_settings;

		struct Entry
		{
			std::string   dds;      // game path, "textures\\FolderGallery\\Placed\\....dds"
			std::string   picture;  // UTF-8 full path of the source picture
			std::uint8_t  shape = 0;
			std::uint8_t  style = 0;
		};

		enum class Conv
		{
			Running,
			Ready,
			Failed
		};

		std::mutex                               g_mtx;  // guards everything below
		std::unordered_map<RE::FormID, Entry>    g_frames;
		std::unordered_map<std::string, Conv>    g_conversions;
		std::array<RE::FormID, Frames::kFrameCount> g_frameForms{};

		struct Placement
		{
			fs::path            picture;
			int                 shape = 0;
			int                 style = 0;
			RE::ObjectRefHandle moving;
			float               scale = 1.0f;
			float               offset = kDefaultOffset;  // distance of the frame's back from the wall
		};
		std::optional<Placement> g_placement;
		std::atomic<bool>        g_placing{ false };
		std::atomic<bool>        g_placeQueued{ false };

		std::optional<FrameInfo> g_activated;

		// ------------------------------------------------------------------ helpers
		std::string ToUtf8(const fs::path& p)
		{
			const auto s = p.u8string();
			return std::string(s.begin(), s.end());
		}

		fs::path FromUtf8(const std::string& s)
		{
			return fs::path(std::u8string(s.begin(), s.end()));
		}

		fs::path DataPath(const std::string& a_gamePath)  // "textures\\x.dds" -> <game>\Data\textures\x.dds
		{
			std::error_code ec;
			return fs::absolute(fs::path(L"Data") / FromUtf8(a_gamePath), ec);
		}

		enum class Note
		{
			Size,
			Distance,
			FrameName,
			Help,
			Confirm,
			Warning,
			Error
		};

		// Top-left game notification, if that kind is switched on in the INI. Any thread.
		void Notify(Note a_kind, const std::string& a_msg)
		{
			const auto& s = g_settings;
			bool        show = false;
			switch (a_kind) {
			case Note::Size:
				show = s.showSize;
				break;
			case Note::Distance:
				show = s.showDistance;
				break;
			case Note::FrameName:
				show = s.showFrameName;
				break;
			case Note::Help:
				show = s.showPlacementHelp;
				break;
			case Note::Confirm:
				show = s.showConfirmations;
				break;
			case Note::Warning:
				show = s.showPlacementWarnings;
				spdlog::info("Notice: {}", a_msg);
				break;
			case Note::Error:
				show = s.showErrors;
				spdlog::warn("Notice: {}", a_msg);
				break;
			}
			if (show && s.notifications) {
				SKSE::GetTaskInterface()->AddTask([a_msg] { RE::DebugNotification(a_msg.c_str()); });
			}
		}

		int FrameIndex(const RE::TESForm* a_base)
		{
			if (!a_base) {
				return -1;
			}
			const auto id = a_base->GetFormID();
			for (int i = 0; i < static_cast<int>(g_frameForms.size()); ++i) {
				if (g_frameForms[i] && g_frameForms[i] == id) {
					return i;
				}
			}
			return -1;
		}

		std::uint64_t Fnv1a(std::string_view a_data, std::uint64_t a_h = 1469598103934665603ull)
		{
			for (const unsigned char c : a_data) {
				a_h = (a_h ^ c) * 1099511628211ull;
			}
			return a_h;
		}

		// Stable texture name for (picture file contents as of now, shape, resolution).
		std::string DdsGamePath(const fs::path& a_picture, int a_shape)
		{
			std::error_code ec;
			const auto      size = fs::file_size(a_picture, ec);
			const auto      time = fs::last_write_time(a_picture, ec).time_since_epoch().count();
			const auto      [w, h] = TextureSize(a_shape);
			auto            key = fmt::format("{}|{}|{}|{}x{}", ToUtf8(a_picture), size, time, w, h);
			std::string     stem;
			for (const char c : ToUtf8(a_picture.stem())) {
				if (std::isalnum(static_cast<unsigned char>(c)) && stem.size() < 40) {
					stem += c;
				}
			}
			if (stem.empty()) {
				stem = "picture";
			}
			return fmt::format("{}{}_{}_{:016x}.dds", PlacedDir(), stem, Frames::kShapes[a_shape].name, Fnv1a(key));
		}

		// Deletes converted pictures in Placed\ that nothing in this save uses any more. Other saves are
		// safe: their frames re-create a missing texture from the original picture when loaded.
		void CleanupUnusedTextures()
		{
			if (!g_settings.cleanUp) {
				return;
			}
			std::unordered_set<std::string> keep;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					keep.insert(DataPath(e.dds).filename().string());
				}
				if (g_placement) {
					keep.insert(DataPath(DdsGamePath(g_placement->picture, g_placement->shape)).filename().string());
				}
				for (const auto& [dds, state] : g_conversions) {
					if (state == Conv::Running) {
						keep.insert(DataPath(dds).filename().string());
					}
				}
			}
			std::thread([keep = std::move(keep)] {
				std::error_code ec;
				const auto      dir = DataPath(PlacedDir());
				int             removed = 0;
				for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
					const auto& p = it->path();
					auto        ext = p.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
					if (ext != ".dds" || keep.contains(p.filename().string())) {
						continue;
					}
					std::error_code rm;
					if (fs::remove(p, rm)) {
						++removed;
						std::lock_guard lk(g_mtx);
						for (auto c = g_conversions.begin(); c != g_conversions.end();) {
							c = DataPath(c->first).filename() == p.filename() ? g_conversions.erase(c) : std::next(c);
						}
					}
				}
				if (removed) {
					spdlog::info("Cleaned up {} unused picture texture(s) in {}", removed, ToUtf8(dir));
				}
			}).detach();
		}

		// ------------------------------------------------------------------ applying a picture to a frame's 3D
		std::string NormalMapFor(RE::BSLightingShaderMaterialBase* a_material)
		{
			if (a_material && a_material->textureSet) {
				if (const char* n = a_material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal); n && *n) {
					std::error_code ec;
					if (fs::exists(DataPath(n), ec)) {
						return n;
					}
				}
			}
			return kFlatNormal;
		}

		bool ApplyTexture(RE::TESObjectREFR* a_ref, const std::string& a_dds)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) {
				return false;
			}
			auto* obj = root->GetObjectByName(RE::BSFixedString("SGPicture"));
			auto* geom = obj ? obj->AsGeometry() : nullptr;
			if (!geom) {
				spdlog::warn("{:08X}: no SGPicture shape in the frame's 3D", a_ref->GetFormID());
				return false;
			}
			auto* shader = netimmerse_cast<RE::BSLightingShaderProperty*>(
				geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get());
			if (!shader || !shader->material) {
				spdlog::warn("{:08X}: SGPicture has no lighting shader", a_ref->GetFormID());
				return false;
			}
			auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);

			auto* textures = RE::BSShaderTextureSet::Create();
			if (!textures) {
				return false;
			}
			const auto normal = NormalMapFor(material);
			textures->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, a_dds.c_str());
			textures->SetTexturePath(RE::BSTextureSet::Texture::kNormal, normal.c_str());

			// Same pattern the game uses for texture-set swaps: copy the material, give the copy the
			// new textures, hand it to the shader (which makes its own cached copy), free ours.
			auto* copy = static_cast<RE::BSLightingShaderMaterialBase*>(material->Create());
			if (!copy) {
				return false;
			}
			copy->CopyMembers(material);
			copy->ClearTextures();
			copy->OnLoadTextureSet(0, textures);
			shader->SetMaterial(copy, true);
			shader->SetupGeometry(geom);
			shader->FinishSetupGeometry(geom);
			copy->~BSLightingShaderMaterialBase();
			RE::free(copy);
			return true;
		}

		// Main thread only. Returns true when done (applied, or nothing to apply), false to retry later.
		bool ApplyToRef(RE::FormID a_id)
		{
			std::string dds;
			{
				std::lock_guard lk(g_mtx);
				const auto      it = g_frames.find(a_id);
				if (it == g_frames.end()) {
					return true;
				}
				const auto c = g_conversions.find(it->second.dds);
				if (c == g_conversions.end() || c->second == Conv::Failed) {
					return true;
				}
				if (c->second != Conv::Ready) {
					return false;  // still converting
				}
				dds = it->second.dds;
			}
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
			if (!ref || ref->IsDisabled()) {
				return true;
			}
			if (!ref->Get3D()) {
				return false;  // 3D not loaded yet
			}
			if (ApplyTexture(ref, dds)) {
				spdlog::info("{:08X}: picture applied ({})", a_id, dds);
			}
			return true;
		}

		// Frames taken down while their cell wasn't loaded: the game only really removes a reference once
		// its 3D exists, so they are hidden now and deleted for good the next time their cell loads.
		std::unordered_set<RE::FormID> g_removals;  // guarded by g_mtx

		// Takes a frame out of the world. Main thread.
		void DeleteFrameRef(RE::TESObjectREFR* a_ref, const char* a_why = "removed")
		{
			if (!a_ref) {
				return;
			}
			const auto id = a_ref->GetFormID();
			const bool loaded = a_ref->Get3D() != nullptr;
			spdlog::info("Frame {:08X} {} ({})", id, a_why, loaded ? "loaded" : "cell not loaded");
			a_ref->Disable();
			if (!loaded) {
				// keep it from showing when its cell loads, and remember to finish the job then
				a_ref->formFlags |= RE::TESForm::RecordFlags::kDisabled;
				a_ref->AddChange(1u << 0);  // CHANGE_FORM_FLAGS - kept in the save
				std::lock_guard lk(g_mtx);
				g_removals.insert(id);
				spdlog::info("Frame {:08X} is in an unloaded cell: hidden now, removed when its cell loads", id);
			}
			a_ref->SetDelete(true);
		}

		// Finishes removals whose frame has loaded since. Main thread.
		void ProcessRemovals()
		{
			std::vector<RE::FormID> ids;
			{
				std::lock_guard lk(g_mtx);
				if (g_removals.empty()) {
					return;
				}
				ids.assign(g_removals.begin(), g_removals.end());
			}
			for (const auto id : ids) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref || !ref->Get3D()) {
					continue;  // not loaded (a temporary frame isn't even in memory while its cell is unloaded)
				}
				ref->Disable();
				ref->SetDelete(true);
				std::lock_guard lk(g_mtx);
				g_removals.erase(id);
				spdlog::info("Frame {:08X}: removal finished now that its cell is loaded", id);
			}
		}

		// Frames whose picture still has to be put on once their 3D exists (main thread only).
		std::unordered_map<RE::FormID, int> g_pending;  // form id -> ticks left

		void QueueApply(RE::FormID a_id, int a_ticks = 600)
		{
			auto& left = g_pending[a_id];
			left = (std::max)(left, a_ticks);
		}

		void ProcessPending()
		{
			for (auto it = g_pending.begin(); it != g_pending.end();) {
				if (ApplyToRef(it->first) || --it->second <= 0) {
					it = g_pending.erase(it);
				} else {
					++it;
				}
			}
		}

		void SetRefScale(RE::TESObjectREFR* a_ref, float a_scale)
		{
			a_ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::lround(a_scale * 100.0f));
			a_ref->AddChange(1u << 4);  // CHANGE_REFR_SCALE - kept in the save
			if (auto* root = a_ref->Get3D()) {
				root->local.scale = a_scale;
				RE::NiUpdateData ud{};
				root->Update(ud);
			}
		}

		void ApplyToAllUsing(const std::string& a_dds)
		{
			std::vector<RE::FormID> ids;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					if (e.dds == a_dds) {
						ids.push_back(id);
					}
				}
			}
			for (const auto id : ids) {
				QueueApply(id);
			}
		}

		// Makes sure a_dds exists on disk, converting a_picture in the background if needed.
		void EnsureTexture(const std::string& a_dds, const fs::path& a_picture, int a_shape)
		{
			std::error_code ec;
			{
				std::lock_guard lk(g_mtx);
				if (fs::exists(DataPath(a_dds), ec)) {
					g_conversions[a_dds] = Conv::Ready;
					return;
				}
				if (const auto it = g_conversions.find(a_dds); it != g_conversions.end() && it->second == Conv::Running) {
					return;  // already being converted
				}
				g_conversions[a_dds] = Conv::Running;
			}
			const auto [w, h] = TextureSize(a_shape);
			std::thread([a_dds, a_picture, w, h] {
				std::string err;
				const auto  t0 = std::chrono::steady_clock::now();
				bool        ok = false;
				try {
					ok = Dds::ConvertPicture(a_picture, w, h, DataPath(a_dds), err);
				} catch (const std::exception& e) {
					err = e.what();
				}
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
				{
					std::lock_guard lk(g_mtx);
					g_conversions[a_dds] = ok ? Conv::Ready : Conv::Failed;
				}
				if (ok) {
					spdlog::info("Converted {} -> {} ({}x{}, {} ms)", ToUtf8(a_picture), a_dds, w, h, ms);
					SKSE::GetTaskInterface()->AddTask([a_dds] { ApplyToAllUsing(a_dds); });  // queues the frames using it
				} else {
					spdlog::error("Could not convert {}: {}", ToUtf8(a_picture), err);
					Notify(Note::Error, "Folder Gallery: could not convert that picture (see FolderGallery.log)");
				}
			}).detach();
		}

		// ------------------------------------------------------------------ placement (main thread, SKSE task)
		struct Hit
		{
			RE::NiPoint3   pos;
			RE::NiPoint3   normal;
			RE::TESObjectREFR* ref = nullptr;
		};

		std::optional<Hit> RaycastFromCamera(float a_maxDistance)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world || !camera || !camera->cameraRoot) {
				return std::nullopt;
			}
			const auto& xf = camera->cameraRoot->world;
			const RE::NiPoint3 from = xf.translate;
			RE::NiPoint3       dir{ xf.rotate.entry[0][1], xf.rotate.entry[1][1], xf.rotate.entry[2][1] };
			const float        len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len < 1e-4f) {
				return std::nullopt;
			}
			dir = { dir.x / len, dir.y / len, dir.z / len };
			// In third person the camera sits behind the player; reach is measured from the player.
			const RE::NiPoint3 p = player->GetPosition();
			const float camToPlayer = std::sqrt((p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y));
			const float dist = a_maxDistance + camToPlayer;
			const RE::NiPoint3 to{ from.x + dir.x * dist, from.y + dir.y * dist, from.z + dir.z * dist };

			const float   s = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(from.x * s, from.y * s, from.z * s, 0.0f);
			pick.rayInput.to = RE::hkVector4(to.x * s, to.y * s, to.z * s, 0.0f);
			std::uint32_t filter = 0;
			player->GetCollisionFilterInfo(filter);
			pick.rayInput.filterInfo = (filter & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);

			world->PickObject(pick);
			if (!pick.rayOutput.HasHit()) {
				return std::nullopt;
			}
			alignas(16) float n[4];
			_mm_store_ps(n, pick.rayOutput.normal.quad);
			const float f = pick.rayOutput.hitFraction;
			Hit out;
			out.pos = { from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f };
			out.normal = { n[0], n[1], n[2] };
			if (pick.rayOutput.rootCollidable) {
				out.ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			}
			return out;
		}

		// Activator form for a frame index, from the IDs resolved in Init().
		RE::TESObjectACTI* FrameForm(int a_shape, int a_style)
		{
			const int idx = Frames::Index(a_shape, a_style);
			const auto id = g_frameForms[idx];
			return id ? RE::TESForm::LookupByID<RE::TESObjectACTI>(id) : nullptr;
		}

		struct Pose
		{
			RE::NiPoint3 pos;
			float        nx = 0.0f, ny = 1.0f;  // horizontal wall normal (unit)
		};

		// Where a frame would hang right now, or why it can't.
		std::optional<Pose> ComputePose(const char*& a_whyNot, RE::TESObjectREFR* a_ignore = nullptr, float a_offset = kDefaultOffset)
		{
			const auto hit = RaycastFromCamera(g_settings.maxDistance);
			if (!hit) {
				a_whyNot = "Nothing in reach - aim at a wall closer to you";
				return std::nullopt;
			}
			if (hit->ref && hit->ref != a_ignore && hit->ref->Is(RE::FormType::ActorCharacter)) {
				a_whyNot = "You can't hang a picture on someone";
				return std::nullopt;
			}
			const auto& nrm = hit->normal;
			const float nlen = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y);
			if (std::fabs(nrm.z) > 0.6f || nlen < 0.3f) {
				a_whyNot = "That's not a wall - aim at a vertical surface";
				return std::nullopt;
			}
			Pose pose;
			pose.nx = nrm.x / nlen;
			pose.ny = nrm.y / nlen;
			// Frame origin is the centre of its back; a_offset moves it out from (or into) the wall.
			pose.pos = { hit->pos.x + pose.nx * a_offset, hit->pos.y + pose.ny * a_offset, hit->pos.z };
			return pose;
		}

		// Skyrim's Z angle turns local +Y (the frame's front) towards (sin z, cos z).
		float AngleZ(const Pose& a_pose) { return std::atan2(a_pose.nx, a_pose.ny); }

		// Rotation whose local +Y is the wall normal and local +Z is up (exact, no Euler conventions).
		RE::NiMatrix3 RotationFor(const Pose& a_pose)
		{
			RE::NiMatrix3 m;
			m.entry[0][0] = a_pose.ny;  m.entry[0][1] = a_pose.nx; m.entry[0][2] = 0.0f;
			m.entry[1][0] = -a_pose.nx; m.entry[1][1] = a_pose.ny; m.entry[1][2] = 0.0f;
			m.entry[2][0] = 0.0f;       m.entry[2][1] = 0.0f;      m.entry[2][2] = 1.0f;
			return m;
		}

		// ------------------------------------------------------------------ preview frame (main thread)
		RE::ObjectRefHandle g_preview;
		bool                g_previewReady = false;     // collision switched off
		std::string         g_previewTexture;          // dds already applied to the preview
		bool                g_previewVisible = false;
		std::atomic<bool>   g_previewQueued{ false };
		// Where the current placement started: going through a door cancels it (main thread only).
		RE::TESObjectCELL*  g_placeCell = nullptr;
		RE::TESWorldSpace*  g_placeWorld = nullptr;
		std::atomic<bool>   g_placeCellReset{ true };
		int                 g_previewTicks = 0;         // ticks since the preview was created (diagnostics)
		bool                g_previewMoved = false;
		RE::FormID          g_stalePreview = 0;         // from a save made while aiming; deleted after load

		// Frames in the game that Folder Gallery has no record of: a frame record as base, not disabled /
		// deleted, not the current preview, not in g_frames. Only frames in memory are found (all hung
		// frames are persistent; a temporary leftover preview is only in memory while its cell is loaded).
		// Main thread.
		std::vector<RE::TESObjectREFR*> FindStrayFrames()
		{
			std::unordered_set<RE::FormID> known;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					known.insert(id);
				}
			}
			const auto previewID = [] {
				auto p = g_preview.get();
				return p ? p->GetFormID() : 0u;
			}();
			std::vector<RE::TESObjectREFR*> out;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			RE::BSReadLockGuard guard{ lock };
			if (!map) {
				return out;
			}
			for (auto& [id, form] : *map) {
				if ((id >> 24) != 0xFF || !form || form->GetFormType() != RE::FormType::Reference) {
					continue;  // frames are always created at runtime (FF......)
				}
				auto* ref = static_cast<RE::TESObjectREFR*>(form);
				if (id == previewID || known.contains(id) || ref->IsDisabled() || ref->IsMarkedForDeletion() || FrameIndex(ref->GetBaseObject()) < 0) {
					continue;
				}
				out.push_back(ref);
			}
			return out;
		}

		void DeletePreview()
		{
			if (auto ref = g_preview.get()) {
				DeleteFrameRef(ref.get(), "preview removed");
			}
			g_preview = {};
			g_previewReady = false;
			g_previewTexture.clear();
			g_previewVisible = false;
		}

		void CreatePreview()
		{
			DeletePreview();
			std::optional<Placement> pl;
			{
				std::lock_guard lk(g_mtx);
				pl = g_placement;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = pl ? FrameForm(pl->shape, pl->style) : nullptr;
			if (!pl || !player || !base) {
				return;
			}
			// Spawn where the player is aiming (or in front of them); it follows the aim every frame.
			const char* why = nullptr;
			auto        pose = ComputePose(why, pl->moving.get().get(), pl->offset);
			RE::NiPoint3 at = player->GetPosition();
			RE::NiPoint3 angle{};
			if (pose) {
				at = pose->pos;
				angle.z = AngleZ(*pose);
			} else {
				const float h = player->GetAngleZ();
				at = { at.x + std::sin(h) * 150.0f, at.y + std::cos(h) * 150.0f, at.z + 100.0f };
				angle.z = h + 3.14159265f;
			}
			auto handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
				base, at, angle, player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
			g_previewTicks = 0;
			g_previewMoved = false;
			if (auto old = pl->moving.get(); old && old->Get3D()) {
				old->Get3D()->SetAppCulled(true);  // the original disappears while it is being moved
			}
			if (auto ref = handle.get()) {
				ref->SetCollision(false);
				g_preview = handle;
				spdlog::info("Preview frame {:08X} ({} {})", ref->GetFormID(), Frames::kShapes[pl->shape].name, Frames::StyleName(pl->shape, pl->style));
			}
		}

		void UpdatePreview()
		{
			g_previewQueued = false;
			std::optional<Placement> pl;
			{
				std::lock_guard lk(g_mtx);
				pl = g_placement;
			}
			if (!pl) {
				DeletePreview();
				return;
			}
			// Left through a door (interior <-> exterior, or another worldspace): stop hanging here, so the
			// preview isn't left behind in a cell that is no longer loaded.
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				auto* cell = player->GetParentCell();
				auto* world = player->GetWorldspace();
				if (g_placeCellReset.exchange(false) || !g_placeCell) {
					g_placeCell = cell;
					g_placeWorld = world;
				} else if (cell && cell != g_placeCell &&
						   (cell->IsInteriorCell() || g_placeCell->IsInteriorCell() || world != g_placeWorld)) {
					spdlog::info("Placement cancelled: the player changed cells");
					DeletePreview();
					CancelPlacement(false);
					Notify(Note::Confirm, "Hanging cancelled - you left the area");
					return;
				}
			}
			auto ref = g_preview.get();
			if (!ref || ref->GetBaseObject() != FrameForm(pl->shape, pl->style)) {
				CreatePreview();
				return;
			}
			++g_previewTicks;
			auto* root = ref->Get3D();
			if (!root) {
				if (g_previewTicks == 120) {
					spdlog::warn("Preview {:08X}: 3D still not loaded after 120 frames", ref->GetFormID());
				}
				return;  // still loading
			}
			if (!g_previewReady) {
				spdlog::info("Preview {:08X}: 3D loaded after {} frame(s)", ref->GetFormID(), g_previewTicks);
				root->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);  // the aiming ray must not hit the preview
				g_previewReady = true;
			}
			const auto dds = DdsGamePath(pl->picture, pl->shape);
			if (g_previewTexture != dds) {
				bool ready = false;
				{
					std::lock_guard lk(g_mtx);
					const auto      it = g_conversions.find(dds);
					ready = it != g_conversions.end() && it->second == Conv::Ready;
				}
				if (ready && ApplyTexture(ref.get(), dds)) {
					g_previewTexture = dds;
				}
			}

			const char* why = nullptr;
			const auto  pose = ComputePose(why, pl->moving.get().get(), pl->offset);
			if (!pose) {
				if (g_previewVisible) {
					root->SetAppCulled(true);
					g_previewVisible = false;
				}
				return;
			}
			// Same as Papyrus SetPosition/SetAngle: update the reference, let the game move its 3D.
			ref->data.location = pose->pos;
			ref->data.angle = { 0.0f, 0.0f, AngleZ(*pose) };
			ref->Update3DPosition(true);
			if (std::fabs(root->local.scale - pl->scale) > 0.001f) {
				SetRefScale(ref.get(), pl->scale);
			}
			if (!g_previewVisible) {
				root->SetAppCulled(false);
				g_previewVisible = true;
			}
			if (!g_previewMoved) {
				g_previewMoved = true;
				const auto& w = root->world.translate;
				spdlog::info("Preview {:08X}: first move to ({:.0f}, {:.0f}, {:.0f}); 3D now at ({:.0f}, {:.0f}, {:.0f})",
					ref->GetFormID(), pose->pos.x, pose->pos.y, pose->pos.z, w.x, w.y, w.z);
			}
		}

		void DoPlace()
		{
			g_placeQueued = false;
			std::optional<Placement> pl;
			{
				std::lock_guard lk(g_mtx);
				pl = g_placement;
			}
			if (!pl) {
				return;
			}
			const char* why = nullptr;
			const auto  pose = ComputePose(why, pl->moving.get().get(), pl->offset);
			if (!pose) {
				Notify(Note::Warning, why ? why : "Can't hang the picture here");
				return;
			}
			auto* base = FrameForm(pl->shape, pl->style);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!base || !player) {
				spdlog::error("Frame form for {} {} not available (form id {:08X})", Frames::kShapes[pl->shape].name,
					Frames::StyleName(pl->shape, pl->style), g_frameForms[Frames::Index(pl->shape, pl->style)]);
				Notify(Note::Error, "Folder Gallery: frame record missing - is FolderGallery.esp enabled?");
				return;
			}
			const RE::NiPoint3 angle{ 0.0f, 0.0f, AngleZ(*pose) };
			auto handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
				base, pose->pos, angle, player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), true, true);
			auto ref = handle.get();
			if (ref && std::fabs(pl->scale - 1.0f) > 0.001f) {
				SetRefScale(ref.get(), pl->scale);
			}
			if (!ref) {
				Notify(Note::Error, "Folder Gallery: could not place the frame here");
				spdlog::error("CreateReferenceAtLocation failed");
				return;
			}

			const auto dds = DdsGamePath(pl->picture, pl->shape);
			{
				std::lock_guard lk(g_mtx);
				g_frames[ref->GetFormID()] = Entry{ dds, ToUtf8(pl->picture), static_cast<std::uint8_t>(pl->shape), static_cast<std::uint8_t>(pl->style) };
			}
			spdlog::info("Placed frame {:08X} ({} {}, {:.0f}%) at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg, picture {}",
				ref->GetFormID(), Frames::kShapes[pl->shape].name, Frames::StyleName(pl->shape, pl->style), pl->scale * 100.0f, pose->pos.x, pose->pos.y, pose->pos.z,
				angle.z * 57.2958f, ToUtf8(pl->picture));
			EnsureTexture(dds, pl->picture, pl->shape);
			QueueApply(ref->GetFormID());  // applied as soon as the new frame's 3D exists

			if (auto old = pl->moving.get()) {
				{
					std::lock_guard lk(g_mtx);
					g_frames.erase(old->GetFormID());
				}
				DeleteFrameRef(old.get(), "moved away");
			}
			DeletePreview();
			CancelPlacement(false);
			CleanupUnusedTextures();  // e.g. the old texture of a moved frame, or other shapes tried while aiming
			Notify(Note::Confirm, "Picture hung");
		}

		// Hands a frame to the UI (frame menu). Any thread.
		bool OpenFrameMenu(RE::TESObjectREFR* a_ref)
		{
			const int idx = a_ref ? FrameIndex(a_ref->GetBaseObject()) : -1;
			if (idx < 0) {
				return false;
			}
			FrameInfo info;
			info.ref = a_ref->GetHandle();
			info.scale = a_ref->GetReferenceRuntimeData().refScale / 100.0f;
			info.shape = Frames::ShapeOf(idx);
			info.style = Frames::StyleOf(idx);
			std::lock_guard lk(g_mtx);
			if (const auto it = g_frames.find(a_ref->GetFormID()); it != g_frames.end()) {
				info.picturePath = FromUtf8(it->second.picture);
				info.picture = ToUtf8(info.picturePath.filename());
			}
			g_activated = info;
			return true;
		}

		// The hung frame closest to the line of sight, ignoring collision (so frames sunk into a wall
		// or beyond the game's short activation range can still be examined). Main thread.
		bool DoExamine(bool a_respectCrosshair)
		{
			// If the game's crosshair is on something else (an NPC, a door...), that activation wins.
			if (auto* pick = RE::CrosshairPickData::GetSingleton(); pick && a_respectCrosshair) {
				if (auto target = pick->target.get(); target && FrameIndex(target->GetBaseObject()) < 0) {
					spdlog::info("Examine: crosshair is on {:08X} ({}), leaving it to the game", target->GetFormID(),
						target->GetBaseObject() ? target->GetBaseObject()->GetName() : "?");
					return false;
				}
			}
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->cameraRoot) {
				spdlog::warn("Examine: no camera");
				return false;
			}
			const auto&  xf = camera->cameraRoot->world;
			const auto   from = xf.translate;
			RE::NiPoint3 dir{ xf.rotate.entry[0][1], xf.rotate.entry[1][1], xf.rotate.entry[2][1] };
			const float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len < 1e-4f) {
				return false;
			}
			dir = { dir.x / len, dir.y / len, dir.z / len };

			std::vector<std::pair<RE::FormID, int>> frames;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					frames.emplace_back(id, e.shape);
				}
			}
			// frames without a record (lost record, leftover preview) can be examined and taken down too
			if (auto* tes = RE::TES::GetSingleton(); tes && RE::PlayerCharacter::GetSingleton()) {
				std::unordered_set<RE::FormID> known;
				for (const auto& f : frames) {
					known.insert(f.first);
				}
				const auto previewID = [] {
					auto p = g_preview.get();
					return p ? p->GetFormID() : 0u;
				}();
				tes->ForEachReferenceInRange(RE::PlayerCharacter::GetSingleton(), g_settings.examineReach + 300.0f, [&](RE::TESObjectREFR& a_ref) {
					const int idx = FrameIndex(a_ref.GetBaseObject());
					if (idx >= 0 && a_ref.GetFormID() != previewID && !known.contains(a_ref.GetFormID())) {
						frames.emplace_back(a_ref.GetFormID(), Frames::ShapeOf(idx));
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			RE::TESObjectREFR* best = nullptr;
			float              bestT = 1e30f;
			float              nearestMiss = 1e30f;
			RE::FormID         nearestID = 0;
			int                checked = 0;
			for (const auto& [id, shape] : frames) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref || ref->IsDisabled() || !ref->Get3D()) {
					continue;
				}
				++checked;
				// Centre of the canvas: a little in front of the frame's back.
				const float z = ref->GetAngleZ();
				auto        c = ref->GetPosition();
				c.x += std::sin(z) * 1.5f;
				c.y += std::cos(z) * 1.5f;
				const RE::NiPoint3 v{ c.x - from.x, c.y - from.y, c.z - from.z };
				const float        t = v.x * dir.x + v.y * dir.y + v.z * dir.z;  // distance along the aim
				if (t < 0.0f || t > g_settings.examineReach) {
					continue;
				}
				const RE::NiPoint3 off{ v.x - dir.x * t, v.y - dir.y * t, v.z - dir.z * t };
				const float        miss = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
				const auto&        sh = Frames::kShapes[std::clamp(shape, 0, Frames::kShapeCount - 1)];
				const float        radius = 0.5f * (std::min)(sh.canvasW, sh.canvasH) * (ref->GetReferenceRuntimeData().refScale / 100.0f) + 4.0f;
				if (miss < nearestMiss) {
					nearestMiss = miss;
					nearestID = id;
				}
				if (miss <= radius && t < bestT) {
					best = ref;
					bestT = t;
				}
			}
			if (best) {
				spdlog::info("Examine: frame {:08X} at {:.0f} units", best->GetFormID(), bestT);
				return OpenFrameMenu(best);
			}
			spdlog::info("Examine: no frame in the line of sight ({} loaded frame(s) checked; nearest {:08X} missed by {:.0f} units)",
				checked, nearestID, nearestMiss < 1e29f ? nearestMiss : -1.0f);
			return false;
		}

		// Diffuse texture currently on a frame's canvas ("" if the 3D isn't there).
		std::string CurrentPicture(RE::TESObjectREFR* a_ref)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			auto* obj = root ? root->GetObjectByName(RE::BSFixedString("SGPicture")) : nullptr;
			auto* geom = obj ? obj->AsGeometry() : nullptr;
			auto* shader = geom ? netimmerse_cast<RE::BSLightingShaderProperty*>(
				geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get()) : nullptr;
			auto* material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
			if (!material || !material->textureSet) {
				return {};
			}
			const char* p = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
			return p ? p : "";
		}

		// The game rebuilds a frame's 3D from the mesh when it is moved or reloaded, which drops the
		// picture without telling anyone. Every so often, put it back where it went missing. Main thread.
		// Checks a slice of the hung frames each call (round robin), so the cost stays flat however many
		// pictures are hung; frames that just loaded are handled right away by the attach event anyway.
		void Watchdog()
		{
			constexpr std::size_t                           kPerCall = 48;
			static std::size_t                              cursor = 0;
			std::vector<std::pair<RE::FormID, std::string>> frames;
			{
				std::lock_guard lk(g_mtx);
				const std::size_t n = g_frames.size();
				if (n == 0) {
					return;
				}
				cursor %= n;
				auto it = g_frames.begin();
				std::advance(it, cursor);
				for (std::size_t k = 0; k < (std::min)(kPerCall, n); ++k) {
					const auto c = g_conversions.find(it->second.dds);
					if (c != g_conversions.end() && c->second == Conv::Ready) {
						frames.emplace_back(it->first, it->second.dds);
					}
					if (++it == g_frames.end()) {
						it = g_frames.begin();
					}
				}
				cursor = (cursor + (std::min)(kPerCall, n)) % n;
			}
			for (const auto& [id, dds] : frames) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref || ref->IsDisabled() || !ref->Get3D()) {
					continue;
				}
				const auto now = CurrentPicture(ref);
				if (_stricmp(now.c_str(), dds.c_str()) != 0 && ApplyTexture(ref, dds)) {
					spdlog::info("{:08X}: picture restored after the frame's 3D was rebuilt", id);
				}
			}
		}

		// ------------------------------------------------------------------ events
		class EventSink final :
			public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
			public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event && a_event->loaded) {
					bool ours = false;
					{
						std::lock_guard lk(g_mtx);
						ours = g_frames.contains(a_event->formID);
					}
					if (ours) {
						const auto id = a_event->formID;
						SKSE::GetTaskInterface()->AddTask([id] { QueueApply(id, 120); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
			{
				if (a_event && a_event->attached && a_event->reference) {
					const auto id = a_event->reference->GetFormID();
					bool       ours = false, remove = false;
					{
						std::lock_guard lk(g_mtx);
						ours = g_frames.contains(id);
						remove = g_removals.contains(id);
					}
					if (ours) {
						SKSE::GetTaskInterface()->AddTask([id] { QueueApply(id, 300); });
					}
					if (remove) {
						SKSE::GetTaskInterface()->AddTask([] { ProcessRemovals(); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (!a_event || !a_event->objectActivated || !a_event->actionRef || a_event->actionRef.get() != RE::PlayerCharacter::GetSingleton()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				OpenFrameMenu(a_event->objectActivated.get());
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		EventSink g_sink;

		// ------------------------------------------------------------------ co-save
		void WriteString(SKSE::SerializationInterface* a_intfc, const std::string& a_s)
		{
			const auto len = static_cast<std::uint16_t>((std::min)(a_s.size(), std::size_t{ 0xFFFF }));
			a_intfc->WriteRecordData(len);
			a_intfc->WriteRecordData(a_s.data(), len);
		}

		bool ReadString(SKSE::SerializationInterface* a_intfc, std::string& a_s)
		{
			std::uint16_t len = 0;
			if (a_intfc->ReadRecordData(len) != sizeof(len)) {
				return false;
			}
			a_s.resize(len);
			return len == 0 || a_intfc->ReadRecordData(a_s.data(), len) == len;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			std::lock_guard lk(g_mtx);
			if (!a_intfc->OpenRecord(kRecordFrames, kRecordVersion)) {
				spdlog::error("Co-save: could not open record");
				return;
			}
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_frames.size()));
			for (const auto& [id, e] : g_frames) {
				a_intfc->WriteRecordData(id);
				a_intfc->WriteRecordData(e.shape);
				a_intfc->WriteRecordData(e.style);
				WriteString(a_intfc, e.dds);
				WriteString(a_intfc, e.picture);
			}
			spdlog::info("Co-save: saved {} frame(s)", g_frames.size());
			if (auto preview = g_preview.get(); preview && a_intfc->OpenRecord(kRecordPreview, 1)) {
				a_intfc->WriteRecordData(preview->GetFormID());
			}
			if (!g_removals.empty() && a_intfc->OpenRecord(kRecordRemovals, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_removals.size()));
				for (const auto id : g_removals) {
					a_intfc->WriteRecordData(id);
				}
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			std::lock_guard lk(g_mtx);
			g_frames.clear();
			g_removals.clear();
			g_activated.reset();
		}

		// ------------------------------------------------------------------ picture renames
		// One "old<TAB>new" line (UTF-8 full paths) per rename, in the SKSE log folder (outside any virtual Data).
		fs::path RenameLogPath()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? (*dir / L"FolderGallery_renames.txt") : fs::path(L"FolderGallery_renames.txt");
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return a_s;
		}

		void LogRename(const fs::path& a_old, const fs::path& a_new)
		{
			std::ofstream out(RenameLogPath(), std::ios::app | std::ios::binary);
			out << ToUtf8(a_old) << '\t' << ToUtf8(a_new) << '\n';
		}

		// Where a picture that no longer exists went, following renames made since (empty if unknown).
		std::string FollowRenames(const std::string& a_picture)
		{
			std::unordered_map<std::string, std::string> map;
			std::ifstream                                 in(RenameLogPath(), std::ios::binary);
			for (std::string line; std::getline(in, line);) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				const auto tab = line.find('\t');
				if (tab != std::string::npos) {
					map[Lower(line.substr(0, tab))] = line.substr(tab + 1);
				}
			}
			std::string cur = a_picture;
			for (int hops = 0; hops < 64; ++hops) {  // a chain of renames; the limit guards against loops
				const auto it = map.find(Lower(cur));
				if (it == map.end()) {
					break;
				}
				cur = it->second;
				std::error_code ec;
				if (fs::exists(FromUtf8(cur), ec)) {
					return cur;
				}
			}
			return {};
		}

		// Moves a frame's converted texture to the name that goes with its (renamed) picture. Returns the new
		// game path. The old file is renamed when possible, so nothing has to be converted again.
		std::string FollowTexture(const std::string& a_oldDds, const fs::path& a_picture, int a_shape)
		{
			const auto newDds = DdsGamePath(a_picture, a_shape);
			if (_stricmp(newDds.c_str(), a_oldDds.c_str()) == 0) {
				return newDds;
			}
			std::error_code ec;
			const auto      from = DataPath(a_oldDds), to = DataPath(newDds);
			if (fs::exists(from, ec) && !fs::exists(to, ec)) {
				fs::rename(from, to, ec);
				if (ec) {  // e.g. still open somewhere: copy instead, the old one is cleaned up later
					ec.clear();
					fs::copy_file(from, to, ec);
				}
				spdlog::info("Texture {} -> {}{}", a_oldDds, newDds, ec ? fmt::format(" FAILED ({})", ec.message()) : "");
			}
			return newDds;
		}

		// A frame's texture that lives outside the current Placed Frames folder: move it in (same file name).
		// Returns the new game path (the old one if nothing had to change).
		std::string RelocateTexture(const std::string& a_dds)
		{
			const auto dir = PlacedDir();
			if (_strnicmp(a_dds.c_str(), dir.c_str(), dir.size()) == 0) {
				return a_dds;
			}
			const auto      slash = a_dds.find_last_of('\\');
			const auto      newDds = dir + (slash == std::string::npos ? a_dds : a_dds.substr(slash + 1));
			std::error_code ec;
			const auto      from = DataPath(a_dds), to = DataPath(newDds);
			fs::create_directories(to.parent_path(), ec);
			if (fs::exists(from, ec) && !fs::exists(to, ec)) {
				fs::rename(from, to, ec);
				if (ec) {
					ec.clear();
					fs::copy_file(from, to, ec);
				}
			}
			return newDds;
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0, version = 0, length = 0;
			std::vector<std::pair<RE::FormID, Entry>> loaded;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kRecordPreview) {
					RE::FormID oldID = 0, newID = 0;
					if (a_intfc->ReadRecordData(oldID) == sizeof(oldID) && a_intfc->ResolveFormID(oldID, newID)) {
						g_stalePreview = newID;
					}
					continue;
				}
				if (type == kRecordRemovals) {
					std::uint32_t n = 0;
					a_intfc->ReadRecordData(n);
					std::lock_guard lk(g_mtx);
					for (std::uint32_t i = 0; i < n; ++i) {
						RE::FormID oldID = 0, newID = 0;
						if (a_intfc->ReadRecordData(oldID) == sizeof(oldID) && a_intfc->ResolveFormID(oldID, newID)) {
							g_removals.insert(newID);
						}
					}
					continue;
				}
				if (type != kRecordFrames || version != kRecordVersion) {
					continue;
				}
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(count);
				for (std::uint32_t i = 0; i < count; ++i) {
					RE::FormID oldID = 0;
					Entry      e;
					if (a_intfc->ReadRecordData(oldID) != sizeof(oldID) || a_intfc->ReadRecordData(e.shape) != 1 ||
						a_intfc->ReadRecordData(e.style) != 1 || !ReadString(a_intfc, e.dds) || !ReadString(a_intfc, e.picture)) {
						spdlog::error("Co-save: record truncated after {} frame(s)", i);
						break;
					}
					RE::FormID newID = 0;
					if (!a_intfc->ResolveFormID(oldID, newID)) {
						spdlog::warn("Co-save: frame {:08X} no longer exists, dropped", oldID);
						continue;
					}
					std::error_code ec;
					if (!e.picture.empty() && !fs::exists(FromUtf8(e.picture), ec)) {
						if (auto moved = FollowRenames(e.picture); !moved.empty()) {
							spdlog::info("Co-save: {} was renamed to {}", e.picture, moved);
							e.picture = moved;
							e.dds = FollowTexture(e.dds, FromUtf8(moved), e.shape);
						}
					}
					e.dds = RelocateTexture(e.dds);  // made while another Placed Frames folder was set
					loaded.emplace_back(newID, std::move(e));
				}
			}
			{
				std::lock_guard lk(g_mtx);
				g_frames.clear();
				for (auto& [id, e] : loaded) {
					g_frames[id] = e;
				}
			}
			for (const auto& [id, e] : loaded) {
				EnsureTexture(e.dds, FromUtf8(e.picture), e.shape);  // re-creates the DDS if it was deleted
			}
			spdlog::info("Co-save: loaded {} frame(s)", loaded.size());
		}
	}

	Settings& GetSettings()
	{
		return g_settings;
	}

	int AutoShape(int a_w, int a_h)
	{
		if (a_w <= 0 || a_h <= 0) {
			return 0;
		}
		const double a = static_cast<double>(a_w) / a_h;
		int          best = 0;
		double       bestErr = 1e9;
		for (int i = 0; i < Frames::kShapeCount; ++i) {
			const double err = std::fabs(std::log(a / (Frames::kShapes[i].canvasW / Frames::kShapes[i].canvasH)));
			if (err < bestErr) {
				bestErr = err;
				best = i;
			}
		}
		return best;
	}

	std::pair<int, int> TextureSize(int a_shape)
	{
		const auto& s = Frames::kShapes[std::clamp(a_shape, 0, Frames::kShapeCount - 1)];
		const float longSide = static_cast<float>(g_settings.resolution);
		const float scale = longSide / (std::max)(s.canvasW, s.canvasH);
		auto        snap = [](float v) { return (std::max)(4, static_cast<int>(std::lround(v / 4.0f)) * 4); };
		return { snap(s.canvasW * scale), snap(s.canvasH * scale) };
	}

	void InstallSerialization()
	{
		const auto* ser = SKSE::GetSerializationInterface();
		ser->SetUniqueID(kSerializationID);
		ser->SetSaveCallback(OnSave);
		ser->SetLoadCallback(OnLoad);
		ser->SetRevertCallback(OnRevert);
	}

	bool Init()
	{
		{
			std::lock_guard lk(g_dirMtx);
			g_placedDir = GamePathFor(g_settings.placedFolder);
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		int   found = 0;
		for (int idx = 0; idx < Frames::kFrameCount; ++idx) {
			auto* form = dh ? dh->LookupForm(Frames::LocalFormID(idx), kPluginFile) : nullptr;
			g_frameForms[idx] = form ? form->GetFormID() : 0;
			found += form ? 1 : 0;
		}
		spdlog::info("Wall frames: found {}/{} frame forms in {}", found, g_frameForms.size(), kPluginFile);

		std::error_code ec;
		if (!fs::exists(DataPath(kFlatNormal), ec)) {
			Dds::WriteFlatNormal(DataPath(kFlatNormal));
		}

		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESObjectLoadedEvent>(&g_sink);
			events->AddEventSink<RE::TESCellAttachDetachEvent>(&g_sink);
			events->AddEventSink<RE::TESActivateEvent>(&g_sink);
		}
		return found == static_cast<int>(g_frameForms.size());
	}

	void OnGameLoaded()
	{
		g_preview = {};
		ProcessRemovals();
		if (g_stalePreview) {
			if (auto* stale = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_stalePreview)) {
				DeleteFrameRef(stale, "preview left in the save");  // finished when its 3D loads if it isn't yet
			}
			g_stalePreview = 0;
		}
		std::vector<RE::FormID> ids;
		{
			std::lock_guard lk(g_mtx);
			for (const auto& [id, e] : g_frames) {
				ids.push_back(id);
			}
		}
		for (const auto id : ids) {
			QueueApply(id, 300);
		}
		CleanupUnusedTextures();
	}

	void BeginPlacement(const fs::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving, float a_scale)
	{
		const int shape = std::clamp(a_shape, 0, Frames::kShapeCount - 1);
		const int style = Frames::ClampStyle(shape, a_style);
		g_placeCellReset = true;
		{
			std::lock_guard lk(g_mtx);
			g_placement = Placement{ a_picture, shape, style, a_moving, std::clamp(a_scale, kMinScale, kMaxScale) };
		}
		// Start converting right away so the texture is usually ready by the time the frame is hung.
		EnsureTexture(DdsGamePath(a_picture, shape), a_picture, shape);
		g_placing = true;
		Notify(Note::Help, "Aim at a wall, left-click to hang. Wheel: size, Shift+wheel: frame, Up/Down: distance from wall. Right-click/Esc cancels.");
	}

	bool IsPlacing()
	{
		return g_placing;
	}

	bool GetPlacementStatus(PlacementStatus& a_out)
	{
		std::lock_guard lk(g_mtx);
		if (!g_placement) {
			return false;
		}
		a_out.frame = Frames::StyleName(g_placement->shape, g_placement->style);
		a_out.shape = Frames::kShapes[Frames::ClampShape(g_placement->shape)].name;
		a_out.scale = g_placement->scale;
		a_out.distance = g_placement->offset - kDefaultOffset;
		return true;
	}

	void Tick()
	{
		if (!g_previewQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask([] {
				if (g_placing || g_preview) {
					UpdatePreview();
				} else {
					g_previewQueued = false;
				}
				ProcessPending();
				static int ticks = 0;
				if (++ticks % 30 == 0) {
					Watchdog();
					ProcessRemovals();
				}
			});
		}
	}

	void AdjustScale(int a_steps)
	{
		float scale = 1.0f;
		{
			std::lock_guard lk(g_mtx);
			if (!g_placement) {
				return;
			}
			auto& sc = g_placement->scale;
			sc = std::clamp(std::round((sc + 0.1f * a_steps) * 10.0f) / 10.0f, kMinScale, kMaxScale);
			scale = sc;
		}
		Notify(Note::Size, fmt::format("Size: {:.0f}%", scale * 100.0f));
	}

	void AdjustOffset(int a_steps)
	{
		float offset = 0.0f;
		{
			std::lock_guard lk(g_mtx);
			if (!g_placement) {
				return;
			}
			auto& o = g_placement->offset;
			o = std::clamp(o + 0.25f * a_steps, kMinOffset, kMaxOffset);
			offset = o;
		}
		Notify(Note::Distance, fmt::format("Distance from wall: {:+.2f}", offset - kDefaultOffset));
	}

	void NudgeFrame(RE::ObjectRefHandle a_ref, float a_distance)
	{
		SKSE::GetTaskInterface()->AddTask([a_ref, a_distance] {
			auto ref = a_ref.get();
			if (!ref) {
				return;
			}
			// The frame's front is its local +Y, which the Z angle turns to (sin z, cos z).
			const float  z = ref->GetAngleZ();
			RE::NiPoint3 pos = ref->GetPosition();
			pos.x += std::sin(z) * a_distance;
			pos.y += std::cos(z) * a_distance;
			ref->SetPosition(pos);
			QueueApply(ref->GetFormID());  // moving rebuilds the 3D; the watchdog also catches it
			spdlog::info("Frame {:08X} nudged {:+.2f} from the wall", ref->GetFormID(), a_distance);
		});
	}

	namespace
	{
		std::mutex                             g_placedMtx;
		std::optional<std::vector<PlacedInfo>> g_placedList;

		std::string NameOf(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			const char* n = a_form->GetName();
			return n ? std::string(n) : std::string();
		}
	}

	void RequestPlacedList()
	{
		SKSE::GetTaskInterface()->AddTask([] {
			std::vector<std::pair<RE::FormID, Entry>> frames;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					frames.emplace_back(id, e);
				}
			}
			std::vector<PlacedInfo> out;
			for (const auto& [id, e] : frames) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref || ref->IsDisabled() || ref->IsMarkedForDeletion()) {
					continue;
				}
				PlacedInfo info;
				info.ref = ref->GetHandle();
				const int idx = FrameIndex(ref->GetBaseObject());
				info.shape = idx >= 0 ? Frames::ShapeOf(idx) : e.shape;
				info.style = idx >= 0 ? Frames::StyleOf(idx) : e.style;
				info.scale = ref->GetReferenceRuntimeData().refScale / 100.0f;
				info.picture = FromUtf8(e.picture);
				info.name = ToUtf8(info.picture.filename());
				auto* cell = ref->GetParentCell();
				auto* loc = ref->GetCurrentLocation();
				if (!loc && cell) {
					loc = cell->GetLocation();
				}
				info.interior = cell ? cell->IsInteriorCell() : false;
				const std::string cellName = NameOf(cell), locName = NameOf(loc);
				const std::string parentName = loc ? NameOf(loc->parentLoc) : std::string();
				const std::string worldName = NameOf(ref->GetWorldspace());
				if (info.interior) {
					info.place = !cellName.empty() ? cellName : (!locName.empty() ? locName : "Unnamed interior");
					info.region = !locName.empty() && locName != info.place ? locName : parentName;
				} else {
					info.place = !locName.empty() ? locName : (!cellName.empty() ? cellName : (!worldName.empty() ? worldName : "Wilderness"));
					info.region = !parentName.empty() ? parentName : (worldName != info.place ? worldName : std::string());
				}
				out.push_back(std::move(info));
			}
			for (auto* ref : FindStrayFrames()) {
				PlacedInfo info;
				info.ref = ref->GetHandle();
				const int idx = FrameIndex(ref->GetBaseObject());
				info.shape = Frames::ShapeOf(idx);
				info.style = Frames::StyleOf(idx);
				info.scale = ref->GetReferenceRuntimeData().refScale / 100.0f;
				info.stray = true;
				auto* cell = ref->GetParentCell();
				auto* loc = cell ? cell->GetLocation() : nullptr;
				info.interior = cell ? cell->IsInteriorCell() : false;
				info.place = !NameOf(cell).empty() ? NameOf(cell) : (!NameOf(loc).empty() ? NameOf(loc) : (!NameOf(ref->GetWorldspace()).empty() ? NameOf(ref->GetWorldspace()) : "Unknown place"));
				info.region = loc && NameOf(loc) != info.place ? NameOf(loc) : (loc ? NameOf(loc->parentLoc) : std::string());
				spdlog::info("Placed list: stray frame {:08X} ({} {}) in {}", ref->GetFormID(), Frames::kShapes[info.shape].name,
					Frames::StyleName(info.shape, info.style), info.place);
				out.push_back(std::move(info));
			}
			std::sort(out.begin(), out.end(), [](const PlacedInfo& a, const PlacedInfo& b) {
				return std::tie(a.stray, a.region, a.place, a.name) < std::tie(b.stray, b.region, b.place, b.name);
			});
			std::lock_guard lk(g_placedMtx);
			g_placedList = std::move(out);
		});
	}

	void SetPlacedFolder(const std::string& a_relative)
	{
		const auto dir = GamePathFor(a_relative);
		{
			std::lock_guard lk(g_dirMtx);
			if (_stricmp(dir.c_str(), g_placedDir.c_str()) == 0) {
				return;
			}
			g_placedDir = dir;
		}
		g_settings.placedFolder = a_relative;
		spdlog::info("Placed Frames folder: {}", dir);
		SKSE::GetTaskInterface()->AddTask([] {
			std::vector<RE::FormID> moved;
			std::vector<std::pair<RE::FormID, std::string>> entries;
			{
				std::lock_guard lk(g_mtx);
				for (const auto& [id, e] : g_frames) {
					entries.emplace_back(id, e.dds);
				}
			}
			std::map<std::string, std::string> done;  // frames can share a texture
			for (const auto& [id, dds] : entries) {
				auto it = done.find(dds);
				if (it == done.end()) {
					it = done.emplace(dds, RelocateTexture(dds)).first;
				}
				if (it->second != dds) {
					std::lock_guard lk(g_mtx);
					if (auto f = g_frames.find(id); f != g_frames.end()) {
						f->second.dds = it->second;
					}
					g_conversions[it->second] = Conv::Ready;
					moved.push_back(id);
				}
			}
			for (const auto id : moved) {
				QueueApply(id);  // frames switch to the moved texture
			}
			spdlog::info("Placed Frames folder changed: {} frame(s) moved", moved.size());
		});
	}

	void RemoveStrayFrames()
	{
		SKSE::GetTaskInterface()->AddTask([] {
			const auto strays = FindStrayFrames();
			for (auto* ref : strays) {
				DeleteFrameRef(ref, "stray frame removed");
			}
			spdlog::info("Removed {} stray frame(s)", strays.size());
			Notify(Note::Confirm, fmt::format("Removed {} stray frame{}", strays.size(), strays.size() == 1 ? "" : "s"));
		});
	}

	std::optional<std::vector<PlacedInfo>> TakePlacedList()
	{
		std::lock_guard lk(g_placedMtx);
		auto            out = std::move(g_placedList);
		g_placedList.reset();
		return out;
	}

	int FrameCount()
	{
		std::lock_guard lk(g_mtx);
		return static_cast<int>(g_frames.size());
	}

	void SetFrameScale(RE::ObjectRefHandle a_ref, float a_scale)
	{
		const float scale = std::clamp(a_scale, kMinScale, kMaxScale);
		SKSE::GetTaskInterface()->AddTask([a_ref, scale] {
			if (auto ref = a_ref.get()) {
				SetRefScale(ref.get(), scale);
			}
		});
	}

	void CycleStyle(int a_delta)
	{
		std::string name;
		{
			std::lock_guard lk(g_mtx);
			if (!g_placement) {
				return;
			}
			auto& st = g_placement->style;
			const int n = Frames::StyleCount(g_placement->shape);
			st = ((st + a_delta) % n + n) % n;
			name = Frames::StyleName(g_placement->shape, st);
		}
		Notify(Note::FrameName, fmt::format("Frame: {}", name));
	}

	void RequestExamine(std::function<void()> a_otherwise)
	{
		SKSE::GetTaskInterface()->AddTask([a_otherwise] {
			if (!DoExamine(!a_otherwise) && a_otherwise) {
				a_otherwise();
			}
		});
	}

	void RequestPlace()
	{
		if (g_placing && !g_placeQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask([] { DoPlace(); });
		}
	}

	void CancelPlacement(bool a_notify)
	{
		RE::ObjectRefHandle moving;
		{
			std::lock_guard lk(g_mtx);
			if (g_placement) {
				moving = g_placement->moving;
			}
			g_placement.reset();
		}
		const bool was = g_placing.exchange(false);
		if (was) {
			SKSE::GetTaskInterface()->AddTask([moving, a_notify] {
				DeletePreview();
				if (a_notify) {
					CleanupUnusedTextures();  // the texture converted in advance for a cancelled hang
				}
				if (auto old = moving.get(); old && !old->IsDisabled() && old->Get3D()) {
					old->Get3D()->SetAppCulled(false);  // move cancelled (or the old frame is gone anyway)
				}
			});
		}
		if (was && a_notify) {
			Notify(Note::Confirm, "Hanging cancelled");
		}
	}

	std::optional<FrameInfo> TakeActivatedFrame()
	{
		std::lock_guard lk(g_mtx);
		auto            out = g_activated;
		g_activated.reset();
		return out;
	}

	void RemoveFrame(RE::ObjectRefHandle a_ref)
	{
		SKSE::GetTaskInterface()->AddTask([a_ref] {
			auto ref = a_ref.get();
			if (!ref) {
				return;
			}
			{
				std::lock_guard lk(g_mtx);
				g_frames.erase(ref->GetFormID());
			}
			spdlog::info("Removed frame {:08X}", ref->GetFormID());
			DeleteFrameRef(ref.get());
			CleanupUnusedTextures();
			Notify(Note::Confirm, "Picture taken down");
		});
	}

	void PictureRenamed(const fs::path& a_old, const fs::path& a_new)
	{
		LogRename(a_old, a_new);
		SKSE::GetTaskInterface()->AddTask([a_old, a_new] {
			const auto oldKey = Lower(ToUtf8(a_old));
			std::vector<RE::FormID>            changed;
			std::map<std::string, std::string> moved;  // old texture -> new (frames can share one)
			{
				std::lock_guard lk(g_mtx);
				for (auto& [id, e] : g_frames) {
					if (Lower(e.picture) != oldKey) {
						continue;
					}
					auto it = moved.find(e.dds);
					if (it == moved.end()) {
						it = moved.emplace(e.dds, std::string{}).first;
					}
					e.picture = ToUtf8(a_new);
					changed.push_back(id);
				}
			}
			for (auto& [oldDds, newDds] : moved) {
				int shape = 0;
				{
					std::lock_guard lk(g_mtx);
					for (const auto& id : changed) {
						if (g_frames[id].dds == oldDds) {
							shape = g_frames[id].shape;
							break;
						}
					}
				}
				newDds = FollowTexture(oldDds, a_new, shape);
				EnsureTexture(newDds, a_new, shape);  // marks it ready, or converts again if the move failed
			}
			{
				std::lock_guard lk(g_mtx);
				for (const auto& id : changed) {
					auto& e = g_frames[id];
					if (const auto it = moved.find(e.dds); it != moved.end()) {
						e.dds = it->second;
					}
				}
			}
			for (const auto& id : changed) {
				QueueApply(id);  // the frame switches to the renamed texture
			}
			spdlog::info("Renamed {} -> {}: {} frame(s) updated", ToUtf8(a_old), ToUtf8(a_new), changed.size());
		});
	}

	void ChangeFrame(RE::ObjectRefHandle a_ref, int a_style)
	{
		SKSE::GetTaskInterface()->AddTask([a_ref, a_style] {
			auto old = a_ref.get();
			if (!old || old->IsDisabled() || old->IsMarkedForDeletion()) {
				return;  // already swapped
			}
			const int idx = FrameIndex(old->GetBaseObject());
			if (idx < 0) {
				return;
			}
			const int shape = Frames::ShapeOf(idx);  // same shape only: the picture texture fits as is
			const int style = Frames::ClampStyle(shape, a_style);
			if (style == Frames::StyleOf(idx)) {
				OpenFrameMenu(old.get());
				return;
			}
			auto* base = FrameForm(shape, style);
			if (!base) {
				Notify(Note::Error, "Folder Gallery: frame record missing - is FolderGallery.esp enabled?");
				return;
			}
			// New frame in exactly the same spot: position, angle (incl. any tilt), scale, cell.
			const RE::NiPoint3 pos = old->GetPosition();
			const RE::NiPoint3 angle = old->GetAngle();
			const float        scale = old->GetReferenceRuntimeData().refScale / 100.0f;
			auto*              cell = old->GetParentCell();
			auto*              world = old->GetWorldspace();
			auto handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
				base, pos, angle, cell, world, nullptr, nullptr, RE::ObjectRefHandle(), true, true);
			auto ref = handle.get();
			if (!ref) {
				Notify(Note::Error, "Folder Gallery: could not change the frame");
				spdlog::error("ChangeFrame: CreateReferenceAtLocation failed");
				return;
			}
			if (std::fabs(scale - 1.0f) > 0.001f) {
				SetRefScale(ref.get(), scale);
			}
			// The picture moves to the new frame before the old one goes, so its texture is never "unused".
			std::optional<Entry> entry;
			{
				std::lock_guard lk(g_mtx);
				if (const auto it = g_frames.find(old->GetFormID()); it != g_frames.end()) {
					entry = it->second;
					entry->style = static_cast<std::uint8_t>(style);
					g_frames[ref->GetFormID()] = *entry;
					g_frames.erase(old->GetFormID());
				}
			}
			if (entry) {
				EnsureTexture(entry->dds, FromUtf8(entry->picture), shape);  // file exists -> just marked ready
				QueueApply(ref->GetFormID());
			}
			DeleteFrameRef(old.get(), "replaced by Change frame");
			spdlog::info("Frame {:08X} -> {:08X}: changed to {} ({})", old->GetFormID(), ref->GetFormID(), Frames::StyleName(shape, style),
				entry ? entry->picture : std::string("no picture"));
			OpenFrameMenu(ref.get());  // the open frame menu switches to the new frame
		});
	}

	void ChangePicture(RE::ObjectRefHandle a_ref, const fs::path& a_picture)
	{
		SKSE::GetTaskInterface()->AddTask([a_ref, a_picture] {
			auto ref = a_ref.get();
			if (!ref) {
				return;
			}
			const int idx = FrameIndex(ref->GetBaseObject());
			if (idx < 0) {
				return;
			}
			const int  shape = Frames::ShapeOf(idx);
			const auto dds = DdsGamePath(a_picture, shape);
			{
				std::lock_guard lk(g_mtx);
				g_frames[ref->GetFormID()] = Entry{ dds, ToUtf8(a_picture), static_cast<std::uint8_t>(shape), static_cast<std::uint8_t>(Frames::StyleOf(idx)) };
			}
			spdlog::info("Frame {:08X}: picture changed to {}", ref->GetFormID(), ToUtf8(a_picture));
			EnsureTexture(dds, a_picture, shape);
			QueueApply(ref->GetFormID());
			CleanupUnusedTextures();
		});
	}
}
