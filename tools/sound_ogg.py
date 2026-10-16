"""
Sound effects to Ogg Vorbis, for a game's assets/sound/ - the same encoding apps/music's `make publish`
gives the music (see the long note above its publish rule): oggenc2 at -q -1, libvorbis's floor, and only
a file above 32 kHz resampled to 32 kHz. Listened for on 2026-09-30 against pads AND struck samples, and
not told apart from the wavs. Mono stays mono, stereo stays stereo.

    python tools/sound_ogg.py chasm              # every .wav in apps/chasm/assets/sound/
    python tools/sound_ogg.py chasm --dry-run    # say what it would do

For each wav there:
  - encodes it to <name>.ogg beside it, replacing an .ogg of that name;
  - MOVES the wav to art_source/<app>/sound/ - it is the source, kept, but no longer an asset, so a baked
    build does not carry both;
  - rewrites "sound/<name>.wav" to "sound/<name>.ogg" in the app's cue tables (assets/cues/*.json).
The loaders tell the two apart by content, not by name (core/AudioDecode.h), so nothing else changes.

New recordings arrive as wav in assets/sound/: drop them there, name them in the cue table as .wav or
.ogg, and run this.

OGGENC in the environment names another oggenc2; the default is foobar2000's (64-bit install).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OGGENC = os.environ.get("OGGENC", r"C:\Program Files\foobar2000\encoders\oggenc2.exe")
QUALITY = "-1"
MAX_RATE = 32000


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

    wavs = sorted(f for f in os.listdir(sound_dir) if f.lower().endswith(".wav"))
    if not wavs:
        print("nothing to do: no .wav in %s" % sound_dir)
        return
    before = after = 0
    for name in wavs:
        src = os.path.join(sound_dir, name)
        dst = os.path.join(sound_dir, name[:-4] + ".ogg")
        with wave.open(src, "rb") as w:
            rate = w.getframerate()
        cmd = [OGGENC, "-Q", "-q", QUALITY]
        if rate > MAX_RATE:
            cmd += ["--resample", str(MAX_RATE)]
        cmd += ["-o", dst, src]
        size = os.path.getsize(src)
        if args.dry_run:
            print("would encode %-28s %6d Hz %7d bytes" % (name, rate, size))
            continue
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(dst):
            sys.exit("oggenc2 failed on %s: %s" % (name, (r.stderr or r.stdout).strip()))
        before += size
        after += os.path.getsize(dst)
        os.makedirs(keep_dir, exist_ok=True)
        shutil.move(src, os.path.join(keep_dir, name))
        print("%-28s %6d Hz %7d -> %6d bytes" % (name, rate, size, os.path.getsize(dst)))

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
