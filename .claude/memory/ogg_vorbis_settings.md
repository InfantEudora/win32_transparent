---
name: ogg-vorbis-settings
description: "archer music samples ship as Ogg Vorbis (32 kHz cap, q-1) since 2026-09-30; make publish encodes with foobar's oggenc2; core decodes at load via stb_vorbis"
metadata:
  node_type: memory
  type: project
  originSessionId: 769861fc-c639-438b-83de-ddeeafeef090
  modified: 2026-09-30T19:44:19.553Z
---

2026-09-30: user listened (headphones, quiet room) and found **-q -1, 32 kHz** inaudible against
the wavs, pads AND plucked/struck samples. Mono downmix is audibly worse at any quality, so it is
ruled out. Only files ABOVE 32 kHz are resampled (the user's rule: the 24 kHz ones stay 24 kHz).

BUILT the same day:
- `core/AudioDecode.{h,cpp}` wraps stb_vorbis (from 3rdparty/miniaudio/extras), USE_SOUND only.
  `SoundSystem::AppendFile` and `LoadMusicSampleFile` sniff "OggS", not the extension. All 42
  samples decode in ~0.5 s even at -Og.
- `apps/music` `make publish` encodes to .ogg, rewrites .wav->.ogg in the GAME's score copy, and
  deletes the game's old .wav. The bench's own wavs stay the source. Knobs are OGG, OGGENC,
  OGG_QUALITY, OGG_MAX_RATE.
- engine.mk gained `APP_SETTINGS` (app-declared command-line knobs). Before that,
  `make publish GAME=archer SCORE=jungle` was refused as "not a build setting".
- Result: 66 MB wav -> 2.0 MB. In the ship blob 57.8 MB -> 1.9 MB, exe 61.9 MB.
  archer_test replay is unchanged.

The encoder is `C:\Program Files\foobar2000\encoders\oggenc2.exe` (the 64-bit install).
-q -1 is libvorbis's floor.

**Why:** the samples folder dominated archer's asset size.

**How to apply:** archer SFX (assets/sound, 41 files) were converted too on 2026-09-30: 11.0 MB -> 537 KB, cues/archer.json names .ogg. Only the `cues` part of archer_test moved (landing plays 1 tick later, peak shifted); baselines rewritten. New SFX arrive as wav and need converting by hand; there is no repo script yet. Ship exe 46.6 MB, meshes 34 MB of it.
Related: [[adaptive-music-plan]].
