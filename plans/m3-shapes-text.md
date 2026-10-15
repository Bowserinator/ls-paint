# M3 — Shapes & text

## Status

**Built.** The "As built" section below records what shipped; the rest of this
document is the original design and stays as the record of what was planned.

## As built

- `src/core/raster.{hpp,cpp}` — hard-edged primitives over a `Target` (pixel
  buffer + optional selection clip mask): line, rect, rounded rect, ellipse,
  polyline, polygon fill, glyph blit, plus bounds helpers.
- `src/tools/shape_tool.{hpp,cpp}` — `ShapeId` (23 kinds), `ShapeMode`,
  `ShapeOptions`, `ShapePreview`, `namespace shape` (outline geometry,
  `svg_commands`, `gallery_commands`), `rasterize_shape`, `ShapeTool`.
- `src/tools/text_tool.{hpp,cpp}` — `TextOptions`, `TextOverlayState`,
  `draw_text`, on vendored stb_truetype + bundled DejaVu faces.
- `ui/globals.slint` gained `EShapeKind`, `EShapeMode`, `ShapeStyle`,
  `ShapeGallery`, `ShapeOverlay`, `FontStyle`, `TextOverlay`; the preview is the
  `Path` overlay in `ui/appwindow.slint` and the text box is the `TextInput`
  host next to it.
- `ui/bar/bar-options.slint` — the text options strip, shown while the text tool
  is active and laid over the canvas area.

