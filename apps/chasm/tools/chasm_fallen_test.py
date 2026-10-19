"""
Chasm's fallen test (zombies_plan.md, "Step 6: the fallen"): each case built on a flat test map (test_maps_plan.md)
from nothing - the guard test's house, store and tower (chasm_guard_test.py), the people it needs - and run paused,
tick by tick, so a case is the same every time. What it checks is the user's (2026-10-19): the dead lie where they
fell, by the blow that killed them, a zombie for days and a person for seconds.

    front       a man fights a zombie off: struck from the front by his blows, it falls BACKWARD, facing him - and lies
                two days still, sinks over the third, and is gone
    behind      a zombie walking away from a guard's tower toward someone far off, shot in the back: it falls FORWARD
    bitten      a man with a few hours of health left bitten to death: he falls by the bite against the way he faced,
                then sinks, his angel rises, and he is gone
    starved     a man out of doors dying of hunger and thirst: no blow, he falls forward
    indoors     ...one dying at home, indoors: no body, his angel from his house
    saved       a fallen zombie saved and loaded again: lying as it was, as long as it had left

Needs a DEBUG chasm with its MCP server on the given port. It REPLACES whatever map is loaded, so run a copy, not
the game someone is playing:

    cp build/chasm_nophysics.exe build/chasm_test.exe
    ./build/chasm_test.exe --minimized --mcp-port 8775 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_fallen_test.py --port 8775 [case ...]

Prints PASS or FAIL per case with what was seen; exit code 0 when every case passed.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chasm_guard_test as g

TPS = 50                            #CALENDAR_TICKS_PER_SECOND
DAY = g.DAY
ZOMBIE_DAYS = 3                     #FALLEN_ZOMBIE_DAYS
ZOMBIE_SINK_DAYS = 1                #FALLEN_ZOMBIE_SINK_DAYS
PERSON_LIE_S = 6.0                  #FALLEN_PERSON_LIE_SECONDS
PERSON_SINK_S = 3.0                 #FALLEN_PERSON_SINK_SECONDS
PERSON_ANGEL_S = 6.0                #FALLEN_PERSON_ANGEL_SECONDS

FIGHTER = {"skills": {"precision": 5, "strength": 8, "ingenuity": 6, "husbandry": 5}}
OPEN = (20.0, 10.0)                 #open ground east of the house
PAST_TOWER = (-20.0, 0.0)           #just west of the guard test's tower, at (-16, 0)
FAR_WEST = (-55.0, 0.0)             #...and someone well off to the west of it


def zombie_status():
    return g.call("chasm_zombie", {"op": "status"})


def fallen(zs=None):
    return (zs or zombie_status()).get("fallen", [])


def posted(at, **who):
    """Someone put down at `at` and ordered to stand there, out of the staffing."""
    r = g.call("chasm_economy", dict(op="add_person", x=at[0], z=at[1], post=True, **who))
    if not r.get("made"):
        raise g.Setup("add_person at %s refused: %s" % (at, r.get("refusal")))
    g.step(2)
    return r["made"][0]


def drop(at, count=1):
    r = g.call("chasm_zombie", {"op": "spawn", "count": count, "x": at[0], "z": at[1]})
    if r.get("refusal"):
        raise g.Setup("zombies at %s refused: %s" % (at, r["refusal"]))
    g.step(2)


def wait_fallen(pred, most, every=10):
    """Steps until a fallen record holds pred; returns (it, the status then) or (None, status)."""
    done = 0
    zs = zombie_status()
    while done <= most:
        for f in fallen(zs):
            if pred(f):
                return f, zs
        g.step(every)
        done += every
        zs = zombie_status()
    return None, zs


def find(id, person):
    for f in fallen():
        if f["id"] == id and f["person"] == person:
            return f
    return None


def lifetime_zombie(f):
    """A fallen zombie's days: still until two are up, sinking through the third, then gone. Steps the clock."""
    age = int(round(f["age_s"] * TPS))
    lie_end = (ZOMBIE_DAYS - ZOMBIE_SINK_DAYS) * DAY
    g.step(lie_end - 50 - age)
    a = find(f["id"], False)
    g.step(50 + DAY // 2)
    b = find(f["id"], False)
    g.step(DAY // 2 + 60)
    c = find(f["id"], False)
    ok = a is not None and a["sinking"] == 0 and b is not None and 0.4 < b["sinking"] < 0.6 and c is None
    return ok, "still at %.1f days (sinking %s), half sunk at %.1f (sinking %s), %s at %.1f" % (
        (lie_end - 50) / DAY, a and a["sinking"], (lie_end + DAY // 2) / DAY, b and b["sinking"],
        "gone" if c is None else "still there", (lie_end + DAY + 60) / DAY)


def lifetime_person(f):
    """A fallen person's seconds: lying, sinking, his angel rising, then gone."""
    age = f["age_s"]
    g.step(int((PERSON_LIE_S - age) * TPS) - 10)
    a = find(f["id"], True)
    g.step(10 + int(PERSON_SINK_S * TPS * 0.5))
    b = find(f["id"], True)
    g.step(int(PERSON_SINK_S * TPS * 0.5) + int(PERSON_ANGEL_S * TPS * 0.5))
    c = find(f["id"], True)
    g.step(int(PERSON_ANGEL_S * TPS * 0.5) + 10)
    d = find(f["id"], True)
    ok = (a is not None and a["sinking"] == 0 and a["rising"] < 0 and b is not None and 0.3 < b["sinking"] < 0.7
          and c is not None and c["sinking"] == 1 and 0.3 < c["rising"] < 0.7 and d is None)
    return ok, "lying (sinking %s), sinking (%s), his angel rising (%s), %s" % (
        a and a["sinking"], b and b["sinking"], c and c["rising"], "gone" if d is None else "still there")


#--- The cases -------------------------------------------------------------------------------------------------------

def case_front():
    g.fresh(False)
    man = posted(OPEN, **FIGHTER)
    drop((OPEN[0] + 10.0, OPEN[1]))
    f, zs = wait_fallen(lambda f: not f["person"], 3000)
    if f is None:
        raise g.Setup("no zombie fell: %s" % zs.get("zombies"))
    w = g.worker(man)
    if w is None:
        return False, "he died first"
    #Fallen facing him - toward his body - and backward, his blows from the front.
    to = (w["x"] - f["x"], w["z"] - f["z"])
    l = math.hypot(*to) or 1.0
    towards = (f["facing"][0] * to[0] + f["facing"][1] * to[1]) / l
    ok, life = lifetime_zombie(f)
    ok = ok and f["fell"] == "backward" and towards > 0.7
    return ok, "fell %s, facing him (%.2f), %.2f from him; %s" % (f["fell"], towards, l, life)


def case_behind():
    ids = g.fresh(False)
    guard = g.person(ids, g.ARCHER)
    ids["tower"] = g.paint("build_add", g.TOWER, kind="tower")
    if g.wait_until(g.on_tower(guard, ids["tower"]), 3000) is None:
        raise g.Setup("the guard never got onto his tower: " + g.describe(g.worker(guard)))
    far = posted(FAR_WEST, **FIGHTER)
    drop(PAST_TOWER)
    #Each zombie looks on a tick of its own: give it its first look.
    zs = zombie_status()
    for _ in range(20):
        if zs["zombies"] and zs["zombies"][0].get("target"):
            break
        g.step(10)
        zs = zombie_status()
    if not zs["zombies"] or zs["zombies"][0].get("target") != far:
        raise g.Setup("the zombie is not after the one far off: %s" % zs["zombies"])
    f, zs = wait_fallen(lambda f: not f["person"], 3000)
    if f is None:
        raise g.Setup("no zombie fell: %s" % zs.get("zombies"))
    #Facing away from the tower, the way it walked, and shot from it: forward.
    away = (f["x"] - g.TOWER[0], f["z"] - g.TOWER[1])
    l = math.hypot(*away) or 1.0
    facing_away = (f["facing"][0] * away[0] + f["facing"][1] * away[1]) / l
    w = g.worker(far)
    fought = w is None or w.get("fighting")
    ok = f["fell"] == "forward" and facing_away > 0.7 and not fought
    return ok, "fell %s, %.1f past the tower, facing away from it (%.2f); the one far off %s" % (
        f["fell"], l, facing_away, "fought" if fought else "never reached")


def case_bitten():
    g.fresh(False)
    man = posted(OPEN, **FIGHTER)
    #The zombie dropped close, and only then his health taken down to under a bite's - before he could eat it back.
    drop((OPEN[0] + 1.5, OPEN[1]))
    g.call("chasm_economy", {"op": "set_needs", "person": man, "health_days": 0.4})
    f, zs = wait_fallen(lambda f: f["person"], 3000, 5)
    if f is None:
        raise g.Setup("he never fell: %s" % g.describe(g.worker(man)))
    #By the bite: against the way he faced, from the zombie at him, backward; along it, forward.
    m = min(zs["zombies"], key=lambda m: math.hypot(m["x"] - f["x"], m["z"] - f["z"])) if zs["zombies"] else None
    if m is None:
        return False, "no zombie by him"
    struck = (f["x"] - m["x"], f["z"] - m["z"])
    want = "backward" if struck[0] * f["facing"][0] + struck[1] * f["facing"][1] < 0 else "forward"
    ok, life = lifetime_person(f)
    ok = ok and f["id"] == man and f["fell"] == want and not f["indoors"]
    return ok, "fell %s (the bite says %s); %s" % (f["fell"], want, life)


def case_starved():
    g.fresh(False)
    man = posted(OPEN, **FIGHTER)
    g.call("chasm_economy", {"op": "set_needs", "person": man, "health_days": 0, "water_days": 0})
    f, zs = wait_fallen(lambda f: f["person"], 600, 5)
    if f is None:
        raise g.Setup("he never died: %s" % g.describe(g.worker(man)))
    ok = f["id"] == man and f["fell"] == "forward" and not f["indoors"]
    return ok, "fell %s, indoors %s" % (f["fell"], f["indoors"])


def case_indoors():
    ids = g.fresh(False)
    who = g.person(ids, FIGHTER)
    if g.wait_until(lambda e: (g.worker(who, e) or {}).get("indoors"), 1500) is None:
        raise g.Setup("he never went indoors: " + g.describe(g.worker(who)))
    g.call("chasm_economy", {"op": "set_needs", "person": who, "health_days": 0, "water_days": 0})
    f, zs = wait_fallen(lambda f: f["person"], 600, 5)
    if f is None:
        raise g.Setup("he never died: " + g.describe(g.worker(who)))
    ok, life = lifetime_person(f)
    ok = ok and f["indoors"]
    return ok, "indoors %s; %s" % (f["indoors"], life)


def case_saved():
    g.fresh(False)
    posted(OPEN, **FIGHTER)
    drop((OPEN[0] + 10.0, OPEN[1]))
    f, zs = wait_fallen(lambda f: not f["person"], 3000)
    if f is None:
        raise g.Setup("no zombie fell")
    g.step(DAY // 2)
    before = fallen()
    g.call("chasm_save", {"name": "fallen_test"})
    g.call("chasm_load", {"name": "fallen_test"})
    g.call("sim_pause", {"paused": True})
    g.step(1)
    after = fallen()
    same = len(before) == len(after) and all(
        a["id"] == b["id"] and a["fell"] == b["fell"] and abs(a["x"] - b["x"]) < 1e-3 and abs(a["z"] - b["z"]) < 1e-3
        and abs(a["age_s"] - b["age_s"]) < 0.1 for a, b in zip(before, after))
    return same, "before %s; after %s" % ([(x["id"], x["fell"], x["age_s"]) for x in before],
                                         [(x["id"], x["fell"], x["age_s"]) for x in after])


CASES = [("front", case_front), ("behind", case_behind), ("bitten", case_bitten), ("starved", case_starved),
         ("indoors", case_indoors), ("saved", case_saved)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=g.PORT)
    ap.add_argument("cases", nargs="*")
    args = ap.parse_args()
    g.PORT = args.port
    failed = []
    for name, fn in CASES:
        if args.cases and name not in args.cases:
            continue
        try:
            ok, detail = fn()
        except g.Setup as s:
            ok, detail = False, "SETUP: %s" % s
        print("%s  %-10s %s" % ("PASS" if ok else "FAIL", name, detail))
        sys.stdout.flush()
        if not ok:
            failed.append(name)
    print("failed: %d %s" % (len(failed), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
