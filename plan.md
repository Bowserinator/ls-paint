# LS Paint — Implementation & Architecture Plan

## 1. Where things stand today

The repo is currently a UI mockup wired to almost no real logic:

- `src/main.cpp` creates the window, pushes a hardcoded color palette, and on every
  click/drag paints a single hardcoded black pixel into a `SharedPixelBuffer` — regardless
  of which tool, color, or brush size is selected.
- `ui/globals.slint` (`UserState`) has `selected-tool` / `selection-type` enums and a color
  palette, but nothing downstream reads `selected-tool` to change behavior.
- The Home bar (`ui/bar/bar-home.slint`) has buttons for every mspaint tool (selection,
  crop, resize, rotate, pencil, fill, text, eraser, dropper, magnify, shapes, brushes,
  size, colors, clipboard) but only `selected-tool`/`selection-type` assignment is wired;
  cut/copy/paste, select-all/invert/clear/transparent, resize, rotate, shapes, brushes,
  and size have no backing behavior.
- The View bar (`ui/bar/bar-view.slint`) is two placeholder buttons.
- There is no document model: no notion of image size, no undo/redo, no
  selection mask, no file I/O (new/open/save), no clipboard integration, no status bar.
- There is exactly one 600×700 in-memory `Rgb8Pixel` buffer with no alpha, no layers,
  and no separation between "what's on screen" and "what tool logic operates on."

None of this is a criticism of the prototype — it correctly established the Slint
software-renderer plumbing (see the renderer-migration patch) and the visual chrome.
This plan is about building the actual application behind that chrome, in a shape that
doesn't have to be re-architected every time a new tool, blend mode, or brush type shows up.

## 2. Goals

1. Implement every tool/menu item the UI already advertises (icons exist for all of them).
2. Introduce a real document/rendering model: layered, RGBA8, undoable, selectable.
3. Keep the Slint side thin — it renders state and forwards input events; all editing
   logic, hit-testing math, and compositing lives in C++ and is unit-testable without a window.
4. Design the core data model so that **layers, blend modes, layer filters, and custom
   brushes** — none of which are required for a v1 mspaint clone — can be added later as
   pure additions (new enum values / new registered strategies), not refactors.
5. Keep the render path efficient: dirty-rect based recompositing and texture upload
   instead of re-copying the whole canvas per mouse-move event.

## 3. High-level architecture

```
                         ui/*.slint  (Slint markup: layout, chrome, overlays)
                               |  properties / callbacks
                               v
                    src/ui/ui_state.{hpp,cpp}   <- binds Slint globals to C++
                               |
                               v
                    src/app/app_controller.*    <- owns everything below, top-level orchestration
                       |         |         |
                       v         v         v
                 ToolRegistry  History   DocumentIO / Clipboard
                       |         |
                       v         v
                 core::CanvasDocument  <-- owns LayerStack + SelectionMask + canvas size
                       |
                       v
                 core::Compositor  --(dirty rects)-->  render::SlintBridge --> Slint::Image
```

Rough separation of concerns:

| Layer | Responsibility | Knows about Slint? |
|---|---|---|
| `ui/*.slint` | Layout, styling, popups, forwarding pointer/keyboard events | n/a |
| `src/ui/` | Translate Slint globals/callbacks <-> plain C++ calls | Yes (only layer that does) |
| `src/app/` | Wiring: which tool is active, dispatch input to it, own undo stack | No |
| `src/tools/` | Per-tool behavior (pencil, fill, selection, shapes, text, ...) | No |
| `src/brushes/` | Brush stamping engine + presets | No |
| `src/core/` | Document model: layers, blend modes, compositing, canvas ops | No |
| `src/selection/` | Selection mask representation + boolean ops | No |
| `src/history/` | Command pattern undo/redo | No |
| `src/io/` | File load/save, clipboard | No |
| `src/render/` | Turns a composited `PixelBuffer` into a `slint::Image` efficiently | Yes (isolated) |

This mirrors the pattern already used in `image-viewer` (small single-purpose headers/
`.cpp` files under topic directories, a thin `AppController`, UI-facing code kept in its
own folder) rather than the current one-giant-`main.cpp` approach.

## 4. Proposed source layout

