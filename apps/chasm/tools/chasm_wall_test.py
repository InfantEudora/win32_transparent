"""
Chasm's wall test (zombies_plan.md, "Step 4: walls break"): each case built on a flat test map (test_maps_plan.md)
from nothing - a palisade ring, someone inside it, zombies outside - and run paused, tick by tick, so a case is the
same every time. Step 4b, walls that break:

    ring        a closed ring of palisade: the zombies break a plot of it - at the rate their number says - and come in;
                the plot is kept as broken until it is built again (step 4d)
    gate_shut   the ring with a road through it, its gate shut: they break the GATE, the weak point, and not the wall,
                twice as fast; the broken door will not shut again
    gate_open   ...its gate open: they walk in through it and break nothing - the control
    round       a short stretch of palisade between them and him: walked round, nothing broken
    house       the ring with a house in its line where the gate would be, someone living in it: the house BREACHED, far
                sooner than a wall, walked through - and whoever is indoors there can be got at
    store       ...a store there instead: every building's door is breached the same way (the user's, 2026-10-19)
    fence       someone in a house's garden, which nobody else can walk into: the zombie CLIMBS its wall, which takes
                no harm
    through     zombies breaking the wall right in front of a man standing behind it: nobody fights through the
                palisade while it stands (and how near they came is reported)

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
TPS = 50                            #CALENDAR_TICKS_PER_SECOND
FIGHT_REACH = 1.0                   #Economy.h: body to body, as far as a bite and a blow reach
WALL_S = 60.0                       #ZOMBIE_BREAK_WALL
GATE_S = 30.0                       #ZOMBIE_BREAK_GATE
DOOR_S = 10.0                       #ZOMBIE_BREACH_DOOR

#The ring: a rectangle of palisade, its corners. Inside is well within it, past the wall plots' own width.
RING = [(-14.0, -10.0), (14.0, -10.0), (14.0, 10.0), (-14.0, 10.0)]
INSIDE = (12.0, 8.0)                #|x| and |z| below these: inside the ring
CENTRE = (0.0, 0.0)
OUTSIDE = (-30.0, 0.0)
ROAD = [(-30.0, 0.0), (0.0, 0.0)]   #through the west wall: its gate at about (-14, 0)
GATE = (-14.0, 0.0)
HOUSE_GAP = [(-14.0, 0.0), (-13.8, -1.1), (-13.5, 1.25)]   #the west wall's plots there, on this map (`house`)
HOUSE_AT = (40.0, 0.0)              #a house in the open, for its garden (`fence`)


def read_only(name, args):
    """A question, not a command: safe to ask twice."""
    op = (args or {}).get("op")
    return (name == "chasm_economy" and op in (None, "status", "buildings")) or (name == "chasm_zombie" and op in (None, "status")) \
        or name in ("chasm_pick", "chasm_time", "chasm_grid")


def call(name, args=None):
    req = urllib.request.Request("http://127.0.0.1:%d/mcp" % PORT,
                                 data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                                  "params": {"name": name, "arguments": args or {}}}).encode(),
                                 headers={"Content-Type": "application/json"})
    #Now and then one request of the thousands a run makes is never answered, while the app goes on answering the next
    #(seen 2026-10-18/19, with the game idle). A question is asked again; a command waits it out, since it may have run.
    for attempt in range(3):
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=30 if read_only(name, args) else 300).read())
            break
        except OSError:
            if attempt == 2 or not read_only(name, args):
                raise
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


def zombie_status():
    return call("chasm_zombie", {"op": "status"})


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


def breaks():
    """The walls-break events so far: (tick, kind, plot, building)."""
    return [(ev["tick"], ev["kind"], ev.get("plot"), ev.get("building")) for ev in economy()["events"]["last"]
            if ev["kind"] in ("wall_broken", "gate_broken", "breached")]


def fresh(road=False, house_in_ring=False, kind="house"):
    """A flat map with a closed ring of palisade - with `road`, a road through its west wall and so a gate; with
    `house_in_ring`, a building of `kind` standing in the west wall's line where that gate would be. Returns its id."""
    call("chasm_generate", {"kind": "flat", "target_fine_cells": 4000, "seed": 5})
    call("sim_pause", {"paused": True})
    for i in range(4):
        a, b = RING[i], RING[(i + 1) % 4]
        r = call("chasm_road", {"ground": "wall", "x0": a[0], "z0": a[1], "x1": b[0], "z1": b[1]})
        if r.get("refused"):
            raise Setup("the ring's wall %s-%s refused: %s" % (a, b, r["refused"]))
    house = None
    if road:
        r = call("chasm_road", {"ground": "road", "x0": ROAD[0][0], "z0": ROAD[0][1], "x1": ROAD[1][0], "z1": ROAD[1][1]})
        if r.get("refused"):
            raise Setup("the road refused: %s" % r["refused"])
    if house_in_ring:
        #The west wall's plots at the gate's place taken out - a gap a villager can walk through, so the house that
        #fills it is the ring's, not a bulge beside it (a plot whose neighbours in the line meet without it) - and one
        #house painted over them all, a stroke.
        gap = []
        for at in HOUSE_GAP:
            p = call("chasm_pick", {"x": at[0], "z": at[1]})
            if p.get("zone_ground") == "wall":
                call("chasm_paint", {"op": "ground_erase", "ground": "wall", "x": at[0], "z": at[1]})
                step(2)
                gap.append(at)
        if way(OUTSIDE, CENTRE) is None:
            raise Setup("taking the wall out at %s left no gap" % (gap,))
        for at in gap:
            r = call("chasm_paint", {"op": "build_paint", "kind": kind, "x": at[0], "z": at[1], "stroke": 77})
            if r.get("refusal"):
                raise Setup("the house in the ring refused at %s: %s" % (at, r["refusal"]))
            house = (r.get("building_here") or {}).get("id")
            step(2)
    step(5)
    shut = way(OUTSIDE, CENTRE) is None
    if not road and not shut:
        raise Setup("the ring is not closed to walkers")
    if road and shut:
        raise Setup("the road's gate does not open the ring")
    return house


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


