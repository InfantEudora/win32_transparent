"""
Chasm's replay test: a recording made from a save replays to the same state, twice over.

    1. Paint a starting village with a road, a walled garden with a lane to its gate, and a walker
       on the road - this is the SAVE the recording will start from, so it starts with a walker part
       way down its path.
    2. Record: paint more over MCP (houses, storeys, an erase, fields, a road branch the walker plans
       onto, a second walker, a third that lives in the garden and leaves by its gate) at different
       ticks, stop.
    3. Paint junk on top, so a replay that failed to restore the start would show it.
    4. Replay twice. Each must end in exactly the zones the original reached (compared as saves:
       every house with its storeys, every field, every ground and road plot) and with the same
       walkers (home and goal - where they are depends on how long after the end the save is taken),
       and the two replays' per-tick traces must match - which covers the walkers tick by tick.

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
    """What a save holds that a replay must reproduce: houses with storeys, fields, ground and roads,
    which walkers there are, and the world."""
    call("chasm_save", {"name": save_name})
    with open("apps/chasm/saves/%s.json" % save_name) as f:
        s = json.load(f)
    walkers = sorted((k["home"], k["goal"]) for k in s.get("walkers", []))
    return (sorted(map(tuple, s["houses"])), sorted(s["fields"]), sorted(map(tuple, s.get("grounds", []))),
            walkers, s["world_hash"])


def main():
    call("sim_pause", {"paused": False})
    call("chasm_generate", {"defaults": True})

    # 1. the starting village
    for i in range(8):
        paint("house_paint", -90 + i * 1.9, 20.0)
    for fx in range(-92, -76, 4):
        paint("field_paint", float(fx), 32.0)
    call("chasm_walker", {"op": "clear"})
    # a walled garden south of the road, and a lane from the road into it: it ends at the wall, so the
    # wall opens a gate there - the only way in for a walker
    for gx in range(-71, -63):
        for gz in range(0, 6):
            call("chasm_paint", {"op": "ground_paint", "ground": "garden", "x": float(gx), "z": float(gz)})
    call("chasm_road", {"x0": -96, "z0": 10, "x1": -60, "z1": 10})
    lane = call("chasm_road", {"x0": -67, "z0": 10, "x1": -67, "z1": 3})
    call("chasm_walker", {"op": "spawn", "home_x": -95, "home_z": 4, "goal_x": -62, "goal_z": 14})
    time.sleep(0.6)     # so the save catches it part way down an edge, not standing on a plot

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
    call("chasm_road", {"x0": -78, "z0": 10, "x1": -70, "z1": 28})
    call("chasm_walker", {"op": "spawn", "home_x": -70, "home_z": 27, "goal_x": -94, "goal_z": 12})
    call("chasm_walker", {"op": "spawn", "home_x": -66, "home_z": 2, "goal_x": -90, "goal_z": 10})
    time.sleep(0.5)
    call("chasm_road", {"x0": -66, "z0": 10, "x1": -62, "z1": 14})
    time.sleep(0.3)
    status = call("input_record", {"action": "stop"})
    recording = status.get("last_recording") or status.get("last_file") or ""
    original = zones_of("replay_test_original")

    # 3. junk on top
    for i in range(10):
        paint("house_add", -60 + i * 1.9, -40.0)
    call("chasm_walker", {"op": "spawn", "home_x": -60, "home_z": -44, "goal_x": -40, "goal_z": -44})

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
            print("  ground original %d, replay %d" % (len(original[2]), len(zones[2])))
            print("  walkers original %s, replay %s" % (original[3], zones[3]))
    t1 = results[0][1]
    t2 = results[1][1]
    if json.dumps(t1, sort_keys=True) != json.dumps(t2, sort_keys=True):
        ok = False
        print("the two replays' traces differ")
    ticks = t1.get("ticks") if isinstance(t1, dict) else None
    print("original: %d houses, %d fields, %d ground plots, %d walkers" %
          (len(original[0]), len(original[1]), len(original[2]), len(original[3])))
    print("recording %s, %s traced ticks" % (recording or "(last)", ticks or "?"))
    if not lane.get("gates"):
        ok = False
        print("the lane into the garden made no gate: %s" % lane)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
