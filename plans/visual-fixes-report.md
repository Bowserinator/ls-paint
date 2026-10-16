# Visual fixes report

Six ribbon defects and five follow-up reports were fixed against the reference
screenshots in the repository root (`ref.png`, `ref2.PNG` … `ref7.PNG`). This
report records each verdict, the change that addresses it and the evidence used
to check it.

## Evidence

| Path | Content |
| --- | --- |
| `build/snapshots-before/` | 13 window captures at launch sizes 620x700 … 1600x960, before any change |
| `build/snapshots-before/dropdowns/` | dropdowns and the tab switch, before any change |
| `build/snapshots-after/` | the same 13 sizes after the change |
| `build/snapshots-after/dropdowns/` | dropdowns, the tab switch and two plain windows, after the change |
| `/tmp/lsp-audit/audit.md` | the audit that reproduced the defects and measured them |

Reproduce a capture with `tests/snapshots/snapshot.sh build/ls_paint <outdir>`
for the plain window, or `/tmp/lsp-fix/capture.sh <WxH> <out.png> [x y]…` for a
window with a dropdown open.

## Verdicts

| # | Defect | Verdict | Result |
| --- | --- | --- | --- |
| 1 | Colors dropdown at the smallest size does not match the screenshot, colours escape it | Confirmed | Fixed |
| 2 | Shapes dropdown shows a "Thickness" section the screenshot does not have | Confirmed | Fixed |
| 3 | Brushes dropdown misses most brushes and uses a list, not a grid | Confirmed | Fixed |
| 4 | Dropdowns open at the bottom of the bar, not below their button | Confirmed | Fixed |
| 5 | Ribbon groups are evenly spaced instead of left aligned | Refuted | Fixed as report 2 below |
| 6 | Options stay condensed after a tab switch until the window is resized | Confirmed | Fixed |

| # | Follow-up report | Verdict | Result |
| --- | --- | --- | --- |
| 1 | The nine brush types are drawn as shapes, not as brushes | Confirmed | Fixed |
| 2 | At 1200px the ribbon groups show gaps between them | Confirmed | Fixed |
| 3 | A dropdown opens below the bar, not below its arrow | Confirmed | Fixed |
| 4 | Outline and fill cannot be switched, both stay selected and shapes always fill | Confirmed | Fixed |
| 5 | The condensed Tools label is smaller than every other label | Confirmed | Fixed |

## Detail

### 1. Colors dropdown

Reference: `ref7.PNG` (and the expanded group in `ref5.PNG`).

The palette holds 30 colours in three rows, but the grid was sized for two
rows, so the third row and every swatch past the panel's right border were
drawn outside it. The panel itself was also anchored 100px left of its button.

- `ui/bar/bar-home.slint:811` anchors the panel under its button and slides it
  left by the amount its 426px width would leave the bar.
- `ui/bar/bar-home.slint:822` widens the panel to `Styles.bar-colors-menu-width`
  (`ui/styles.slint:48`).
- `ui/bar/bar-home.slint:829,837` puts the two colour buttons beside the
  palette, `:864` puts the picker tile at the end of that row, and `:872`
  captions the panel.

Evidence: `build/snapshots-after/dropdowns/colors-620.png` — the panel spans
x 190…615 inside a 620px window with every swatch inside its border, against
`build/snapshots-before/dropdowns/colors-620-win.png`.

### 2. Shapes dropdown

Reference: `ref3.PNG` (grid) and `ref4.PNG` (outline and fill beside the grid).

The dropdown carried a "Thickness" title, divider and three thickness items.
The references show no such section, and the separate Size group already sets
the same `ShapeStyle.thickness`. The section is gone.

- `ui/bar/bar-home.slint:553-615` holds the grid, the outline and fill buttons
  and the caption.
- `ui/styles.slint:13,54` widen the panel and those two buttons so "Outline" is
  not elided.

Evidence: `build/snapshots-after/dropdowns/shapes-620.png`.

### 3. Brushes dropdown

Reference: `ref5.PNG` and `ref6.PNG` — nine brushes in a four column grid of
brush artwork.

