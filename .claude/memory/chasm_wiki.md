---
name: chasm-wiki
description: "apps/chasm/wiki/*.md -> build/wiki.html via apps/chasm/tools/chasm_wiki.py; a short present-tense page per game thing, figures as constant names; update when a mechanic changes"
metadata:
  node_type: memory
  type: project
  originSessionId: 101173dd-eaad-425e-b5a4-41599acb4b2e
  modified: 2026-10-09T15:11:50.618Z
---

Started 2026-10-09 at the user's request: a clickable wiki of what chasm does TODAY and what is planned,
kept apart from the plans (docs/*.md hold reasons + history, including reversed decisions). By the end of
2026-10-09: 23 pages, none wanted - everything built was covered (people, lifelines, needs, homes, goods,
fields/food, construction, roads and walls, bridge, posts, skills, seasons; crate.md by the crates window).
Other agents DO maintain it (the crates window updated person/store/crate unprompted). Open then:
person.md still said "[planned] births" - births are built; its Planned bullets should go (they are on
lifelines.md / skills.md now). The user liked the `idea` status.

Design the user approved: markdown source, one generated HTML (build/, not committed); a page per THING
not per plan; at most 45 non-blank lines; figures NEVER typed - `{CONSTANT}` / `{CONSTANT:days}` filled
from the #defines at build, `{CONSTANT:#days}` for a count with its noun (1 day / 2 days), and constant
TABLES too, `{crop_days[ZONE_CROP_BEANS]}` / `{crop_after[A][B]}` (index a number or a #define);
`plans:` headings and `code:` symbols are CHECKED (tool exits 1); statuses
built/partial/planned/proposed/idea/parked/scrapped, and `- [status]` bullets roll up into the front page's
"Where we're going" - put each roadmap bullet on ONE page only. Scrapped things keep their page/bullet with a why.
`--unused` lists ECONOMY_/NEEDS_/LIFE_ constants no page names; what is left there is internal (enum ids etc).
Figures measured off the map (not in code) go on a page's `measured:` line and are used by `{= expr}` tokens
(arithmetic on constants/tables/measured, shown rounded). cells.md: plateau plot avg 3.3 (p10 1.9, p90 4.5),
coarse cell 13.1, seed 12, 2026-10-09 - Economy.h comments ("about 30 a plot-storey", "one-plot house holds 3")
assume a plot of ~4 and were stale then. The checks DO catch live drift: EconomyStoreyWood -> EconomyStoreyCost
was caught within the hour.

**Why:** many agents add and retune mechanics; the user wanted one solid overview without it rotting.
**How to apply:** after changing what a chasm mechanic does, update its page, set `verified:`, run the tool
(CLAUDE.md says so too). Write pages FROM THE CODE, not the plans. Related: [[chasm-game-plan]],
[[tuning-numbers-not-rules]].
