"""
Replays input recordings in a running app and compares what its cues did against a baseline.

The cue layer's proof (apps/archer/docs/cue_plan.md): moving hand-wired sounds onto cues is only a
refactor if the same recording replayed before and after prints the same cue_log lines. So:

    # once, on the code as it is - writes recordings/<name>.cues beside each recording
    python tools/cue_replay.py --write archer_test

    # after the change - replays each one and diffs against its .cues; exit 1 on any difference
    python tools/cue_replay.py archer_test

With no recordings named, every recording that already has a .cues file is checked.

The app must be running with its MCP server up; start it --minimized on a port of its own so a
person's session on 8765 is left alone:

    ./build/archer.exe --minimized --mcp-port 8768 2>stderr.log &

It is muted for the run (archer_sound volume 0), which does not change the log - see CueLog.h.

SKIP LINES ARE LEFT OUT of the comparison by default: a skip is the cue system saying why it
did NOT play something (a chance that said no, a gap, a busy group), which is for someone tuning
a cue to read, not something heard - and the hand-wired code the first baselines came from had
no such lines at all. --with-skips compares them too, once every baseline was written by the
cues.
Each replay restores its recording's start state, including the level tick every random draw is
hashed from, so two runs of one recording print identical lines; a difference is a real change.

THE STATE TRACE is checked first: `replay_trace` hands back a hash of every tick's state
(Application::HashSimState), --write keeps it as recordings/<name>.trace, and a check names the
first tick and part that differ - where two runs parted, long before a sound shows it. See
docs/replay_determinism_plan.md. --tick-starts reports state changed between ticks; --detail
gives every physics body, every object and each part of the physics world a part of its own, to
name the one that parts first (a diagnosis - never --write with it).

REPLAYS ARE EXACT (2026-09-28): the same bits every tick, fresh app or not, in any order, debug
or release - so any difference, in the state or the sounds, is a real change. It took the restore
forgetting the animation and the Puppet, and the physics world rebuilding its internal state
(section 9-10 of the plan); if a check ever differs between two runs of one exe, that is a new
leak, and --detail is where to start.
Windows Python; nothing beyond the standard library.
"""
import argparse, difflib, glob, json, os, sys, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("recordings", nargs="*", help="recording names (without .rec)")
    ap.add_argument("--app", default="archer", help="the app folder under apps/, default archer")
    ap.add_argument("--port", type=int, default=8768, help="the app's MCP port, default 8768")
    ap.add_argument("--write", action="store_true", help="write the baselines instead of checking")
    ap.add_argument("--with-skips", action="store_true", help="compare skip lines too")
    ap.add_argument("--tick-starts", action="store_true",
                    help="also report state changed BETWEEN ticks (the app's view, UI or a tool)")
    ap.add_argument("--detail", action="store_true",
                    help="a part per physics body, per object and per physics-world part, to name the one that parts first "
                         "(a diagnosis - do not --write baselines with it)")
    args = ap.parse_args()

    def compared(lines):
        if args.with_skips:
            return lines
        # The third column is the decision: play, stop, act, skip.
        return [l for l in lines if len(l.split()) < 3 or l.split()[2] != "skip"]

    folder = os.path.join(ROOT, "apps", args.app, "recordings")
    names = args.recordings
    if not names:
        names = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(folder, "*.cues")))
    if not names:
        sys.exit("no recordings named and no .cues baselines in %s" % folder)

    url = "http://127.0.0.1:%d/mcp" % args.port
    ids = [0]

    def call(name, arguments=None):
        ids[0] += 1
        body = json.dumps({"jsonrpc": "2.0", "id": ids[0], "method": "tools/call",
                           "params": {"name": name, "arguments": arguments or {}}}).encode()
        req = urllib.request.Request(url, body, {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=600) as r:
            text = json.loads(r.read())["result"]["content"][0]["text"]
        try:
            return json.loads(text)
        except ValueError:
            return text

    # Harmless if the title is already gone; needed if the app has just started.
    call("archer_hold", {"action": "continue", "ticks": 5})
    # Continue fades the title to black before the world takes over (ApplicationArcher's
    # ScreenFade), so wait for the title to have gone rather than for a fixed time. An archer
    # without the `on_title` field is one from before the fade, where a second was plenty.
    time.sleep(0.5)
    for _ in range(40):
        state = call("archer_state", {})
        if not isinstance(state, dict) or not state.get("on_title", False):
            break
        time.sleep(0.25)
    time.sleep(0.5)
    call("archer_sound", {"volume": 0})
    call("replay_trace", {"tick_starts": args.tick_starts, "detail": args.detail, "parts": False})

    def trace_lines():
        # The state trace (Application::HashSimState): one line per tick, `tick total part=hash ...`.
        t = call("replay_trace", {})
        if not isinstance(t, dict) or "trace" not in t:
            return None, []
        lines = []
        between = []
        for row in t["trace"]:
            parts = " ".join("%s=%s" % (k, v) for k, v in sorted(row.get("parts", {}).items()))
            lines.append("%6d %s %s\n" % (row["t"], row["h"], parts))
            if row.get("changed_between"):
                between.append((row["t"], row["changed_between"]))
        return lines, between

    def parts_of(words):
        # The `name=hash` words of a trace line, as {name: hash}. A NAME MAY HOLD SPACES - with
        # --detail every object is a part, named after the object, and "Title Screen" is two words
        # on the line - so a word with no `=` is the start of the next word's name, not a part.
        out = {}
        pending = []
        for w in words:
            if "=" in w:
                k, v = w.split("=", 1)
                out[" ".join(pending + [k])] = v
                pending = []
            else:
                pending.append(w)
        if pending:
            out[" ".join(pending)] = ""
        return out

    def first_difference(baseline, run):
        # The first tick whose line differs, and which parts - the line's `name=hash` words.
        for i in range(max(len(baseline), len(run))):
            a = baseline[i] if i < len(baseline) else None
            b = run[i] if i < len(run) else None
            if a == b:
                continue
            if a is None or b is None:
                return "%s ends at line %d" % ("the baseline" if a is None else "this run", i + 1)
            wa, wb = a.split(), b.split()
            pa = parts_of(wa[2:])
            pb = parts_of(wb[2:])
            parts = sorted(k for k in set(pa) | set(pb) if pa.get(k) != pb.get(k))
            return "from tick %s, in %s" % (wa[0], ", ".join(parts) if parts else "the total")
        return None

    failures = 0
    for name in names:
        call("cue_log", {"clear": True})
        r = call("input_replay", {"file": name, "wait": True})
        if isinstance(r, dict) and r.get("error"):
            print("%-28s REPLAY FAILED: %s" % (name, r["error"]))
            failures += 1
            continue
        time.sleep(0.5)     # the replay's last tick, and anything it fired, lands first
        lines = [l + "\n" for l in call("cue_log", {})["lines"]]
        # Only what follows the replay's OWN restart (CueSystem::Reset logs a `reset` line): between
        # clearing the log and the replay restarting the level, whatever ran before it - a flight
        # still in the air at the end of the last recording - can still log a line or two.
        resets = [i for i, l in enumerate(lines) if len(l.split()) >= 3 and l.split()[2] == "reset"]
        if resets:
            lines = lines[resets[-1] + 1:]
        trace, between = trace_lines()
        for tick, parts in between:
            print("%-28s changed between ticks at %d: %s" % (name, tick, ", ".join(parts)))
        path = os.path.join(folder, name + ".cues")
        trace_path = os.path.join(folder, name + ".trace")
        if args.write:
            with open(path, "w", newline="\n") as f:
                f.writelines(lines)
            if trace:
                with open(trace_path, "w", newline="\n") as f:
                    f.writelines(trace)
            print("%-28s wrote %d lines, %s" % (name, len(lines),
                  "%d traced ticks" % len(trace) if trace else "no state trace (the app has no replay_trace)"))
            continue
        if not os.path.exists(path):
            print("%-28s NO BASELINE (%s) - run with --write first" % (name, path))
            failures += 1
            continue
        with open(path) as f:
            baseline = compared(f.readlines())
        run = compared(lines)
        f_bad = False
        # The state first: it says where two runs parted, which the sounds only show once it is loud.
        if trace and os.path.exists(trace_path):
            with open(trace_path) as f:
                trace_baseline = f.readlines()
            where = first_difference(trace_baseline, trace)
            if where:
                f_bad = True
                print("%-28s STATE DIFFERENT %s" % (name, where))
            else:
                print("%-28s state same (%d ticks)" % (name, len(trace)))
        elif trace:
            print("%-28s no state baseline - run with --write to make one" % name)
        if run == baseline:
            skipped = len(lines) - len(run)
            print("%-28s same (%d lines%s)" % (name, len(run), ", %d skips not compared" % skipped if skipped else ""))
        else:
            f_bad = True
            print("%-28s DIFFERENT" % name)
            sys.stdout.writelines(difflib.unified_diff(baseline, run, name + ".cues", "this run"))
        failures += 1 if f_bad else 0
    print("%s (%d of %d different)" % ("FAILED" if failures else "ALL SAME", failures, len(names)))
    sys.exit(1 if failures else 0)

if __name__ == "__main__":
    main()
