# music

The home of the adaptive background music: ambient layers and generated notes that follow the
game's key, tempo and suspense, rather than a fixed score. It is a sample library for now. It
becomes an app - a test bench for the music player, the way `testfx` is for effects - once there
is a player to test.

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
| `name_*` | what the path says: instrument words, a note (`C4`), a key (`Am`), a tempo (`90bpm`) |
| `name_vs_pitch` | written note against measured pitch, in semitones. **12 is the other octave convention** (C3 as middle C), not a wrong label |
| `pitch_*`, `voiced`, `stability_cents` | the fundamental, by YIN, and whether there is only one |
| `key`, `key_r`, `key_margin` | for material with several notes; it cannot tell a key from its relative minor/major |
| levels, `lead_ms`/`tail_ms` | dBFS peak and RMS; silence at each end |
| `envelope`, `attack_ms`, `decay20_ms`, `sustain_db` | shape: short / decaying / sustained / swell |
| `head_tail_db` | end level against start level: near 0 suggests a loop |
| `centroid_hz`, `flatness`, `low_share` | brightness, noisiness, energy under 200 Hz |

The roles `guess` sorts into are what a sample does in the music, not what made it:

- `note_struck`: one pitched note that dies away (kalimba, marimba, bell)
- `note_sustained`: one pitched note that holds (flute, pad, voice)
- `drone`: a low held note
- `hum`: low and held, with no clear pitch
- `texture`: unpitched and held (wind, rain, leaves, whispers)
- `hit`: unpitched and short
- `phrase`: several notes
- `mix`: long and busy, a finished ambience

The thresholds behind the guess were set on synthetic tones with known answers and on the
effects already in the repo, and want tuning once the real library is in.
