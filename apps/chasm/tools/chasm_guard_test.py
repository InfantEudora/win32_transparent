"""
Chasm's guard test (zombies_plan.md, "Step 3b: guards hold their posts"): each case built on a flat test map
(test_maps_plan.md) from nothing - a house, a store, a tower, the people it needs - and run paused, tick by tick,
so a case is the same every time. What it checks is the user's (2026-10-18):

    shortage    a builders' hut painted with nobody free: the tower's automatic guard keeps his tower
    death       the range's archer dies (Lieve's case, zombies_plan.md step 3): the guard is not moved to the range
    player      the player's ways out still work: release_guard, and the tower's count lowered and raised
    wave        his tower and pack dry, his water pressing, zombies in sight: he stays and shoots, and goes for
                water once the last is down
    drink       his tower dry but his pack full, thirsty, zombies in sight: he drinks from his pack between shots,
                never goes dry, and shoots on
    pack        a guard setting out from a home with nothing in it climbs with water in his pack
    pack_errand ...and one made a guard while out at work (the range's line), who never passes his home

Needs a DEBUG chasm with its MCP server on the given port. It REPLACES whatever map is loaded, so run a copy, not
the game someone is playing:

    cp build/chasm_nophysics.exe build/chasm_test.exe
    ./build/chasm_test.exe --minimized --mcp-port 8775 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_guard_test.py --port 8775 [case ...]

Prints PASS or FAIL per case with what was seen; exit code 0 when every case passed.
"""
import argparse
import json
import sys
import urllib.request

PORT = 8769
DAY = 2250                          #CALENDAR_DAY_TICKS
PRESSING_WATER_DAYS = 4.0 / 3.0     #NEEDS_PRESSING_WATER: below this he goes for water
PACK_WATER = 3                      #NEEDS_PACK_WATER

HOUSE = (0.0, 0.0)
STORE = (8.0, 0.0)
RANGE = (0.0, 14.0)
TOWER = (-16.0, 0.0)
BUILDERS = (-6.0, -10.0)
ZOMBIES = (-28.0, 6.0)              #about 14 from the tower: in a guard's sight, out of his reach

#Two settlers for the cases with a range: the better archer the range's, the other the tower's.
ARCHER = {"skills": {"precision": 9, "strength": 5, "ingenuity": 5, "husbandry": 5}, "archery": 900000}
GUARD = {"skills": {"precision": 6, "strength": 5, "ingenuity": 5, "husbandry": 5}, "archery": 300000}


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


def worker(id, e=None):
    for w in (e or economy())["workers"]:
        if w["id"] == id:
            return w
    return None


def describe(w):
    if w is None:
        return "dead"
    post = w.get("post")
    return "%s, %s%s, water %.2f d, pack %s" % (w["job"], w["state"],
                                              (" (there)" if post and post.get("there") else ""),
                                              w["water_days"], w.get("pack", {}))


def paint(op, at, **more):
    args = dict(op=op, x=at[0], z=at[1])
    args.update(more)
    r = call("chasm_paint", args)
    if r.get("refusal"):
        raise Setup("%s at %s refused: %s" % (op, at, r["refusal"]))
    step(2)
    return (r.get("building_here") or {}).get("id")


def wait_until(pred, most, every=25):
    """Steps until pred(economy) holds, `every` ticks at a time, at most `most` ticks. Returns the economy then, or None."""
    done = 0
    while done <= most:
        e = economy()
        if pred(e):
            return e
        step(every)
        done += every
    return None


def on_tower(id, tower):
    def pred(e):
        w = worker(id, e)
        return (w is not None and w["job"] == "guard" and w.get("building") == tower and w["state"] == "on_post"
                and w.get("post", {}).get("there"))
    return pred


def fresh(with_range):
    """A flat map with a house of two storeys, a stocked store and a tower - a range before the tower, so the range's
    place comes first in the staffing's order, as in Lieve's colony."""
    call("chasm_generate", {"kind": "flat", "target_fine_cells": 4000, "seed": 5})
    call("sim_pause", {"paused": True})
    ids = {}
    ids["house"] = paint("build_add", HOUSE, kind="house")
    paint("build_add", HOUSE, kind="house")
    ids["store"] = paint("build_add", STORE, kind="store")
    call("chasm_economy", {"op": "set_stock", "building": ids["store"],
                           "contents": {"water": 60, "wheat": 40, "beans": 20}})
    if with_range:
        ids["range"] = paint("range_paint", RANGE)
    step(2)
    return ids


def person(ids, who):
    args = dict(op="add_person", x=HOUSE[0] + 3.0, z=HOUSE[1] - 4.0, house=ids["house"], **who)
    r = call("chasm_economy", args)
    if not r.get("made"):
        raise Setup("add_person refused: %s" % r.get("refusal"))
    step(2)
    return r["made"][0]


