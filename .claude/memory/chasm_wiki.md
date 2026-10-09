---
name: chasm-wiki
description: "apps/chasm/wiki/*.md -> build/wiki.html via apps/chasm/tools/chasm_wiki.py; a short present-tense page per game thing, figures as constant names; update when a mechanic changes"
metadata:
  node_type: memory
  type: project
  originSessionId: 101173dd-eaad-425e-b5a4-41599acb4b2e
  modified: 2026-10-09T13:01:27.269Z
---

Started 2026-10-09 at the user's request: a clickable wiki of what chasm does TODAY and what is planned,
kept apart from the plans (docs/*.md hold reasons + history, including reversed decisions). Pages so far
(2026-10-09): woodcutter, tree, store, person, thirst, then health, house, camp, water collector,
fetching home; 14 wanted (red links) - the rest is written as features are touched, not all at once.
The user liked the `idea` status. Births were being added to Economy.cpp the same day by another window,
so person.md's "[planned] births" line may be stale.

Design the user approved: markdown source, one generated HTML (build/, not committed); a page per THING
not per plan; at most 45 non-blank lines; figures NEVER typed - `{CONSTANT}` / `{CONSTANT:days}` filled
from the #defines at build, `{CONSTANT:#days}` for a count with its noun (1 day / 2 days); `plans:` headings and `code:` symbols are CHECKED (tool exits 1); statuses
built/partial/planned/proposed/idea/parked/scrapped, and `- [status]` bullets roll up into the front page's
"Where we're going". Scrapped things keep their page/bullet with a why.

**Why:** many agents add and retune mechanics; the user wanted one solid overview without it rotting.
**How to apply:** after changing what a chasm mechanic does, update its page, set `verified:`, run the tool
(CLAUDE.md says so too). Write pages FROM THE CODE, not the plans. Related: [[chasm-game-plan]],
[[tuning-numbers-not-rules]].
