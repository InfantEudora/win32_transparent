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
  paints. The build bar's tools can now be CLICKED as well as keyed. (The bar is a grid in the bottom right
  corner since 2026-10-08 - below.)
- **Outlines**: four outline-only objects - the selected building, the hovered one, and a selected
  person's house and workplace - each rebuilt alone (the zones with every other building taken out, so
  it is drawn as standing by itself; a field is a slab over its cells, since its crops would outline
  every plant), when what it holds or the zones change. People's outlines are set every frame. Colours:
  selected orange, related pale gold, hover faint.
- **Badges** (the user's, 2026-10-07): ONE over everyone out of doors, on a short stem - his job's icon
  (axe, wheat, bucket, hammer, a log for the jobless who carry) on a dark disc, ringed by his health, the
  icon a water drop while he is short of water (`needs_plan.md`). Only on screen.
- **Pins are the badges**: each person related to the selection (and the selected person) has his badge
  ringed and stemmed in the selection's colour - orange selected, amber related - kept on screen at its
  edge, clear of the bottom edge the HUD's panels stand on, and drawn last so it stays on top. A selection adds colour; nothing moves.
- **The card** (right side): as listed above. A store's chips send a recorded `store_allow` - a PLAY
  command, from play mode - and are dimmed and fixed while the store is next to a workplace.
- **Tool**: `chasm_select` (select a building at x,z or by id, or a person by id; clear; status - the
  selection, the hover, the card as lines, who is pinned).

### The HUD's corners and the neediest (the user's, 2026-10-08)

The bar along the bottom had grown too full. Now:

- **The tools are a grid in the bottom right corner** (`DrawBuildBar`): three columns, a row a group - HOUSE,
  GARDEN, LOT; STORE, WATER, WOODCUTTER; FIELD, BUILDERS, CAMP; ROAD, WALL, BRIDGE; WINCH, ERASE - each cell
  its key and name, the one in hand lit, a click takes it up as before. BUILDING / WOOD stands over the grid
  at its right edge; why a placement was refused, at the bottom in the middle.
- **The jobs overview sits in the bottom left corner** (`builders_plan.md`), lifted over the grid only in a
  window too narrow for both.
- **MOST IN NEED** (`DrawNeediest`), over the jobs: the five people with the least health, the dry first
  where it is even. Each row his badge, his name, what he is short of - DRY red, THIRSTY blue, HUNGRY amber
  (below the water and health he goes for them at, `NEEDS_PRESSING_*`) - and his health in the badge ring's
  colour. A click on a row SELECTS him (`OVERLAY_HIT_PERSON`) and the view glides to him at the zoom it
  has (`cam_seek_person`; the camp's glide still sets its own zoom). The selected one's row is lit orange.
- **NEEDS ATTENTION** (`DrawIssues`, 2026-10-09, `issues_plan.md`), over the grid and its BUILDING line: the
  top five of what wants doing. A row on a place selects it and glides there (`OVERLAY_HIT_BUILDING`,
  `cam_seek_building`); a hint takes its tool up and keeps it up (`OVERLAY_HIT_TAKE_TOOL`). In a narrow window
  the jobs overview goes over it too (`build_stack_top`).
- **`chasm_select op:press px,py`**: a left click at a pixel as the player's, taken on the next pass (paused
  too) - on the HUD it presses what is drawn there, on the scene it selects. `pressed` says what the HUD had
  there and the tool in hand after. Checked with it 2026-10-08: a tool cell takes the tool up and a second
  press puts it down; a row selects its person and the view went 70 units to him in 3 s, distance kept;
  pause and speed (2x, 3x, back to 1x) - so the calendar's buttons are now checked by a click too.

### The goods strip (the user's, 2026-10-09)

Every card that holds goods shows them the same way, so a player can tell at a glance what is UP FOR GRABS
and what is KEPT for a house, a field or a site. A goods line is a header ("Pantry - the Veen family's", the
figure "9 of 19" at the right), a strip over its room cut into a piece per good, and a key under it
naming each good and how many (`ChasmHud.h` / `ChasmHud.cpp`: `HudStock`, `DrawHudStock`; the card's
`CARD_STOCK`).

- **Colours** (`HudGoodColour`): the top bar's wood, food and water, wheat taking the food's gold, and a
  colour of its own for every other food - greens green, beans rose, provisions a pale hardtack (the user
  chose three food colours over one). The top bar reads its colours from the same function. Strips, keys
  and a store's chips run in the top bar's order: wood, the foods, water (`HudGoodsOrder`).
- **The fill says who may take it**: SOLID anyone's - a store, the camp's supplies, a woodpile, a
  collector's water, a house nobody lives in; HOLLOW kept - a lived-in house's pantry and woodshed, the
  camp's pantry and woodshed, a field's harvest (its land's family first), a builders' hut's wheat, beans
  and provisions, what a site has been brought; STRIPED (the user's) what somebody is already on his way
  to take - a site carrier's load, a builder's for provisions, a load fetched home (`EconomyReserved`, the
  same counts `GoodFree` and `FetchHome` keep). Kept or not is `EconomyPrivateStock`, which the rules'
  `PrivateStock` now calls, so the card cannot disagree with what villagers do.
- **A house is two rows** (the user's): food and water in its PANTRY against `room`, firewood in its
  WOODSHED against `woodshed`. The one bar before counted the firewood against the pantry's room and could
  read "18 of 16". The camp the same, plus its SUPPLIES; its pantry was on no card before.
- **A store's "Takes" chips** are their goods' colours: on, filled with it and ticked; off, outlined with
  its swatch.
- **A card too long to end above the bottom right stack** (NEEDS ATTENTION and the tools) moves to its left
  (`build_stack_left`), as the jobs overview rises over it. A camp full of settlers does this.
- `chasm_select` reports a goods line as `{"stock", "whose", "kept", "room", "goods", "reserved"}`.
- Checked in play 2026-10-09 on a village round the start camp, stocks set with `chasm_economy set_stock`:
  every card above, a camp's 5 wheat reserved by a girl fetching home drawn striped.

**Open:**
- A person's pack, and a crate once it can be selected, are still text: the same strip fits both.
- The right click was checked by reading the code, not by a click (`press` is the left button).
- The mouse wheel over the card still zooms the camera.
- A family at home is only on the card - indoors, they have no pin.
- A pin kept at the bottom edge can sit under a corner panel; the badges draw over the panels.

## Who builds what

- Core outline pass: win32-transparent-39.
- Chasm (selection, the building's outline mesh, hover, cards, pins): win32-transparent-c4.

## What belongs together (2026-10-06, the user's)

Selecting something outlines, in the related colour, the PLACES that belong with it - one list,
`FindRelatedPlaces` in ApplicationChasmSelect.cpp, so a new kind of belonging is a case there:

- a woodcutter's hut: his woodpile's lot plot (EconomyState::pile_plot - one lot plot to a woodcutter)
  and his worker's house;
- any other workplace - a field, a collector: its worker's house;
- a house: its family's workplaces - a farmer's field, a woodcutter's hut and lot;
- a person: his house and workplace, and a woodcutter's lot.

The related buildings are merged into one outline-only object (slot 2) and the related ground into
another (slot 3, flat plot tiles - BuildPlotTile in ZoneMesh.h, shared with the ghost), each rebuilt when
the list or the zones change; so any number of related things costs two objects. Only the plot a
woodcutter piles on is his, not the whole lot area round it, which may be another's.

Not yet: gardens belong to nobody (they do nothing yet - the user); a lot plot itself cannot be selected.
