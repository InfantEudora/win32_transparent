# Yards: lots and gardens that belong to a building

The user's, 2026-10-09. Gardens and lots have been ground that belongs to nobody, and a woodcutter's woodpile
was "a lot beside the hut" that no other hut stood beside. They become a building's own, its YARD: a house's
garden and lot, a woodcutter's lot. Talked through the same day; proposed and agreed ("yeah it great").

## Decided (the user, 2026-10-09)

- **A woodcutter can be built without a lot.** It is placed like any building. Without a lot of its own it
  does not work, and says so, as a house shut in on every side does: its card, and NEEDS ATTENTION's
  "WOODCUTTER: NO LOT TO PILE ON" (`issues_plan.md`, row 8). Until now a hut was refused without one.
- **Lots and gardens belong to a building**, as fields belong to a house (`land_plan.md`).
- **The way to give a building a yard: select it first, then paint.** With a house or a hut selected, the
  LOT or GARDEN tool paints ground that is that building's. Its card has "Add lot" / "Add garden", which take
  the tool up with the building still selected.
- **What a house's yard does is for later.** The user's idea: a house's lots extend its storage. Not now.

## Proposed, and agreed

### Who owns what

Changed 2026-10-10: a yard's plot is to touch its building itself, never only its yard - "Noted", below.

- **Owners**: a HOUSE owns gardens and lots; a WOODCUTTER owns lots. No other kind owns ground, and roads and
  walls are never owned. A plot has one owner, or none.
- **Painted with a building selected**: each plot must touch the building, or ground of its yard - so a yard
  grows out from the building, a plot at a time. A plot that does not is refused: "a lot goes beside its
  building, or its yard". A kind the building cannot own is refused too ("a woodcutter's hut has no
  garden"). Painting over ground of the same kind claims it if it is nobody's; another building's stays its
  owner's ("that lot is another's").
- **Painted with nothing selected** (or a building that owns no ground): the plot goes to a building that
  can own it and touches it - the lowest id where several do; else to the owner of yard ground it touches;
  else to nobody. So a lot painted beside a hut is its lot, and one painted on from that lot is still its.
  A drag between two houses splits where it touches each - select one to give it all to that one.
- **A building painted beside ground nobody owns takes it** (if its kind can own that ground). So the old way
  still works: paint a lot, put the hut beside it, and the lot is the hut's.
- **When the owner goes** (its last plot erased), its yard is nobody's - to be taken by the next building
  beside it. A building split by an erase keeps its yard with the piece that keeps its id; the other piece
  starts with none. A known limit; erasing the middle of a house is rare.

### The woodcutter

- **His woodpile** is a lot of his own yard beside his hut, built: the lowest-numbered such plot. One owner a
  plot means two huts can never pile on one spot, which the "no other hut beside it" rule did before.
- **No lot of his own**: he fells nothing (as now when his lot was erased); the card says "No lot of its own
  beside the hut: nowhere to pile wood" with its "Add lot" chip; NEEDS ATTENTION lists it.

### What the player sees

- **The card**: a house's or a hut's yard on a line ("Yard: 2 garden plots, 1 lot plot"), and the chips - a
  house "Add lot" and "Add garden", a hut "Add lot" - which take up the tool (`OVERLAY_HIT_TAKE_TOOL`).
- **Outlines**: a selected house or hut outlines its whole yard in the related colour (`FindRelatedPlaces`),
  so while a yard is painted the building and what it already owns stand out.
- **The ghost** goes red where the yard rule refuses, as it does for every other rule.

### State and commands

