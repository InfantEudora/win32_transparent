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

- A house's lots extend its storage (the user's idea, 2026-10-09).
- What a garden does (food for its family, or comfort) - nothing yet.
- Other kinds with a yard: a store's lot as outdoor storage, a builders' hut's timber yard.
