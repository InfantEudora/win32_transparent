# Fields: built, sown, rotated

The user's, from play testing, 2026-10-08. Fields today are painted and grow at once, wheat unless changed,
the same yield every time. They become something to plan: a field is BUILT, SOWN by its farmer with a crop
you choose, and what you grow next decides how well it does. The proposals below agreed as they stand ("fits
the original plan perfectly"). BUILT 2026-10-08 - see the end. Who farms a field changed the same day: a
field belongs to a house's land, farmed by its family (`land_plan.md`).

## Decided (the user, 2026-10-08)

- **A field shows its yield** - 100% for its first crop - and lets you **pick the next crop**, each choice
  with the yield it would get. The next crop **defaults to the one with the highest yield**.
- **What decides the yield: a follows-what table** on the crop just harvested (gameplay_plan.md section 8,
  "Crop rotation on the coarse cell" - the idea it sketched). Repeating a crop wears it down; beans feed the
  soil for whatever follows:

  | Yield of the next crop | wheat | greens | beans |
  |---|---|---|---|
  | first crop | 100% | 100% | 100% |
  | after wheat | 60% | 100% | 100% |
  | after greens | 100% | 70% | 100% |
  | after beans | 120% | 120% | 60% |

  A harvest is the field's area x its crop's food per area (`crop_yield`) x this.
- **A field placed in play is a construction site first**: 2 wood, carried and built like any site, and then
  it is EMPTY.
- **Its first crop is chosen on its card**; until then nothing happens. **The farmer's first job is to sow
  it.**
- **Sowing costs time, no seed**: the farmer walks out and sows the whole field, at his pace (husbandry).

## Proposed, and agreed (the user, 2026-10-08)

- **2 wood per cell** - a one-cell field 2, a field of four 8 - like a garden's wall by its area.
- **Two choices per field, both player commands (recorded zone ops):** the STARTING crop (`ZoneBuilding::crop`,
  none until chosen), used for its first sowing only; and the NEXT crop (`next_crop`) for every sowing after,
  AUTO by default - the highest yield after what was just harvested, a tie to the earlier in the table
  (wheat, greens, beans). A next crop picked STAYS picked until set back to auto; the card shows auto with what
  it would choose. So left alone a field rotates by itself: wheat, greens, wheat, greens... and beans when you
  ask for them.
- **The farmer at home**: sowing first - a field built, empty, its crop decided - then fetching a harvest that
  waits. Sowing takes `ECONOMY_SOW_SECONDS` a cell at his pace, standing at the field's middle vertex (where
  he picks up). Growth then runs as now: while he works it, not in winter, not under the snow.
- **At harvest** the field is EMPTY again with its crop remembered as the last one; its yield for what comes
  next is set when he sows. The growing crop and its yield are the economy's (`EconomyField`); the choices are
  the zones'.
- **A debug paint** stands at once, but is empty too: choose its crop (`chasm_paint field_crop`) and its
  farmer sows it. Saves from before: their fields stand, sown with their crop at 100%.
- **Looks** (`CropMesh`): a site is staked out - stakes at the cell's corners on untilled ground; built and
  empty is the tilled ground that is there already (`FIELD_STAGE_BARE`); sown, the growth stages as now.
- **The card**: a site, as any site (wood, carriers, Build first). Empty and never sown: "Choose a crop to sow"
  with a chip per crop. Sown: the crop, how far grown, and its yield; then "Next:" - AUTO (naming its pick)
  and each crop with its yield. Waiting to be sown: "Waiting for its farmer to sow wheat".

## Build order

1. Zones: a field's cells built or not (`field_built`, like a garden's `ground_built`), a play paint a site and
   a debug one standing; `next_crop` and its op; the starting crop able to be none. Saves, hash.
2. Economy: a field site's wood (2 a cell), built through a raise op on the tick like any building's; the
   field's state - sown, growing crop, yield, last crop; the yield table; sowing as the farmer's errand; the
   harvest by yield.
3. The look: the staked-out site.
4. The card and the tools: yield, the next crop's chips with their yields, the starting crop's chips;
   `chasm_paint field_next`, `chasm_economy` reporting a field's sown, crop, yield, last and next.

## As built (2026-10-08)

All four steps. Files: `Zones.*` (`ZONE_CROP_NONE` / `ZONE_CROP_AUTO`, `ZoneBuilding::next_crop`, `ZoneState::field_built`,
`ZONE_OP_FIELD_NEXT` 15 and `ZONE_OP_FIELD_RAISE` 16, `ZoneBuildingInfo::site_cells`; a field's floor area is its built
cells'), `ChasmSave.cpp`, `Economy.*` (`EconomyField` sown / yield / last, `crop_after`, `EconomyCropYield`,
`EconomyAutoCrop`, `EconomyFieldSowCrop`, `GoSow`, `WORKER_TO_SOW` 19 and `WORKER_SOWING` 20, field sites in
`SiteNeed` / `Build` / `FindSiteWork`, `FieldRaises`), `CropMesh.*` (`FieldLook`, the staked-out site),
`BoundaryMesh.cpp` (no fence round a site cell), the card in `ApplicationChasmSelect.cpp` (`CARD_CHOICE`,
`OVERLAY_HIT_FIELD_CROP` / `_NEXT`), the panel and `chasm_paint` / `chasm_economy` in `ApplicationChasm.cpp` and
`ApplicationChasmEconomy.cpp`, the build bar's BUILDING line in `ApplicationChasmTime.cpp`.

- **A field's place to walk to** is its middle vertex: `Economy::NearestPlot` falls back to `home_plot` for a
  building with no plots, so its farmer, its builders and the hungry all reach it there.
- **A store next to a field takes every food** now, its crop changing as it is rotated.
- **Saves**: a field's `"next"` (when not auto) and `"cells_built"` (only while some are a site); an economy
  field is `[building, crop, grown, sown, yield, last]` - an older three-number one loads sown at 100%.
  Hashed: `field_built` with the zones, `next_crop` in the building word, the field record with the economy.
- **Measured** (seed 12, debug, a copied exe): a one-cell field painted in play - "Field - being built, Wood 0
  of 2" - built by the idle 6 s later (speed 3); empty, "choose a crop to sow", no chip lit; greens chosen and
  sown in 4 s at 100%, its next row "Auto: Wheat | Wheat 100% | Greens 70% | Beans 100%"; harvested 85 s later
  (3 greens), then AUTO sowed wheat at 100%, the next row now "Auto: Greens | Wheat 60% | ...". A site is drawn
  as four corner stakes and a cord on the grass; built and empty, tilled earth inside its fence.

**Not run**: the replay test (seed 12 gets new ones - the user's).
