---
title: Zeppelin
aliases: zeppelins, airship, envelope, gondola, scaffold
kind: buildings
status: partial
summary: Built on a dock in five stages - frame, hull, rigging, envelope, and steam last to fill it. Finished, it is a craft of its own, moored there, and flies where it is sent.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > The zeppelin dock, and building the zeppelin; steam_plan.md > The zeppelin, modelled (2026-10-23); steam_plan.md > As built: step 5 (2026-10-23)
code: Economy.h: ZEPPELIN_STAGES ECONOMY_ZEPPELIN_WOOD ECONOMY_ZEPPELIN_FLOATSTONE ECONOMY_ZEPPELIN_ROPE ECONOMY_ZEPPELIN_CANVAS ECONOMY_ZEPPELIN_STEAM EconomyZeppelinGood EVENT_ZEPPELIN_STAGE; Economy.cpp: ZeppelinSite SiteNeed Build; ApplicationChasmDock.cpp: UploadDocks
verified: 2026-10-25
---

The colony's way over the chasm - it floats only over it, held up by its heat and its steam. It is built on a
[[Zeppelin dock|dock]]: once the dock stands and a zeppelin fits on it, the dock is a [[Construction|site]] again,
for its zeppelin.

## Its stages

| Stage | Takes | What it adds |
|---|---|---|
| 1 frame | {ECONOMY_ZEPPELIN_WOOD} [[Wood|wood]] | the gondola's keel and ribs, the envelope's hoops and stringers |
| 2 hull | {ECONOMY_ZEPPELIN_FLOATSTONE} [[Floatstone|floatstone]] | the gondola planked, its bottom in crystal; a cabin on deck |
| 3 rigging | {ECONOMY_ZEPPELIN_ROPE} [[Rope|rope]] | the gondola hung from the frame, the steam pipe, the propellers |
| 4 envelope | {ECONOMY_ZEPPELIN_CANVAS} [[Canvas|canvas]] | the canvas on - slack, fallen in between the hoops |
| 5 filling | {ECONOMY_ZEPPELIN_STEAM} [[Steam|steam]] | filled taut: finished |

- **One stage at a time**: the site asks for the next stage's good alone, and the builders carry it as to any site,
  from stores, the camp, crates and the workshops' piles - a ropewalk's rope, a sail loft's canvas. Nothing is
  brought for a later stage.
- **A stage stands once all of its good is there**, so the zeppelin rises stage by stage, the steam goes in last and
  it is never left half-filled for want of canvas.
- Each stage is told on the notice line and kept in the [[Statistics|journal]]; the last as its finishing.
- **Finished, it is a craft of its own**, the colony's rather than the dock's, moored on the dock it was built on,
  and it [[Flight|flies]] where it is sent. The dock builds no other for now.

## Its card

The dock's card shows how many stages stand and what the next takes, against what has been brought; who is carrying;
the stages after it. When none of the next good is to be had in the colony, what makes it: a mine on a seam, a
ropewalk, a sail loft, a steam collector.

## How it looks

The **scaffold** - trestles under the gondola, frames under the envelope, a ladder - stands from the zeppelin's
beginning until it is finished (the user's, 2026-10-23); then it comes down, and the filled zeppelin floats on its
mooring lines, its nose in the mast's cup - until it is sent off. Flying, its propellers turn
([[Flight]], "How it looks").

## Planned

- [planned] The dock beginning another once its own is away ([[Flight]]; `flight_plan.md`). People already go aboard
  and off by order ([[Passengers]]), and goods by its load list at a [[Mooring mast|mast]] or dock.
