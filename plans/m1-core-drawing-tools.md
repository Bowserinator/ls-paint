# M1 — Core drawing tools

## Status

**MOSTLY IMPLEMENTED.** Pencil, Eraser, Fill, Dropper, the Size dropdown → `Brush.size`
path, and single-command-per-stroke undo/redo with the Ctrl+Z/Ctrl+Y shortcut table all
exist and work in the working tree today. This file has two jobs: (a) document the
as-built behavior with exact file paths and APIs so a later milestone never re-reads
these files to find out how a stroke works, and (b) specify the remaining gaps as
concrete, ordered work items. Everything under "Already done" must not be re-implemented.

### Already done (as-built, verified by reading the code)

- **Tools registered**: `src/tools/tool_registry.cpp:10-15` registers `ToolId::Pencil`
  (`src/tools/pencil_tool.hpp`), `ToolId::Eraser` (`src/tools/eraser_tool.hpp`),
  `ToolId::Fill` (`src/tools/fill_tool.{hpp,cpp}`), `ToolId::Dropper`
  (`src/tools/dropper_tool.{hpp,cpp}`). All other `ToolId`s return `nullptr` from
  `ToolRegistry::find`.
- **BrushStrokeTool flow** (`src/tools/brush_stroke_tool.{hpp,cpp}`): shared base for
  Pencil/Eraser implementing the down/move/up lifecycle around an optional
  `StrokeRecorder` + `BrushEngine`, with `virtual Rgba8 stroke_color(ToolContext&,
  PointerButton) const` as the only per-tool hook. Pencil returns
  `context.colors.for_button(button)` (left = primary, right = secondary); Eraser returns
  `context.colors.secondary` regardless of button.
- **StrokeRecorder before/after dirty-rect snapshot** (`src/tools/stroke_recorder.{hpp,cpp}`):
  ctor copies the *whole* active layer (`m_snapshot`), `touch(RectI)` unions the touched
  region (clipped to document bounds) and calls `CanvasDocument::mark_dirty`, `commit(HistoryStack&)`
  pushes exactly one `PaintStrokeCommand(layer_index, region, snapshot.copy_region(region),
  current.copy_region(region))` — so the *committed* command stores only the dirty rect
  (before-image taken from the pre-stroke full snapshot, which is why a region touched
  late in the stroke still restores correctly), and a stroke with an empty region pushes
  nothing. Result: one undo entry per `pointer_down`…`pointer_up`, never per mouse-move.
- **BrushEngine** (`src/brushes/brush_engine.{hpp,cpp}`): `stroke_to` interpolates stamps
  along the segment with `step = max(1, spacing * size)` (no dotted fast strokes),
  `stamp` renders round/square masks with odd sizes snapped to pixel centers and returns
  the clipped dirty `RectI`.
- **Fill = scanline flood fill** (`src/tools/fill_tool.cpp:12-52`): stack of seeds,
  4-connected, exact `Rgba8` equality (no tolerance), early-return when `target == fill`,
  right button fills with secondary, records `dirty` via `StrokeRecorder` so one fill =
  one undo entry.
- **Dropper samples the flattened buffer** (`src/tools/dropper_tool.cpp:9-15`): reads
  `context.document.flattened()` (the composited cache, not the raw layer), `floor`s the
  position, bounds-checks, writes `context.colors.for_button(button)` (left → Color 1,
  right → Color 2). `UiState` writes the picked colors back into
  `UserState.primary-color`/`secondary-color` after the event (`src/ui/ui_state.cpp:63-67`).
- **Size dropdown → real `Brush.size`**: `ui/bar/bar-home.slint:226-259` `size-dropdown`
  PopupWindow with four `BarSizeDropdownItem`s (1–4px) each doing
  `ToolOptions.brush-size = self.size`; `src/ui/ui_state.cpp:57-58` converts the `length`
  to `int` (`std::lround`, min 1) into `m_controller.brush().size` on pointer-down;
  `ToolContext::brush` is a `const Brush&` that tools read.
- **Single-command-per-stroke undo/redo + shortcut table**: `ui/appwindow.slint:23-27`
  `FocusScope.key-pressed` → `UserState.key-pressed(text, ctrl||meta, shift)` →
  `src/ui/ui_state.cpp:81-89` → `lookup_shortcut` (`src/ui/shortcuts.cpp:25-35`, table at
  `:17-21`: Ctrl+Z → Undo, Ctrl+Shift+Z → Redo, Ctrl+Y → Redo; control chars normalized
  `key + 0x60` at `:29`, lowercased) → `HistoryStack::undo/redo` → `refresh()`.
  The table is toolbar-agnostic: no Slint-side key binding exists anywhere else.

### Remaining work

