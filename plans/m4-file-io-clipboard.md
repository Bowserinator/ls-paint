# M4 — File I/O & clipboard

## Status

**Built.** The "As built" section below records what shipped; the rest of this
document is the original design and stays as the record of what was planned.

## As built

- `src/io/document_io.{hpp,cpp}` — `ImageFormat`, `LoadResult`, `load`, `save`,
  `format_from_extension`, `extension_for`, `format_from_index`, `index_from`,
  `flatten_to_opaque`, over stb_image and stb_image_write.
- `src/io/stb_impl.cpp` — the single implementation unit for both stb image
  headers.
- `src/io/clipboard.{hpp,cpp}` — `Clipboard::set_image`/`get_image` over the
  vendored `third_party/clip`, converting between its RGBA spec and `Rgba8`.
- `src/history/history_stack.{hpp,cpp}` — gained `clear()`, `is_modified()` and
  `mark_clean()`; every push, undo and redo marks the document modified.
- `src/app/app_controller.{hpp,cpp}` — `IoResult`, `new_document`,
  `open_document`, `save_document`, `display_name`, `default_directory`,
  `list_directory`, `copy_to_clipboard`, `paste_from_clipboard`,
  `paste_from_file`, `copy_source_rect`.
- `ui/topbar.slint` — the classic File/Home/View tab strip; the std-widgets
  tabs looked like a different application's chrome.
- `ui/bar/bar-file.slint` — the File menu: New, Open, Save, Save As, Exit.
- `ui/dialogs/{file-dialog,confirm-dialog,message-dialog}.slint` — the modal
  file browser, the unsaved-changes question and the error box.
- `ui/globals.slint` — `DocumentState.title`/`modified`, `DialogState`,
  `EFileDialogMode`, and the `file-*` / `paste-from-file` callbacks.
- `src/ui/ui_state.{hpp,cpp}` — the dialog driving, the pending-action state
  machine, the window title and the close request handler.
- `tests/io_test.cpp` and the file steps in `tests/ui_tests.cpp`.

Differences from the design above: `CanvasDocument` needed no new API —
`restore(...)` replaces the planned `replace_contents()` and `resize(w, h, fill)`
is the planned `set_size()`, keeping the overlap and filling the new area; the
planned `ClipboardPasteCommand` was not written because the existing
`CanvasResizeCommand` already expresses a whole-image paste, so one undo restores
the pixels *and* the old canvas size; the size guard is `kMaxImageDimension`
beside the existing `kMaxCanvasDimension`; stb is fetched with `FetchContent`,
while clip and the DejaVu faces are vendored copies.

The tree before this milestone (verified by reading the code, not assumed):

Already present:

- **Chrome for the whole milestone, unwired**:
  - `ui/topbar.slint:14,19` — exactly two `Tab`s titled `"First"` / `"Second"` hosting
    `HomeBar` / `ViewBar`. No File tab, no file menu anywhere.
  - `ui/bar/bar-home.slint:13-56` — Clipboard group: a `Paste` `ImageButtonVerticalDropdown`
    whose main button sets `UserState.selected-tool = ETool.PASTE` (line 36) and whose two
    dropdown items ("Paste", "Paste from file", lines 19-25) set `ETool.NONE` — all no-ops;
    `Cut` (47-50) and `Copy` (51-54) `ImageButtonHorizontal`s have **no** `clicked` handler.
  - Icons: `icons/cut.png`, `icons/copy.png`, `icons/paste.png` all exist. **No file
    icons exist** — `icons/` contains only: about, clear-selection, colorpicker, copy,
    crop, cut, edit-colors, eraser, free-selection, invert-selection, magnify, paint,
    paste, pencil, resize, rotate, select-all, selection, shape-fill, shape-outline, size,
    text, transparent-selection (no new/open/save/exit assets → File tab is text-only).
- `ui/globals.slint` — `DocumentState` has only `canvas-width`/`canvas-height`; no
  `dirty`, no `title`. No dialog state of any kind.
- `src/app/app_controller.{hpp,cpp}` — owns document/history/brush/router only; no file
  or clipboard methods, no path.
- `src/core/canvas_document.{hpp,cpp}` — only `mark_dirty(RectI)`/`flush()`; no content
  replace, no resize.
- `src/history/history_stack.{hpp,cpp}` — `push/undo/redo` only; no `clear()`, no
  modified/saved flag; `src/history/commands/` has only `paint_stroke_command.*`.
- `src/render/slint_bridge.{hpp,cpp}` — `SharedPixelBuffer` sized once in the `UiState`
  ctor (`ui_state.cpp:38`); no resize path (m0 plan Risks explicitly lists this gap).
- `src/ui/shortcuts.{hpp,cpp}` — `ShortcutAction { Undo, Redo }`, 3 rows in `kShortcuts`;
  `ui_state.cpp:81-89` still has the ad-hoc `if (*action == Undo) … else redo()`.
- `ui/` has **no** `ui/dialogs/`, no confirm/message overlay.
- No `src/io/`, no `third_party/`.
- `CMakeLists.txt` — `project(… LANGUAGES CXX)` (no C), one `FetchContent` (Slint
  `v1.17.1`), no stb, no clip.

Reference material verified in `image-viewer.zip`:

- `third_party/clip/` — full vendored `clip` checkout (sources + `CMakeLists.txt` +
  junk: `.git/`, `CMakeFiles/`, `Makefile`, `cmake_install.cmake`, `examples/`,
  `tests/`, `.github/`). Its `CMakeLists.txt` defaults `CLIP_ENABLE_IMAGE=on`, on
  Linux compiles `clip_x11.cpp`, `check_include_files(xcb/xcb.h …)` (FATAL_ERROR if
  missing), `check_include_files(png.h …)` + links `xcb pthread png` — the **C**
  language must be enabled in our project for `check_include_files` to run.
- `CMakeLists.txt` of image-viewer shows the exact FetchContent-stb pattern
  (`FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master)`,
  include via `${stb_SOURCE_DIR}`) and `add_subdirectory(third_party/clip)` with
  `CLIP_EXAMPLES/CLIP_TESTS/CLIP_INSTALL OFF`.
- It also vendors `third_party/portable-file-dialogs/portable-file-dialogs.h` and uses
  `pfd::open_file` in `src/ui/app.cpp:171,185` (considered and rejected — see Design).

Verified against the Slint 1.17.1 sources in `build/_deps/slint-src`:

- **Close interception**: there is **no** Slint-side `close-requested` callback
  (`internal/compiler/builtins.slint` `WindowItem`, lines 1500-1568, declares `close()`
  and `hide()` only — the phrase "triggers the `close-requested` callback" appears only
  in a doc comment). The API is C++-side: `slint::Window::on_close_requested(F)` where F
  returns `slint::CloseRequestResponse::{HideWindow, KeepWindowShown}`
  (`api/cpp/include/private/slint_window.h:426-435`, enum at
  `slint-build/generated_include/private/slint_generated_public.h:88`, pulled in by
  `slint.h:10`). Canonical usage: `build/_deps/slint-src/examples/todo/cpp/app.cpp:41`.
  The generated `AppWindow` exposes `auto window() const -> slint::Window&;`
  (`build/appwindow.h:1852`) and `hide()`; `Window::dispatch_close_requested_event()`
  is public (`slint_window.h:694`) — that is the Exit-button entry point.
- std-widgets available and API-checked: `Button` (`text`, `primary`, `clicked`),
  `LineEdit` (`in-out text`, `callback accepted(text: string)`), `ComboBox`
  (`model: [string]`, `in-out current-index`, `selected(value)`), `ListView`
  (`for … in model : Row`, rows need explicit `height`), conditional element syntax
  `if cond : Element {}` (used throughout `tests/cases/`), `PopupWindow.close-policy`
  replaces deprecated `close-on-click` (bar-home.slint:327 still uses the old one — not
  our file to fix beyond what we touch).

Handoffs from earlier plans (do not contradict them):

- `plans/m1-core-drawing-tools.md` lands `UiState::handle_shortcut(ShortcutAction)`
  (a `switch`), documents the 3-step shortcut recipe in `shortcuts.hpp`, adds
  `InputRouter::active()`, writes `DocumentState.can-undo/can-redo` in `refresh()`
  **before** the `flush()` early-return, and creates the Slint-free CTest target
  `ls_paint_tests` (sources + a `run_*_tests()` entry per file, no framework). It
  explicitly hands M4: *"Ctrl+C/X/V rows appended to `kShortcuts` + one
  `handle_shortcut` case each … `DocumentState.dirty` (M4) sits beside `can-undo` in
  the same `refresh()` write."*