def watch(who, ticks, every=10, until=None):
    """Steps up to `ticks`, looking every `every`: when a zombie first got inside, when `who` first fought or was
    bitten, the nearest any zombie's body came to his, and the harm as it went - [(tick, what, id, harm_s, hitting)]
    - and the most zombies hitting one thing at once. Stops early once `until(seen)` holds."""
    seen = {"inside": None, "fought": None, "nearest": 1e9, "harm": [], "most_hitting": 0, "t": 0}
    t = 0
    while t <= ticks:
        zs = zombie_status()
        w = worker(who) if who else None
        hitting = {}
        for m in zs["zombies"]:
            if inside(m["x"], m["z"]) and seen["inside"] is None:
                seen["inside"] = t
            h = m.get("hit")
            if h and h.get("hitting"):
                key = (h["what"], h["id"])
                hitting[key] = hitting.get(key, 0) + 1
            if w is not None:
                bx, bz = w["x"] + w.get("off", [0, 0])[0], w["z"] + w.get("off", [0, 0])[1]
                mx, mz = m["x"] + m.get("off", [0, 0])[0], m["z"] + m.get("off", [0, 0])[1]
                seen["nearest"] = min(seen["nearest"], math.hypot(bx - mx, bz - mz))
        seen["most_hitting"] = max([seen["most_hitting"]] + list(hitting.values()))
        for h in zs.get("harm", []):
            seen["harm"].append((t, h["what"], h["id"], h["harm_s"], hitting.get((h["what"], h["id"]), 0)))
        if who and seen["fought"] is None:
            if w is None:
                seen["fought"] = t
            elif w.get("fighting") or any(m.get("target") == who and m.get("next_bite_ticks") for m in zs["zombies"]):
                seen["fought"] = t
        seen["t"] = t
        if until and until(seen):
            break
        step(every)
        t += every
    return seen


def rate(seen, what):
    """Harm's seconds gained a second on `what`, over the stretch where the same number hit it - the hitters."""
    rows = [r for r in seen["harm"] if r[1] == what]
    best = None
    for a, b in zip(rows, rows[1:]):
        if a[4] > 0 and a[4] == b[4] and b[0] > a[0] and b[3] > a[3]:
            best = ((b[3] - a[3]) / ((b[0] - a[0]) / TPS), a[4])
    return best


#--- The cases -------------------------------------------------------------------------------------------------------