def two_settlers():
    """The range's archer and the tower's automatic guard, both up and at it."""
    ids = fresh(True)
    archer = person(ids, ARCHER)
    guard = person(ids, GUARD)
    ids["tower"] = paint("build_add", TOWER, kind="tower")
    if wait_until(on_tower(guard, ids["tower"]), 3000) is None:
        raise Setup("the guard never got onto his tower: " + describe(worker(guard)))
    if worker(archer)["job"] != "archer":
        raise Setup("the better archer is not the range's: " + describe(worker(archer)))
    return ids, archer, guard


#--- The cases -------------------------------------------------------------------------------------------------------

def case_shortage():
    ids, archer, guard = two_settlers()
    paint("build_add", BUILDERS, kind="builders")
    step(300)
    w = worker(guard)
    ok = on_tower(guard, ids["tower"])(economy())
    return ok, "after a builders' hut: guard %s; the range's archer %s" % (describe(w), describe(worker(archer)))


def case_death():
    ids, archer, guard = two_settlers()
    call("chasm_economy", {"op": "set_needs", "person": archer, "health_days": 0, "water_days": 0})
    step(300)
    e = economy()
    if worker(archer, e) is not None:
        raise Setup("the archer did not die: " + describe(worker(archer, e)))
    ok = on_tower(guard, ids["tower"])(e)
    return ok, "after the range's archer died: guard %s" % describe(worker(guard, e))


def case_player():
    ids, archer, guard = two_settlers()
    seen = []
    call("chasm_economy", {"op": "release_guard", "person": guard})
    step(300)
    w = worker(guard)
    released = w["job"] != "guard"
    seen.append("released: " + describe(w))
    call("chasm_economy", {"op": "guards", "building": ids["tower"], "count": 1})
    back = wait_until(on_tower(guard, ids["tower"]), 3000) is not None
    seen.append("count 1: " + describe(worker(guard)))
    call("chasm_economy", {"op": "guards", "building": ids["tower"], "count": 0})
    step(300)
    w = worker(guard)
    lowered = w["job"] != "guard"
    seen.append("count 0: " + describe(w))
    return released and back and lowered, "; ".join(seen)


def case_wave():
    ids = fresh(False)
    guard = person(ids, ARCHER)
    ids["tower"] = paint("build_add", TOWER, kind="tower")
    if wait_until(on_tower(guard, ids["tower"]), 3000) is None:
        raise Setup("the guard never got onto his tower: " + describe(worker(guard)))
    #Nothing left to drink up there: the tower emptied, and his pack drunk dry - a unit at a time with room for one
    #(NEEDS_WATER_WORTH, 2 days) but not yet pressing, so he has no reason to leave before the zombies come.
    call("chasm_economy", {"op": "set_stock", "building": ids["tower"],
                           "contents": {"water": 0, "provisions": 0, "wheat": 0, "beans": 0}})
    for _ in range(8):
        if worker(guard).get("pack", {}).get("water", 0) <= 0:
            break
        call("chasm_economy", {"op": "set_needs", "person": guard, "water_days": PRESSING_WATER_DAYS + 0.2})
        step(150)
    w = worker(guard)
    if w.get("pack", {}).get("water", 0) > 0 or w["state"] != "on_post":
        raise Setup("his pack would not drink dry on his tower: " + describe(w))
    r = call("chasm_zombie", {"op": "spawn", "count": 3, "x": ZOMBIES[0], "z": ZOMBIES[1]})
    if r.get("refusal"):
        raise Setup("zombies refused: " + r["refusal"])
    if wait_until(lambda e: worker(guard, e).get("shooting", {}).get("zombie"), 500, 5) is None:
        raise Setup("he never took aim at the zombies: " + describe(worker(guard)))
    #...and only then thirsty: a pressing need with zombies in his sight.
    call("chasm_economy", {"op": "set_needs", "person": guard, "water_days": 1.2})
    step(2)
    #Through the wave: on his post at every look while any zombie stands.
    left = None
    shots = 0
    ticks = 0
    while ticks < 6000:
        z = call("chasm_zombie", {"op": "status"})
        w = worker(guard)
        if w is None:
            return False, "he died in the wave"
        if w.get("shooting", {}).get("zombie"):
            shots += 1
        if not z.get("zombies"):
            break
        if w["state"] != "on_post" and left is None:
            left = "tick %d, %d zombies standing: %s" % (ticks, len(z["zombies"]), describe(w))
        step(25)
        ticks += 25
    if left:
        return False, "left his post during the wave - " + left
    if z.get("zombies"):
        return False, "the wave never ended: %d zombies after %d ticks; he is %s" % (len(z["zombies"]), ticks,
                                                                                    describe(worker(guard)))
    #...and then for water, as ever.
    went = wait_until(lambda e: worker(guard, e) is not None and worker(guard, e)["state"] != "on_post", 1500)
    after = describe(worker(guard))
    return went is not None and shots > 0, "the wave down in %d ticks, shooting in %d looks; after it: %s" % (
        ticks, shots, after)


