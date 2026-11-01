# Every control, and whether it works

Checked against the built application: the automated UI suite
(`build/tests/lsp_ui_tests`, 31 numbered steps over the real Slint window) plus
screen captures of the window at several sizes with the dropdowns open. A tick
means the control does what its caption says through the real pointer path.

## File tab

| Control | Works | Evidence |
| --- | --- | --- |
| New | yes | ui_tests 22, 24: asks before discarding unsaved work, then empties the paper and clears the history |
| Open | yes | ui_tests 25, 26: reads the file back; a missing file reports itself in the dialog |
| Save | yes | ui_tests 23: writes the PNG, names the document, clears the marker |
| Save As | yes | ui_tests 21: asks for a name and a format (PNG/BMP/JPEG) |
| Exit | yes | ui_tests 31: asks before it goes; the window's own close button takes the same path |

## Home tab

| Control | Works | Evidence |
| --- | --- | --- |
| Paste (ribbon and dropdown) | yes | ui_tests 27, 30: adds the clipboard image, grows the canvas when needed, one undo restores size and pixels |
| Paste from file | yes | ui_tests 27 |
| Cut | yes | ui_tests 9: empties the selection into the clipboard as one edit |
| Copy | yes | ui_tests 9-10 with the paste back |
| Select (three shapes) | yes | ui_tests 3, 13, 14: marquee, ants, grips; the polygon closes on its first vertex |
| Select all / Invert / Clear / Transparent / Crop | yes | ui_tests 6-12 |
| Resize | yes | ui_tests 28: the dialog takes a new canvas size as one undoable edit |
| Rotate ▾ / Flip | yes | ui_tests 8 |
| Pencil, Fill, Text, Eraser, Dropper, Magnify | yes | ui_tests 2, 15h-15j, 18 (magnify zooms in on a left click, out on a right click) |
| Brushes ▾ | yes | ui_tests 29: a preset reaches the next stroke |
| Shapes box, Outline ▾, Fill ▾, Size ▾ | yes | ui_tests 15c-15g and the fill walk over all 20 closed kinds |
| Color 1 / Color 2 / palette | yes | ui_tests 2, 9, 15d |
| Edit colors | no | there is no picker dialog yet; the button is the one ribbon control with no handler (M6 scope) |
| About | yes | opens the About window; a build addition, not part of Paint's ribbon |

## View tab

| Control | Works | Evidence |
| --- | --- | --- |
| Zoom in / Zoom out / 100% | yes | ui_tests 16-19 cover the zoom range and the wheel path |

## Status bar

| Cell | Works | Evidence |
| --- | --- | --- |
| Pointer position | yes | ui_tests 15b |
| Selection size | yes | empty without a selection, the size with one |
| Canvas size | yes | follows the Resize dialog |
| Zoom readout, slider, -/+ buttons | yes | ui_tests 19 |

## Canvas

| Gesture | Works | Evidence |
| --- | --- | --- |
| Left drag | yes | draws or moves, depending on the tool |
| Right press | yes | the square compact menu opens for the selection tool only (ui_tests 6-7) |
| Ctrl+wheel | yes | zooms around the cursor in 60px notches (ui_tests 16-19) |
| Double click with the shape tool | yes | closes the polygon |