The dropdown listed seven size presets in a single column, and the engine
modelled only size, shape and spacing, so the brush types did not exist.

- `src/brushes/brush.hpp:8,22,42` add `BrushKind`, `BrushProfile` and the kind
  the brush carries.
- `src/brushes/brush.cpp:5` gives each kind its tip, angle, spacing, coverage,
  alpha and grain.
- `src/brushes/brush_engine.cpp:16` supplies the grain, `:26` turns a profile
  into per pixel coverage, `:47` keeps the plain brush on the tool options so
  the existing square and spacing settings still apply.
- `ui/globals.slint:21,187,210,214` add `EBrushKind`, the tool option, the
  gallery struct and its nine entries; `src/ui/ui_state.cpp:189,257` map the
  kind to the engine.
- `ui/bar/bar-home.slint:522,531` draw the grid, four columns wide.
- `icons/brushes/generate.sh` draws the nine thumbnails with ImageMagick.

Evidence: `build/snapshots-after/dropdowns/brushes-620.png` shows nine distinct
thumbs in a 4+4+1 grid, against the seven size rows in
`build/snapshots-before/dropdowns/brushes-620-win.png`.

### 4. Dropdown anchor

Every popup sat at the bar's bottom edge minus its padding, which put its top
edge four pixels inside the ribbon and its contents below the button row.

- `ui/styles.slint:8,9` derive the anchor from the button row height:
  `bar-dropdown-y: bar-button-height - bar-padding` (82px).
- All eleven popups in `ui/bar/bar-home.slint` use `y: Styles.bar-dropdown-y`.
- `ui/bar/bar.slint:34` gives the group content the same
  `Styles.bar-button-height`, so the anchor and the row cannot drift apart.

Evidence: `ref5.PNG` puts the panel's top border at window y=106. Every
captured panel in `build/snapshots-after/dropdowns/` now reports y=106
(brushes x 218…335, colors x 190…615, shapes x 292…559, size x 358…551),
against y=130 for all of them before.

### 5. Ribbon group spacing

Reference: `ref2.PNG` and `ref.png`.

The audit refuted the original claim: the dividers are the group widths, packed
from x=0, and nothing stretches them. The evenly spaced look came from defect
6, with every group collapsed.

The follow-up report was right for a different reason. The Image and Tools
groups hide their content with `visible:` when they merge, and an invisible
layout still holds its slot, so a merged group kept the width of the content it
no longer showed.

- The four `width: Styles.ribbon-width-*` bindings on the groups are gone, so
  each group is as wide as its content.
- `ui/bar/bar-home.slint:359,457` drop the hidden content's width while the
  group is merged.
- `ui/styles.slint:28,29,31` carry the measured expanded widths
  (`ribbon-width-image: 142px`, `ribbon-width-tools: 100px`,
  `ribbon-width-shapes: 312px`) for the collapse arithmetic.

Evidence: at 620x700 the group dividers sit at x = 80, 146, 212, 286, 352, 416
and 482, so the seven merged buttons pack from x=0 with no slack inside a
group; at 1200x800 they sit at 72, 138, 240, 314, 628, 692 and 1118. Before the
change the Image group measured 196px at 1200x800 while showing a 56px merged
button.

### 6. Options stay condensed after a tab switch

`TopBar` recreates `HomeBar` when the tab changes. The bar tracked its width
through a `changed` handler, which does not fire for the width the new instance
is created with, so the collapse conditions read a width of 0 and merged every
group.

- `ui/bar/bar-home.slint:75` binds the width directly:
  `private property <length> strip-width: root.width;`

Evidence: `build/snapshots-after/dropdowns/tabswitch-1100x800.png` and
`-1200x800.png` were taken after clicking View and then Home, and match the
plain window at the same size (ImageMagick `AE` metric: 0 pixels at 1200x800,
one pixel of two levels at 1100x800), against
`build/snapshots-before/dropdowns/tabswitch-home-after.png` from the audit,
which showed every group merged.

### Follow-up 4. Outline and fill

The two buttons in the shapes picker only ever escalated to `EShapeMode.BOTH`,
so both looked selected and a shape always filled.