| # | Gap | Work item |
|---|-----|-----------|
| R1 | Eraser semantics undocumented | Decision (see Design § "Eraser semantics"): **keep painting `context.colors.secondary`** — that *is* mspaint behavior. Add the explanatory comment to `src/tools/eraser_tool.hpp`; no behavior change. |
| R2 | `ToolOptions.brush-size` (and colors) sampled only on pointer-down (`ui_state.cpp:57-58`) | Extract `UiState::sync_tool_options()` and call it from all three pointer callbacks; document that an in-flight stroke keeps the brush captured at pointer-down (`BrushEngine` copies `Brush` by value at `brush_stroke_tool.cpp:10`). |
| R3 | Size dropdown offers only 1–4px | Expand to the mspaint list **1, 2, 3, 4, 5, 8, 10 px** (7 items) in `bar-home.slint`; keep `ToolOptions.brush-size` type `length`. |
| R4 | No can-undo/can-redo exposed to Slint (plan §6 gap 15) | Add `HistoryStack::can_undo()/can_redo()`, `DocumentState.can-undo`/`can-redo`, write them in `UiState::refresh()` **before** its `flush()` early-return. |
| R5 | No visual feedback of current brush size | (a) highlight the selected row in the size dropdown (`BarSizeDropdownItem.focused`), (b) brush-size outline that follows the pointer over the canvas while Pencil/Eraser is active. |
| R6 | Fill (and strokes) ignore selection | No `SelectionMask` exists yet (M2). M1 only marks the hook points with `// M2 hook:` comments and fixes the `ClipToSelection` API in this plan's Design section, so M2 inserts it without redesign. |
| R7 | Zero unit tests (plan §9 unimplemented) | Add a Slint-free CTest target + tests for fill, brush geometry, stroke/eraser/recorder, dropper, history, shortcut lookup. |
| R8 | Shortcut table only covers undo/redo and `ui_state.cpp` has an ad-hoc `if/else` on the action | Extract `UiState::handle_shortcut(ShortcutAction)` dispatcher + document the 3-step `ShortcutAction` extension recipe in `shortcuts.hpp` so M3+ adds actions without touching `lookup_shortcut`. |
| R9 | Undo/redo fires mid-stroke | Guard the key handler with a new `InputRouter::active()` accessor so Ctrl+Z during a drag cannot interleave with the in-flight `StrokeRecorder` snapshot. |

Optional micro-item (not required for acceptance): the Dropper samples only on
pointer-down; `on_pointer_move` would drag-pick (mspaint allows this) — same body as
`on_pointer_down`, one override in `dropper_tool.hpp`.