Differences from the design above: the catalogue is 23 shapes instead of 5
(rect/oval/rounded rect use the dedicated primitives, every other kind is a
sampled vertex list filled by `draw_polygon_fill` and stroked by
`draw_polyline`); picker glyphs are drawn by Slint from the same geometry the
rasterizer uses, so no shape icon assets were added; Enter inserts a newline
(Slint's `TextInput` has no commit-on-Return), so the commit keys are Escape, a
click elsewhere, a tool switch, and leaving the tool; the options strip is laid
over the canvas rather than inside the window's root layout, because a layout
child pinned the window's maximum width.

What existed before this milestone (verified by reading the code, not assumed):

- *(This bullet described the pre-M3 tree; the milestone is now built — see
  "As built" at the top of this file.)* `src/tools/` contains only
  `tool.hpp` (ITool/`ToolId`/`PointerButton`), `tool_context.hpp`
  (`ToolContext{document, history, brush, colors}`), `tool_registry.{hpp,cpp}`
  (registers **Pencil/Eraser/Fill/Dropper only**), `stroke_recorder.{hpp,cpp}`,
  `brush_stroke_tool.{hpp,cpp}`, `pencil_tool.hpp`, `eraser_tool.hpp`,
  `fill_tool.{hpp,cpp}`, `dropper_tool.{hpp,cpp}`. There is **no** `shape_tool.*`,
  **no** `text_tool.*`, **no** `src/core/raster.*`, **no** `third_party/`.
- `ToolId` (src/tools/tool.hpp:9) = `None, Paste, Selection, Crop, Resize, Rotate,
  Pencil, Fill, Text, Eraser, Dropper, Magnify` — **there is no `ToolId::Shape`**,
  but `ToolId::Text` already exists and is simply never registered (so
  `ToolRegistry::find` returns `nullptr` and the click is a no-op).
- `ETool` (ui/globals.slint:1) = `NONE, PASTE, SELECTION, CROP, RESIZE, ROTATE,
  PENCIL, FILL, TEXT, ERASER, DROPPER, MAGNIFY` — **no `SHAPE` value**. The only
  exhaustive `switch` over it is `tool_from_slint()` in `src/ui/ui_state.cpp:15`
  (no `default:` → adding an enum value without a case trips `-Wswitch`).
- `ToolOptions` (ui/globals.slint:27) holds only `brush-size`. No shape/font options.
- `ui/bar/bar-home.slint`:
  - **Tools grid** line 163-167: Text button already sets `UserState.selected-tool =
    ETool.TEXT` and highlights on `== ETool.TEXT` — **already wired on the Slint side**;
    it only fails because C++ registers no `ToolId::Text`.
  - **Shapes group** lines 202-219: two dead `ImageButtonHorizontal`s, `"Outline ▾"`
    (`icons/shape-outline.png`) and `"Fill ▾"` (`icons/shape-fill.png`) — no
    `clicked`, no popup, no state.
  - Size dropdown (lines 226-259) is the popup pattern to copy
    (`size-dropdown := PopupWindow { … }` + `.show()`).
- `ui/appwindow.slint`: `focus-scope` forwards `key-pressed`; the single
  `touch := TouchArea` forwards `pointer-event` (down/up/cancel) and `moved`, and
  hosts `canvas := Image { x: 4px; y: 4px; … }` plus M1's `brush-outline`. There is
  **no `ui/canvas-overlay.slint`** and **no shared parent Rectangle** yet (both are
  M2 work, see `plans/m0-foundation.md` / `plans/m1-core-drawing-tools.md`).
- `icons/` inventory relevant to M3 — **present**: `shape-outline.png`,
  `shape-fill.png`, `text.png`, `size.png`. **Absent**: any per-shape-kind icon
  (no line/rectangle/rounded-rect/ellipse/polygon glyph), no bold/italic glyph, no
  "outline *and* fill" glyph. Decision for M3: **create no new icon files** (see
  *Slint specifics* → icons).
- Cross-cutting M1 machinery M3 builds on (`plans/m1-core-drawing-tools.md`):
  `UiState::sync_tool_options()` called at the top of all three pointer lambdas,
  `UiState::handle_shortcut()`, `InputRouter::active()` mid-stroke guard, `refresh()`
  writing `DocumentState.can-undo/can-redo` **before** the `flush()` early-return, the
  `tests/` + `ls_paint_tests` target, and the frozen M2 clip contract
  `lsp::clip_to_selection(const CanvasDocument&, RectI)` (file
  `src/selection/clip.hpp`, delivered by M2).

## Acceptance criteria

1. With the **Shape** tool selected (`ETool.SHAPE`), dragging on the canvas shows a
   **live outline/fill preview in a Slint overlay** that follows the pointer; the pixel
   buffer is **not** modified until mouse-up (verified by pressing Ctrl+Z *during* the
   drag → nothing to undo; and by inspecting that the texture upload only happens on
   release).
2. On mouse-up the shape is written into the active layer and appears pixel-identical
   to where the preview was (within 1–2 px), as **one** undo entry (one Ctrl+Z removes
   the whole shape, Ctrl+Y restores it).
3. `ShapeId` supports **Line, Rectangle, RoundedRect, Ellipse, Polygon**; the active
   kind is chosen from a new shape-picker dropdown in the Shapes group and persists in
   `ToolOptions.shape-kind`.
4. Shape mode **Outline / Fill / Both** is selectable from the existing Shapes-group
   buttons: clicking the body of `Outline ▾` sets Outline, the body of `Fill ▾` sets
   Fill, and the ▾ on either opens a popup listing all three modes with the current one
   highlighted; both write `ToolOptions.shape-mode`.
5. `ToolOptions.shape-thickness` (1–32 px) drives both the live preview stroke width
   and the committed raster, and is changeable from the options strip while the tool is
   active.
6. **Polygon**: each click drops a vertex with a rubber-band edge to the cursor
   (hover-tracked); **double-click**, **Enter**, or clicking within 8 px of the first
   vertex closes and commits it (≥3 vertices); **Escape** cancels the in-progress
   polygon (preview disappears, nothing is written, no history entry). Closing with
   fewer than 3 vertices does nothing.
7. With the **Text** tool selected, clicking the canvas opens a floating, focus-taking
   Slint `TextInput` overlay at that point (empty, with a 1 px box outline); typing
   edits it live; **Enter**, **Escape**, clicking elsewhere on the canvas, or switching
   tools all **commit** the text: glyphs are rasterized into the active layer's
   `PixelBuffer` under the click point as one undo entry, and the overlay closes.
   `Shift+Enter` inserts a newline instead of committing; empty text commits nothing.
8. The options strip appears below the ribbon while `SHAPE` or `TEXT` is selected and
   exposes shape thickness (SHAPE) and font family / size / bold / italic (TEXT);
   changing font size/family/weight while a text box is open updates the overlay live
   and is what gets rasterized on commit.
9. The rasterized text honors `font-family` (3 bundled families), `font-size`,
   `font-bold`, `font-italic`, is UTF-8 correct, multi-line aware, alpha-blended with
   anti-aliasing, and clipped to the canvas bounds (and to the selection once M2's clip
   hook is wired).
10. Selected-tool cursor over the canvas is `crosshair` for SHAPE and `text` for TEXT.
11. Hovering the canvas (no button) keeps the shape/polygon preview following the
    cursor — i.e. overlay properties update from `pointer-move` without any document
    dirty rect, and nothing is uploaded to the texture in that case.
12. Everything builds warning-clean under `-Wall -Wextra` and `ctest` passes with the
    new `raster`/`shape`/`text` unit tests.

## Design

### Data flow (one shape drag, one text commit)

```
Slint bar (ToolOptions.* / selected-tool)          Slint canvas overlay (OverlayState.*)
        │  getters, on every event                         ▲  setters, every refresh()
        ▼                                                  │
UiState::sync_tool_options() ──► AppController            │
   (M1 R2 pattern, extended)      ├─ ShapeOptions m_shape_options ─┐
UiState::sync_overlays() ◄────────┼─ ToolUiState  m_tool_ui  ◄─────┼── ShapeTool/TextTool
        │                         │    { ShapePreview shape,        │   (plain C++ state,
        │                         │      TextOverlayState text }    │    no Slint types)
        ▼                         │                                │
UserState.pointer-down/move/up ──► InputRouter ──► ITool            │
UserState.key-pressed ──────────► InputRouter::key ─► ITool         │
UserState.double-click ─────────► InputRouter::double_click ─► ITool│
UserState.commit-text(string) ──► UiState::do_commit_text ──────────┘
                                          │
                                          ▼
                  AppController::commit_text → raster::* → StrokeRecorder → HistoryStack
```

Two invariants this design preserves:

- **Tools never see Slint.** plan.md §3 says `src/tools/` must not know Slint, so tools
  write plain structs (`ShapePreview`, `TextOverlayState`) into `ToolContext`, and
  `UiState::refresh()` *polls* them into globals — exactly the round-trip the existing
  code already uses for `ColorPair` (tool writes `context.colors`, `ui_state.cpp:63`
  reads it back).
- **Options are Slint-owned** (M1 R2): C++ only ever *reads* `ToolOptions`, in
  `sync_tool_options()`, called at the top of `pointer-down/move/up`, the key handler
  and the text-commit callback (font options can change while a box is open).

### Types & APIs (exact)

```cpp
// src/tools/shape_tool.hpp          (NEW)
namespace lsp {
struct ToolContext;                          // forward decl — no include cycle

enum class ShapeId { Line, Rectangle, RoundedRect, Ellipse, Polygon };
enum class ShapeMode { Outline, Fill, Both };

struct ShapeOptions {                        // owned by AppController, ref'd by ToolContext
    ShapeId  kind      = ShapeId::Rectangle;
    ShapeMode mode     = ShapeMode::Outline;
    int      thickness = 1;                  // px, clamped 1..32 from ToolOptions.shape-thickness
    int      corner_radius = 8;              // RoundedRect only; fixed in M3 (not user-exposed)
};

struct ShapePreview {                        // written by ShapeTool, read by UiState::sync_overlays()
    bool        visible       = false;
    bool        draw_fill     = false;
    bool        draw_outline  = true;
    Rgba8       color         {0, 0, 0, 255};
    std::string commands;                    // SVG path data, canvas-px coords → OverlayState.shape-commands
};

namespace shape {
// Pure, unit-tested helpers (also used by the commit path, so preview and raster
// are derived from the *same* geometry).
[[nodiscard]] RectI  normalized_rect(Point2f a, Point2f b);      // min/max + round, ≥1×1
[[nodiscard]] RectI  bounds(ShapeId, Point2f a, Point2f b,
                            std::span<const Point2f> vertices, int thickness);
[[nodiscard]] std::string svg_commands(ShapeId, Point2f a, Point2f b, int corner_radius,
                                       std::span<const Point2f> vertices,
                                       Point2f cursor, bool polygon_open);
}  // namespace shape

class ShapeTool final : public ITool {
public:
    void on_pointer_down(ToolContext&, Point2f, PointerButton) override;
    void on_pointer_move(ToolContext&, Point2f) override;
    void on_pointer_up(ToolContext&, Point2f) override;
    void on_key_pressed(ToolContext&, std::string_view text) override;  // "\n" = Enter, "\u001b" = Esc
    void on_double_click(ToolContext&) override;
    void on_deactivate(ToolContext&) override;                          // tool switch → cancel polygon

private:
    void update_preview(ToolContext&, Point2f cursor);
    void close_polygon(ToolContext&);      // ≥3 verts → rasterize + StrokeRecorder commit
    void cancel_polygon(ToolContext&);

    bool         m_dragging = false;       // line/rect/rounded/ellipse drag in progress
    PointerButton m_button  = PointerButton::Primary;
    Point2f      m_origin{}, m_cursor{};
    std::vector<Point2f> m_polygon;        // vertices placed so far
    bool         m_polygon_open = false;
};
}  // namespace lsp
```

```cpp
// src/tools/text_tool.hpp            (NEW)
namespace lsp {
struct ToolContext;

struct TextOptions {                        // owned by AppController; read at commit time
    std::string family = "DejaVu Sans";     // must match a row of the font-family dropdown
    int         size_px = 16;
    bool        bold    = false;
    bool        italic  = false;
};

struct TextOverlayState {                   // written by TextTool, read by UiState::sync_overlays()
    bool    visible = false;
    Point2f origin  {0, 0};                 // canvas px, top-left of the text block
    Rgba8   color   {0, 0, 0, 255};         // captured from the placing click's button
};

class TextTool final : public ITool {
public:
    void on_pointer_down(ToolContext&, Point2f, PointerButton) override;   // opens the overlay
};

// Rasterizes `utf8` into the active layer at `origin` as ONE undoable stroke.
// Returns the dirty rect (empty if nothing was drawn). Empty `utf8` → no-op.
[[nodiscard]] RectI commit_text(ToolContext& context, Point2i origin, std::string_view utf8,
                                const TextOptions& options, Rgba8 color);
}  // namespace lsp
```

```cpp
// src/tools/tool.hpp — ITool gains three defaulted hooks (no existing tool breaks)
virtual void on_key_pressed(ToolContext&, std::string_view /*text*/) {}
virtual void on_double_click(ToolContext&) {}
virtual void on_deactivate(ToolContext&) {}   // tool deselected: release transient state
enum class ToolId { None, Paste, Selection, Crop, Resize, Rotate,
                    Pencil, Fill, Shape, Text, Eraser, Dropper, Magnify };  // + Shape, 1:1 with ETool
```

```cpp
// src/tools/tool_context.hpp — additions
#include "tools/shape_tool.hpp"     // ShapeOptions, ShapePreview
#include "tools/text_tool.hpp"      // TextOptions (needed by AppController), TextOverlayState
struct ToolUiState {
    ShapePreview      shape;
    TextOverlayState  text;
};
struct ToolContext {
    CanvasDocument&  document;
    HistoryStack&    history;
    const Brush&     brush;
    ColorPair&       colors;
    ShapeOptions&    shape_options;   // NEW: live read each event (thickness/mode/kind)
    ToolUiState&     ui;              // NEW: tool → UiState channel
};
```

```cpp
// src/app/input_router.hpp — pointer_move gains the tool id; three new dispatchers
void pointer_move(ToolId tool, Point2f position);  // was pointer_move(Point2f)
void key(ToolId tool, std::string_view text);      // Enter/Escape while no drag is active
void double_click(ToolId tool);                    // NOT guarded by m_active (see below)
void deactivate(ToolId tool);
[[nodiscard]] bool active() const noexcept;        // M1 R9, unchanged
```

```cpp
// src/app/app_controller.hpp — new members (MUST be declared before m_context:
// m_context binds references to them; see M0's "member declaration order matters")
[[nodiscard]] ShapeOptions& shape_options() noexcept;
[[nodiscard]] TextOptions&  text_options()  noexcept;
[[nodiscard]] ToolUiState&  tool_ui()       noexcept;
[[nodiscard]] ToolContext&  context()       noexcept;
[[nodiscard]] ITool*        tool(ToolId id) const noexcept { return m_registry.find(id); }
void commit_text(Point2i origin, std::string_view utf8, Rgba8 color);   // → lsp::commit_text(...)
private:
 ShapeOptions m_shape_options;
 TextOptions  m_text_options;
 ToolUiState  m_tool_ui;
 ToolContext  m_context;   // {m_document, m_history, m_brush, m_colors, m_shape_options, m_tool_ui}
```

```cpp
// src/core/raster.hpp (+ .cpp) — pure, Slint-free, unit-testable (plan §9)
namespace lsp::raster {

// Selection-clip hook (plan §5.4). `clip_mask` = one byte per pixel, same dims as
// `pixels`, 0 = outside selection; nullptr = no clipping (the M2-free default).
struct Target {
    PixelBuffer&          pixels;
    const std::uint8_t*   clip_mask = nullptr;
    [[nodiscard]] bool allows(int x, int y) const noexcept;
};

void draw_line           (Target, Point2f a, Point2f b, int thickness, Rgba8);
void draw_rect_outline   (Target, RectI rect, int thickness, Rgba8);
void draw_rect_fill      (Target, RectI rect, Rgba8);
void draw_rounded_rect_outline(Target, RectI rect, int radius, int thickness, Rgba8);
void draw_rounded_rect_fill   (Target, RectI rect, int radius, Rgba8);
void draw_ellipse_outline     (Target, RectI bounds, int thickness, Rgba8);
void draw_ellipse_fill        (Target, RectI bounds, Rgba8);
void draw_polyline       (Target, std::span<const Point2f> pts, int thickness, Rgba8, bool closed);
void draw_polygon_fill   (Target, std::span<const Point2f> pts, Rgba8);
void draw_glyph          (Target, int x, int y, int w, int h, int stride,
                          const std::uint8_t* coverage, Rgba8);   // 8-bit AA coverage blend

RectI bounds_line(Point2f a, Point2f b, int thickness);
RectI bounds_rect(RectI rect, int thickness);
RectI bounds_points(std::span<const Point2f> pts, int thickness);
}  // namespace lsp::raster
```

### WHY `src/core/raster.hpp` and not inside `shape_tool`

plan.md §9 wants `core/` pure and unit-testable without a window; the primitives are
geometry, not tool policy, and `text_tool` needs `draw_glyph` too (a second consumer).
`shape_tool` keeps only *policy*: normalization, mode resolution, polygon state, and
`shape::svg_commands` (which is UI-facing and therefore lives in the tool).

**Algorithm notes (so the coding agent never has to decide):**

- *Line*: integer Bresenham; at each step stamp an axis-aligned `thickness ×
  thickness` square centered on the pixel → square caps/joins, gap-free for fast drags.
  `thickness == 1` degenerates to the plain 1 px line.
- *Ellipse*: per-scanline analytic span. For row center `yc`, `dy = yc - cy`;
  `dx = rx·sqrt(max(0, 1 - (dy/ry)²))` → span `[ceil(cx-dx), floor(cx+dx)]`. **Outline**
  = span of the outer rect minus span of the inner rect `(rx-t, ry-t)` (clamped ≥ 0.5) —
  never "fill then erase", so underlying pixels are never destroyed.
- *Rounded rect*: `inside(p, inset)` predicate = point in `rect inset by inset` OR in a
  corner disc of radius `max(0, r - inset)`; outline = `span(y, 0) \ span(y, t)`.
- *Polygon fill*: scanline even-odd at pixel centers (y+0.5), edges sorted per row.
- *Polygon/polyline outline*: consecutive `draw_line` calls plus the closing edge when
  `closed`.
- *Bounds*: computed by `shape::bounds()` **before** drawing (the raster functions do
  not report dirty rects); `StrokeRecorder::touch()` already intersects with
  `document.bounds()` so off-canvas shapes are safe, and every pixel write goes through
  `Target::allows` (bounds + optional clip mask).
- No anti-aliasing for shapes (hard pixels) — consistent with the rest of the app;
  only glyphs are AA (they carry an 8-bit coverage mask from stb_truetype).

### Shape preview mechanism — Slint overlay, never the pixel buffer

Per plan.md §7 item 5 the preview is a **`Path` element inside
`ui/canvas-overlay.slint`**, bound to one new global:

```slint
shape-preview := Path {
    visible: OverlayState.shape-visible && UserState.selected-tool == ETool.SHAPE;
    x: 0px; y: 0px;
    width:  DocumentState.canvas-width  * 1px;
    height: DocumentState.canvas-height * 1px;
    viewbox-x: 0; viewbox-y: 0;
    viewbox-width:  DocumentState.canvas-width;    // viewbox units == canvas px
    viewbox-height: DocumentState.canvas-height;   // ⇒ scale factor is exactly 1:1
    fit: fill;
    commands: OverlayState.shape-commands;         // Path's `commands` takes a binding ✓
    fill:     OverlayState.shape-draw-fill    ? OverlayState.shape-color : transparent;
    stroke:   OverlayState.shape-draw-outline ? OverlayState.shape-color : transparent;
    stroke-width: ToolOptions.shape-thickness;
    stroke-line-cap: round;
    stroke-line-join: round;
}
```

WHY the `viewbox`: the docs state the path's `width`/`height` (default **100 % of the
parent**) scale the path's bounding box into the element bounds, which would destroy
canvas-pixel coordinates. Declaring an explicit viewbox equal to the element size makes
the transform the identity *by construction*, so `OverlayState.shape-commands` can be
authored directly in canvas pixels by C++.

