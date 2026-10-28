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
		constexpr const char*      kPlacedDir = "textures\\FolderGallery\\Placed\\";
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
		std::array<RE::FormID, Frames::kShapeCount * Frames::kStyleCount> g_frameForms{};

		struct Placement
		{
			fs::path            picture;
			int                 shape = 0;
			int                 style = 0;
			RE::ObjectRefHandle moving;
			float               scale = 1.0f;
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

		void Notify(const std::string& a_msg)
		{
			SKSE::GetTaskInterface()->AddTask([a_msg] { RE::DebugNotification(a_msg.c_str()); });
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
			return fmt::format("{}{}_{}_{:016x}.dds", kPlacedDir, stem, Frames::kShapes[a_shape].name, Fnv1a(key));
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
					Notify("Folder Gallery: could not convert that picture (see FolderGallery.log)");
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
			const int idx = std::clamp(a_shape, 0, Frames::kShapeCount - 1) * Frames::kStyleCount + std::clamp(a_style, 0, Frames::kStyleCount - 1);
			const auto id = g_frameForms[idx];
			return id ? RE::TESForm::LookupByID<RE::TESObjectACTI>(id) : nullptr;
		}

		struct Pose
		{
			RE::NiPoint3 pos;
			float        nx = 0.0f, ny = 1.0f;  // horizontal wall normal (unit)
		};

		// Where a frame would hang right now, or why it can't.
		std::optional<Pose> ComputePose(const char*& a_whyNot, RE::TESObjectREFR* a_ignore = nullptr)
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
			// Frame origin is the centre of its back; nudge it off the wall to avoid flicker.
			pose.pos = { hit->pos.x + pose.nx * 0.3f, hit->pos.y + pose.ny * 0.3f, hit->pos.z };
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
		int                 g_previewTicks = 0;         // ticks since the preview was created (diagnostics)
		bool                g_previewMoved = false;
		RE::FormID          g_stalePreview = 0;         // from a save made while aiming; deleted after load

		void DeletePreview()
		{
			if (auto ref = g_preview.get()) {
				ref->Disable();
				ref->SetDelete(true);
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
			auto        pose = ComputePose(why, pl->moving.get().get());
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
				spdlog::info("Preview frame {:08X} ({} {})", ref->GetFormID(), Frames::kShapes[pl->shape].name, Frames::kStyles[pl->style]);
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
			const auto  pose = ComputePose(why, pl->moving.get().get());
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
			const auto  pose = ComputePose(why, pl->moving.get().get());
			if (!pose) {
				RE::DebugNotification(why);
				return;
			}
			auto* base = FrameForm(pl->shape, pl->style);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!base || !player) {
				spdlog::error("Frame form for {} {} not available (form id {:08X})", Frames::kShapes[pl->shape].name,
					Frames::kStyles[pl->style], g_frameForms[pl->shape * Frames::kStyleCount + pl->style]);
				RE::DebugNotification("Folder Gallery: frame record missing - is FolderGallery.esp enabled?");
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
				RE::DebugNotification("Folder Gallery: could not place the frame here");
				spdlog::error("CreateReferenceAtLocation failed");
				return;
			}

			const auto dds = DdsGamePath(pl->picture, pl->shape);
			{
				std::lock_guard lk(g_mtx);
				g_frames[ref->GetFormID()] = Entry{ dds, ToUtf8(pl->picture), static_cast<std::uint8_t>(pl->shape), static_cast<std::uint8_t>(pl->style) };
			}
			spdlog::info("Placed frame {:08X} ({} {}, {:.0f}%) at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg, picture {}",
				ref->GetFormID(), Frames::kShapes[pl->shape].name, Frames::kStyles[pl->style], pl->scale * 100.0f, pose->pos.x, pose->pos.y, pose->pos.z,
				angle.z * 57.2958f, ToUtf8(pl->picture));
			EnsureTexture(dds, pl->picture, pl->shape);
			QueueApply(ref->GetFormID());  // applied as soon as the new frame's 3D exists

			if (auto old = pl->moving.get()) {
				{
					std::lock_guard lk(g_mtx);
					g_frames.erase(old->GetFormID());
				}
				old->Disable();
				old->SetDelete(true);
			}
			DeletePreview();
			CancelPlacement(false);
			RE::DebugNotification("Picture hung");
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
					bool       ours = false;
					{
						std::lock_guard lk(g_mtx);
						ours = g_frames.contains(id);
					}
					if (ours) {
						SKSE::GetTaskInterface()->AddTask([id] { QueueApply(id, 300); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (!a_event || !a_event->objectActivated || !a_event->actionRef || a_event->actionRef.get() != RE::PlayerCharacter::GetSingleton()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto*     ref = a_event->objectActivated.get();
				const int idx = FrameIndex(ref->GetBaseObject());
				if (idx < 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				FrameInfo info;
				info.ref = ref->GetHandle();
				info.scale = ref->GetReferenceRuntimeData().refScale / 100.0f;
				info.shape = idx / Frames::kStyleCount;
				info.style = idx % Frames::kStyleCount;
				{
					std::lock_guard lk(g_mtx);
					if (const auto it = g_frames.find(ref->GetFormID()); it != g_frames.end()) {
						info.picturePath = FromUtf8(it->second.picture);
						info.picture = ToUtf8(info.picturePath.filename());
					}
					g_activated = info;
				}
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
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			std::lock_guard lk(g_mtx);
			g_frames.clear();
			g_activated.reset();
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
		auto* dh = RE::TESDataHandler::GetSingleton();
		int   found = 0;
		for (int s = 0; s < Frames::kShapeCount; ++s) {
			for (int t = 0; t < Frames::kStyleCount; ++t) {
				const int idx = s * Frames::kStyleCount + t;
				auto*     form = dh ? dh->LookupForm(Frames::LocalFormID(s, t), kPluginFile) : nullptr;
				g_frameForms[idx] = form ? form->GetFormID() : 0;
				found += form ? 1 : 0;
			}
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
		if (g_stalePreview) {
			if (auto* stale = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_stalePreview)) {
				spdlog::info("Removing preview frame {:08X} left in the save", g_stalePreview);
				stale->Disable();
				stale->SetDelete(true);
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
	}

	void BeginPlacement(const fs::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving, float a_scale)
	{
		const int shape = std::clamp(a_shape, 0, Frames::kShapeCount - 1);
		const int style = std::clamp(a_style, 0, Frames::kStyleCount - 1);
		{
			std::lock_guard lk(g_mtx);
			g_placement = Placement{ a_picture, shape, style, a_moving, std::clamp(a_scale, kMinScale, kMaxScale) };
		}
		// Start converting right away so the texture is usually ready by the time the frame is hung.
		EnsureTexture(DdsGamePath(a_picture, shape), a_picture, shape);
		g_placing = true;
		Notify("Aim at a wall, left-click to hang. Wheel: size, Shift+wheel: frame. Right-click/Esc cancels.");
	}

	bool IsPlacing()
	{
		return g_placing;
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
		Notify(fmt::format("Size: {:.0f}%", scale * 100.0f));
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
			st = ((st + a_delta) % Frames::kStyleCount + Frames::kStyleCount) % Frames::kStyleCount;
			name = Frames::kStyles[st];
		}
		Notify(fmt::format("Frame: {}", name));
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
			SKSE::GetTaskInterface()->AddTask([moving] {
				DeletePreview();
				if (auto old = moving.get(); old && !old->IsDisabled() && old->Get3D()) {
					old->Get3D()->SetAppCulled(false);  // move cancelled (or the old frame is gone anyway)
				}
			});
		}
		if (was && a_notify) {
			Notify("Hanging cancelled");
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
			ref->Disable();
			ref->SetDelete(true);
			RE::DebugNotification("Picture taken down");
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
			const int  shape = idx / Frames::kStyleCount;
			const auto dds = DdsGamePath(a_picture, shape);
			{
				std::lock_guard lk(g_mtx);
				g_frames[ref->GetFormID()] = Entry{ dds, ToUtf8(a_picture), static_cast<std::uint8_t>(shape), static_cast<std::uint8_t>(idx % Frames::kStyleCount) };
			}
			spdlog::info("Frame {:08X}: picture changed to {}", ref->GetFormID(), ToUtf8(a_picture));
			EnsureTexture(dds, a_picture, shape);
			QueueApply(ref->GetFormID());
		});
	}
}