- `ui/bar/bar-home.slint:591,601` set one mode each: the Outline button sets
  `OUTLINE` and the Fill button sets `FILL`.

Evidence: `build/snapshots-after/dropdowns/shapes-620.png` shows one button
focused, not both. The ribbon's own Outline and Fill dropdowns still offer the
combined mode.

### Follow-up 5. One label size

- `ui/styles.slint:2` keeps `bar-font-size: 12px` as the only ribbon label
  size; `bar-merged-font-size` is gone.
- `ui/bar/bar-home.slint:109,192` widen the merged Clipboard button so its
  longer label still fits at that size.

Evidence: `build/snapshots-after/dropdowns/plain-620x700.png` shows every
merged label, "Clipboard" included, at one size and without elision.

## Verification

- `cmake --build build -j 8` — clean.
- `ctest --test-dir build --output-on-failure` — 6 of 6 suites pass, including
  the 31 step window suite and its brush step 29.
- `tests/snapshots/snapshot.sh build/ls_paint build/snapshots-after` — 13 sizes
  from 1600x960 down to 620x700.
- Anchor measurement, group divider measurement, the tab switch pixel
  comparison and the dropdown captures, as described above.

## Known gaps

- The palette has 30 entries (20 default colours and 10 custom slots), so the
  grid is three rows where the reference shows four.
- The palette does not ring the colour in use.
- The Clipboard dropdown cannot be opened from its button body, because that
  button pastes and only its sub-arrow opens the menu; the shared anchor change
  covers it, but its position was not captured.
- The Edit colors entry in the colors dropdown has no handler. This matches the
  ribbon's own Edit colors button, which has never had one, and the entry it
  replaces had none either.
- The brush thumbnails are generated art, not the reference's artwork.

## Round 3 — the eight follow-up defects (goal `mvahi24l-f6q47m`)

Before-state: `/tmp/lsp-goal3/baseline-audit.md` (all eight reproduced with
captures in `/tmp/lsp-goal3/base/`). After-captures: `build/snapshots-after/round3/`.

| # | Report | Verdict | Fix |
|---|--------|---------|-----|
| 1 | Brushes dropdown: each brush 2x, every choice bordered | fixed | `ui/styles.slint:46`, `ui/bar/bar-home.slint` `BrushGlyphButton` |
| 2 | Brush must not depend on the pencil tool | fixed | `src/tools/tool.hpp:13`, `src/tools/brush_tool.hpp`, `src/tools/tool_registry.cpp:16`, `ui/globals.slint`, `src/ui/ui_state.cpp:149,260`, `ui/bar/bar-home.slint:539` |
| 3 | Shape preview shows the wrong fill colour | fixed | `src/tools/shape_tool.cpp:295,511,536`, `src/tools/tool_context.hpp:36`, `ui/globals.slint`, `ui/appwindow.slint:357`, `ui/styles.slint:52` |
| 4 | Fill mode still needs an outline | fixed | `src/tools/shape_tool.cpp` (`rasterize_shape`, `publish`), `ui/bar/bar-home.slint`, `tests/ui_tests.cpp` step 15d |
| 5 | Fill/Outline chips must not close the dropdown | fixed | `ui/bar/bar-home.slint` (both chips) |
| 6 | Paste from the system clipboard | fixed | `src/ui/ui_state.cpp:635`, `:325`, `:355` |
| 7 | Wide layout: mode buttons top-aligned, menus on the arrow | fixed | `ui/bar/bar-home.slint:714`, `:625-626`, `:643-644`, `ui/components/image-buttons.slint:176-199` |
| 8 | Dropdown-to-arrow distance as one global constant | fixed | `ui/styles.slint:12`, `:15` — **0px** |

### Details

1. The cells are `Styles.bar-brush-cell-size` = `(bar-small-icon-size + 6px) * 2`
   = 52px, and `BrushGlyphButton` now draws a border on every choice
   (`focused ? #8dbcec : touch.has-hover ? #c3ddf8 : Styles.bar-border-light`).
   Measured cell pitch 52px at 620x700 and 1200x800; the panel is 222px wide.
