# Issues: what needs the player's attention

The user's, 2026-10-09. A list of what wants doing, at the top of the panel with the build options, so a
forgotten choice is not left lying - the first case a field nobody chose a crop for - and so the same list
can say what to do next while the player learns the game: no woodcutter, no water collector, no store. A top
five, like the villagers' MOST IN NEED. Proposed and agreed the same day ("both are good decisions the way
proposed, as are the first 8 issues").

## Decided (the user, 2026-10-09)

- **The panel**: NEEDS ATTENTION, over the tools' grid in the bottom right corner (over the BUILDING / WOOD
  line when that is up), the grid's width. Up to five rows, the most urgent first; hidden with nothing to say.
  It looks like MOST IN NEED (`DrawNeediest`): a small title, a row an issue.
- **A click on a row** does the obvious thing: on a place, selects it and the view glides there (as a click
  on a person in MOST IN NEED does); on a "build" hint, takes up that tool.
- **Two kinds of timer**:
  - a COUNTDOWN, where the game has a sensible default: when it runs out the game chooses for the player.
    Today only the crop.
  - a DEADLINE ("WATER 6 DAYS"), which resolves nothing - it ranks the row and says how urgent it is.
- **A crop left unchosen defaults to AUTO after one game day** (`ECONOMY_CROP_GRACE`, tuning): AUTO is the
  next crop's AUTO - the most food of the crops that still ripen before the frost - now a STARTING crop too,
  and on the card's "Start with:" row as a chip of its own. Not a fixed crop: wheat chosen for you in late
  summer would wait for spring.
- **Hints say what to do** ("BUILD A STORE"), not what is missing: they are the tutorial. They follow their
  condition, not a one-off - erase your only woodcutter in year three and its hint is back.

## The first eight

