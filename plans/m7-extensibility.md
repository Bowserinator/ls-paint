# M7 — Extensibility payoff (opt-in)

## Status

M7 is **plan.md §8's opt-in milestone**: the layers panel, non-destructive per-layer
adjustments, and custom-brush loading, built on machinery M0 already generalized
(`LayerStack`, `blend_fn_for`, §5.3 `IFilter` shape). Nothing below exists in the repo
today (verified by reading the working tree):

| Area | As-built today | M7 delta |
|---|---|---|
| `src/core/layer_stack.hpp` | header-only, only `add/at/active/active_index/layers` (lines 11–27). No remove/move/merge/set_active/index_of, no revision counter | + `remove/move/set_active/index_of/merge_down/size/revision/bump_revision` |
| `src/core/layer.hpp` | aggregate `Layer { name, pixels, opacity, blend_mode, visible }` — **no `locked`, no `filters`, no `clone()`** | + `locked`, + `filters`, + `clone()` |
| `src/core/blend_modes.*` | `enum class BlendMode { Normal, Multiply, Screen }` + `blend_fn_for` table (`blend_modes.hpp:7-11`) | + name table for the UI picker (pure addition) |
| `src/core/compositor.cpp` | `composite(const LayerStack&, RectI region, PixelBuffer&)` blends `layer.pixels` rows directly (lines 7–24). No filter pass | + pre-blend filter scratch pass (signature and call sites unchanged) |
| `src/core/filters/` | **directory does not exist** | NEW: `filter.hpp`, `builtin_filters.*`, `filter_registry.*` |
| `src/history/commands/` | only `paint_stroke_command.*`; `ICommand` is `{undo(CanvasDocument&), redo(CanvasDocument&)}` (`history/command.hpp:10-12`), no merge API | NEW: `layer_command.*` (5 command classes) |
| `src/ui/ui_state.cpp` | binds `UserState`/`DocumentState`/`ToolOptions` only; `refresh()` = `flush()+upload` (lines 94–100). No layer model, no layer callbacks | + `bind_layer_callbacks()`, + `sync_layers()`, + locked-layer input gate |
| `ui/globals.slint` | `UserState`, `DocumentState{canvas-width,height}`, `ToolOptions{brush-size}` — **no `LayerState`** | + `LayerSummary`/`AdjustmentEntry` structs + `LayerState` global (+ `BrushState` if M6 absent) |
| `ui/` | **no `layers-panel.slint` anywhere**; `appwindow.slint` is `VerticalLayout { TopBar; ScrollView }` (lines 29–64), no right dock | NEW `ui/layers-panel.slint`; `appwindow.slint` gains a workspace row |
| `ui/bar/bar-home.slint` | Image group has only `select-dropdown` (line 63) + Crop/Resize/Rotate; the Brushes dropdown (line 194) is a dead `ImageButtonVerticalDropdown` with no popup | + `adjust-dropdown` (Adjustments menu) + `brightness-popup` + Brushes popup (M6) / `brush-dropdown` + `load-brush-popup` (M7) |
| `src/brushes/` | `brush.hpp` = `{size, spacing, shape}` with `BrushShape { Round, Square }`; `brush_engine.cpp:34-56` `stamp()` has **no custom-stamp path**; **no `brush_presets.*`** (M6 not landed) | + `BrushShape::Custom` + `custom_stamp`; `stamp()` custom branch; `brush_presets::load_custom` |
| `CMakeLists.txt` | 17 sources listed (lines 27–46), `slint_target_sources(ls_paint ui/appwindow.slint)`, no test target | + 3 `.cpp` lines (+1 if M6 absent) |

**Deviation from plan.md §8's wording** (which claims M7 "touches only UI + registries,
not the compositor or history system"): §5.3's own text says the compositor later gains
"a compositor change to run them before blending" — that is this milestone. The design
below therefore does touch `compositor.cpp` and adds history commands, but **no existing
compositor call site, no existing command, and no existing tool changes**: `composite()`
keeps its exact signature, `CanvasDocument::flush()` is untouched, and
`PaintStrokeCommand`/`StrokeRecorder` are untouched.

## Acceptance criteria

All are user-visible and hand-testable unless noted.

