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
		constexpr const char*      kPlacedDir = "textures\\FolderGallery\\Placed\\";
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

		// Main thread only.
		void ApplyToRef(RE::FormID a_id)
		{
			std::string dds;
			{
				std::lock_guard lk(g_mtx);
				const auto      it = g_frames.find(a_id);
				if (it == g_frames.end()) {
					return;
				}
				const auto c = g_conversions.find(it->second.dds);
				if (c == g_conversions.end() || c->second != Conv::Ready) {
					return;  // applied when the conversion finishes
				}
				dds = it->second.dds;
			}
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
			if (ref && ApplyTexture(ref, dds)) {
				spdlog::info("{:08X}: picture applied ({})", a_id, dds);
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
				ApplyToRef(id);
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
					SKSE::GetTaskInterface()->AddTask([a_dds] { ApplyToAllUsing(a_dds); });
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
			const auto hit = RaycastFromCamera(g_settings.maxDistance);
			if (!hit) {
				RE::DebugNotification("Nothing in reach - aim at a wall closer to you");
				return;
			}
			if (hit->ref && hit->ref->Is(RE::FormType::ActorCharacter)) {
				RE::DebugNotification("You can't hang a picture on someone");
				return;
			}
			const auto& nrm = hit->normal;
			const float nlen = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y);
			if (std::fabs(nrm.z) > 0.6f || nlen < 0.3f) {
				RE::DebugNotification("That's not a wall - aim at a vertical surface");
				return;
			}
			const float nx = nrm.x / nlen, ny = nrm.y / nlen;

			const auto local = Frames::LocalFormID(pl->shape, pl->style);
			auto*      base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(local, kPluginFile);
			auto*      player = RE::PlayerCharacter::GetSingleton();
			if (!base || !player) {
				spdlog::error("Frame form {:03X} not found in {}", local, kPluginFile);
				RE::DebugNotification("Folder Gallery: frame record missing - is FolderGallery.esp enabled?");
				return;
			}
			// Frame origin is the centre of its back; its front faces local +Y.
			// Skyrim's Z angle turns local +Y towards (sin z, cos z), so z = atan2(nx, ny).
			const RE::NiPoint3 pos{ hit->pos.x + nx * 0.3f, hit->pos.y + ny * 0.3f, hit->pos.z };
			const RE::NiPoint3 angle{ 0.0f, 0.0f, std::atan2(nx, ny) };

			auto handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(
				base, pos, angle, player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), true, true);
			auto ref = handle.get();
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
			spdlog::info("Placed frame {:08X} ({} {}) at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg, picture {}",
				ref->GetFormID(), Frames::kShapes[pl->shape].name, Frames::kStyles[pl->style], pos.x, pos.y, pos.z,
				angle.z * 57.2958f, ToUtf8(pl->picture));
			EnsureTexture(dds, pl->picture, pl->shape);
			ApplyToRef(ref->GetFormID());  // works right away if the texture already exists and the 3D is loaded

			if (auto old = pl->moving.get()) {
				std::lock_guard lk(g_mtx);
				g_frames.erase(old->GetFormID());
				old->Disable();
				old->SetDelete(true);
			}
			CancelPlacement(false);
			RE::DebugNotification("Picture hung");
		}

		// ------------------------------------------------------------------ events
		class EventSink final :
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
						SKSE::GetTaskInterface()->AddTask([id] { ApplyToRef(id); });
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
			events->AddEventSink<RE::TESActivateEvent>(&g_sink);
		}
		return found == static_cast<int>(g_frameForms.size());
	}

	void OnGameLoaded()
	{
		std::vector<RE::FormID> ids;
		{
			std::lock_guard lk(g_mtx);
			for (const auto& [id, e] : g_frames) {
				ids.push_back(id);
			}
		}
		for (const auto id : ids) {
			ApplyToRef(id);
		}
	}

	void BeginPlacement(const fs::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving)
	{
		const int shape = std::clamp(a_shape, 0, Frames::kShapeCount - 1);
		const int style = std::clamp(a_style, 0, Frames::kStyleCount - 1);
		{
			std::lock_guard lk(g_mtx);
			g_placement = Placement{ a_picture, shape, style, a_moving };
		}
		// Start converting right away so the texture is usually ready by the time the frame is hung.
		EnsureTexture(DdsGamePath(a_picture, shape), a_picture, shape);
		g_placing = true;
		Notify("Aim at a wall and left-click to hang the picture. Right-click or Esc to cancel.");
	}

	bool IsPlacing()
	{
		return g_placing;
	}

	void RequestPlace()
	{
		if (g_placing && !g_placeQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask([] { DoPlace(); });
		}
	}

	void CancelPlacement(bool a_notify)
	{
		{
			std::lock_guard lk(g_mtx);
			g_placement.reset();
		}
		const bool was = g_placing.exchange(false);
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
			ApplyToRef(ref->GetFormID());
		});
	}
}
