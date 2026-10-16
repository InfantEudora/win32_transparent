---
name: chasm-demo-menus
description: "Chasm demo_plan ALL 7 steps built 2026-10-16 (menus, saves, recording for testers, Settings, discoveries unlocking tools, goals + death pop-up + end page)"
metadata:
  node_type: memory
  type: project
  originSessionId: 7a3c3850-95bd-4a10-bf9a-4f6563ca8e67
  modified: 2026-10-16T20:36:28.181Z
---

apps/chasm/docs/demo_plan.md steps 1, 2, 4 and 5 were BUILT by 2026-10-16:
- the title scene with its diorama;
- the pause menu and the save/load lists (ApplicationChasmMenu.cpp);
- autosaves;
- recording for testers (ApplicationChasmRecord.cpp): always recording to `chasm_auto_*.rec`, Report a bug writing to `reports/`, and replaying from the title.

Core gained:
- `Debugger::OpenLogFile`;
- release crash reports (.txt and .dmp, a .map from the release link, `tools/crash_map.py`);
- `RequestRecordingStart(stem)` and `RequestRecordingRestart`;
- a virtual `DrawInputRecordingBadge`.

Step 3, Settings, was also built: archer's five settings with no Controls screen (the user wants no key mapping or pad support yet), in `build/settings.json`, applied by ApplySettings. The music volume goes on the `music` bus. The effects volume goes on the cue table's layer bus `sounds`, so the table's buses are named `sounds/<name>`.

Step 6, discoveries, was built 2026-10-16 ("As built: discoveries" in demo_plan.md):
- read from the journal, the exploration and standing buildings, never saved as flags (the user's choice);
- the user's unlocks: river → WATER/BRIDGE, chasm → WINCH, boulder → STONECUTTER, floatstone → MINE, builders' hut → WALL/RANGE/TOWER; play mode only;
- new events: boulder_found, balcony_reached, frost_took, grew_up, expecting, all told on the notice line.

Step 7 was built 2026-10-16 ("As built: the goals and the end"):
- the goals are rules in `TickGoals` (ApplicationChasmGoals.cpp), told once as journaled events;
- the goals panel sits at the top left;
- a death pop-up (`CHASM_ASK_DEATH`);
- the end is the statistics page in end mode (`stats_end`).

Also built 2026-10-16:
- the VERSION, 0.0.1, in ChasmVersion.cpp: not semver; the git hash and date come from the makefile's stamp;
- the INTERFACE SIZE: HudUiScale is sqrt(h/810) × the `ui_size` setting, and menus follow it through UIMenuLayout::scale in core.

New HUD code takes `s` from DrawOverlay. Never compute h/810 again.

My call, flagged to the user: the end comes once both goals are DECIDED (met or failed), not only both met.

Pop-ups and the end page appear in PLAY MODE only. Agents driving debug mode are never stopped. In play mode, `chasm_menu` reports and clicks them: question "death", `end_page`, `goals`.

**Why:** the user wants a ship build testers can play and report bugs from without the debug menu.

**How to apply:**
- The four New game maps (`CHASM_STARTS`) stay as placeholders. The user changes them by hand when they spot interesting seeds, so don't propose seed sheets.
- Always recording is on only in builds without the panels. In a debug build it would take over the `input_record` an agent starts, so there it is a checkbox.
- The user often plays `build/chasm_nophysics.exe` while I test. If the link fails with Permission denied, rename the running exe aside rather than stopping it.
- Stop my own test instance by its process id, found from the port with netstat.

Related: [[chasm-game-plan]], [[chasm-statistics]], [[input-recording]].