WHY the commands string comes from C++ (not a Slint function): a polygon needs a loop
over a variable vertex list, and Slint expressions cannot build a string in a loop;
C++ gets `shape::svg_commands()` for free (one implementation used by preview only —
the raster path uses the same `ShapeId`/`Point2f` inputs, so the two agree by
construction). Rect/ellipse/rounded/polygon/line are all emitted as SVG subpaths
(`M`/`L`/`H`/`V`/`A`/`Z`), which `Path` supports.

Repaint/invalidation: `UiState::refresh()` calls `sync_overlays()` **unconditionally,
before the `flush()` early-return**, so a hover move that dirties nothing still updates
`OverlayState` → Slint re-evaluates the `commands` binding → the software renderer
repaints the affected element. No texture upload happens (that part still early-returns).

### Polygon interaction — exact rules

State lives in the `ShapeTool` instance (it must survive across
down/up pairs; `InputRouter::m_active` is cleared on every `pointer-up`).

| Event | Behavior |
|---|---|
| down, polygon not open, kind == POLYGON | start: `m_polygon = {p}`, `m_polygon_open = true`, no drag, no recorder; preview on |
| down, polygon open, `size() ≥ 3` and `‖p − v0‖ ≤ 8 px` | close → commit (vertex *not* appended) |
| down, polygon open, otherwise | append `p` (ignore if within 3 px of the last vertex); preview on |
| down, polygon open but kind was switched away from POLYGON | `cancel_polygon()` first, then start a normal drag |
| move (hover **or** drag) | `m_cursor = p`; rebuild `shape-commands` (rubber band = last vertex → cursor) |
| `double-clicked` (Slint callback, fires on the 2nd release *before* `pointer-event up`) | if open: drop the trailing vertex added by the 2nd `down` when it is within 12 px of its predecessor, then close if `size() ≥ 3` |
| Enter (`"\n"`) | if open and `size() ≥ 3` → close; else no-op |
| Escape (`"\u001b"`) | if open → cancel (vertices cleared, preview hidden, no pixels, no history); else no-op |
| up | no-op while `m_polygon_open` (only `m_dragging` shapes rasterize on up) |
| tool switch | `on_deactivate()` → `cancel_polygon()` (also: the preview is gated on `selected-tool == ETool.SHAPE` in Slint, so it hides instantly even before C++ notices) |

Key constants are **not** guesses: `internal/common/key_codes.rs` in Slint defines
`Return = '\u{000a}'` and `Escape = '\u{001b}'`, so C++ compares against `"\n"` and
`"\u001b"` (Slint-side comparisons use `Key.Return` / `Key.Escape`). The dispatch goes
through `InputRouter::key(ToolId, text)` which — unlike `pointer_*` — is **not**
blocked by `m_active` only in the sense that it early-returns while a drag is active;
between polygon clicks `m_active` is null, which is exactly when Enter/Esc arrive.
`double_click` must *not* be `m_active`-guarded, because Slint fires
`double-clicked` before the corresponding `pointer-event up`.

`double-clicked` needs a new `UserState.double-click()` callback forwarded from
appwindow's TouchArea — Slint already implements OS click-counting (no manual
timestamps; `click_count` starts at 0 on the first click and becomes 1 on the second,
which is when `double-clicked` fires).

### Commit path (shapes) — one undo entry

```cpp
void ShapeTool::on_pointer_up(ToolContext& ctx, Point2f p) {   // sketch
    if (!m_dragging) return;
    m_cursor = p;
    const ShapeOptions& o = ctx.shape_options;
    ShapeId kind = o.kind;                       // POLYGON never reaches here while open
    ShapeMode mode = (kind == ShapeId::Line && o.mode == ShapeMode::Fill)
                     ? ShapeMode::Outline : mode_for(mode);   // Line has no fill: Fill → Outline
    const Rgba8 color = ctx.colors.for_button(m_button);
    PixelBuffer& px = ctx.document.active_layer().pixels;
    raster::Target t{px /*, clip mask once M2 lands */};

    StrokeRecorder recorder(ctx.document);       // snapshot-before, see stroke_recorder.cpp:7
    RectI dirty;
    switch (kind) { /* shape::normalized_rect / vertices → the raster::draw_* calls */ }

    recorder.touch(dirty);
    recorder.commit(ctx.history);                // one PaintStrokeCommand for the whole shape
    m_dragging = false;
    ctx.ui.shape.visible = false;                // overlay disappears as the raster appears
}
```

