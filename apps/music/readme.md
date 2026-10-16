# music

The home of the adaptive background music: ambient layers and generated notes that follow the
game's key, tempo and suspense, rather than a fixed score. This app is the DESIGNER - the sample
library, the score, and a bench to play and measure it - and the games play what it makes. The
score, the engine and the player are core (`core/MusicScore.h`, `core/MusicEngine.h`,
`core/MusicPlayer.h`, built with `USE_SOUND`); what lives here is the panels and the library.

## Handing a score to a game

```bash
mingw32-make.exe publish GAME=archer SCORE=jungle
```

The game keeps its own copy, in git: the score goes to `apps/<game>/assets/music/<score>.json`,
and every wav it names goes to `music/sounds/` beside it. Only the wavs the score names are
copied; an export no part plays is an experiment, and stays here. Run it again after changing the
score. It overwrites what the game has, and it leaves a sample the score has stopped naming for
you to delete. How archer plays the score is described at `title_music` in
`apps/archer/ApplicationArcher.h`; how chasm plays its own (`GAME=chasm SCORE=chasm`) at the top of
`apps/chasm/ApplicationChasmMusic.cpp`.

**The game's copy is Ogg Vorbis.** Each wav is encoded to `<name>.ogg`, and the game's score is
rewritten to name the `.ogg` files. A `.wav` of the same name already in the game is deleted,
because the `.ogg` replaces it. The wavs here stay the source; this bench still plays and exports
wav. The settings are `-q -1` at 32 kHz, resampling only files above that rate, with mono and
stereo kept as they are. Those were chosen by listening on 2026-09-30, and the reasons are in the
`publish` block of the makefile. For archer this took 66 MB of wav to 2.0 MB, and 57.8 MB to
1.9 MB in the baked ship exe. The encoder is foobar2000's `oggenc2.exe`, from
`C:\Program Files\foobar2000\encoders\`. `OGGENC=<path>` points it at another copy, and `OGG=0`
copies plain wavs instead. The game decodes the `.ogg` files to PCM at load (`core/AudioDecode.h`),
which takes about 0.5 s for all 42.

**A game's sound effects** (`apps/archer/assets/sound/`) take the same settings. Since 2026-10-16
`python tools/sound_ogg.py <game>` converts every wav in a game's `assets/sound/` with them, keeps
the wav in `art_source/<game>/sound/` and points the cue tables at the `.ogg`; by hand it is: Add `--resample 32000` only
for a file above 32 kHz; the rate is the 4 bytes at offset 24 of a plain wav
(`od -An -tu4 -j24 -N4 file.wav`). For example:

```bash
"/c/Program Files/foobar2000/encoders/oggenc2.exe" -Q -q -1 -o web_hit.ogg web_hit.wav
```

Then name the `.ogg` in the game's cue table (`assets/cues/archer.json`), check that it loads (the
log says `Loaded sound '<name>' (sound/<name>.ogg)`), and delete the wav, which is how archer's
other effects were handled on 2026-09-30.

## The bench

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd apps/music
mingw32-make.exe samples            # once: exports the samples the score uses to assets/music/sounds/*.wav
mingw32-make.exe -j8
./build/music_nophysics.exe --minimized --mcp-port 8767 2>stderr.log &
```

The score is [assets/music/jungle.json](assets/music/jungle.json) at start: key, mode, tempo, and
the beds, voices, motifs and stingers with their calm and tense values. The combo beside Reload
lists every score in `assets/music/` and switches to one; `music_reload` takes `score` for the same.
Edit a score and press Reload. How each part behaves is described in `core/MusicScore.h` and
`core/MusicEngine.h`.

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
A section with `"rotation": false` is one the game asks for by name - a title screen, a cave -
rather than one that takes its turn: the rotation never wanders into it, it holds until asked to
move (`section_bars` does not apply to it), and "next" from inside it goes back to the rotation.
jungle.json has two in the rotation, 16 bars each, and two asked for by name, all sharing the
tension drones:

