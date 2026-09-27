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

Two sliders shape it. **Suspense** moves every part between its calm and tense values.
**Brightness** runs from rumble and bass (0) to high and bright (1), with 0.5 the score as
written. Every part has a height from C2 to C6: its note, the middle of its register, or for an
unpitched bed how bright the recording is. A score can set `"height"` itself when that estimate
is wrong (desert wind hiss reads as fully bright). Brightness re-weights the parts by height
without changing the overall level, confines each voice to the lower or upper half of its
register, and cuts the far side of a 500 Hz split. Measured on 40-second renders: the
spectral centroid goes 178 → 307 → 699 Hz from 0 to 0.5 to 1, with the RMS level within 2 dB
throughout.

A score can have **sections**: sets of beds and voices that take turns, so a long stretch of
play hears the same theme from a different band rather than one loop. Beds and voices at the
top level of the file play in every section, and those inside a section play only while it
is on. A section change waits for a bar line: the new voices start on the downbeat and the beds
crossfade over `section_fade_s`. Sections move on by themselves after `bars` (or
`section_bars` for all of them), in order or at random (`section_order`), or when asked.
jungle.json has two, 16 bars each, with the tension drones shared:

- **canopy**: the bright day set. Bamboo jungle, desert wind, the flute loop and violin; kalimbas, pan flute, high flute.
- **undergrowth**: lower and earthier. Low wind, cemetery wind, the atmospheric drums (moved from
  A# to A), the jungle at a distance, and creepy whistles that come in with suspense; the hang drums,
  a bass note, zanka, a metallophone sparkle, the bamboo-flute flourish, and a low string that
  plays only when tense.

Measured on 60-second renders at suspense 0.2: both read as A minor, and they are within 1 dB of
each other (−25.7 and −24.9 dB RMS); the undergrowth carries 69% of its energy under 200 Hz
against the canopy's 51%. The level holds through the crossfade.

The "Music" panel and the MCP tools do the same things: `music_state`, `music_set`
(suspense, brightness, bpm, gains), `music_key` (root / shift / mode, at the next bar or now),
`music_section` (by name, or the next one), `music_stinger`, `music_reload`, and
`music_render`, which writes an offline wav to `renders/` (with an optional timeline of key
changes, section changes, suspense moves and stingers). Measure a
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

### The Library panel

The bench's second panel is the catalog, live. Every file under `samples/` appears in one of
four states: **not scanned** (new since the last scan), **unclassified** (measured, with a guess
shown in grey), **classified** (has a category), or **missing** (catalogued, gone from disk).
Select a file to see how it measured, **Audition** it (it plays as recorded, beside the music),
and set its category, instrument, root and comment. **Accept guess** fills the fields in, and
**Save** writes them to the catalog. **Scan new** measures only the files that are not scanned
yet and leaves every other row as it is (`samplescan --new`); **Scan all** re-measures the
whole library, which is the one to run after a threshold changes. Both keep what you typed.
The MCP tools are `library_list`, `library_get`, `library_scan`,
`library_classify` and `library_audition`.

The analysis stays in samplescan, which the app runs as a process: this app links the
engine's miniaudio, which has no decoders, and it cannot also carry samplescan's build. So
samplescan has to be built (`cd tools/samplescan && mingw32-make.exe`).

### Cataloguing

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd tools/samplescan && mingw32-make.exe -j8
./build/samplescan.exe                       # scans apps/music/samples, writes catalog.csv
./build/samplescan.exe --new                 # only the files catalog.csv has no row for yet
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