```
src/
  main.cpp                          # argv parsing + AppWindow::create() + AppController wiring only

  app/
    app_controller.hpp/.cpp         # top-level glue: owns Document, History, ToolRegistry, IO
    input_router.hpp/.cpp           # PointerEvent -> active ITool, keyboard shortcut table

  core/
    types.hpp                       # Point2i/Point2f, RectI, Rgba8, Length helpers
    pixel_buffer.hpp/.cpp           # raw RGBA8 buffer: get/set/blit/sub-rect copy, resize
    layer.hpp/.cpp                  # PixelBuffer + name + opacity + blend mode + visible + locked
    blend_modes.hpp/.cpp            # BlendMode enum + BlendFn registry (extensible)
    layer_stack.hpp/.cpp            # ordered layers + active layer index + add/remove/move/merge
    canvas_document.hpp/.cpp        # LayerStack + canvas size + SelectionMask + dirty tracking
    compositor.hpp/.cpp             # LayerStack (+ filters) -> flattened PixelBuffer, dirty-rect aware
    filters/
      filter.hpp                    # IFilter interface: apply(PixelBuffer&, RectI region, params)
      builtin_filters.hpp/.cpp      # grayscale, invert, brightness/contrast, hue/saturation, blur
      filter_registry.hpp/.cpp      # name/id -> IFilter factory (extensible)

  selection/
    selection_mask.hpp/.cpp         # 8-bit coverage mask same size as canvas
    selection_builders.hpp/.cpp     # rect / freehand(lasso) / polygon -> SelectionMask
    selection_ops.hpp/.cpp          # invert / select-all / clear / make-transparent

  history/
    command.hpp                     # ICommand { do_(); undo(); merge_with(next) }
    history_stack.hpp/.cpp          # undo/redo stacks, coalesces consecutive stroke commands
    commands/
      paint_stroke_command.hpp/.cpp
      selection_change_command.hpp/.cpp
      canvas_resize_command.hpp/.cpp
      canvas_rotate_flip_command.hpp/.cpp
      layer_command.hpp/.cpp        # add/remove/reorder/opacity/blend-mode/rename

  tools/
    tool.hpp                        # ITool: on_pointer_down/move/up, on_key, cursor(), options widget id
    tool_context.hpp                # shared refs: CanvasDocument&, Brush&, primary/secondary color, HistoryStack&
    tool_registry.hpp/.cpp          # ETool -> ITool factory map
    pencil_tool.hpp/.cpp
    eraser_tool.hpp/.cpp
    fill_tool.hpp/.cpp              # flood fill with tolerance
    dropper_tool.hpp/.cpp
    text_tool.hpp/.cpp
    shape_tool.hpp/.cpp             # line/rectangle/rounded-rect/ellipse/polygon, outline/fill/both
    selection_tool.hpp/.cpp         # rect/freehand/polygon, drag-to-move selection contents
    crop_tool.hpp/.cpp
    magnify_tool.hpp/.cpp           # click = zoom in, alt-click = zoom out

  brushes/
    brush.hpp                       # Brush { size, hardness, opacity, spacing, shape, stamp_source }
    brush_engine.hpp/.cpp           # stamps brush along a stroke path (spacing/interp/pressure-ready)
    brush_presets.hpp/.cpp          # built-in presets registry; loads custom stamp images later

  io/
    document_io.hpp/.cpp            # new/open/save/save-as; PNG/BMP/JPEG via stb_image (+ write)
    clipboard.hpp/.cpp              # cut/copy/paste + paste-from-file, wraps vendored `clip`
    recent_files.hpp/.cpp           # small MRU list, persisted like image-viewer's settings store

  render/
    slint_bridge.hpp/.cpp           # PixelBuffer(+dirty rects) -> slint::Image, minimal re-upload
    view_transform.hpp/.cpp         # canvas<->screen coordinate mapping (zoom/scroll)

  ui/
    ui_state.hpp/.cpp               # binds every Slint global (see §6) to the C++ side
    shortcuts.hpp/.cpp              # keyboard shortcut table -> app_controller actions

  config.hpp                        # app name/version, default canvas size, default palette
```

`third_party/clip` (already vendored in `image-viewer`) gets reused as-is for clipboard;
`stb_image`/`stb_image_write` (also already used by `image-viewer`) covers load/save.
Both should be pulled in via the same `FetchContent` pattern already established in
`ls-paint`'s `CMakeLists.txt` rather than re-vendoring.

## 5. Core data model details

### 5.1 PixelBuffer / Layer / LayerStack

- `PixelBuffer` is a flat `std::vector<Rgba8>` + width/height. All raster ops (set pixel,
  blit rect, resize/crop, flip/rotate) live here so tools never touch raw memory directly.
