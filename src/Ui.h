#pragma once

// Folder Gallery screens (gallery grid, picture viewer, hang dialog, frame menu).
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

	struct GalleryModel
	{
		const char*                 folder = "";
		std::vector<Picture>        pictures;
		int                         framesOnWalls = 0;
		bool                        replaceMode = false;  // choosing a new picture for an existing frame
		const char*                 notice = nullptr;     // shown above the grid when set
		int*                        thumbSize = nullptr;  // in/out, 100..320
		int*                        uiScalePct = nullptr; // in/out, 60..200 (applied on release -> UiScale)
		std::function<void(int)>    requestThumb;          // called for visible pictures without a thumbnail

		// viewer
		int         viewing = -1;
		ImTextureID full = 0;
		int         fullW = 0, fullH = 0;
		bool        fullFailed = false;

		// hang dialog
		const char* const*                   styles = nullptr;
		int                                  styleCount = 0;
		const char* const*                   shapes = nullptr;
		int                                  shapeCount = 0;
		int*                                 hangStyle = nullptr;  // in/out
		int*                                 hangShape = nullptr;  // in/out
		bool                                 openHangDialog = false;  // set to open the dialog (cleared when opened)
		std::function<int(int, int)>         bestShape;            // (w, h) -> shape
		std::function<std::pair<int, int>(int)> textureSize;       // shape -> texture size
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
	};

	enum class FrameAction
	{
		None,
		Close,
		SizeChanged,
		Closer,
		Further,
		ChangePicture,
		Move,
		TakeDown,
	};

	FrameAction DrawFrameMenu(FrameModel& a_model);
}