`StrokeRecorder` is constructed *immediately before* the writes (not at pointer-down),
so a shape that never commits (zero-vertex polygon, cancelled drag) costs nothing and
cannot leave a dangling snapshot. Undo/redo then works with no new command type — the
same `PaintStrokeCommand` M1 already tests.

**Selection clipping (M2 hook, deferred as instructed).** M3 does not implement
clipping: there is no `SelectionMask` until M2. Two touch-points are reserved and
documented with `// M2 hook:` comments, mirroring M1's R6 contract:

1. `raster::Target::clip_mask` — a `const std::uint8_t*` (byte-per-pixel coverage,
   `0` = clipped away), so no header from `src/selection/` is needed before M2 exists.
   Both `ShapeTool::on_pointer_up` and `lsp::commit_text` construct their `Target`
   through one local helper each:
   ```cpp
   // shape_tool.cpp / text_tool.cpp  (file-local)  // M2 hook:
   inline const std::uint8_t* selection_mask(const CanvasDocument& doc);
   // today: return nullptr;  after M2: return doc.selection().data();   (adjust to
   // M2's real accessor name — this is the *only* place that changes)
   ```
2. `raster::bounds_*` intersected with `lsp::clip_to_selection(document, dirty)`
   (`src/selection/clip.hpp`, delivered by M2, returns `std::nullopt` when there is no
   selection) for the dirty-rect side, exactly as M1 froze it.

### Text tool — the overlay, and the rasterization decision

**Overlay (Slint).** A `TextInput` inside `ui/canvas-overlay.slint`, positioned in
canvas pixels:

```slint
in property <bool> editing <=> OverlayState.text-visible;   // local alias → `changed` works

changed editing => { if (self.editing) { text-input.focus(); } }

text-host := Rectangle {
    visible: OverlayState.text-visible;
    x: OverlayState.text-x;  y: OverlayState.text-y;
    width: text-input.width + 4px;  height: text-input.height + 4px;
    border-width: 1px;  border-color: #555;  background: transparent;

    text-swallower := TouchArea { mouse-cursor: text; }   // declared first ⇒ behind the input
    text-input := TextInput {
        x: 2px; y: 2px;
        width:  max(self.preferred-width,  20px);
        height: max(self.preferred-height, ToolOptions.font-size);
        text <=> OverlayState.text-content;               // C++ seeds "" on open, reads on commit
        font-family: ToolOptions.font-family;
        font-size:   ToolOptions.font-size;
        font-weight: ToolOptions.font-bold   ? 700 : 400;
        font-italic: ToolOptions.font-italic;
        color:       OverlayState.text-color;
        single-line: false;                                // default is TRUE — must be set
        wrap: no-wrap;
        key-pressed(event) => {
            if (event.text == Key.Escape)                          { UserState.commit-text(self.text); accept }
            else if (event.text == Key.Return && !event.modifiers.shift) { UserState.commit-text(self.text); accept }
            else                                                   { reject }   // Shift+Enter ⇒ TextInput inserts "\n"
        }
        changed has-focus => {
            if (!self.has-focus && OverlayState.text-visible) { UserState.commit-text(self.text); }
        }
    }
}
```

WHY this shape: (a) `TextInput` *has* `focus()`/`has-focus`/`accepted`, so no
`FocusScope` juggling is required; (b) interception lives in `key-pressed` rather than
`accepted`, because returning `accept` skips TextInput's default handling — that is
what guarantees Enter does *not* also insert a newline; (c) the `text-swallower`
`TouchArea` (declared before the input, hence painted/hit-tested behind it) stops a
click inside the box from falling through to the canvas `TouchArea`, which would
otherwise immediately open a second text box; (d) the `text-visible` re-check in
`changed has-focus` prevents a double commit when hiding the box clears its focus.

**Commit triggers** funnel into one C++ function `UserState.commit-text(string)`:
Enter, Escape, focus-out (all Slint-side), plus `UiState::flush_transient_state()` for
"clicked elsewhere" and "switched tools" (C++ side, reads `OverlayState.text-content`).
All four call `UiState::do_commit_text(text)`:

```cpp
void UiState::do_commit_text(const std::string& text) {
    auto& ov = m_window.global<OverlayState>();
    if (!ov.get_text_visible()) return;                 // re-entrancy guard
    sync_tool_options();                                 // font options may have just changed
    const TextOverlayState& t = m_controller.tool_ui().text;
    const Rgba8 color = t.color;                         // captured at placement
    m_controller.tool_ui().text.visible = false;
    m_last_text_visible = false;                         // forces the "" seed on next open
    ov.set_text_visible(false);
    if (text.empty()) return;
    m_controller.commit_text({iround(t.origin.x), iround(t.origin.y)}, text, color);
}
```

and `UiState::flush_transient_state()` runs at the top of **all three pointer lambdas
and the key handler** (same call pattern as M1's `sync_tool_options()`):

```cpp
void UiState::flush_transient_state() {
    if (m_window.global<OverlayState>().get_text_visible()) do_commit_text(get_text_content());
    const ETool cur = m_window.global<UserState>().get_selected_tool();
    if (cur != m_last_tool) {
        if (ITool* prev = m_controller.tool(tool_from_slint(m_last_tool)))
            prev->on_deactivate(m_controller.context());   // cancels an open polygon
        m_last_tool = cur;
    }
}
```

**Rasterization — DECISION: vendored `stb_truetype` + bundled DejaVu TTFs.**

Rejected alternatives, with reasons:

| Option | Why not |
|---|---|
| Capture Slint's own `Text` from the window | Slint's public C++ API has **no** readback of rendered pixels (the software renderer draws straight to the window surface); also can't run headless in tests. Impossible, not just awkward. |
| Slint-offscreen / `slint::Image` of a `Text` | No API rasterizes text into a `SharedPixelBuffer`. Same dead end. |
| FreeType (+ Harfbuzz) via FetchContent | Correct but heavy: multi-minute cold configure on top of the already-huge Slint fetch, CMake portability work, and shaping is overkill for a single-line/multi-line mspaint box. |
| `stb_easy_font` | Single header and zero assets, but one hard-wired low-resolution vector face, no family/size/weight fidelity → would make `font-family`/`font-bold` UI dishonest. |
| Skia text | Skia renderer is explicitly OFF (`SLINT_FEATURE_RENDERER_SKIA OFF`). |

Chosen: **`stb_truetype.h`** — the same single-header family the project already uses
(`stb_image`), it returns an 8-bit anti-aliased coverage bitmap per glyph (→ clean
alpha blend into `Rgba8`), supports arbitrary pixel sizes and full Unicode via
UTF-8 decoding, and adds **no build-system dependency**:

- `third_party/stb/stb_truetype.h` — vendored header (acquisition command in *Build
  changes*). `#define STB_TRUETYPE_IMPLEMENTATION` appears exactly once, at the top of
  `src/tools/text_tool.cpp`.
- `third_party/fonts/{DejaVuSans,DejaVuSerif,DejaVuSansMono}.ttf` — three bundled
  families (~2 MB total) + the DejaVu license file. Selected by `ToolOptions.font-family`
  through a file-local table:
  ```cpp
  struct Face { const char* family; const char* file; };
  constexpr std::array<Face, 3> kFaces{{ {"DejaVu Sans", "DejaVuSans.ttf"},
                                         {"DejaVu Serif", "DejaVuSerif.ttf"},
                                         {"DejaVu Sans Mono", "DejaVuSansMono.ttf"} }};
  ```
  Fonts are located via the `LSP_FONT_DIR` compile definition (absolute source-tree
  path baked at configure time — no runtime cwd guessing), loaded lazily and cached in
  a function-local `static std::map<std::string, LoadedFont>`. Missing directory →
  print one `stderr` warning and fall back to the first face found; if none loads,
  `commit_text` returns an empty rect (no crash).
- **Bold/italic are synthesized** from the single regular face per family: italic is a
  shear of `tan(12°) ≈ 0.21` px per px *above* the baseline
  (`dst(x,y) = cov(x - round((baseline_y - (y+y0)) * 0.21), y)`), bold is a horizontal
  dilation of `max(1, round(size_px/16))` passes (`dst[x] = max(cov[x], cov[x-1])`),
  applied *after* the shear. WHY synthetic instead of four style files per family:
  3 font files instead of ~12 (≈2 MB instead of ≈8 MB of binary in git), one code path
  for every family, and the same "faux weight" Slint's preview shows when the system
  lacks a matching face. Real style faces can be dropped into `third_party/fonts/`
  later by extending the table — no call-site changes.

Layout algorithm inside `lsp::commit_text` (single function, in this order):

1. Resolve face → `stbtt_ScaleForPixelHeight(&info, size_px)`.
2. `stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap)`; `line_h = (ascent -
   descent + line_gap) * scale`; pen starts at `(origin.x, origin.y + round(ascent*scale))`
   i.e. the click point is the **top-left of the first line box** (mspaint-like).
3. Walk UTF-8 with `stbtt_Utf8decode`; `'\n'` → pen returns to `origin.x`, `pen_y += line_h`.
4. Per codepoint: `stbtt_GetCodepointBitmap(&info, scale, scale, cp, &w, &h, &xoff, &yoff)`
   → optional italic shear → optional bold dilation → `raster::draw_glyph(Target,
   pen_x+xoff, pen_y+yoff, w, h, stride, coverage, color)` (source-over blend:
   `dst = lerp(dst, color, coverage/255)`), accumulate `dirty`, then
   `stbtt_FreeCodepointBitmap` and `pen_x += stbtt_GetCodepointHMetrics(...) * scale`.
5. `StrokeRecorder recorder(context.document); … recorder.touch(dirty);
   recorder.commit(context.history);` → one undo entry for the whole text block.

**Where the text lands:** the **active layer's `PixelBuffer`** (not the flattened
texture), top-left anchored at the click point, **under the selection clip** once the
M2 hook is wired (same `selection_mask()` helper as shapes), fully bounded by
`document.bounds()`.

