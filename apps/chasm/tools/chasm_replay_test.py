"""
Chasm's replay test: a recording made from a save replays to the same state, twice over.

    1. Paint a starting village - this is the SAVE the recording will start from.
    2. Record: paint more over MCP (houses, storeys, an erase, fields) at different ticks, stop.
    3. Paint junk on top, so a replay that failed to restore the start would show it.
    4. Replay twice. Each must end in exactly the state the original reached (compared as saves:
       every house with its storeys, every field), and the two replays' per-tick traces must match.

Needs a DEBUG or RELEASE chasm running with its MCP server on the given port (default 8769):

    ./build/chasm_nophysics.exe --minimized --mcp-port 8769 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_replay_test.py [port]

Prints PASS or the first difference. Exit code 0 on pass.
"""
import json
import sys
import time
import urllib.request

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8769


def call(name, args=None):
    req = urllib.request.Request(
        "http://127.0.0.1:%d/mcp" % PORT,
        data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                         "params": {"name": name, "arguments": args or {}}}).encode(),
        headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=120).read())
    if "error" in r:
        raise RuntimeError("%s: %s" % (name, r["error"]))
    return json.loads(r["result"]["content"][0]["text"])


def paint(op, x, z):
    return call("chasm_paint", {"op": op, "x": x, "z": z})


def zones_of(save_name):
    """What a save holds that a replay must reproduce: houses with storeys, and fields."""
    call("chasm_save", {"name": save_name})
    with open("apps/chasm/saves/%s.json" % save_name) as f:
        s = json.load(f)
    return sorted(map(tuple, s["houses"])), sorted(s["fields"]), s["world_hash"]


def main():
    call("sim_pause", {"paused": False})
    call("chasm_generate", {"defaults": True})

    # 1. the starting village
    for i in range(8):
        paint("house_paint", -90 + i * 1.9, 20.0)
    for fx in range(-92, -76, 4):
        paint("field_paint", float(fx), 32.0)

    # 2. the recording
    call("input_record", {"action": "start"})
    for i in range(8):
        paint("house_add", -90 + i * 1.9, 20.0)
        time.sleep(0.05)
    for i in range(6):
        paint("house_paint", -90 + i * 1.9, 24.0)
        time.sleep(0.03)
    paint("house_remove", -90 + 2 * 1.9, 20.0)
    paint("house_erase", -90 + 5 * 1.9, 24.0)
    for fx in range(-92, -76, 4):
        paint("field_paint", float(fx), 40.0)
        time.sleep(0.04)
    paint("field_erase", -88.0, 32.0)
    status = call("input_record", {"action": "stop"})
    recording = status.get("last_recording") or status.get("last_file") or ""
    original = zones_of("replay_test_original")

    # 3. junk on top
    for i in range(10):
        paint("house_add", -60 + i * 1.9, -40.0)

    # 4. two replays
    results = []
    for n in (1, 2):
        call("input_replay", {"action": "start", "restore_state": True, "wait": True})
        trace = call("replay_trace", {"parts": True})
        results.append((zones_of("replay_test_replay%d" % n), trace))

    ok = True
    for n, (zones, _) in enumerate(results, 1):
        if zones != original:
            ok = False
            print("replay %d ends in a different state than the original:" % n)
            print("  houses original %d, replay %d" % (len(original[0]), len(zones[0])))
            print("  fields original %d, replay %d" % (len(original[1]), len(zones[1])))
    t1 = results[0][1]
    t2 = results[1][1]
    if json.dumps(t1, sort_keys=True) != json.dumps(t2, sort_keys=True):
        ok = False
        print("the two replays' traces differ")
    ticks = t1.get("ticks") if isinstance(t1, dict) else None
    print("original: %d houses, %d fields" % (len(original[0]), len(original[1])))
    print("recording %s, %s traced ticks" % (recording or "(last)", ticks or "?"))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