- `plans/m0-foundation.md` Risks: `composite()` assumes every layer has the target's
  size (resize must touch **all** layers + `m_flattened` together), and `SlintBridge`
  reallocation + full re-upload is required when canvas dims change.

## Acceptance criteria

1. **File tab**: the ribbon has three tabs titled `File`, `Home`, `View` (was
   `First`/`Second`). The File tab shows five text buttons: New, Open, Save, Save As,
   Exit; each is clickable and does what its label says.
2. **New** (menu or Ctrl+N): canvas becomes 800×600 opaque white, undo history is
   cleared (Ctrl+Z is a no-op afterwards), window title shows `untitled - LS Paint`
   (dirty state cleared), current path cleared (next Save opens Save As).
3. **Open** (menu or Ctrl+O): a modal open dialog appears; it lists the current
   directory (dirs first, then files, `..` first), an `Up` button navigates to the
   parent, a filter combo offers `Images (*.png;*.bmp;*.jpg;*.jpeg)` / `All files (*)`,
   and clicking a directory row enters it while clicking a file row fills the path
   field. Accepting loads the file: canvas becomes the image's size, content is
   top-left aligned, the texture updates in one repaint, undo history cleared, title
   shows the file name, dirty cleared.
4. **Save/Save As** (menu, Ctrl+S, Ctrl+Shift+S): Save As shows a modal dialog with a
   format combo `PNG / BMP / JPEG`; an extension-less name gets the format's extension
   appended; the file is written and re-openable in another viewer; a successful save
   clears the dirty marker and sets the current path (so a later Ctrl+S saves without
   a dialog).
5. **Format fidelity**: PNG round-trips alpha; BMP and JPEG are written flattened onto
   opaque white (no black/transparent halo); a JPEG saved from a transparent document
   looks white where alpha was 0.
6. **Open failure** (missing/corrupt/oversized file) shows an error dialog with a
   human-readable message and leaves the current document, path, history, and title
   untouched.
7. **Copy** (button or Ctrl+C) puts the composited region (whole canvas when there is
   no selection — see Design for the M2 hook) on the system clipboard as RGBA; pasting
   into another app (e.g. another image editor or `clip`-based tool) yields the same
   pixels.
8. **Cut** (button or Ctrl+X) = Copy + fill of the same region with Color 2
   (background color, matching `EraserTool`), and is a **single** undo step: one
   Ctrl+Z restores the removed pixels byte-exactly and re-marks dirty.
9. **Paste** (button, dropdown item, or Ctrl+V) inserts the clipboard image at (0,0),
   rasterized immediately into the active layer; if the image is larger than the
   canvas in either dimension the canvas grows to fit it (mspaint growth rule,
   top-left anchored); it is one undo step that restores both pixels **and** the
   previous canvas size; dirty becomes true.
10. **Paste from file** (dropdown item): opens the same modal dialog in paste mode and
    pastes the chosen file's pixels exactly like criterion 9.
11. **Empty clipboard paste** (or clipboard unavailable) shows an error dialog; the
    document is unchanged and no undo entry is created.
12. **Unsaved-changes prompt**: closing the window (title-bar X) while dirty shows a
    modal confirm dialog `Save changes to <name>?` with Save / Don't Save / Cancel —
    Don't Save closes, Cancel stays open, Save saves (routing through Save As if the
    document has no path) and then closes. Closing while clean closes immediately with
    no dialog. The Exit button follows the identical path (it dispatches a close
    request). New/Open while dirty get the same prompt, and the chosen action runs
    after a successful save.
13. **Shortcuts**: Ctrl+N/O/S/Shift+S/C/X/V all resolve through `kShortcuts` +
    `handle_shortcut`; none fire while a modal dialog is open or mid-stroke.
14. **Title bar**: title is `<file name or "untitled">` + ` * ` when dirty + ` - LS Paint`;
    the `*` appears on the first edit and disappears after save/new/open.
15. `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` is
    warning-clean under `-Wall -Wextra`, and `ctest --test-dir build --output-on-failure`
    passes including the new `run_document_io_tests()` cases.

## Design

### Data flow (Slint ↔ UiState ↔ AppController ↔ core)

```
ui/bar/bar-file.slint, bar-home.slint, dialogs/*.slint   (buttons/fields)
        │  UserState.file-* / clipboard-* callbacks, DialogState.* callbacks,
        │  DialogState properties (file-path, file-format, file-filter, …)
        v
src/ui/ui_state.cpp   UiState::handle_shortcut / lambdas
        │  reads DialogState, calls AppController, writes results back into globals,
        │  then refresh()
        v
src/app/app_controller.{hpp,cpp}      owns path + IO orchestration
        │                │
        v                v
src/io/document_io.*   src/io/clipboard.*   (stb_image/_write, vendored clip)
        │                │
        v                v
core::CanvasDocument (replace_contents/set_size)  ←  HistoryStack
        │                                            (PaintStrokeCommand for cut,
        v                                             ClipboardPasteCommand for paste)
render::SlintBridge::resize → set_texture
```