- `Layer` wraps one `PixelBuffer` plus `name`, `opacity (0..1)`, `blend_mode`, `visible`,
  `locked`. v1 ships with **exactly one non-removable layer** ("Background") — the UI
  doesn't expose a layers panel yet — but the type exists from day one so nothing above
  it (compositor, history, file I/O) needs to change when a layers panel is added later.
- `LayerStack` is just `std::vector<Layer>` + active index + `add/remove/move/merge_down`.

### 5.2 Blend modes (future-proofing point #1)

```cpp
enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten,
                        Difference, Add, /* extend here */ };

using BlendFn = Rgba8(*)(Rgba8 base, Rgba8 blend, float layer_opacity);
const BlendFn& blend_fn_for(BlendMode);   // table lookup, one place to extend
```

The compositor only ever calls `blend_fn_for(layer.blend_mode)`. Adding a new mode is a
new enum value + one function + one table entry — no compositor changes. v1 only wires
`Normal` into UI (single layer, so blend mode is moot), but the mechanism is real and
tested from the start so a future layers panel is additive.

### 5.3 Layer filters (future-proofing point #2)

```cpp
struct IFilter {
    virtual ~IFilter() = default;
    virtual void apply(PixelBuffer& buf, RectI region) const = 0;
};
```

`filter_registry` maps an id/name to a factory, the same way `tool_registry` maps `ETool`
to a tool. v1 ships 3-4 filters (grayscale, invert, brightness/contrast) as immediate,
destructive "Image > Adjustments" menu actions (there's precedent for this even in real
mspaint). Making them `IFilter` instead of ad-hoc functions means a later "non-destructive
per-layer filter stack" feature only needs a `std::vector<std::unique_ptr<IFilter>>` on
`Layer` and a compositor change to run them before blending — the filters themselves don't
change.

### 5.4 Selection

- `SelectionMask`: single-channel 8-bit coverage buffer, same dimensions as canvas.
  Empty/all-zero = "no selection" = "whole canvas."
- Builders: `from_rect`, `from_freehand(points)` (scanline fill of a closed polyline),
  `from_polygon(points)`. `ESelectionType` in `globals.slint` already anticipates
  `POLYGON`, just unused today — this plan wires it up.
- Ops: `invert()`, `select_all()`, `clear()`, `make_transparent()` (writes 0 alpha into the
  layer under the mask and drops the mask) — these map 1:1 to the four dropdown items
  already in `bar-home.slint`'s Image group that currently do nothing.
- Selection constrains: pencil/eraser/fill/shape drawing (writes are clipped to the mask),
  crop (crops to selection bounds), cut/copy (reads only inside the mask), move (drag
  translates the masked region).

### 5.5 History (undo/redo)

- `ICommand { do_(); undo(); virtual bool try_merge(ICommand&) }`.
- `HistoryStack` holds two stacks (undo/redo) of `unique_ptr<ICommand>`; pushing a new
  command clears the redo stack.
- Stroke commands store only the **dirty rect** before/after (not whole-canvas copies) so
  undo of a small pencil dab is cheap even on a large canvas; `try_merge` lets consecutive
  pixels of the *same* stroke coalesce into one command instead of one per mouse-move.
- Canvas-level ops (resize/crop/rotate/flip) store enough to fully reconstruct (they're
  infrequent, so a full-buffer snapshot is acceptable there).

### 5.6 Tools

```cpp
struct ToolContext {
    CanvasDocument& doc;
    HistoryStack& history;
    Brush& brush;
    Rgba8 primary_color, secondary_color;
    ShapeOptions& shape_options;   // outline/fill/both, thickness
    ViewTransform& view;           // for tools that need canvas<->screen mapping
};

struct ITool {
    virtual ~ITool() = default;
    virtual void on_pointer_down(ToolContext&, Point2f screen_pos, PointerButton) = 0;
    virtual void on_pointer_move(ToolContext&, Point2f screen_pos) = 0;
    virtual void on_pointer_up(ToolContext&, Point2f screen_pos) = 0;
    virtual std::string_view cursor() const = 0;
};
```

`ToolRegistry` maps every `ETool` value to a concrete `ITool`. `AppController::input_router`
looks up the active tool by `UserState.selected-tool` and forwards pointer events — this
is the piece that's completely missing today (currently `main.cpp` hardcodes "draw a black
pixel" regardless of tool).

Per-tool notes:
- **Pencil/Eraser** — go through `BrushEngine`, so size (wired from the existing Size
  dropdown) and hardness/opacity apply uniformly; eraser is pencil with the layer's own
  background/alpha as the "color."
