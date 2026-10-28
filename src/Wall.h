#pragma once

namespace Wall
{
	struct Settings
	{
		int   resolution = 1024;   // long side of the generated DDS
		int   defaultStyle = 2;    // index into Frames::kStyles (Dark Oak)
		float maxDistance = 400.0f;
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
	void BeginPlacement(const std::filesystem::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving = {});
	bool IsPlacing();
	void TickPlacement();          // every frame while placing: moves the preview frame (SKSE task)
	void CycleStyle(int a_delta);  // mouse wheel while placing
	void RequestPlace();  // from the input handler; the raycast + placement run as an SKSE task
	void CancelPlacement(bool a_notify = true);

	// ---- frames in the world
	struct FrameInfo
	{
		RE::ObjectRefHandle ref;
		int                 shape = 0;
		int                 style = 0;
		std::string         picture;  // UTF-8 file name of the picture shown, empty if unknown
		std::filesystem::path picturePath;
	};
	// Set by the activation sink when the player activates one of our frames; taken by the UI.
	std::optional<FrameInfo> TakeActivatedFrame();
	void                     RemoveFrame(RE::ObjectRefHandle a_ref);
	void                     ChangePicture(RE::ObjectRefHandle a_ref, const std::filesystem::path& a_picture);
}
