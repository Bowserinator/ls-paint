# Icons

## Hand-drawn, replace with real artwork

These files were generated for this project instead of being taken from an icon
set. They are rough approximations of the Microsoft Paint glyphs. Replace them
with proper artwork when a source is available:

| File | Used by | What it shows |
| --- | --- | --- |
| `tools.png` | Tools group button in `ui/bar/bar-home.slint` | a pencil over a brush |
| `brushes.png` | Brushes group button in `ui/bar/bar-home.slint` | a paintbrush with a paint tip |
| `shapes.png` | merged Shapes group button in `ui/bar/bar-home.slint` | a square, a circle and a triangle |

The remaining files in this directory predate the shape and file I/O work and
were not drawn here.

## Generated brush and texture samples

`brushes/generate.sh` draws the nine thumbnails the Brushes picker shows.
`textures/generate.sh` draws the samples the shape Outline and Fill menus show.
Run the script after changing a sample; it rewrites the files in place, so each
picture stays with its generator.

| File | Used by | What it shows |
| --- | --- | --- |
| `brushes/brush.png` | Brushes picker in `ui/bar/bar-home.slint` | a round bristle brush |
| `brushes/calligraphy-1.png` | Brushes picker | a wide flat calligraphy nib |
| `brushes/calligraphy-2.png` | Brushes picker | a narrow angled calligraphy nib |
| `brushes/airbrush.png` | Brushes picker | a nozzle spattering drops |
| `brushes/oil.png` | Brushes picker | a short square oil brush |
| `brushes/crayon.png` | Brushes picker | a blunt crayon |
| `brushes/marker.png` | Brushes picker | a broad chisel marker |
| `brushes/natural-pencil.png` | Brushes picker | a thin wooden pencil |
| `brushes/watercolor.png` | Brushes picker | a round wash brush |
| `textures/no-outline.png` | shape Outline menu | a hollow square crossed out |
| `textures/no-fill.png` | shape Fill menu | an empty square crossed out |
| `textures/crayon.png` | shape Outline and Fill menus | a grainy crayon band |
| `textures/marker.png` | shape Outline and Fill menus | even marker strokes |
| `textures/oil.png` | shape Outline and Fill menus | thick oil strokes |
| `textures/natural-pencil.png` | shape Outline and Fill menus | thin pencil lines |
| `textures/watercolor.png` | shape Outline and Fill menus | a soft watercolor wash |

## Generated text background samples

`text/generate.sh` draws the background selector icon the Text tab shows.
Run the script after changing it; it rewrites the file in place, so the picture
stays with its generator.

| File | Used by | What it shows |
| --- | --- | --- |
| `text/background-frame.png` | Background group in `ui/bar/bar-text.slint` | a serif A in an open dashed square over a blue square; the Opaque selector paints the text fill color behind the square and the Transparent one shows the blue through it |

## Generated ruler digits

`ruler/generate.sh` draws the two sheets the canvas ruler reads its numbers
from: `digits.png` for the strip along the top edge and `digits-turned.png`,
drawn a quarter turn round, for the strip down the left edge. Each sheet holds
the digits 0 to 9 in order, one cell each, trimmed so no cell cuts a glyph; the
script prints the cell size, which `ui/components/canvas-ruler.slint` repeats.
The cells are white: the ruler tints them with the color it draws its labels
in, so a color change needs no regeneration. Run the script after changing it.

| File | Used by | What it shows |
| --- | --- | --- |
| `ruler/digits.png` | numbers on the canvas ruler's top strip, `ui/components/canvas-ruler.slint` | the digits 0 to 9, one 8x9 cell each |
| `ruler/digits-turned.png` | numbers on the canvas ruler's left strip | the same cells turned a quarter turn, 9x8 each |

## Placeholder icons, replace with real artwork

These are copies of existing icons under their final names, so the File menu and
the View tab have a file to point at. Draw the real ones over them.

| File | Used by | Copied from |
| --- | --- | --- |
| `zoom-in.png`, `zoom-out.png`, `zoom-100.png` | Zoom group in `ui/bar/bar-view.slint` | `magnify.png` |
| `file-new.png`, `file-open.png`, `file-save.png`, `file-save-as.png`, `file-exit.png` | File menu in `ui/components/file-menu.slint` | `copy.png` |
