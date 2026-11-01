# M0 — Foundation

## Status

**FULLY IMPLEMENTED.** This file is the **as-built reference document**, not a work plan.
Every type, file, and API described below was read from the working tree as it exists
today; the "File-by-file changes" and "Implementation order" sections record what M0
built and in what shape, so a later milestone subagent can extend the foundation without
re-reading the whole tree. Do not re-implement anything here.

- Core model: `src/core/types.hpp`, `pixel_buffer.{hpp,cpp}`, `layer.hpp`,
  `layer_stack.hpp`, `blend_modes.{hpp,cpp}`, `compositor.{hpp,cpp}`,
  `canvas_document.{hpp,cpp}` — all present and in use.
- History: `src/history/command.hpp`, `history_stack.{hpp,cpp}`,
  `commands/paint_stroke_command.{hpp,cpp}` — present, wired to Ctrl+Z/Ctrl+Y.
- Render: `src/render/slint_bridge.{hpp,cpp}` — present, dirty-rect (single merged rect)
  row-wise upload into `SharedPixelBuffer<Rgba8Pixel>`.
- App glue: `src/app/app_controller.{hpp,cpp}`, `input_router.{hpp,cpp}` — present;
  `src/main.cpp` is 11 lines (AppController + UiState + `window->run()`).
- Tools skeleton: `src/tools/tool.hpp` (ITool/ToolId/PointerButton),
  `tool_context.hpp` (ToolContext/ColorPair), `tool_registry.{hpp,cpp}`,
  `stroke_recorder.{hpp,cpp}`, `brush_stroke_tool.{hpp,cpp}` — present. Registry
  additionally registers M1 tools (Pencil/Eraser/Fill/Dropper).
- UI binding: `src/ui/ui_state.{hpp,cpp}`, `shortcuts.{hpp,cpp}` — present;
  `ui/globals.slint` has `UserState` pointer/key callbacks + `DocumentState`.
- Build: `CMakeLists.txt` lists all 19 `.cpp` files; no test target exists.

Known foundation gaps that later milestones must fill are listed at the end of
**Risks & edge cases** (§ "Foundation gaps") — read that list before planning M2/M5/M7.

## Acceptance criteria

M0's criteria, as satisfied by the current tree (all testable today):

1. `src/main.cpp` contains no editing logic — only `AppWindow::create()`,
   `lsp::AppController controller;`, `lsp::UiState ui(*window, controller);`,
   `window->run()`. (11 lines; verified by reading the file.)
2. Launching the app shows an 800×600 white "Background" canvas as a Slint `Image`
   (`AppWindow.texture`), top-left aligned in the gray `#c5cfdf` workspace (canvas sits
   at `x: 4px; y: 4px` inside a `TouchArea` anchored at the ScrollView origin), rendered
   1:1 with `image-rendering: pixelated`.
3. Pointer input flows Slint → C++ → tool with no per-tool special-casing:
   `TouchArea.pointer-event/moved` → `UserState.pointer-down/move/up` →
   `UiState` lambdas → `InputRouter::pointer_*` → `ToolRegistry::find(ToolId)` →
   `ITool::on_pointer_*`. Selecting a different `UserState.selected-tool` changes which
   object receives events (no hardcoded "draw a black pixel" path remains anywhere).
4. Compositing is dirty-rect based: a tool calls `CanvasDocument::mark_dirty(RectI)`;
   `CanvasDocument::flush()` returns one `std::optional<RectI>` and re-composites only
   that region; `SlintBridge::upload` `memcpy`s only those rows — a mouse-move never
   copies the whole canvas (verifiable by code inspection or a row-copy counter).
5. `Ctrl+Z` undoes and `Ctrl+Shift+Z` / `Ctrl+Y` redoes a stroke, restoring byte-exact
   pixels of the affected region; each stroke is exactly one `PaintStrokeCommand`
   (one command per `on_pointer_down`…`on_pointer_up`, not per mouse-move).
6. Undo/redo is bounded: `HistoryStack::push` drops the oldest entry past
   `kMaxHistoryEntries` (200) and clears the redo stack on a new push.
7. Layer blending goes through the `blend_fn_for(BlendMode)` table
   (Normal/Multiply/Screen implemented with correct alpha compositing); adding a mode is
   a new enum value + one function + one `switch` case, with no compositor change.
8. Unregistered tools are safe: `ToolRegistry::find` returns `nullptr` for
   `ToolId::Paste/Selection/Crop/Resize/Rotate/Text/Magnify`, and `InputRouter`
   dispatches nothing (no crash, no paint).