def case_ring():
    fresh()
    who = person(CENTRE)
    drop(OUTSIDE, 3)
    s = watch(who, 4000, until=lambda s: s["inside"] is not None and s["fought"] is not None)
    b = breaks()
    r = rate(s, "wall")
    kinds = sorted(set(k for _, k, _, _ in b))
    #Broken, the plot keeps its harm at its strength while it is a wall site (step 4d: drawn as what is left of it);
    #built again - a debug paint over a site builds it - it is whole.
    kept = whole = None
    broken = [ev for ev in economy()["events"]["last"] if ev["kind"] == "wall_broken"]
    if broken:
        plot = broken[0]["plot"]
        marks = lambda: [h for h in zombie_status().get("harm", []) if h["what"] == "wall" and h["id"] == plot]
        kept = any(h.get("broken") for h in marks())
        x, z = broken[0]["x"], broken[0]["z"]
        call("chasm_road", {"ground": "wall", "x0": x, "z0": z, "x1": x, "z1": z})
        step(5)
        p = call("chasm_pick", {"x": x, "z": z})
        whole = p.get("zone_ground") == "wall" and p.get("zone_ground_built") is True and not marks()
    ok = kinds == ["wall_broken"] and s["inside"] is not None and r is not None and abs(r[0] - r[1]) < 0.05 \
        and kept and whole
    return ok, "broke: %s; hitting a wall %s at a time, its harm rising %s s a second; inside at tick %s, fought at %s; " \
               "kept as broken: %s; built again, whole: %s" % (
        b, s["most_hitting"], "%.2f" % r[0] if r else "?", s["inside"], s["fought"], kept, whole)


def case_gate_shut():
    fresh(road=True)
    r = call("chasm_paint", {"op": "gate_shut", "x": GATE[0], "z": GATE[1], "on": True})
    if r.get("gate_here") != "shut":
        raise Setup("no gate shut at %s: %s" % (GATE, r.get("gate_here")))
    step(2)
    if way(OUTSIDE, CENTRE) is not None:
        raise Setup("the shut gate still lets a walker through")
    who = person(CENTRE)
    drop(OUTSIDE, 3)
    s = watch(who, 3000, until=lambda s: s["inside"] is not None)
    b = breaks()
    r = rate(s, "gate")
    #The broken door will not shut again until it is repaired.
    again = call("chasm_paint", {"op": "gate_shut", "x": GATE[0], "z": GATE[1], "on": True})
    step(2)
    refused = again.get("refusal") or ""
    kinds = sorted(set(k for _, k, _, _ in b))
    ok = kinds == ["gate_broken"] and r is not None and abs(r[0] - r[1]) < 0.05 and s["inside"] is not None \
         and "broken" in refused
    return ok, "broke: %s; its harm rising %s s a second with %s at it; inside at tick %s; shut again: %s" % (
        b, "%.2f" % r[0] if r else "?", r[1] if r else "?", s["inside"], refused or again.get("gate_here"))


def case_gate_open():
    fresh(road=True)
    who = person(CENTRE)
    drop(OUTSIDE, 1)
    s = watch(who, 2500, until=lambda s: s["fought"] is not None)
    b = breaks()
    ok = s["inside"] is not None and s["fought"] is not None and not b and not any(r for r in s["harm"])
    return ok, "came in at tick %s, fought at %s; broke: %s" % (s["inside"], s["fought"], b or "nothing")


def case_round():
    call("chasm_generate", {"kind": "flat", "target_fine_cells": 4000, "seed": 5})
    call("sim_pause", {"paused": True})
    r = call("chasm_road", {"ground": "wall", "x0": -14.0, "z0": -4.0, "x1": -14.0, "z1": 4.0})
    if r.get("refused"):
        raise Setup("the wall refused: %s" % r["refused"])
    step(5)
    who = person(CENTRE)
    drop(OUTSIDE, 1)
    s = watch(who, 3000, until=lambda s: s["fought"] is not None)
    b = breaks()
    ok = s["fought"] is not None and not b and not s["harm"]
    return ok, "a wall of 8 between them: fought at tick %s; broke: %s" % (s["fought"], b or "nothing")


