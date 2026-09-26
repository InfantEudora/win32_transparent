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

**How to apply:** tune samplescan's GuessCategory thresholds on the real library once it lands, not on synthetic tones; ask the user to listen - Claude cannot judge whether it sounds right.