Deferred (documented, not M1): **fill tolerance** (plan §5.6 "with a tolerance option
added to the bar"). Exact-match fill is correct today; when it is built it is
`ToolOptions.fill-tolerance` (int 0–255) in `globals.slint` plus replacing the equality
predicate at `fill_tool.cpp:20,29,33,34,43` with `color_distance(a, target) <= tolerance`
— no algorithm change. Leave to the milestone that adds the fill options UI.

## Acceptance criteria

Criteria 1–8 are user-visible and testable by hand; 9–14 are testable mechanically.

1. **Pencil**: dragging draws a continuous stroke (no gaps at fast drag speeds) in Color 1
   with left button and Color 2 with right button, at the current brush size.
2. **Eraser**: dragging paints the *background color* (Color 2) at the current brush size
   with **either** mouse button (mspaint semantics), never transparency.
3. **Fill**: left-clicking a region recolors exactly the 4-connected same-color region;
   diagonally-touching pixels of the same color are **not** filled; right-click fills with
   Color 2; clicking a color identical to the fill color changes nothing and creates no
   undo entry; clicking outside the canvas does nothing.
4. **Dropper**: left-click sets Color 1, right-click sets Color 2, to the color shown on
   screen (composited/flattened, including layer opacity — not the raw layer byte);
   clicking outside the canvas does nothing.
5. **Size dropdown**: lists 1px, 2px, 3px, 4px, 5px, 8px, 10px; the currently-active size
   is visually highlighted in the open dropdown; selecting one changes the width of the
   next stroke drawn by Pencil/Eraser (and of fill is unaffected — fill ignores size).
6. **Brush-size feedback**: while the Pencil or Eraser is selected and the pointer is over
   the canvas, an outline box the size of `ToolOptions.brush-size` follows the pointer;
   it disappears for Fill/Dropper and when the pointer leaves the canvas.
7. **One command per stroke**: a stroke made of many mouse-moves is removed by a *single*
   Ctrl+Z (pixels restored byte-exactly to pre-stroke state) and restored by a single
   Ctrl+Y or Ctrl+Shift+Z. Two separate strokes require two undos. Fill = one undo entry.
8. **Shortcut table**: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y work with any toolbar dropdown open
   state and regardless of which tool is selected; they are resolved entirely in C++
   (`lookup_shortcut`), with no Slint-side key binding; Ctrl+Z with an empty undo stack is
   a no-op; a new shortcut added to `kShortcuts` requires **zero** changes to
   `lookup_shortcut`.
9. **can-undo/can-redo**: `DocumentState.can-undo`/`can-redo` are `false`/`false` at
   startup, become `true`/`false` after any stroke or fill, `false`/`true` after an undo,
   and drawing after an undo resets `can-redo` to `false` (verified by
   `tests/history_test.cpp` against `HistoryStack` and by inspecting `UiState::refresh()`).
10. **Mid-stroke guard**: holding the mouse down and pressing Ctrl+Z does not undo or
    corrupt the in-flight stroke; the stroke commits as one command on release.
11. `ctest --test-dir build --output-on-failure` passes: fill correctness (region
    connectivity, button color, no-op case, undo restore), brush stamp geometry
    (coverage of 1px/2px/3px/4px round + square stamps, gap-free interpolation, edge
    clipping), stroke/eraser/recorder behavior (colors, exactly-one-command-per-stroke),
    dropper (flattened sampling, button mapping, bounds), history
    (can_undo/can_redo/eviction), shortcut lookup (normalization, case, modifiers).
12. `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` is
    warning-clean under `-Wall -Wextra`, and the test target builds without linking
    Slint.
13. Grep-level structure holds: `ToolOptions.brush-size` is still `in-out property
    <length>`, C++ still never writes it; `src/history/`, `src/tools/`, `src/brushes/`
    still contain no Slint includes.
14. `src/tools/fill_tool.cpp` and `src/tools/brush_stroke_tool.cpp` carry `// M2 hook:`
    comments naming the exact insertion point and helper for selection clipping.

## Design

### As-built data flow (one stroke)

```
ui/appwindow.slint TouchArea (canvas px, 1:1)
  │ pointer-down/moved/pointer-up → UserState.pointer-down(x, y, right?)
  v
src/ui/ui_state.cpp  on_pointer_down:
  │   sync colors (Slint → ColorPair), ToolOptions.brush-size → Brush.size   [R2: → all 3 callbacks]
  │   tool_from_slint(ETool) → InputRouter::pointer_down(ToolId, pos, button)
  v
src/app/input_router.cpp  registry.find(tool) → ITool* m_active → on_pointer_down
  v
BrushStrokeTool::on_pointer_down (brush_stroke_tool.cpp:7-12)
  │   m_recorder.emplace(document)      // full-layer before-snapshot
  │   m_engine.emplace(context.brush,   // Brush copied HERE (size frozen per stroke)
  │                    stroke_color(context, button))
  │   on_pointer_move(pos)              // stamps immediately → a click paints
  │ move: m_recorder->touch(m_engine->stroke_to(active_layer().pixels, pos))
  │   BrushEngine::stroke_to → spacing-interpolated stamp() → writes pixels, returns RectI
  │   StrokeRecorder::touch → m_region |= rect; document.mark_dirty(rect)
  v
on_pointer_up → final move → m_recorder->commit(history)
  │   PaintStrokeCommand(layer, region, before_region, after_region) → push (clears redo)
  v
src/ui/ui_state.cpp refresh()
      document.flush() → optional<RectI> → composite(region) → SlintBridge::upload
      → window.set_texture(...)                       [R4: also writes can-undo/can-redo first]
```

Fill and Dropper bypass `BrushStrokeTool`: `FillTool::on_pointer_down` runs the scanline
fill then a local `StrokeRecorder` commit; `DropperTool::on_pointer_down` only reads
`document.flattened()` and writes `ColorPair`.

### Eraser semantics — DECISION (R1)

**Keep `EraserTool::stroke_color` returning `context.colors.secondary`.** Rationale:

- Real mspaint's eraser paints with the **background color (Color 2)**, regardless of
  which button is held — it does not erase to transparency.
- plan.md §5.6 says "eraser is pencil with the layer's own background/alpha as the
  'color'". With the current single, fully-opaque "Background" layer, "the layer's
  background" *is* the background color, so the implementation satisfies the plan; the
  alpha variant only becomes meaningful once a transparent-capable layer stack exists
  (M7), and even then mspaint erases to the background color, not to alpha 0.
- Erasing to transparent would show the gray workspace through the canvas, which is not
  mspaint behavior.

Work item: replace `eraser_tool.hpp:10-12` body comment with the explicit rule:

```cpp
// mspaint semantics: the eraser paints with the background color (Color 2),
// ignoring which button is held (plan.md §5.6 "eraser is pencil with the layer's
// own background ... as the color"). Not alpha-0 transparency: the canvas has a
// single opaque Background layer and mspaint erases to Color 2.
Rgba8 stroke_color(ToolContext& context, PointerButton) const override {
    return context.colors.secondary;
}
```

### Brush-size sampling — DECISION (R2)

Sample `ToolOptions` (and colors) in **all three** pointer callbacks via an extracted
helper, *and* document the capture semantics:

```cpp
// src/ui/ui_state.hpp
private:
    void sync_tool_options();   // Slint globals → m_controller.brush() / colors()
    void handle_shortcut(ShortcutAction action);
```

```cpp
void UiState::sync_tool_options() {
    const auto& state = m_window.global<UserState>();
    const slint::Color primary = state.get_primary_color();
    const slint::Color secondary = state.get_secondary_color();
    m_controller.colors() = {
        {primary.red(), primary.green(), primary.blue(), primary.alpha()},
        {secondary.red(), secondary.green(), secondary.blue(), secondary.alpha()},
    };
    m_controller.brush().size = std::max(
        1, static_cast<int>(std::lround(m_window.global<ToolOptions>().get_brush_size())));
}
```

Called at the top of the `on_pointer_down`, `on_pointer_move`, `on_pointer_up` lambdas
(replacing `ui_state.cpp:50-58`). WHY both halves:

- *Live refresh* keeps `context.brush` honest for any tool that reads it mid-stroke
  (future M3 shapes read thickness the same way), and costs two property getters.
- *Capture at down is still the effective rule for painting*: `BrushEngine` stores
  `Brush m_brush` **by value**, copied in `BrushStrokeTool::on_pointer_down`
  (`brush_stroke_tool.cpp:10`), so changing the size mid-stroke cannot alter the stroke
  in flight — that is desirable (a stroke has one consistent width). Document this in a
  comment at `brush_stroke_tool.cpp:10`: "brush is captured per stroke; option changes
  apply from the next pointer-down."

### Size list expansion — DECISION (R3)

- **List**: `1, 2, 3, 4, 5, 8, 10` px (seven `BarSizeDropdownItem`s in
  `bar-home.slint`, replacing the current four at lines 231-247). Same markup pattern per
  row: `text: "8px"; size: 8px; clicked => { ToolOptions.brush-size = self.size; }`.
- **Type stays `length`**: `ToolOptions.brush-size: 1px` is consumed in three places that
  all want a length — the dropdown row's own `size: length` (drives the black preview bar
  `height: size` in `BarSizeDropdownItem`), the new brush outline (`width:
  ToolOptions.brush-size`), and `BarSizeDropdownItem.size` assignment. Converting to `int`
  happens exactly once, in `sync_tool_options()` (`std::lround`, clamped ≥ 1), keeping C++
  as the only place that knows `Brush.size` is `int`. C++ never writes the property
  (Slint is the source of truth for options) — direction stays one-way as today.
- No `kBrushSizes` array is introduced in `config.hpp`: the list has exactly one
  consumer (Slint markup) and duplicating it in C++ would create a second source of
  truth. Adding a row later = one markup block.
- NOTE (`TODO.txt` line "change size values depending on tool"): per-tool size lists
  (e.g. 1/2/3/5 for the text outline) are out of scope; if ever needed, switch on
  `UserState.selected-tool` inside the dropdown rows.

### can-undo / can-redo (R4)

