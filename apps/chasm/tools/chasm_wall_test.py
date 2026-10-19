"""
Chasm's wall test (zombies_plan.md, "Step 4: walls break"): each case built on a flat test map (test_maps_plan.md)
from nothing - a palisade ring, someone inside it, zombies outside - and run paused, tick by tick, so a case is the
same every time. Step 4a, what holds today:

    ring        a closed ring of palisade: the zombies outside never get in, and nobody inside is bitten
    gate_shut   the ring with a road through it, its gate shut: the same
    gate_open   ...its gate open: they come in through it and fight - the control, that the ring and the fight are real
    through     zombies chasing someone round the OUTSIDE of the ring, close past a man standing at its INSIDE:
                nobody fights across the palisade (and how near they came through it is reported)

Needs a DEBUG chasm with its MCP server on the given port. It REPLACES whatever map is loaded, so run a copy, not
the game someone is playing:

    cp build/chasm_nophysics.exe build/chasm_test.exe
    ./build/chasm_test.exe --minimized --mcp-port 8775 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_wall_test.py --port 8775 [case ...]

Prints PASS or FAIL per case with what was seen; exit code 0 when every case passed.
"""
import argparse
import json
import math
import sys
import urllib.request

PORT = 8769
FIGHT_REACH = 1.0                   #Economy.h: body to body, as far as a bite and a blow reach

#The ring: a rectangle of palisade, its corners. Inside is well within it, past the wall plots' own width.
RING = [(-14.0, -10.0), (14.0, -10.0), (14.0, 10.0), (-14.0, 10.0)]
INSIDE = (12.0, 8.0)                #|x| and |z| below these: inside the ring
CENTRE = (0.0, 0.0)
OUTSIDE = (-30.0, 0.0)
ROAD = [(-30.0, 0.0), (0.0, 0.0)]   #through the west wall: its gate at about (-14, 0)
GATE = (-14.0, 0.0)


def call(name, args=None):
    req = urllib.request.Request("http://127.0.0.1:%d/mcp" % PORT,
                                 data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                                  "params": {"name": name, "arguments": args or {}}}).encode(),
                                 headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=300).read())
    if "error" in r:
        raise RuntimeError("%s: %s" % (name, r["error"]))
    out = json.loads(r["result"]["content"][0]["text"])
    if isinstance(out, dict) and out.get("error"):
        raise RuntimeError("%s: %s" % (name, out["error"]))
    return out


class Setup(Exception):
    """The case could not be built as it should be - not a verdict on the rules."""


def step(ticks):
    while ticks > 0:
        n = min(ticks, 2000)
        call("sim_step", {"num_ticks": n})
        ticks -= n


def economy():
    return call("chasm_economy", {})


def zombies():
    return call("chasm_zombie", {"op": "status"}).get("zombies", [])


def worker(id, e=None):
    for w in (e or economy())["workers"]:
        if w["id"] == id:
            return w
    return None


def inside(x, z):
    return abs(x) < INSIDE[0] and abs(z) < INSIDE[1]


def way(a, b):
    """The A* route a villager would take from a to b: its plots, or None with no way."""
    r = call("chasm_pick", {"x": a[0], "z": a[1], "to_x": b[0], "to_z": b[1]})
    route = r.get("route") or {}
    return route.get("plots") if route.get("found") else None


def fresh(road):
    """A flat map with a closed ring of palisade - and with `road`, a road through its west wall and so a gate."""
    call("chasm_generate", {"kind": "flat", "target_fine_cells": 4000, "seed": 5})
    call("sim_pause", {"paused": True})
    for i in range(4):
        a, b = RING[i], RING[(i + 1) % 4]
        r = call("chasm_road", {"ground": "wall", "x0": a[0], "z0": a[1], "x1": b[0], "z1": b[1]})
        if r.get("refused"):
            raise Setup("the ring's wall %s-%s refused: %s" % (a, b, r["refused"]))
    if road:
        r = call("chasm_road", {"ground": "road", "x0": ROAD[0][0], "z0": ROAD[0][1], "x1": ROAD[1][0], "z1": ROAD[1][1]})
        if r.get("refused"):
            raise Setup("the road refused: %s" % r["refused"])
    step(5)
    shut = way(OUTSIDE, CENTRE) is None
    if not road and not shut:
        raise Setup("the ring is not closed to walkers")
    if road and shut:
        raise Setup("the road's gate does not open the ring")


def person(at, **more):
    r = call("chasm_economy", dict(op="add_person", x=at[0], z=at[1], **more))
    if not r.get("made"):
        raise Setup("add_person at %s refused: %s" % (at, r.get("refusal")))
    step(2)
    return r["made"][0]


def drop(at, count):
    r = call("chasm_zombie", {"op": "spawn", "count": count, "x": at[0], "z": at[1]})
    if r.get("refusal"):
        raise Setup("zombies at %s refused: %s" % (at, r["refusal"]))
    step(2)