- **Fill** — flood fill (4-connected, stack-based) with a tolerance option added to the
  bar; respects the active selection mask as a hard clip.
- **Dropper** — samples the composited buffer at the click point into
  primary/secondary (mirrors `color_idx`, already in `UserState`).
- **Shapes** — line/rectangle/rounded-rect/ellipse/polygon; outline/fill/both mode already
  has two dropdown buttons in the bar (`shape-outline.png`/`shape-fill.png`) that need a
  real `ShapeOptions.mode` enum and a live preview overlay while dragging.
- **Text** — click creates a floating editable text box (a real Slint `TextInput` overlay
  positioned over the canvas, not rendered into the raster buffer until committed), with
  a small options sub-bar for font family/size/bold/italic.
- **Selection** — rect/freehand/polygon per `ESelectionType`; dragging inside an existing
  selection moves its contents (cut from old location, paste at new, like mspaint).
- **Crop** — crops canvas to current selection bounds (disabled/no-op with no selection).
- **Magnify** — left click zooms in one step centered on click point, right/alt-click
  zooms out; ties into `ViewTransform`.
- **Resize/Rotate** — these are dialogs in real mspaint, not continuous pointer tools:
  clicking the bar button opens a modal (`ui/dialogs/resize-dialog.slint`,
  `ui/dialogs/rotate-dialog.slint`) that calls into `CanvasResizeCommand` /
  `CanvasRotateFlipCommand` (90°/180°/270°, horizontal/vertical flip, plus arbitrary-angle
  rotate for the image — not just canvas — via bilinear resample).

### 5.7 Brushes (future-proofing point #3)

```cpp
struct Brush {
    float size, hardness, opacity, spacing;   // spacing as fraction of size
    enum class Shape { Round, Square, Custom } shape;
    std::optional<PixelBuffer> custom_stamp;  // set when shape == Custom
};
```

`BrushEngine::stroke(path, brush, color, target)` handles spacing/interpolation between
mouse-move samples so fast strokes don't look dotted, and stamps either a generated
round/square mask or `custom_stamp` alpha-multiplied by `color`. v1 ships 2-3 built-in
presets (hard round, soft round, square) wired to the currently-empty Brushes dropdown;
"custom brush" support later is just: load an image into `custom_stamp` and add it to
`brush_presets` — `BrushEngine` doesn't change.

## 6. Slint-side additions

New/expanded globals in `ui/globals.slint` (kept as thin state, no logic):

- `DocumentState`: `canvas-width`, `canvas-height`, `zoom`, `has-selection`,
  `selection-rect`/`selection-points`, `can-undo`, `can-redo`, `dirty` (unsaved changes,
  for title bar / close-confirmation).
- `ToolOptions`: `brush-size`, `brush-hardness`, `brush-opacity`, `shape-mode`
  (outline/fill/both), `fill-tolerance`, `font-family`, `font-size`, `font-bold`,
  `font-italic`.
- `UserState` gains `primary-color`/`secondary-color` derivation already implied by
  `color_idx`/`selected_color_idxs` — no change needed there, just consumers.

