# Selection in play mode

Agreed with the user 2026-10-06. Play mode gets a selection of its own in place of the debug line
mesh: an OUTLINE like Blender's round what is hovered and selected, and a CARD saying what it is. View
only: nothing about selection is simulation state or recorded. Only an action taken from a card (what a
store takes) is a recorded command, as it is from the debug panel now.

## The outline (core)

A general renderer feature, so it goes in core, and nothing changes for an app that does not use it.

1. Objects with an outline colour are drawn a second time into an offscreen MASK - id per object -
   depth-tested against the scene, so the mask knows which of its pixels are hidden.
2. A full-screen pass: a pixel outside the mask with a mask pixel within the width gets that object's
   colour; where two outlined objects meet, a line between them. Width in screen pixels, so the same at
   every zoom. Hidden parts drawn fainter (or not), by a setting.
3. An object may be OUTLINE-ONLY: drawn into the mask and nowhere else. Chasm needs this because its
   buildings are merged into one mesh per terrain chunk: the selected building is rebuilt alone into a
   small mesh that only outlines.
4. No outlined object, no pass and no cost.

**As built (core, win32-transparent-39, 2026-10-06):**
- API: `Object::SetOutline(vec4)` / `GetOutline()` (alpha 0 = none); `Object::SetOutlineOnly(bool)` /
  `IsOutlineOnly()`; `Renderer::outline_width` (2, in pixels of an 810-tall frame) and
  `Renderer::outline_hidden_alpha` (0.35). MCP: `object_outline {id|name, color, only, width, hidden_alpha}`.
- How: `Renderer::OutlinePass`, after the MSAA resolve, GPU pass timer "Outline". The mask
  (`shaders/outline_mask.vert/.frag`) is RGBA32I - index+1, hidden flag, packed colour - with its own
  depth, so the nearest outlined object wins. Hidden is judged by distance from the eye against the
  G-buffer position, with 0.3% + 0.03 of slack, so an outline-only copy lying on a merged mesh counts as
  visible. The line (`shaders/outline.frag`): the nearest other-object mask pixel within width + 0.5,
  coverage softened over the last pixel; between two outlined objects, drawn on one side only.
  Scissored to the outlined objects' projected bounds, padded by the width.
- Outline-only objects are taken out of `renderable_objects` in CullObjects: no other pass, culling or
  picking sees them.
- Cost (chasm, 1440x810, debug): 0 with nothing outlined (the pass does not run; the frame is
  pixel-identical to before); a person and the camp 0.08 ms at width 2, 0.16 at width 4 (0.34 / 0.9
  before the scissor).
- Limits: plain meshes only - instance sets, skinned and line meshes are skipped; morph targets and
  wind are not applied in the mask; without PIPELINE_DEFERRED nothing counts as hidden.

## In chasm

- **Hover**: a thin faint outline (pale), instead of the debug line mesh. **Click**: the full outline
  (Blender orange). Clicking empty ground, Escape or a right click deselects.
- **Related things** in a second, paler colour: a house's family, a workplace's worker, a site's
  carriers, a person's house and workplace.
- **Pins**: a person is a few pixels tall at play zoom, so a family member or worker who is related to
  the selection gets a small pin over his head (overlay, screen space), which finds him across the map.
- **Cards** (overlay, clickable through core's UIMenu hit-testing):
  - house: its family - name, age, job, at home or out - and how many it holds;
  - store: each good as a bar against its room, what it takes (clickable chips: a recorded
    `store_allow`; fixed while next to a workplace), who carries to or from it;
  - workplace: its worker, his skill, what he is doing, what waits to be carried;
  - field: crop, growth;
  - construction site: wood brought of needed, its carriers;
  - camp: who lives there, and how many more houses that takes;
  - a person: name, age, family, skills, job, what he is doing.

## As built: chasm's side (2026-10-06)

Files: `ApplicationChasmSelect.cpp` (all of it), `ChasmHud.h` (the overlay's colours, shared with the
date), hooks in `ApplicationChasm.cpp` (UpdatePick, UpdatePickView, PreRender) and
`ApplicationChasmTime.cpp` (DrawOverlay gathers what the mouse can press).

- **Picking** (physics thread): a person first - the nearest out of doors within 14 px (at 810 high) of
  his middle on screen - then a building BY ITS SHAPE: the ray from the cursor is sampled from 8 above
  the ground it meets down to it, each sample asking the plot under it whether what stands there reaches
  that high (storeys - a site's planned ones too - and some roof, a tent, a winch's gantry). So a roof
  picks its house, not the ground behind it. Then the field or building on the plot under the cursor.
  `chasm_select op:under px,py` reports what a pixel picks, against what the ground alone gives. A click selects, a
  click on bare ground clears; a RIGHT CLICK (pressed and let go within 6 px, so not a pan) puts a tool
  down, or with none clears the selection. Escape still closes the window (core's default), so it does
  not deselect.
- **The overlay takes the mouse** where it draws: DrawOverlay records rectangles (the build bar, the card,
  its close mark, a store's chips) and UpdatePick tests the cursor against them before it picks or
  paints. The build bar's tools can now be CLICKED as well as keyed.
- **Outlines**: four outline-only objects - the selected building, the hovered one, and a selected
  person's house and workplace - each rebuilt alone (the zones with every other building taken out, so
  it is drawn as standing by itself; a field is a slab over its cells, since its crops would outline
  every plant), when what it holds or the zones change. People's outlines are set every frame. Colours:
  selected orange, related pale gold, hover faint.
- **Pins**: over each person related to the selection, out of doors (and the selected person), a dot on
  a stem, kept on screen at its edge, clear of the build bar.
- **The card** (right side): as listed above. A store's chips send a recorded `store_allow` - a PLAY
  command, from play mode - and are dimmed and fixed while the store is next to a workplace.
- **Tool**: `chasm_select` (select a building at x,z or by id, or a person by id; clear; status - the
  selection, the hover, the card as lines, who is pinned).

**Open:**
- The chips and the right click were checked by reading the code, not by a click (MCP has no mouse here).
- The mouse wheel over the card still zooms the camera.
- A family at home is only on the card - indoors, they have no pin.

## Who builds what

- Core outline pass: win32-transparent-39.
- Chasm (selection, the building's outline mesh, hover, cards, pins): win32-transparent-c4.
