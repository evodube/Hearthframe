# Hearthframe

An SKSE plugin for **Skyrim Special Edition** that adds an in-game picture gallery reading straight from a folder on your PC, and lets you hang any of those pictures on the walls of Skyrim in wooden, marble and painted frames.

Press **F8**, browse your screenshots or artwork, pick one, and hang it in your house, inn room or camp.

---

## Requirements

| Requirement | Notes |
|---|---|
| Skyrim Special Edition | Tested on **1.5.97**. Built for SE and AE (AE untested). VR is not supported. |
| [SKSE64](https://skse.silverlock.org/) | Built and tested with SKSE 2.0.20 |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | Use the version that matches your game |
| Windows 10 / 11 | Picture decoding uses the built-in Windows Imaging Component |

No other mods are needed: no SkyUI, MCM or other masters beyond `Skyrim.esm`. The plugin is **ESL-flagged**.

---

## Features

### Gallery
- Opens with **F8** (changeable in game) anywhere in the world
- Reads **PNG, JPG, JPEG, BMP and TGA** from a folder of your choice
  (default `Documents\My Games\Skyrim Special Edition\Hearthframe\Pictures`)
- **Refresh** picks up new pictures without restarting; optional **Include subfolders**
- **Folder tabs**: All / Landscape / Square / Portrait, with counts
- **Search** by name
- Picture-shaped cards, **thumbnail size slider** (remembered)
- Full-size **viewer** with Previous / Next
- **Rename** pictures from the gallery (renames the file; hung copies follow the rename)
- **Open folder** button that opens the picture folder in Explorer
- Fast loading: background decoding at a reduced size, a disk thumbnail cache, visible pictures first
- Scalable interface (Ctrl + mouse wheel) that stays sharp from 1080p to 4K, with an always-visible scroll bar

### Hanging pictures
- **59 frames**: 29 landscape, 20 square and 10 portrait (rustic woods, noble carved woods, marble, painted)
- The frame shape is picked automatically from the picture's proportions
- A live preview follows your aim and sits flat against the wall
- Size **30% to 300%**, frame style and distance from the wall can all be adjusted while placing
- Pictures are converted to DDS (BC1 with mipmaps) at **512 to 4096** px on the long side
- Hung pictures are stored in the SKSE co-save and persist with your save game

**Controls while hanging**

| Action | Keys |
|---|---|
| Hang | Left-click |
| Size | Mouse wheel or Left / Right (hold to repeat) |
| Frame style | Shift + wheel or Shift + Left / Right |
| Distance from wall | Up / Down (hold; Shift for big steps) |
| Cancel | Right-click or Esc |

*Settings → Frames & hanging → Resize with* chooses **Wheel + arrows**, **Arrows only** (the wheel zooms the camera as usual) or **Wheel only**.

### Managing hung pictures
- **Activate** a hung picture (or press F8 while looking at it) for its frame menu:
  **Change picture**, **Change frame** (same shape, picture kept), **Move**, **Closer / Further**, **Take down**
- **Placed Pictures** window (click *On walls* in the gallery header): a grid of every hung picture with its name and
  location (interior / exterior); delete any of them, even in cells that aren't loaded
- Finds and removes **stray frames** with no record (left over from crashes or older versions)
- Optional cleanup of picture textures that no save uses any more

### Settings
- In-game **Settings** window with Save / Cancel; changes apply immediately and are written to `Hearthframe.ini`
- Gallery key picker (ignores Tab / Alt / Ctrl / Shift / Win / Enter, cancels on Alt+Tab, warns about keys the game already uses)
- Per-type **notification** switches and an on-screen placement info line

---

## Installation

1. Install SKSE64 and Address Library.
2. Install the Hearthframe archive with your mod manager and enable **`Hearthframe.esp`**.
3. Put pictures in `Documents\My Games\Skyrim Special Edition\Hearthframe\Pictures` (created on first run),
   or choose another folder in **Settings**.

**Mod Organizer 2:** the textures made for hung pictures are written to `Data\textures\Hearthframe\Placed`, which MO2
puts in *overwrite*. That is expected. Keep them there or move them into their own mod.

**Uninstalling:** take your pictures down first (the Placed Pictures window makes this quick), save, then remove the mod.

**ENB:** a faint shimmer around frames comes from ENB's ambient occlusion. Enabling AO **Complex Filter** fixes it.

### Installed files

```
Data/
├── Hearthframe.esp                     59 frame activators (ESL)
├── Meshes/Hearthframe/*.nif            frame meshes (Landscape_*, Square_*, Portrait_*)
├── Textures/ax/paintings/*.dds         frame textures
└── SKSE/Plugins/
    ├── Hearthframe.dll
    └── Hearthframe.ini
```

Created at run time:

| Path | Contents |
|---|---|
| `Documents\My Games\Skyrim Special Edition\Hearthframe\Pictures` | default picture folder |
| `Documents\My Games\Skyrim Special Edition\Hearthframe\ThumbCache` | thumbnail cache (safe to delete) |
| `Data\textures\Hearthframe\Placed` | DDS textures of hung pictures |
| `Documents\My Games\Skyrim Special Edition\SKSE\Hearthframe.log` (+ `.1`, `.2`) | logs of the last three runs |

---

## Configuration

Everything can be changed in game, but [`Hearthframe.ini`](Hearthframe.ini) is documented line by line:

| Section | Keys |
|---|---|
| `[General]` | `sFolder`, `iToggleKey`, `bRecursive`, `iThumbnailSize`, `iGalleryFolder`, `fFontSize`, `fUIScale`, `fMouseSpeed` |
| `[Frames]` | `iPictureResolution`, `iDefaultFrameStyle`, `fPlaceReach`, `fExamineReach`, `bCleanUpTextures`, `iResizeWith` |
| `[Notifications]` | `bNotifications`, `bShowSize`, `bShowDistance`, `bShowFrameName`, `bShowPlacementHelp`, `bShowConfirmations`, `bShowPlacementWarnings`, `bShowErrors`, `bShowPlacementInfo` |
| `[Debug]` | `bHookPresent`, `bInputSink` (troubleshooting switches) |

---

## Building

The plugin is built by **GitHub Actions** ([`.github/workflows/build.yml`](.github/workflows/build.yml)) on every push to `main`.
The built `Data/` folder (dll, ini, esp) is committed to the **`dist`** branch, and the `.pdb` is uploaded as an artifact.

To build locally on Windows:

```powershell
git clone --recursive https://github.com/evodube/Hearthframe
cd Hearthframe
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

- Visual Studio 2022, CMake 3.24+, [vcpkg](https://github.com/microsoft/vcpkg)
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) v3.7.0 as a git submodule in `extern/`
- vcpkg packages (see [`vcpkg.json`](vcpkg.json)): fmt, spdlog, rapidcsv, imgui (dx11), stb

The frame meshes and textures are **not** in this repository. They ship in the release archive only.

### Layout

| Path | What it is |
|---|---|
| `src/main.cpp` | SKSE entry point, logging, Present hook install |
| `src/Gallery.cpp` | Gallery window, input, picture loading and thumbnail cache, settings |
| `src/Ui.cpp`, `src/Theme.cpp` | ImGui screens and the parchment / wood look |
| `src/Wall.cpp` | Hanging frames: placement, frame menu, co-save, cleanup |
| `src/Dds.cpp` | Picture → DDS (BC1 + mipmaps) conversion |
| `src/Frames.h` | Frame list (generated) |
| `tools/catalog.py` | Frame catalog: source mesh → shape and style name |
| `tools/make_esp.py` | Generates `Hearthframe.esp` and `src/Frames.h` from the catalog (`python tools/make_esp.py tools/frames.json`) |
| `tools/frames.json` | Measured object bounds for each frame |
| `tools/nif/` | NIF helpers used to convert the frame meshes to SSE format |

---

## Credits

- **Artisanix** for the painting frame meshes and textures
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT) for the interface
- [stb](https://github.com/nothings/stb) for image loading and DXT compression
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) and the [SKSE](https://skse.silverlock.org/) team