```cpp
// src/history/history_stack.hpp  (additions)
[[nodiscard]] bool can_undo() const noexcept { return !m_undo.empty(); }
[[nodiscard]] bool can_redo() const noexcept { return !m_redo.empty(); }
```

```slint
// ui/globals.slint, DocumentState  (additions)
in property <bool> can-undo;
in property <bool> can-redo;
```

```cpp
// src/ui/ui_state.cpp, refresh() — ORDER MATTERS: before the flush() early-return,
// because undo/redo/push all funnel through refresh() and a command may leave the
// document's dirty rect empty in edge cases.
void UiState::refresh() {
    m_window.global<DocumentState>().set_can_undo(m_controller.history().can_undo());
    m_window.global<DocumentState>().set_can_redo(m_controller.history().can_redo());
    CanvasDocument& document = m_controller.document();
    const auto region = document.flush();
    if (!region) return;
    m_bridge.upload(document.flattened(), *region);
    m_window.set_texture(m_bridge.image());
}
```

WHY `DocumentState` (not a new `HistoryState` global): plan §6 lists `can-undo`/`can-redo`
under `DocumentState`; one more global would split document-ish state and force every
consumer to import two globals. Type `in` matches the existing `canvas-width`/`canvas-height`
style (written by C++, read by Slint). Consumers arrive later: Edit menu / toolbar buttons
(M4+), close-confirmation logic (`dirty`) will sit next to them.

### Brush-size visual feedback (R5)

Two parts, both pure Slint, no new files:

1. **Dropdown highlight** — `BarSizeDropdownItem` gains
   `in-out property <bool> focused: false;` and gets `BarDropdownItem`'s exact focus
   styling (`background: focused ? #c9e0f7 : touch.has-hover ? #edf4fc : transparent;`
   border `#8dbcec`/`#bfdcfb`). Each row sets
   `focused: ToolOptions.brush-size == self.size;`.
2. **Pointer outline** — a sibling `Rectangle` declared *after* `canvas` inside `touch`
   in `appwindow.slint` (so it stacks on top; both share the TouchArea coordinate space,
   which is exactly the canvas-pixel space used by `mouse-x - canvas.x`):

```slint
import { UserState, DocumentState, ToolOptions, ETool } from "globals.slint";
...
brush-outline := Rectangle {
    visible: touch.has-hover
        && (UserState.selected-tool == ETool.PENCIL || UserState.selected-tool == ETool.ERASER);
    x: touch.mouse-x - self.width / 2;
    y: touch.mouse-y - self.height / 2;
    width: ToolOptions.brush-size;
    height: ToolOptions.brush-size;
    border-radius: 50%;          // Brush.shape is Round and not yet user-selectable
    border-width: 1px;
    border-color: black;
    background: transparent;
}
```

WHY this shape: it costs zero C++ (position/size are property bindings, so Slint
invalidates it on hover-move and on every `ToolOptions.brush-size` change), it reuses the
existing `touch` coordinate space, and it does not require `ui/canvas-overlay.slint`
(that file, per plan §6, is for selection ants / resize handles / shape preview in M2 and
must layer *above* this outline when it arrives). Known limitation, acceptable for M1: a
1px brush shows a 1px box (accurate but subtle), and contrast of a single black border is
poor on dark pixels — if that reads badly in the manual check, upgrade to a nested
two-tone outline (outer `Rectangle` at `brush-size + 2px` with white border, inner black)
without touching C++.

### Selection clipping hook — DESIGN ONLY for M1 (R6)

No `SelectionMask` exists (M0 gap 9), so M1 cannot clip. It only freezes the contract so
M2 does not have to re-decide it:

```cpp
// Future: src/selection/clip.hpp  (added by M2, no M1 code changes)
namespace lsp {
// Empty optional = no selection = whole canvas (plan §5.4).
[[nodiscard]] inline std::optional<RectI> clip_to_selection(const CanvasDocument& doc,
                                                            RectI region);
}
```

M1 adds `// M2 hook:` comments at exactly two places:

- `src/tools/fill_tool.cpp` above the seed loop: M2 will (a) restrict the scanline bounds
  with `clip_to_selection`, and (b) skip writes where the mask is 0 — fill becomes
  "selection is a hard clip" per plan §5.4 without touching the algorithm structure.
- `src/tools/brush_stroke_tool.cpp` in `on_pointer_move`: M2 will intersect
  `m_recorder->touch(rect)` with the selection clip (dirty-rect side) and pass the mask
  into `BrushEngine::stroke_to` (per-pixel side).

### Shortcut extension pattern (R8)

Keep `lookup_shortcut` and `kShortcuts` exactly as they are; move the *dispatch* out of
the lambda so adding an action never touches lookup logic:

```cpp
// src/ui/ui_state.hpp
void handle_shortcut(ShortcutAction action);
```

```cpp
void UiState::handle_shortcut(ShortcutAction action) {
    switch (action) {
        case ShortcutAction::Undo: m_controller.history().undo(); break;
        case ShortcutAction::Redo: m_controller.history().redo(); break;
    }
}
```

and document the recipe in `src/ui/shortcuts.hpp` above `enum class ShortcutAction`:

```cpp
// Adding a shortcut (M3+):  1) add a ShortcutAction value here;
//   2) add row(s) to kShortcuts in shortcuts.cpp (key char, ctrl, shift);
//   3) add a case to UiState::handle_shortcut in ui_state.cpp.
// lookup_shortcut and its callers never change for new actions.
// Extending modifiers: add `bool alt` to struct Shortcut (default false in every row)
// and one parameter to lookup_shortcut — there is exactly one call site
// (ui_state.cpp key handler), which also forwards event.modifiers.alt.
// Multi-char keys (F-keys) need text.size() != 1 support: switch the match on
// std::string_view rather than char; today's normalization (control char + 0x60,
// lowercase) already handles Ctrl+Z arriving as '\x1A'.
```