2. `ToolId::Brush` and `class BrushTool final : public BrushStrokeTool` make the
   brush an independent tool. `ETool.BRUSH` maps to it, the picked brush kind
   reaches the nib only for that tool (the pencil and eraser always stamp the
   plain tip), and clicking a thumbnail selects the tool. Covered by ui-test
   step 30 (`tests/ui_tests.cpp:782`).
3. The drag preview now carries the same two colours the commit rasterizes with
   (outline = the pressed button, fill = the other one) instead of a single
   colour, and the shape picker's glyphs use the reference's light fill
   (`shape-glyph-fill: #eef3f7`) for closed shapes.
4. An outline is always drawn, so `EShapeMode.FILL` means fill plus border. The
   outline menu keeps one item that only reports the border; the fill menu maps
   "Solid color" to FILL and "No fill" to OUTLINE.
5. Both chips stay open so the mode can be compared with the preview.
6. `do_paste()` prefers the in-app clipboard and falls back to the system one
   (`AppController::paste_from_clipboard()`), reporting failures in the message
   dialog. Reading goes through the vendored `clip` library (X11 backend); on
   this Wayland session the XWayland clipboard bridge serves the selection, so
   no external `wl-paste`/`xsel` call is needed.
7. `modes := VerticalLayout { height: 100%; alignment: start; }` puts the mode
   pair at the top of the row like the glyph grid beside it. Each button is an
   `ImageButtonHorizontalDropdown` (`ui/components/image-buttons.slint`), which
   exposes its arrow's box, so its panel opens under that arrow:
   `x: modes.x + btn.x + btn.arrow-x` and
   `y: modes.y + btn.y + btn.arrow-y + btn.arrow-height + Styles.bar-dropdown-gap`.
   Measured at 1200x800: the Outline panel at x 610 against its arrow glyph at
   611..614, the Fill panel at x 584 against its arrow at 583..588; the panel
   tops are y 46 and y 72, one under each button.
8. `Styles.bar-dropdown-gap` is the one knob for how far a panel sits below the
   button that opened it, and it is 0px today: a panel opens flush under the
   button. Every menu's vertical position derives from it through
   `bar-dropdown-y: bar-button-height - bar-padding + bar-dropdown-gap` (82px
   inside the group, window y 106). The nine ribbon menus use that line; the
   Outline and Fill menus add the same constant below their own arrow.

### Verification

- `cmake --build build -j 8` — clean.
- `env -u WAYLAND_DISPLAY ctest --test-dir build` — 6 of 6 pass.
- Panel top edges: 620x700 colours/shapes/brushes all y=106, 1600x960 brushes
  y=106 (the line `ref5.PNG` shows). 1200x800: the Outline panel at x 610, top
  y 46, under its arrow glyph at x 611..614; the Fill panel at x 584, top y 72,
  under its arrow at x 583..588. 1600x960: the Outline panel border at x 740
  under its arrow at x 741..744.
- System clipboard: `wl-copy --type image/png` of a 200x120 red/green image then
  Ctrl+V leaves 16 219 red and 7 381 green canvas pixels.

### Known gaps

- The nine ribbon menus open on the shared `bar-dropdown-y` line instead of each
  button's own box; the constant controls that line, and the reference-verified
  106px placement was kept. The Outline and Fill menus are the exception: they
  follow the arrow of the button that opened them.
- The brush thumbnails are generated art (`icons/brushes/generate.sh`), not the
  reference's artwork.

## Round 4 — four follow-up defects (no goal, user list)

Verdicts for the four defects reported after round 3.

| # | Defect | Verdict | Fix |
|---|--------|---------|-----|
| 1 | A filled shape shows a white line on its diagonal edges | confirmed, fixed | `src/tools/shape_tool.cpp` `bridge_fill_to_outline`, called from `rasterize_shape` |
| 2 | A condensed group's button does not look selected | open, needs a decision | not started |
| 3 | The chosen shape cell should be blue-outlined on a light blue fill | confirmed, fixed | `ui/bar/bar-home.slint` `ShapeGlyphButton`, `ui/styles.slint` `shape-glyph-selected-fill`, `shape-glyph-selected-outline` |
| 4 | Paste reports "no image in clipboard" in a modal | confirmed, fixed | `src/ui/ui_state.cpp` `do_paste`, `src/app/app_controller.cpp` `paste_from_clipboard`, `src/io/clipboard.{hpp,cpp}` `get_text` |