def case_house():
    house = fresh(house_in_ring=True)
    who = person(CENTRE)
    #Housed by the rules in the only house there is, he would go indoors: posted where he stands, he stays out.
    r = call("chasm_economy", {"op": "order", "person": who, "x": CENTRE[0], "z": CENTRE[1]})
    if r.get("refusal"):
        raise Setup("posting him refused: " + r["refusal"])
    #Someone living in the house in the ring, at home and indoors - out of the zombies' reach until it is breached. Put
    #down beside it he walks in at its door, which may face either way: inside the ring first, else outside.
    resident = None
    for spot in ((-11.5, 0.0), (-17.0, 3.0)):
        resident = person(spot, house=house)
        step(600)
        if worker(resident).get("indoors"):
            break
        call("chasm_economy", {"op": "set_needs", "person": resident, "health_days": 0, "water_days": 0})
        step(5)
        resident = None
    if resident is None:
        raise Setup("nobody put beside the house got indoors")
    drop(OUTSIDE, 3)
    s = watch(who, 3000, until=lambda s: any(k == "breached" for _, k, _, _ in breaks()))
    b = breaks()
    r = rate(s, "door")
    #...and then, its door down, the one indoors is someone a zombie can go for.
    after = []
    for _ in range(20):
        step(25)
        zs = zombie_status()["zombies"]
        after = [m.get("target") for m in zs]
        if resident in after:
            break
    kinds = sorted(set(k for _, k, _, _ in b))
    ok = kinds == ["breached"] and r is not None and abs(r[0] - r[1]) < 0.05 and resident in after
    return ok, "broke: %s; its harm rising %s s a second with %s at it (%.1f s to breach); zombies after %s (the resident %d)" % (
        b, "%.2f" % r[0] if r else "?", r[1] if r else "?", DOOR_S / r[1] if r else 0, after, resident)


def case_store():
    store = fresh(house_in_ring=True, kind="store")
    who = person(CENTRE)
    drop(OUTSIDE, 3)
    s = watch(who, 3000, until=lambda s: s["inside"] is not None)
    b = breaks()
    r = rate(s, "door")
    kinds = sorted(set(k for _, k, _, _ in b))
    ok = kinds == ["breached"] and b[0][3] == store and r is not None and abs(r[0] - r[1]) < 0.05 and s["inside"] is not None
    return ok, "broke: %s (the store %s); its harm rising %s s a second with %s at it; inside at tick %s" % (
        b, store, "%.2f" % r[0] if r else "?", r[1] if r else "?", s["inside"])


def case_fence():
    call("chasm_generate", {"kind": "flat", "target_fine_cells": 4000, "seed": 5})
    call("sim_pause", {"paused": True})
    r = call("chasm_paint", {"op": "build_add", "kind": "house", "x": HOUSE_AT[0], "z": HOUSE_AT[1]})
    if r.get("refusal"):
        raise Setup("the house refused: " + r["refusal"])
    house = (r.get("building_here") or {}).get("id")
    step(2)
    hp = call("chasm_pick", {"x": HOUSE_AT[0], "z": HOUSE_AT[1]})["pick"]["plot"]
    gardens = []
    for k in range(8):
        a = k * math.pi / 4
        at = (hp["x"] + 2.2 * math.cos(a), hp["z"] + 2.2 * math.sin(a))
        r = call("chasm_paint", {"op": "ground_paint", "ground": "garden", "x": at[0], "z": at[1], "owner": house})
        step(2)
        if not r.get("refusal"):
            gardens.append(at)
    #In the garden's plot furthest east, someone nobody else could walk to.
    gardens.sort(key=lambda g: -g[0])
    if not gardens:
        raise Setup("no garden would paint round the house")
    spot = gardens[0]
    if way((HOUSE_AT[0] + 15.0, HOUSE_AT[1]), spot) is not None:
        raise Setup("the garden at %s is open to walkers" % (spot,))
    who = person(spot)
    drop((HOUSE_AT[0] + 15.0, HOUSE_AT[1]), 1)
    s = watch(who, 3000, until=lambda s: s["fought"] is not None)
    b = breaks()
    ok = s["fought"] is not None and not b and not s["harm"]
    return ok, "in the garden at (%.1f, %.1f): fought at tick %s; broke: %s" % (spot[0], spot[1], s["fought"],
                                                                                   b or "nothing")


def case_through():
    """A man stands just inside the west wall; the zombies come straight at him and break the plot in front of him."""
    fresh()
    still = person((-11.5, 0.0))
    drop(OUTSIDE, 3)
    s = watch(still, 3000, 5, until=lambda s: bool(breaks()))
    b = breaks()
    w = worker(still)
    ok = bool(b) and s["fought"] is None and s["nearest"] > FIGHT_REACH and w is not None
    return ok, "until the wall broke (%s): nearest body %.2f (reach %.1f); fought: %s" % (
        b, s["nearest"], FIGHT_REACH, s["fought"] if s["fought"] is not None else "never")


CASES = [("ring", case_ring), ("gate_shut", case_gate_shut), ("gate_open", case_gate_open), ("round", case_round),
         ("house", case_house), ("store", case_store), ("fence", case_fence), ("through", case_through)]


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
