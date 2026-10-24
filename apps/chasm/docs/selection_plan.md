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
- Limits: instance sets and line meshes are skipped; morph targets and wind are not applied in the mask;
  without PIPELINE_DEFERRED nothing counts as hidden. SKINNED meshes are outlined in their pose since
  2026-10-11 (`outline_mask_skinned.vert`, the bones uploaded per draw by `Renderer::AppendBoneData`, the
  scissor box from the joints padded by a third) - before that, chasm's rigged adults had no outline at all.
- Where two outlined objects meet (2026-10-11): the line goes on the one FARTHER from the eye, in the colour
  of the one in front - the mask keeps each pixel's distance - so a selected person standing on his outlined
  post is outlined orange round him. It used to go by object index, and drew his edge in the ground's gold.

## In chasm

- **Hover**: a thin faint outline (pale), instead of the debug line mesh - a PERSON's in full light blue
  (the user's, 2026-10-11: a few pixels tall, he needs it to show), over the related colour, so one of a
  selected house's family is picked out. **Click**: the full outline (Blender orange). Clicking empty
  ground, Escape or a right click deselects.
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

- **Picking** (physics thread): a person first - by his figure since 2026-10-11 ("Picked by the figure",
  below; until then the nearest within 14 px of his middle) - then a building BY ITS SHAPE: the ray from the cursor is sampled from 8 above
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
  where it is even. Health as this round of four ticks began, not as it is (2026-10-24): a baby's or child's
  bars fall on only some ticks of the four, by his id, so two at one health traded places every other tick and
  the card flickered. Each row his badge, his name, what he is short of - DRY red, THIRSTY blue, HUNGRY amber
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
- `chasm_select` reports a goods line as `{"stock", "whose", "kept", "room", "goods", "reserved"}`, and a
  person's load with `"load": true` as well.
- Checked in play 2026-10-09 on a village round the start camp, stocks set with `chasm_economy set_stock`:
  every card above, a camp's 5 wheat reserved by a girl fetching home drawn striped.

**Open:**
- The mouse wheel over the card still zooms the camera.
- A pin kept at the bottom edge can sit under a corner panel; the badges draw over the panels.
- A person standing inside his own outlined workplace (a woodcutter at his hut's door) loses his outline to
  it: the mask keeps the nearer outlined object, and the hut's walls are nearer.

### What a person carries, as a strip (the user's, 2026-10-10)

The user: selecting a person should show what they are carrying **the same way a store shows what it
holds**. Today their card says it in words - "Pack: 3 water, 2 beans" on a line of its own, and a load as
", with 4 wood" after what they are doing. Wanted, not built:

- **Their LOAD**, what they carry for someone else - a woodcutter's logs, a farmer's harvest, wood for a
  site, food fetched home - as a goods line (`HudStock`) against what they can carry. One good at a time
  today (`EconomyWorker::carry`, `carry_good`).
- **Their PACK**, what a posted person carries to live on (`defence_plan.md`, "The pack"), as a second
  line against the pack's size. On a scout it says at a glance how long they can stay out.
- **Each only when it holds something**, as a store's category lines are.
- **The fill means what it means everywhere** (proposed): the pack HOLLOW, their own; a load HOLLOW while
  it is a site's or a house's, SOLID on its way to a store.

To settle when it is built: what a load is measured against, since loads differ by errand; and how the
pack shows two measures in one strip - water in units, food by the days of health it gives
(`NEEDS_PACK_WATER`, `NEEDS_PACK_FOOD`) - for instance without a room, with the days in the header.

**As built (2026-10-11): the pack** - the user asked for it ("in the same colors as the storage would"). A goods
strip on the person's card, hollow ("Pack - his own", "her own"), in each good's colour with the key below, only
while it holds something; it replaces the "Pack:" line. **No room**, as proposed: its pieces share the width by
count, and the header's right says what it is worth in days, "water 12 days, food 10" (a unit of water is
`NEEDS_WATER_WORTH`, food by `EconomyPackFood`). Seen on a scout's full pack, 6 water and 5 beans. **The load is
still text**, not asked for this time.

**As built (2026-10-18): the load** - the user asked for it in **the reserved goods' stripes**, "so it's clear that it's
not intended for themselves" - which settles the proposal above the other way: not hollow or solid by where it goes,
but striped through, key swatch too (`HudStock::f_load`). A load is goods already spoken for, which is what the
stripes mean on every other card. The rest:

- **Header**: "Load - for a site", "for a tower", "for a store", "for the camp", "for the woodpile", "for home",
  "for the ropewalk", "for the builders' hut" (`LoadFor`). Empty where it is not decided yet - a log on his
  shoulder while he goes for a second, or a load still on him while he eats - since the rules choose a store only
  when he sets off with it (`deliver`).
- **Measured against what he can carry on this errand** (`EconomyLoadRoom`): a woodcutter two logs of his
  workplace's (8 at a hut, 10 at one with a loft, 4 at the camp), the camp's water carrier `ECONOMY_CAMP_WATER_LOAD`, anyone else
  `ECONOMY_LOAD`. So the camp's picker shows "1 of 5" as he goes on to the next bush. A woodcutter taking wood
  from his own pile carries one log, and still shows it as 4 of 8: what he could carry, not what he meant to.
- **Only while he has something on him** (`carry > 0`). It replaces ", with 4 wood" after what he is doing.
- Checked 2026-10-18 on `saves/camp_3` and `saves/save_20261018_123527`, every card read each 25 ticks over two
  days: woodcutters to the woodpile and to a store, a collector's water carrier, the camp's picker between bushes,
  people fetching home, children at play.

### How far along (the user's, 2026-10-18)

The user: a progress bar on a selected person's card that shows **how far they are in their current task**.

**As built**: the line saying what he is doing gets a bar under it while that runs on his timer - felling,
picking up a log, unloading, building, laying a road, sowing, drawing water, picking fruit, digging, making
rope or provisions, lifting from a crate, training, playing - filling as he gets on with it. Walking has no bar.

- From the rules, not guessed by the card: every timer is started through `EconomyStartTimer`, which keeps where
  it began in `EconomyWorker::timer_full`, and `EconomyTaskProgress` gives 0..1 (or -1: no bar). `timer_full`
  is saved, so a loaded game shows the bar at once; it is never hashed and no rule reads it.
- **Not while eating and drinking** (`WORKER_SUPPLYING`): the meal has had its own bar since zombies step 2.
- **Picking or drawing by hand** fills once a unit (a fruit, a unit of water), since the rules time them one at a
  time; the camp's picker's bar fills once for the whole plant, and its water carrier's once for the load.
- `chasm_economy`'s workers report it as `task_progress` and `task_ticks`.
- Checked 2026-10-18 the same way as the load, plus someone sent to pick a bush and to draw water by hand.

### Selecting someone indoors, from their house's card (the user's, 2026-10-10)

The user: a house's card shows who lives there, but to select one of them the player has to wait for them
to come outside. Today the card lists the family a line each - "name, age - job, what they are doing",
dimmed while they are indoors - and a person can be selected only by their figure or pin on the map, which
someone indoors does not have. Wanted, not built:

- **A family line on the card selects that person**, indoors or out, exactly as clicking them on the map
  does: their card, and the outlines of what belongs with them.
- **Everything a selection allows then works for someone indoors**: a right click orders them, their card's
  chips work - the scout's "gather supplies" among them (`defence_plan.md`, "Scouting, provisioned first").
- **The camp's card the same**: it lists the settlers waiting for a house.

To settle when it is built: how the map shows that someone inside a house is selected, with no figure to
outline - for instance the house drawn in the selection's colour, or a pin over its roof while selected;
and what a camp's card does with a long list (it already moves left of the bottom-right stack).

**As built (2026-10-11).** A house's or the camp's family line is a card line naming a person
(`Card::Person`, `CardLine::person`): a click on it selects him, at home or out (`OVERLAY_HIT_FAMILY`) - the
view stays where it is, unlike the neediest list's glide: he is in the house on screen, or his pin shows
where he went. **Hovering a line makes him the hover**: his figure outlined light blue if he is out, the line
lit light blue - and the other way round, hovering him on the map lights his line, and a neediest row the
same. **Someone selected at home is shown by his house**, which a selected person's RELATED outline already
draws - nothing new was needed. Checked with `chasm_select press` on the village save: Elin, at home,
selected from house 20's card, her house outlined gold; a right click on open ground then took her out of
the house to a post. The camp's long list is left as it is.

### Picked by the figure (the user's, 2026-10-11)

Selecting people was hard: a person was picked within 14 px of a point at his waist, while his badge - what
a player reaches for - is drawn 20-80 px above that, depending on the zoom. A click on a badge picked the
building behind it, or cleared the selection; up close, his head and feet did not pick him; and someone
standing behind a house stole the clicks on its roof. The user's answer: pick people the way the castle's
surfaces are picked, from the engine's own readback of what is under the cursor.

- **The people are PICKABLE objects** - the static figures, the rigged figures' skeletons and the loads on
  them - and `person_objects` (rebuilt each frame by UploadWorkers) says who each one is. `PlayPickUnder`
  takes the person whose figure the engine read back under the cursor: exact at every zoom, and nobody hidden
  behind a roof is picked through it. The readback counts only if it was read within `PICK_READ_SLOP` of where
  the cursor is now.
- **Failing that, near him** (`PERSON_NEAR_PX`, 8 at 810 high): the nearest whose figure's middle line, feet
  to crown on screen, passes within it - unless what the cursor is on stands `PERSON_HIDDEN_SLACK` nearer the
  eye than he does. At the far zoom a figure is about 4 x 10 px, so this is most of his target; close up it
  lies inside the figure, which is target enough.
- **The badges are not a target** (the user: not needed); they stay the display of health and job.
- **Measured** (village save, 1440 x 810, `chasm_select op under` scanned over each figure): at the far zoom
  (55) a person picks over about 14 x 24 px, at the default zoom (34) about 16 x 28, and close up (12) his whole
  figure, about 25 x 50; a roof in front of someone standing behind a house picks the house; on the ramparts
  scene a guard on the wall's deck picks by his figure the same as on the ground.
- **The tool reads the pixel as the mouse's**: `chasm_select` `under`, `press` and `hover` pin the pixel the
  engine reads back at (`InputController::PinPickPixel`, core) and wait for an answer read there, a few frames
  - so a minimised window's tools see the same picks a mouse would, the castle's surfaces included. `under`
  adds `figure` (the person drawn at the pixel itself) and `read`. `hover` now pins the scene's hover as well
  as the overlay's, and returns it, so the light blue outline is in the next screenshot.

### Several people selected (the user's, agreed 2026-10-11)

Selection has been one thing at a time, and so have orders ("One person at a time for now", `defence_plan.md`,
"Direct orders"). Two things now need several: soldiers for defence, and the two or three people who go out to
found a camp at the chasm's edge (`settlements_plan.md`, "Founding by expedition" - worked on separately). Orders
are already per person and recorded - one `CHASM_CMD_ORDER` each, the pack and "Gather supplies first" each - so a
group's order is several of them, and needs no new command.

**Decided** (the user, 2026-10-11):

- **Purely a selection**: a list of people, view state - not recorded, not saved. A group, an expedition or a
  squad is not something the game keeps; what founding needs is for the settlements plan.
- **Grown-ups only**, since only they take orders. Moving a whole family, children included, is for later if wanted.
- **A left click on bare ground clears the group**, as it clears one selection. A drag never clears.
- **Spread out**: a group sent somewhere stands apart - **on the one plot when it has room for them all**,
  otherwise over the plots round it.

**Proposed** (the details):

- **Choosing them.** Shift+click a grown-up: added, or taken out if already in. Shift+click a name on a house's or
  the camp's card: the same, at home too. A left drag with no tool in hand draws a BOX: every grown-up out of doors
  whose figure's middle is inside it - the selection, or added to it with Shift. A press that moves less than
  `RIGHT_CLICK_SLOP` is a click, not a drag. While dragging, the box is drawn in the light blue hover colour and
  everyone it would take is outlined as hovered. A plain click still selects one thing, a child included.
- **One person is today's selection exactly** - his card, his house and workplace outlined, his pin. Two or more
  are a group.
- **On the map**: each of them outlined orange, his badge pinned, his route's dots. Houses and workplaces are not
  outlined for a group - for three people that is half the village.
- **The group's card**: "3 people", a row each - badge, name, job, what he is doing, how many days his pack holds -
  dimmed while at home. A click on a row selects just him; hovering it outlines him light blue; an × takes him out
  of the group. Under the rows, for everyone posted among them: **Release** and **Gather supplies first**, lit when
  all of them are at it - each person's own command, as on his card.
- **A right click** sends each his own `CHASM_CMD_ORDER`:
  - **One plot when it holds them**: its area at least `ECONOMY_POST_ROOM` (tuning, about 0.8 square units) a person.
    **Everyone posted on the same plot and layer stands in a place of his own on it**, by rank in id order, as a
    guard tower spreads its guards (`EconomyPostPoint`) - one person still on its point. That is a RULES change:
    where people stand moves, so a recording with two posted on one plot plays differently from now.
  - **Otherwise the clicked plot and the nearest round it**, open and on the same layer - along a wall's deck on
    the castle - each filled to its room, as many as it takes. The nearest person to the nearest place.
  - **Refused**, one line for the group: "2 OF 3 SENT - NO WAY THERE".
  - **On a guard tower**, as many as it has places; the rest are not sent.
- **The tool**: `chasm_select` op `select` with `people` (ids), op `add` / `remove` (person), `press` with `shift`,
  op `box` (px0, py0, px1, py1, `shift`); `status` reports the group.

**Build order**:

1. **The group**: the list, Shift+click on the map and on card names, the outlines, pins and routes, the group's
   card with its rows, × and chips; the tool. **Built 2026-10-11**, below.
2. **The box**. **Built 2026-10-11**, below.
3. **Group orders**: places on one plot (the rule), the spread over plots round it, the group's refusal, a tower.
   **Built 2026-10-11**, below.

#### As built: step 1, the group (2026-10-11)

- **The selection**: `PLAY_PICK_GROUP` with its people in `play_group`, in the order added. Every change goes through
  `SelectPeople` - one id a person selected (a child too), two or more a group of those who can take an order
  (`EconomyCanOrder`), none nothing - or `ToggleSelected` for a Shift+click; `SelectedPeople` gives the one or the
  group to whatever acts on them. The dead leave a group on the next pass (`PruneGroup`). View state only, as ever.
- **Shift+click**: on a grown-up's figure, on a name on a house's or the camp's card, and on a row of the neediest list
  (a line naming a person, like the card's) - added, or taken out; the view stays. On a child, a building or bare
  ground it does nothing, and the selection stays. A plain click on someone selects him alone; on bare ground clears.
- **From a house's card**: a house's card is reached by selecting the house, which would lose the group being put
  together. So the people selected before a building was are kept (`play_people_before`), and a Shift+click on a name
  on that building's card adds to THEM - two picked outside, a click on the woodcutter's house, Shift on his name:
  three. Shift+clicks on the map add only to what is selected. Clearing forgets them.
- **On the map**: each of them outlined orange, his badge pinned orange, his route's dots - all the routes in the one
  mesh. A neediest row of anyone in the group is lit orange.
- **The group's card** (`GroupCard`): "3 people", a row each (`CARD_PERSON`) - his badge, ringed orange; name, job and
  what he is doing, dimmed at home; how long his pack lasts by what runs out first ("pack 10 days", "pack under a day")
  - and an x (`OVERLAY_HIT_GROUP_DROP`). A click on a row selects him alone; hovering it outlines him light blue. With
  anyone ordered out: "Posted" (or "2 of them posted"), **Release** - each released - and **Gather supplies first**,
  lit while all of them are at it: pressed with some gathering it sets the rest going, with all it stops them all.
  The person card's chips go through the same code, so for one person nothing changed.
- **A right click** sends each his own order **to the clicked plot**, all of them - or onto the guard tower clicked.
  Standing apart is step 3.
- **The tool**: as proposed - `select` with `people`, `add` / `remove`, `press` with `shift`; `status` reports `group`,
  and a group's card lines as `{"person","text","pack"}`. The box's op came with step 2.

**Checked** (village save, debug, minimised on 8769; `chasm_select press` at pixels the engine reads as each figure):
Shift on Kaat with nothing selected - Kaat alone; on Dolf, on Siem - a group of three; on Kaat again, on Dolf - Siem
alone. A plain click on Bente - Bente alone. Shift on bare ground - the group kept; a plain click there - cleared. Two
selected, house 20 clicked, Shift on Elin's name (at home) - the three of them. Shift on a child's name (Thijs) - the
house still selected. The x on Dolf's row - two left; a click on Siem's row - Siem alone. Shift on a neediest row -
added, the camera unmoved. Three sent by one right click - posted, all on one plot; Gather pressed - all three
gathering, pressed again - none, then with one gathering already - all three; Release - all three released. On screen:
the card's rows, three orange outlines and pinned badges, the routes' dots, Siem's neediest row lit.

#### As built: step 2, the box (2026-10-11)

- **The press**: with no tool in hand a left press on the scene is now a CLICK only once let go within
  `RIGHT_CLICK_SLOP` of where it began - acted on then, not on the press - and moved further, a BOX. So a press that
  becomes a box has selected and cleared nothing on the way, and "a drag never clears" holds. The overlay's buttons
  still act on the press, and a script's press (`chasm_select press`) is let go at once.
- **The box** (`PeopleInBox`): every grown-up out of doors whose figure's middle - half his figure's height over his
  feet - lands inside it on screen, behind a roof or not, as RTS boxes take units behind buildings. Let go
  (`TakeBox`): they are the selection, or with Shift held they join it; a box with nobody in it changes nothing.
  One person in it is one person selected.
- **While dragging**: the box drawn faintly filled and edged in the hover's light blue, and everyone it would take
  outlined light blue (`box_people`) - the selected keep their orange.
- **The tool**: `chasm_select` op `box` px0, py0, px1, py1 - let go as a drag would be, `shift` to add, `in_box` who it
  took; with `hold` drawn as still being dragged, its people outlined, until an op `box` without it.

**Checked** (village save, debug, minimised on 8769, `op box`): a box round Dolf and Bente - the two of them, a group;
an empty box - the group kept; a Shift box round Kaat - three; a plain one - Kaat alone. Held: the box on screen, Siem
outlined light blue and Floris faintly through the roof he stands behind, Kaat (selected) orange. The whole of step
1's checks again, all passing with the click now on letting go. **Not checked by a script**: a drag with the real
mouse - press, move and let go are the mouse's own, which a minimised window's tools cannot drive.

#### As built: step 3, group orders (2026-10-11)

As proposed, with one change: **the spread is the rules', not the view's.** Built first in the view, it sent people to
open plots nobody can walk into - a pocket between a woodcutter's hut and two stores - and they were refused. Only
the economy knows who can get where, so it decides, and it passes such a pocket over rather than refusing.

- **Places on one plot** (`EconomyPostPoint`, a RULE): everyone ordered to the same plot and layer stands in a place
  of his own, by rank in id order - evenly round a ring through its corners (`ZoneTowerCorners`), `ECONOMY_POST_STAND`
  of the way out from its point - as a tower spreads its guards. One alone still stands on its point. Whenever one
  joins or leaves - an order, a release, a guard posted by hand, a tent pitched - those left there are sent to their
  new places at once (`Economy::RePlace`), not at their next look.
- **The room** (`EconomyPostRoom`): a plot's area over `ECONOMY_POST_ROOM`, at least one.
- **The spread** (`Economy::SpreadPost`): a group's orders carry a flag (`CHASM_CMD_ORDER` value[2], 0 in every
  recording made before, which replay as they did). Applied in turn, each one goes to the plot clicked while it has
  room - those already posted there counted - else to the nearest plot round it, searched outward through plots open as
  an order finds them and walkable as a walker's ground is (or, up on the castle, with the deck sent to), that has room
  and **a way there for him** (`CanWalkPosted`) - `ECONOMY_SPREAD_PLOTS` looked at, at most. With none, the plot
  clicked. The view sends the nearest person first, so the nearest take the plot clicked and the farther the ones round it.
- **The group's refusal**: the per-order refusals held back until all of a group's orders are done, then one line -
  "2 of 3 sent - every place is taken", "none of 3 sent - something stands there"; nothing when all went. Given up
  after ten seconds unanswered (`group_orders`, ApplicationChasmTime.cpp).
- **A tower**: the group in order of distance to it, each posted by hand (`CHASM_CMD_GUARD`), so the nearest take its
  places and the rest are refused "every place is taken".
- One person ordered goes to the plot's point, as before.

**Checked** (debug, minimised on 8769): on the village save, three to a plot with room for four - all on it, 0.86 to
0.92 apart, all standing there; one released - the two left moved across the plot from each other. Six to a plot
with room for three - three on it and three on the next, each within its room. Three onto a house - none posted, "NONE
OF 3 SENT - SOMETHING STANDS THERE". One person to the big plot - on its point. On the ramparts scene, four sent up onto
the wall - all on one deck plot (layer 4), in a square, there within 1,500 ticks; nine - five on it and four on the
next deck plot along the wall. A debug tower with two places and three sent - the two nearest its guards (the
automatic one stood down), "2 OF 3 SENT - EVERY PLACE IS TAKEN". A 1,620-tick recording of two group orders (one on a
plot, one spread) and a release, replayed twice: every tick's state hash equal, the same posts at the end. Found on
the way, in core: `input_replay` with a null `file` aborted the app (a JSON `value()` on a null) - it checks the type now.

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
(Both since changed: gardens and lots belong to a house or hut, and a garden or lot still being built is selected
by its patch, with a card of its own - yards_plan.md, "A lot or garden site selected like a field site". A
standing one still cannot be.)
