# music

The home of the adaptive background music: ambient layers and generated notes that follow the
game's key, tempo and suspense, rather than a fixed score. It is a sample library for now. It
becomes an app - a test bench for the music player, the way `testfx` is for effects - once there
is a player to test.

## The bench

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd apps/music
mingw32-make.exe samples            # once: exports the samples the score uses to assets/sound/*.wav
mingw32-make.exe -j8
./build/music_nophysics.exe --minimized --mcp-port 8767 2>stderr.log &
```

The score is [assets/music/jungle.json](assets/music/jungle.json): key, mode, tempo, and the
beds, voices and stingers with their calm and tense values. Edit it and press Reload (or call
`music_reload`). How each part behaves is described in `MusicScore.h` and `MusicEngine.h`.

The "Music" panel and the MCP tools do the same things: `music_state`, `music_set`
(suspense, bpm, gains), `music_key` (root / shift / mode, at the next bar or now),
`music_stinger`, `music_reload`, and `music_render`, which writes an offline wav to
`renders/` (with an optional timeline of key changes, suspense moves and stingers). Measure a
render with `tools/samplescan/build/samplescan.exe --file renders/x.wav`: its `key` line is the
quickest check that nothing in the score is out of key. That is how the jungle bed was found to
be in C# major, and moved into C major by marking it pitched (`"root": "C#4", "degree": 3`).

## The sample library

```
samples/
    unsorted/        drop anything here, in any folder structure a pack came with
    catalog.csv      one row per file, written by tools/samplescan
```

Everything under `samples/` is scanned, not just `unsorted/`, so a file keeps its row when it is
later moved into a sorted folder.

### Cataloguing

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd tools/samplescan && mingw32-make.exe -j8
./build/samplescan.exe                       # scans apps/music/samples, writes catalog.csv
./build/samplescan.exe --file some.mp3       # one file, every measurement, readable
```

It reads WAV (16/24/32-bit, float), FLAC and MP3. OGG, AIFF and M4A are listed as
`unsupported` so they show up in the table to be converted, not silently skipped.

### What the catalog holds

The first columns after `file` are **yours**: `category`, `instrument`, `root`, `comment`. A
re-scan keeps what you type there, matched by path, and rewrites everything else from the
audio. Excel's semicolon CSV (a Dutch-locale save) reads back fine.

The rest is measured or read from the name:

| columns | what they say |
|---|---|
| `guess`, `why` | a first sort into the roles below, and the numbers that decided it |
| `loopable` | no silence at either end and matching end levels: worth listening to as a loop |
| `name_*` | what the path says: instrument words, mood words (creepy, calm, comedy…), a note (`C4`), a key (`Am`), a tempo (`90bpm`) |
| `note_count`, `notes`, `pitch_classes` | the notes in order (`E5 G5 A5`, a slide as `G#4~E4`) and the distinct notes used |
| `onsets`, `onset_rate`, `bpm`, `beat_strength` | attacks, and how strongly they form a pulse |
| `name_vs_pitch` | written note against measured pitch, in semitones. **12 is the other octave convention** (C3 as middle C), not a wrong label |
| `pitch_*`, `voiced`, `stability_cents` | the fundamental, by YIN, and whether there is only one |
| `key`, `key_r`, `key_margin` | for material with several notes; it cannot tell a key from its relative minor/major |
| levels, `lead_ms`/`tail_ms` | dBFS peak and RMS; silence at each end |
| `envelope`, `attack_ms`, `decay20_ms`, `sustain_db` | shape: short / decaying / sustained / swell |
| `head_tail_db` | end level against start level: near 0 suggests a loop |
| `centroid_hz`, `noisiness`, `low_share` | brightness, noise-versus-tone (flatness within each octave), energy under 200 Hz |

The roles `guess` sorts into are what a sample does in the music, not what made it:

- `note_struck`: one pitched note that dies away (kalimba, marimba, bell)
- `note_sustained`: one pitched note that holds (flute, pad, voice)
- `drone`: a low held note
- `hum`: low and held, with no clear pitch
- `texture`: unpitched and held (wind, rain, leaves, whispers)
- `hit`: unpitched and short
- `rhythm`: a pulse (drums, a percussion loop)
- `phrase`: several notes
- `mix`: long and busy, a finished ambience

One more role is assigned **by hand only**, in the `category` column, because two examples are
not enough to guess it from: `stinger`, a one-off hit the music plays on an event (a jump
scare, a discovery).

Where a result was checked by ear, the `comment` column says so ("by ear: …"). Those rows
are the ground truth to re-test against after changing a threshold.

The thresholds behind the guess were set on synthetic tones with known answers, then corrected
against the first 20 real samples (2026-09-26). The name decides only where the audio cannot:
a whistling wind is really pitched, and slow drums show no pulse in five seconds.
