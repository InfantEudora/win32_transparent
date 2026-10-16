"""
Sound effects to Ogg Vorbis, for a game's assets/sound/ - the same encoding apps/music's `make publish`
gives the music (see the long note above its publish rule): oggenc2 at -q -1, libvorbis's floor, and only
a file above 32 kHz resampled to 32 kHz. Listened for on 2026-09-30 against pads AND struck samples, and
not told apart from the wavs. Mono stays mono, stereo stays stereo.

    python tools/sound_ogg.py chasm              # every .wav under apps/chasm/assets/sound/
    python tools/sound_ogg.py chasm --dry-run    # say what it would do
    python tools/sound_ogg.py chasm --again speech/     # the kept sources again, after a rule here changed

For each wav there, in a subfolder or not:
  - encodes it to <name>.ogg beside it, replacing an .ogg of that name;
  - MOVES the wav to art_source/<app>/sound/, in the same subfolder - it is the source, kept, but no longer
    an asset, so a baked build does not carry both;
  - rewrites "sound/<path>.wav" to "sound/<path>.ogg" in the app's cue tables (assets/cues/*.json).
The loaders tell the two apart by content, not by name (core/AudioDecode.h), so nothing else changes.

SPEECH (anything under sound/speech/) is made ready first, an effect never:
  - TRIMMED: a spoken line answers a click, and the generated takes start 0.16-0.62 s in (chasm's taunts,
    2026-10-16): that much late reads as lag. Kept: everything from TRIM_LEAD before the first sample above
    TRIM_FLOOR to TRIM_TAIL after the last, faded in and out over a few ms so the cut does not click. The
    floor sits under a breath and above the takes' own padding (at most 46 of 32768 in them).
  - LEVELLED: every line to the same loudness, SPEECH_LEVEL - so a voice's lines sound alike whichever is
    drawn, and two voices take one gain in the cue table. The takes ranged 8 dB (the user asked for them
    evened out, 2026-10-16). Loudness is the RMS of the voiced part: LEVEL_WINDOW windows within LEVEL_GATE of
    the loudest, so pauses and breaths do not pull it down. Never raised past SPEECH_PEAK at its peak.
The wav kept in art_source is the take as it came; --again makes the .ogg from it once more, leaving it
there, when these rules change.

New recordings arrive as wav in assets/sound/: drop them there, name them in the cue table as .wav or
.ogg, and run this.

OGGENC in the environment names another oggenc2; the default is foobar2000's (64-bit install).
"""
import argparse
import array
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OGGENC = os.environ.get("OGGENC", r"C:\Program Files\foobar2000\encoders\oggenc2.exe")
QUALITY = "-1"
MAX_RATE = 32000

SPEECH_DIR = "speech"       #under sound/: trimmed
TRIM_FLOOR = 96             #of 32768, about -51 dBFS
TRIM_LEAD = 0.040           #seconds kept before the first sound...
TRIM_TAIL = 0.080           #...and after the last, for a breath's tail
TRIM_FADE = 0.005           #the fade at each cut
SPEECH_LEVEL = -18.0        #dBFS, the voiced part's RMS
SPEECH_PEAK = -1.0          #dBFS, the most a sample is raised to: Vorbis puts back up to a dB (-1.0 decoded at -0.1)
LEVEL_WINDOW = 0.020        #seconds
LEVEL_GATE = -20.0          #dB under the loudest window: what counts as voiced


def db(x):
    return 20.0 * math.log10(max(x, 1e-9) / 32768.0)


def loudness(s, channels, rate):
    """The voiced part's RMS in dBFS, and the peak's."""
    win = max(1, int(LEVEL_WINDOW * rate)) * channels
    power = []
    for i in range(0, len(s) - win + 1, win):
        power.append(sum(x * x for x in s[i:i + win]) / win)
    if not power:
        return -200.0, -200.0
    gate = max(power) * 10.0 ** (LEVEL_GATE / 10.0)
    voiced = [p for p in power if p >= gate]
    return db(math.sqrt(sum(voiced) / len(voiced))), db(max(abs(x) for x in s))


