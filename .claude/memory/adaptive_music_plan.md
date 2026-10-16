---
name: adaptive-music-plan
description: "Adaptive ambient music direction (key/tempo/suspense-driven, not a score); sample library at apps/music/samples catalogued by tools/samplescan; exploratory, user wants to zone in iteratively"
metadata:
  node_type: memory
  type: project
  originSessionId: f48f1106-0612-4b9f-b48d-224679e62ba1
  modified: 2026-10-16T13:07:06.490Z
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

2026-10-14 CHASM MUSIC (user asked what fits; agreed): folk chamber - plucks, flute, fiddle/cello, hand drum, sparse. apps/music/assets/music/chasm.json, root D, STAND-IN samples from the jungle library (user is gathering real ones). Sections spring/summer/autumn/winter (pent major/ionian/dorian/aeolian, rotation, held until asked) + clouds (phrygian, unexplored ground, unease not horror) + chasm (lydian, awe, `reveal` = war horn on first sight). Game plan, NOT built: 3 MusicPlayers on one score (land/clouds/chasm), masters faded by the camera's cloud/chasm share; season = section event. Core added same day: MOTIFS (written phrase in scale degrees, `phrases` shared across sections, MusicEvent::MOTIF, music_motif), section `mode`/`root` taken on its downbeat, lydian+mixolydian appended; bench score picker. Jungle render byte-identical before/after. The user auditions in the bench themselves - no renders needed. Same day, the user's own samples went in (birds, woods, frame drum, strums, choir, pad, spooky chimes/voice; exported as strum_F etc.); core `"unpitched": true` voices (one-shots at own speed, log '-'); chord strums held to the root by a one-note register; chasm section moved to DORIAN because the choir is on the white keys (lydian would need -3 semitones). Sections levelled within 1.8 dB.

2026-10-16 user asked for MORE GUITAR in spring/summer/autumn (rest "quite good already"): denser guitar + strums, extra chord strums as single-note-register voices (spring I/V, summer I/IV/V, autumn i/bVII/IV; strum_C newly exported), autumn gained a guitar_A2 pluck, and a `pick` fingerpicked motif (guitar_A2, step 0.5, every 4 bars) in all three. +0.8..2 dB per season; awaiting the user's ear.

2026-10-16 CHASM PLAYS IT (user asked, "tune in the music app, then apply to the game"): `make publish GAME=chasm
SCORE=chasm` -> apps/chasm/assets/music (46 ogg, 2.2 MB). apps/chasm/ApplicationChasmMusic.cpp: 3 MusicPlayers
(land = season section posted at the season's turn; clouds/chasm layers faded by the view's unexplored/seen-chasm
share, land ducked MUSIC_LAND_DUCK; silent layer paused), buses music_land/_clouds/_chasm under `music`; `music` cue
action; chasm_reveal row on the chasm area's first sight. Plays on in a pause (agent's choice, archer pauses).
Re-publish after tuning. See [[chasm-areas-plan]].

**How to apply:** tune samplescan's GuessCategory thresholds on the real library once it lands, not on synthetic tones; ask the user to listen - Claude cannot judge whether it sounds right.