WHY a switch in one method instead of a `std::function` table: actions are heterogeneous
(no shared payload), the codebase uses plain structs/enum classes with no type erasure,
and a `default:`-free switch gives -Wall a completeness check the moment a new enum value
is added (unhandled case = compiler warning), which a lambda map would not.

### Mid-stroke shortcut guard (R9)

```cpp
// src/app/input_router.hpp — addition
[[nodiscard]] bool active() const noexcept { return m_active != nullptr; }
```

`ui_state.cpp` key handler starts with `if (m_controller.input().active()) return;`.
WHY: `StrokeRecorder`'s before-snapshot is taken at pointer-down; an undo issued *during*
a stroke would mutate the layer underneath that snapshot, and the subsequent `commit`
would push a command whose before-image no longer matches the on-screen pre-state →
visual corruption on the next undo. Ignoring shortcuts for the sub-second duration of a
drag is what mspaint effectively does (the canvas owns the pointer). `PointerEventKind
.cancel` already maps to pointer-up, so `m_active` always clears.

### Eraser / fill / dropper behavior notes (as-built, kept)

- Eraser and Fill honor the *button*→color mapping via `ColorPair::for_button`, except
  the eraser, which is hard-wired to secondary (decision above). This matches mspaint
  (bucket right-click = fill with Color 2).
- Dropper bounds-check uses `flattened.contains(x, y)` — negative/out-of-range clicks are
  silently ignored, and no undo entry is created (droppers don't mutate the document).
- Fill's `target == fill` early-out (`fill_tool.cpp:20`) is what keeps "fill with the
  same color" from polluting the undo stack.

## File-by-file changes

### New files

- `tests/test_util.hpp` — check macros, no framework dependency:
  `namespace lsp::test { inline int g_failures = 0; }`, `#define LSP_CHECK(cond)` printing
  `FAIL <file>:<line>: <cond>` to stderr and incrementing `g_failures`,
  `LSP_CHECK_EQ(a, b)` variant for pixel/int comparisons, `int failures()` helper.
- `tests/test_suite.hpp` — declares the six entry points: `void run_fill_tool_tests();`,
  `run_brush_engine_tests()`, `run_brush_stroke_tests()`, `run_dropper_tool_tests()`,
  `run_history_tests()`, `run_shortcuts_tests()`.
- `tests/test_main.cpp` — `int main()` calling all six, printing
  `"OK (0 failures)"` or the count, returning `g_failures == 0 ? 0 : 1`.
- `tests/fill_tool_test.cpp` — builds `CanvasDocument(16, 16)` + `HistoryStack` + `Brush`
  + `ColorPair` + `ToolContext` directly and drives `FillTool::on_pointer_down`:
  (1) fill from the white area recolors only the connected region, a same-color island
  stays; (2) 4- vs 8-connectivity: red pixels at (0,0) and (1,1) — filling (0,0) leaves
  (1,1) untouched; (3) right button fills with `colors.secondary`; (4) `target == fill` →
  zero pixel changes and `can_undo() == false`; (5) seed at (-5, 200) → no crash, no
  change; (6) after a fill, one `history.undo()` restores every original byte and one
  `redo()` reapplies (compare layer pixels, then `document.flush()` and compare
  `flattened()` too); (7) seed at a corner fills to both edges without writing OOB.
- `tests/brush_engine_test.cpp` — `BrushEngine` against a scratch `PixelBuffer`:
  (1) size-1 round stamp writes exactly the floor-aligned seed pixel; (2) 3px round stamp
  covers the full 3×3 (radius 1.5 ⊇ centers at ±1), 2px round covers 2×2; (3) `Square`
  shape covers exactly size×size; (4) gap-free interpolation: stroke (0,0) → (20,0) with
  `size 4, spacing 0.25` and assert every pixel `(x, 0)` for `x ∈ [0,20]` is painted;
  (5) stamping centered on (0.5, 0.5) with size 10 returns a rect fully inside
  `bounds()` and writes nothing outside; (6) the returned dirty rect from a move contains
  both endpoints' stamp areas (used by `StrokeRecorder::touch`).
- `tests/brush_stroke_test.cpp` — `PencilTool` / `EraserTool` through the real
  down/move/up lifecycle with a real `HistoryStack`: (1) pencil primary/secondary button →
  primary/secondary pixels; (2) eraser → `colors.secondary` with *both* buttons (locks
  decision R1); (3) down + 3 moves + up on a fresh document ⇒ `can_undo()` true and
  **one** undo restores the whole stroke (second `undo()` leaves `can_undo()` false);
  (4) two strokes ⇒ two undos; (5) down+up with no move still yields exactly one command
  (the immediate stamp at `brush_stroke_tool.cpp:11`); (6) stroke at position
  `(-10, -10)` and `(1e6, 1e6)` → no crash, no OOB write; (7) undo-then-draw clears redo.
- `tests/dropper_tool_test.cpp` — (1) set layer pixel (5,5) red, `document.flush()`,
  dropper down at (5.4, 5.6) → `colors.primary == red` (`floor` semantics); (2) right
  button → `colors.secondary`; (3) out-of-bounds → colors unchanged; (4) flattened, not
  raw: layer opacity 0.5, pixel red, `flush()`, sample ⇒ result equals
  `document.flattened().at(x, y)` and differs from the raw red layer pixel.
- `tests/history_test.cpp` — (1) initial `can_undo()/can_redo()` = false/false; (2)
  push→true/false, undo→false/true, redo→true/false; (3) push clears redo; (4) undo/redo
  on empty stacks are no-ops (no crash); (5) eviction: push 205 tiny test-local
  `ICommand`s (a `CountingCommand` recording `undo()` calls), then undo 200 times →
  `can_undo()` becomes false and the first 5 commands were never undoable. No
  `HistoryStack` API changes needed for tests beyond `can_undo/can_redo` (R4).
- `tests/shortcuts_test.cpp` — pure `lookup_shortcut`: (1) `"\x1A"`+ctrl → Undo;
  `"\x1A"`+ctrl+shift → Redo; `"\x19"`+ctrl → Redo (control-char normalization); (2) `"z"`
  without ctrl → nullopt; `"Z"`+ctrl → Undo (lowercasing); (3) `"a"`+ctrl → nullopt;
  (4) `"ab"`+ctrl → nullopt (`size() != 1`); (5) `"z"`+ctrl+shift but no-ctrl variants →
  nullopt; (6) exact table contents: exactly Undo/Redo reachable, no other action value
  appears (guard against accidental table edits).

### Modified files

- `src/history/history_stack.hpp` — add `can_undo()`/`can_redo()` (see Design; header-only,
  2 lines). No `.cpp` change.
- `src/app/input_router.hpp` — add `[[nodiscard]] bool active() const noexcept` (1 line).
- `src/ui/ui_state.hpp` — declare `void sync_tool_options();` and
  `void handle_shortcut(ShortcutAction action);` in the private section (needs
  `#include "ui/shortcuts.hpp"` — already transitively available via the `.cpp`, but the
  header must include it explicitly for the parameter type).
- `src/ui/ui_state.cpp` —
  1. add `#include "ui/shortcuts.hpp"` (already present at line 9);
  2. replace the color/brush-size block (lines 50-58) in `on_pointer_down` with
     `sync_tool_options();`, and call it at the top of `on_pointer_move` (line 71-72) and
     `on_pointer_up` (line 76-77);
  3. `on_key_pressed` (lines 81-89) becomes: `if (m_controller.input().active()) return;
     const auto action = lookup_shortcut(...); if (action) handle_shortcut(*action);
     refresh();`
  4. add the two `handle_shortcut` + `sync_tool_options` definitions (Design has exact
     bodies);
  5. `refresh()` (lines 94-100): insert the two `set_can_undo`/`set_can_redo` lines
     before `document.flush()`.
- `src/ui/shortcuts.hpp` — add the extension-recipe comment block above
  `enum class ShortcutAction` (Design § shortcut pattern). Enum values and
  `lookup_shortcut` signature unchanged.
- `src/tools/eraser_tool.hpp` — replace lines 10-12 with the commented body from Design
  § "Eraser semantics" (behavior identical).
- `src/tools/fill_tool.cpp` — add `// M2 hook: clip to selection here (plan §5.4,
  ClipToSelection)` immediately above `void FillTool::on_pointer_down`'s seed loop (after
  the bounds check at line 16).
