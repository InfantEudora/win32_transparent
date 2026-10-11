---
name: chasm-range
description: "chasm archery range (docs/range_plan.md) - ZONE_KIND_RANGE on coarse cells like a field, full-time WORKER_JOB_ARCHER trainees, per-person archery; BUILT 2026-10-09"
metadata:
  node_type: memory
  type: project
  originSessionId: 962d00b9-0f7e-485d-b7ba-0758261e2ced
  modified: 2026-10-11T20:23:43.343Z
---

Built 2026-10-09 with the user's agreement (docs/range_plan.md).

**What it is:**
- ZONE_KIND_RANGE (10), painted on coarse cells by ZONE_OP_RANGE_PAINT. It shares ZoneState::field and the
  field-site machinery.
- ZoneKindOnCells(kind) is the "lives on cells" test. A plain `kind == ZONE_KIND_FIELD` now means crop-specific
  only.
- Cost is 12 wood + 8 wheat a cell (4x the first price, the user: "not something you'd build early on"; a tech tree may come later), via EconomyCellCost.

**The user's three decisions:**
- full-time trainees first (one job a person);
- wheat in the cost once sites took goods;
- training all year, snow included.

**Trainees:**
- One place per built cell, filled by precision (WORKER_JOB_ARCHER 4).
- States WORKER_TO_RANGE 25 / WORKER_TRAINING 26; a 40 s stint, then home.
- EconomyWorker::archery (0..ARCHERY_FULL) grows only while training, capped at precision × 10%. An average
  trainee caps in about 9 days (measured).

**The look:**
- Every range shoots NORTH (ZoneRangeCellAt), so the targets face the camera.
- Props `range_target` / `hay_bale` / `arrow` are in chasm_props.glb, added with blender_chasm_props.py --add.
  ApplicationChasmRange.cpp draws them as instance sets, with stand-ins if the GLB lacks a prop.

**Shooting, built 2026-10-11** (range_plan.md "Shooting", user agreed the 6 steps): trainees loop the Shoot clip
by their stint timer (ShotClockOf: whole shots a stint, 8 at 5.0 s); bow (per-frame from the palms, upright when
carried) + quiver (Spine child) for ARCHER and GUARD jobs; arrow in hand from SHOOT_TAKE to SHOOT_RELEASE; flights
+ landings in ApplicationChasmRange.cpp (ShotOf: normal scatter by archery, misses into the ground behind).
All view-only: nothing saved/hashed. User's answers: archer always carries the bow; stand-ins first; low skill
must be able to MISS. Not yet seen in a still: an arrow mid-flight, an archer walking with his bow.

**Why:** archers are the first unit; archery is groundwork for the guard tower (defence step 5, staff by
archery then precision) and the zombies.
**How to apply:** a trainee's cell is not stored (store/site/prop mean other things) - find it from his
position. The later "share of time" step should only change how many ticks he trains. See [[chasm-game-plan]].