### 1. The fill/outline seam

A stroke follows the pixel grid. At thickness 1 and 2 the stamp is not centred
on the ideal edge, so the stroke can miss the pixel next to the fill and leave a
line of background running through the shape. Measured before the fix on a
filled diamond: 29 seam pixels at 60px and 99 at 200px, thickness 1; 27 and 97 at
thickness 2; 0 at thickness 3 and 4. Diamond 94, Pentagon 19, Star5 76 and Heart
79 seam pixels at thickness 1, Rectangles, Ovals and Triangles clean.

`bridge_fill_to_outline` repaints such a pixel with the fill colour after the
fill and the stroke are drawn. Only a pixel that has the fill colour within two
pixels on one side of it and the stroke colour within two pixels on the other is
repainted, so the repair cannot reach outside the band between the two. The
candidates are collected before any of them is painted, so one repair cannot
create the next.

Measured after the fix with the same probe: 0 seam pixels for Diamond, Pentagon,
Star5 and Heart at thickness 1, 2 and 3, and the new
`tests/shape_tool_test.cpp` case `test_fill_meets_the_outline_at_thin_thickness`
checks every closed kind at thickness 1 and 2 (0 failures, 6/6 suites green).

### 3. The chosen shape cell

`ShapeGlyphButton` now draws the chosen cell's glyph with
`Styles.shape-glyph-selected-outline: #1979ca` and
`Styles.shape-glyph-selected-fill: #d6e8fa`, the blue and light blue the
reference uses, and its border takes the same blue. Evidence:
`build/snapshots-after/round4/shapes-selected-620x700.png`.

### 4. Paste with no image

The failure path no longer opens a modal: it writes one line to the console that
names what the clipboard does hold. `Clipboard::has_image` reports whether the
clipboard advertises an image at all, so a library that cannot read an offered
image is distinguishable from a clipboard that holds no image;
`Clipboard::get_text` then prints the text (first 200 characters, newlines
flattened). Evidence: `build/snapshots-after/round4/paste-no-image-620x700.png`
(no dialog) and the console line
`Paste failed: The clipboard does not contain an image; the clipboard advertises
no image; clipboard text: "debug text line one line two"`.

### 3a. Follow-up: the blue is the default, not the chosen state

The user reported the shape icons as still black. Every glyph now takes the blue
outline and the light fill whatever its state
(`Styles.shape-glyph-outline: #1979ca`, `Styles.shape-glyph-fill: #eef3f7`, the
pair measured from `ref3.PNG`/`ref4.PNG`); the chosen cell keeps a bluer interior
(`Styles.shape-glyph-selected-fill: #d6e8fa`) plus the box and the blue border.
Blue outline pixels in the picker: 138 before, 295 after.
Evidence: `build/snapshots-after/round4/shapes-icons-blue-620x700.png`,
`shapes-icons-blue-zoom.png`.

### 2. A merged group button shows its state

A merged group button lights up while its own dropdown is open
(`PopupWindow.is-open`) and while a tool of its group is the active tool. The
groups with a tool: Image (Select), Tools (pencil, fill, text, eraser, dropper,
magnifier), Brushes (the brush tool), Shapes (the shape tool). Clipboard, Size
and Colors have no tool of their own, so `is-open` alone marks them.
Evidence: `build/snapshots-after/round4/merged-highlight-brushes-620x700.png`
(1454 lit pixels against 144 in a plain capture) and
`merged-highlight-tools-620x700.png` (444 against 0).

### 4a. Why a browser image never pasted

The app is a native Wayland client when it starts from the user's session
(`WAYLAND_DISPLAY` set; Slint's winit backend then uses Wayland and there is no
X11 window to find). The vendored `clip` library speaks only X11, so it saw none
of the Wayland clipboard's content: the paste reported "the clipboard offers
nothing" even with an image on the clipboard. Through the X11 bridge the answer
depends on size: a 1.8 MB PNG pasted, a 9.9 MB 1600x1200 PNG came back truncated
(`libpng error: IDAT: invalid stored block lengths`).

