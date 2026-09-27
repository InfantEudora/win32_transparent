---
name: adaptive-music-plan
description: "Adaptive ambient music direction (key/tempo/suspense-driven, not a score); sample library at apps/music/samples catalogued by tools/samplescan; exploratory, user wants to zone in iteratively"
metadata:
  node_type: memory
  type: project
  originSessionId: f48f1106-0612-4b9f-b48d-224679e62ba1
  modified: 2026-09-26T19:18:25.293Z
---

2026-09-26: user wants dynamic background music for the games (archer's jungle vibe first) - ambient layers plus generated notes driven by parameters (key, instruments, tempo, suspense), with key changes on game events that can go EITHER way (brighter or darker, the game asks). Deliberately vague: "as we zone in we'll figure out what works" - propose small steps, don't over-design.

Direction discussed (not yet agreed in detail): hybrid - recorded textures (hum, whispers, wind, leaves) as gain-driven layers (wind can follow archer's Wind system), tonal parts as a sampler + scale sequencer; one music voice with internal polyphony (NUM_SOUND_VOICES is 16); sequencer on the audio thread, bar-quantised changes; own RNG, never the shared rrand ([[rrand-shared-stream-todo]]).

Step done: apps/music/ (not an app yet, just readme + samples/unsorted/) and tools/samplescan (own miniaudio build WITH decoders, wav/flac/mp3; YIN pitch, key profile, envelope; writes samples/catalog.csv, keeps the 4 human columns on re-scan). User has pre-mixed ambience examples and said they can find the separate stems.

Tuning round 2026-09-26 against the user's ears: 5 files confirmed by ear and recorded in catalog.csv comment/root columns ("by ear: ..."); `stinger` is a hand-assigned category (user agreed). Findings: tremolo/reverb swells rise 6-16 dB vs real tongued notes 31-40, so same-pitch splits need >=18 dB; attack SPEED does not separate real material (tried, removed). Library leans A minor / E minor.

Bench BUILT 2026-09-26: apps/music (exe music_nophysics.exe), MusicEngine (pure DSP: beds w/ loop+key crossfade, pitched beds follow key, voices = random walk on scale, tension notes with suspense, bar-quantised key changes) + MusicPlayer (ma_data_source on SoundSystem::PlayStream, try_lock hand-off) + JSON score assets/music/jungle.json. Samples exported by `make samples` (wavs gitignored). Verify with music_render + samplescan --file (key line). Run on --mcp-port 8767 (engine-wide flag added) and drive via curl; archer/other sessions use 8765.

**How to apply:** tune samplescan's GuessCategory thresholds on the real library once it lands, not on synthetic tones; ask the user to listen - Claude cannot judge whether it sounds right.
