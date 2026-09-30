---
name: adaptive-music-plan
description: "Adaptive ambient music direction (key/tempo/suspense-driven, not a score); sample library at apps/music/samples catalogued by tools/samplescan; exploratory, user wants to zone in iteratively"
metadata:
  node_type: memory
  type: project
  originSessionId: f48f1106-0612-4b9f-b48d-224679e62ba1
  modified: 2026-09-30T12:13:12.229Z
---

2026-09-26: user wants dynamic background music for the games (archer's jungle vibe first) - ambient layers plus generated notes driven by parameters (key, instruments, tempo, suspense), with key changes on game events that can go EITHER way (brighter or darker, the game asks). Deliberately vague: "as we zone in we'll figure out what works" - propose small steps, don't over-design.

Direction discussed (not yet agreed in detail): hybrid - recorded textures (hum, whispers, wind, leaves) as gain-driven layers (wind can follow archer's Wind system), tonal parts as a sampler + scale sequencer; one music voice with internal polyphony (NUM_SOUND_VOICES is 16); sequencer on the audio thread, bar-quantised changes; own RNG, never the shared rrand ([[rrand-shared-stream-todo]]).

Step done: apps/music/ (not an app yet, just readme + samples/unsorted/) and tools/samplescan (own miniaudio build WITH decoders, wav/flac/mp3; YIN pitch, key profile, envelope; writes samples/catalog.csv, keeps the 4 human columns on re-scan). User has pre-mixed ambience examples and said they can find the separate stems.

Tuning round 2026-09-26 against the user's ears: 5 files confirmed by ear and recorded in catalog.csv comment/root columns ("by ear: ..."); `stinger` is a hand-assigned category (user agreed). Findings: tremolo/reverb swells rise 6-16 dB vs real tongued notes 31-40, so same-pitch splits need >=18 dB; attack SPEED does not separate real material (tried, removed). Library leans A minor / E minor.

Bench BUILT 2026-09-26: apps/music (exe music_nophysics.exe), MusicEngine (pure DSP: beds w/ loop+key crossfade, pitched beds follow key, voices = random walk on scale, tension notes with suspense, bar-quantised key changes) + MusicPlayer (ma_data_source on SoundSystem::PlayStream, try_lock hand-off) + JSON score assets/music/jungle.json. Samples exported by `make samples` (wavs gitignored). Verify with music_render + samplescan --file (key line). Run on --mcp-port 8767 (engine-wide flag added) and drive via curl; archer/other sessions use 8765.

2026-09-27: Library panel + library_* MCP tools (MusicLibrary runs samplescan.exe as a subprocess - the app must NOT link decoders) and a Brightness slider (part heights C2..C6, energy-compensated re-weighting, register half-window, cut-only 500 Hz tilt; centroid 178->699 Hz, level within 2 dB). Minimised runs save apps/music/imgui.ini at a 32x32 display, squashing the dock layout - engine-level, unfixed.

2026-09-27 later: Scan new / samplescan --new (measures only uncatalogued files). Score SECTIONS: top-level parts play always, per-section parts take turns, bar-quantised, beds equal-power crossfade over section_fade_s, auto after `bars`; jungle.json = canopy (original set) + undergrowth (hang drums, bass, zanka, low wind, drums, whistles), both A minor within 1 dB. music_section tool. Balance by the catalog's active_rms_db: gain ~ 10^((target - active_rms)/20).

2026-09-29/30: Library Export (samples/exports.csv is the one list; `make samples` reads it); sections `"rotation": false` (menu, cave - asked for by name only); engine PAUSE (fade out, then HOLD: clock frozen, resume is sample-exact - tested offline by pause_test render pair). USER DECISIONS: apps/music is the DESIGNER, games keep their OWN git copy (`make publish GAME=archer` copies score + only named wavs; both use music/sounds/ names); sim_pause and Escape-to-title behave the same (fade out, resume where left).

2026-09-30 MOVED TO CORE: core/MusicScore|MusicEngine|MusicPlayer (USE_SOUND only; samples stored PCM16, renders byte-identical to float). Archer plays it: title_music (menu) + world_music (held until first continue, horn once) on a `music` bus NOT under any level bus (level buses are hard-held by UpdateView); UpdateMusic pauses via engine fade; `music` cue action (section/stinger/key, world cues only); `cave` scope -> music_cave rows; suspense from vitals fear. archer_sound shows `music`. Later 2026-09-30: SoundSystem::SetBusPaused(bus, paused, fade_s) + UpdateFades (bus fader, then hold) - level sounds fade 1.5 s / back 0.5 s like the music; hard holds again once sim_step is used in a pause; sound_test.cpp offline checks. Archer "Music" ImGui tab beside Cues (sections, suspense Hold, brightness, key, stingers, volume, graphs); world starts in undergrowth. NEXT the user wants: asset size via OGG (engine miniaudio has MA_NO_DECODING in 3rdparty/miniaudio_config.h - would need vorbis enabled); brightness from zones.

**How to apply:** tune samplescan's GuessCategory thresholds on the real library once it lands, not on synthetic tones; ask the user to listen - Claude cannot judge whether it sounds right.
