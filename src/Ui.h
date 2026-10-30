#pragma once

// Hearthframe screens (gallery grid, picture viewer, hang dialog, frame menu).
// Pure ImGui + Theme: data in, the user's action out. Used by the plugin and by the preview harness.

#include "Theme.h"

#include <functional>
#include <utility>
#include <vector>

namespace Ui
{
	struct Picture
	{
		const char* name = "";
		ImTextureID thumb = 0;  // 0 while loading
		int         thumbW = 0, thumbH = 0;
		int         origW = 0, origH = 0;
		bool        failed = false;
		const char* error = "";  // reason, shown in the tooltip when failed
	};

	// A picture hanging somewhere in this save (Placed pictures window).
	struct PlacedPicture
	{
		const char* name = "";       // picture file name
		ImTextureID thumb = 0;       // 0 while loading / missing
		int         thumbW = 0, thumbH = 0;
		bool        missing = false;  // picture file not found
		const char* frame = "";      // frame name
		const char* shape = "";
		float       canvasAspect = 1.0f;
		int         sizePct = 100;
		const char* place = "";      // cell, e.g. "Breezehome"
		const char* region = "";     // location / hold, e.g. "Whiterun" ("" if none)
		bool        interior = true;  // interior cell (else an exterior worldspace cell)
		bool        stray = false;    // a frame with no record / picture
	};

	// A rendered picture of a frame with its canvas cut out (so the gallery can show any picture inside it).
	struct FramePreview
	{
		ImTextureID tex = 0;
		float       w = 0.0f, h = 0.0f;                          // image size in pixels
		float       hx0 = 0.0f, hy0 = 0.0f, hx1 = 0.0f, hy1 = 0.0f;  // the cut-out (canvas) in those pixels
	};

	struct GalleryModel
	{
		const char*                 folder = "";
		std::vector<Picture>        pictures;
		int                         framesOnWalls = 0;
		bool                        replaceMode = false;  // choosing a new picture for an existing frame
		const char*                 notice = nullptr;     // shown above the grid when set
		int                         openFolderStyle = 1;  // 0 off, 1 the path is a link, 2 an Open folder button (OpenFolder)
		int*                        thumbSize = nullptr;  // in/out, 100..320 (ThumbSize when let go)
		int*                        uiScalePct = nullptr; // in/out, 60..200 (applied on release -> UiScale)
		std::vector<PlacedPicture>  placed;                // filled after OpenPlaced
		bool                        placedLoading = false;
		std::function<void(int)>    requestThumb;          // called for visible pictures without a thumbnail

		// search (pictures holds only the matches; totalPictures counts all)
		char* search = nullptr;
		int   searchSize = 0;
		int   totalPictures = 0;

		// folders: 0 = all, 1.. = one per frame shape (Landscape, Square, Portrait); pictures holds only that folder
		int* shapeFolder = nullptr; // in/out
		int  folderCounts[4] = {};  // pictures in each (after the search)

		// rename (the box edits the name without its extension)
		char*       renameBuf = nullptr;
		int         renameSize = 0;
		const char* renameError = nullptr;  // why the last rename failed, shown in the box
		bool        renameDone = false;     // the last rename worked: close the box

		// viewer
		int         viewing = -1;
		ImTextureID full = 0;
		int         fullW = 0, fullH = 0;
		bool        fullFailed = false;

		// hang dialog
		std::function<std::pair<const char* const*, int>(int)> stylesFor;  // shape -> frame names, count
		const char* const*                   shapes = nullptr;
		int                                  shapeCount = 0;
		int*                                 hangStyle = nullptr;  // in/out
		int*                                 hangShape = nullptr;  // in/out
		bool                                 openHangDialog = false;  // set to open the dialog (cleared when opened)
		std::function<int(int, int)>         bestShape;            // (w, h) -> shape
		std::function<std::pair<int, int>(int)> textureSize;       // shape -> texture size
		std::function<FramePreview(int, int)>   framePreview;      // (shape, style) -> preview image (tex 0 = none)
	};

