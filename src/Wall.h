#pragma once

namespace Wall
{
	struct Settings
	{
		int   resolution = 1024;   // long side of the generated DDS
		int   defaultStyle = 2;    // index into Frames::kStyles (Dark Oak)
		float maxDistance = 2000.0f;   // how far you can hang a picture
		float examineReach = 600.0f;   // how far a hung picture can be examined with Activate
	};
	Settings& GetSettings();

	// Plugin load: registers the SKSE co-save callbacks.
	void InstallSerialization();
	// kDataLoaded: finds the 50 frame forms in FolderGallery.esp and registers game event sinks.
	bool Init();
	// kPostLoadGame / kNewGame: re-applies pictures to frames whose 3D is already loaded.
	void OnGameLoaded();

	// Frame shape whose canvas aspect ratio is closest to a picture of a_w x a_h.
	int AutoShape(int a_w, int a_h);
	// Target DDS size for a shape (multiple of 4, long side = Settings::resolution).
	std::pair<int, int> TextureSize(int a_shape);

	// ---- placement mode (aim at a wall, left click to hang)
	void BeginPlacement(const std::filesystem::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving = {}, float a_scale = 1.0f);
	bool IsPlacing();
	void Tick();                    // every frame: moves the preview frame, puts pictures on new frames (SKSE task)
	void CycleStyle(int a_delta);   // Shift + mouse wheel while placing
	void AdjustScale(int a_steps);  // mouse wheel while placing (10% per step)
	void AdjustOffset(int a_steps); // arrow up/down while placing (0.25 units per step, + = away from the wall)
	void RequestPlace();  // from the input handler; the raycast + placement run as an SKSE task
	void CancelPlacement(bool a_notify = true);

	// ---- frames in the world
	struct FrameInfo
	{
		RE::ObjectRefHandle ref;
		int                 shape = 0;
		int                 style = 0;
		float               scale = 1.0f;
		std::string         picture;  // UTF-8 file name of the picture shown, empty if unknown
		std::filesystem::path picturePath;
	};
	// Set by the activation sink when the player activates one of our frames; taken by the UI.
	std::optional<FrameInfo> TakeActivatedFrame();
	int                      FrameCount();  // pictures hanging in this save
	// Opens the menu of the hung picture being looked at. Activate: no fallback (and the game's crosshair
	// target wins). Gallery key: a_otherwise runs when no picture is being looked at.
	void                     RequestExamine(std::function<void()> a_otherwise = {});
	void                     RemoveFrame(RE::ObjectRefHandle a_ref);
	void                     SetFrameScale(RE::ObjectRefHandle a_ref, float a_scale);
	void                     NudgeFrame(RE::ObjectRefHandle a_ref, float a_distance);  // + = away from the wall
	void                     ChangePicture(RE::ObjectRefHandle a_ref, const std::filesystem::path& a_picture);
}
