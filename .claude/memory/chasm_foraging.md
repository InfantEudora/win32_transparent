---
name: chasm-foraging
description: "chasm foraging_plan.md - fruit from bushes + 10% of oaks, FRUIT eaten first, camp picker, posted pick/draw by right-click menu; BUILT 2026-10-12; .blend export drops Idle"
metadata:
  node_type: memory
  type: project
  originSessionId: ec35e2fd-7f50-4523-9942-7c0c019f9b3f
  modified: 2026-10-12T20:08:23.316Z
---

Decided by the user 2026-10-12 ("all points are perfect") and BUILT the same day (apps/chasm/docs/foraging_plan.md,
wiki/fruit.md): GOOD_FRUIT (8) eaten first; bushes + oaks with PropInstance::f_fruit bear (ForestBears); fruit/regrow
by prop in EconomyState (saved as `plants`, hashed); frost at dawn in winter/under snow; built over = FORAGE_GONE for
good; WORKER_JOB_PICKER (7) at a camp with a plant in its land, after the water carrier; Economy::Forage +
CHASM_CMD_FORAGE (SIM_CMD_LAST+14) for the posted, right-click menu (DrawForageMenu, SendSelected); fruit meshes
`*_fruit` drawn as extra forest sets per chunk. chasm_economy op forage / op plants; settlements list `plants`.

The user: the start camp need NOT live from fruit; tuning left to them after the count - start camp land (reach 50):
23 bushes + 2 fruit trees = 58 fruit at full, ~5.8/day; an outpost of 3 tents ~2/day (estimate by area).

BLENDER TRAP found then: exporting art_source/chasm/chasm_props.blend (collection exporter OR the remembered File >
Export settings) DROPS the villagers' Idle clip - in the saved file Idle has only a fake user, on no armature; the
committed GLB came from an earlier session state. The collection exporter also unsamples Shoot (45 -> 16 channels).
So the fruit meshes were exported alone and MERGED into the committed GLB (script, clips byte-identical). Before
re-exporting that .blend, check its clips against the GLB. blender_chasm_props.py --add now exports with the
remembered settings and no longer rewrites images[0]'s path (it was a packed Downloads palette).

**Why:** a new outpost had no food until a field ripened. **How to apply:** fruit tuning is the user's call; any new
prop for chasm_props.glb faces the Idle trap until the user fixes the .blend. Related: [[chasm-settlements]],
[[chasm-game-plan]].
