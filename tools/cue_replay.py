"""
Replays input recordings in a running app and compares what its cues did against a baseline.

The cue layer's proof (apps/archer/cue_plan.md): moving hand-wired sounds onto cues is only a
refactor if the same recording replayed before and after prints the same cue_log lines. So:

    # once, on the code as it is - writes recordings/<name>.cues beside each recording
    python tools/cue_replay.py --write archer_20260925_143356 archer_20260925_145919

    # after the change - replays each one and diffs against its .cues; exit 1 on any difference
    python tools/cue_replay.py archer_20260925_143356 archer_20260925_145919

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
    time.sleep(1.0)
    call("archer_sound", {"volume": 0})

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
        path = os.path.join(folder, name + ".cues")
        if args.write:
            with open(path, "w", newline="\n") as f:
                f.writelines(lines)
            print("%-28s wrote %d lines" % (name, len(lines)))
            continue
        if not os.path.exists(path):
            print("%-28s NO BASELINE (%s) - run with --write first" % (name, path))
            failures += 1
            continue
        with open(path) as f:
            baseline = compared(f.readlines())
        run = compared(lines)
        if run == baseline:
            skipped = len(lines) - len(run)
            print("%-28s same (%d lines%s)" % (name, len(run), ", %d skips not compared" % skipped if skipped else ""))
        else:
            failures += 1
            print("%-28s DIFFERENT" % name)
            sys.stdout.writelines(difflib.unified_diff(baseline, run, name + ".cues", "this run"))
    print("%s (%d of %d different)" % ("FAILED" if failures else "ALL SAME", failures, len(names)))
    sys.exit(1 if failures else 0)

if __name__ == "__main__":
    main()
