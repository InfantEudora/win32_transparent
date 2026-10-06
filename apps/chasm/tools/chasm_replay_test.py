"""
Chasm's replay test: a recording made from a save replays to the same state, twice over.

    1. Paint a starting village with a road, a walled garden with a lane to its gate, and a walker
       on the road - this is the SAVE the recording will start from, so it starts with a walker part
       way down its path.
    2. Record: paint more over MCP (houses, storeys, an erase, fields, a road branch the walker plans
       onto, a second walker, a third that lives in the garden and leaves by its gate; and buildings -
       a store made in one stroke and then split by an erase in its middle, a woodcutter, a house
       extended by a stroke that starts on it, a field's crop changed; a WINCH on the rim above the
       home balcony and a walker who lives on the balcony and rides it up to the plateau; the date set
       to autumn, a recorded calendar command; a store beside the woodcutter, which makes it his wood
       store, and the split store's first half set to take food only - so the woodcutter's worker
       fells and carries during the recording; and by the settlers' camp, houses for its families and
       a woodcutter's hut, so families move in and the best-suited of them walks to work) at different
       ticks, stop.
    3. Paint junk on top, so a replay that failed to restore the start would show it.
    4. Replay twice. Each must end in exactly the zones the original reached (compared as saves:
       every building with its id, kind, plots and storeys or cells and crop, every ground and road
       plot), on the same day, and with the same
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
"""
Where the village stands: every x below is shifted by this. Seed 1's chasm now comes from the seed,
and the village's old ground (x -100 to -36) is the chasm floor, where nothing may be built; at +190 it
stands on the home (east) side's plateau, all of it temperate ground. Moved from +190 to +250 when the
smaller balconies (2026-10-06) re-rolled every seed's rivers and one ran through the old spot.
"""
DX = 250.0


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


REFUSED = {}
SITE = []       # where the recording put its construction site


def paint(op, x, z, **extra):
    args = {"op": op, "x": x + DX, "z": z}
    args.update(extra)
    r = call("chasm_paint", args)
    if r.get("refusal"):
        REFUSED[r["refusal"]] = REFUSED.get(r["refusal"], 0) + 1
    return r


def find_winch():
    """A rim plot above the home balcony a winch can stand on, and a balcony home and plateau goal a
    walker gets between by riding it - found by trying, since the layout comes from the seed. Leaves
    nothing behind: the probe winch is erased and the probe walkers cleared."""
    g = call("chasm_generate", {"defaults": True})
    bal = [b for b in g["layout"]["balcony_list"] if b["side"] == "east"][0]
    (sx, sz), (ex, ez), (mx, mz) = bal["start"], bal["end"], bal["middle"]
    cx, cz = (sx + ex) / 2, (sz + ez) / 2
    dx, dz = cx - mx, cz - mz
    n = (dx * dx + dz * dz) ** 0.5
    dx, dz = dx / n, dz / n
    winch = None
    for along in [i * 0.5 for i in range(-40, 41)]:
        for off in [i * 0.5 for i in range(-12, 13)]:
            x, z = cx + dx * off - dz * along, cz + dz * off + dx * along
            if not call("chasm_paint", {"op": "build_paint", "kind": "winch", "x": x, "z": z}).get("refusal"):
                winch = (x, z)
                break
        if winch:
            break
    if not winch:
        return None
    wx, wz = winch
    home = (wx + (mx - wx) * 0.45, wz + (mz - wz) * 0.45)
    found = None
    for far in (12.0, 18.0, 25.0):
        for turn in (0.0, 0.7, -0.7, 1.4, -1.4):
            # away from the balcony, swung either way, so a river behind the rim does not stop it
            ax = dx * (1 - abs(turn) * 0.3) - dz * turn
            az = dz * (1 - abs(turn) * 0.3) + dx * turn
            goal = (wx + ax * far, wz + az * far)
            call("chasm_walker", {"op": "clear"})
            call("chasm_walker", {"op": "spawn", "home_x": home[0], "home_z": home[1], "goal_x": goal[0], "goal_z": goal[1]})
            k = call("chasm_walker", {"op": "list"})["walkers"]
            if k and not k[0]["stuck"]:
                found = goal
                break
        if found:
            break
    call("chasm_paint", {"op": "build_erase", "x": wx, "z": wz})
    call("chasm_walker", {"op": "clear"})
    return (winch, home, found) if found else None


def zones_of(save_name):
    """What a save holds that a replay must reproduce: every building (id, kind, plots with storeys or
    cells, crop), ground and roads, which walkers there are, and the world."""
    call("chasm_save", {"name": save_name})
    with open("apps/chasm/saves/%s.json" % save_name) as f:
        s = json.load(f)
    walkers = sorted((k["home"], k["goal"]) for k in s.get("walkers", []))
    buildings = sorted((b["id"], b["kind"], tuple(sorted(map(tuple, b.get("plots", [])))),
                        tuple(sorted(b.get("cells", []))), b.get("crop", "")) for b in s["buildings"])
    day = s.get("calendar", 0) // 2250     # CALENDAR_DAY_TICKS: the day, not the tick - the save is late by a few
    return (buildings, sorted(map(tuple, s.get("grounds", []))), walkers, s["world_hash"], day)


def main():
    call("sim_pause", {"paused": False})
    winch = find_winch()
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
            paint("ground_paint", float(gx), float(gz), ground="garden")
    call("chasm_road", {"x0": -96 + DX, "z0": 10, "x1": -60 + DX, "z1": 10})
    lane = call("chasm_road", {"x0": -67 + DX, "z0": 10, "x1": -67 + DX, "z1": 3})
    call("chasm_walker", {"op": "spawn", "home_x": -95 + DX, "home_z": 4, "goal_x": -62 + DX, "goal_z": 14})
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
    call("chasm_road", {"x0": -78 + DX, "z0": 10, "x1": -70 + DX, "z1": 28})
    call("chasm_walker", {"op": "spawn", "home_x": -70 + DX, "home_z": 27, "goal_x": -94 + DX, "goal_z": 12})
    call("chasm_walker", {"op": "spawn", "home_x": -66 + DX, "home_z": 2, "goal_x": -90 + DX, "goal_z": 10})
    time.sleep(0.5)
    call("chasm_road", {"x0": -66 + DX, "z0": 10, "x1": -62 + DX, "z1": 14})
    # buildings: a store in one stroke, split in two by an erase in its middle; a woodcutter; the first
    # house extended by a stroke that starts on it; the second field row's crop changed
    for i in range(7):
        paint("build_paint", -94 + i * 1.6, 15.5, kind="store", stroke=1)
        time.sleep(0.02)
    paint("build_erase", -94 + 3 * 1.6, 15.5, kind="store")
    for i in range(3):
        paint("build_paint", -58 + i * 1.6, 20.0, kind="woodcutter", stroke=2)
    for i in range(2):
        paint("build_paint", -58 + (i + 3) * 1.6, 20.0, kind="store", stroke=4)
    econ = call("chasm_economy", {"op": "store_allow", "x": -94 + DX, "z": 15.5, "goods": ["wheat", "greens", "beans"]})
    if econ.get("refusal"):
        REFUSED["store_allow: " + econ["refusal"]] = REFUSED.get("store_allow: " + econ["refusal"], 0) + 1
    paint("build_paint", -90.0, 20.0, kind="house", stroke=3)
    paint("build_paint", -90.0, 22.0, kind="house", stroke=3)
    paint("field_crop", -84.0, 40.0, crop="beans")
    # the date: autumn's third day, a quarter through it (gameplay_plan.md P1)
    call("chasm_time", {"set_day": 22, "day_fraction": 0.25})
    # the winch, and a walker who lives on the balcony below it and works on the plateau
    rider = None
    if winch:
        (wx, wz), home, goal = winch
        call("chasm_paint", {"op": "build_paint", "kind": "winch", "x": wx, "z": wz})
        call("chasm_walker", {"op": "spawn", "home_x": home[0], "home_z": home[1], "goal_x": goal[0], "goal_z": goal[1]})
        rider = call("chasm_walker", {"op": "list"})["walkers"][-1]
    time.sleep(0.3)
    # people (P4): beside the settlers' camp, a two-storey house and two of one, and a woodcutter's hut
    camp = call("chasm_economy", {}).get("camp")
    if camp:
        cx, cz = camp["x"], camp["z"]
        for i, (dx, storeys) in enumerate([(-4.0, 2), (-1.5, 1), (1.0, 1)]):
            for _ in range(storeys):
                call("chasm_paint", {"op": "build_add", "kind": "house", "x": cx + dx, "z": cz + 9.0, "stroke": 40 + i})
        for i in range(2):
            call("chasm_paint", {"op": "build_paint", "kind": "woodcutter", "x": cx - 10.0 + i * 1.6, "z": cz + 9.0, "stroke": 45})
        # construction (construction_plan.md): a PLAY house of two storeys, a site the idle settlers carry
        # the camp's wood to - the first spot beside the camp that takes it
        for dx, dz in [(5.0, 9.0), (0.0, -9.0), (9.0, 0.0), (-9.0, 0.0), (5.0, -9.0)]:
            r = call("chasm_paint", {"op": "build_add", "kind": "house", "x": cx + dx, "z": cz + dz, "stroke": 47, "play": True})
            if not r.get("refusal"):
                call("chasm_paint", {"op": "build_add", "kind": "house", "x": cx + dx, "z": cz + dz, "stroke": 47, "play": True})
                SITE.append((cx + dx, cz + dz))
                break
    time.sleep(3.0)     # long enough for the woodcutter to walk out and start on a tree
    status = call("input_record", {"action": "stop"})
    recording = status.get("last_recording") or status.get("last_file") or ""
    original = zones_of("replay_test_original")

    # 3. junk on top
    for i in range(10):
        paint("house_add", -60 + i * 1.9, -40.0)
    call("chasm_walker", {"op": "spawn", "home_x": -60 + DX, "home_z": -44, "goal_x": -40 + DX, "goal_z": -44})

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
            print("  buildings original %d, replay %d" % (len(original[0]), len(zones[0])))
            for a, b in zip(original[0], zones[0]):
                if a != b:
                    print("    first differing building: %s / %s" % (a, b))
                    break
            print("  ground original %d, replay %d" % (len(original[1]), len(zones[1])))
            print("  walkers original %s, replay %s" % (original[2], zones[2]))
            print("  day original %s, replay %s" % (original[4], zones[4]))
    t1 = results[0][1]
    t2 = results[1][1]
    if json.dumps(t1, sort_keys=True) != json.dumps(t2, sort_keys=True):
        ok = False
        print("the two replays' traces differ")
    ticks = t1.get("ticks") if isinstance(t1, dict) else None
    kinds = {}
    for b in original[0]:
        kinds[b[1]] = kinds.get(b[1], 0) + 1
    print("original: buildings %s, %d ground plots, %d walkers" %
          (", ".join("%d %s" % (n, k) for k, n in sorted(kinds.items())), len(original[1]), len(original[2])))
    print("recording %s, %s traced ticks" % (recording or "(last)", ticks or "?"))
    e = call("chasm_economy", {})
    print("economy at the end: %d people %s, %d felled, stores %s" % (
        len(e["workers"]), e["people"], e["felled"],
        [(st["id"], st["takes"], st["stock"]) for st in e["stores"]]))
    if e["people"]["housed"] == 0 or e["people"]["working"] == 0:
        ok = False
        print("nobody moved in or went to work by the camp")
    # The site, carried for and built by the idle - after the replays, run on at full speed until it stands.
    if not SITE:
        ok = False
        print("no construction site could be placed by the camp")
    else:
        sx, sz = SITE[0]
        call("chasm_time", {"speed": 3})
        seen, built, carriers = None, False, set()
        for _ in range(90):
            e2 = call("chasm_economy", {})
            carriers |= {k["name"] for k in e2["workers"] if k.get("site")}
            if e2["sites"]:
                seen = e2["sites"][0]
            elif seen:
                built = True
                break
            time.sleep(1.0)
        call("chasm_time", {"speed": 1})
        if not built:
            ok = False
            print("the construction site at (%.1f, %.1f) did not get built: last seen %s" % (sx, sz, seen))
        else:
            print("the construction site at (%.1f, %.1f) stands, %d wood carried by %s; stored wood now %s" %
                  (sx, sz, seen["wood_needed"], ", ".join(sorted(carriers)), e2["stored"]["wood"]))
    if original[4] != 22:
        ok = False
        print("the recorded date jump did not hold: the original ends on day %s, not 22" % original[4])
    print("refusals while painting (the junk and the replays included): %s" % (REFUSED or "none"))
    if not lane.get("gates"):
        ok = False
        print("the lane into the garden made no gate: %s" % lane)
    if not winch or not rider or rider["stuck"]:
        ok = False
        print("no winch ride: winch %s, walker %s" % (winch, rider))
    else:
        print("winch at (%.1f, %.1f); the balcony walker plans %d plots, riding it" %
              (winch[0][0], winch[0][1], rider["plots_to_go"]))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