9. The project configures and builds warning-clean with
   `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
   under `-Wall -Wextra`.
10. No Slint header is included outside `src/ui/` and `src/render/` (grep
    `#include <slint.h>` → only `render/slint_bridge.hpp`; `appwindow.h` → only
    `main.cpp`, `ui/ui_state.*`).

Not part of M0 (deferred, see gaps): any user-visible new feature, zoom/scroll,
selection, file I/O, layers panel, tests.

## Design

### Ownership & data flow (as built)

```
ui/appwindow.slint  TouchArea/FocusScope
   │  UserState.pointer-down(x, y, secondary) / pointer-move / pointer-up /
   │  key-pressed(text, ctrl, shift)          [canvas px, 1:1, no transform]
   v
src/ui/ui_state.cpp   (only Slint-aware layer)
   │  copies Slint colors → ColorPair, ToolOptions.brush-size → Brush.size,
   │  ETool → ToolId (tool_from_slint), dispatches, then refresh()
   v
src/app/input_router.*   InputRouter{ const ToolRegistry&, ToolContext&, ITool* m_active }
   │  pointer_down(tool,pos,btn) → registry.find(tool) → on_pointer_down
   v
src/tools/*   ITool mutates CanvasDocument active layer within a RectI
   │  mark_dirty(region);  StrokeRecorder::commit → HistoryStack::push(PaintStrokeCommand)
   v
src/ui/ui_state.cpp refresh()
   document.flush() → optional<RectI> → composite(layers, region, m_flattened)
   → SlintBridge::upload(flattened, region) → window.set_texture(bridge.image())
```

Key non-obvious choices and WHY:

- **`flush()` returns the dirty rect and clears it** — the compositor result and the
  upload are driven by one call, so `UiState::refresh()` can no-op cheaply when nothing
  changed (`std::nullopt`). Dirty tracking lives in `CanvasDocument`, not in tools, so
  undo/redo and future canvas ops reuse the same path (every mutation only has to
  `mark_dirty`).
- **Two buffers: per-layer `PixelBuffer` + a flattened `m_flattened`.** Tools write
  layers; Slint only ever sees `m_flattened`. This is what makes a future layers panel
  additive (plan §5.1) — the render path never learns about layers.
- **`ICommand::redo(CanvasDocument&)` re-executes rather than a stored "do" pass.**
  Commands are pushed *after* the mutation already happened (stroke tool paints, then
  records before/after), so there is no separate execute-at-push; `redo` applies the
  stored after-image. Consequently `ICommand` has no `try_merge` — merging is done
  structurally by `StrokeRecorder` (one snapshot pair per stroke).
- **`HistoryStack` does not know about Slint.** It only mutates the document (commands
  call `mark_dirty` themselves); the UI refresh is triggered by `UiState` after every
  shortcut. Keeps `src/history/` Slint-free and unit-testable (plan §9).
- **Single merged dirty rect (`RectI m_dirty`), not a list.** `united()` of all
  `mark_dirty` calls; simplest correct scheme. Cost: two disjoint dirty areas coalesce
  into their bounding box. Recorded as a gap (plan §7 says "dirty rects" plural).
- **`SlintBridge` row-wise `memcpy` with `static_assert(sizeof(Rgba8) ==
  sizeof(slint::Rgba8Pixel))`** — buffer layouts are identical, so upload is a straight
  row copy of the dirty region only; no per-pixel conversion on the hot path.
- **`ToolContext::brush` is `const Brush&`** — tools read the brush; the UI writes it
  (`m_controller.brush().size = …` in `ui_state.cpp`). Avoids tools mutating global tool
  options behind the UI's back.
- **`InputRouter` keeps a raw `ITool* m_active`** set on pointer-down, cleared on
  pointer-up; nested/re-entrant pointer-down while active is ignored. Registry owns all
  tools (`unique_ptr`), so the pointer stays valid for the controller's lifetime.

### Core APIs as built (signatures)