| # | Row | Shown while | Timer / value | Click |
|---|---|---|---|---|
| 1 | FIELD: CHOOSE A CROP | a field built, never sown, with no starting crop | the countdown, then AUTO | selects the field |
| 2 | BUILD A WATER COLLECTOR | no collector placed | WATER n DAYS | the WATER tool |
| 3 | BUILD A FIELD | no field placed | FOOD n DAYS | the FIELD tool |
| 4 | BUILD A WOODCUTTER | no woodcutter's hut placed | WOOD n | the WOODCUTTER tool |
| 5 | BUILD HOUSES | anyone living in the camp (or nowhere) | n IN THE CAMP | the HOUSE tool |
| 6 | BUILD A STORE | no store placed, once a woodcutter or collector stands | - | the STORE tool |
| 7 | NOBODY AT THE ... | a workplace standing with a place and nobody in it (the card's "Nobody works here") | - | selects it |
| 8 | WOODCUTTER: NO ROOM FOR WOOD | a hut standing that cannot keep a log - too small indoors, and no lot (`woodcutting_plan.md`; was NO LOT TO PILE ON, `yards_plan.md`) | - | selects it |
| 9 | HOUSE SHUT IN | a house with no door - shut in on every side (`EconomyShutIn`); RED at the very top with anyone living in it, who cannot get out | n INSIDE | selects it |
| 10 | HOUSE CROWDED | a house standing with more living in it than it holds - a child born into a full house (`lifeline_plan.md`) | n IN ROOM FOR m | selects it |
| 11 | ...: NEEDS ROPE | a site that takes rope the colony lacks and makes none (`goods_plan.md`) | GROW HEMP / BUILD A ROPEWALK | a field, or the tool |
| 12 | TREES IN THE WAY: NO WOODCUTTER NEAR | a road, wall or site with a tree in its way that no woodcutter reaches - only a woodcutter fells (`woodcutting_plan.md`) | n PLACES | the WOODCUTTER tool |

Rows 9 and 10 are the user's (2026-10-09, with births: "a house that holds 3, getting a kid would mean we need to
expand it").

**The order**: red first - a hint whose deadline is under two days (rows 2 and 3); then the countdowns, the
least time left first; then what is stuck (7, 8); then the next steps in the table's order. A new map opens
with rows 2-5: the first screen is the checklist.

**The days**: what the colony holds - stores, the camp's supplies and the houses' pantries - over everyone,
children too. Water: a unit is `NEEDS_WATER_WORTH` of one person's bar. Food: each unit at what it is worth
eaten (`EconomyFoodWorth`). Not what lies at workplaces or is carried, and nothing of what is being made: a
deadline for a colony with no source of it, which is when the hint shows. At the start (12 settlers, 40 water,
40 wheat and 30 beans) that is about 6 days of water and 8 of food - and greens take 8 days to grow, so the
field is the hint that teaches the most.

## How it works

- **The list is DERIVED**, worked out on the render thread from the published zones and economy each frame,
  like MOST IN NEED: never saved, never hashed, nothing a tick reads. `ChasmIssues` in
  `ApplicationChasmIssues.cpp` makes it, sorted; the panel draws its first five and `chasm_issues` returns all
  of it.
- **The crop's countdown is a RULE**, since what it does changes the run: it counts ticks, so it stops with
  the pause and runs four times as fast at 4x, and a replay does it again on the same tick.
  - `EconomyField::wait`: ticks a field has stood with every cell built, never sown, its starting crop
    `ZONE_CROP_NONE` - 0 when it is not waiting. Saved (a field record's ninth number, written only while it
    waits) and hashed with the record.
  - At `ECONOMY_CROP_GRACE` the economy names a cell of it (`Economy::CropDefaults`) and the app sets its
    starting crop to `ZONE_CROP_AUTO` on the same tick (`ZONE_OP_FIELD_CROP`), as it raises what the builders
    finished - not a command, since it follows from the state.
  - A starting crop of AUTO sows `EconomyAutoCrop` of nothing harvested - the most food per growing day - or,
    the frost allowing, the most of those still in time (`EconomyFieldSowNow`), like the next crop's AUTO.
- **The card**: an empty field with no crop shows the countdown as a bar ("Choose a crop to sow - else Auto");
  "Start with:" has an "Auto: <its pick>" chip, which the player can take as well, and is lit once the
  countdown has chosen it.

## Later

Easy once the panel exists, and each the user's call: a field lying fallow; a field nobody can reach; "the
frost will take it, n days short"; people carrying with nowhere to put it down (stores full); sites with
nobody free to carry; winter coming with too little firewood; a collector frozen in winter; water or food
running low WITH a source (the deadline alone, as now, would mislead: one collector fills about 11 units a day).

## As built (2026-10-09)

All of it, the first eight rows. Files: `ApplicationChasmIssues.cpp` (new: `ChasmIssues`, `DrawIssues`,
`ChasmBuildingPoint`, `chasm_issues`), `ApplicationChasm.h` (`ChasmIssue`, `ISSUE_*`, `OVERLAY_HIT_TAKE_TOOL` 12 and
`OVERLAY_HIT_BUILDING` 13, `cam_seek_building`, `build_stack_top`), `ApplicationChasmTime.cpp` (`DrawBuildBar`
returns its top; the jobs overview, lifted in a narrow window, goes over the whole stack), `ApplicationChasm.cpp`
(the glide to a building, `chasm_paint field_crop auto`, the panel's auto radio), `ApplicationChasmSelect.cpp` (the
two clicks, the card's Auto chip and countdown bar, `chasm_select press` naming `take_tool` / `building`),
`Economy.*` (`ECONOMY_CROP_GRACE`, `EconomyField::wait`, `CropDefaults`, AUTO as a start in `EconomyFieldSowCrop` /
`EconomyFieldSowNow`), `ApplicationChasmEconomy.cpp` (applied on the tick; `wait` hashed with the field record),
`ChasmSave.cpp` (the field record's ninth number), `Zones.*` (`ZONE_OP_FIELD_CROP` and a save's restore take AUTO).

- **A hint's click keeps its tool in hand** (`OVERLAY_HIT_TAKE_TOOL`), where the grid's own cells put a tool down on
  a second press. **A place's click** selects it and glides there at the zoom the view has, like the neediest list.
- **Checked** (debug, a copied exe on 8774, paused and stepped):
  - a new map lists BUILD A WATER COLLECTOR (WATER 6 DAYS), BUILD A FIELD (FOOD 8 DAYS), BUILD A WOODCUTTER
    (WOOD 40), BUILD HOUSES (12 IN THE CAMP);
  - a field painted by the camp: FIELD: CHOOSE A CROP at the top, BUILD A FIELD gone;
  - 1000 ticks on, wait 1002 and 0.55 of the countdown left, on the panel's line and the card's bar;
  - saved and loaded, wait 1003 a tick later;
  - at 2250 the row went, the card lit "Auto: Beans", and the camp's farmer went to sow beans at 100%;
  - a woodcutter placed with its lot: BUILD A WOODCUTTER gone, BUILD A STORE in. Its lot erased: WOODCUTTER: NO LOT
    TO PILE ON at the top, and a click on it selected the hut and took the view from the camp to it;
  - a hint pressed twice: WATER in hand both times;
  - with MCP and ImGui off (as `make ship`), the changed files compile clean.
- **A hut could not be PLACED without a lot** (Zones refused it), so row 8 was for a lot erased afterwards. Since
  `yards_plan.md` a hut is placed bare and row 8 is the usual way to hear it has no lot.
- **Not checked by a run**: NOBODY AT (every test hut found a worker - it needs more workplaces than free adults),
  and the red tier (a hint under two days).
- **Rows 9 and 10** (added with births, lifeline_plan.md step 2): in `ChasmIssues`' loop over buildings, with a
  count of who lives where beside its `working`. Checked on seed 12: a storey taken off the Wevers' two-storey house
  after their baby was born - HOUSE CROWDED, 5 IN ROOM FOR 2, and the card's "Crowded - add a storey or a plot". The
  shut-in row by the flag it reads, which was checked on its own (lifeline_plan.md, "a building's door").