### ToolOptions / new global

```slint
// ui/globals.slint
export enum ETool { NONE, PASTE, SELECTION, CROP, RESIZE, ROTATE,
                    PENCIL, FILL, SHAPE, TEXT, ERASER, DROPPER, MAGNIFY }   // + SHAPE
export enum EShapeMode  { OUTLINE, FILL, BOTH }
export enum EShapeKind  { LINE, RECTANGLE, ROUNDED_RECTANGLE, ELLIPSE, POLYGON }

export global ToolOptions {
    in-out property <length>     brush-size: 1px;          // (M1)
    in-out property <EShapeMode> shape-mode: OUTLINE;      // NEW
    in-out property <EShapeKind> shape-kind: RECTANGLE;    // NEW (shape picker)
    in-out property <length>     shape-thickness: 1px;     // NEW
    in-out property <string>     font-family: "DejaVu Sans"; // NEW
    in-out property <length>     font-size: 16px;          // NEW
    in-out property <bool>       font-bold: false;         // NEW
    in-out property <bool>       font-italic: false;       // NEW
}

// NEW global — transient view state for the canvas overlay.
// WHY a 4th global instead of DocumentState/ToolOptions: plan.md §6 assigns neither
// preview geometry (document facts) nor drag state (tool parameters) a home, and M1
// set the precedent that state is grouped by meaning, not by count. It is still plain
// globals-only state (never local window state), so ui_state.cpp can read/write it —
// the actual UI convention being enforced.
export global OverlayState {
    in property <bool>    shape-visible: false;       // C++-written (like DocumentState.can-undo)
    in property <string>  shape-commands: "";
    in property <bool>    shape-draw-fill: false;
    in property <bool>    shape-draw-outline: true;
    in property <color>   shape-color: black;

    in property <bool>    text-visible: false;
    in property <length>  text-x: 0px;
    in property <length>  text-y: 0px;
    in property <color>   text-color: black;
    in-out property <string> text-content: "";        // in-out: TextInput edits it
}
```

`UserState` gains exactly two callbacks: `callback commit-text(string);` and
`callback double-click();`. `DocumentState` gains `canvas-x`/`canvas-y` **only if M2
has not delivered them yet** (see *Milestone dependencies* → coordinate contract).

### Options strip — where and why

**Decision: a new strip row directly under the ribbon, inside the Home tab**
(`ui/bar/bar-options.slint` → `ToolOptionsBar`, instantiated in `topbar.slint`
beneath `HomeBar`), *not* a popup from the Shapes group.

Why: font size/family must be adjustable **while a text box is open** and thickness
while a drag preview is on screen — a popup you have to re-open hides exactly the
controls you are watching, and mspaint shows these as a persistent bar. Why inside the
Tab rather than inside `HomeBar`: `Bar` hard-fixes `height: Styles.bar-height` (110 px),
so a second row cannot live inside it; putting the strip in the Tab keeps it tied to
the Home tab (where tools are chosen) and out of the View tab, and `visible: false`
elements are skipped by Slint layouts, so the strip costs nothing when another tool is
active.

```slint
// ui/bar/bar-options.slint
export component ToolOptionsBar inherits Rectangle {
    in property <bool> show-shape: UserState.selected-tool == ETool.SHAPE;
    in property <bool> show-text:  UserState.selected-tool == ETool.TEXT;
    visible: show-shape || show-text;
    height: Styles.options-bar-height;                 // new Styles entry: 36px
    background: #f5f6f7;  border-width: 1px; border-color: #eee;   // matches Bar

    HorizontalLayout {
        // SHAPE: thickness dropdown (PopupWindow + BarSizeDropdownItem rows 1..32 subset)
        // TEXT : font-family dropdown (3 BarDropdownItems → ToolOptions.font-family)
        //        font-size    dropdown (BarSizeDropdownItem 8/10/12/14/16/20/24/32/48 px)
        //        Bold / Italic toggle chips (local `OptionsToggle` component)
    }
}
```

`topbar.slint`:

```slint
Tab {
    title: "First";
    VerticalLayout {
        HomeBar {}
        ToolOptionsBar {}
    }
}
```

(The `Rectangle { background: red; }` debug fill already in `TopBar` is left alone —
it stays behind and is not M3 scope.)

### Wiring the dead Shapes buttons

`ImageButtonHorizontal` has only `clicked`; both dead buttons carry a baked `▾` in
their label. The two-zone pattern already exists as `ImageButtonVerticalDropdown`
(`clicked` = body, `clicked_dropdown` = arrow) → add its horizontal twin
`ImageButtonHorizontalDropdown` to `ui/components/image-buttons.slint` and replace the
two dead rows (all three rows fit: 24 px each + 2×2 px spacing = 76 px ≤ the 86 px
available in `BarItemGroup`).

| Control | `clicked` (body) | `clicked_dropdown` (▾) |
|---|---|---|
| `Outline ▾` (`shape-outline.png`) | `ToolOptions.shape-mode = OUTLINE; UserState.selected-tool = ETool.SHAPE;` | select SHAPE, `.show()` the shared-mode popup |
| `Fill ▾` (`shape-fill.png`) | `ToolOptions.shape-mode = FILL;  UserState.selected-tool = ETool.SHAPE;` | select SHAPE, `.show()` the shared-mode popup |
| `<current kind> ▾` (**new**, `shape-outline.png` as the generic shape glyph) | `UserState.selected-tool = ETool.SHAPE;` | `.show()` the shape-picker popup |

Both mode popups are instances of one local `component ShapeModePopup inherits
PopupWindow` (two instances so the proven "x/y bound to the button" pattern is kept —
there is no need to mutate popup geometry imperatively), each listing
`Outline` / `Fill` / `Outline and Fill` with `focused: ToolOptions.shape-mode == …`.
The picker popup lists the five `EShapeKind` values with
`focused: ToolOptions.shape-kind == …`.

**Icons — exact answer.** Present: `icons/shape-outline.png`, `icons/shape-fill.png`,
`icons/text.png`, `icons/size.png`. **Missing: all five shape-kind glyphs, a
"both"-mode glyph, and bold/italic glyphs.** M3 creates **no new icon files**:
mode items reuse the two existing PNGs (the third item, "Outline and Fill", is
text-only), the kind picker items are **text-only `BarDropdownItem`s** (their `icon`
property is simply not set — an empty `image` renders nothing), and the picker button
reuses `shape-outline.png`. If Slint logs a warning for the unset image at runtime,
set `icon: @image-url("../../icons/shape-outline.png")` on those rows as a placeholder.
A later asset pass may add `icons/shape-line.png`, `shape-rectangle.png`,
`shape-rounded-rect.png`, `shape-ellipse.png`, `shape-polygon.png` — none are required
by this milestone.

**Text button:** already correct on the Slint side
(`bar-home.slint:163-167` sets `ETool.TEXT`); M3 only has to (a) add `SHAPE` to
`ETool`, (b) add `case ETool::SHAPE: return ToolId::Shape;` to `tool_from_slint`, and
(c) register `ToolId::Text` + `ToolId::Shape` in `ToolRegistry` — after (c) the
existing button starts working with no markup change.

### Cursor & overlay invalidation