```cpp
// src/core/types.hpp
struct Rgba8 { std::uint8_t r=0, g=0, b=0, a=255; friend bool operator==(...) = default; };
struct Point2f { float x = 0, y = 0; };
struct RectI { int x=0, y=0, width=0, height=0;
    [[nodiscard]] bool empty() const noexcept;          // width<=0 || height<=0
    [[nodiscard]] int right() const noexcept;           // x + width
    [[nodiscard]] int bottom() const noexcept;          // y + height
    [[nodiscard]] RectI united(const RectI&) const noexcept;
    [[nodiscard]] RectI intersected(const RectI&) const noexcept;  // empty RectI if disjoint
};

// src/core/pixel_buffer.hpp
class PixelBuffer {
public:
    PixelBuffer();
    PixelBuffer(int width, int height, Rgba8 fill = {0, 0, 0, 0});
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] RectI bounds() const noexcept;                 // {0,0,w,h}
    [[nodiscard]] bool contains(int x, int y) const noexcept;
    [[nodiscard]] std::span<Rgba8>       row(int y);
    [[nodiscard]] std::span<const Rgba8> row(int y) const;
    [[nodiscard]] Rgba8&       at(int x, int y);
    [[nodiscard]] const Rgba8& at(int x, int y) const;
    [[nodiscard]] PixelBuffer copy_region(RectI region) const;   // clipped to bounds
    void blit(int x, int y, const PixelBuffer& source);          // clipped to bounds
private:
    int m_width = 0, m_height = 0;
    std::vector<Rgba8> m_pixels;
};

// src/core/layer.hpp
struct Layer {
    std::string name;
    PixelBuffer pixels;
    float opacity = 1.0f;
    BlendMode blend_mode = BlendMode::Normal;
    bool visible = true;
};                                    // NOTE: no `locked` member

// src/core/layer_stack.hpp  (header-only)
class LayerStack {
public:
    void add(Layer layer);                        // push_back + m_active = last
    [[nodiscard]] Layer& at(std::size_t index);
    [[nodiscard]] Layer& active();
    [[nodiscard]] const Layer& active() const;
    [[nodiscard]] std::size_t active_index() const noexcept;
    [[nodiscard]] std::span<const Layer> layers() const noexcept;
private:
    std::vector<Layer> m_layers;
    std::size_t m_active = 0;
};                                    // NOTE: no remove/move/merge/set_active

// src/core/blend_modes.hpp
enum class BlendMode { Normal, Multiply, Screen };
using BlendFn = Rgba8 (*)(Rgba8 base, Rgba8 top, float opacity);
[[nodiscard]] BlendFn blend_fn_for(BlendMode mode);   // fallback: blend_normal

// src/core/compositor.hpp
void composite(const LayerStack& layers, RectI region, PixelBuffer& target);

// src/core/canvas_document.hpp
class CanvasDocument {
public:
    CanvasDocument(int width, int height);   // 1 layer "Background", opaque white,
                                             // whole bounds marked dirty
    [[nodiscard]] int width() const noexcept;     // = m_flattened.width()
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] RectI bounds() const noexcept;
    [[nodiscard]] LayerStack& layers() noexcept;
    [[nodiscard]] Layer& active_layer();
    [[nodiscard]] const PixelBuffer& flattened() const noexcept;
    void mark_dirty(RectI region);                // union, clipped to bounds
    [[nodiscard]] std::optional<RectI> flush();   // nullopt if nothing dirty;
                                                  // else composite(region) + return it
private:
    LayerStack m_layers;
    PixelBuffer m_flattened;
    RectI m_dirty;
};

// src/history/command.hpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo(CanvasDocument& document) = 0;
    virtual void redo(CanvasDocument& document) = 0;
};

// src/history/history_stack.hpp
class HistoryStack {
public:
    explicit HistoryStack(CanvasDocument& document);
    void push(std::unique_ptr<ICommand> command);  // clears redo; evicts front > 200
    void undo();                                   // no-op if empty
    void redo();                                   // no-op if empty
private:
    CanvasDocument& m_document;
    std::deque<std::unique_ptr<ICommand>> m_undo, m_redo;
};                                    // NOTE: no can_undo/can_redo/clear

// src/history/commands/paint_stroke_command.hpp
class PaintStrokeCommand final : public ICommand {
public:
    PaintStrokeCommand(std::size_t layer_index, RectI region,
                       PixelBuffer before, PixelBuffer after);
    void undo(CanvasDocument&) override;  // blit `before` at region
    void redo(CanvasDocument&) override;  // blit `after`  at region
    // apply() also calls document.mark_dirty(m_region)
};

// src/render/slint_bridge.hpp
class SlintBridge {
public:
    SlintBridge(int width, int height);   // allocates SharedPixelBuffer<Rgba8Pixel>
    void upload(const PixelBuffer& source, RectI region);  // row memcpy, clipped
    [[nodiscard]] slint::Image image() const;
private:
    slint::SharedPixelBuffer<slint::Rgba8Pixel> m_pixels;
};

// src/tools/tool.hpp
enum class ToolId { None, Paste, Selection, Crop, Resize, Rotate,
                    Pencil, Fill, Text, Eraser, Dropper, Magnify };
enum class PointerButton { Primary, Secondary };
class ITool {
public:
    virtual ~ITool() = default;
    virtual void on_pointer_down(ToolContext&, Point2f position, PointerButton) = 0;
    virtual void on_pointer_move(ToolContext&, Point2f) {}
    virtual void on_pointer_up(ToolContext&, Point2f) {}
};                                    // NOTE: no cursor(), no on_key()

// src/tools/tool_context.hpp
struct ColorPair {
    Rgba8 primary{0, 0, 0};
    Rgba8 secondary{255, 255, 255};
    [[nodiscard]] Rgba8& for_button(PointerButton button) noexcept;
};
struct ToolContext {
    CanvasDocument& document;
    HistoryStack& history;
    const Brush& brush;
    ColorPair& colors;
};                                    // NOTE: no ShapeOptions&, no ViewTransform&

// src/tools/tool_registry.hpp
class ToolRegistry {
public:
    ToolRegistry();                   // emplaces Pencil, Eraser, Fill, Dropper
    [[nodiscard]] ITool* find(ToolId id) const;   // nullptr if unregistered
private:
    std::unordered_map<ToolId, std::unique_ptr<ITool>> m_tools;
};

// src/tools/stroke_recorder.hpp
class StrokeRecorder {
public:
    explicit StrokeRecorder(CanvasDocument& document);
        // stores active layer index + FULL copy of that layer's pixels
    void touch(RectI region);         // m_region |= clipped region; mark_dirty
    void commit(HistoryStack& history);  // pushes PaintStrokeCommand(layer,
        // m_region, before.copy_region(m_region), after.copy_region(m_region))
        // (no push if m_region empty)
};

// src/app/input_router.hpp
class InputRouter {
public:
    InputRouter(const ToolRegistry& registry, ToolContext& context);
    void pointer_down(ToolId tool, Point2f position, PointerButton button);
    void pointer_move(Point2f position);
    void pointer_up(Point2f position);
private:
    const ToolRegistry& m_registry;
    ToolContext& m_context;
    ITool* m_active = nullptr;
};

// src/app/app_controller.hpp
class AppController {
public:
    AppController();   // m_document(800, 600), m_history(m_document),
                       // m_context{m_document, m_history, m_brush, m_colors},
                       // m_router(m_registry, m_context)
    [[nodiscard]] CanvasDocument& document() noexcept;
    [[nodiscard]] HistoryStack&   history()  noexcept;
    [[nodiscard]] InputRouter&    input()    noexcept;
    [[nodiscard]] Brush&          brush()    noexcept;
    [[nodiscard]] ColorPair&      colors()   noexcept;
private:
    CanvasDocument m_document;
    HistoryStack   m_history;
    Brush          m_brush;      // brushes/brush.hpp: size=1, spacing=0.25f, Round
    ColorPair      m_colors;
    ToolContext    m_context;
    ToolRegistry   m_registry;
    InputRouter    m_router;
};

// src/ui/shortcuts.hpp
enum class ShortcutAction { Undo, Redo };
[[nodiscard]] std::optional<ShortcutAction>
lookup_shortcut(std::string_view text, bool ctrl, bool shift);
// table: z/ctrl → Undo, z/ctrl+shift → Redo, y/ctrl → Redo
// normalizes control chars (key + 0x60) and lowercases before lookup

// src/ui/ui_state.hpp
class UiState {
public:
    UiState(AppWindow& window, AppController& controller);
private:
    void refresh();   // flush → upload → set_texture
    AppWindow& m_window;
    AppController& m_controller;
    SlintBridge m_bridge;   // sized once from document dims at construction
};
```

