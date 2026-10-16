"""
Cut a seamless loop out of a long ambience recording - a stream, wind, rain: anything steady that a cue plays
with "looping": true. The engine loops a sound by wrapping its last sample onto its first (ma_sound_set_looping
in core/SoundSystem.cpp), so the file itself has to join up; nothing at play time hides a seam.

    python tools/sound_loop.py long.wav loop.wav                 # 10 s, the steadiest stretch it can find
    python tools/sound_loop.py long.wav loop.wav --seconds 8 --fade 1.5
    python tools/sound_loop.py long.wav loop.wav --start 31.25   # take this stretch instead of searching

How the seam is made: take SECONDS + FADE of the recording from START, keep the first SECONDS, and crossfade the
FADE beyond them back over the first FADE. The loop then wraps from x[start+seconds-1] to x[start+seconds] -
two samples that were neighbours in the recording - so there is no step at the wrap at all; the only join is
the crossfade, FADE long, which on noise-like material is not heard as one.

The crossfade is equal-power, corrected for how alike the two overlapping stretches are: two unrelated
stretches of water (the usual case) add in power, so a linear fade would dip by 3 dB in the middle, while
two that happen to match add in amplitude and an equal-power fade would bump instead. Measuring the
correlation and scaling by 1/sqrt(a^2 + b^2 + 2abr) keeps the level flat either way.

Where to cut, when --start is not given: in 0.25 s blocks, the stretch whose level varies least, has no
standout splash (a distinctive event repeating every SECONDS is what gives a loop away), sits near the
recording's typical level, and whose head and tail match in level and brightness so the crossfade has
nothing to bridge. It prints the level across the wrap afterwards, which should look like anywhere else.

16-bit PCM wav in, the same out; channels and rate are kept. tools/sound_ogg.py then encodes it like any
other sound.
"""
import argparse
import array
import math
import sys
import wave

BLOCK_S = 0.25


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit("%s: %d-bit; only 16-bit PCM is handled" % (path, 8 * w.getsampwidth()))
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        a = array.array("h")
        a.frombytes(w.readframes(n))
    if sys.byteorder == "big":
        a.byteswap()
    return ch, rate, a


def write_wav(path, ch, rate, a):
    out = array.array("h", a)
    if sys.byteorder == "big":
        out.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())


def blocks(a, ch, block):
    """Per block of `block` frames: (rms, brightness, peak). Brightness is the rms of the first difference
    relative to the rms - a cheap spectral tilt, enough to tell a hiss from a rumble."""
    feats = []
    step = block * ch
    for s in range(0, len(a) - step + 1, step):
        seg = a[s:s + step]
        ss = sum(x * x for x in seg)
        dd = sum((seg[i] - seg[i - ch]) ** 2 for i in range(ch, len(seg)))
        rms = math.sqrt(ss / len(seg))
        feats.append((rms, math.sqrt(dd / len(seg)) / max(rms, 1.0), max(abs(x) for x in seg)))
    return feats


