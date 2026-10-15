# Cleanup pass — findings and dispositions

Two read-only reviews fed this pass: one over `src/**/*.{hpp,cpp}` and
`tests/*.cpp`, one over `ui/**/*.slint`, `tests/ui_tests.cpp` and
`tests/snapshots/snapshot.sh`. Each finding is listed with the file it came from
and what was done about it.

## Fixed

1. `src/io/clipboard.cpp` — `read_channel` shifted a 32-bit word by a mask width
   that can be 32, which is undefined. The channel is now scaled in 64 bits.
2. `src/core/canvas_document.cpp` — `restore` walked the snapshot without
   checking its length, so a short snapshot left part of the stack untouched and
   an empty one read layer 0. The loop is clamped and the base layer is read only
   when a buffer exists.
3. `src/core/compositor.cpp` — a layer whose size differs from the canvas was
   composited out of bounds; such a layer is skipped now.
4. `src/io/document_io.cpp` — the BMP/JPEG row index `x * 3` was computed in
   `int`; it is a `std::size_t` now.
5. `src/core/types.hpp` — added `distance(Point2f, Point2f)`. Both
   `src/tools/selection_tool.cpp` (`distance_to`) and `src/tools/shape_tool.cpp`
   (`distance`) carried their own copy; both copies are gone.
6. `src/selection/selection_ops.cpp` — the three-line `centred_on` helper had one
   caller and is inline again.
7. `src/ui/ui_state.hpp` — a comment beside `do_commit_text` described a
   different member; it moved to the member it describes.
8. `src/selection/selection_mask.hpp`, `src/selection/selection_ops.cpp` —
   comments described how the data is stored rather than what the code does.
9. `ui/appwindow.slint`, `ui/bar/bar.slint` — dropped the unused `Button`,
   `VerticalBox` and `HorizontalBox` imports.
10. `ui/globals.slint`, `src/ui/ui_state.cpp` — the `file-choose` callback was
    declared and bound but never invoked; removed.
11. `tests/ui_tests.cpp` — `abs` on an `int` difference now reads `std::abs`.
12. `src/tools/shape_tool.cpp` — the heart was fitted to a hard-coded 22-unit
    band that the sampled curve exceeds, so it sat high and narrow; the curve is
    measured and fitted now. The bolt's unit vertices were replaced because its
    outline crossed itself at the waist, which cancelled the fill along one edge.
13. `tests/shape_tool_test.cpp` (new) and `tests/ui_tests.cpp` — the geometry
    rules are now asserted for every kind: a simple outline, a fill with no
    enclosed background, a fill that covers more ground than the outline, a heart
    wider than it is tall, and a walk of every closed kind through the real
    pointer path in both modes.

## Deliberately kept

- `src/core/canvas_document.hpp` — `restore(vector<PixelBuffer>)` instead of the
  M4 plan's `replace_contents`/`set_size`. The existing method already does the
  whole job; the deviation is recorded in `plans/m4-file-io-clipboard.md`.
- `src/core/layer.hpp`, `src/core/blend_modes.hpp` — `Layer::name` and the
  `Multiply`/`Screen` modes are unreachable with the single layer v1 ships, but
  the data model in `plan.md` §5 specifies them for M7.
- `src/tools/tool.hpp` — `ToolId::Resize`, `Rotate` and `Magnify` have no
  registered `ITool`: Resize opens the resize dialog, Rotate opens the dropdown
  and the magnifier is handled by the canvas zoom path. The values stay because
  they mirror `ETool` and `tool_from_slint` maps them.
- `src/core/raster.hpp` — `bounds_line` is used by the tests. It belongs to the
  primitive set and is unit tested.
- `src/selection/selection_ops.cpp` — `rect_from_corners` and the shapes'
  `normalized_rect` round differently on purpose: a selection wants the inclusive
  pixel bounds the user dragged, a shape wants the box the primitive rasterises.
  `resample` repeats index arithmetic across its two overloads; folding them
  would need a template over the pixel type, which is more code than it saves.
- `ui/bar/bar-home.slint` — the rotate and flip rows appear both in the ribbon
  dropdown and in the canvas menu, and the brush preset list repeats the Size
  list. The menu and the ribbon are separate components with no shared model, and
  the two lists set different options, so they stay separate.
- `ui/globals.slint` — the review listed `cursor-x`/`cursor-y` as unused; they
  feed the status bar's pointer cell (`ui/statusbar.slint:43`) and are kept. The
  Edit colors button is the only ribbon control without a handler until M6.
- `ui/bar/bar-home.slint` — the Edit colors button has no picker until M6.
- `ui/bar/bar-home.slint` — the `grp-*` ids name the ribbon groups for the
  measurement and snapshot tooling.
- `tests/ui_tests.cpp`, `tests/selection_tests.cpp` — the pixel-scan helpers and
  the per-test fixture repeat a few lines on purpose; each keeps its own
  tolerance and alpha rule, and the suite follows the framework-free style of the
  other test files.

## Verification

- `cmake --build build -j` is warning-free and `ctest --test-dir build` passes
  six of six suites (`lsp_tests`, `lsp_raster_tests`, `lsp_text_tests`,
  `lsp_io_tests`, `lsp_shape_tests`, `lsp_ui_tests`).
