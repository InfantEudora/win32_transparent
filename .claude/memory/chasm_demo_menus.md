---
name: chasm-demo-menus
description: "Chasm demo_plan steps 1, 2, 4 and 5 built 2026-10-16 (title, pause menu, save/load, autosave, always recording, bug reports, replay list); steps 3, 6 and 7 are left"
metadata:
  node_type: memory
  type: project
  originSessionId: 7a3c3850-95bd-4a10-bf9a-4f6563ca8e67
  modified: 2026-10-16T12:57:35.746Z
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

Left: step 3 (settings, waiting for sound), step 6 (discoveries), step 7 (goals).

**Why:** the user wants a ship build testers can play and report bugs from without the debug menu.

**How to apply:**
- The four New game maps (`CHASM_STARTS`) stay as placeholders. The user changes them by hand when they spot interesting seeds, so don't propose seed sheets.
- Always recording is on only in builds without the panels. In a debug build it would take over the `input_record` an agent starts, so there it is a checkbox.
- The user often plays `build/chasm_nophysics.exe` while I test. If the link fails with Permission denied, rename the running exe aside rather than stopping it.
- Stop my own test instance by its process id, found from the port with netstat.

Related: [[chasm-game-plan]], [[chasm-statistics]], [[input-recording]].
