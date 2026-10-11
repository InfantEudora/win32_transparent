# Stopping work at a workplace

The user's, 2026-10-12, playtesting a small settlement (`saves/camp_3.json`: a woodcutter, a water carrier and one
free person). The water carrier was a full-time job for somebody with nothing to carry: there was no store, so she
sat at home while her collector filled. The user asked for a way to block a production building's job, so that
person is free for other work.

## Decided (the user, 2026-10-12)

- **A workplace can be stopped from its card**, and set working again. While stopped it takes nobody, and its
  people go free for other work: building, carrying, laying roads.
- **Which**: the water collector, the woodcutter's hut, the ropewalk, the builders' hut and the archery range
  (`ZoneKindStoppable`). Not a field (a family's land), a guard tower (it has its guard count) or a camp.
- **A collector still fills only while it is worked.** It does not fill by itself. A collector is stopped
  when water is plentiful in the stores, and its water stops with it.
- **Nobody draws water by hand instead**: the freed carrier must not take up the camp's water carrying.

## Worked out here, for the user to overrule

- **A collector takes over the camp's water carrier**, as a woodcutter's hut already takes over the camp's
  woodcutter: once one stands in the settlement, worked or stopped, the camp has no water carrier of its own.
  That is how a stopped collector keeps anyone from drawing water by hand. A stopped hut likewise keeps the
  camp's woodcutter away.
- **On or off, not a count.** The builders' hut and the range have several places; stopping takes them all.
  A count like the guard tower's could come later.
- **A stopped hut fells nothing**, so it no longer counts as a woodcutter near trees in a road's or a site's
  way: NEEDS ATTENTION's "TREES IN THE WAY: NO WOODCUTTER NEAR" can show for them.
- **What it holds stays anyone's**: a stopped collector's water and a stopped hut's wood are still fetched
  from by houses, as before.

## As built (2026-10-12)

- `ZoneBuilding::f_stopped`, set by the zone commands `ZONE_OP_WORK_STOP` (a plot) and `ZONE_OP_RANGE_STOP`
  (a range's cell), so it is recorded, replayed, saved (`"stopped": true`, written only when set) and in the
  state hash. Kept by the pieces of a split building.
- `Derive`: a stopped workplace has no `places`, so `AssignJobs` frees its worker the usual way. The camp's
  `drawer_places` (and its `draw_plot`) go once a collector stands in its settlement, as `cutter_places` do for a hut.
- The card: a "Stop work" toggle on a workplace that stands, and "Stopped: no water is collected until it is set
  working again" (the collector; the others say nobody works or trains there) in place of the amber "Nobody works
  here". `chasm_paint` has `work_stop` and `range_stop` (`on`, default true); `chasm_economy`'s building shows
  `stopped` and `places`.
- *Checked* on `camp_3`: the collector stopped, Bente went free at once and carried wood to the sites; the
  collector's water went 8 -> 6 as people drank and never refilled; the camp of that settlement had no water
  carrier's place (its `draws_at` gone), the home village's camp, with no collector, still had its own. Set working
  again, it took Marit (ingenuity 10) by the usual score. The hut stopped: Dirk free, and the camp's woodcutter
  place stayed taken over. A house refused ("no workplace there to stop"). Saved and loaded, still stopped.