def case_drink():
    ids = fresh(False)
    guard = person(ids, ARCHER)
    ids["tower"] = paint("build_add", TOWER, kind="tower")
    if wait_until(on_tower(guard, ids["tower"]), 3000) is None:
        raise Setup("the guard never got onto his tower: " + describe(worker(guard)))
    call("chasm_economy", {"op": "set_stock", "building": ids["tower"],
                           "contents": {"water": 0, "provisions": 0, "wheat": 0, "beans": 0}})
    packed = worker(guard).get("pack", {}).get("water", 0)
    if packed < PACK_WATER:
        raise Setup("he climbed without his water: " + describe(worker(guard)))
    r = call("chasm_zombie", {"op": "spawn", "count": 3, "x": ZOMBIES[0], "z": ZOMBIES[1]})
    if r.get("refusal"):
        raise Setup("zombies refused: " + r["refusal"])
    if wait_until(lambda e: worker(guard, e).get("shooting", {}).get("zombie"), 500, 5) is None:
        raise Setup("he never took aim at the zombies: " + describe(worker(guard)))
    #Thirsty enough for a unit to fit (NEEDS_WATER_WORTH, 2 days), with only his pack to drink from.
    call("chasm_economy", {"op": "set_needs", "person": guard, "water_days": 1.2})
    step(2)
    least = 99.0
    drank_at = None
    shots_after = 0
    left = None
    ticks = 0
    while ticks < 6000:
        z = call("chasm_zombie", {"op": "status"})
        w = worker(guard)
        if w is None:
            return False, "he died in the wave"
        if not z.get("zombies"):
            break
        least = min(least, w["water_days"])
        if drank_at is None and w.get("pack", {}).get("water", 0) < packed:
            drank_at = ticks
        if drank_at is not None and w.get("shooting", {}).get("zombie"):
            shots_after += 1
        if w["state"] != "on_post" and left is None:
            left = "tick %d: %s" % (ticks, describe(w))
        step(25)
        ticks += 25
    if left:
        return False, "left his post during the wave - " + left
    w = worker(guard)
    detail = "the wave down in %d ticks; drank from his pack at tick %s, shooting in %d looks after; water never below " \
             "%.2f d; now %s" % (ticks, drank_at, shots_after, least, describe(w))
    return drank_at is not None and least > 0.0 and shots_after > 0, detail


def case_pack():
    ids = fresh(False)
    guard = person(ids, GUARD)
    step(600)
    #Home has nothing: what he takes, he takes from the store.
    call("chasm_economy", {"op": "set_stock", "building": ids["house"],
                           "contents": {"water": 0, "wheat": 0, "beans": 0, "greens": 0, "fruit": 0}})
    ids["tower"] = paint("build_add", TOWER, kind="tower")
    if wait_until(on_tower(guard, ids["tower"]), 4000) is None:
        return False, "never got onto his tower: " + describe(worker(guard))
    w = worker(guard)
    return w.get("pack", {}).get("water", 0) >= PACK_WATER, "on his tower: " + describe(w)


def case_pack_errand():
    ids = fresh(True)
    guard = person(ids, GUARD)
    if wait_until(lambda e: worker(guard, e)["state"] == "training", 3000) is None:
        raise Setup("he never got to the range's line: " + describe(worker(guard)))
    call("chasm_economy", {"op": "set_stock", "building": ids["house"],
                           "contents": {"water": 0, "wheat": 0, "beans": 0, "greens": 0, "fruit": 0}})
    #The range stopped and a tower put up on the same tick: he is its guard from the line.
    call("chasm_paint", {"op": "range_stop", "x": RANGE[0], "z": RANGE[1]})
    ids["tower"] = paint("build_add", TOWER, kind="tower")
    if wait_until(on_tower(guard, ids["tower"]), 4000) is None:
        return False, "never got onto his tower: " + describe(worker(guard))
    w = worker(guard)
    return w.get("pack", {}).get("water", 0) >= PACK_WATER, "on his tower: " + describe(w)


CASES = [("shortage", case_shortage), ("death", case_death), ("player", case_player), ("wave", case_wave),
         ("drink", case_drink), ("pack", case_pack), ("pack_errand", case_pack_errand)]


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
        print("%-5s %-12s %s" % (verdict, name, detail))
        sys.stdout.flush()
        if not ok:
            failed.append(name)
    call("chasm_zombie", {"op": "clear"})
    print("failed:", len(failed), failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