- **canopy**: the bright day set. Bamboo jungle, desert wind, the flute loop and violin; kalimbas, pan flute, high flute.
- **undergrowth**: lower and earthier. Low wind, cemetery wind, the atmospheric drums (moved from
  A# to A), the jungle at a distance, and creepy whistles that come in with suspense; the hang drums,
  a bass note, zanka, a metallophone sparkle, the bamboo-flute flourish, and a low string that
  plays only when tense.
- **menu** (not in the rotation): the title screen, a night clearing. The jungle night pad moved
  from B to A, frogs and crickets, and a slow tribal drum loop that happens to be 16 beats at
  60 bpm; a tremolo flute on long notes, a second hang drum, the kalimba and a low guitar pluck.
  The `horn` stinger (a war horn) is meant for Start.
- **cave** (not in the rotation): no wind, since it is indoors. A synth bass on A under a
  dungeon rumble moved to E; a spirit cello on D and a scary texture that come in only with
  suspense; metallophone and hang "drips" panned wide, and a cello walking slowly in the bass.
  The `gust` stinger (a creepy wind gust) is for the cave mouth, `sweep` for bats or falling
  rocks.

Measured on 60-second renders at suspense 0.2: canopy and undergrowth both read as A minor, and
they are within 1 dB of each other (−25.7 and −25.6 dB RMS); the undergrowth carries 68% of its
energy under 200 Hz against the canopy's 51%. The level holds through the crossfade. The menu is
the lightest, at −27.7 dB with 26% under 200 Hz; the cave is the darkest, at −29.4 dB with 64%,
and becomes −25.1 dB with 77% at suspense 0.8. Both centre on A (A2 and A1), too strongly for
samplescan to name a key.

### Motifs, and a section's own key

A **motif** is a phrase that is written rather than generated, played now and then by one sampled
instrument. Its notes are scale degrees, so the same phrase sounds in whatever key and mode is
playing: one tune heard bright in ionian and sad in aeolian. It is text, one token per step:

```json
"phrases": {"theme": "1 - 2 3 | 5 - 6 5 | 3 - 2 1 | 2 - 1 -"},
"motifs": [{"name": "theme", "phrase": "theme", "sample": "music/sounds/hang_G4.wav", "root": "G4",
            "home": "D4", "ring": true, "every_bars": 8, "chance_calm": 0.6, "chance_tense": 0.3}]
```

`1`-`7` are degrees (`'` an octave up, `,` an octave down), `-` holds, `.` rests, `|` is ignored.
Degree 1 sounds on the key's root nearest `home`. `step_beats` sets how long a step is (1 beat by
default). A phrase named in `phrases` can be shared by motifs in several sections, so one theme is
written once. A motif may start on every `every_bars`th bar - of its section, so a section's motif
can come in on the section's first downbeat - with a chance of `chance_calm`..`chance_tense`.
`ring` lets each note ring out, for plucked and struck samples; otherwise a note is released at its
written length over `release_s`. The Motifs buttons and `music_motif` play one at the next bar.
In a pentatonic mode a degree the scale lacks moves to its nearest neighbour (`core/MusicScore.h`).

A voice with `"unpitched": true` plays its sample as recorded - no key, no scale, no register - but
is rolled against its density like any voice: birdsong, a woodpecker, a far-off voice, dropped in
now and then. Better than a bed for a recording that fades out at its end, whose loop would have
a hole in it. The note log shows its notes as `-`.

A voice whose register is a single note plays only that note: chasm.json's guitar strums are
chords, so each is held to one chord of its own (`"low": "D2", "high": "D2"`) - the F major strum
becomes a D major chord, the E minor one a D minor chord - and the frame drum to the fifth. A
season's other chords are more such voices, each on its own note: the same F major strum at G2
is a G major chord, the C major one at A1 an A major chord. Which chords suit is the section's
mode, so they are chosen per section. The register is absolute, so these do NOT follow a key
change to another root (a held note the new scale lacks still plays); chasm stays on D.

A **section** may name its own `mode` and `root`, which it takes on its first downbeat, so choosing
the section is enough to hear it as it is meant. A key change asked for afterwards still applies,
until the next section that names one. The modes are pentatonic_minor, aeolian, dorian, phrygian,
pentatonic_major, ionian, lydian and mixolydian.

### chasm.json

Chasm's music, drafted 2026-10-14: folk chamber music - plucks, flute, fiddle and cello, a hand
drum, sparse. Its own samples (birds, woods, frame drum, guitar strums and a note, a breathy pan
flute, choir, pad, spooky chimes and voice) went in the same day; the rest are still stand-ins from
the jungle library. Root D throughout, so every part shares the root and the fifth. One theme (the
`theme` phrase) runs through four seasons, each playing it on its own instrument and in its own mode:

- **spring**: pentatonic major; birds and a woodpecker, breeze, frogs; kalimba, guitar, D and A
  major strums, a sparse frame drum, a high flute. Theme on hang drum.
- **summer**: ionian, the fullest; crickets and woods; kalimba, hang, guitar, D, G and A major
  strums (I, IV, V), frame drum, fiddle, far birds. Theme on flute.