- **`ZoneState::ground_owner`**: per fine vertex, the building that owns its garden or lot, 0 nobody. A
  zones change like any other: in the state hash, saved as a ground entry's fourth number (`[plot, kind,
  line, owner]`, only where owned) and set back as saved after a load. A save without owners (none exist -
  old saves were let go) loads with every yard nobody's.
- **The owner travels in the paint command**: `CHASM_CMD_ZONE`'s `value[3]` carries the play flag in bit 0,
  as before, and the owner above it (`owner * 2 + play`, exact to 2^23 ids). Every recording so far has 0
  or 1 there - no owner, automatic - so nothing recorded changes meaning.
- **Tools**: `chasm_paint` takes `owner` (a building id) for `ground_paint`, and says `ground_owner_here`; a
  building's json gives its yard.

## Build order

1. Zones: `ground_owner`, the rules in `ZONE_OP_GROUND_PAINT` (explicit and automatic), a building taking
   ground nobody owns, the yard let go with its owner; saves, hash, the command's owner. The woodcutter's
   placing rule dropped.
2. Economy: the woodpile from his own yard.
3. The view: the card's yard line and chips, the selected owner sent with a paint, the ghost, the outlines;
   `chasm_paint owner`.
4. Checked in the game: a hut placed bare and listed; "Add lot" from its card, painted, piling; a house's
   garden by selection and by touch; a lot refused away from its owner; erase and load.

## Later

- A house's lots extend its storage (the user's idea, 2026-10-09) - as firewood: "Noted", below.
- What a garden does (food for its family, or comfort) - nothing yet.
- Other kinds with a yard: a store's lot as outdoor storage, a builders' hut's timber yard.

## As built (2026-10-09)

All four steps. Files: `Zones.*` (`ground_owner`, `ZoneOwnsGround`, `ZoneKindHasYard`, `ZoneGroundOwner`,
`ZoneCanYard`, `ZoneAutoOwner`, `ZoneYardPlots`; `Apply`'s `owner`; a building plot's `claim_yard`; the yard let go
with its last plot; `ZoneCommandValue3` / `ZoneCommandPlay` / `ZoneCommandOwner`; `ZoneLotBeside` and `ZoneLotTaken`
gone with the woodcutter's placing rule), `ApplicationChasm.*` (`PaintOwner`, `SubmitZone`'s owner, the handler, the
hash - owned plots only, so zones without a yard hash as before - the save, `chasm_paint owner` and
`ground_owner_here`, a building's json `yard`), `ApplicationChasmGhost.cpp` (`GhostKey::owner`, red where
`ZoneCanYard` refuses), `ApplicationChasmSelect.cpp` (`YardLines`: the yard line and the Add chips on a house's and a
hut's card; the yard in `FindRelatedPlaces`), `Economy.cpp` (the woodpile from his own yard; `lot_used` gone),
`ChasmSave.cpp` (a ground entry's fourth number).

- **Checked** (debug, a copied exe on 8774, paused and stepped; seed as the default):
  - a hut painted on bare grass: placed, and NO LOT TO PILE ON listed;
  - a lot painted for it (`owner`): its yard, and the row gone; one painted away from it refused "a lot goes beside
    its building, or its yard"; a garden for it refused "a woodcutter's hut has no garden";
  - a house, then a garden beside it with nobody named: the house's; a garden painted on from that one, touching
    only the garden: the house's;
  - a lot painted alone: nobody's; a hut put beside it: the lot became the hut's;
  - saved and loaded: every owner back; the house erased: its garden nobody's;
  - in play, by clicks (`chasm_select press`): NO LOT's row selected a new hut by the camp and took the view to it;
    its card's "Add lot" took the LOT tool up with the hut still selected; a click beside the hut painted a lot site
    into its yard ("Yard: 1 lot plot", BUILDING 1 - WOOD 0/3). 1000 ticks on the idle had built it, the row had gone
    and the woodcutter was going for wood - the lot fenced beside the hut, outlined with it.
- **Under the clouds** a player's paint is refused as ever, yard or not: a hut placed by a debug paint in unexplored
  ground cannot be given a lot from play until the ground is explored.
- **Not checked by a run**: a house's yard by "Add garden" through the clicks (the same path as "Add lot"), and the
  ghost's red, seen only in code.

### Fix: shut in a yard (2026-10-10)

The user's recording `chasm_dirk_dewit.rec`: Dirk de Wit stood in woodcutter 4's one-plot lot, "at home" but nowhere
near it, a quarter of his water left - no route from there to anywhere. A lot is meant to be empty (the woodpile is
reached from the hut beside it), and its fence is crossed only at a gate, where a road ends at it. This one had roads
running past it on three sides and ending at none, so no gate; and a hut is walked through only at a walk's ends. How
he got in is not in the recording, which starts with him there; most likely he was crossing the lot while it was
still a site, which is open ground, when its fence went up, and the re-plan stopped him where he stood. A road run on
past a gate would shut someone in the same way.

The fix is in the walking rule (`Walkers::EdgeOpen`): **someone whose walk starts inside a standing yard may go out
over its fence, never in.** Entering is still closed, so the walk can only reach that fence from inside the yard it
started in. Checked by replaying the recording: within 25 ticks he was over the fence onto the road, drank at the
collector and walked home to his tent.

## Extend: a house's own plots, the same way (2026-10-10)

The user's: a house was extended by starting a drag on it, and that drag's press - a click, as far as the
house tool knew - also put a storey on the plot it started from. "That mechanic can simply be the same as placing
the lots, with an extend option that selects the house tool automatically and extends a house when one is
selected."

### As built

- **The card's Extend chip** (`YardLines`), first in the row with Add lot / Add garden: takes up the HOUSE tool
  (`OVERLAY_HIT_TAKE_TOOL`) with the house still selected. Left out once the house has `ZONE_HOUSE_MAX_PLOTS`.
- **A building tool with a building of its kind selected extends that one** (`PaintOwner`), whether the tool came
  from the chip or the grid: the selected id travels as the command's owner, as a yard's does. In
  `Zones::Apply` an owner on `build_paint`/`build_add` means that building or nothing: every plot must join it -
  beside it, or across a cell with the corner plot between brought in (`ZoneJoinCorner`, the old stroke code
  pulled out) - and a house must have room (`ZoneCanExtend`). Refused, with a reason, never a new building: "an
  extension goes beside its building", "a house is at most four plots". With another kind selected, or nothing,
  the tool makes its own building as before. Not for a winch or a bridge, which are placed whole.
- **A click's storey waits for the release** (`paint_press_plot`), for every building tool, selected or not: let
  go on the plot it was pressed on, it is a click and adds a storey; moved off it first, it was a drag, which
  starts on that building with no storey. So the old way - a drag started on the house, nothing selected - still
  extends it, now without the floor. Shift (a storey off) is unchanged.
- **The ghost** goes red where `ZoneCanExtend` refuses. **`chasm_paint`** takes `owner` for `build_paint` and
  `build_add` too.
- **Replays**: every recording so far has no owner on a building command, so `Apply` does what it did; the press
  change is the view's, which a replay does not run (it replays the commands).

### Checked (debug, a copied exe on 8774, paused and stepped, the default seed, scripted drags by `chasm_tool`)

- A one-plot house selected: its card read "Extend, Add lot, Add garden"; a press on Extend took up the house tool
  with the house still selected.
- A drag from the house onto a plot across a cell: the house took that plot and the corner between, "2 storeys to
  build, 1 standing" - no storey on the plot the drag started from.
- A click on the house, the same tool and selection: "3 storeys to build" - one storey more.
- A plot away from it: red ghost, "can't build: an extension goes beside its building".
- Nothing selected, a drag from a fresh one-plot house: extended to three plots, no storey on the first.
- Filled to four plots by `chasm_paint owner`: the card's row lost Extend; a fifth plot refused. After `ZoneCanExtend`
  was made to ask "full" first (rebuilt 2026-10-10, with defence step 6 in the tree): a plot away from a full house is
  refused "a house is at most four plots", one away from a one-plot house still "an extension goes beside its building".

## Noted: a yard's room, and a lot site (the user, 2026-10-10)

Three of the user's, noted to be built. None is worked out yet; what is open is listed under each.

### Decided (the user, 2026-10-10)

From a screenshot of a hut with a three-plot lot ("Yard: 3 lot plots", "Woodpile 8 of 24"): every lot painted
on already joins the hut's yard, and the fenced lot looks as if the hut opens onto all of it.

- **A yard's plots touch the building itself**, never only ground of its yard. A yard no longer grows out
  from its own ground a plot at a time: it is at most the plots round its building, and the building is the
  door to every one of them. In code: `ZoneCanYard` asks for the building alone (its `TouchesYard` takes the
  yard too today), `ZoneAutoOwner` loses its second pass (the owner of yard ground it touches), a building
  put down takes only the ground nobody owns beside it (`claim_yard` floods on through all of it joined to
  that today), and the refusals lose their "or its yard". One rule for lots and gardens.
- **Gardens get the same as lots while a site**: selectable, and staked out.
- **Saves are let go**, not converted: they are not meant to work across a change like this (the user).
- Open: the fence. `ZoneBoundaryBetween` puts none between two standing garden or lot plots, whoever owns
  them, so a lot that is nobody's, or another building's yard, beside a yard is drawn inside the same fence.

### A woodpile that grows with the yard

- **Today** the woodpile is ONE plot of the hut's yard, its lowest-numbered lot plot beside the hut
  (`pile_plot`), and holds `ECONOMY_HUT_PILE` whatever else the yard holds: a second lot, or a bigger one,
  adds nothing.
- **The user's: a woodcutter with more than one lot, or larger lots, stacks more wood.**
- Reached as the pile is now: wood goes on and off it from the hut plot beside it (`pile_door`), and
  nobody walks into a yard. With every lot plot beside the hut (Decided, above), the hut is the door to
  each of them.
- Open: room by the yard's lot area, as a store's goes by floor area (`ECONOMY_STORE_ROOM`), or by its plots.

### A house's lot keeps firewood

- **Today** a house keeps firewood in its woodshed, `ECONOMY_WOODSHED_DAYS` of its burning, apart from its
  food and water. A house can own lots (step 1), but they do nothing.
- **The user's: a house with a lot stores wood there, extending its store for the winter.** This is what
  "a house's lots extend its storage" (Later, 2026-10-09) has become: wood, for the winter.
- The house is its door, as the hut is the woodpile's: its lot plots all touch it (Decided, above).
- Open: how much a lot holds (by area, as above); and whether the family fetches for it as for the
  woodshed, below `ECONOMY_HOME_DAYS` and by fair shares (`FirewoodFair`), so a lot fills in autumn.

### A lot or garden site selected like a field site

- **Today** a garden or lot painted in play is a site a plot at a time (`ground_built` 0, a worker's
  `ECONOMY_SITE_GROUND | plot`), and a click on one selects nothing (selection_plan.md: "a lot plot itself
  cannot be selected"). It is drawn as a finished lot with only its fence missing - the ground mesh does
  not look at `ground_built` - so a lot waiting for its wood looks like a lot.
- **The user's: a lot is selectable while it is being built, like a field, and looks like a site.** Its
  card as a field site's: the wood brought and still wanted, and who is carrying it. Its look: staked out
  as a field site is (`CropMesh.h`: a stake at each corner, a cord along its edges) until it stands.
- **Gardens the same** (Decided, above): the same kind of site, selected and staked out alike.
- Open: a ground site has no id, so what a click selects - the one plot, or every plot of that lot still
  to build; and whether Build first comes with it.