def choose_start(feats, n_loop, n_fade):
    """The block index to start at; see the module notes for what is weighed."""
    rms_all = sorted(f[0] for f in feats)
    typical = rms_all[len(rms_all) // 2]
    best = None
    for b in range(0, len(feats) - (n_loop + n_fade) + 1):
        span = feats[b:b + n_loop + n_fade]
        r = [f[0] for f in span]
        mean = sum(r) / len(r)
        spread = math.sqrt(sum((x - mean) ** 2 for x in r) / len(r)) / typical
        standout = max(f[2] for f in span) / typical / 10.0     #a crest of 10x typical rms scores 1
        level = abs(mean - typical) / typical
        head, tail = span[:n_fade], span[n_loop:]
        seam = (abs(sum(f[0] for f in head) - sum(f[0] for f in tail)) / n_fade / typical +
                abs(sum(f[1] for f in head) - sum(f[1] for f in tail)) / n_fade)
        cost = spread + standout + level + seam
        if best is None or cost < best[0]:
            best = (cost, b, spread, standout, level, seam)
    return best


def correlation(a, ch, i0, i1, n):
    sxy = sxx = syy = 0
    for k in range(n * ch):
        x, y = a[i0 + k], a[i1 + k]
        sxy += x * y
        sxx += x * x
        syy += y * y
    return sxy / math.sqrt(max(sxx * syy, 1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1].strip())
    ap.add_argument("src", help="the long recording, 16-bit wav")
    ap.add_argument("dst", help="the loop to write")
    ap.add_argument("--seconds", type=float, default=10.0, help="loop length (default 10)")
    ap.add_argument("--fade", type=float, default=2.0, help="crossfade at the seam (default 2)")
    ap.add_argument("--start", type=float, help="take the loop from here, in seconds, instead of searching")
    args = ap.parse_args()

    ch, rate, a = read_wav(args.src)
    frames = len(a) // ch
    block = int(round(rate * BLOCK_S))
    n_loop = int(round(args.seconds / BLOCK_S))
    n_fade = int(round(args.fade / BLOCK_S))
    loop = n_loop * block
    fade = n_fade * block
    if loop + fade > frames:
        sys.exit("%s is %.2f s; a %.2f s loop with a %.2f s fade needs %.2f s" %
                 (args.src, frames / rate, loop / rate, fade / rate, (loop + fade) / rate))
    if fade > loop:
        sys.exit("the fade cannot be longer than the loop")

    feats = blocks(a, ch, block)
    if args.start is None:
        cost, b, spread, standout, level, seam = choose_start(feats, n_loop, n_fade)
        start = b * block
        print("start %.2f s (cost %.3f: spread %.3f, standout %.3f, level %.3f, seam %.3f)" %
              (start / rate, cost, spread, standout, level, seam))
    else:
        start = int(round(args.start * rate))
        if start + loop + fade > frames:
            sys.exit("--start %.2f leaves too little recording after it" % args.start)
        print("start %.2f s (given)" % (start / rate))

    head = start * ch                   #sample index of the loop's first frame
    tail = (start + loop) * ch          #...and of the stretch that is faded back over it
    r = correlation(a, ch, head, tail, fade)
    out = array.array("h", a[head:head + loop * ch])
    clipped = 0
    for i in range(fade):
        t = (i + 0.5) / fade
        g_in, g_out = math.sin(0.5 * math.pi * t), math.cos(0.5 * math.pi * t)
        norm = 1.0 / math.sqrt(max(g_in * g_in + g_out * g_out + 2.0 * g_in * g_out * r, 1e-6))
        for c in range(ch):
            k = i * ch + c
            v = int(round((a[head + k] * g_in + a[tail + k] * g_out) * norm))
            if v > 32767 or v < -32768:
                clipped += 1
                v = max(-32768, min(32767, v))
            out[k] = v
    write_wav(args.dst, ch, rate, out)
    print("wrote %s: %.2f s, %d frames, %d ch, %d Hz; fade %.2f s, correlation %.3f%s" %
          (args.dst, loop / rate, loop, ch, rate, fade / rate, r,
           ", %d samples clipped" % clipped if clipped else ""))

    # The proof: the level in blocks across the wrap - the loop's last 2 s, then its first 3 s - set against
    # the loop's own spread. A seam shows as a dip or a bump at the bar or at the end of the fade.
    lf = blocks(out, ch, block)
    lr = [f[0] for f in lf]
    mean = sum(lr) / len(lr)
    print("loop level %.0f rms, blocks %.0f..%.0f" % (mean, min(lr), max(lr)))
    show = list(range(len(lf) - 8, len(lf))) + list(range(0, min(12, len(lf))))
    for j, k in enumerate(show):
        if j == 8:
            print("  ---- wrap ----")
        rms, bright, peak = lf[k]
        mark = "  <- fade ends" if k == n_fade - 1 else ""
        print("  %6.2f s  %5.0f  %s%s" % (k * BLOCK_S, rms, "#" * int(40 * rms / max(lr)), mark))


if __name__ == "__main__":
    main()