- **autumn**: dorian; low wind, gusts, the rain flute loop; hang, a swelling guitar note, a guitar
  pluck, D minor, C and G strums (i, bVII, IV - the dorian folk progression), frame drum, breath
  flute, cello and bass. Theme on cello.

Those three also have a `pick`: a fingerpicked guitar figure on the tonic chord, two bars of
half-beat steps (`1, 5, 1 3 5 3 1 5,` ...), which may start every 4 bars. It is written in
degrees like the theme, so it is a D major arpeggio in spring and summer and D minor in autumn.
It can land under the theme, which it harmonises.
- **winter**: aeolian, sparse; snow wind, a drone and the angelical pad, cold strings when tense;
  metallophone "ice", breath flute and a low cello. Theme on a high flute, half as fast again.

Two more are asked for by name, and are meant to play in the game on players of their own, faded
in by what the camera looks at, rather than taking a turn:

- **clouds**: phrygian, over unexplored ground - unease, not horror. Winds, whistles, a low
  heartbeat, a distant drum, spooky chimes and a voice now and then; the theme's opening as a slow
  cello `fragment`, whose flat second is the unease.
- **chasm**: dorian, over the chasm once it is seen - awe, no rhythm. Rift wind, a root drone,
  the angelical choir, wide glints and a hang `call` in open fifths. The `reveal` stinger (the war
  horn) is for the first time the chasm comes into sight. Dorian because the choir is recorded on
  the white keys, which IS D dorian, so it plays as recorded; lydian (wonder) would need it three
  semitones down.

Measured on 60-second renders at suspense 0.2, every section is within 1.8 dB: spring -25.1,
summer -25.7, autumn -26.9, winter -26.1, clouds -26.7, chasm -26.0 dB RMS.

The guitar went up in spring, summer and autumn on 2026-10-16 (more guitar and strum notes, the
other chords, the `pick`). On 120-second renders at suspense 0.2, two seeds each, that added
0.8 to 2.0 dB: spring -27.1/-26.7 to -25.5/-25.8, summer -26.0/-26.6 to -25.2/-24.6, autumn
-27.1/-27.4 to -26.3/-26.1, beside winter's unchanged -24.8/-26.2. Summer's key estimate now tips
to A major rather than D major, both times by a margin of 0.03 to 0.04 (it was D major by 0.04 to
0.05): more weight on A from the A major strum and the pick, not a note out of key.

The bench plays one section at a time, so the blend of the land with clouds or chasm is not heard
here. **The game plays it since 2026-10-16** (`apps/chasm/ApplicationChasmMusic.cpp`): one player
in the season's section, which it is sent at each season's turn; the `clouds` and `chasm` sections
on players of their own, faded in by how much of the view is unexplored ground and seen chasm, with
the season's turned down under them; `reveal` when the chasm is first seen. It plays on in a pause.
The layering is the game's (`MUSIC_*` in that file); everything else is this score's.

**Pause** is the game's pause, tried here before any game uses it. It fades the music out over
`pause_fade_s` (1.5 s) and then HOLDS it. Once the music is silent the engine stops rendering, so
its clock, the beat, every bed's place in its loop and every ringing note all stop where the fade
ended. **Resume** fades it back in over `resume_fade_s` (0.5 s) from exactly that point. This is
not a mute: a mute lets the music run on, so it comes back somewhere else in the phrase. The
audition plays through a pause, because it belongs to the library, not to the music. The pause is
in real time, not game time: the game's `sim_pause` and Escape to the title will both trigger it,
and the music does not step with `sim_step`.

The "Music" panel and the MCP tools do the same things: `music_state`, `music_set`
(suspense, brightness, bpm, gains, the two fade times), `music_pause`, `music_key` (root /
shift / mode, at the next bar or now), `music_section` (by name, or the next one),
`music_stinger`, `music_reload`, and
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

**Export** writes the selected file to `assets/music/sounds/<name>.wav`, which a score names as
`music/sounds/<name>.wav`. The name starts as instrument and root (`kalimba_Fs3`), and **Trim** starts
on unless samplescan found the file loopable, because a loop's seam is its first and last
sample. Every export is recorded in `samples/exports.csv` (`name,file,trim`), and that list is
what `make samples` rebuilds the wavs from. So an export made in the panel is one a fresh
checkout gets back: the wavs are not in git, and the list is. A name another file already has
turns the button into **Replace**. **Export missing** writes every listed wav that is not on
disk, which does what `make samples` does without leaving the bench. The filter's
"Classified, not exported" list is what is still waiting for a wav. A score that is already
playing a wav hears the new one after Reload.

The MCP tools are `library_list`, `library_get`, `library_scan`, `library_classify`,
`library_audition` and `library_export`.

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