- `src/tools/brush_stroke_tool.cpp` — two comments: at line 10
  (`m_engine.emplace(...)`) note the per-stroke brush copy (R2), and above
  `on_pointer_move` the `// M2 hook:` selection-clip note (R6).
- `ui/globals.slint` — `DocumentState` gains `in property <bool> can-undo;` and
  `in property <bool> can-redo;` (after `canvas-height`, line 25). `ToolOptions` and
  `UserState` unchanged.
- `ui/components/bar-dropdown.slint` — `BarSizeDropdownItem` (lines 91-131): add
  `in-out property <bool> focused: false;` and change the row `Rectangle` (lines 100-104)
  to the focused/hover styling quoted in Design § R5. `size`/`text`/`clicked` unchanged.
- `ui/bar/bar-home.slint` — size dropdown (lines 226-248): replace the four
  `BarSizeDropdownItem`s with seven (1,2,3,4,5,8,10 px), each with
  `focused: ToolOptions.brush-size == self.size;`. Everything else in the file untouched
  (Brushes/Edit Colors stay dead until M6 — no dead buttons are newly wired by M1).
- `ui/appwindow.slint` — line 4: import gains `ToolOptions, ETool`; inside `touch`, after
  the `canvas := Image` block (line 61), add the `brush-outline := Rectangle { ... }`
  element from Design § R5. `TouchArea` sizing/coordinate math unchanged.
- `CMakeLists.txt` — test target only (see Build changes). **No new production `.cpp`
  files**, so the `add_executable(ls_paint ...)` list (lines 27-46) is untouched.

## Implementation order

Each step ends in a compiling build (`cmake --build build -j`).

1. **History + shortcuts foundations**: add `HistoryStack::can_undo/can_redo`; create
   `tests/test_util.hpp`, `test_suite.hpp`, `test_main.cpp`, `history_test.cpp`,
   `shortcuts_test.cpp`; add the test target to `CMakeLists.txt` (Build changes). Build +
   `ctest --test-dir build` green.
2. **C++ glue**: `InputRouter::active()`; extract `UiState::sync_tool_options()` and call
   it from all three pointer callbacks; extract `UiState::handle_shortcut` and guard the
   key handler for mid-stroke. Build (app target) green; manual smoke: stroke + Ctrl+Z,
   drag while pressing Ctrl+Z does nothing.
3. **can-undo/can-redo**: `globals.slint` `DocumentState` properties + `refresh()`
   writes. Build green (Slint recompiles); verify property values by temporarily binding
   them to the window `title` if desired (revert afterwards).
4. **Size list + highlight**: `bar-dropdown.slint` `focused` property, then
   `bar-home.slint` seven items. Build; open the dropdown — current size highlighted,
   7 rows fit.
5. **Brush outline**: `appwindow.slint` import + `brush-outline`. Build; hover the canvas
   with Pencil/Eraser/Fill selected.
6. **Documentation comments**: `eraser_tool.hpp` (R1), `fill_tool.cpp` + 
   `brush_stroke_tool.cpp` (R2 capture note, R6 hooks), `shortcuts.hpp` recipe (R8).
   Build green.