	enum class GalleryAction
	{
		None,
		Close,
		Refresh,
		Open,         // index
		Back,
		Prev,
		Next,
		OpenHangDialog,
		Hang,         // index, *hangShape, *hangStyle
		UseForFrame,  // index
		UiScale,      // *uiScalePct changed
		Rename,       // index, new name (without extension) in renameBuf
		RenameOpened, // index: the rename box opened (clear the old error)
		ThumbSize,    // *thumbSize changed (slider let go)
		OpenPlaced,   // the On walls box was clicked: fill placed
		DeletePlaced, // index into placed: take that picture down
		RemoveStrays, // remove every stray frame
		OpenFolder,   // open the picture folder in Explorer
		OpenSettings, // the Settings button
	};

	struct GalleryResult
	{
		GalleryAction action = GalleryAction::None;
		int           index = -1;
	};

	GalleryResult DrawGallery(GalleryModel& a_model);

	struct FrameModel
	{
		const char* style = "";
		const char* shape = "";
		const char* picture = "";  // file name, "" if unknown
		int*        sizePct = nullptr;
		bool        canMove = true;
		// change frame: the frames of this picture's shape
		const char* const* styles = nullptr;
		int                styleCount = 0;
		int                currentStyle = 0;
		int*               chosenStyle = nullptr;  // out, with FrameAction::ChangeFrame
		bool               changing = false;       // a swap is in progress (frame list disabled)
		// preview of the picture, cropped to the frame's shape
		ImTextureID preview = 0;
		int         previewW = 0, previewH = 0;
		bool        previewLoading = false;
		bool        previewMissing = false;
		float       canvasAspect = 1.0f;  // width / height of the frame's picture area
	};

	enum class FrameAction
	{
		None,
		Close,
		SizeChanged,
		Closer,
		Further,
		ChangePicture,
		ChangeFrame,  // *chosenStyle
		Move,
		TakeDown,
	};

	FrameAction DrawFrameMenu(FrameModel& a_model);
	void        ResetFrameMenu();  // back to the main frame menu (call when the menu opens)

	// ---- Settings window (edits a copy; Save writes it)
	struct SettingsModel
	{
		// gallery
		char  folder[520] = {};
		bool  recursive = false;
		const char* keyName = "F8";
		bool  waitingForKey = false;  // "press a key" mode
		int   thumbSize = 200;
		int   uiScalePct = 100;
		int   fontSize = 18;
		float mouseSpeed = 1.0f;
		// frames & hanging
		int   defaultStyle = 7;
		const char* const* landscapeStyles = nullptr;
		int   landscapeCount = 0;
		const char* defaultStyleOthers = "";  // what the same position means for Square / Portrait
		int   resolution = 1024;
		int   placeReach = 2000;
		int   examineReach = 600;
		bool  cleanUp = true;
		int   resizeWith = 0;  // 0 wheel + arrows, 1 arrows only, 2 wheel only
		// notifications
		bool notifications = true, showSize = false, showDistance = false, showFrameName = false, showPlacementHelp = true,
			 showConfirmations = true, showPlacementWarnings = true, showErrors = true, placementInfo = true;
		const char* iniPath = "";
		const char* error = nullptr;  // shown above the buttons; Save is disabled while set
		const char* keyWarning = nullptr;  // e.g. the chosen key is also a game control
	};

	enum class SettingsAction
	{
		None,
		Save,
		Cancel,
		OpenPictures,   // open the (saved) picture folder in Explorer
		ChangeKey,      // start waiting for a key
		ResetTab,       // index = tab
	};

	struct SettingsResult
	{
		SettingsAction action = SettingsAction::None;
		int            index = -1;
	};

	// Opens with OpenSettings(); call DrawSettings every frame while the gallery is open.
	void           OpenSettings(int a_tab = -1);  // a_tab >= 0 opens on that tab
	SettingsResult DrawSettings(SettingsModel& a_model);

	// Small line at the bottom of the screen while hanging a picture (no window, no input).
	void DrawPlacementInfo(const char* a_values, const char* a_keys);
}