1. **Panel**: a "Layers" panel is docked on the right of the workspace; it lists every
   layer with name, visibility toggle, opacity slider, blend-mode picker, and lock
   toggle; the topmost layer is the **first** row (matches Photoshop/GIMP); the active
   row is highlighted (#c9e0f7); the canvas remains top-left aligned and full drawing
   behavior from M1 is unchanged.
2. **Add/Delete**: "Add" creates a transparent layer at the top of the stack and makes
   it active; "Delete" removes the clicked layer (button disabled when only one layer
   exists); each is one Ctrl+Z / Ctrl+Y undo entry that restores/removes the layer
   *with its pixels and filters* byte-exactly.
3. **Duplicate**: creates a deep copy (pixels + filter instances, not shared) directly
   above the source and activates it; undo removes the copy without touching the
   original (editing the copy's adjustments afterwards must not affect the original —
   this is the test that filter ownership is deep).
4. **Merge down**: merges the layer into the one below it — upper layer's adjustments
   are baked into its pixels first, then its opacity + blend mode are applied onto the
   lower layer, then the upper layer is removed; one Ctrl+Z restores both layers
   exactly (upper layer reappears with its filters, lower's pixels revert).
5. **Reorder**: "Up"/"Down" buttons move a layer; the active layer follows its moved
   layer; a single undo restores the previous order. (Drag-reorder is explicitly not
   required — see Out of scope.)
6. **Opacity**: dragging a row's slider updates the composite live; releasing pushes
   exactly **one** undo entry; Ctrl+Z restores the pre-drag opacity; the slider shows
   a `100%`-style numeric readout.
7. **Blend mode**: each row has a dropdown with Normal/Multiply/Screen; switching a
   layer's mode visibly changes compositing and is one undo entry.
8. **Visibility/lock**: the eye toggle hides a layer (full recomposite, undo restores);
   the lock toggle blocks *painting* — with the active layer locked, Pencil/Eraser/Fill
   produce nothing (Dropper still works), and Adjustments refuse to apply.
9. **Rename**: double-clicking a row's name turns it into an editable field; Enter
   commits; renaming is one undo entry.
10. **Adjustments are non-destructive**: `Image > Adjustments > Grayscale/Invert`
    changes what's on screen but **does not modify any layer pixel bytes** (verifiable
    by "Bake" then undo, or by inspecting `layer.pixels` in a debugger); the row shows
    an `fx` badge; Ctrl+Z removes the filter; "Remove adjustments" clears them;
    "Brightness & Contrast…" opens a parameter popup (two sliders, OK/Cancel) and
    applies with the chosen parameters.
11. **Bake path (mspaint-style destructive)**: `Image > Adjustments > Bake adjustments
    into layer` flattens the active layer's filter stack into its pixels and clears the
    list — undoable as a *pixel* command (same mechanism as a stroke).
12. **Custom brush**: `Brushes > Load brush…` accepts an image path, loads it via
    `stb_image`, appends a preset named after the file, selects it, and the next stroke
    stamps the image (alpha as mask, tinted with the current color); the built-in
    round/square stamps are byte-for-byte unchanged.
13. **Regression guards**: existing stroke/fill/undo behavior from M1 passes unchanged;
    compositing a layer *without* filters produces byte-identical output to today
    (golden-image property, plan §9).
14. **Pure logic** (testable without a window): filter math, `LayerStack`
    remove/move/merge invariants, and "region composite == crop of full composite" for
    point-op filters — see Verification.

## Design

### D1. `IFilter` ownership on `Layer`: move-only `Layer` + `unique_ptr` (DECISION)

Two candidate designs were considered:

* **(A) `std::vector<std::unique_ptr<IFilter>> filters;` → `Layer` becomes
  non-copyable (move-only).**
* (B) `std::vector<std::shared_ptr<IFilter>> filters;` — `Layer` stays copyable, but a
  shallow copy *aliases* filter instances: "Duplicate layer" then changing the copy's
  brightness would silently rewrite the original's parameters. Fixing that requires a
  user-defined deep-copy constructor anyway — i.e. the same `clone()` machinery as (A)
  — so shared ownership buys nothing except copyability **that nothing in the codebase
  needs**.

**Chosen: (A).** Verification that nothing copies `Layer` today:

* `StrokeRecorder` snapshots only pixels: `m_snapshot(document.active_layer().pixels)`
  (`stroke_recorder.cpp:10`) and `PixelBuffer::copy_region` at commit (`:21`) —
  `PaintStrokeCommand` stores two `PixelBuffer`s (`paint_stroke_command.hpp:23-24`).
  Snapshots never touch `Layer`, so they are **completely unaffected**.
* `LayerStack::add(Layer layer)` takes the parameter by value and `push_back(std::move(layer))`
  (`layer_stack.hpp:13-16`); its only call site passes a prvalue
  (`canvas_document.cpp:8`, `m_layers.add({"Background", PixelBuffer(...)})`) — with a
  move-only `Layer` this is still fine: braced aggregate-init of a prvalue, guaranteed
  copy elision into the parameter, then one move.
* `std::vector<Layer>` reallocation needs `MoveInsertable`: the implicit move
  constructor of `Layer` is `noexcept` (all members — `std::string`, `PixelBuffer`,
  scalars, `vector<unique_ptr>` — have `noexcept` moves), so vector growth moves, never
  copies.
* `compositor.cpp` and every other consumer take `const Layer&` / `span<const Layer>`.
  `grep -rn '\bLayer\b' src/` before finishing (Verification step) must show no
  pass-by-value/assignment of `Layer` outside `layer_stack.hpp` and the new commands.

Cost of (A): anything that must produce a second copy of a layer (duplicate, merge
undo/redo, reinsert-on-redo) calls the new

```cpp
// src/core/layer.hpp (member, inline; Layer stays an aggregate — member functions
// are allowed, so canvas_document.cpp:8 brace-init keeps compiling)
[[nodiscard]] Layer clone() const;   // name/pixels/scalars copied; filters deep-cloned
```

which uses a `clone()` on every filter:

```cpp
// src/core/filters/filter.hpp
[[nodiscard]] virtual std::unique_ptr<IFilter> clone() const = 0;
inline std::vector<std::unique_ptr<IFilter>> clone_filters(
    const std::vector<std::unique_ptr<IFilter>>& src);   // helper used everywhere
```

**Invariant to state in code comments:** `Layer` is move-only; if a future milestone
needs `Layer` copies, it must call `clone()` — never rely on an implicit copy.

### D2. Core additions (pure extensions — no existing compositor call site changes)

```cpp
// src/core/layer.hpp — full M7 form
struct Layer {
    std::string name;
    PixelBuffer pixels;
    float opacity = 1.0f;
    BlendMode blend_mode = BlendMode::Normal;
    bool visible = true;
    bool locked = false;                                  // NEW
    std::vector<std::unique_ptr<IFilter>> filters;        // NEW, non-destructive

    [[nodiscard]] Layer clone() const;                    // NEW, inline (see D1)
};

// src/core/layer_stack.hpp — additions around the existing add/at/active (header-only)
class LayerStack {
public:
    void add(Layer layer);                                // existing body + bump counters
    void remove(std::size_t index);                       // no-op if size()==1 or index out of range
    void move(std::size_t from, std::size_t to);          // `to` in ORIGINAL indexing (see algo below)
    void set_active(std::size_t index);                   // bounds-checked, no-op if out of range
    void merge_down(std::size_t index);                   // index>=1; merges layers[index] into [index-1]
    [[nodiscard]] std::optional<std::size_t> index_of(const Layer* layer) const noexcept;  // linear scan
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;            // any change (incl. active/props)
    [[nodiscard]] std::uint64_t structure_revision() const noexcept;  // add/remove/move/merge only
    void bump_revision() noexcept;                        // call after mutating a Layer& IN PLACE
    void bump_structure_revision() noexcept;              // rarely needed directly (mutators do it)
    // ... existing at/active/active_index/layers ...
};
```

Exact algorithms (write them literally — they are the correctness core):

* **`move(from, to)`** — `to` is expressed in the *pre-move* indexing; exact body:
  ```cpp
  if (m_layers.size() < 2 || from >= m_layers.size() || to >= m_layers.size() || from == to) return;
  const bool active_was_moved = (m_active == from);      // (1) capture BEFORE touching the vector
  Layer held = std::move(m_layers[from]);
  m_layers.erase(m_layers.begin() + from);               // (2a) erase at `from`
  m_layers.insert(m_layers.begin() + to, std::move(held)); // (2b) insert at ORIGINAL-index `to`
  // (3) remap m_active (indices below are in pre-move numbering, applied in order):
  //     a) if (m_active > from) --m_active;        // erase shifted everything above `from` down
  //     b) if (m_active >= to)  ++m_active;        // insert shifted everything at/above `to` up
  //     c) if (active_was_moved) m_active = to;    // last: overrides a)/b) for the moved layer
  bump_revision(); bump_structure_revision();
  ```
  The three-step remap must run in exactly that order (`active_was_moved` is applied
  last). Worked checks on `[A,B,C]` (index 0 = bottom, drawn first):
  `move(0,1)` → `[B,A,C]`; active=1(B), `move(2,0)` → `[C,A,B]` with active(B) → 2;
  active=2(C), `move(2,0)` → active → 0.
* **`remove(index)`**: `if (m_layers.size() <= 1 || index >= size) return;` erase;
  then `if (m_active > index) --m_active; else if (m_active == index) m_active = std::min(index, size()-1);`
  bump both counters.
* **`merge_down(index)`**: `if (index == 0 || index >= size) return;`
  ```cpp
  Layer& lower = m_layers[index - 1];
  const Layer& upper = m_layers[index];
  PixelBuffer merged = upper.pixels;                      // copy
  for (const auto& f : upper.filters) f->apply(merged, merged.bounds());  // bake upper adjustments
  const BlendFn fn = blend_fn_for(upper.blend_mode);
  for (int y ...) for (int x ...) lower.pixels.at(x, y) = fn(lower.pixels.at(x, y), merged.at(x, y), upper.opacity);
  m_layers.erase(m_layers.begin() + index);
  if (m_active >= index) --m_active;                      // active==index lands on lower
  bump both counters;
  ```
  Semantics note: upper's filters/opacity/blend are consumed; lower keeps *its own*
  filters/opacity/blend as properties (applied later at composite time). All layers
  share canvas dimensions in v1 (no resize yet, M2), so no dimension guard is needed —
  add a `assert(lower.pixels.width() == merged.width())`-style early-return anyway.
* **`index_of(const Layer*)`**: address scan returning `std::nullopt` when absent.
  Consumers: `AppController` helpers that hand a `Layer*` back to `UiState`
  (`request_adjustment`, `bake_adjustments`) so the UI can map it to a display row via
  `row = size() - 1 - index_of(ptr)` even if a future change bypasses the revision
  counter; also a defensive re-resolve in commands (bounds-checked, never dereferences
  a stale pointer across structural changes — commands store **indices**, see D5).
* **Revision rule (one sentence, put as a comment in `layer.hpp`)**: every structural
  mutation bumps `structure_revision()` + `revision()` inside `LayerStack`; every *in-place*
  mutation of a `Layer&` field (opacity/name/blend/visible/locked/filters) must be
  followed by an explicit `layers().bump_revision()` by the caller. Pixel-only changes
  (strokes, fills, bake) do **not** bump `revision` (the panel doesn't show pixels) —
  except bake, which clears `filters` and therefore must bump.

`BlendMode` gains no values in M7 (Normal/Multiply/Screen suffice); the picker is
data-driven (D6), so adding a mode stays "enum value + fn + table entry + one name in
`blend_mode_names()`".

### D3. Filters: `IFilter` + builtins + registry

```cpp
// src/core/filters/filter.hpp  (namespace lsp; plan §5.3 signature kept verbatim)
class IFilter {
public:
    virtual ~IFilter() = default;
    virtual void apply(PixelBuffer& buf, RectI region) const = 0;
    [[nodiscard]] virtual std::unique_ptr<IFilter> clone() const = 0;
    [[nodiscard]] virtual std::string_view id() const = 0;
};
inline std::vector<std::unique_ptr<IFilter>> clone_filters(const std::vector<std::unique_ptr<IFilter>>&);
```

`apply` receives a buffer whose `region` is expressed in that buffer's coordinates;
implementations do `region = region.intersected(buf.bounds())` first and touch **only**
those pixels. Parameters live *inside* each concrete filter object (this is what makes
param-undo cheap, D5).

```cpp
// src/core/filters/builtin_filters.hpp
class GrayscaleFilter final : public IFilter {
public:
    void apply(PixelBuffer& buf, RectI region) const override;
    [[nodiscard]] std::unique_ptr<IFilter> clone() const override;
    [[nodiscard]] std::string_view id() const override { return "grayscale"; }
};

class InvertFilter final : public IFilter { /* same shape, id "invert" */ };

struct BrightnessContrastParams {
    float brightness = 0.0f;   // -1.0 .. +1.0, additive, applied after contrast
    float contrast   = 1.0f;   // 0.0 .. 3.0, pivot 0.5; 1.0 = neutral
};

class BrightnessContrastFilter final : public IFilter {
public:
    BrightnessContrastFilter() = default;
    explicit BrightnessContrastFilter(BrightnessContrastParams params) noexcept : m_params(params) {}
    void apply(PixelBuffer& buf, RectI region) const override;
    [[nodiscard]] std::unique_ptr<IFilter> clone() const override;
    [[nodiscard]] std::string_view id() const override { return "brightness-contrast"; }
    [[nodiscard]] const BrightnessContrastParams& params() const noexcept { return m_params; }
    void set_params(const BrightnessContrastParams& params) noexcept { m_params = params; }
private:
    BrightnessContrastParams m_params;
};
```

Exact per-pixel math (deterministic integer/float code, no lookup tables):

* grayscale: `out.g = (r*54 + g*183 + b*19 + 128) >> 8` written to r,g,b; **alpha untouched**.
* invert: `c = 255 - c` for r,g,b; alpha untouched.
* brightness/contrast, per RGB channel, alpha untouched:
  `y = (v/255 - 0.5) * contrast + 0.5 + brightness; out = round(clamp(y,0,1)*255)`.

```cpp
// src/core/filters/filter_registry.hpp  (mirrors ToolRegistry, plan §5.3)
class FilterRegistry {
public:
    FilterRegistry();                                              // registers the 3 builtins
    void register_filter(std::string id, std::string display_name,
                         std::function<std::unique_ptr<IFilter>()> factory);
    [[nodiscard]] std::unique_ptr<IFilter> create(std::string_view id) const;  // nullptr if unknown
    [[nodiscard]] std::string_view display_name(std::string_view id) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::string_view id_at(std::size_t i) const;      // menu order
    [[nodiscard]] std::string_view name_at(std::size_t i) const;
private:
    struct Entry { std::string id; std::string name; std::function<std::unique_ptr<IFilter>()> factory; };
    std::vector<Entry> m_entries;    // insertion order == menu order (vector, not unordered_map)
};
[[nodiscard]] FilterRegistry& global_filter_registry();   // function-local static
```

Registration order = menu order: `"grayscale"`/`Grayscale`, `"invert"`/`Invert`,
`"brightness-contrast"`/`Brightness & Contrast`.

### D4. Compositor: pre-blend filter pass (exact diff-level change)

`composite()` keeps its signature `void composite(const LayerStack&, RectI, PixelBuffer&)`
— `canvas_document.cpp:20` (`flush()`) and any other call site is **unchanged**. The
change is *inside* `compositor.cpp`, in two spots:

```cpp
void composite(const LayerStack& layers, RectI region, PixelBuffer& target) {
    region = region.intersected(target.bounds());
    const auto offset = static_cast<std::size_t>(region.x);
    const auto count   = static_cast<std::size_t>(region.width);

    // NEW: one scratch copy per FILTERED layer, over `region` only, computed once
    // (hoisted out of the row loop). Empty PixelBuffer (width 0) = layer not filtered.
    std::vector<PixelBuffer> scratch(layers.layers().size());
    std::size_t li = 0;
    for (const Layer& layer : layers.layers()) {
        if (!layer.filters.empty()) {
            scratch[li] = layer.pixels.copy_region(region);
            for (const auto& filter : layer.filters)           // filters run BEFORE blending
                filter->apply(scratch[li], scratch[li].bounds());
        }
        ++li;
    }

    for (int y = region.y; y < region.bottom(); ++y) {
        const auto out = target.row(y).subspan(offset, count);
        std::ranges::fill(out, Rgba8{0, 0, 0, 0});

        li = 0;
        for (const Layer& layer : layers.layers()) {
            if (!layer.visible) { ++li; continue; }
            const BlendFn blend = blend_fn_for(layer.blend_mode);
            // unfiltered: identical source expression to today (byte-identical output);
            // filtered: scratch rows are region-relative → NO x offset (width == count)
            const std::span<const Rgba8> source =
                scratch[li].width() > 0 ? std::span<const Rgba8>(scratch[li].row(y - region.y))
                                        : layer.pixels.row(y).subspan(offset, count);
            for (std::size_t i = 0; i < count; ++i)
                out[i] = blend(out[i], source[i], layer.opacity);
            ++li;
        }
    }
}
```

Why this shape: the loop nest is y-outer/layer-inner, so a per-layer filtered copy
**must** be hoisted above the row loop (otherwise the copy would happen once per row).
`std::vector<PixelBuffer>` default-constructs to width 0, giving the "is filtered" flag
for free.

**Dirty-rect semantics still hold**: `region` is exactly the dirty rect from
`CanvasDocument::flush()`; the scratch is `copy_region(region)`, filters see only
`scratch.bounds()` (== region), and only `target` rows/cols inside `region` are written.
Rows outside `region` are never read or written. For v1's **point-op** filters
(grayscale/invert/brightness-contrast) the result is *identical* to filtering the whole
canvas and cropping — that equivalence is a Verification test. Neighborhood filters
(blur, plan §5.3 future) would need `region` inflated by the kernel radius — documented
as a known limitation (Out of scope), with the fix noted: inflate `region` before the
scratch copy when a filter reports a radius (interface extension, later).

**Filter param change ⇒ full recomposite**: filters are evaluated only inside a *dirty*
region, and a parameter change by itself dirties no pixels — so every path that mutates
a filter list or params must call `document.mark_dirty(document.bounds())`:
`AppController::request_adjustment / apply_brightness_contrast / clear_adjustments`
and `LayerFiltersCommand::undo/redo`. (Layer add/remove/move/visibility already imply
whole-canvas recomposite; their commands do the same.)

`layer.filters` is read through `const Layer&` — calling the `const apply()` through
`const std::unique_ptr<IFilter>&` is fine; no `mutable`, no cast.

### D5. History: `layer_command.{hpp,cpp}` (5 command classes) + **params, not pixel snapshots**

**DECISION — how filter parameter changes undo:** store the *filter objects* (which
contain the parameters), not layer pixels.

* Filters are non-destructive in this design (D4): `Layer::pixels` **never changes**
  when a filter is added/removed/re-parameterized, so a pixel before/after snapshot
  would store two *identical* full-layer copies — expensive (W·H·8 bytes) and useless.
* Storing `before`/`after` filter vectors costs a few floats each and is exact (no
  re-render rounding), works for stacked filters, and reuses `IFilter::clone()`.
* The only operation that *does* change pixels is "Bake" — and that one deliberately
  goes through the existing `StrokeRecorder` → `PaintStrokeCommand` (full-rect region,
  §5.5 precedent for infrequent canvas-wide ops), needing **zero new pixel-history code**.

All five classes live in `src/history/commands/layer_command.{hpp,cpp}`, all implement
`ICommand { void undo(CanvasDocument&); void redo(CanvasDocument&); }`, and all finish
with `document.mark_dirty(document.bounds())` + the appropriate revision bumps.

```cpp
enum class LayerField { Name, Opacity, BlendMode, Visible, Locked };

// 1) Add / Remove — `kind` says what redo does; command always OWNS m_payload,
//    the stack always holds clones (see insert/extract symmetry).
class LayerPresenceCommand final : public ICommand {
public:
    enum class Kind { Add, Remove };
    LayerPresenceCommand(Kind kind, std::size_t index, Layer payload,
                         std::size_t prev_active, std::size_t post_active);
    void undo(CanvasDocument&) override;
    void redo(CanvasDocument&) override;
private:
    Kind m_kind; std::size_t m_index; Layer m_payload;
    std::size_t m_prev_active, m_post_active;
};
// insert helper (used by redo(Add) and undo(Remove)):
//   n = ls.size(); ls.add(m_payload.clone()); ls.move(n, m_index); ls.set_active(...)   // add() already activates
// extract helper (used by undo(Add) and redo(Remove)):
//   m_payload = std::move(ls.at(m_index)); ls.remove(m_index);   // move live layer back into the command
// active: set_active(m_prev_active) on undo, m_post_active on redo (both captured at push time
// and valid by construction after the structure is restored).

// 2) Reorder
class LayerMoveCommand final : public ICommand {
public:
    LayerMoveCommand(std::size_t from, std::size_t to, std::size_t prev_active, std::size_t post_active);
    void undo(CanvasDocument&) override;   // ls.move(m_to, m_from) + set_active(m_prev_active)
    void redo(CanvasDocument&) override;   // ls.move(m_from, m_to) + set_active(m_post_active)
};

// 3) Merge down — captures everything the merge destroys
class LayerMergeCommand final : public ICommand {
public:
    LayerMergeCommand(std::size_t upper_index, Layer upper, PixelBuffer lower_before,
                      std::size_t prev_active, std::size_t post_active);
    void undo(CanvasDocument&) override;
    //   lower.pixels = m_lower_before; re-insert m_upper.clone() at m_upper_index;
    //   set_active(m_prev_active);  bump_structure_revision()
    void redo(CanvasDocument&) override;
    //   ls.add(m_upper.clone()); ls.move(n, m_upper_index); ls.merge_down(m_upper_index);
    //   (merge is deterministic → byte-identical to the original execution)
};

// 4) Scalar/string properties (rename/opacity/blend/visible/locked)
class LayerPropCommand final : public ICommand {
public:
    using Value = std::variant<std::string, float, BlendMode, bool>;
    LayerPropCommand(std::size_t index, LayerField field, Value before, Value after);
    void undo(CanvasDocument&) override;   // write(ls.at(m_index), m_before); bump_revision()
    void redo(CanvasDocument&) override;   // write(ls.at(m_index), m_after);  bump_revision()
private:
    static void write(Layer&, LayerField, const Value&);   // switch + std::get_if<...> (no exceptions)
};

// 5) Filter-list changes (add / clear / param change) — THE params-not-pixels decision
class LayerFiltersCommand final : public ICommand {
public:
    LayerFiltersCommand(std::size_t index,
                        std::vector<std::unique_ptr<IFilter>> before,
                        std::vector<std::unique_ptr<IFilter>> after);
    void undo(CanvasDocument&) override;   // ls.at(i).filters = clone_filters(m_before); dirty(bounds); bump_revision()
    void redo(CanvasDocument&) override;   // ls.at(i).filters = clone_filters(m_after);  dirty(bounds); bump_revision()
};
```

**Index-safety invariant** (state it as a comment at the top of `layer_command.hpp`):
`PaintStrokeCommand` and every layer command store *indices*. They stay valid because
(a) `HistoryStack` is a strict LIFO (undo/redo in `history_stack.cpp:13-25`) and
(b) **every structural layer mutation goes through `AppController`'s layer API, which
always pushes a command** — so when any command is undone, the structure is exactly the
structure that existed when that command was pushed. Consequence worth documenting:
panel actions are mouse-driven and a stroke always commits on pointer-up before another
click can reach the panel, so no structural change can interleave *inside* a stroke's
index capture. A future keyboard shortcut for layer ops must respect the same rule.

### D6. `LayerState` Slint global + model sync strategy (DECISION)

```slint
// ui/globals.slint — new exports (generated C++: structs/enums land at global scope in
// build/appwindow.h, same as `enum class ETool` today; fields snake_cased: blend-index → blend_index)
export struct LayerSummary {
    name: string,
    visible: bool,
    locked: bool,
    opacity: float,        // 0..1
    blend-index: int,      // index into LayerState.blend-modes (== static_cast<BlendMode>)
    has-filters: bool,     // "fx" badge
}

export struct AdjustmentEntry { id: string, label: string }

export global LayerState {
    in-out property <[LayerSummary]> layers;             // display order: row 0 = TOPMOST
    in-out property <int> active-row: 0;                 // display row of the active layer (toolbar target)
    in-out property <[string]> blend-modes;              // ["Normal","Multiply","Screen"]
    in-out property <[AdjustmentEntry]> adjustments;     // from FilterRegistry
    in-out property <int> version: 0;                    // mirrors LayerStack::revision()
    in-out property <bool> params-dialog-open: false;    // Brightness & Contrast popup

    callback add-layer();
    callback delete-layer(int row);
    callback duplicate-layer(int row);
    callback merge-down(int row);
    callback move-up(int row);
    callback move-down(int row);
    callback set-active(int row);
    callback rename-layer(int row, string name);
    callback set-opacity(int row, float value);   // live drag, NO history
    callback commit-opacity(int row);             // slider release → ONE command
    callback set-blend(int row, int blend-index);
    callback toggle-visible(int row);
    callback toggle-locked(int row);
    callback request-adjustment(string id);       // C++ decides dialog vs immediate
    callback apply-brightness-contrast(float brightness, float contrast);
    callback clear-adjustments();
    callback bake-adjustments();
}
```

**Row ↔ stack mapping (single most bug-prone detail):** the panel shows topmost first,
the compositor stacks bottom-first (`for layer in layers.layers()`), therefore

```
stack_index = layers.size() - 1 - display_row        // and the reverse for building rows
move-up(row)   → LayerStack::move(s, s + 1)          // s = stack_index, higher index = drawn later = on top
move-down(row) → LayerStack::move(s, s - 1)
merge-down(row) → merge_down(s)                      // merges into s-1 (the layer visually BELOW)
```

`AppController` receives *display rows* and converts internally — Slint never sees
stack indices.

**Model update strategy: incremental, guarded by two counters (DECISION).**
`UiState` keeps one long-lived
`std::shared_ptr<slint::VectorModel<LayerSummary>> m_layer_model` (created in the ctor,
`set_layers(m_layer_model)` exactly like the palette at `ui_state.cpp:39-44`) and syncs
from `refresh()`:

```cpp
// shared helper: LayerSummary make_summary(const Layer& l)   // 6 fields, no active flag —
//   active-row is a single LayerState property, not a per-row field (row 0 = topmost)

void UiState::sync_layers() {
    const LayerStack& ls = m_controller.document().layers();
    if (ls.structure_revision() == m_structure_rev && ls.revision() == m_rev) return;
    const bool structural = ls.structure_revision() != m_structure_rev;
    m_structure_rev = ls.structure_revision();
    m_rev = ls.revision();
    auto& state = m_window.global<LayerState>();
    state.set_version(static_cast<int>(m_rev));                       // informational; truncation harmless
    state.set_active_row(static_cast<int>(ls.size() - 1 - ls.active_index()));  // both branches
    if (structural) {
        // rebuild ONLY on add/remove/move/merge — ONE notify_reset via set_vector()
        std::vector<LayerSummary> rows;
        rows.reserve(ls.size());
        for (std::size_t i = ls.size(); i-- > 0;) rows.push_back(make_summary(ls.at(i)));
        m_layer_model->set_vector(std::move(rows));
        return;
    }
    // property-only change (opacity/blend/visible/locked/filters): update rows IN PLACE
    for (std::size_t row = 0; row < ls.size(); ++row) {
        const std::size_t s = ls.size() - 1 - row;                  // display row → stack index
        LayerSummary cur = make_summary(ls.at(s));
        if (std::optional<LayerSummary> old = m_layer_model->row_data(row); old && !(*old == cur))
            m_layer_model->set_row_data(row, cur);   // Slint generates `operator== … = default`
    }
}
```

Why incremental-with-version rather than rebuild-on-every-change:
1. Opacity dragging and visibility toggles are *hot* (one event per mouse-move) while
   structural changes are rare button clicks — rebuilding the model on a slider drag
   would allocate n rows per mouse-move for no reason.
2. Rebuilding (clear+push) destroys and recreates every `for` row element, which would
   kill in-progress row-local UI (the rename `LineEdit`, an open blend dropdown, hover
   state) after *unrelated* changes elsewhere. In-place `set_row_data` keeps element
   identity; only genuinely structural changes accept the rebuild.
3. Two counters are needed because order can change without count changing (`move`) —
   a single "count-differs" check would miss it. `revision()` alone can't tell
   "something changed" from "structure changed", hence `structure_revision()`.

Sync is a no-op when counters match, so calling it from every `refresh()` (which runs
on every pointer/key event) costs two integer compares in the common case.

### D7. AppController API (the only layer-mutation entry point)

```cpp
// src/app/app_controller.hpp — additions (all take DISPLAY ROWS; private helper
// static std::size_t stack_row(std::size_t row, std::size_t n) { return n - 1 - row; })
void add_layer();                                  // ls.add({"Layer N", PixelBuffer(w,h)}) + LayerPresenceCommand(Add)
void remove_layer(std::size_t row);                // disabled-safe (size==1 → no-op) + command
void duplicate_layer(std::size_t row);             // clone → add → move above source → command (Add kind)
void merge_down(std::size_t row);                  // snapshot lower pixels + upper clone, ls.merge_down, command
void move_layer_up(std::size_t row);               // move(s, s+1) + LayerMoveCommand
void move_layer_down(std::size_t row);
void set_active_layer(std::size_t row);            // ls.set_active only: NOT a command (selection is UI state,
                                                    // like mspaint) but DOES bump revision() for the highlight
void rename_layer(std::size_t row, std::string);   // LayerPropCommand(Name)
void set_layer_opacity(std::size_t row, float);    // live write + mark_dirty(bounds) + bump_revision, NO command
void commit_layer_opacity(std::size_t row, float before);  // pushes LayerPropCommand(Opacity, before, now) if changed
void set_layer_blend(std::size_t row, int blend_index);    // bounds-checked static_cast<BlendMode> + command
void toggle_layer_visible(std::size_t row);        // command(Visible)
void toggle_layer_locked(std::size_t row);         // command(Locked)
Layer* request_adjustment(std::string_view id);    // nullptr if active layer locked / unknown id
Layer* apply_brightness_contrast(float brightness, float contrast);  // same guard
void clear_adjustments();                          // LayerFiltersCommand(filters → {})
bool bake_adjustments();                           // false if locked or no filters; StrokeRecorder path (below)
[[nodiscard]] BrushPresets& brush_presets() noexcept;
```

`request_adjustment` body (representative; the others follow the same 6 steps):

```cpp
Layer& layer = m_document.active_layer();
if (layer.locked) return nullptr;
auto filter = global_filter_registry().create(id);
if (!filter) return nullptr;
const std::size_t idx = m_document.layers().active_index();
auto before = clone_filters(layer.filters);
layer.filters.push_back(std::move(filter));
auto after  = clone_filters(layer.filters);
m_history.push(std::make_unique<LayerFiltersCommand>(idx, std::move(before), std::move(after)));
m_document.mark_dirty(m_document.bounds());
m_document.layers().bump_revision();
return &layer;
```

`bake_adjustments` (destructive path, reuses M1 machinery untouched):

```cpp
Layer& layer = m_document.active_layer();
if (layer.locked || layer.filters.empty()) return false;
StrokeRecorder recorder(m_document);              // snapshots layer pixels BEFORE mutation
PixelBuffer baked = layer.pixels;
for (const auto& f : layer.filters) f->apply(baked, baked.bounds());   // full-canvas bake
layer.pixels = std::move(baked);
layer.filters.clear();
recorder.touch(m_document.bounds());
recorder.commit(m_history);                       // one PaintStrokeCommand, full-rect region
m_document.layers().bump_revision();              // clears has-filters badge
return true;
```

**Locked-layer enforcement (DECISION: gate in `UiState`, not in tools).** In
`UiState::on_pointer_down` before `m_controller.input().pointer_down(...)`:

```cpp
const ToolId tool = tool_from_slint(state.get_selected_tool());
if (m_controller.document().active_layer().locked && tool != ToolId::Dropper) return;
```

One choke point, zero changes to `BrushStrokeTool`/`FillTool`/`tool_registry` (keeping
M7 out of the tools layer). Dropper is read-only and stays enabled. Selecting a locked
layer *is* allowed (`set_active` does not check `locked`) so the user can unlock it —
documented behavior, matches Photoshop.

### D8. `ui/layers-panel.slint` + `appwindow.slint` layout (dock DECISION)

**Dock position: right side of the workspace**, 240px fixed. Rejected alternatives:
left dock (fights the ribbon's left-to-right reading order) and a floating/toggleable
panel (adds window-state persistence work that belongs to M8).

`ui/appwindow.slint` restructure — the *only* structural change; the `ScrollView`
internals (including the `touch` width formula and `canvas := Image { x:4px; y:4px }`)
stay byte-identical, which is what preserves the **top-left alignment** requirement:

```slint
import { LayersPanel } from "layers-panel.slint";
...
focus-scope := FocusScope {
    key-pressed(event) => { ... unchanged ... }

    VerticalLayout {
        alignment: start;
        bar := TopBar{}

        HorizontalLayout {              // NEW: workspace row under the ribbon
            height: 100%;               // mirrors the old ScrollView height:100% pattern

            ScrollView {                // workspace — internals unchanged
                height: 100%;
                min-height: 100px;
                horizontal-stretch: 1;
                Rectangle { background: #c5cfdf; }
                touch := TouchArea { /* EXACTLY as today, lines 40-61 */ }
            }

            LayersPanel {               // NEW right dock
                width: 240px;
                min-width: 240px;
                horizontal-stretch: 0;
                vertical-stretch: 1;
            }
        }
    }
}
```

Also bump `min-width: 200px` → `min-width: 480px` on the `Window` (240 dock + 240
minimum workspace) so the canvas can never be squeezed to zero. Layout semantics:
`VerticalLayout` still packs `TopBar` at the start; the row takes the remaining height;
the dock is a *sibling* of the `ScrollView`, so the scroll content's origin — and hence
`touch.mouse-x - canvas.x` coordinate math and top-left placement of `canvas` — is
untouched. If the workspace row overflows by the bar height (i.e. `height: 100%`
resolves against the full layout box rather than the remainder), fall back to removing
`height: 100%` and setting `vertical-stretch: 1` on the row — verify visually in step 6
of the implementation order. Future milestones: M2's resize-handle overlay lives *inside*
this same `ScrollView`/`touch` coordinate space (plan §6), so this change does not
collide with it.

Panel composition (`ui/layers-panel.slint`, all components local to this file except
reused `BarDropdown*` and std-widgets `Button`/`LineEdit`):

```
LayersPanel (Rectangle, 240px, bg #f5f6f7, 1px border)
└─ VerticalLayout (padding: Styles.bar-padding, spacing 4px)
   ├─ Text "Layers" (font-weight 700)
   ├─ HorizontalLayout: Button "Add" | "Duplicate" | "Delete"
   ├─ HorizontalLayout: Button "Up"   | "Down"      | "Merge Down"
   └─ Rectangle { vertical-stretch: 1; ScrollView {
         VerticalLayout {
            for row[i] in LayerState.layers: LayerRow { layer-index: i; ... }
         } } }
```

**Toolbar buttons act on the ACTIVE layer** (row click selects; mspaint/Photoshop
convention), so they call `LayerState.<action>(LayerState.active-row)` — e.g.
`clicked => LayerState.delete-layer(LayerState.active-row)`. Enablement (exact
expressions, `active-row` is display order):

| Button | `enabled:` |
|---|---|
| Add / Duplicate | always (no expression) |
| Delete | `LayerState.layers.length > 1` |
| Up | `LayerState.active-row > 0` |
| Down | `LayerState.active-row < LayerState.layers.length - 1` |
| Merge Down | `LayerState.active-row < LayerState.layers.length - 1` (nothing below → no-op) |

Row-level controls act on **their own row** (`layer-index`): select, eye, lock, blend,
opacity, rename.

`LayerRow` (per-row, three lines, height ≈ 84px; instantiations pass
`active: i == LayerState.active-row` plus the six summary fields):

```
line 1: PanelToggle(visible) | name area: if !editing → Text "name  fx"
                                          if  editing → LineEdit { text: row.name; accepted(t) => { rename-layer(i, t); editing = false; } }
                                 | PanelToggle(locked)
line 2: blend button showing LayerState.blend-modes[blend-index] + "▼"
        → PopupWindow + BarDropdown + `for mode[k] in LayerState.blend-modes: BarDropdownItem { focused: k == blend-index; clicked => { set-blend(layer-index, k); popup.close(); } }`
line 3: LayerOpacitySlider (custom, see D9) | Text "NN%"
```

* Active highlight: row `Rectangle` background `active ? #c9e0f7 : transparent` +
  1px `#8dbcec` border (same colors as focused dropdown items,
  `bar-dropdown.slint:52-54`); row click (on the name area `TouchArea`) →
  `LayerState.set-active(layer-index)`.
* **Rename**: double-click on the name area (`TouchArea.double-clicked`, no args —
  verified in `internal/compiler/builtins.slint:837`) sets the row-local
  `property <bool> editing: true`; the `if editing:` guard creates a **fresh**
  `LineEdit` each time (its `text` binding evaluates once, and typing replacing the
  binding is harmless because the element is destroyed on commit). Footer "Rename"
  button was rejected: it would need C++-side active-row plumbing for no gain.
* `fx` badge: `Text { text: has-filters ? "fx" : ""; }` after the name.
* **Visibility/lock toggles**: a 14×14 `PanelToggle` component — filled inner square
  when true, hollow when false. Chosen over an eye glyph because `icons/` contains no
  eye/lock icon (verified against the icons listing) and over std-widgets `CheckBox`
  because `CheckBox.checked` is `in-out` — the widget writes it on click, which would
  clobber our `row.visible` binding (see D9).
* **Drag-reorder is optional and NOT in v1** — the Up/Down buttons are the supported
  path; a `TouchArea` drag-to-reorder in the list can be added later without core
  changes (it would only call the existing `move-up`/`move-down` callbacks).

### D9. Custom controls instead of std-widgets Slider/ComboBox (DECISION, Slint gotcha)

Slint widgets **write** their own `in-out` value properties on interaction
(`internal/compiler/widgets/common/slider-base.slint` `set-value()` → `root.value = …`;
ComboBox likewise writes `current-index`/`current-value`). An assignment replaces any
existing binding, so `Slider { value: row.opacity * 100 }` silently detaches after the
first drag and stops reflecting undo/redo. Because the panel's whole point is that
model → widget state stays authoritative, every bound control in the panel is either
read-only-binds or fully custom:

* **`LayerOpacitySlider`** (local component): reads `in property <float> value` (0..1)
  for thumb position when not pressed; a `TouchArea` computes
  `v = clamp(mouse-x / self.width, 0, 1)`; `pointer-event` down → emit
  `set-opacity(layer-index, v)`; `moved` (while `pressed`) → same; `pointer-event`
  up/cancel → `commit-opacity(layer-index)`. C++ (D7) captures the "before" value on
  the first `set-opacity` of a drag (`m_opacity_drag`/`m_opacity_before` members) and
  pushes exactly one `LayerPropCommand` on commit, skipping it if the value is
  unchanged — so live preview costs no history entries.
* **Blend dropdown**: house-pattern `PopupWindow` + `BarDropdown` (as in
  `bar-home.slint:63-99`), display text is a pure read of `blend-modes[blend-index]`.
* **Rename `LineEdit`**: writing is fine here — the user's typing is the source of
  truth; the element is destroyed on commit (`editing-row` is a *local* property of
  `LayersPanel`, not a global: C++ never reads it), so the next edit gets a fresh
  binding. `accepted(text)` → `rename-layer(row, text)` → hide.
* std-widgets `Button` is used freely (write-only-on-click callbacks, no bound value).

### D10. "Image > Adjustments" menu (DECISION: non-destructive is v1, bake is the destructive exposure)

plan.md §5.3 frames *destructive* menu actions as the pre-M7 state of affairs and M7's
charter is "Adjustments menu filters made non-destructive per-layer". mspaint's precedent
(destructive) exists because mspaint has no layers. v1 therefore ships **both**, exposed
as:

* **Default (non-destructive)** — `ui/bar/bar-home.slint` Image group gains
  `adjust-dropdown-btn := ImageButtonVerticalDropdown { text: "Adjust"; icon: edit-colors.png }`
  (no dedicated "adjust" icon exists in `icons/`; `edit-colors.png` is the closest and
  icon reuse is already the norm — `paste.png` is the default icon for every button).
  Its `adjust-dropdown := PopupWindow` contains:
  ```
  BarDropdownTitle "Adjustments" / BarDropdownDivider
  for e[i] in LayerState.adjustments:          // data-driven from FilterRegistry
      BarDropdownItem { text: e.label; clicked => { adjust-dropdown.close(); LayerState.request-adjustment(e.id); } }
  BarDropdownDivider
  BarDropdownItem { text: "Remove adjustments";   clicked => { adjust-dropdown.close(); LayerState.clear-adjustments(); } }
  BarDropdownItem { text: "Bake adjustments into layer"; clicked => { adjust-dropdown.close(); LayerState.bake-adjustments(); } }
  ```
  Adding a filter later = one `register_filter` call in C++; the menu extends itself.
* **`request-adjustment` policy lives in C++** (`UiState::on_request_adjustment`): if
  `id == "brightness-contrast"` it sets `LayerState.params-dialog-open = true`, else it
  calls `AppController::request_adjustment(id)` immediately. So the UI never encodes
  which filters need parameters.
* **Brightness & Contrast dialog** — `if LayerState.params-dialog-open: brightness-popup := PopupWindow { init => { self.show() } … }`.
  The `if` guarantees a *fresh* element per open (literal slider bindings
  `value: 0` / `value: 100` re-evaluate on each creation — again avoiding the D9 binding
  trap). Contents: `Text` labels + `Slider { minimum: -100; maximum: 100 }` (brightness),
  `Slider { minimum: 0; maximum: 200 }` (contrast), `Button "OK"` →
  `LayerState.apply-brightness-contrast(bright.value / 100, contrast.value / 100); LayerState.params-dialog-open = false;`,
  `Button "Cancel"` → just closes (element is destroyed by the `if`). Anchor: near the
  Adjust button (`x`/`y` derived from `adjust-dropdown-btn` like `size-dropdown`,
  `bar-home.slint:227-228`). **DECISION: `PopupWindow`, not std-widgets `Dialog`/`Window`** —
  the About dialog already establishes the PopupWindow pattern (and the shared-context
  guidance asks for one justified choice); a real modal would introduce nested
  window handling for a two-slider form, and outside-click dismissal comes free via
  `close-policy: close-on-click-outside` (plus explicitly setting
  `LayerState.params-dialog-open = false` where practical so the `if` tears it down).
* **Destructive path** = the "Bake adjustments into layer" item (D7): mspaint-style
  permanence, implemented entirely on top of `StrokeRecorder`/`PaintStrokeCommand`.

### D11. Custom brush loading

Dependencies: `stb_image` arrives with M4 (plan §4: pulled via FetchContent exactly like
`image-viewer`), and `src/brushes/brush_presets.{hpp,cpp}` + the Brushes dropdown popup
arrive with M6. **If either has not landed when M7 is executed, this plan defines the
exact fallback** (create `brush_presets.*` with the interface below and wire a minimal
dropdown popup) — do not invent alternate names.

```cpp
// src/brushes/brush.hpp — M7 delta
enum class BrushShape { Round, Square, Custom };
struct Brush {
    int size = 1;
    float spacing = 0.25f;
    BrushShape shape = BrushShape::Round;
    std::optional<PixelBuffer> custom_stamp;   // plan §5.7; set iff shape == Custom
    // (M6 may add hardness/opacity — M7 does not depend on them)
};

// src/brushes/brush_presets.hpp — M6 file; M7 adds load_custom()
struct BrushPreset { std::string name; Brush brush; };
class BrushPresets {
public:
    BrushPresets();                                     // registers built-ins (M6: hard round, soft round, square)
    [[nodiscard]] std::span<const BrushPreset> all() const noexcept;
    [[nodiscard]] const BrushPreset* at(std::size_t index) const noexcept;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    [[nodiscard]] std::size_t load_custom(std::string_view path);   // M7: index of new preset, or npos on failure
private:
    std::vector<BrushPreset> m_presets;
};
```

`load_custom` (uses stb_image already vendored by M4; force RGBA:
`stbi_load(path, &w, &h, &comp, 4)` → copy into a `PixelBuffer` →
`stbi_image_free`):

```cpp
Brush b;
b.shape = BrushShape::Custom;
b.size = std::max(w, h);                 // initial stamp size = native size (ToolOptions.brush-size
                                         // still overrides size on pointer-down, ui_state.cpp:57-58)
b.custom_stamp = std::move(buf);
m_presets.push_back({std::filesystem::path(path).stem().string(), std::move(b)});
return m_presets.size() - 1;
```

**`BrushEngine::stamp()` — exact change** (`src/brushes/brush_engine.cpp:44-55`). The
current round/square `inside` test is kept byte-identical in an `else` branch; the new
custom branch maps the stamp image onto the same `area` rect (which is `size + 1` wide —
use `size + 1` as the divisor so the full stamp spans the area):

```cpp
const bool custom = m_brush.shape == BrushShape::Custom && m_brush.custom_stamp.has_value()
                    && m_brush.custom_stamp->width() > 0;
for (int y = area.y; y < area.bottom(); ++y) {
    for (int x = area.x; x < area.right(); ++x) {
        if (custom) {
            const PixelBuffer& s = *m_brush.custom_stamp;
            const int sx = std::clamp((x - area.x) * s.width()  / (size + 1), 0, s.width()  - 1);
            const int sy = std::clamp((y - area.y) * s.height() / (size + 1), 0, s.height() - 1);
            const Rgba8 px = s.at(sx, sy);
            if (px.a == 0) continue;
            const auto a = static_cast<std::uint8_t>(static_cast<int>(m_color.a) * px.a / 255);
            target.at(x, y) = blend_normal(target.at(x, y),
                                           Rgba8{m_color.r, m_color.g, m_color.b, a}, 1.0f);
        } else {
            // existing dx/dy `inside` test + target.at(x, y) = m_color;  (unchanged)
        }
    }
}
```

Requires `#include "core/blend_modes.hpp"` in `brush_engine.cpp`. Semantics: the stamp
image's **alpha is the mask**, the current color is the paint (plan §5.7
"custom_stamp alpha-multiplied by color"); non-square images stretch to the square stamp
box (documented behavior, v1). The round/square path is untouched → criterion 12/13.

**UI + selection/storage**: the Brushes dropdown (`bar-home.slint:194`) becomes a popup
(M6's preset list) plus, for M7:

```slint
BarDropdownDivider {}
BarDropdownItem { text: "Load brush…"; clicked => { brush-dropdown.close(); load-brush-popup.show(); } }
```

`load-brush-popup := PopupWindow` (same pattern as the brightness dialog: `if`-guarded
fresh element) with a `LineEdit` path field + `Button "OK"` →
`LayerState.load-brush(path)` **and** `Button "Cancel"`. Selection state lives in a
`BrushState` global (M6's, or created by M7's fallback):

```slint
export global BrushState {
    in-out property <[string]> preset-names;
    in-out property <int> selected-preset: 0;
    callback select-preset(int index);
    callback load-brush(string path);
}
```

`UiState::on_load_brush`: `idx = m_controller.brush_presets().load_custom(path);`
if `!= npos` → rebuild `preset-names` from the registry, `set_selected_preset(idx)`,
`on_select_preset(idx)` → `m_controller.brush() = *presets.at(idx)->brush` (the loaded
`PixelBuffer` lives on in the preset for the whole session; persistence across restarts
is M8 settings territory, out of scope). The dropdown's `for` rows mark
`focused: k == BrushState.selected-preset` and click → `BrushState.select-preset(k)`.

## File-by-file changes

### New files

- `src/core/filters/filter.hpp` — `IFilter` (D3, §5.3 signature), `clone_filters()` inline helper. Header-only.
- `src/core/filters/builtin_filters.hpp` — `GrayscaleFilter`, `InvertFilter`,
  `BrightnessContrastParams`, `BrightnessContrastFilter` declarations (D3).
- `src/core/filters/builtin_filters.cpp` — the three `apply()` implementations with the
  exact math from D3; each starts with `region = region.intersected(buf.bounds());`.
- `src/core/filters/filter_registry.hpp` — `FilterRegistry` + `global_filter_registry()` (D3).
- `src/core/filters/filter_registry.cpp` — ctor registers grayscale/invert/brightness-contrast
  in menu order; `create` returns `nullptr` for unknown ids.
- `src/history/commands/layer_command.hpp` — the 5 command classes + `LayerField` +
  the index-safety invariant comment (D5).
- `src/history/commands/layer_command.cpp` — `undo`/`redo` bodies per D5; each ends
  with `document.mark_dirty(document.bounds())` and the right revision bumps; `LayerPropCommand::write`
  uses `std::get_if` (no exceptions, per house rules).
- `ui/layers-panel.slint` — `LayersPanel` + local `LayerRow`, `LayerOpacitySlider`,
  `PanelToggle`, blend `PopupWindow`; imports `LayerState` from `globals.slint`,
  `BarDropdown*` from `components/bar-dropdown.slint`, `Styles`, std-widgets `Button`/`LineEdit` (D8).

### Modified files

- `src/core/layer.hpp` — add `#include <memory>`, `<vector>`,
  `#include "core/filters/filter.hpp"`; add `bool locked = false;`,
  `std::vector<std::unique_ptr<IFilter>> filters;`, inline `Layer clone() const`
  (deep-clones filters). Keep it an aggregate (no user-declared constructors) so
  `canvas_document.cpp:8` brace-init still compiles. Add the move-only/revision rule comment (D1/D2).
- `src/core/layer_stack.hpp` — add `remove/move/set_active/index_of/merge_down/size`,
  `m_revision`/`m_structure_revision` + accessors + `bump_*`, include `<optional>`/`<cstdint>`;
  append the bump calls to the existing `add()` body (behavior otherwise unchanged).
  Implementations per D2 (stays header-only, matching today's style).
- `src/core/compositor.cpp` — the scratch-filter pass from D4 (signature untouched;
  include nothing new — `layer.hpp` already pulls `filter.hpp`).
- `src/core/blend_modes.hpp/.cpp` — add
  `[[nodiscard]] std::span<const std::string_view> blend_mode_names();` (enum order:
  Normal/Multiply/Screen) used to populate `LayerState.blend-modes`. No enum/fn changes.
- `src/history/commands/paint_stroke_command.*` — **no change** (documented on purpose).
- `src/tools/stroke_recorder.*` — **no change** (snapshots pixels, not Layer — D1).
- `src/app/app_controller.hpp/.cpp` — declare/define the D7 API; add
  `BrushPresets m_presets;` + `brush_presets()` accessor; private
  `stack_row()` helper; includes: `brushes/brush_presets.hpp`,
  `core/filters/filter_registry.hpp`, `history/commands/layer_command.hpp`,
  `tools/stroke_recorder.hpp`.
- `src/ui/ui_state.hpp` — add private `bind_layer_callbacks()`, `sync_layers()`,
  `bind_brush_callbacks()` (M7 part: load/select); members
  `std::shared_ptr<slint::VectorModel<LayerSummary>> m_layer_model;`,
  `std::uint64_t m_layer_revision = 0, m_layer_structure_revision = 0;`,
  `bool m_opacity_drag = false; float m_opacity_before = 1.0f;` (model kept in
  `.cpp`-visible type — include `appwindow.h` already present).
- `src/ui/ui_state.cpp` — ctor: build `m_layer_model`, fill `blend-modes` from
  `blend_mode_names()`, fill `adjustments` from `global_filter_registry()`, seed model,
  call `bind_layer_callbacks()`; implement all 17 `LayerState.on_*` handlers as thin
  calls into `AppController` (opacity split: live + commit per D9); brightness policy
  handler (D10); locked-layer gate in `on_pointer_down` (D7); call `sync_layers()` at
  the top of `refresh()` (before the `flush()` early-return so undo still syncs the panel).
- `ui/globals.slint` — add `LayerSummary`, `AdjustmentEntry`, `LayerState` (D6), and
  `BrushState` (only if M6 hasn't added it; D11 fallback).
- `ui/appwindow.slint` — import `LayersPanel`; workspace `HorizontalLayout` restructure +
  `min-width: 480px` (D8). `ScrollView`/`touch`/`canvas` bodies untouched.
- `ui/bar/bar-home.slint` — Image group: add `adjust-dropdown-btn` + `adjust-dropdown`
  (data-driven items) after the Crop/Resize/Rotate column; add `brightness-popup`
  (`if LayerState.params-dialog-open`, D10). Brushes group: `brush-dropdown` popup with
  preset `for` + "Load brush…" + `load-brush-popup` (D11; skip preset rows if M6 already
  built them). Import `LayerState`/`BrushState`.
- `src/brushes/brush.hpp` — `BrushShape::Custom`, `custom_stamp`, include
  `<optional>` + `core/pixel_buffer.hpp`.
- `src/brushes/brush_engine.cpp` — custom branch in `stamp()` (D11) + blend_modes include.
- `src/brushes/brush_presets.hpp/.cpp` — M7 delta: `load_custom()` (+ whole file only if
  M6 hasn't created it; built-ins then = hard round / soft round / square, M6 scope).
- `CMakeLists.txt` — see Build changes.

## Implementation order

Each step ends in a compiling build (`cmake --build build -j`).

1. **Filters**: create `src/core/filters/{filter.hpp,builtin_filters.hpp,builtin_filters.cpp,filter_registry.hpp,filter_registry.cpp}`; add `src/core/filters/builtin_filters.cpp` and `src/core/filters/filter_registry.cpp` to CMake. Build.
2. **Layer**: `layer.hpp` (locked/filters/clone) + `layer_stack.hpp` extensions +
   `blend_mode_names()`. Build. Then run
   `grep -rnE '\bLayer\b' src/ | grep -v 'layer'` (and eyeball `Layer ` usages) to
   confirm no implicit copy sites appeared — fix any by calling `clone()`.
3. **Compositor**: D4 scratch pass. Build + run the app; draw a stroke and undo —
   output must be unchanged (no filters anywhere yet).
4. **History**: `layer_command.{hpp,cpp}` + CMake line. Build.
5. **AppController API**: D7 methods (adjustment guards included). Build.
6. **Slint state + layout**: `globals.slint` structs/`LayerState`, `layers-panel.slint`,
   `appwindow.slint` restructure (+min-width). Callbacks are unregistered (no-ops) at
   this point — build and *look*: dock on the right, canvas still top-left, window
   resizable down to 480px. This is where you verify the `height: 100%` fallback note
   in D8.
7. **Wiring**: `ui_state.{hpp,cpp}` bindings + `sync_layers()` + locked gate. Build →
   panel fully live (add/delete/reorder/opacity/blend/eye/lock/rename, all undoable).
8. **Adjustments menu**: `bar-home.slint` adjust dropdown + brightness popup +
   `request_adjustment` policy in `UiState`. Build → criterion 10/11 testable.
9. **Custom brush**: `brush.hpp`, `brush_engine.cpp`, `brush_presets::load_custom`,
   Brushes popup + load dialog + `BrushState` callbacks. Build → criterion 12.
10. **Full verification pass** (below), then the grep invariant check from step 2 again.

## Slint specifics

* **New global `LayerState`** (exact names in D6) — 7 in-out properties, 17 callbacks,
  2 new structs. Conventions follow `UserState`: actions are *callbacks* (C++ registers
  them), data is `in-out` properties written only by C++ (except the dialog-open flag,
  which Slint toggles).
* **New `BrushState` global** (D11) — only if M6 hasn't created it.
* **New file `ui/layers-panel.slint`** exporting `LayersPanel`.
* **New callbacks consumed in `src/ui/ui_state.cpp`**: `on_add-layer`,
  `on-delete-layer`, `on-duplicate-layer`, `on-merge-down`, `on-move-up`, `on-move-down`,
  `on-set-active`, `on-rename-layer`, `on-set-opacity`, `on-commit-opacity`,
  `on-set-blend`, `on-toggle-visible`, `on-toggle-locked`, `on-request-adjustment`,
  `on-apply-brightness-contrast`, `on-clear-adjustments`, `on-bake-adjustments`
  (+ `on-select-preset`, `on-load-brush` on `BrushState`).
* **Dead UI that gets wired**:
  * Brushes dropdown (`bar-home.slint:194-198`) → preset popup + "Load brush…" (M6/M7).
  * Image group gains the previously-nonexistent Adjustments dropdown (no dead button
    exists for it — it is new chrome reusing `ImageButtonVerticalDropdown`).
  * `size-dropdown`/`clipboard-dropdown`/`select-dropdown` are untouched.
* **Dynamic menus via `for`**: `for e[i] in LayerState.adjustments: BarDropdownItem {…}`
  inside `BarDropdown`'s `@children` — verified pattern
  (`build/_deps/slint-src/tests/cases/widgets/radiogroup_for.slint:20`). Fallback if
  the compiler surprises us: hardcode three `BarDropdownItem`s calling
  `request-adjustment("grayscale"|"invert"|"brightness-contrast")` (C++ side unchanged,
  the registry still owns id→factory).
* **Popups**: new ones use `close-policy: close-on-click-outside` and explicit
  `popup.close()` after an item click; `close-on-click` (used by the legacy About
  dialog) is deprecated in 1.17 — do not copy it. Fresh-element-per-open via
  `if`-guard + `init => { self.show(); }` for both parameter dialogs (D9/D10/D11).

## Build changes

Exact lines to add to `add_executable(ls_paint …)` in `CMakeLists.txt` (keep the list
alphabetical within topic dirs, matching the existing grouping):

```cmake
    src/core/filters/builtin_filters.cpp
    src/core/filters/filter_registry.cpp
    src/history/commands/layer_command.cpp
```

Plus, **only if M6 hasn't already added it**, `src/brushes/brush_presets.cpp`.

No `slint_target_sources` change: `slint_target_sources(ls_paint ui/appwindow.slint)`
already tracks imported `.slint` files (`layers-panel.slint` is imported from
`appwindow.slint`). No new libraries: `stb_image` comes from M4's FetchContent setup.
No test target is introduced by M7 (see Verification for the test-harness assumption).

## Verification

**Build**: `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
(fix warnings — `-Wall -Wextra` are on, `CMakeLists.txt:55`).

**Manual script** (top-left alignment checked first, it is the cross-cutting requirement):

1. Launch → workspace fills the window under the ribbon, gray, canvas at top-left (4px
   inset), Layers panel docked right at 240px showing one "Background" row highlighted
   as active; resize the window down to 480px wide — canvas never disappears.
2. Draw with Pencil, Ctrl+Z → stroke undone (M1 regression check).
3. Add → new top row, active; draw on it; Delete → gone; Ctrl+Z → back *with the
   drawing*; Ctrl+Y → gone again.
4. Draw on layer A, Add layer B, draw on B, press Up/Down on B — order changes, active
   highlight follows B; Ctrl+Z restores order only (pixels untouched).
5. Duplicate a layer that has a Grayscale adjustment → adjust brightness on the copy →
   original unaffected (deep filter ownership, criterion 3).
6. Opacity: drag slider → live change; release; Ctrl+Z → exactly one step back.
7. Blend: set a layer to Multiply → visibly different; Ctrl+Z → Normal again.
8. Eye off → layer vanishes (full recomposite); Ctrl+Z → back. Lock on → Pencil/Eraser/
   Fill do nothing, Dropper still picks; unlock restores.
9. `Image > Adjustments > Grayscale` → screen grays, `fx` badge on the row; verify
   non-destructiveness: `Bake` now changes nothing visually (already gray), Ctrl+Z on
   the bake → still gray (filter intact), Ctrl+Z again → color back (filter removed).
10. Brightness & Contrast… → set sliders, OK → applied; one Ctrl+Z removes it.
    Cancel → nothing applied, reopening shows reset sliders (fresh `if` element).
11. `Bake adjustments into layer` on a filtered layer → pixels flattened, badge gone;
    one Ctrl+Z restores filters + original pixels; Redo works.
12. Merge down with upper(Multiply, 50%, grayscale) over lower → combined result;
    Ctrl+Z → both layers byte-restored.
13. Rename: Rename → type → Enter → label changes; Ctrl+Z reverts.
14. `Brushes > Load brush…` → enter a PNG path → preset appears/selected → stroke
    stamps the image alpha-masked in the current color; round/square presets unchanged;
    an invalid path does nothing (returns `npos`).
15. Re-run the structural grep from step 2 (no implicit `Layer` copies).

**Unit-testable pure logic** (no window needed — plan §9; if M1's R7 CTest target
exists by now, add these there, otherwise document them for the test milestone):

* Filters: grayscale luma weights sum to 256; invert is an involution; brightness 0 /
  contrast 1 is identity on all 256 values; each filter touches only `region` (a
  sentinel buffer outside `region` must not change).
* `LayerStack`: `move` permutations preserve the element set and follow `active`
  (property-test all (from,to) pairs on a 4-layer stack); `remove` refuses the last
  layer and clamps `active`; `merge_down(0)` is a no-op; `index_of` round-trips.
* Compositor equivalence: fixture stack + a point-op filter list →
  `composite(stack, R)` == crop of `composite(stack, full_bounds)` to `R`; and a stack
  with zero filters == today's byte output (golden-image guard, §9).
* Commands: scripted undo/redo sequences (add→stroke→remove→undo×3→redo×2) asserting
  layer count, active index, and pixel bytes at each step — this is the D5 index-safety
  invariant as an executable test.
* `BrushPresets::load_custom`: `npos` on missing file; correct dimensions/alpha from a
  fixture PNG.

## Risks & edge cases

* **Binding clobber (highest Slint risk)**: never bind a std-widgets `in-out` property
  that the widget writes (`Slider.value`, `ComboBox.current-index/value`, `CheckBox.checked`)
  to model data — D9's custom controls exist precisely for this. Any new row control
  must follow the same rule.
* **Row/stack index inversion** (D6): every callback is display-order; a single
  off-by-one shows the wrong layer's opacity. The `stack_row()` helper is the only
  place conversion happens — never inline the arithmetic elsewhere.
* **Index safety of history**: relies on LIFO + "all structural changes are commands"
  (D5). If a future shortcut mutates the stack without pushing, `PaintStrokeCommand`
  indices silently corrupt. Guarded by the comment in `layer_command.hpp` and the
  scripted command test.
* **`height: 100%` in the new workspace row** may resolve against the full layout box
  (D8 fallback: drop it, use `vertical-stretch: 1`). Visual check in step 6.
* **`PopupWindow` inside the scrolling layer list** (blend dropdown): coordinates are
  parent-relative and should render unclipped; verify manually, and if a popup is
  clipped, anchor it to the panel-level element instead of the row (design change:
  hoist one shared popup whose target row is set on open — say so in the code if used).
* **Point-op-only filters**: neighborhood filters need region inflation (D4). Adding
  blur later without inflating produces seam artifacts at dirty-rect borders — the
  limitation is recorded in `filter.hpp`.
* **Mid-drag undo**: a Ctrl+Z during an opacity drag would push against
  `m_opacity_before`; mitigated because keyboard shortcuts during a mouse drag are
  unreachable in practice — note it, don't engineer around it.
* **Full-canvas `mark_dirty` on every layer property change** re-composites the whole
  image per slider tick; at mspaint-typical sizes (800×600 default) this is fine and
  matches §7's visibility-toggle precedent. If it ever isn't, the optimization is
  region-limited recomposite for opacity (bottom layer still needs full bounds).
* **Locked layer gives no visible reason** when a click is ignored; acceptable for v1
  (the lock glyph is shown), status-bar hint is M8 polish.
* **Bake history cost**: full-rect `PaintStrokeCommand` = 2 × W×H×4 bytes per bake —
  accepted per §5.5 (infrequent ops may snapshot fully); `kMaxHistoryEntries` (200,
  `config.hpp:12`) bounds the worst case.
* **`has-filters` must bump `revision`** on every filter mutation (including undo/redo
  and bake) or the `fx` badge goes stale — `LayerFiltersCommand` does it; do not add
  filter mutations elsewhere.
* **Non-square custom stamps stretch** to the stamp box (D11) — documented, not fixed.

## Out of scope (explicit)

Per plan.md M7/§10 and the milestone charter:

* **No GPU acceleration, no new renderers, no tiled/streaming canvas** (§10 non-goals;
  the whole filter path is deliberately CPU/software like the rest of the app).
* Drag-to-reorder in the panel (Up/Down buttons only — optional future addition, needs
  no core change).
* Per-layer filter *stack editor* UI (reorder/remove individual filters, per-filter
  param widgets): v1 exposes add (menu), clear-all (menu), the one brightness dialog,
  and undo. `Layer::filters` already supports stacking.
* Neighborhood filters (blur/sharpen) and the `IFilter` radius/region-inflation
  extension they need.
* More blend modes (Overlay/Darken/…), layer masks, layer groups, clipping masks,
  per-layer opacity curves.
* Saving/loading layered documents (file I/O stays flat PNG/BMP/JPEG — M4);
  persisting custom brushes or panel layout across sessions (M8 settings).
* Canvas resize/rotate/flip, selection, crop interacting with layer structure (M2);
  zoom/scroll (M5) — the panel does not add or require either, it only shares the
  workspace row.
* Text/shapes/selection tools operating on locked layers beyond the paint gate;
  drag-to-reorder; touch-specific UX.

## Milestone dependencies

* **Preconditions (already true or from earlier plans in `plans/`)**: M0 (LayerStack /
  compositor / dirty rects / `blend_fn_for`), M1 (StrokeRecorder, PaintStrokeCommand,
  `UiState::refresh`, shortcut table — see `plans/m1-core-drawing-tools.md`).
* **Preconditions from later milestones that must have landed first**:
  * **M4** — `stb_image` (FetchContent) and the file-path conventions used by
    `load_custom`; M4's file-open plumbing, if it exposes a picker, should be used by
    the "Load brush…" dialog instead of the raw path field (drop-in swap).
  * **M6** — `brush_presets.*` + Brushes dropdown preset list + any `Brush`
    hardness/opacity fields. If either is missing, M7's D11 fallback defines the exact
    interfaces to create; do not duplicate built-in presets twice.
* **M7 delivers for later milestones**: layered editing + `LayerState` (M8 settings can
  persist panel state; a future file format can serialize `Layer` incl. filters),
  `IFilter` registry (any later milestone adds filters by registering, never by touching
  the compositor), `BrushShape::Custom` (M8 can re-load the last custom brush path),
  and the right-dock layout slot (M8's status bar / M2's dialogs share the workspace row
  conventions established here).
* **Explicitly NOT required for a "done" mspaint clone** (plan.md §8: M7 is opt-in) —
  the app must remain fully usable if every M7 feature is ignored.