New Slint files:
- `ui/topbar-file.slint` (or a `bar-file.slint` alongside the existing home/view bars) —
  New/Open/Save/Save As/Exit, wired as a proper `Tab` next to Home/View in `topbar.slint`
  (currently only two tabs exist and there's no file menu at all).
- `ui/dialogs/` — `resize-dialog.slint`, `rotate-dialog.slint`, `edit-colors-dialog.slint`
  (the Edit Colors button already exists but opens nothing), `canvas-properties-dialog.slint`.
- `ui/canvas-overlay.slint` — marching-ants selection outline, shape drag preview, crop
  handles, text-box editing overlay — all drawn as ordinary Slint elements positioned from
  `DocumentState`/`ToolOptions` properties, layered on top of the raster `Image` in
  `appwindow.slint` rather than baked into the pixel buffer until committed.
- `ui/statusbar.slint` — cursor position, zoom %, canvas size, selection size (mirrors
  `image-viewer`'s `widgets/statusbar.slint`).
- `ui/bar/bar-view.slint` gets real content: zoom in/out/percentage field/fit-to-window,
  show/hide gridlines and status bar toggles.

`appwindow.slint` changes: wrap `canvas` + `canvas-overlay` in a shared coordinate `Rectangle`
so overlay elements and the raster image always agree on position/scale; forward
`scroll-event` for zoom-with-ctrl-wheel; keep the existing `TouchArea` but forward richer
event data (buttons, modifiers) to `UserState`/`ui_state.cpp` instead of the current
click-position-only callback.

## 7. Rendering pipeline

1. A tool mutates the active layer's `PixelBuffer` within some `RectI` (returned by the
   tool call, e.g. the bounding box of a brush stamp or shape).
2. `CanvasDocument` accumulates dirty rects per frame.
3. `Compositor::composite(layer_stack, dirty_rects) -> flattened PixelBuffer` only
   re-blends the dirty region (full recomposite only on load/undo/layer-visibility toggle).
4. `render::SlintBridge` copies just the dirty sub-rect into the `SharedPixelBuffer`
   backing `AppWindow.texture` (same `SharedPixelBuffer<Rgb8Pixel>`/`slint::Image` pattern
   the renderer-migration patch already established) and calls
   `ui->set_texture(...)` — avoiding a full 600×700+ copy per mouse-move event.
5. Selection/shape-preview/text-box overlays are **not** part of the raster buffer at all
   — they're Slint elements positioned by `DocumentState` properties, so they repaint via
   normal Slint invalidation and never touch the pixel buffer until "committed" (e.g. text
   box loses focus, shape drag ends).

## 8. Milestones

1. **M0 — Foundation**: `core::PixelBuffer/Layer/LayerStack/CanvasDocument`,
   `render::SlintBridge` with dirty-rect upload, `ToolContext`/`ToolRegistry` skeleton,
   `AppController` replacing the current inline `main.cpp` logic. No new user-visible
   features yet, but `main.cpp` shrinks to argument parsing + wiring.
2. **M1 — Core drawing tools**: Pencil, Eraser, Fill, Dropper, Size dropdown wired to a
   real `Brush.size`, single-command-per-stroke undo/redo (Ctrl+Z/Ctrl+Y + toolbar-agnostic
   shortcut table).
3. **M2 — Selection & transform**: rect/freehand/polygon selection, select-all/invert/
   clear/transparent, crop, resize dialog, rotate/flip dialog.
4. **M3 — Shapes & text**: shape tool with outline/fill/both + thickness, text tool with
   floating editable overlay and font options bar.
5. **M4 — File I/O & clipboard**: New/Open/Save/Save As (PNG/BMP/JPEG), cut/copy/paste
   and paste-from-file via vendored `clip`, unsaved-changes prompt on close.
6. **M5 — View & status**: real zoom (in/out/percentage/fit), status bar, gridlines,
   magnify tool.
7. **M6 — Brush engine & presets**: spacing/interpolation, hardness/opacity, 2-3 presets
   wired into the Brushes dropdown; edit-colors dialog (custom color picker) replacing
   the current dead button.
8. **M7 — Extensibility payoff (opt-in, not required for a "done" mspaint clone)**:
   layers panel UI on top of the already-generalized `LayerStack`/blend-mode/filter
   machinery from §5.2–5.3 — add/remove/reorder/opacity/blend-mode-picker per layer,
   "Adjustments" menu filters made non-destructive per-layer, custom-brush-from-image
   loading. Because M0–M6 were built against the general model rather than a
   single-buffer shortcut, this milestone touches only UI + registries, not the compositor
   or history system.
9. **M8 — Polish**: About dialog dynamic version/link (already has a TODO placeholder),
   settings persistence (window size, last used tool/brush/palette — reuse the sqlite
   settings-store pattern from `image-viewer`'s `util/store_sqlite.*`), translations
   (`TODO.txt` already flags this).

## 9. Testing strategy

- `core/`, `selection/`, `history/`, `brushes/` are pure C++ with no Slint dependency —
  unit test them directly (buffer compositing, blend-mode math, flood-fill correctness,
  selection boolean ops, undo/redo stack behavior, brush stamping spacing) without
  spinning up a window.
- Golden-image tests for the compositor: render a small `LayerStack` fixture, compare
  against a checked-in expected PNG (byte-identical, since compositing is deterministic).
- `render::SlintBridge` gets a narrow test ensuring only the passed dirty rect is
  re-uploaded (guards the perf property, not just correctness).

## 10. Non-goals for this plan

- Tiled/streaming canvases for very large images (out of scope; a flat `PixelBuffer` per
  layer is fine at mspaint-typical sizes).
- Real-time collaborative editing.
- GPU-accelerated filters (the whole app deliberately targets Slint's software renderer,
  per the renderer-migration patch).
