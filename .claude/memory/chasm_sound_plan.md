---
name: chasm-sound-plan
description: "Chasm sound: apps/chasm/docs/sound_plan.md; step 2 BUILT 2026-10-16 (USE_SOUND, assets/cues/chasm.json, per-pass cues on the physics thread, ground listener in core, chasm_sound) + work blows on per-person clocks + people's voices (leo/leah taunts on pick and taken orders, busy replace)"
metadata:
  node_type: memory
  type: project
  originSessionId: e532a19b-410c-4f18-b4c7-cc0b53ada9b0
  modified: 2026-10-16T22:48:20.474Z
---

Plan: apps/chasm/docs/sound_plan.md - its "As built" section is the current state. Core idea (accepted): game
events are made ONCE by the rules as one record (EconomyEvent ring, built with [[chasm-statistics]]); every
reader - notice line, journal, sound - reads it. Nature sounds (birds, crickets, winds) belong to the MUSIC score
(apps/music's chasm.json, not the cue table assets/cues/chasm.json) - don't duplicate. EXCEPTION (user, 2026-10-17):
birds you can SEE call through the cue table, alongside the score's songbird beds ([[chasm-birds-plan]]).

2026-10-16 the user added their own first sounds to apps/chasm/assets/sound/ (click, place_building, refused,
notification, chopping_tree_1..3, hammer_hit_1, hammer_chisel_1, sawing_wood_1; voices in progress) and asked
for short sounds "randomised and timed after each other", like archer's heartbeat beats. BUILT that day:
- ApplicationChasmSound.cpp; USE_SOUND := 1; cue table assets/cues/chasm.json (mtime hot reload, buses
  effects > world/activity/ui, `notice` group). Makefile no longer excludes sound/click.wav (excludes match names).
- Cues tick on the PHYSICS thread once per PASS (UpdateSound, top of UpdateView), clock = pass count = real time
  (4x is 4 ticks per pass). Agent's choice instead of the plan's render thread; open question 1 is still the
  user's. Clicks (UpdatePlayPick overlay press, tool keys) signal directly - same thread.
- Core: CueSystem::SetListener(x, z, height, right_x, right_z) - ground listener; a payload without "z" is
  unchanged (archer safe). 5 checks in tools/cue_test.cpp. Its offline-mix check fails unless tools/sound_test
  ran first (needs %TEMP%/sound_test_assets/sine.wav) - pre-existing, not a regression.
- Events signalled by EconomyEventName with x, z, amount, person, building; only the 4 notices have rows.
- placed/erased (zone_placed/zone_erased in the CHASM_CMD_ZONE handler), refused (zone+order+crate counts).
- WORK BLOWS (SignalWork): nearest 4 woodcutters felling + nearest 4 builders BUILDING within 48; chop at a
  per-person tempo; builders in bouts hammer/chisel/saw + rest. Rhythm = SOUND_* defines in code, sound = table.
- Pause/menu/title/stats: `activity` bus fades 0.6 s and holds. MCP chasm_sound (status, play, volume).

Second round, same day (user: "already really good"):
- EVERY EVENT plays the notification for now (user's call): born/died/settled + the 4 notices on `ui`, `notice`
  group, busy queue; tree_felled/rock_broken/storey_raised/building_finished/harvest on `world` with distance
  falloff, `place` group gap 25 (agent's split, so 4x felling doesn't chime nonstop). Place rows need
  `when distance < 48` (a gain-0 play still takes a voice + the group); building_finished priority 1 + busy
  interrupt (same tick as its last storey); a harvest has plot -1 but a pos - SignalEvents reads pos != (0,0).
- `pitch_jitter` added to core's cue table (salt 4, from the trigger; no jitter = exact pitch, archer unchanged);
  blows pass the worker id as instance. A table using it is REFUSED by an older exe - ship table + build together.
- Sounds are .ogg via tools/sound_ogg.py; the wavs live in art_source/chasm/sound/ ([[ogg-vorbis-settings]]).
- Water ambience (river + archer's waterfall loops, `ambience` bus, faint under the clouds) built 2026-10-16 with
  the discoverable areas - see [[chasm-areas-plan]].
- BUS OWNERSHIP (2026-10-16, the Settings-screen agent): the cue table plays under a LAYER bus "sounds" (CHASM_SOUND_LAYER;
  table buses are "sounds/<name>" - FindBus with that prefix). The Settings' ApplySettings (ApplicationChasmMenu.cpp)
  owns the gains of `music` and `sounds` - never set those two elsewhere; the music players' own buses stay mine.

- VOICES (2026-10-16, user's taunts in assets/sound/speech/taunts/, `<voice>_<line>_s<seed>` - the seed is just the
  take, kept): leo = any man, leah = any woman (grown-up figure = anyone orderable; toddlers silent). Picked -> hi/
  whatsup/yes/whereto/morning; sent -> headingover/onit/imonit; gather -> grabsupplies; take_tent/settle -> packingup;
  everything else -> okay (user: "okay for any order not handled yet"). Orders answer in their COMMAND HANDLER only
  when the rules take them (refused = refusal sound only); each order is its own signal so a new line is a table row.
  Say() = one answer per pass (order rank over pick, first asked = a group's nearest), lock-guarded for MCP's thread.
  Core gained `busy: "replace"` (cuts same priority if begun on an EARLIER tick); orders priority 1 so picks never cut them.
  2026-10-17 (user): a GROUP answers in TWO voices at once, man+woman where possible, panned -0.3/+0.3 by payload `side`
  (one alone: side from his place across the view). Core group option `together: 2` lets same-tick, same-priority
  lines play together past busy and a row's gap; the next answer cuts both.
  User's answers: `morning` plays at any hour; takes LEVELLED at conversion (all -18 dBFS), so every voice row is gain 0.8.

**Why:** the user's sounds are single blows, so blows-on-clocks replaced the plan's looping "beds" for chop/build.
**How to apply:** a sound for an event = a row in chasm.json, no code. Verify with chasm_sound on a MUTED test
instance (volume 0) - read [[chasm-test-instance-autosaves]] before loading a save in one.
Next per the plan: event sounds (step 3), beds (step 4), cue panel into core (step 5). See [[cue-plan]],
[[adaptive-music-plan]], [[chasm-game-plan]].