7. **Remaining tests**: `fill_tool_test.cpp`, `brush_engine_test.cpp`,
   `brush_stroke_test.cpp`, `dropper_tool_test.cpp`; append their `.cpp`s to the test
   target. Build + full `ctest` green.
8. **Verification pass**: run the complete checklist below (build, ctest, manual).

## Slint specifics

- **No new `.slint` files** and no new globals. All additions live in existing globals so
  `src/ui/ui_state.cpp` can read/write them (UI conventions).
- `ui/globals.slint`:
  - `DocumentState` += `in property <bool> can-undo;`, `in property <bool> can-redo;`
    (C++-written via `set_can_undo`/`set_can_redo`).
  - Unchanged: `ToolOptions.brush-size` stays `in-out property <length>: 1px`; C++ never
    writes it.
- `ui/components/bar-dropdown.slint`: `BarSizeDropdownItem` += `in-out property <bool>
  focused: false;` with `BarDropdownItem`'s focus/hover fill+border.
- `ui/bar/bar-home.slint`: size dropdown rows → 1/2/3/4/5/8/10 px, each binding
  `focused: ToolOptions.brush-size == self.size;`.
- `ui/appwindow.slint`: import line gains `ToolOptions, ETool`; new `brush-outline`
  `Rectangle` child of `touch` (properties: `visible`, `x`, `y`, `width`, `height`,
  `border-radius`, `border-width`, `border-color`, `background`). No new callbacks; no
  coordinate transform (still 1:1).
- **Dead buttons wired by M1**: none new — M1 extends the already-wired Size dropdown.
  Still dead (later milestones): Brushes dropdown, Edit Colors, Outline/Fill shape
  buttons, cut/copy/paste, selection dropdown ops, Resize/Rotate/Text/Magnify/Crop tool
  buttons (those `ETool`s have no registered tool; `find` → `nullptr`).
- Deprecated-syntax note: while editing `bar-home.slint`, do not copy the existing
  `close-on-click: false` pattern (line 327) into new popups — use `close-policy`.

## Build changes

Append to `CMakeLists.txt` after the existing target setup (after line 56's warning
flags block, before the `WIN32` block). The `add_executable(ls_paint ...)` list does
**not** change:

```cmake
option(LSP_BUILD_TESTS "Build unit tests" ON)
if(LSP_BUILD_TESTS)
    enable_testing()
    add_executable(ls_paint_tests
        tests/test_main.cpp
        tests/fill_tool_test.cpp
        tests/brush_engine_test.cpp
        tests/brush_stroke_test.cpp
        tests/dropper_tool_test.cpp
        tests/history_test.cpp
        tests/shortcuts_test.cpp
        src/brushes/brush_engine.cpp
        src/core/blend_modes.cpp
        src/core/canvas_document.cpp
        src/core/compositor.cpp
        src/core/pixel_buffer.cpp
        src/history/history_stack.cpp
        src/history/commands/paint_stroke_command.cpp
        src/tools/brush_stroke_tool.cpp
        src/tools/dropper_tool.cpp
        src/tools/fill_tool.cpp
        src/tools/stroke_recorder.cpp
        src/ui/shortcuts.cpp
    )
    target_include_directories(ls_paint_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(ls_paint_tests PRIVATE /W4)
    else()
        target_compile_options(ls_paint_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ls_paint_unit COMMAND ls_paint_tests)
endif()
```

WHY a hand-rolled check macro instead of GoogleTest: every source in the list is
Slint-free, the repo has no test dependency today, and adding FetchContent for a test
framework would roughly double cold-configure time next to the Slint fetch. The macro
harness keeps `ctest` integration (non-zero exit = failure) with zero dependencies; if M2+
wants fixtures/gmock, swapping `test_util.hpp` for GTest is a mechanical change confined
to `tests/`.

## Verification

- Build + tests:
  ```
  cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```
  Both targets warning-clean under `-Wall -Wextra`.
- Manual checklist:
  1. Pencil: left-drag draws Color 1, right-drag draws Color 2; fast diagonal drags show
     no dotting; stroke width equals the selected size.
  2. Eraser: left- *and* right-drag paint Color 2 (set Color 2 to red to see it); never
     transparent/gray.
  3. Fill: draw a red closed blob, fill green inside → only inside; fill blue from outside
     → everything except the blob; two diagonally-touching red pixels fill separately;
     right-click fill uses Color 2; filling white with white leaves no Ctrl+Z entry.
  4. Dropper: pick a pixel → "Color 1" swatch updates immediately; right-click pick →
     "Color 2"; click in the gray workspace (outside the canvas) → no change.
  5. Size: open Size → rows 1,2,3,4,5,8,10 px; current size row is highlighted; select
     10 → next pencil stroke is 10px wide; select 1 → 1px.
  6. Outline: with Pencil selected, hover the canvas → box follows the cursor at brush
     size; switch to Fill → outline gone; leave the canvas → outline gone.
  7. Undo: make a 20-move stroke → one Ctrl+Z removes all of it; Ctrl+Y restores; repeat
     with Fill; Ctrl+Z on fresh document does nothing; draw after undo → Ctrl+Y does
     nothing.
  8. Mid-stroke: press and hold the button, drag, press Ctrl+Z while still dragging →
     nothing happens; release → stroke intact and undoable once.
  9. While the Size dropdown popup is open, press Ctrl+Z → undo still works (if the
     popup swallows keys, close it on Esc first and record the behavior in Risks — see
     below; fix by forwarding keys from the popup FocusScope only if it fails).
  10. Undo repeatedly to the empty state: nothing crashes; draw again → redo cleared.
- Unit-testable pure logic (covered by the `tests/` target): scanline fill correctness &
  connectivity, brush stamp geometry & interpolation gaps, stroke→single-command
  invariant, eraser color rule, dropper flattened-sampling & bounds, history
  can_undo/can_redo/eviction/clear-on-push, shortcut table normalization & modifiers.
  Not unit-testable (manual only): Slint property plumbing (`DocumentState.can-undo`,
  dropdown highlight, outline visibility) — those are verified by checklist items 5/6 and
  code inspection of `refresh()`.

