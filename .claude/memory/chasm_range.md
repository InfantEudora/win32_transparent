---
name: chasm-range
description: "chasm archery range (docs/range_plan.md) - ZONE_KIND_RANGE on coarse cells like a field, full-time WORKER_JOB_ARCHER trainees, per-person archery; BUILT 2026-10-09"
metadata:
  node_type: memory
  type: project
  originSessionId: 962d00b9-0f7e-485d-b7ba-0758261e2ced
  modified: 2026-10-19T12:49:01.143Z
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

**The bow follows TRAINING (the user's, 2026-10-19)**: `EconomyCarriesBow` (Economy.h) = archery > 0 or job ARCHER/GUARD,
used by Shooter (rules) and the view's kit - a trained farmer carries his bow, and ORDERED to a post is a posted archer.
User wants other arms by trade later (pitchfork, axe) through the same seam. Debug: People panel "Put an archer down"
(precision 10, slider archery, posted on the selected plot) / add_person `post: true` (PERSON_POSTED bit, Order in the
CHASM_CMD_PERSON handler). Open: bow stays in the left palm while carrying a pail etc.

**Shot PACE (the user's, 2026-10-19)**: a turn at a zombie takes ARROW_SHOT_SECONDS 5.0 (no archery) to ARROW_SHOT_MASTER
3.5, +-ARROW_SHOT_JITTER 10% per turn by ShotHash(tick, id, zombie 0) - `EconomyShotTicks`, fixed at turn start in
`EconomyWorker::draw_turn` (saved as draw[2], hashed) so the release tick never moves; view rate from draw_turn, clip
re-phased per turn (clock = tick - draw). Range PRACTICE (ShotClockOf, view-only) deliberately still at clip speed.
People panel: pack water / provisions sliders ride CHASM_CMD_NEEDS value[2]/[3] stored ONE UP (old recordings = leave).

**Bow SOUNDS (2026-10-19)**: archer's bow_tension/arrow_leave/arrow_swoosh copied into chasm; SignalShooting
(ApplicationChasmSound.cpp): scope `bow_drawn` nock->release, `arrow_loose`, per-arrow scope `arrow` with `arrow_miss`
forecast (`in` = ticks / physics_time_factor) and `arrow_hit` once. User rejected archer's arrow_hitting (cartoony) and
made their own HIT sound (arrow_hit.ogg, row added 2026-10-19). Sounds only fire LIVE (unpaused) - test with a
muted copy running at 1x, not sim_step. Test save: saves/archers_test.json (4 posted archers 25-100%, flat seed 5).

**Why:** archers are the first unit; archery is groundwork for the guard tower (defence step 5, staff by
archery then precision) and the zombies.
**How to apply:** a trainee's cell is not stored (store/site/prop mean other things) - find it from his
position. The later "share of time" step should only change how many ticks he trains. See [[chasm-game-plan]].