- Cursor: `appwindow.slint`'s `touch` gains
  `mouse-cursor: UserState.selected-tool == ETool.SHAPE ? crosshair
               : UserState.selected-tool == ETool.TEXT  ? text : default;`
  (both enum members exist in Slint's `MouseCursor`).
- Hover moves: Slint's `moved()` **only fires while pressed** (documented), so the
  `pointer-event` handler gains a `PointerEventKind.move` branch — but guarded to avoid
  double-dispatch with `moved` during drags:
  ```slint
  pointer-event(event) => {
      if (event.kind == PointerEventKind.down) { focus-scope.focus(); UserState.pointer-down(...); }
      else if (event.kind == PointerEventKind.move) {
          if (!touch.pressed) { UserState.pointer-move(touch.mouse-x - canvas.x, touch.mouse-y - canvas.y); }
      }
      else if (event.kind == PointerEventKind.up || event.kind == PointerEventKind.cancel) { UserState.pointer-up(...); }
  }
  double-clicked => { UserState.double-click(); }
  ```
  (`moved =>` stays as-is for pressed drags — including right-drags, which set the
  internal grab but not `pressed`.)
- `InputRouter::pointer_move(ToolId, pos)` dispatches to `m_active` when a drag is in
  flight, otherwise to `registry.find(tool)` — that is what makes hover rubber-banding
  work before any button is pressed. `BrushStrokeTool::on_pointer_move` already
  early-returns without an engine, so existing tools are unaffected.
- `UiState::refresh()` becomes: `sync_overlays(); …can-undo/can-redo…; flush();
  upload()` — `sync_overlays()` **must** run before the `flush()` early-return.

## File-by-file changes

### New files

- **`src/core/raster.hpp` / `src/core/raster.cpp`** — `lsp::raster::Target` +
  the `draw_*` / `bounds_*` functions listed in Design, implemented with the
  algorithms documented there. No Slint, no tools, no history includes. ~350 lines.
- **`src/tools/shape_tool.hpp` / `src/tools/shape_tool.cpp`** — `ShapeId`,
  `ShapeMode`, `ShapeOptions`, `ShapePreview`, `namespace lsp::shape` helpers
  (`normalized_rect`, `bounds`, `svg_commands`), `ShapeTool` (drag + polygon state
  machine, `on_key_pressed`/`on_double_click`/`on_deactivate`, `commit` via
  `StrokeRecorder`). `.cpp` includes `tools/tool_context.hpp`,
  `tools/stroke_recorder.hpp`, `core/raster.hpp`.
- **`src/tools/text_tool.hpp` / `src/tools/text_tool.cpp`** — `TextOptions`,
  `TextOverlayState`, `TextTool` (open overlay), `lsp::commit_text(...)`; the file
  starts with `#define STB_TRUETYPE_IMPLEMENTATION` + `#include "stb_truetype.h"`,
  holds `kFaces`, the lazy face cache, the synthetic bold/italic helpers
  (`apply_italic_shear(std::uint8_t* dst, int w, int h, float shear)`,
  `apply_bold_dilate(std::uint8_t* dst, int w, int h, int passes)`) and the layout loop.
- **`ui/bar/bar-options.slint`** — `ToolOptionsBar` + a local `OptionsToggle`
  component (rectangle chip with `label`, `active` focus styling copied from
  `ImageButtonBase`, `clicked` callback) used for Bold/Italic.
- **`tests/raster_test.cpp`** — line gap-free-ness on fast diagonals, ellipse outline
  never erases pixels underneath (draw over a filled rect, assert untouched inner
  pixels), rounded-rect corner radius, polygon fill scanline correctness, `Target`
  clip-mask honouring (mask byte 0 → pixel unchanged), bounds helpers.
- **`tests/shape_tool_test.cpp`** — `shape::svg_commands` output for each kind
  (string equality on small fixtures), `shape::normalized_rect`, and an end-to-end
  `ShapeTool` drag on a `CanvasDocument` asserting: exactly one history entry, layer
  pixels changed only inside the expected rect, undo restores byte-for-byte.
- **`tests/text_raster_test.cpp`** — skips (prints `skipped`) when `LSP_FONT_DIR`
  holds no TTF; otherwise renders `"A\nB"` and asserts (a) pixels changed inside the
  expected block, (b) nothing outside, (c) `commit_text("")` returns an empty rect and
  pushes no history entry, (d) `bold=true` produces strictly more set pixels than
  `bold=false`.
- **`third_party/stb/stb_truetype.h`** — vendored single header (see Build changes).
- **`third_party/fonts/DejaVuSans.ttf`, `DejaVuSerif.ttf`, `DejaVuSansMono.ttf`,
  `LICENSE.txt`** — bundled faces + license.

### Modified files

- **`src/tools/tool.hpp`** — add `ToolId::Shape` (after `Fill`, keeping the 1:1
  mirror with `ETool`); add the three defaulted `ITool` virtuals.
- **`src/tools/tool_context.hpp`** — include `shape_tool.hpp` + `text_tool.hpp`; add
  `ToolUiState`; add `ShapeOptions& shape_options;` and `ToolUiState& ui;` to
  `ToolContext`.
- **`src/tools/tool_registry.cpp`** — register
  `ToolId::Shape → ShapeTool` and `ToolId::Text → TextTool` (+ the two includes).
- **`src/app/input_router.hpp/.cpp`** — `pointer_move(ToolId, Point2f)`; add
  `key(ToolId, std::string_view)`, `double_click(ToolId)`, `deactivate(ToolId)`.
- **`src/app/app_controller.hpp/.cpp`** — new members `m_shape_options`,
  `m_text_options`, `m_tool_ui` **declared before `m_context`**; extended context
  init list; accessors; `AppController::commit_text(...)`.
- **`src/ui/ui_state.hpp`** — members `ETool m_last_tool = ETool::NONE;` and
  `bool m_last_text_visible = false;`; private methods
  `sync_tool_options()` (M1), `sync_overlays()`, `flush_transient_state()`,
  `do_commit_text(const std::string&)`.
- **`src/ui/ui_state.cpp`** —
  - `tool_from_slint`: add `case ETool::SHAPE: return ToolId::Shape;` (and keep the
    switch `default:`-free so `-Wswitch` keeps checking it).
  - helpers `shape_kind_from_slint(EShapeKind)`, `shape_mode_from_slint(EShapeMode)`
    mirroring `tool_from_slint`'s style.
  - extend `sync_tool_options()` with the `ToolOptions → m_controller.shape_options()`
    (kind/mode, `thickness = clamp(lround(shape-thickness), 1, 32)`) and
    `ToolOptions → m_controller.text_options()` (family `to_string()`,
    `size_px = clamp(lround(font-size), 1, 512)`, bold, italic) block.
  - `flush_transient_state()` called at the top of `on_pointer_down`,
    `on_pointer_move`, `on_pointer_up`, `on_key_pressed`.
  - `on_pointer_move` passes `tool_from_slint(state.get_selected_tool())` to
    `InputRouter::pointer_move`.
  - new bindings: `on_commit_text(...)`, `on_double_click(...)`.
  - `on_key_pressed`: early-return while a text box is open or
    `input().active()` (M1 R9); `"\n"` / `"\u001b"` → `input().key(...)` + `refresh()`
    and return; otherwise the existing `lookup_shortcut` path unchanged.
  - `refresh()`: first line `sync_overlays();` (before the M1 can-undo/can-redo block
    and before `flush()`'s early-return).
  - `sync_overlays()`: the 9 `OverlayState.set_*` calls from Design, plus
    false→true transition detection for `ui.text.visible` that seeds
    `set_text_content("")` and updates `m_last_text_visible`.
- **`ui/globals.slint`** — `ETool.SHAPE`; new `EShapeMode`/`EShapeKind`; the seven
  new `ToolOptions` properties; new `OverlayState` global; `UserState` gains
  `commit-text(string)` and `double-click()`.
- **`ui/components/image-buttons.slint`** — new `ImageButtonHorizontalDropdown`
  (mirrors `ImageButtonVerticalDropdown`: `focused1/focused2`, `text`, `icon`,
  `clicked` ↔ body, `clicked_dropdown` ↔ a right-hand `▼` strip, `any-hovered`
  force-hover).
- **`ui/bar/bar-home.slint`** — Shapes group: local `component ShapeModePopup
  inherits PopupWindow`, two mode-popup instances, one `shape-picker-popup`, the two
  dead rows replaced by `ImageButtonHorizontalDropdown`s (body sets the mode and
  selects `ETool.SHAPE`, arrow opens the popup) plus the third picker row; imports
  `EShapeMode`, `EShapeKind`. Also, while in this file: do **not** copy the legacy
  `close-on-click:` (line 327) into new popups — use `close-policy` (M1 note).
- **`ui/canvas-overlay.slint`** — M3 adds the `shape-preview` `Path`, the text editor
  block, and (if M2 hasn't created the file yet) the minimal shell:
  ```slint
  export component CanvasOverlay inherits Rectangle {
      clip: true;
      // … M2: marching ants + resize handles …
      // … M3: shape-preview + text-host, as designed above …
  }
  ```
- **`ui/appwindow.slint`** — import `OverlayState` (+ `ToolOptions`, `ETool` if not
  already); restructure so `canvas`, M1's `brush-outline`, `CanvasOverlay` are children
  of one shared `Rectangle` (M2's coordinate contract) with `CanvasOverlay` declared
  **last** (it must paint above both); add the `pointer-event move` branch,
  `double-clicked`, and the `mouse-cursor` binding. **Fallback if M2 is not merged
  yet**: keep today's `touch`/`canvas` nesting, add `CanvasOverlay` as the last child of
  `touch` with `x: canvas.x; y: canvas.y; width/height` = canvas size, and add
  `in-out property <length> canvas-x: 4px; canvas-y: 4px;` to `DocumentState` bound to
  `canvas.x/y` (delete the fallback when M2 lands).
- **`ui/topbar.slint`** — first `Tab` becomes `VerticalLayout { HomeBar {}
  ToolOptionsBar {} }`; import `ToolOptionsBar`.
- **`ui/styles.slint`** — `out property<length> options-bar-height: 36px;`.
- **`CMakeLists.txt`** — see Build changes.

## Implementation order

Each numbered step ends in a compiling, runnable build.

1. **Raster core.** `src/core/raster.{hpp,cpp}` + `tests/raster_test.cpp` +
   `add_executable`/`ls_paint_tests` entries → `cmake --build build -j && ctest`.
2. **Enum/registry plumbing (no behavior yet).** `ToolId::Shape`, `ETool.SHAPE`,
   `EShapeMode`/`EShapeKind`, `case ETool::SHAPE` in `tool_from_slint`; empty
   `ShapeTool`/`TextTool` classes (`on_pointer_down {}`) registered in `ToolRegistry`.
   Builds clean under `-Wswitch`.
3. **State plumbing.** `ShapeOptions`/`ToolUiState`/`ShapePreview`/`TextOverlayState`
   into `tool_context.hpp`; new `AppController` members + accessors (declaration order!);
   extend `sync_tool_options()`; add `OverlayState` + `sync_overlays()` +
   `refresh()` first-line hook. Still no visible change.
4. **Drag shapes, committed.** `shape::normalized_rect`/`bounds`, the `switch` in
   `ShapeTool::on_pointer_up` writing through `StrokeRecorder`. Verify by dragging: the
   shape appears only on release, one Ctrl+Z removes it. (No preview yet.)
5. **Overlay + preview.** `ui/canvas-overlay.slint` (shell if needed), the
   `shape-preview` `Path`, `shape::svg_commands`, `update_preview`, appwindow
   `move`-branch, `InputRouter::pointer_move(ToolId, …)`. Now the preview follows the
   pointer.
6. **Polygon.** `on_key_pressed` / `on_double_click` / `on_deactivate`,
   `InputRouter::key/double_click/deactivate`, `UserState.double-click`,
   `flush_transient_state()`.
7. **Bar wiring.** `ImageButtonHorizontalDropdown`, `ShapeModePopup`, picker popup,
   three rows in `bar-home.slint`. Buttons + popups now drive `ToolOptions.shape-*`.
8. **Options strip.** `styles.slint` entry, `ui/bar/bar-options.slint`,
   `topbar.slint` Tab restructure, thickness + font rows.
9. **Fonts.** `third_party/stb` + `third_party/fonts` + CMake include/define → the app
   still builds (nothing links them yet).
10. **Text tool.** `text_tool.{hpp,cpp}`, the overlay `TextInput`, `UserState.commit-text`,
    `do_commit_text`, `TextTool::on_pointer_down`, `ToolId::Text` registration.
11. **M2 clip hook** (only if M2 has landed): point the two `// M2 hook:` helpers at
    `clip_to_selection` / the selection mask.
12. **Tests + manual pass.** `tests/shape_tool_test.cpp`, `tests/text_raster_test.cpp`,
    `run_*_tests()` entries in `tests/test_main.cpp`, full checklist below.

## Slint specifics

- **Globals** (`ui/globals.slint`): `ETool` += `SHAPE`; new `EShapeMode`,
  `EShapeKind`; `ToolOptions` += `shape-mode`, `shape-kind`, `shape-thickness`
  (`length`), `font-family` (`string`), `font-size` (`length`), `font-bold`,
  `font-italic` (`bool`); new global `OverlayState` (9 properties, Design above);
  `UserState` += `commit-text(string)`, `double-click()`. Everything C++-written in
  `OverlayState` is declared `in` (matching M1's `DocumentState.can-undo` style);
  only `text-content` is `in-out` because the `TextInput` edits it.
- **New `.slint` files**: `ui/bar/bar-options.slint` (`ToolOptionsBar`) and, if M2 has
  not created it, the `ui/canvas-overlay.slint` shell. `ui/bar/bar-home.slint` gains a
  *local* (non-exported) `ShapeModePopup` component; `ui/components/image-buttons.slint`
  gains `ImageButtonHorizontalDropdown`.
- **Dead buttons wired**: Shapes `Outline ▾`/`Fill ▾` → `ToolOptions.shape-mode`
  (body = one-click set, arrow = full 3-mode popup); new picker row →
  `ToolOptions.shape-kind`; **Text button needs no markup change** (already
  `ETool.TEXT`, bar-home.slint:163-167) — it starts working the moment
  `ToolId::Text` is registered. Still dead after M3: Brushes, Edit Colors,
  cut/copy/paste, selection dropdown ops, Resize/Rotate/Magnify/Crop (their `ETool`s
  have no registered tool).
- **Icons**: no new PNGs; mode rows reuse `shape-outline.png`/`shape-fill.png`;
  kind rows are text-only; picker button reuses `shape-outline.png` (fallback if an
  unset `image` warns: give those rows `shape-outline.png` too).
- **Popup convention**: `PopupWindow` declared as a sibling before the layout inside
  `BarItemGroup`, positioned by `x:`/`y:` bindings off the button, opened with
  `.show()`; use `close-policy`, never the deprecated `close-on-click`.
- **Overlay/preview conventions**: preview is `Path` (never pixels); `commands` is
  bound from a global string (the docs forbid reading `commands` in an expression, but
  binding *to* it is exactly the supported use); the text editor is a real `TextInput`
  with `single-line: false` (its default is `true`).

## Build changes

```cmake
# 1) add_executable(ls_paint …) — append three sources:
    src/core/raster.cpp
    src/tools/shape_tool.cpp
    src/tools/text_tool.cpp

# 2) after target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/src):
target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_compile_definitions(ls_paint PRIVATE
    LSP_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")

# 3) inside if(LSP_BUILD_TESTS) — append the same three sources plus the new tests
#    (ls_paint_tests already builds core/history/tools sources per M1):
        src/core/raster.cpp
        src/tools/shape_tool.cpp
        src/tools/text_tool.cpp
        tests/raster_test.cpp
        tests/shape_tool_test.cpp
        tests/text_raster_test.cpp
    target_include_directories(ls_paint_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
    target_compile_definitions(ls_paint_tests PRIVATE
        LSP_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")
    # and add run_raster_tests()/run_shape_tool_tests()/run_text_raster_tests()
    # calls to tests/test_main.cpp's dispatch (M1's harness convention).
```

Assets (one-time, outside CMake):

```bash
mkdir -p third_party/stb third_party/fonts
curl -fsSL -o third_party/stb/stb_truetype.h \
    https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
# DejaVu fonts (from the system if present, else the official release zip):
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
   /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \
   /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf third_party/fonts/ 2>/dev/null || {
  curl -fsSL -o /tmp/dejavu.zip \
      https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip
  unzip -j /tmp/dejavu.zip '*/ttf/DejaVuSans.ttf' '*/ttf/DejaVuSerif.ttf' \
      '*/ttf/DejaVuSansMono.ttf' -d third_party/fonts/
}
# ship the DejaVu license alongside (third_party/fonts/LICENSE.txt) — it requires it.
```

WHY vendored rather than `FetchContent`: `nothings/stb` has no root `CMakeLists.txt`,
so `FetchContent_MakeAvailable` degenerates to population-only (and
`FetchContent_Populate(<name>)` is deprecated by CMake ≥ 3.30 / CMP0169), while plan.md
§4's FetchContent rule exists to avoid *re-vendoring things already vendored in
image-viewer*. One self-contained header with a single `STB_TRUETYPE_IMPLEMENTATION`
translation unit is the library's documented usage and adds zero configure time.

## Verification

- Build & tests:
  ```
  cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```
  Warning-clean under `-Wall -Wextra` (note: a missing `case ETool::SHAPE` fails this).
- Manual checklist:
  1. Select Shape → drag a rectangle: outline follows the cursor live; **hold the drag
     and press Ctrl+Z → nothing happens to the buffer**; release → shape appears where
     the preview was; one Ctrl+Z removes it, Ctrl+Y restores it.
  2. Repeat for Line, Rounded rect, Ellipse (left-drag = Color 1, right-drag = Color 2 →
     preview color matches the committed color).
  3. Modes: `Outline ▾` body → hollow shape; `Fill ▾` body → solid; arrow → pick
     "Outline and Fill" → both; popup highlights the current mode.
  4. Thickness 1/4/12 from the options strip: preview stroke and committed stroke both
     change, live, without restarting the drag.
  5. Picker: switch Rectangle → Ellipse → Polygon → Line; button label follows.
  6. Polygon: 4 clicks with a rubber band; move the mouse (no button) → band follows;
     double-click → closed polygon committed; repeat and press Enter; repeat and click
     on the first vertex; repeat and press Escape → preview vanishes, no pixels, Ctrl+Z
     does nothing. Switch tools mid-polygon → polygon discarded.
  7. Text: click canvas → focused empty box with a border; type → caret works; change
     font size/family/Bold/Italic in the strip → live update; Enter → glyphs appear in
     the layer at the click point, box closes, one Ctrl+Z removes all of it.
  8. Text: Escape commits; clicking elsewhere commits *then* starts a new box at the new
     point; switching to Pencil on the next canvas click commits first; `Shift+Enter`
     inserts a newline and does not commit; empty box + Escape → nothing, no history.
  9. Text: `"héllo → ✓"` (multi-byte) renders the right glyphs; a 3-line paste renders 3
     lines; text partially outside the canvas is clipped, not crashed.
  10. Cursors: crosshair over the canvas with Shape, text-cursor with Text, default with
      Fill; text-box area shows the text cursor.
  11. Undo of a shape and of text each produce exactly one entry; redo works; a stroke
      drawn after undo clears redo (M1 invariant still holds).
  12. Options strip visible only for SHAPE/TEXT; with Pencil selected the ribbon is
      back to 110 px and the workspace does not shift horizontally.
- Unit-testable pure logic: `raster::*` (incl. clip-mask + non-destructive outlines),
  `shape::svg_commands`/`normalized_rect`/`bounds`, ShapeTool end-to-end
  (one-command-per-shape, undo restores bytes), `commit_text` (empty input, bounds,
  bold>regular pixel count). Not unit-testable (manual only): Slint overlay plumbing —
  `OverlayState` bindings, `TextInput` focus/Enter/Escape, popup wiring, cursor.

## Risks & edge cases

- **`Path` coordinate scaling**: `Path.width/height` default to 100 % of the parent and
  scale the path bbox into it. Mitigated by the explicit `viewbox` == element size
  (identity transform). If the preview still lands off-by-scale at 1:1, verify with
  checklist 1 and fall back to `width: 0px; height: 0px;` (documented as "no fit
  scaling"); if `Path` is unsupported by the software renderer for some reason, the
  last-resort fallback is per-shape Slint elements (a `Rectangle` with
  `border-width`/`border-radius` for rect/rounded-rect, `Path` for the rest).
- **Preview vs raster mismatch**: the overlay is anti-aliased and stroke-centered while
  the raster is hard-pixel and outline-inward — expect ≤2 px disagreement on thick
  outlines. Do not "fix" by moving the raster to AA (plan §10 keeps everything on the
  software renderer with no AA for shapes).
- **Text overlay focus vs window shortcuts**: while the box is open the appwindow
  `FocusScope` may still receive bubbled keys; guarded by the
  `if (ui.text.visible) return;` at the top of the key handler (Ctrl+Z during editing is
  ignored on purpose — the box owns the keyboard). Verify in checklist 7/8.
- **Double commit**: `changed has-focus` can fire when hiding the box clears focus →
  guarded by `OverlayState.text-visible` (Slint) *and* `get_text_visible()` (C++)
  before any rasterization.
- **Tool switch with an open text box**: ribbon clicks do not move Slint focus, so the
  box can outlive the tool for a moment; `flush_transient_state()` commits it before the
  next pointer/key event dispatches. If a future UI element *does* steal focus, the
  focus-out path commits anyway — both paths are idempotent.
- **Popup key swallowing**: same open question M1 flagged for the Size dropdown — if a
  `PopupWindow` eats Enter/Esc while a polygon is open, close the popup on the key or
  forward `key-pressed` from it; check during checklist 6.
- **Unset `BarDropdownItem.icon`**: expected to render nothing; if Slint warns, apply
  the documented placeholder image (Slint specifics → icons).
- **Font fidelity**: the Slint preview uses the *system* font matching
  `ToolOptions.font-family`, the raster uses the bundled TTF — same family name, so
  glyph shapes match where DejaVu is installed, but Slint's real bold face vs our
  synthetic bold can differ slightly in weight. Documented, accepted.
- **`font-size` vs canvas px**: sizes are canvas pixels because there is no zoom yet;
  M5's `ViewTransform` must scale the *overlay* (it will — the overlay lives in the
  canvas-origin Rectangle), while `sync_tool_options()` keeps reading canvas px.
- **Huge inputs**: a 512 px font or a multi-thousand-character box rasterizes in one
  commit (a few ms); history stores only the dirty rect. Pathological (text larger than
  the canvas) is clipped by `bounds`.
- **Polygon with collinear/duplicate vertices**: duplicate suppression at 3 px and the
  8 px close radius prevent zero-length edges; scanline fill handles collinear edges by
  construction.
- **Right-button drags**: `TouchArea.pressed` tracks only the left button, so right-drag
  moves arrive via the `pointer-event move && !pressed` branch — covered, but verify
  right-drag preview in checklist 2.

## Milestone dependencies

- **Preconditions**: M0 (`plans/m0-foundation.md`) and M1 (`plans/m1-core-drawing-tools.md`)
  fully implemented — `CanvasDocument`, `HistoryStack`, `StrokeRecorder`,
  `InputRouter`, `UiState::refresh` + `sync_tool_options` + `handle_shortcut` +
  `active()`, the `tests/` target, and the `UserState`/`DocumentState`/`ToolOptions`
  globals all exist today. **M2 (Selection & transform) is the direct predecessor** and
  is assumed to deliver three contracts this plan plugs into:
  1. **Coordinate contract (required by the overlay).** `ui/canvas-overlay.slint`
     exporting `CanvasOverlay`, placed in the same parent `Rectangle` as the raster
     `Image`, with `DocumentState.canvas-x/canvas-y` as the canvas origin; the overlay's
     own coordinate space is *canvas pixels with origin at the image's top-left*, which
     is what `ShapePreview.commands`, `OverlayState.text-x/y` and
     `touch.mouse-x - canvas.x` all assume. appwindow.slint must declare `CanvasOverlay`
     **after** M1's `brush-outline` so it paints above it. If M2 has not landed when M3
     is built, use the documented fallback in *File-by-file changes* →
     `ui/appwindow.slint` (identical overlay math, `canvas-x/y` defaults to the current
     hard-coded 4 px).
  2. **Clip contract (deferred, as required)**: `lsp::clip_to_selection(const
     CanvasDocument&, RectI)` in `src/selection/clip.hpp` (frozen by M1's R6) plus an
     accessor for the 8-bit mask. M3 touches this in exactly two `// M2 hook:` helpers
     (`selection_mask()` in `shape_tool.cpp` and `text_tool.cpp`) and in the dirty-rect
     intersection — no raster-function signature changes.
  3. `ESelectionType`/selection UI from M2 is *not* otherwise used by M3.
- **Cross-cutting requirements**: top-left alignment (#1) holds — the overlay shares the
  canvas origin exactly, so nothing is centered or offset. The 8-handle drag-resize
  (#2) belongs to M2 (`canvas-overlay.slint`); M3 only guarantees its two additions
  (`shape-preview`, `text-host`) live inside the same clipped overlay Rectangle and do
  not cover or intercept the handles (they are drawn by M2 *after* these elements, and
  the text box's swallow-`TouchArea` is sized to the text box only). M5 (zoom) must
  scale the whole overlay Rectangle; because preview coordinates are canvas px inside
  that Rectangle, no M3 code changes for zoom.
- **This milestone delivers to later milestones**:
  - **M4 (File I/O)** — shape/text output is ordinary layer pixels, so save/clipboard
    need no awareness of either tool.
  - **M5 (View & status)** — re-anchors/scales the overlay and the mouse-cursor mapping
    (`canvas_px = (screen − canvas_origin) / zoom`); the status bar's cursor-position
    readout can reuse `InputRouter`'s hover dispatch added here.
  - **M6 (brush engine/presets)** — `raster::Target` is the pattern to copy if brush
    stamps ever need selection clipping.
  - **Any milestone adding tests** — append sources + `run_*_tests()` entries to the
    existing `ls_paint_tests` target (no new framework).
- **Explicitly not in M3**: shape corner-radius UI, dash/style presets, text editing of
  already-committed text (re-editing rasterized glyphs), text selection/alignment
  options, shape-icons assets, `fill-tolerance`, zoom-aware preview scaling, layers.
