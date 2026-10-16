"""
Sound effects to Ogg Vorbis, for a game's assets/sound/ - the same encoding apps/music's `make publish`
gives the music (see the long note above its publish rule): oggenc2 at -q -1, libvorbis's floor, and only
a file above 32 kHz resampled to 32 kHz. Listened for on 2026-09-30 against pads AND struck samples, and
not told apart from the wavs. Mono stays mono, stereo stays stereo.

    python tools/sound_ogg.py chasm              # every .wav under apps/chasm/assets/sound/
    python tools/sound_ogg.py chasm --dry-run    # say what it would do

For each wav there, in a subfolder or not:
  - encodes it to <name>.ogg beside it, replacing an .ogg of that name;
  - MOVES the wav to art_source/<app>/sound/, in the same subfolder - it is the source, kept, but no longer
    an asset, so a baked build does not carry both;
  - rewrites "sound/<path>.wav" to "sound/<path>.ogg" in the app's cue tables (assets/cues/*.json).
The loaders tell the two apart by content, not by name (core/AudioDecode.h), so nothing else changes.

SPEECH (anything under sound/speech/) has the silence at either end TRIMMED first. A spoken line answers a
click, and the generated takes start 0.16-0.62 s in (chasm's taunts, 2026-10-16): that much late reads as
lag. Kept: everything from TRIM_LEAD before the first sample above TRIM_FLOOR to TRIM_TAIL after the last,
faded in and out over a few ms so the cut does not click. The floor sits under a breath and above the
takes' own padding (at most 46 of 32768 in them). An effect's attack is never touched. The wav kept in
art_source is the untrimmed one.

New recordings arrive as wav in assets/sound/: drop them there, name them in the cue table as .wav or
.ogg, and run this.

OGGENC in the environment names another oggenc2; the default is foobar2000's (64-bit install).
"""
import argparse
import array
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


def trimmed_copy(src, dst):
    """Writes src without its silent ends to dst. Returns (lead cut, tail cut) in seconds, or None when
    the file is not 16-bit PCM, which is then encoded as it is."""
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
    if sys.byteorder != "little":
        out.byteswap()
    with wave.open(dst, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    return first / rate, (n - end) / rate


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("app", help="the app's folder name under apps/, e.g. chasm")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    sound_dir = os.path.join(ROOT, "apps", args.app, "assets", "sound")
    cue_dir = os.path.join(ROOT, "apps", args.app, "assets", "cues")
    keep_dir = os.path.join(ROOT, "art_source", args.app, "sound")
    if not os.path.isdir(sound_dir):
        sys.exit("no %s" % sound_dir)
    if not args.dry_run and not os.path.isfile(OGGENC):
        sys.exit("no encoder at %s - set OGGENC to an oggenc2.exe" % OGGENC)

    #Paths under sound/, with forward slashes - as the cue tables name them.
    wavs = []
    for folder, _, files in os.walk(sound_dir):
        for f in files:
            if f.lower().endswith(".wav"):
                wavs.append(os.path.relpath(os.path.join(folder, f), sound_dir).replace(os.sep, "/"))
    wavs.sort()
    if not wavs:
        print("nothing to do: no .wav under %s" % sound_dir)
        return
    before = after = 0
    scratch = tempfile.mkdtemp(prefix="sound_ogg_")
    try:
        for name in wavs:
            src = os.path.join(sound_dir, name)
            dst = src[:-4] + ".ogg"
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
                encode = os.path.join(scratch, "trimmed.wav")
                cut = trimmed_copy(src, encode)
                if cut is None:
                    encode = src
                    what = "  (not trimmed: not 16-bit, or silent)"
                else:
                    what = "  trimmed %3d + %3d ms" % (cut[0] * 1000, cut[1] * 1000)
            cmd = [OGGENC, "-Q", "-q", QUALITY]
            if rate > MAX_RATE:
                cmd += ["--resample", str(MAX_RATE)]
            cmd += ["-o", dst, encode]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.isfile(dst):
                sys.exit("oggenc2 failed on %s: %s" % (name, (r.stderr or r.stdout).strip()))
            before += size
            after += os.path.getsize(dst)
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
        print("%d files: %d -> %d bytes; the wavs are in %s" % (len(wavs), before, after, os.path.relpath(keep_dir, ROOT)))


if __name__ == "__main__":
    main()
