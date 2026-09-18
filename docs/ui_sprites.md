# UI sprites recovered from the bomber mockups

The bomber UI exists so far only as flat painted mockups - `apps/bomber/assets/images/splash.jpg`
and `art_source/bomber_ui_example_2.png`. A UI framework needs the opposite of a mockup: blank
widgets with an alpha channel that can be drawn at any size and have real text put over them.

These are the pieces recovered so far, and the numbers a 9-slice needs. Everything here is
reproducible - the two tools below rebuild the PNGs from the mockups, so the assets are outputs,
not hand-painted originals, and re-running after a mockup is redrawn is the intended workflow.

| asset | size | slicing | from |
|---|---|---|---|
| `ui/button_blank.png` | 242 x 104 | **3-slice**, l/r = 20 | splash.jpg, the OPTIONS button |
| `ui/window_frame.png` | 472 x 388 | 9-slice, 30 all round | bomber_ui_example_2.png, the VIDEO panel |
| `ui/button_close.png` | 118 x 107 | fixed size, do not slice | bomber_ui_example_2.png |

**The button is a three-slice, not a nine.** Its natural vertical insets would be 42 and 40, which
leaves 22 px of stretchable middle in a 104 px sprite - so drawn at anything shorter than 82 px the
vertical clamp removes the middle row and the two halves of the plank meet in the centre. The
bomber menu's BACK button is 68 px and showed exactly that. A button only ever stretches to fit its
label, so its height belongs to the artist and the whole sprite scales to it; only the end caps'
width has to be preserved. Nine-slicing is for the window frame, which resizes in both axes.

Names resolve as `<category>/<file>` against the app's asset roots, so these are `ui/…` - see
`core/File.h`.

```bash
python tools/ui_extract_splash.py [--demo DIR]            # button_blank
python tools/ui_extract_options.py [--out DIR] [--proof DIR]   # window_frame, button_close
python tools/ui_extract_probe.py [mockup.png]             # re-derive the coordinates
```

Windows Python only - MSYS's has no Pillow. See the "Two pythons" note in `CLAUDE.md`.

## The sheet, the theme, and how they are drawn

The three PNGs above are packed into `ui/uisheet.png` with `ui/uisheet.json` beside it. Two files
carry the metadata, and the split is the point:

- **`uisheet.json` is GENERATED** by the packer, rewritten in full on every repack. It says where
  each sprite is and nothing else, because that is all a packer knows. Never hand-edit it.
- **`theme.json` is HAND-AUTHORED.** It says what the sprites MEAN - which one is the window frame,
  where its nine-slice cuts fall - none of which is derivable from a packed atlas, all of which has
  to survive a repack.

Sprites are addressed **by name**, so repacking is free: positions churn and nothing cares. A
renamed *input file* does break it, which is why roles live in the theme rather than being parsed
out of filename suffixes.

`core/UISheet.h` reads both. It accepts either exporter shape this packer has produced so far - the
TexturePacker hash format (a `frames` wrapper, `w`/`h`, `meta.image`) and the flat one
(`width`/`height`, no `meta`) - because the setting changed once between two packs of the same art
and a loader that read only one was already broken. It refuses trimmed or rotated frames rather
than drawing them in the wrong place.

nlohmann::json costs nothing here. It is **not** behind `USE_MCP`: `core/Application.h` includes it
unconditionally, and `GLTFLoader.cpp` - never dropped from any build - is built on tinygltf, which
is built on it. Verified by building `make ship` (`USE_MCP=0 USE_NET=0 USE_IMGUI=0`), which loads
the sheet and theme out of the baked asset blob.

Drawing goes through `UIOverlay::AddNineSliceSprite`. The theme atlas is a **second sampler** in the
same shader, chosen per vertex by `ui_vertex::sprite`, so glyphs, rounded boxes and artwork all stay
in **one draw call** - the property the overlay was built around. The font atlas could not hold the
artwork: it is `GL_R8` and a distance field, and painted wood is neither.

## How the text came off the button

Not by inpainting. A gradient scan across the button shows it is *constant along x* everywhere
except its two end caps and the lettering between them, so plain plank carries no information
along x, only along y. The lettered span is therefore rebuilt by interpolating between one clean
column taken from each side of it, and the rim, both grooves, the three plank faces and their
bevels all come back exactly - each copied along the axis it is constant on.

The trap: source columns must sit strictly inside the measured flat runs. A first attempt
mirror-tiled a wider strip, dragged the end-cap bevels into the middle, and produced a repeating
chocolate-bar pattern.

## How the panel and cross were cut

Alpha is a flood fill inward from the border, never a colour threshold - the cave wall behind is
not uniformly dark (lit stones, moss) and the frame is not uniformly light (its bars have dark
gaps that connect outside to interior, so a plain fill leaks through and hollows the panel out).

What makes it work is that both shapes are convex. After the fill, taking each row's span between
its first and last opaque pixel closes every gap and reproduces the silhouette exactly, so a leak
stops mattering. The mask is then grown 3 px to take back the dark outline the art draws around
each shape - that outline is as dark as the cave wall, so the fill counts it as background, and
without this step wood meets transparency with no edge and reads as a cut-out.

The panel interior is flat neutral grey, so it is *repainted* rather than preserved, which is what
removes the title and every widget. A thin band just inside the frame is kept from the art, because
the inner bevel and its shadow live there and belong to the frame.

## What these are not

- **The button and the frame come from different mockups and their wood tones differ** - the splash
  button is warmer and more orange than the options-screen frame. For one coherent theme, cut a
  button from `bomber_ui_example_2.png` instead; it has a left nav column, a RESET TO DEFAULTS, and
  red/green CANCEL/APPLY buttons, all in the frame's palette.
- The button's middle span loses the faint wood grain, and a small V-notch chipped into its top rim
  at x≈822 in the splash. Both are arguably better gone on a widget that stretches.
- The panel interior is flatter than the painted one, which had a subtle mottled texture.
- `button_close.png` is one fixed sprite. It has no meaningful stretch axis; scale it, do not slice
  it.
- No sprite here is a hover or pressed state - the mockups only ever show one state per widget. The
  themed path derives them instead: `AddSprite` multiplies the texel by a colour, so the bomber
  menu's held button is the same sprite drawn with a darker tint. That is one number rather than a
  second sprite to author, pack and keep in step, and it is why states were never extracted.

## Still in the mockup, not yet cut

Checkboxes, radio buttons, the slider track and its octagonal knob, the left-arrow/right-arrow
spinner, the stone title bar behind "OPTIONS", and the coloured buttons. The slider track and the
title bar are the two that want the column trick rather than a plain cut, being constant along x
the way the splash button is.