Rules: `src/io/` includes no Slint headers; `src/app/` includes no Slint headers;
only `src/ui/` touches globals; every controller mutation ends with
`UiState::refresh()` which is the single place that syncs `DocumentState` and the
texture (same pattern as M1's `can-undo` sync).

### 1. `src/io/document_io.hpp/.cpp` — exact API

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <string>

#include "core/pixel_buffer.hpp"

namespace lsp {

class CanvasDocument;

enum class ImageFormat { Png, Bmp, Jpeg };

struct LoadResult {
    bool ok = false;
    PixelBuffer pixels;        // valid iff ok
    std::string error;         // non-empty iff !ok
};

// Decodes PNG/BMP/JPEG (plus anything stb_image supports) to straight RGBA8.
[[nodiscard]] LoadResult load(const std::filesystem::path& path);

// Writes document.flattened() in `format`. `error` (may be null) receives the
// reason on failure. Alpha is flattened onto white for Bmp/Jpeg.
[[nodiscard]] bool save(const CanvasDocument& document, const std::filesystem::path& path,
                        ImageFormat format, std::string* error);

[[nodiscard]] std::optional<ImageFormat> format_from_extension(const std::filesystem::path& path);
[[nodiscard]] const char* extension_for(ImageFormat format);   // ".png" / ".bmp" / ".jpg"
[[nodiscard]] ImageFormat format_from_index(int index);        // clamps to Png
[[nodiscard]] int index_from(ImageFormat format);              // 0/1/2

// Alpha-over-background helper used by save() for Bmp/Jpeg (out.a = 255).
[[nodiscard]] PixelBuffer flatten_to_opaque(const PixelBuffer& source,
                                            Rgba8 background = {255, 255, 255, 255});

}  // namespace lsp
```

Implementation notes (`.cpp`):

- `#include <stb_image.h>` / `#include <stb_image_write.h>` **without** the
  implementation macros (declarations only); the implementation lives in its own TU
  (below).
- `load()`: guard `std::filesystem::is_regular_file` → `"Not a file: <path>"`;
  `stbi_load(path.string().c_str(), &w, &h, &channels, 4)` → straight RGBA;
  failure → `"Cannot decode image: <stbi_failure_reason()>"` (requires
  `STBI_FAILURE_USERMSG` in the impl TU); `w/h <= 0 || w > kMaxImageDimension ||
  h > kMaxImageDimension` → `"Image too large"`; free with `stbi_image_free`.
- `save()`: `document.flattened()` must be current — the caller (`AppController`)
  calls `document.flush()` first (defensive; flush is idempotent when clean).
  - Png: `stbi_write_png(path.string().c_str(), w, h, 4, px, w * 4)`.
  - Bmp: `flatten_to_opaque` then `stbi_write_bmp(…, w, h, 3, px)`.
  - Jpeg: `flatten_to_opaque` then `stbi_write_jpg(…, w, h, 3, px, kJpegQuality)`.
  - Any `0` return → `"Failed to write <path>"`. Missing parent directory →
    `"Directory does not exist: <parent>"` (checked with `fs::exists` up front).
- `format_from_extension` is case-insensitive (`.PNG`, `.Jpg`) and maps only
  png/bmp/jpg/jpeg; unknown → `std::nullopt`.

**stb acquisition — DECISION: FetchContent, not vendoring.**
plan.md §4 says both stb and clip should come from the established FetchContent
pattern "rather than re-vendoring"; for stb there is no reason to deviate: it is two
header-only public-domain files with no build system, image-viewer already proves the
pattern works with this exact toolchain, the repo already requires network at
configure time (Slint is fetched the same way), and vendoring would add ~700 KB of
third-party source plus a license file to review. clip is the opposite case — it is a
compiled multi-platform library that we need *exactly* as tested in image-viewer, and
its CMake probes system libs — so clip is vendored from the zip (below). Exact CMake
(add to `CMakeLists.txt`, see Build changes):

```cmake
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)
...
target_include_directories(ls_paint SYSTEM PRIVATE ${stb_SOURCE_DIR})
```

(`SYSTEM` so `-Wall -Wextra` stays clean for the stb implementation code, which lives
in headers. `stb` has no `CMakeLists.txt`, so `FetchContent_MakeAvailable` only
populates — same as image-viewer. `GIT_TAG master` matches image-viewer; stb publishes
no version tags. Hardening note for later: after the first successful configure run
`git -C build/_deps/stb-src rev-parse HEAD` and pin that SHA.)

**The `STB_IMAGE_IMPLEMENTATION` TU** — new file `src/io/stb_impl.cpp`, exactly:

```cpp
#include "config.hpp"

#define STBI_MAX_DIMENSIONS lsp::kMaxImageDimension
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
```

One TU for both implementations (both headers are static-function amalgamations;
`STBI_MAX_DIMENSIONS` affects only implementation code, so `document_io.cpp` — which
includes the headers for declarations only — does not need the macro). `config.hpp`
gains `inline constexpr int kMaxImageDimension = 16384;`.

### 2. `CanvasDocument` additions

```cpp
// src/core/canvas_document.hpp  (public, after flush())
// Replaces the whole document: size = pixels.size(), layer stack reset to a single
// "Background" layer holding `pixels`. Used by Open and New. Marks the full new
// bounds dirty and discards any pending dirty rect.
void replace_contents(PixelBuffer pixels);

// Resizes the canvas, top-left anchored: existing content in the overlap is kept,
// newly exposed area is filled opaque white (matches the ctor's Background fill and
// mspaint's canvas growth). Resizes EVERY layer and m_flattened together — required
// because compositor.cpp reads layer.pixels at target coordinates (m0 Risks).
// No-op when dims are unchanged. Marks full bounds dirty.
void set_size(int width, int height);
```

`.cpp` sketch:

```cpp
void CanvasDocument::replace_contents(PixelBuffer pixels) {
    m_flattened = PixelBuffer(pixels.width(), pixels.height());
    m_layers = LayerStack{};                       // LayerStack is copy/move-assignable
    m_layers.add({"Background", std::move(pixels)});
    m_dirty = {};
    mark_dirty(bounds());
}

void CanvasDocument::set_size(int width, int height) {
    if (width == this->width() && height == this->height()) return;
    for (std::size_t i = 0; i < m_layers.layers().size(); ++i) {
        PixelBuffer resized(width, height, Rgba8{255, 255, 255, 255});
        resized.blit(0, 0, m_layers.at(i).pixels); // blit clips the overlap (pixel_buffer.cpp:18)
        m_layers.at(i).pixels = std::move(resized);
    }
    m_flattened = PixelBuffer(width, height);
    m_dirty = {};
    mark_dirty(bounds());
}
```

WHY these two and not more: Open/New need a full replace (`replace_contents`);
paste-growth needs a size change that preserves content (`set_size`). M2's future
`CanvasResizeCommand` should be built *on* `set_size` rather than duplicating it —
see Milestone dependencies.

### 3. Dirty-flag plumbing (unsaved-changes state)

Source of truth = `HistoryStack` (every document mutation already funnels through
`push`, and undo/redo are the only other mutators):

```cpp
// src/history/history_stack.hpp
[[nodiscard]] bool is_modified() const noexcept { return m_modified; }
void mark_clean() noexcept { m_modified = false; }
void clear();                       // empties both stacks, m_modified = false

private:
    bool m_modified = false;
```

- `push()` sets `m_modified = true` (last line before eviction check).
- `undo()`/`redo()` set `m_modified = true` only when the respective stack is
  non-empty (conservative: an undo that returns exactly to the saved state still
  reports modified — over-prompting is safe, under-prompting is not).
- `clear()` is called by `new_document()`/`open()` (which also call nothing else —
  clear already marks clean); `save()` calls `mark_clean()`.

UI side (M1's `refresh()` write block, i.e. before the `flush()` early-return):

```cpp
m_window.global<DocumentState>().set_dirty(m_controller.history().is_modified());
m_window.global<DocumentState>().set_title(m_controller.display_name());
```

Window title binding in `appwindow.slint`:

```slint
title: DocumentState.title + (DocumentState.dirty ? " *" : "") + " - LS Paint";
```

WHY HistoryStack and not a bool on CanvasDocument: `CanvasDocument` already uses
"dirty" for dirty-*rects*; overloading it would conflate two meanings, and undo/redo
would need callbacks to keep a separate flag in sync. One flag on the object that
owns mutation entry points covers every path (stroke, fill, cut, paste) for free.

### 4. `src/io/clipboard.hpp/.cpp` — clip wrapper

```cpp
#pragma once
#include <optional>
#include "core/pixel_buffer.hpp"

namespace lsp {

class Clipboard {
public:
    // RGBA8 (straight alpha) -> system clipboard. false if the clipboard could not
    // be locked / image format unsupported.
    [[nodiscard]] static bool set_image(const PixelBuffer& image);
    // system clipboard -> RGBA8; nullopt when empty or contains no image.
    [[nodiscard]] static std::optional<PixelBuffer> get_image();
};

}  // namespace lsp
```

Implementation (single `.cpp`, `#include <clip.h>`):

- **RGBA spec** (little-endian `Rgba8{r,g,b,a}` in memory = one LE `uint32`):
  `bits_per_pixel: 32`, `bytes_per_row: width * 4`,
  `red_mask 0x000000ff / shift 0`, `green 0x0000ff00 / 8`,
  `blue 0x00ff0000 / 16`, `alpha 0xff000000 / 24`.
- `set_image`: build `clip::image img(pixels.row(0).data(), spec)` (the
  non-owning ctor; `clip::set_image` serializes synchronously — X11 writes PNG, Win
  copies), return `clip::set_image(img)`.
- `get_image`: `clip::image img; if (!clip::get_image(img)) return std::nullopt;`
  then convert per-pixel via the returned `image_spec`'s masks/shifts **using
  `spec.bytes_per_row` for row stride** (it may exceed `width*4`), and
  `if (spec.alpha_mask == 0) a = 255`. clip guarantees straight (non-premultiplied)
  alpha on the way out (`clip.h` comment) and converts Windows' premultiplied data
  itself, so no premultiply handling is needed here.

**clip acquisition — DECISION: vendored copy from `image-viewer.zip`** (plan.md §4:
"third_party/clip (already vendored in image-viewer) gets reused as-is"). FetchContent
of `github.com/dhcapello/clip` would re-download an uncontrolled revision and could
drift from the copy image-viewer was validated against; the zip is already in the
repo, so the build stays reproducible. Exact vendoring steps:

```bash
cd /home/bowserinator/programming-projects/ls-paint
mkdir -p third_party
unzip image-viewer.zip "third_party/clip/*" -d /tmp/clip-extract
cp -r /tmp/clip-extract/third_party/clip third_party/clip
rm -rf third_party/clip/.git third_party/clip/CMakeFiles third_party/clip/.github \
       third_party/clip/examples third_party/clip/tests \
       third_party/clip/Makefile third_party/clip/cmake_install.cmake
```

Keep: `clip.cpp clip.h clip_base.h clip_common.h clip_lock_impl.h clip_none.cpp
clip_osx.h clip_osx.mm clip_win*.h clip_win*.cpp clip_x11.cpp clip_x11_png.h
image.cpp CMakeLists.txt clip-config.cmake.in LICENSE.txt README.md
CONTRIBUTING.md`. CMake side (see Build changes):

```cmake
set(CLIP_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(CLIP_TESTS     OFF CACHE BOOL "" FORCE)
set(CLIP_INSTALL   OFF CACHE BOOL "" FORCE)
set(CLIP_ENABLE_IMAGE ON CACHE BOOL "" FORCE)   # default on; pinned explicitly
add_subdirectory(third_party/clip)
...
target_link_libraries(ls_paint PRIVATE Slint::Slint clip)
```

`CLIP_ENABLE_IMAGE=1` is a PUBLIC compile definition on `clip`, so `clip.h` exposes
the image API to `src/io/clipboard.cpp` automatically; the include dir comes from
clip's own `INTERFACE` include. On Linux clip links `xcb pthread png` transitively.
**Build prerequisite**: `project(ls_paint LANGUAGES C CXX)` — clip's
`check_include_files(xcb/xcb.h …)` / `check_include_files(png.h …)` need the C
compiler (image-viewer does the same via `enable_language(C)`). System has
`xcb/xcb.h`, `png.h`, `pkg-config xcb` (verified), so both probes pass; without
libpng, image copy/paste silently degrades on X11 (see Risks).

### 5. Cut / Copy / Paste semantics

**Copy** — region = whole canvas baseline: `RectI source = m_document.bounds()`.
M2 hook (single function, marked in code):

```cpp
// AppController private
[[nodiscard]] RectI copy_source_rect() const;   // TODAY: return m_document.bounds();
// after M2: return m_document.selection().bounds() when a selection exists, else full bounds.
```

`copy()`: `m_document.flush()` (flattened must be current) →
`Clipboard::set_image(m_document.flattened().copy_region(copy_source_rect()))`.

**Cut** = copy + erase, reusing the M1 stroke machinery (so it is *one* undo entry
exactly like a fill):

```cpp
IoResult AppController::cut() {
    if (IoResult r = copy(); !r.ok) return r;          // clipboard first: no doc change on failure
    StrokeRecorder recorder(m_document);               // snapshots active layer
    const RectI region = copy_source_rect();
    PixelBuffer& pixels = m_document.active_layer().pixels;
    for (int y = region.y; y < region.bottom(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            pixels.at(x, y) = m_colors.secondary;      // background color, like EraserTool
    recorder.touch(region);
    recorder.commit(m_history);                        // PaintStrokeCommand before/after
    return IoResult::success();
}
```

WHY Color 2 and not transparent: `EraserTool::stroke_color` already paints
`context.colors.secondary` (M1/mspaint eraser semantics); cut behaves like a
rectangular eraser, so one mental model covers both. (Transparent-fill is a
two-line change if alpha-preserving cut is ever wanted — see Risks.)

**Paste — DECISION: rasterize-immediately (not a floating selection).**
mspaint leaves a floating selection you commit by clicking elsewhere; implementing
that requires M2's `SelectionMask` + selection-move tool + commit-on-outside-click,
i.e. pulling the whole selection milestone into M4 for a purely cosmetic intermediate
state. Rasterize-immediately needs one history command, works before/without M2, and
upgrading later is localized: `paste_pixels()` would hand the buffer to the floating
selection instead of blitting — the clipboard wrapper, the dialog, and the command
stay as-is. Paste position is **(0,0)** (top-left, consistent with the required
top-left document alignment; centering is a one-line follow-up).

```cpp
IoResult AppController::paste() {
    std::optional<PixelBuffer> image = Clipboard::get_image();
    if (!image) return IoResult::failure("Clipboard does not contain an image");
    return paste_pixels(std::move(*image));
}
IoResult AppController::paste_from_file(const std::filesystem::path& path) {
    LoadResult loaded = load(path);
    if (!loaded.ok) return IoResult::failure(loaded.error);
    return paste_pixels(std::move(loaded.pixels));
}
// private
IoResult AppController::paste_pixels(PixelBuffer image);
```

`paste_pixels(image)` (mspaint growth rule: grow only where the paste is bigger,
top-left anchored):

1. reject `image.width()/height() <= 0`;
2. snapshot `before = {m_document.width(), m_document.height(),
   m_document.active_layer().pixels}` and `before_index = active_index()`;
3. `set_size(max(old_w, w), max(old_h, h))` (skipped internally when unchanged);
4. `m_document.active_layer().pixels.blit(0, 0, image);`
5. `m_document.mark_dirty(m_document.bounds());`
6. snapshot `after = {new_w, new_h, active_layer().pixels}`;
7. `m_history.push(std::make_unique<ClipboardPasteCommand>(…))` → modified flag flips;
8. return success.

### 6. History: `ClipboardPasteCommand`

`PaintStrokeCommand`/`StrokeRecorder` cannot express paste: the canvas *size* changes,
and both store a fixed `RectI` region (`stroke_recorder.cpp:17-23`). Cut/copy pixel
changes stay on `StrokeRecorder`; full-document replace gets its own command:

```cpp
// src/history/commands/clipboard_paste_command.hpp
#pragma once
#include "core/pixel_buffer.hpp"
#include "history/command.hpp"

namespace lsp {

class ClipboardPasteCommand final : public ICommand {
public:
    ClipboardPasteCommand(std::size_t layer_index,
                          int before_w, int before_h, PixelBuffer before,
                          int after_w, int after_h, PixelBuffer after)
        : m_layer_index(layer_index),
          m_before_w(before_w), m_before_h(before_h), m_before(std::move(before)),
          m_after_w(after_w), m_after_h(after_h), m_after(std::move(after)) {}

    void undo(CanvasDocument& document) override { apply(document, m_before_w, m_before_h, m_before); }
    void redo(CanvasDocument& document) override { apply(document, m_after_w, m_after_h, m_after); }

private:
    void apply(CanvasDocument& document, int w, int h, const PixelBuffer& pixels) const;
    std::size_t m_layer_index;
    int m_before_w, m_before_h; PixelBuffer m_before;
    int m_after_w,   m_after_h;   PixelBuffer m_after;
};

}  // namespace lsp
```

`apply()`: `document.set_size(w, h);` → `document.layers().at(m_layer_index).pixels = pixels;`
→ `document.mark_dirty(document.bounds());` (set_size makes dims match the snapshot;
the assignment restores content exactly, so undo works even though `set_size`'s
keep-overlap rule would otherwise lose shrunken-away data). Full-document snapshots
are the plan-sanctioned choice for infrequent canvas-level ops (plan.md §5.5); cost is
2 × W×H×4 bytes per paste, bounded by `kMaxHistoryEntries = 200`.

### 7. `AppController` IO surface

```cpp
// src/app/app_controller.hpp
#include <filesystem>
#include "io/document_io.hpp"

struct IoResult {
    bool ok = false;
    std::string error;                     // non-empty iff !ok
    [[nodiscard]] static IoResult success() noexcept { return {true, {}}; }
    [[nodiscard]] static IoResult failure(std::string message) { return {false, std::move(message)}; }
};

class AppController {
public:
    ...
    // File (each returns a result the UI turns into refresh + dialog/error)
    IoResult new_document();                                       // default 800x600 white
    IoResult open(const std::filesystem::path& path);
    IoResult save(const std::filesystem::path& path, ImageFormat format);
    IoResult save();                                               // uses current path + its extension
    [[nodiscard]] bool has_current_path() const noexcept { return !m_current_path.empty(); }
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept { return m_current_path; }
    [[nodiscard]] std::string display_name() const;                // filename or "untitled"
    [[nodiscard]] std::filesystem::path default_directory() const; // current file's dir or cwd

    // File-dialog browsing (pure filesystem, no Slint)
    [[nodiscard]] std::vector<std::filesystem::path>
        list_directory(const std::filesystem::path& dir, bool images_only) const;

    // Clipboard
    IoResult cut();
    IoResult copy();
    IoResult paste();
    IoResult paste_from_file(const std::filesystem::path& path);

private:
    IoResult paste_pixels(PixelBuffer image);
    [[nodiscard]] RectI copy_source_rect() const;
    std::filesystem::path m_current_path;   // declared last: no ctor-init ordering impact
};
```

Bodies (`.cpp` includes: `config.hpp`, `io/clipboard.hpp`,
`tools/stroke_recorder.hpp`, `history/commands/clipboard_paste_command.hpp`):

- `new_document()` → `replace_contents(PixelBuffer(kDefaultCanvasWidth,
  kDefaultCanvasHeight, {255,255,255,255}))`, `m_history.clear()`, clear path.
- `open()` → `fs::is_directory` guard, `load()`, on success
  `replace_contents(move(pixels))`, `m_history.clear()`, `m_current_path = path`.
- `save(path, fmt)` → `m_document.flush()`, free function `lsp::save(m_document,
  path, fmt, &err)` (qualify — inside a member named `save` unqualified lookup would
  hide the namespace function), success → set `m_current_path`, `m_history.mark_clean()`.
- `save()` → no path ⇒ `failure("No file name — use Save As")`; extension unknown ⇒
  `failure("Unknown file extension — use Save As")` (the UI pre-checks the same thing
  and opens the dialog instead — belt and braces).
- `list_directory(dir, images_only)`: entries = `".."` (when `dir` is not the
  filesystem root) + directories + files, each group sorted by filename;
  `images_only` keeps files whose extension is in `kImageExtensions`
  (`config.hpp`: `inline constexpr std::array<std::string_view,4> kImageExtensions
  {".png",".bmp",".jpg",".jpeg"}`); skip entries whose `is_directory(ec)`/`is_regular_file(ec)`
  errors; `".."` entry is `dir / ".."` (canonicalized later by the caller).
- `default_directory()`: `current_path.empty() ? fs::current_path() :
  current_path.parent_path()`.

WHY the controller owns IO and not `UiState`: plan.md §3 puts `DocumentIO/Clipboard`
under `AppController`; `UiState` stays a translator (Slint globals ↔ controller calls)
so every flow (button, shortcut, close-requested) shares one code path and the IO layer
remains Slint-free and testable.

### 8. File dialogs — DECISION: custom Slint modal overlay

**Decision**: a hand-written `ui/dialogs/file-dialog.slint` — centered card over a
full-window dimmed backdrop, with a path `LineEdit`, a directory `ListView`, an
Up button, and a filter/format `ComboBox` — driven by a new `DialogState` global.

Rejected alternatives, explicitly:

- **rfd (Rust) via Cargo**: not viable here. Slint's C++ FetchContent build compiles
  Slint's own Rust workspace through Corrosion; there is no extension point to add an
  extra crate to that static library, and bridging a separate Rust staticlib into the
  C++ binary means a second Cargo project, Corrosion wiring, and a Rust toolchain
  requirement — far beyond "trivially available".
- **`portable-file-dialogs`** (vendored in `image-viewer.zip`, used at
  `src/ui/app.cpp:171`): the one genuinely cheap native option, but on Linux it
  spawns `zenity`/`kdialog` (a runtime dependency that may be absent — pfd then
  throws), and the calling thread blocks on the subprocess, freezing Slint's event
  loop for the dialog's lifetime. It also creates a *second* modal system next to the
  custom confirm dialog M4 must build anyway (and next to M2's resize/rotate dialogs).
  Rejected for M4; swapping it in later is one function
  (`native_file_dialog()`) if native pickers are ever demanded.
- **`PopupWindow`** (About-dialog pattern): can't cover the whole window, closes on
  outside clicks per `close-policy`, and has no modal semantics. **std-widgets
  `Dialog`**: only offers fixed button kinds and Fluent chrome, but our dialogs need
  a directory list and app-specific fields; a plain overlay `Rectangle` gives full
  layout control with zero new machinery, and `ui/dialogs/` is exactly where plan.md §6
  already expects dialogs to live.

Modal-blocking: the overlay root contains a full-size `TouchArea {}` declared first,
so it swallows every click that would otherwise reach the canvas `TouchArea`
underneath (dialogs are declared as the **last** children of `AppWindow`, after
`focus-scope`, so they paint and hit-test above the ribbon — verified visually in the
checklist).

### 9. Close-confirmation (API verified)

Registered once in the `UiState` constructor (the only layer allowed to touch
`AppWindow`):

```cpp
m_window.window().on_close_requested([this]() -> slint::CloseRequestResponse {
    auto& dialog = m_window.global<DialogState>();
    if (dialog.get_file_visible()) {          // a modal file dialog is open: cancel it first
        dialog.set_file_visible(false);
        m_pending = PendingAction::None;
    }
    if (!m_controller.history().is_modified())
        return slint::CloseRequestResponse::HideWindow;
    m_pending = PendingAction::Exit;
    dialog.set_confirm_message("Save changes to " + slint::SharedString(m_controller.display_name()) + "?");
    dialog.set_confirm_visible(true);
    return slint::CloseRequestResponse::KeepWindowShown;
});
```

The **Exit** button does *not* implement its own flow — its callback calls
`m_window.window().dispatch_close_requested_event()`, so WM-close and Exit share one
code path (`slint_window.h:694`: dispatches CloseRequested → runs the callback above →
hides only if it returns `HideWindow`).

### 10. Pending-action state machine (UiState)

```cpp
enum class PendingAction { None, Exit, New, Open };   // member of UiState
PendingAction m_pending = PendingAction::None;
```

- `request_action(PendingAction a)`: if `!m_controller.history().is_modified()` →
  `run_pending(a)` immediately; else set `m_pending = a`, show the confirm dialog.
- `run_pending(PendingAction a)` (also clears `m_pending`):
  - `Exit` → `m_window.hide()`;
  - `New` → `apply(m_controller.new_document())`;
  - `Open` → `open_file_dialog(EFileDialogMode::OPEN)`;
  - `None` → nothing.
- confirm callbacks: `confirm-save` → hide confirm, run the save flow (save() if a
  usable path exists, else `open_file_dialog(SAVE)`), and **only on success** call
  `run_pending(m_pending)`; on save failure show the message dialog and drop
  `m_pending` (never exit/new/open after a failed save); `confirm-discard` →
  `run_pending(m_pending)`; `confirm-cancel` → `m_pending = None`, hide confirm.
- file-dialog `file-ok` in SAVE mode, after a successful save → `run_pending(m_pending)`
  (covers both plain Save As — `None`, no-op — and save-then-exit/new/open).
- file-dialog `file-cancel` → hide dialog, `m_pending = None` (cancelling the file
  picker aborts the whole pending operation).

`modal_open()` helper: `DialogState.file-visible || confirm-visible || message-visible`.
Every shortcut handler and every action callback early-returns when `modal_open()`
(or, per M1's handoff, when `m_controller.input().active()` mid-stroke).

### 11. Shortcuts

`src/ui/shortcuts.hpp` — enum + the recipe comment M1 installs:

```cpp
// M4 rows (Ctrl+C/X/V were handed over by M1):
enum class ShortcutAction { Undo, Redo, New, Open, Save, SaveAs, Cut, Copy, Paste };
```

`src/ui/shortcuts.cpp` — append to `kShortcuts` (existing pattern, no
`lookup_shortcut` change):

```cpp
Shortcut{'n', true, false, ShortcutAction::New},
Shortcut{'o', true, false, ShortcutAction::Open},
Shortcut{'s', true, false, ShortcutAction::Save},
Shortcut{'s', true, true,  ShortcutAction::SaveAs},
Shortcut{'x', true, false, ShortcutAction::Cut},
Shortcut{'c', true, false, ShortcutAction::Copy},
Shortcut{'v', true, false, ShortcutAction::Paste},
```

`UiState::handle_shortcut` (M1's switch) gains one case per action, e.g.
`case ShortcutAction::New: request_action(PendingAction::New); break;`,
`case ShortcutAction::Save: save_shortcut(); break;`,
`case ShortcutAction::Cut: do_cut(); break;` — `do_cut/do_copy/do_paste` are the same
private methods the Home-bar buttons call. `save_shortcut()` = "save if the current
path has a known extension, otherwise open the Save As dialog". Control characters
already normalize (`shortcuts.cpp:29-31` maps `'\x13'` → `'s'`), so Ctrl+Shift+S
resolves via the `shift` flag.

## File-by-file changes

### New files

- **`src/io/document_io.hpp` / `.cpp`** — API of Design §1: `ImageFormat`, `LoadResult`,
  `load`, `save`, `format_from_extension`, `extension_for`, `format_from_index`,
  `index_from`, `flatten_to_opaque`. Slint-free; includes `stb_image.h` /
  `stb_image_write.h` for declarations only.
- **`src/io/stb_impl.cpp`** — the single `STB_IMAGE_IMPLEMENTATION` +
  `STB_IMAGE_WRITE_IMPLEMENTATION` TU (exact contents in Design §1).
- **`src/io/clipboard.hpp` / `.cpp`** — `class Clipboard { static set_image/get_image }`
  wrapping vendored `clip` with the 32-bit RGBA `image_spec` (Design §4).
- **`src/history/commands/clipboard_paste_command.hpp` / `.cpp`** — command of Design
  §6; `apply()` in the `.cpp` (needs `core/canvas_document.hpp`).
- **`ui/bar/bar-file.slint`** — `export component FileBar inherits Bar` with one
  `BarItemGroup { title: "File"; }` containing five new text-only `BarButton`s:

  ```slint
  import { Bar, BarItemGroup } from "bar.slint";
  import { BarButton } from "../components/image-buttons.slint";
  import { UserState } from "../globals.slint";

  export component FileBar inherits Bar {
      HorizontalLayout {
          BarItemGroup {
              title: "File";
              HorizontalLayout {
                  BarButton { text: "New";     clicked => { UserState.file-new(); } }
                  BarButton { text: "Open";    clicked => { UserState.file-open(); } }
                  BarButton { text: "Save";    clicked => { UserState.file-save(); } }
                  BarButton { text: "Save As"; clicked => { UserState.file-save-as(); } }
                  BarButton { text: "Exit";    clicked => { UserState.file-exit(); } }
              }
          }
      }
  }
  ```

  (No icons exist for these actions — see Status — hence text buttons.)
- **`ui/dialogs/file-dialog.slint`** — `export component FileDialog inherits Rectangle`:
  full-window root (`width/height: 100%`), backdrop `TouchArea {}`, centered white
  card (560×460, About-dialog drop-shadow styling) containing, in order: title `Text`
  (bound to `DialogState.file-mode`: "Open" / "Save As" / "Paste From File"); a
  directory caption + `Button "Up"` (`DialogState.file-up()`); `ListView` of
  `for label[i] in DialogState.file-dir-labels` rows (`height: 26px`, hover highlight,
  `clicked => DialogState.file-navigate(i)`); a horizontal row with either
  `ComboBox` filter (OPEN/PASTE modes: `["Images (*.png;*.bmp;*.jpg;*.jpeg)", "All files (*)"]`,
  `current-index <=> DialogState.file-filter`) or format `ComboBox` (SAVE mode:
  `["PNG (*.png)", "BMP (*.bmp)", "JPEG (*.jpg)"]`, `current-index <=> DialogState.file-format`)
  plus `LineEdit { text <=> DialogState.file-path; accepted(text) => DialogState.file-ok(); }`;
  an error `Text` shown when `DialogState.file-has-error`; and Cancel / OK
  (`primary: true`, label "Open"/"Save") buttons calling `file-cancel()` / `file-ok()`.
  Root `init => { path-input.focus(); }` (give the LineEdit that id).
- **`ui/dialogs/confirm-dialog.slint`** — `ConfirmDialog`: same backdrop + centered card
  (420×150), `Text { text: DialogState.confirm-message; wrap: word-wrap; }`, three
  buttons `Save` (`confirm-save()`), `Don't Save` (`confirm-discard()`), `Cancel`
  (`confirm-cancel()`).
- **`ui/dialogs/message-dialog.slint`** — `MessageDialog`: backdrop + card with
  `DialogState.message-title` / `message-text` and a single OK button →
  `message-ok()`. Used for every error raised outside the file dialog (clipboard
  failures, save failures, shortcut-triggered failures).
- **`third_party/clip/**`** — vendored per Design §4 (steps + keep-list above).

### Modified files

- **`CMakeLists.txt`** — see Build changes (C language, stb FetchContent, clip
  subdirectory, 4 new sources, `SYSTEM` stb include, `clip` link, test-target additions).
- **`src/config.hpp`** — add `kMaxImageDimension = 16384`, `kJpegQuality = 90`,
  `kImageExtensions`.
- **`src/core/canvas_document.hpp` / `.cpp`** — add `replace_contents(PixelBuffer)`
  and `set_size(int, int)` (Design §2).
- **`src/history/history_stack.hpp` / `.cpp`** — add `m_modified`,
  `is_modified()`, `mark_clean()`, `clear()`; set `m_modified = true` in `push()` and
  (when the stack is non-empty) in `undo()`/`redo()`.
- **`src/render/slint_bridge.hpp` / `.cpp`** — add

  ```cpp
  void resize(int width, int height);   // no-op if same; else m_pixels = SharedPixelBuffer<Rgba8Pixel>(w, h)
  [[nodiscard]] int width() const noexcept  { return static_cast<int>(m_pixels.width()); }
  [[nodiscard]] int height() const noexcept { return static_cast<int>(m_pixels.height()); }
  ```

  (`SharedPixelBuffer` is copy/move-assignable — plain struct with `std::vector`.)
- **`src/app/app_controller.hpp` / `.cpp`** — `IoResult`; all methods of Design §7;
  new members `m_current_path` (last) ; includes `io/document_io.hpp`,
  `io/clipboard.hpp`, `tools/stroke_recorder.hpp`,
  `history/commands/clipboard_paste_command.hpp`.
- **`src/ui/shortcuts.hpp` / `.cpp`** — enum values + `kShortcuts` rows (Design §11).
- **`src/ui/ui_state.hpp`** — add:

  ```cpp
  enum class PendingAction { None, Exit, New, Open };
  void handle_shortcut(ShortcutAction action);        // exists after M1; extend
  void request_action(PendingAction action);
  void run_pending(PendingAction action);
  void save_shortcut();
  void open_file_dialog(EFileDialogMode mode);
  void populate_directory(const std::filesystem::path& dir);
  void do_cut(); void do_copy(); void do_paste();
  void apply_result(const IoResult& result, const char* context);  // error -> MessageDialog
  void show_error(const std::string& title, const std::string& message);
  [[nodiscard]] bool modal_open() const;
  PendingAction m_pending = PendingAction::None;
  std::filesystem::path m_current_dir;
  std::vector<std::filesystem::path> m_dir_entries;   // index-aligned with the labels model
  std::shared_ptr<slint::VectorModel<slint::SharedString>> m_dir_labels;
  ```

- **`src/ui/ui_state.cpp`** — the bulk of the wiring:
  - ctor: create `m_dir_labels`, `m_window.global<DialogState>().set_file_dir_labels(m_dir_labels)`;
    register **all** new callbacks (list in Slint specifics); register
    `on_close_requested` (Design §9); initial `set_title("untitled")` happens via the
    first `refresh()`.
  - `refresh()` — M1's structure, extended (order matters, all before the `flush()`
    early-return):

    ```cpp
    void UiState::refresh() {
        CanvasDocument& document = m_controller.document();
        if (m_bridge.width() != document.width() || m_bridge.height() != document.height())
            m_bridge.resize(document.width(), document.height());   // open/new/paste growth
        auto& ds = m_window.global<DocumentState>();
        if (ds.get_canvas_width() != document.width())  ds.set_canvas_width(document.width());
        if (ds.get_canvas_height() != document.height()) ds.set_canvas_height(document.height());
        ds.set_can_undo(m_controller.history().can_undo());     // M1
        ds.set_can_redo(m_controller.history().can_redo());     // M1
        ds.set_dirty(m_controller.history().is_modified());     // M4
        ds.set_title(slint::SharedString(m_controller.display_name()));
        const auto region = document.flush();
        if (!region) return;
        m_bridge.upload(document.flattened(), *region);
        m_window.set_texture(m_bridge.image());
    }
    ```

    (guarded sets avoid re-assigning unchanged properties every event; the resized
    bridge always uploads the full bounds because `replace_contents`/`set_size` mark
    the whole document dirty.)
  - `handle_shortcut`: `if (modal_open()) return;` (after M1's mid-stroke guard), then
    the new cases; `on_key_pressed` keeps calling it.
  - File-dialog handlers read `DialogState` (`get_file_path()`, `get_file_mode()`,
    `get_file_format()`, `get_file_filter()`), normalize with
    `std::filesystem::absolute`, and on success close the dialog + `refresh()`;
    on failure set `file-error`/`file-has-error` (stays in-dialog, Design §8).
- **`ui/globals.slint`** — new enum + globals + callbacks (exact block in Slint specifics).
- **`ui/topbar.slint`** — import `FileBar`, add the File tab, rename titles:

  ```slint
  import { FileBar } from "./bar/bar-file.slint";
  ...
  Tab { title: "File";  FileBar {} }
  Tab { title: "Home";  HomeBar {} }
  Tab { title: "View";  ViewBar {} }
  ```

- **`ui/components/image-buttons.slint`** — add text-only `BarButton` (reuses
  `ImageButtonBase` hover/press styling and the local `ButtonText`):

  ```slint
  export component BarButton inherits ImageButtonBase {
      in-out property text <=> txt.text;
      min-width: 78px;
      height: 46px;
      VerticalLayout {
          alignment: center;
          txt := ButtonText { text: root.text; horizontal-alignment: center; }
      }
  }
  ```

- **`ui/bar/bar-home.slint`** — wire the dead clipboard controls:
  - Paste dropdown items (lines 19-25): `clicked => { UserState.clipboard-paste(); }`
    and `clicked => { UserState.clipboard-paste-from-file(); }` (replacing the
    `ETool.NONE` assignments).
  - Paste main button (lines 30-41): `clicked => { UserState.clipboard-paste(); }`;
    drop the `focused1/focused2` bindings on `ETool.PASTE` (Paste is an action, not a
    tool — it can never be "focused"; `ETool::PASTE` stays in the enum, now vestigial).
  - Cut/Copy (lines 47-54): add `clicked => { UserState.clipboard-cut(); }` /
    `clicked => { UserState.clipboard-copy(); }`.
- **`ui/appwindow.slint`** —
  - `title: DocumentState.title + (DocumentState.dirty ? " *" : "") + " - LS Paint";`
    (replaces line 19's literal);
  - import `DialogState` + the three dialog components;
  - **after** the `focus-scope` (last children → painted/hit-tested on top):

    ```slint
    if DialogState.file-visible    : FileDialog {}
    if DialogState.confirm-visible : ConfirmDialog {}
    if DialogState.message-visible : MessageDialog {}   // last: can appear over the file dialog
    ```

## Implementation order

Each step ends in a compiling build.

1. **Build plumbing**: `CMakeLists.txt` → `LANGUAGES C CXX`, stb FetchContent +
   `SYSTEM` include, vendored `third_party/clip` (steps in Design §4) +
   `add_subdirectory` + `target_link_libraries(… clip)`. No new sources yet —
   configure + build to prove clip and stb fetch/compile.
2. **Core primitives**: `CanvasDocument::replace_contents`/`set_size`,
   `HistoryStack::is_modified/mark_clean/clear` + modified flag,
   `SlintBridge::resize/width/height`, `config.hpp` constants. Build.
3. **`src/io/stb_impl.cpp` + `src/io/document_io.*`** (+ CMake source entries). Build;
   optionally smoke-test with the round-trip command in Verification.
4. **`src/history/commands/clipboard_paste_command.*`** (+ CMake). Build.
5. **`src/io/clipboard.*`** (+ CMake). Build.
6. **`AppController`**: `IoResult` + all file/clipboard methods (+ CMake). Build.
7. **Shortcuts**: enum + `kShortcuts` rows + `handle_shortcut` cases (compiles against
   M1's dispatcher; `UiState` methods it calls are stubs if the UI layer isn't done —
   prefer doing step 8 first if a stub would be thrown away; together they are one
   compile unit). Build.
8. **Slint state**: `ui/globals.slint` (enum, `DialogState`, `DocumentState.dirty/title`,
   `UserState` callbacks) — build regenerates `appwindow.h`.
9. **`UiState`**: member/method declarations, callback registrations,
   `on_close_requested`, pending-action machine, `refresh()` extension. Build.
10. **Dialogs**: `ui/dialogs/{file-dialog,confirm-dialog,message-dialog}.slint` +
    `appwindow.slint` overlay + title binding. Build + visual check.
11. **File tab**: `BarButton` in `image-buttons.slint`, `ui/bar/bar-file.slint`,
    `topbar.slint` three tabs. Build + click each button.
12. **Home bar wiring**: cut/copy/paste buttons + dropdown. Build.
13. **Tests**: `tests/document_io_test.cpp` (append source + `run_document_io_tests()`
    to M1's `ls_paint_tests`/`test_main.cpp` registration) and CMake test-target
    additions. `ctest`.
14. **Full manual checklist** (Verification).

## Slint specifics

New enum + `DocumentState`/`UserState` additions in `ui/globals.slint`
(append; keep `UserState` as the home for action callbacks — it already hosts
`key-pressed`, and plan.md §6 puts document-ish state in `DocumentState`):

```slint
export enum EFileDialogMode { OPEN, SAVE, PASTE }

export global UserState {
    // ... existing ...
    callback file-new();
    callback file-open();
    callback file-save();
    callback file-save-as();
    callback file-exit();
    callback clipboard-cut();
    callback clipboard-copy();
    callback clipboard-paste();
    callback clipboard-paste-from-file();
}

export global DocumentState {
    in property <int> canvas-width;
    in property <int> canvas-height;
    in property <bool> can-undo;          // M1
    in property <bool> can-redo;          // M1
    in-out property <bool> dirty: false;              // M4, written by UiState::refresh
    in-out property <string> title: "untitled";       // M4, file name only; appwindow composes
}

export global DialogState {
    in-out property <bool> file-visible: false;
    in-out property <EFileDialogMode> file-mode: OPEN;
    in-out property <string> file-directory: "";      // caption, set by C++
    in-out property <string> file-path: "";           // LineEdit value (full path)
    in-out property <int> file-format: 0;             // 0 PNG / 1 BMP / 2 JPEG
    in-out property <int> file-filter: 0;             // 0 images / 1 all files
    in-out property <bool> file-has-error: false;
    in-out property <string> file-error: "";
    in-out property <bool> confirm-visible: false;
    in-out property <string> confirm-message: "";
    in-out property <bool> message-visible: false;
    in-out property <string> message-title: "";
    in-out property <string> message-text: "";

    callback file-ok();
    callback file-cancel();
    callback file-navigate(int);      // ListView row index
    callback file-up();
    callback confirm-save();
    callback confirm-discard();
    callback confirm-cancel();
    callback message-ok();
}
```

(`file-dir-labels` is **not** declared here as a literal — C++ sets it once from the
ctor via `set_file_dir_labels(std::shared_ptr<VectorModel<SharedString>>)`, same
mechanism as `UserState.color_palette`; declare
`in-out property <[string]> file-dir-labels;` in the global so the setter exists.)

C++ callback registrations in `UiState` (all
`m_window.global<…>().on_…([this](…){ … })`):
`UserState.on_file_new/file_open/file_save/file_save_as/file_exit`,
`UserState.on_clipboard_cut/clipboard_copy/clipboard_paste/clipboard_paste_from_file`,
`DialogState.on_file_ok/file_cancel/file_navigate/file_up`,
`DialogState.on_confirm_save/confirm_discard/confirm_cancel`,
`DialogState.on_message_ok`.

Dead buttons → wiring map:

| Control | Before | After |
|---|---|---|
| Home `Cut` (`bar-home.slint:47`) | no handler | `UserState.clipboard-cut()` |
| Home `Copy` (`:51`) | no handler | `UserState.clipboard-copy()` |
| Home `Paste` main (`:30-41`) | sets `ETool.PASTE` | `UserState.clipboard-paste()` |
| Home dropdown "Paste" (`:19`) | sets `ETool.NONE` | `UserState.clipboard-paste()` |
| Home dropdown "Paste from file" (`:23`) | sets `ETool.NONE` | `UserState.clipboard-paste-from-file()` |
| — | no File tab | `bar-file.slint` × 5 `BarButton`s → `file-*` callbacks |
| Window close (WM) | unconditional | `on_close_requested` → confirm overlay |
| Ctrl+N/O/S/Shift+S/C/X/V | unmapped | `kShortcuts` rows → `handle_shortcut` |

New `.slint` files are picked up transitively by `slint_target_sources(ls_paint
ui/appwindow.slint)` — **no CMake change is needed for Slint files** (appwindow →
topbar → bar-file; appwindow → dialogs → globals).

## Build changes

Exact `CMakeLists.txt` edits:

1. Line 2: `project(ls_paint LANGUAGES C CXX)` (C is required by clip's
   `check_include_files` probes on Linux).
2. After the Slint `FetchContent_MakeAvailable(Slint)` block (line 25):

   ```cmake
   FetchContent_Declare(
       stb
       GIT_REPOSITORY https://github.com/nothings/stb.git
       GIT_TAG        master
   )
   FetchContent_MakeAvailable(stb)

   set(CLIP_EXAMPLES OFF CACHE BOOL "" FORCE)
   set(CLIP_TESTS OFF CACHE BOOL "" FORCE)
   set(CLIP_INSTALL OFF CACHE BOOL "" FORCE)
   set(CLIP_ENABLE_IMAGE ON CACHE BOOL "" FORCE)
   add_subdirectory(third_party/clip)
   ```
3. Inside `add_executable(ls_paint …)` (alphabetical placement keeps the list tidy):

   ```cmake
       src/history/commands/clipboard_paste_command.cpp
       src/io/clipboard.cpp
       src/io/document_io.cpp
       src/io/stb_impl.cpp
   ```
4. After line 48:

   ```cmake
   target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/src)
   target_include_directories(ls_paint SYSTEM PRIVATE ${stb_SOURCE_DIR})
   ```
5. Line 49: `target_link_libraries(ls_paint PRIVATE Slint::Slint clip)`.
6. Inside M1's `if(LSP_BUILD_TESTS)` block: append to `ls_paint_tests`
   `src/history/commands/clipboard_paste_command.cpp`, `src/io/document_io.cpp`,
   `src/io/stb_impl.cpp`, plus `tests/document_io_test.cpp`; add
   `target_include_directories(ls_paint_tests SYSTEM PRIVATE ${stb_SOURCE_DIR})`.
   (No `clip` link for tests — clipboard code needs a live X server, keep it out of
   unit tests.)

## Verification

Build + tests:

```
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Both targets warning-clean under `-Wall -Wextra`.

Unit-testable pure logic — `tests/document_io_test.cpp`, `run_document_io_tests()`
(no window, no Slint; uses M1's harness style):

- `format_from_extension(".PNG") == Png`, `".jpg" == Jpeg`, `".xyz" -> nullopt`;
  `extension_for` / `format_from_index` / `index_from` round-trip for 0/1/2.
- PNG round-trip: `save()` a 4×4 buffer with varied alpha to a temp `.png` → `load()`
  → pixel-identical (alpha preserved). BMP/JPEG: loaded alpha is 255 everywhere and
  RGB matches within JPEG tolerance (use a flat-color image → exact for BMP).
- `load()` on a garbage file → `ok == false`, non-empty `error`, on a missing file →
  the "Not a file" error.
- `CanvasDocument`: `replace_contents` changes dims and resets to one `Background`
  layer; `set_size(1000, 500)` on 800×600 keeps the top-left 800×600 content, fills
  the new strip white, and `composite()` runs without OOB (all layers resized).
- `ClipboardPasteCommand`: build an 800×600 doc, apply a paste that grows it to
  900×700, `undo()` → dims **and** every pixel back to the original, `redo()` → paste
  result again; `HistoryStack::push` flips `is_modified`, `mark_clean` clears it,
  `clear()` empties both stacks.

Manual checklist (fresh launch each group):

1. Tabs read File/Home/View; File tab shows New/Open/Save/Save As/Exit; title is
   `untitled - LS Paint`.
2. Draw one pencil dab → title gains ` * `; Ctrl+S opens Save As (no path yet) → pick
   PNG, name `shot` (no extension) → file written as `shot.png`, `*` gone; Ctrl+S now
   saves silently; confirm mtime changes.
3. Ctrl+N → (dirty? prompt appears) → Don't Save → blank 800×600 white canvas,
   Ctrl+Z does nothing, title `untitled - LS Paint`.
4. Open `icons/paste.png` via the dialog: navigate into `icons/` by clicking rows,
   Up button works, filter switches between Images/All files, canvas becomes the PNG's
   size with content top-left; window title `paste.png - LS Paint`.
5. Open a non-image (e.g. `plan.md`) → error dialog; document/title/history unchanged.
6. Save As BMP then JPEG from a canvas containing a translucent region → open both in
   another viewer → no black/transparent fringe (white where alpha < 255).
7. Copy: Ctrl+C, then paste into another image app → identical pixels. Cut: Ctrl+X →
   region becomes Color 2 (set Color 2 to red first to see it); one Ctrl+Z restores it
   and the `*` reappears.
8. Paste a larger image (copy a screenshot from another app, Ctrl+V) → canvas grows to
   fit, paste sits at top-left; one Ctrl+Z restores original size **and** pixels;
   paste a smaller image → overlaid at (0,0), same single-undo behavior.
9. Paste with an empty clipboard → error dialog, no undo entry.
10. Paste from file → dialog in paste mode → pick `icons/cut.png` → appears at (0,0).
11. While dirty: click the title-bar close button → confirm dialog; Cancel stays;
    Don't Save exits; Save routes through Save As (no path) then exits. Exit button:
    identical behavior. While clean: close exits with no dialog.
12. Open the file dialog, press Ctrl+N/Ctrl+O/typing in the path field → no shortcut
    fires behind the modal; clicking the dimmed backdrop does not draw on the canvas.
13. `git status` shows changes only in the files this milestone owns.

## Risks & edge cases

- **Overlay stacking**: dialogs are the last children of `AppWindow`; Slint paints
  children in declaration order, so they must appear above the ribbon. If the visual
  check shows them behind, wrap the three `if` blocks in a trailing full-size
  `Rectangle` — same pattern, one-line fix (verification step 12 catches it).
- **libpng missing on a Linux target**: clip's CMake then compiles `clip_x11.cpp`
  without `HAVE_PNG_H` and X11 image copy/paste stops working (text formats still
  work). Detectable at configure time; document in the PR description — this machine
  has `png.h` + `libpng`.
- **X11 clipboard latency**: `clip::get_image` blocks up to `clip::get_x11_wait_timeout()`
  (default 1 s) waiting for the owner. Acceptable on the UI thread for now; if it
  hurts, lower the timeout or move paste on a worker later. In Wayland sessions clip
  falls back through XWayland (works, same caveats).
- **`ClipboardPasteCommand` snapshots only the active layer** (v1 has exactly one,
  plan.md §5.1). When M7 adds layers, extend it to snapshot the whole `LayerStack`
  — the command's shape (before/after size + buffers) does not change.
- **Cut fills with Color 2** (mspaint eraser/background semantics) rather than
  transparency; for documents with real alpha this paints an opaque rectangle.
  Flip to `Rgba8{0,0,0,0}` if alpha-preserving cut is preferred — isolated to the
  fill loop in `AppController::cut`.
- **Non-ASCII paths on Windows**: stb uses narrow `fopen`; `path.string()` on a
  UTF-8 path may fail to open. Documented; the Linux target is unaffected.
- **16-bit/channel PNGs** decode to 8-bit via stb (documented loss, same as mspaint);
  animated GIF → first frame (stb behavior).
- **`dirty` after undo-to-saved-state** stays true (conservative flag) — user gets one
  extra prompt, never a lost prompt.
- **`GIT_TAG master` for stb** is unpinned (matches image-viewer); pin the SHA after
  first configure if reproducibility is required.
- **`close-on-click` in `bar-home.slint:327`** (About dialog) is deprecated in 1.17;
  untouched here — only fix if the compiler warns while we are in that file.
- **Modal + WM-close**: while the file dialog is open, a WM close cancels the dialog
  and then runs the normal dirty check (Design §9) — deliberate; verify in step 11/12.
- **Resize handles / `DocumentState.canvas-x/y`** (cross-cutting requirement #2) do
  not exist yet; this milestone only keeps `canvas-width/height` truthful. When M2
  adds handles, `set_size` is the commit primitive behind `CanvasResizeCommand`.

## Milestone dependencies

- **Preconditions**: M0 (document/history/bridge as built) and M1 (StrokeRecorder +
  `PaintStrokeCommand`, `handle_shortcut`, `InputRouter::active()`, `refresh()` state
  write, `ls_paint_tests` target). This plan extends those exact seams rather than
  adding parallel ones.
- **M2 (Selection & transform)** — before M4 executes, M2 should land
  `CanvasResizeCommand` **on top of** `CanvasDocument::set_size` (if M2 has already
  introduced its own layer-resizing code, fold it into `set_size` — one primitive,
  used by both). Cut/copy's `copy_source_rect()` is the single hook where M2's
  selection bounds replace whole-canvas semantics (plan.md §5.4: "cut/copy reads only
  inside the mask"); the rest of M4 does not depend on selection. If M2/M3 land a
  modal convention in `ui/dialogs/` first, adapt the three new dialogs' chrome to it —
  the `DialogState` contract above stays as specified.
- **M5 (View & status)**: consumes `DocumentState.dirty`/`title` in the status bar and
  must re-anchor the texture when `DocumentState.canvas-x/y` + zoom arrive (M4 only
  guarantees width/height truthfulness and full-buffer re-upload after size changes).
- **Later milestones**: the overlay-rectangle modal pattern (file/confirm/message) is
  the template for M2's resize/rotate dialogs and M6's edit-colors dialog;
  `ShortcutAction` extension follows M1's documented 3-step recipe; `IoResult` +
  `show_error()` is how any future operation reports failure.