## Risks & edge cases

- **Dropdown popup height**: 7 rows × `Styles.bar-dropdown-height * 1.5` (42px) = 294px
  popup below a bar that ends ~110px down. Fine in the 600px default window; the window's
  `min-height: 200px` would clip it. Mitigation if it matters: set row height to
  `Styles.bar-dropdown-height` (28px → 196px total) — the 10px preview bar still fits.
- **Popup focus vs shortcuts**: if an open `PopupWindow` steals key events from the
  window `FocusScope`, Ctrl+Z silently stops working while the Size dropdown is open
  (checklist item 9). Fallback: close popups on `Escape`/any key, or forward
  `key-pressed` from inside the popup. Verify before declaring R-done.
- **`touch.mouse-x/y` validity**: `Rectangle` bindings read `mouse-x`/`mouse-y`, which
  Slint updates on hover; if the outline does not track the *unpressed* cursor in this
  Slint version, fall back to also handling `PointerEventKind.move` in `pointer-event`
  into two `in-out` properties (worst case: outline visible only while dragging — still
  acceptable feedback).
- **Mid-stroke option change**: after R2, `context.brush` updates live but in-flight
  `BrushEngine`s keep the copy made at pointer-down — do not "fix" this by storing a
  reference; per-stroke width consistency is intentional (Design § R2).
- **Undo-during-stroke corruption**: guarded by R9; if a future tool pushes commands from
  `on_pointer_move`, re-audit — the guard only protects keyboard shortcuts.
- **History memory**: a whole-canvas fill commits 2 × 800×600×4 ≈ 3.8 MB; 200 stacked
  full-canvas fills ≈ 768 MB worst case (M0's `StrokeRecorder` full-layer snapshot also
  costs one layer copy per stroke start). Acceptable at mspaint sizes; a region-size
  budget or `kMaxHistoryEntries` setting belongs to M8, not here.
- **Dropper alpha**: with the current opaque background, sampled colors always have
  a = 255. After M2 transparency exists, dropper can return a < 255 — `ColorButton`
  renders it as-is and `slint::Color::from_argb_uint8` round-trips correctly; no action
  needed, but the "flattened" test (dropper test 4) is what keeps this honest.
- **`length` vs future zoom (M5)**: sizes are logical px == canvas px only while the view
  is 1:1. When `ViewTransform` lands, `sync_tool_options()` must keep reading canvas-px
  sizes (it will — the dropdown value is canvas px) while the *outline* in
  `appwindow.slint` must be multiplied by zoom and offset by `DocumentState.canvas-x/y`.
  The outline element is deliberately a direct child of `touch` (which M5 will re-anchor
  inside the shared overlay Rectangle of plan §6) to minimize that migration.
- **Eraser & future layers**: the secondary-color eraser is correct for the single
  Background layer; if M7 adds layers, erasing a transparent layer to an opaque Color 2
  may surprise — re-evaluate then (mspaint has no layers panel either).
- **Fill ignores brush size** (by design — mspaint bucket is size-independent); do not
  "wire" `brush.size` into fill when adding the R6 clip.
- **No per-tool size lists** (`TODO.txt` "change size values depending on tool") — out of
  scope; the seven-row list is global.

## Milestone dependencies

- **Preconditions**: M0 Foundation — fully implemented (`plans/m0-foundation.md`).
  M1 consumes `CanvasDocument`, `HistoryStack`, `StrokeRecorder`, `BrushEngine`,
  `InputRouter`, `UiState::refresh`, the `UserState`/`DocumentState`/`ToolOptions`
  globals, and the Size dropdown markup; all exist today.
- **Cross-cutting requirements**: the top-left alignment requirement (#1) holds trivially
  at M1's 1:1 layout; the canvas resize-handle requirement (#2) belongs to M2 and needs
  `ui/canvas-overlay.slint`. M1's `brush-outline` is deliberately not in that overlay —
  when M2 adds one, the overlay must layer above the outline and both must read the same
  `DocumentState.canvas-x/y` origin (M5's `ViewTransform` then scales both).
- **This milestone delivers to later milestones**:
  - **M2 (Selection & transform)** — consumes R6's frozen `ClipToSelection` contract at
    the two `// M2 hook:` sites, the `tests/` harness for `SelectionMask`/clip tests, and
    `DocumentState.can-undo/can-redo` for selection-change commands.
  - **M3 (Shapes & text)** — consumes R8's `ShortcutAction` recipe (e.g. shape-picker
    shortcuts) and R2's `sync_tool_options()` pattern for `ToolOptions.shape-mode` /
    thickness.
  - **M4 (File I/O & clipboard)** — Ctrl+C/X/V rows appended to `kShortcuts` + one
    `handle_shortcut` case each; `can-undo/can-redo` drives Edit-menu item enablement;
    `DocumentState.dirty` (M4) sits beside `can-undo` in the same `refresh()` write.
  - **M5 (View & status)** — must rescale the brush outline and re-anchor it to the
    canvas origin (Risks above).
  - **M6 (Brush engine & presets)** — replaces the fixed seven-row size list mechanics
    only if brush presets need per-preset sizes; `Brush.size`/`spacing` plumbing and the
    `sync_tool_options()` path stay.
  - **Any milestone adding tests** — reuses the `ls_paint_tests` target; append source
    files + a `run_*_tests()` entry point, no new framework.
- **Explicitly not in M1**: fill tolerance, dropper drag-pick, per-tool size lists, undo/
  redo toolbar buttons, `ui/canvas-overlay.slint`, zoom-aware cursor, `Brush.hardness/
  opacity/custom_stamp` (M6), selection clipping (M2).