`Clipboard` now reads the Wayland clipboard first when `WAYLAND_DISPLAY` is set:
`wl-paste --list-types` lists what is offered, `wl-paste --type <type>` fetches
the bytes, and the bytes are decoded by the existing stb path through the new
`load_from_memory` in `src/io/document_io.hpp`. The X11 library stays the
fallback, and `Clipboard::available_types` feeds the failure note, so a paste
that fails prints what the clipboard really offers.

Probe evidence (`/tmp/lsp-probe/clip.cpp`, linked against `build/liblsp_core.a`
plus the clip objects) with a 9.9 MB 1600x1200 PNG on the clipboard:

    WAYLAND_DISPLAY=wayland-0: type: image/png / has_image: 1 / image: yes 1600x1200
    WAYLAND_DISPLAY unset:     has_image: 0 / image: no

Known limits: reading needs `wl-paste` on the PATH; writing still goes through
the X11 library alone, which XWayland bridges to Wayland applications.

## Round 5 — a pasted image arrives as a ghost selection (user request)

A paste used to blit the block into the layer immediately and grow the canvas
around it. It now installs the block as a floating selection: the layer keeps
its pixels, the block hovers with the marching ants and the eight grips, it can
be dragged and resized, and clicking outside applies it.

| # | Requirement | Verdict | Where |
|---|-------------|---------|-------|
| 1 | A paste is a selection, not an immediate edit | fixed | `src/selection/selection_ops.hpp:96` `float_block`, `src/app/app_controller.cpp:119` `install_floating` |
| 2 | The hovering block can be moved | fixed | `src/tools/selection_tool.cpp:170-174` drags a float |
| 3 | The hovering block can be resized | fixed | `src/selection/selection_ops.cpp:295` `resize_selection` rescales from `source`/`source_mask`, which `float_block` fills |
| 4 | Clicking outside finalizes the placement | fixed | `src/tools/selection_tool.cpp:148` (a press outside the bounds applies the float) |
| 5 | The paste stays one undo step, even when the canvas grew | fixed | `src/history/commands/canvas_resize_command.hpp:19` `floating_after` |

Mechanism: `float_block` builds a floating copy whose `origin` and `origin_mask`
stay empty, so applying it paints the block and restores nothing behind it;
`install_floating` (`src/app/app_controller.cpp:119`) installs it and points the
selection mask at the block's box; both paste paths
(`src/app/app_controller.cpp:199` for the in-app clipboard, `:338` for an image
from the OS clipboard or a file) hand the block to the selection tool
(`src/ui/ui_state.cpp:650`, `:791`), so its grips own the hover.

Evidence:

- `tests/selection_tests.cpp` — `test_paste_hovers_and_can_move_before_it_lands`
  (the layer under the block is still white until the selection is dropped) and
  `test_paste_can_be_resized_before_it_lands` (an 8x8 resized block lands as 64
  coloured pixels); `test_cut_copy_paste_round_trip` was rewritten for the new
  contract. All six suites pass: `ctest` 0 failed out of 6.
- GUI captures at 1200x800 (`build/snapshots-after/round5/ghost/`,
  harness `harness.sh`): a stroke is drawn, cut, pasted back, moved, and applied.
  Region means (1.0 = blank) over the stroke's original row and its moved row:

      capture                original row   moved row
      1-stroke               0.9375         1.0
      2-cleared  (after cut) 1.0            1.0
      3-hover    (after ^V)  0.9375         1.0
      4-moved    (dragged)   1.0            0.9375
      5-applied  (clicked)   1.0            0.9375
      6-settled  (more)      1.0            0.9375

  The floating block is visible in `3-hover.png` as a canvas-sized selection
  with ants and grips, and `4-moved.png` shows the same content moved with the
  vacated area blank, which is what "not destructively editing" means here.

Known limits: the block lands at the canvas origin; a failed paste still only
prints to stderr. Resizing is verified by the unit test above, not by a capture:
a GUI resize attempt moved the block but its capture sequence was ambiguous, so
it is not claimed as a visual result.