def speech_copy(src, dst):
    """Writes src trimmed and levelled to dst. Returns (lead cut, tail cut, gain dB, level before, level
    after), or None when the file is not 16-bit PCM, which is then encoded as it is."""
    with wave.open(src, "rb") as w:
        channels, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        frames = w.readframes(n)
    if width != 2:
        return None
    s = array.array("h")
    s.frombytes(frames)
    if sys.byteorder != "little":
        s.byteswap()
    loud = [i // channels for i, x in enumerate(s) if abs(x) > TRIM_FLOOR]
    if not loud:
        return None
    first = max(0, loud[0] - int(TRIM_LEAD * rate))
    end = min(n, loud[-1] + 1 + int(TRIM_TAIL * rate))
    out = s[first * channels:end * channels]
    fade = max(1, int(TRIM_FADE * rate))
    frames_out = end - first
    for f in range(min(fade, frames_out)):
        a = f / fade
        for c in range(channels):
            out[f * channels + c] = int(out[f * channels + c] * a)
            g = (frames_out - 1 - f) * channels + c
            out[g] = int(out[g] * a)
    level, peak = loudness(out, channels, rate)
    gain = min(SPEECH_LEVEL - level, SPEECH_PEAK - peak)
    scale = 10.0 ** (gain / 20.0)
    for i in range(len(out)):
        out[i] = max(-32768, min(32767, int(round(out[i] * scale))))
    after, _ = loudness(out, channels, rate)
    if sys.byteorder != "little":
        out.byteswap()
    with wave.open(dst, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    return first / rate, (n - end) / rate, gain, level, after


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("app", help="the app's folder name under apps/, e.g. chasm")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--again", nargs="?", const="", metavar="PATH",
                    help="encode the wavs kept in art_source/<app>/sound/ once more (those under PATH, e.g. speech/), "
                         "leaving them there")
    args = ap.parse_args()

    sound_dir = os.path.join(ROOT, "apps", args.app, "assets", "sound")
    cue_dir = os.path.join(ROOT, "apps", args.app, "assets", "cues")
    keep_dir = os.path.join(ROOT, "art_source", args.app, "sound")
    if not os.path.isdir(sound_dir):
        sys.exit("no %s" % sound_dir)
    if not args.dry_run and not os.path.isfile(OGGENC):
        sys.exit("no encoder at %s - set OGGENC to an oggenc2.exe" % OGGENC)
    f_again = args.again is not None
    from_dir = keep_dir if f_again else sound_dir

    #Paths under sound/, with forward slashes - as the cue tables name them.
    wavs = []
    for folder, _, files in os.walk(from_dir):
        for f in files:
            if f.lower().endswith(".wav"):
                wavs.append(os.path.relpath(os.path.join(folder, f), from_dir).replace(os.sep, "/"))
    if f_again:
        wavs = [w for w in wavs if w.startswith(args.again.replace(os.sep, "/"))]
    wavs.sort()
    if not wavs:
        print("nothing to do: no .wav under %s" % os.path.join(from_dir, args.again or ""))
        return
    before = after = 0
    scratch = tempfile.mkdtemp(prefix="sound_ogg_")
    try:
        for name in wavs:
            src = os.path.join(from_dir, name)
            dst = os.path.join(sound_dir, name[:-4] + ".ogg")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with wave.open(src, "rb") as w:
                rate = w.getframerate()
            size = os.path.getsize(src)
            f_speech = name.startswith(SPEECH_DIR + "/")
            if args.dry_run:
                print("would encode %-40s %6d Hz %7d bytes%s" % (name, rate, size, ", trimmed" if f_speech else ""))
                continue
            what = ""
            encode = src
            if f_speech:
                encode = os.path.join(scratch, "speech.wav")
                done = speech_copy(src, encode)
                if done is None:
                    encode = src
                    what = "  (as it is: not 16-bit, or silent)"
                else:
                    what = "  trimmed %3d + %3d ms, %+5.1f dB (%5.1f -> %5.1f dBFS)" % (
                        done[0] * 1000, done[1] * 1000, done[2], done[3], done[4])
            cmd = [OGGENC, "-Q", "-q", QUALITY]
            if rate > MAX_RATE:
                cmd += ["--resample", str(MAX_RATE)]
            cmd += ["-o", dst, encode]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.isfile(dst):
                sys.exit("oggenc2 failed on %s: %s" % (name, (r.stderr or r.stdout).strip()))
            before += size
            after += os.path.getsize(dst)
            if not f_again:
                kept = os.path.join(keep_dir, name)
                os.makedirs(os.path.dirname(kept), exist_ok=True)
                shutil.move(src, kept)
            print("%-40s %6d Hz %7d -> %6d bytes%s" % (name, rate, size, os.path.getsize(dst), what))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    # The cue tables name the files: point them at the .ogg.
    names = [n[:-4] for n in wavs]
    if os.path.isdir(cue_dir):
        for table in sorted(f for f in os.listdir(cue_dir) if f.endswith(".json")):
            path = os.path.join(cue_dir, table)
            with open(path, "r", encoding="utf-8", newline="") as f:
                text = f.read()
            changed = text
            for n in names:
                changed = re.sub(r'"sound/%s\.wav"' % re.escape(n), '"sound/%s.ogg"' % n, changed)
            if changed != text:
                count = sum(changed.count('"sound/%s.ogg"' % n) - text.count('"sound/%s.ogg"' % n) for n in names)
                if args.dry_run:
                    print("would rewrite %d name(s) in cues/%s" % (count, table))
                else:
                    with open(path, "w", encoding="utf-8", newline="") as f:
                        f.write(changed)
                    print("cues/%s: %d name(s) now .ogg" % (table, count))
    if not args.dry_run:
        print("%d files: %d -> %d bytes; the wavs %s %s" % (len(wavs), before, after, "stay in" if f_again else "are in",
                                                         os.path.relpath(keep_dir, ROOT)))


if __name__ == "__main__":
    main()