### Slint globals binding (as built, `ui/globals.slint`)

- `UserState`: callbacks `pointer-down(length, length, bool)`,
  `pointer-move(length, length)`, `pointer-up(length, length)`,
  `key-pressed(string, bool, bool)`; properties `color_palette ([color])`,
  `color_idx (int)`, `primary-color`, `secondary-color`, `selected-tool (ETool,
  default PENCIL)`, `selection-type (ESelectionType)`.
- `DocumentState`: `in property <int> canvas-width`, `canvas-height` — set **once** in
  the `UiState` ctor; nothing updates them afterwards.
- `ToolOptions`: `in-out property <length> brush-size: 1px` (read by `UiState` on every
  pointer-down; its UI wiring in `bar-home.slint`'s size dropdown is M1).
- Enums `ETool` (11 values, mirrors `ToolId` 1:1) and `ESelectionType`
  (RECTANGULAR/FREEHAND/POLYGON, unused so far).
- `appwindow.slint`: `in property <image> texture <=> canvas.source`;
  `FocusScope` forwards `key-pressed` → `UserState.key-pressed`; `TouchArea` sized
  `DocumentState.canvas-width * 1px + 2 * canvas.x`, computes canvas coords as
  `mouse-x/y - canvas.x/y` (i.e. raw canvas pixels; there is no zoom transform).

## File-by-file changes

### New files

Everything below exists today; this is the M0 inventory with real responsibilities.

- `src/core/types.hpp` — value types: `Rgba8`, `Point2f`, `RectI` with
  `empty/right/bottom/united/intersected`. No `Point2i`, no `Length` helpers (plan §4
  listed them; not needed yet).
- `src/core/pixel_buffer.hpp/.cpp` — flat `std::vector<Rgba8>` raster with span row
  access, bounds-checked-by-construction `at`, `copy_region`, clipped `blit`. All raster
  I/O funnels through here; tools never touch raw memory. **No resize/crop/flip/rotate**
  (needed by M2).
- `src/core/layer.hpp` — plain struct `Layer { name, pixels, opacity, blend_mode,
  visible }`. Header-only, no `.cpp`.
- `src/core/layer_stack.hpp` — ordered `std::vector<Layer>` + active index; `add`
  appends and activates. Header-only. Only `add/at/active/active_index/layers` exist.
- `src/core/blend_modes.hpp/.cpp` — `BlendMode` enum (3 values), `BlendFn` function
  pointer, `blend_fn_for` switch; shared `blend_with<>` template implements
  source-over with per-mode mix lambda (Normal/Multiply/Screen).
- `src/core/compositor.cpp` — `composite(layers, region, target)`: per row, fill
  region with transparent, then blend every visible layer via `blend_fn_for`. Assumes
  `target` and every layer share dimensions (indexes `layer.pixels.row(y)` directly).
- `src/core/canvas_document.hpp/.cpp` — owns `LayerStack` + flattened cache + single
  `RectI m_dirty`; ctor seeds one white "Background" layer; `mark_dirty`/`flush`
  implement the dirty-rect contract used by tools, undo, and the bridge.
- `src/history/command.hpp` — minimal `ICommand { undo(document); redo(document); }`
  (no merge hook).
- `src/history/history_stack.hpp/.cpp` — two `std::deque<std::unique_ptr<ICommand>>`;
  push clears redo and caps at `kMaxHistoryEntries` (from `src/config.hpp`).
- `src/history/commands/paint_stroke_command.{hpp,cpp}` — region-scoped before/after
  snapshot command; `apply` blits into `layers().at(index)` and marks dirty. The only
  command type in the repo.
- `src/render/slint_bridge.hpp/.cpp` — owns `SharedPixelBuffer<Rgba8Pixel>`;
  `upload(source, region)` clips + row-`memcpy`s only the region; `image()` wraps it for
  `AppWindow::set_texture`. Only file besides `ui_state` that includes Slint.
- `src/app/app_controller.{hpp,cpp}` — constructs and owns document, history, brush,
  colors, `ToolContext`, `ToolRegistry`, `InputRouter` in the exact member order above;
  exposes accessors. No IO/clipboard members (plan §3 shows them — not built).
- `src/app/input_router.{hpp,cpp}` — ToolId → `ITool*` dispatch with an `m_active`
  latch; no keyboard path (keys go `UiState` → `lookup_shortcut` → `HistoryStack`
  directly) and no tool-switch/cancel abort.
- `src/tools/tool.hpp` — `ToolId` (11 values), `PointerButton`, `ITool` interface.
- `src/tools/tool_context.hpp` — `ColorPair` (+`for_button`) and `ToolContext`.
- `src/tools/tool_registry.{hpp,cpp}` — ctor registers `Pencil`, `Eraser`, `Fill`,
  `Dropper`; `find` returns `nullptr` otherwise.
- `src/tools/stroke_recorder.{hpp,cpp}` — per-stroke before/after capture; `touch`
  accumulates the dirty region, `commit` pushes one `PaintStrokeCommand` holding only
  the region (but the ctor snapshots the **whole layer** — memory note in gaps).
- `src/tools/brush_stroke_tool.{hpp,cpp}` — shared `ITool` base implementing
  down/move/up lifecycle around an optional `StrokeRecorder` + `BrushEngine`, with
  `virtual Rgba8 stroke_color(ToolContext&, PointerButton) const` — foundation pattern
  M1's Pencil/Eraser derive from.
- `src/ui/ui_state.{hpp,cpp}` — the single Slint↔C++ adapter: pushes the default
  palette (`kDefaultPalette` 20 + `kCustomColorSlots` 10 white) into `UserState`, sets
  `DocumentState.canvas-width/height`, binds the four `UserState` callbacks (color +
  brush-size sync happens **on pointer-down**), owns the `SlintBridge`, and implements
  `refresh()` (flush → upload → `set_texture`). Also contains `tool_from_slint(ETool)`
  mapping.
- `src/ui/shortcuts.{hpp,cpp}` — `ShortcutAction { Undo, Redo }` + `lookup_shortcut`
  over a `constexpr` table.
- `src/config.hpp` — `kDefaultCanvasWidth = 800`, `kDefaultCanvasHeight = 600`,
  `kMaxHistoryEntries = 200`, `kCustomColorSlots = 10`, `kDefaultPalette` (20 colors).

### Modified files

- `src/main.cpp` — replaced the inline mockup (hardcoded palette push + draw-black-pixel
  click handler, previously ~250 lines) with 11 lines of wiring (see Status).
- `CMakeLists.txt` — C++23, Slint `v1.17.1` via `FetchContent` with
  `SLINT_FEATURE_RENDERER_SOFTWARE ON` (FemtoVG/Skia OFF, interpreter/live-preview OFF);
  `add_executable(ls_paint …)` now lists all 19 `.cpp` files (lines 27–46);
  `target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/src)`;
  `slint_target_sources(ls_paint ui/appwindow.slint)`; `-Wall -Wextra` (MSVC `/W4`).
- `ui/appwindow.slint` — `texture <=> canvas.source`; `FocusScope` with
  `init => { focus-scope.focus() }` forwarding `key-pressed`; `TouchArea` sized from
  `DocumentState` forwarding `pointer-down/up/moved` in canvas coordinates; gray
  workspace `Rectangle { background: #c5cfdf; }`; `Image` canvas `x:4px y:4px`,
  `image-rendering: pixelated`.
- `ui/globals.slint` — `UserState` lost mockup callbacks (`mouse_click`, `on-test`,
  `selected_color_idxs`) and gained `pointer-down/move/up`, `key-pressed`,
  `primary-color`, `secondary-color`; added globals `DocumentState` and `ToolOptions`
  (the latter's `brush-size` UI wiring is M1).
- `ui/bar/bar-home.slint` — color buttons now bind `UserState.primary-color` /
  `secondary-color` instead of `selected_color_idxs` (so `UiState` can read/write them).
  The `ToolOptions.brush-size` size-dropdown wiring in the same file is M1 work.

## Implementation order

The order M0 was (and should be) built in; each step compiles:

1. `core/types.hpp` → `pixel_buffer` → `blend_modes` → `layer`/`layer_stack` →
   `compositor` → `canvas_document` (pure C++, no Slint; add each `.cpp` to
   `CMakeLists.txt` as it appears).
2. `history/command.hpp` → `history_stack` → `commands/paint_stroke_command`
   (depends on `CanvasDocument`; still Slint-free).
3. `render/slint_bridge` (first Slint-dependent file; `static_assert` layout equality).
4. `tools/tool.hpp` → `tool_context.hpp` → `brushes/brush.hpp` → `tool_registry` →
   `stroke_recorder` → `brush_stroke_tool` (skeleton only; concrete tools are M1).
5. `app/input_router` → `app/app_controller` (member declaration order matters:
   document → history → brush → colors → context → registry → router).
6. `ui/shortcuts` → `ui/ui_state` (binds globals, implements `refresh()`).
7. `main.cpp` shrinks to wiring; run and verify a stroke + undo/redo.

## Slint specifics

No new `.slint` files are added by M0; it consumes/extends existing ones:

- `ui/globals.slint`: `UserState.pointer-down(length, length, bool)` /
  `pointer-move(length, length)` / `pointer-up(length, length)` /
  `key-pressed(string, bool, bool)` callbacks; `UserState.primary-color` /
  `secondary-color` (replacing `selected_color_idxs`); new globals `DocumentState`
  (`canvas-width`, `canvas-height`) and `ToolOptions` (`brush-size`).
- `ui/appwindow.slint`: `texture` property bound to the canvas `Image`; `FocusScope`
  key forwarding; `TouchArea` event forwarding with `- canvas.x/- canvas.y` coordinate
  conversion (pure translation, scale = 1).
- Dead-but-present UI (not M0's job, listed for later milestones): Home bar buttons for
  paste/cut/copy, selection ops, resize/rotate, text/magnify/selection/crop tools,
  Edit Colors, Brushes dropdown; `bar-view.slint` placeholder; no file menu, no
  `ui/dialogs/`, no `ui/canvas-overlay.slint`, no `ui/statusbar.slint`.

## Build changes

Exact current source list in `CMakeLists.txt` (lines 27–46) — M0 added all non-M1
entries; any future `.cpp` must be appended here:

```cmake
add_executable(ls_paint
    src/main.cpp
    src/app/app_controller.cpp
    src/app/input_router.cpp
    src/brushes/brush_engine.cpp
    src/core/blend_modes.cpp
    src/core/canvas_document.cpp
    src/core/compositor.cpp
    src/core/pixel_buffer.cpp
    src/history/history_stack.cpp
    src/history/commands/paint_stroke_command.cpp
    src/render/slint_bridge.cpp
    src/tools/brush_stroke_tool.cpp
    src/tools/dropper_tool.cpp
    src/tools/fill_tool.cpp
    src/tools/stroke_recorder.cpp
    src/tools/tool_registry.cpp
    src/ui/shortcuts.cpp
    src/ui/ui_state.cpp
)
```

No test target exists (`enable_testing`/`add_test` absent) — see gaps.

## Verification

- Build: `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
  (must be warning-clean under `-Wall -Wextra`).
- Manual checklist (all pass today):
  1. App opens with white 800×600 canvas top-left in gray workspace, 4px gutter.
  2. Drag with Pencil → stroke appears; drag with Fill/Dropper/Eraser behaves
     differently per tool (proves routing, not hardcoded paint).
  3. `Ctrl+Z` reverts the last stroke exactly; `Ctrl+Shift+Z` and `Ctrl+Y` reapply;
     drawing after undo clears redo.
  4. Resize the window — canvas stays top-left, no repaint glitch.
  5. Select Resize/Crop/Text/Magnify/Selection from the bar → nothing happens and
     nothing crashes (unregistered `ToolId` → `nullptr`).
- Unit-testable pure logic (no test harness exists yet; plan §9 says to add one):
  `RectI::united/intersected`, `PixelBuffer::copy_region/blit` clipping,
  `blend_fn_for` math (alpha cases), `composite` layer stacking, `CanvasDocument`
  dirty-rect accumulation (`mark_dirty` unions, `flush` clears and returns),
  `HistoryStack` push/undo/redo/eviction semantics, `PaintStrokeCommand`
  region-only restoration, `lookup_shortcut` normalization, and a
  `SlintBridge::upload` test asserting only the dirty rows are written.

## Risks & edge cases

- `composite` assumes all layers are the same size as `target`; a future
  `CanvasDocument::resize` that resizes layers asymmetrically will read out of bounds.
  Resize must resize **every** layer and `m_flattened` together (or add a guard).
- `SlintBridge` is sized once in the `UiState` ctor; changing canvas dimensions requires
  reallocating the bridge (new `SlintBridge(w,h)`), updating
  `DocumentState.canvas-width/height`, and re-uploading full bounds — none of that path
  exists yet.
- `StrokeRecorder` copies the entire active layer at construction (O(W·H) per stroke
  start) even though `commit` stores only the dirty region. Acceptable at M0/M1 canvas
  sizes; optimize by snapshotting lazily/per-region if large canvases arrive.
- `InputRouter::pointer_down` silently drops a second pointer-down while a stroke is
  active; Slint's `PointerEventKind.cancel` is mapped to `pointer-up`, so a canceled
  stroke still commits (no rollback path).
- `UiState` syncs colors/brush-size only on pointer-down — a color change followed by a
  keyboard shortcut does not need it, but any future tool that reads options without a
  preceding pointer-down must re-read globals itself.
- `HistoryStack` eviction (`pop_front` past 200) silently makes old undos
  unrecoverable; fine for v1, but a "history depth" setting (M8) should reuse
  `kMaxHistoryEntries`.
- `ETool` ↔ `ToolId` mapping is a hand-written `switch` in `ui_state.cpp`; adding an
  enum value requires touching both enums + that switch (no static-assert tying them).

### Foundation gaps (work for later milestones)

Explicit list of what M0 does **not** provide — check this before planning:

1. **`CanvasDocument`**: no `resize`, `rotate`, `flip`, no selection-mask member
   (plan §3/§5 show it owning a `SelectionMask`), no document-changed/dirty-flag for
   unsaved changes, no signal when canvas dimensions change, no
   `recreate-after-resize` support for the bridge.
2. **`LayerStack`**: no `remove`, `move`/reorder, `merge_down`, `set_active`, `size`,
   and no mutable `layers()` span (only `at()` for mutation).
3. **`Layer`**: no `locked` member (plan §5.1 requires it).
4. **`HistoryStack`**: no `can_undo()`/`can_redo()`/`clear()`/`depth()` for the UI;
   `ICommand` has no `try_merge`/`merge_with` (plan §5.5); `history/commands/` contains
   only `paint_stroke_command` — no `selection_change`, `canvas_resize`,
   `canvas_rotate_flip`, or `layer_command`.
5. **No `ViewTransform`** (`src/render/view_transform.*` does not exist): screen == canvas
   coordinates at 1:1; no zoom/scroll mapping (M5).
6. **`ITool`**: no `cursor()`, no `on_key`, no options hook; `ToolContext` lacks the
   plan's `ShapeOptions&` and `ViewTransform&` members.
7. **`ToolRegistry`** has no entries for `Paste`, `Selection`, `Crop`, `Resize`,
   `Rotate`, `Text`, `Magnify` (`find` → `nullptr`).
8. **No test target**: `CMakeLists.txt` has no `enable_testing()`/test executable —
   plan §9's unit + golden-image + upload tests are all unimplemented.
9. **No `src/selection/` directory or type** (`SelectionMask` and its builders/ops do
   not exist) and no `ESelectionType` consumer.
10. **Dirty tracking is a single merged `RectI`**, not a rect list — disjoint dirty areas
    become their bounding box (plan §7 step 3 allows multiple rects; `SlintBridge::upload`
    takes one region).
11. **No `src/io/`** (document_io/clipboard/recent_files), **no `src/core/filters/`**
    (IFilter/builtin filters/filter registry per plan §5.3), no file menu plumbing in
    `AppController`.
12. **`PixelBuffer`** lacks `resize`/`crop`/flip/rotate helpers that plan §5.1 says
    should live here (M2 needs them).
13. **`BlendMode`** ships only 3 of the plan's 8 modes (mechanism is complete, entries
    are not).
14. **`Brush`** has only `size`/`spacing`/`shape` — no `hardness`, `opacity`,
    `custom_stamp` (M6); `BrushEngine` exists but presets/Brushes dropdown are unwired.
15. **Slint-side state gaps**: `DocumentState` has no `zoom`, `has-selection`,
    `selection-rect`, `can-undo`, `can-redo`, `dirty` (plan §6); `HistoryStack` never
    pushes these to the UI.
16. **Cross-cutting requirement #2 unimplemented**: no canvas edge/corner resize
    handles, no `ui/canvas-overlay.slint`, no `canvas-x/canvas-y`/zoom as single source
    of truth (requirement #1 — top-left alignment — holds today only trivially, via the
    1:1 untransformed layout).

## Milestone dependencies

- **Preconditions**: none. M0 is the root milestone; it consumes only the pre-existing
  UI mockup (`ui/*.slint`, icons) and the Slint 1.17.1 software-renderer build setup.
- **Already built on top of it (do not re-plan)**: M1 (Pencil/Eraser/Fill/Dropper,
  `ToolOptions.brush-size`, undo/redo shortcuts, StrokeRecorder one-command-per-stroke)
  — its registrations are visible in `tool_registry.cpp` today.
- **What later milestones build on this foundation**:
  - **M2 (Selection & transform)** — needs gaps 1, 9, 12, 16: `SelectionMask`,
    `CanvasDocument::resize/rotate/flip` + `CanvasResizeCommand`/`CanvasRotateFlipCommand`,
    `PixelBuffer` geometry ops, bridge/DocumentState resize path, overlay handles
    (depends on the cross-cutting `canvas-x/y` + zoom source of truth).
  - **M3 (Shapes & text)** — needs gap 6: `ToolContext::ShapeOptions`, `ITool` extras,
    Slint preview overlay.
  - **M4 (File I/O & clipboard)** — needs gap 11: `src/io/`, AppController IO members,
    document dirty flag (gap 1).
  - **M5 (View & status)** — needs gaps 5, 15, and the bridge-resize path: `ViewTransform`,
    zoom in `DocumentState`, status bar, `SlintBridge` re-allocation.
  - **M7 (Layers panel)** — needs gaps 2, 3: `LayerStack::remove/move/merge/set_active`,
    `Layer::locked`, plus a mutable layers accessor.
  - **Testing (any milestone)** — needs gap 8: a test target in `CMakeLists.txt`.
