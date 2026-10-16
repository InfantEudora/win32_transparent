---
name: ogg-vorbis-settings
description: "archer music samples ship as Ogg Vorbis (32 kHz cap, q-1) since 2026-09-30; make publish encodes with foobar's oggenc2; core decodes at load via stb_vorbis"
metadata:
  node_type: memory
  type: project
  originSessionId: 769861fc-c639-438b-83de-ddeeafeef090
  modified: 2026-10-16T12:57:22.703Z
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

**How to apply:** archer SFX (assets/sound, 41 files) were converted too on 2026-09-30: 11.0 MB -> 537 KB, cues/archer.json names .ogg. Only the `cues` part of archer_test moved (landing plays 1 tick later, peak shifted); baselines rewritten. Ship exe 46.6 MB, meshes 34 MB of it.
New SFX arrive as wav: since 2026-10-16 `python tools/sound_ogg.py <app>` converts every wav in apps/<app>/assets/sound/
with the same settings, moves the wav to art_source/<app>/sound/ (the source), and rewrites the cue tables to .ogg.
Used for chasm's first ten (512 KB -> 65 KB). The user called the setting "pretty much just the lowest setting".
Since 2026-10-16 (chasm's voices) it walks SUBFOLDERS (kept under art_source too) and TRIMS the silent ends of anything
under sound/speech/ (40 ms lead, 80 ms tail, faded) - generated takes started up to 0.6 s late, which reads as lag
after a click. Effects are never trimmed. The kept wav is the untrimmed source.
Related: [[adaptive-music-plan]].