def watch(who, ticks, every=10):
    """Steps `ticks`, looking every `every`: whether a zombie was ever inside, whether `who` ever fought or was bitten,
    and the nearest any zombie's body came to his."""
    seen = {"inside": None, "fought": None, "nearest": 1e9, "lost": 0, "zombies": 0}
    t = 0
    while t <= ticks:
        zs = zombies()
        w = worker(who)
        seen["zombies"] = len(zs)
        seen["lost"] = sum(1 for m in zs if m.get("lost"))
        for m in zs:
            if inside(m["x"], m["z"]) and seen["inside"] is None:
                seen["inside"] = "tick %d: zombie %d at (%.1f, %.1f)" % (t, m["id"], m["x"], m["z"])
            if w is not None:
                bx, bz = w["x"] + w.get("off", [0, 0])[0], w["z"] + w.get("off", [0, 0])[1]
                mx, mz = m["x"] + m.get("off", [0, 0])[0], m["z"] + m.get("off", [0, 0])[1]
                seen["nearest"] = min(seen["nearest"], math.hypot(bx - mx, bz - mz))
        if w is None:
            seen["fought"] = seen["fought"] or "tick %d: he is dead" % t
        elif (w.get("fighting") or any(m.get("target") == who and m.get("next_bite_ticks") for m in zs)) \
                and seen["fought"] is None:
            seen["fought"] = "tick %d: %s" % (t, "fighting" if w.get("fighting") else "being bitten")
        step(every)
        t += every
    return seen


#--- The cases -------------------------------------------------------------------------------------------------------

def shut_out(road, gate_shut):
    fresh(road)
    if gate_shut:
        r = call("chasm_paint", {"op": "gate_shut", "x": GATE[0], "z": GATE[1], "on": True})
        if r.get("gate_here") != "shut":
            raise Setup("no gate shut at %s: %s" % (GATE, r.get("gate_here")))
        step(2)
        if way(OUTSIDE, CENTRE) is not None:
            raise Setup("the shut gate still lets a walker through")
    who = person(CENTRE)
    drop(OUTSIDE, 3)
    s = watch(who, 1500)
    ok = s["inside"] is None and s["fought"] is None
    return ok, "%d zombies, %d lost; inside: %s; fought: %s; nearest %.1f" % (
        s["zombies"], s["lost"], s["inside"] or "never", s["fought"] or "never", s["nearest"])


def case_ring():
    return shut_out(False, False)


def case_gate_shut():
    return shut_out(True, True)


def case_gate_open():
    fresh(True)
    who = person(CENTRE)
    drop(OUTSIDE, 1)
    s = watch(who, 2500)
    ok = s["inside"] is not None and s["fought"] is not None
    return ok, "came in: %s; fought: %s" % (s["inside"] or "never", s["fought"] or "never")


def case_through():
    """A man stands at the ring's north wall, inside; another walks the outside of that wall, ordered from its west end
    to its east, with zombies after him - so they pass along the wall, the inside man a palisade's width away."""
    fresh(False)
    #The man on the plot nearest the wall inside it (the line steps between z 9 and 12 on this grid); the runner's way
    #along just outside it, which his walk takes hugging the wall - the closest two walkers come with a palisade between.
    still = person((0.0, 8.0))
    runner = person((-22.0, 12.5))
    r = call("chasm_economy", {"op": "order", "person": runner, "x": 22.0, "z": 12.5})
    if r.get("refusal"):
        raise Setup("the runner's order refused: " + r["refusal"])
    drop((-26.0, 12.5), 3)
    s = watch(still, 2500, 5)
    w = worker(still)
    ok = s["fought"] is None and w is not None
    return ok, "zombies past him outside: nearest body %.2f (reach %.1f); fought: %s" % (
        s["nearest"], FIGHT_REACH, s["fought"] or "never")


CASES = [("ring", case_ring), ("gate_shut", case_gate_shut), ("gate_open", case_gate_open), ("through", case_through)]


def main():
    global PORT
    ap = argparse.ArgumentParser(description=__doc__.strip().split("\n")[0])
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("cases", nargs="*", help="cases to run (default: all) - " + ", ".join(n for n, _ in CASES))
    a = ap.parse_args()
    PORT = a.port
    failed = []
    for name, fn in CASES:
        if a.cases and name not in a.cases:
            continue
        try:
            ok, detail = fn()
            verdict = "PASS" if ok else "FAIL"
        except Setup as s:
            ok, verdict, detail = False, "SETUP", str(s)
        print("%-5s %-10s %s" % (verdict, name, detail))
        sys.stdout.flush()
        if not ok:
            failed.append(name)
    call("chasm_zombie", {"op": "clear"})
    print("failed:", len(failed), failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
