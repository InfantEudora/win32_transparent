---
name: cue-plan
description: Archer cue/event layer (sound first, then shake/rumble/music/zones) - apps/archer/cue_plan.md revised 2026-09-27 and AGREED; step 0 (SoundSystem buses/live gain-pitch-pan/offline render) DONE
metadata:
  type: project
---

2026-09-27: user agreed with every revision of apps/archer/cue_plan.md - it is a generic event layer (events -> cues -> actions), sound first only because sound needs it most. Key decisions: cues only PRESENT, game-state effects (flags, locks, cutscenes, hitstop) live in Stage; cue table is a hot-reloaded JSON file; cues fire once at the END of RunSimulationTick on stage.ticks; shake is an offset in PlaceCamera, never camera_target (sun texel snap).

Step 0 DONE the same day: core/SoundSystem gained SoundParams Play overload, SetGain/SetPitch/SetPan, buses (AddBus/SetBusGain, master always exists), 256 buffers/32 voices, LengthOf, ListVoices, InitialiseOffline+Render. Old Play/PlayStream signatures unchanged. Verified by tools/sound_test.cpp offline section (`--offline-only`, silent, 19 checks); device checks not re-run.

Step 1 DONE the same day: core/CueLog.h (the line format is the contract), archer PlayCue/StopCue around every sound, MCP cue_log + archer_sound (volume 0 mutes without changing the log), five baselines in apps/archer/recordings/*.cues, checked by `python tools/cue_replay.py` against archer on --mcp-port 8768 (all identical run to run).

Step 2 DONE the same day: core/CueSystem (table format documented atop CueSystem.h; plays through the CueOutput interface because core is compiled without USE_SOUND - core/CueSoundOutput.h is the header-only SoundSystem adapter), tools/cue_test.cpp 32 checks incl. parity with the old kick/swoosh code over 4000 kicks and 160 flights. Methods are LoadTable/LoadTableText (LoadString is a Windows macro).

**How to apply:** next is step 3 - wire archer to CueSystem with the parity rows recorded in cue_plan.md's step 2 section (seed 0, no_repeat false), then prove it with `python tools/cue_replay.py` (no --write); only after it matches flip those two fields and re-baseline. Recordings pick up the person's key-ups typed in other windows (`up <action> key=...` lines) - strip them from a new recording. Use offline render for any audio check rather than listening. See [[adaptive-music-plan]] for the music side a cue will drive.
