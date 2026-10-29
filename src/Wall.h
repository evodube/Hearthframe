#pragma once

namespace Wall
{
	struct Settings
	{
		int   resolution = 1024;   // long side of the generated DDS
		int   defaultStyle = 7;    // position in each shape's frame list (7 = Rustic Dark Oak / Smoked Oak)
		float maxDistance = 2000.0f;   // how far you can hang a picture
		float examineReach = 600.0f;   // how far a hung picture can be examined with Activate
		bool  cleanUp = true;          // delete converted textures no picture in the current save uses
		std::string placedFolder = "FolderGallery\\Placed";  // under Data\textures: where hung pictures' textures go (fixed)
		// top-left game notifications ([Notifications] in the INI); everything is also written to the log
		bool notifications = true;     // master switch
		bool showSize = false;         // "Size: 110%" while placing
		bool showDistance = false;     // "Distance from wall: +0.50" while placing
		bool showFrameName = false;    // "Frame: Rustic Dark Oak" while placing
		bool showPlacementHelp = true; // how-to line when placing starts
		bool showConfirmations = true; // picture hung / taken down / hanging cancelled
		bool showPlacementWarnings = true;  // can't hang it here (floor, too far...)
		bool showErrors = true;        // something went wrong
	};
	Settings& GetSettings();

	// Plugin load: registers the SKSE co-save callbacks.
	void InstallSerialization();
	// kDataLoaded: finds the 50 frame forms in FolderGallery.esp and registers game event sinks.
	bool Init();
	// kPostLoadGame / kNewGame: re-applies pictures to frames whose 3D is already loaded.
	void OnGameLoaded();

	// Moves the hung pictures' textures to another folder under Data\textures (a_relative, e.g. "MyGallery\\Hung")
	// and uses it from now on. Any thread.

	// Frame shape whose canvas aspect ratio is closest to a picture of a_w x a_h.
	int AutoShape(int a_w, int a_h);
	// Target DDS size for a shape (multiple of 4, long side = Settings::resolution).
	std::pair<int, int> TextureSize(int a_shape);

	// ---- placement mode (aim at a wall, left click to hang)
	void BeginPlacement(const std::filesystem::path& a_picture, int a_shape, int a_style, RE::ObjectRefHandle a_moving = {}, float a_scale = 1.0f);
	bool IsPlacing();
	// Current placement values for the on-screen info line (any thread). False when not placing.
	struct PlacementStatus
	{
		std::string frame;   // frame (style) name
		std::string shape;
		float       scale = 1.0f;
		float       distance = 0.0f;  // relative to the default distance from the wall
	};
	bool GetPlacementStatus(PlacementStatus& a_out);
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

	// Every picture hanging in this save, for the Placed pictures window (sorted by location).
	struct PlacedInfo
	{
		RE::ObjectRefHandle   ref;
		std::filesystem::path picture;
		std::string           name;      // file name (UTF-8)
		int                   shape = 0;
		int                   style = 0;
		float                 scale = 1.0f;
		std::string           place;     // cell / location name
		std::string           region;    // parent location, hold or worldspace ("" if none)
		bool                  interior = true;
		bool                  stray = false;  // a frame Folder Gallery has no record of (no picture)
	};
	// Gathers the list on the main thread; TakePlacedList returns it once ready (any thread).
	void                                   RequestPlacedList();
	std::optional<std::vector<PlacedInfo>> TakePlacedList();
	// Removes every frame Folder Gallery has no record of (leftover previews, frames whose record was lost).
	void                                   RemoveStrayFrames();
	// Opens the menu of the hung picture being looked at. Activate: no fallback (and the game's crosshair
	// target wins). Gallery key: a_otherwise runs when no picture is being looked at.
	void                     RequestExamine(std::function<void()> a_otherwise = {});
	void                     RemoveFrame(RE::ObjectRefHandle a_ref);
	void                     SetFrameScale(RE::ObjectRefHandle a_ref, float a_scale);
	void                     NudgeFrame(RE::ObjectRefHandle a_ref, float a_distance);  // + = away from the wall
	void                     ChangePicture(RE::ObjectRefHandle a_ref, const std::filesystem::path& a_picture);
	// A picture file was renamed on disk: frames showing it switch to the new name (their converted texture
	// is renamed to match), and the rename is remembered so frames in other saves find the file too.
	void                     PictureRenamed(const std::filesystem::path& a_old, const std::filesystem::path& a_new);
	// Swaps a hung frame for another frame of the same shape (style = index in that shape's list), keeping its
	// spot, size and picture. The frame menu then shows the new frame (via TakeActivatedFrame).
	void                     ChangeFrame(RE::ObjectRefHandle a_ref, int a_style);
}
