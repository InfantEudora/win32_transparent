"""
Chasm's old-age test (lifeline_plan.md, step 4): every case but `guard` starts from the stored save, saves/old_age_test.json -
village_year5 with two old people put in and two birth ticks moved, so what takes hours of play comes within days -
and runs paused, a tick or a few at a time, so a case is the same every time. What it checks is the user's
(2026-10-21):

    retire      Jelle De Wit, a woodcutter, turns 36 a day and a half in: his job "old" and his place at the hut
                filled by someone else on that tick, told in the journal, and home with no more work
    errands     Evi Wever, old: her house runs dry and she is the one who fetches; she walks near home now and then,
                and does no job's work
    death       Wessel Veen's day comes 3 days in: he dies of old age the first time he is home - at home on the last
                tick he was seen, fallen indoors (no body, his angel) - and the journal and the dead's list say so
    home_first  someone put down past his day, away from home: alive the whole walk home, fallen indoors at its end
    orders      the old are refused an order ("too old"); someone posted who turns old is released and goes home
    guard       a tower's guard turns old (on the guard test's flat map, not the save): down and home, and the spare
                goes up in his place
    saved      Jelle, retired, saved and loaded: the old's jobs, and their days, the same
    replay      Jelle growing old and Wessel dying, recorded and replayed twice: the traces equal on every tick

Needs a DEBUG chasm with its MCP server on the given port. It REPLACES whatever map is loaded, so run a copy, not
the game someone is playing:

    cp build/chasm_nophysics.exe build/chasm_test.exe
    ./build/chasm_test.exe --minimized --mcp-port 8775 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_old_age_test.py --port 8775 [case ...]

Prints PASS or FAIL per case with what was seen; exit code 0 when every case passed.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chasm_guard_test as g

DAY = g.DAY                         #CALENDAR_DAY_TICKS
SAVE = "old_age_test"
SAVES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "saves")

JELLE = 1                           #a woodcutter, 36 a day and a half in
EVI = 19                            #old, her day 94 days off
WESSEL = 20                         #old, his day 3 days in
EVI_HOUSE = 19
WESSEL_HOUSE = 20
WESSEL_DOOR = (243.1, -31.5)        #where those at home in house 20 stand

#What the old never do: any job's round, a post, the builders' sites and roads.
WORK = {"to_work", "working", "to_sow", "sowing", "to_range", "training", "to_post", "on_post", "to_fetch", "fetching",
        "to_site", "building", "to_road", "clearing", "laying", "to_crate", "lifting", "to_input", "taking_input",
        "to_workshop", "making"}
ERRAND = {"to_get", "getting", "to_pantry"}
WALK = {"to_play", "playing"}


def load():
    r = g.call("chasm_load", {"name": SAVE})
    if not r.get("ok"):
        raise g.Setup("%s did not load: %s" % (SAVE, r))
    g.call("sim_pause", {"paused": True})


def journal(person, kind):
    s = g.call("chasm_stats", {"journal": -1})
    return [ev["text"] for ev in s["journal"] if ev["kind"] == kind and ev["person"] == person]


def put_down(house, near, **who):
    """Someone put down at the first open spot round `near`, of `house`'s family. Returns his id."""
    for dx, dz in ((12, 0), (-12, 0), (0, 12), (0, -12), (9, 9), (-9, -9), (9, -9), (-9, 9)):
        r = g.call("chasm_economy", dict(op="add_person", x=near[0] + dx, z=near[1] + dz, house=house, **who))
        if r.get("made"):
            return r["made"][0]
    raise g.Setup("nowhere to put anyone down round %s" % (near,))


def fallen(id):
    """His record among THE FALLEN (zombies_plan.md step 6), as he died: indoors means no body, only his angel."""
    return next((f for f in g.call("chasm_zombie", {"op": "status"}).get("fallen", []) if f["id"] == id), None)


def wait_old(id, most):
    """Steps until `id` is old, 5 ticks at a time; the economy then."""
    e = g.wait_until(lambda e: (g.worker(id, e) or {}).get("stage") == "old", most, 5)
    if e is None:
        raise g.Setup("%d never grew old" % id)
    return e


#--- The cases -------------------------------------------------------------------------------------------------------

def case_retire():
    load()
    e = g.economy()
    j = g.worker(JELLE, e)
    hut = j["building"]
    if j["job"] != "woodcutter" or not hut:
        raise g.Setup("Jelle is no woodcutter in the save: %s" % g.describe(j))

    def at_hut(e):
        return sorted(w["id"] for w in e["workers"] if w["job"] == "woodcutter" and w["building"] == hut)
    before = at_hut(e)
    e = wait_old(JELLE, 2 * DAY)
    j = g.worker(JELLE, e)
    after = at_hut(e)
    #...and home from whatever he was at, never at work on the way.
    states = set()
    home = g.wait_until(lambda e: states.add(g.worker(JELLE, e)["state"]) or (g.worker(JELLE, e)["state"] == "at_home"
                                                                               and g.worker(JELLE, e)["indoors"]), DAY, 5)
    told = journal(JELLE, "grew_old")
    detail = "age %s, job %s at %s; the hut's woodcutters %s -> %s; then %s%s; the journal %s" % (
        j["age"], j["job"], j["building"], before, after, sorted(states), "" if home else " (NEVER HOME)", told)
    return (j["job"] == "old" and not j["building"] and JELLE not in after and len(after) == len(before)
            and home is not None and not (states & WORK) and told == ["Jelle De Wit grew old"]), detail


def case_errands():
    load()
    #Her house dry: short of water, and she - home, with no job - is the one to go (FetchHome).
    for f_pantry in (False, True):
        g.call("chasm_economy", {"op": "set_stock", "building": EVI_HOUSE, "contents": {"water": 0}, "pantry": f_pantry})
    seen = set()
    fetchers = set()
    for _ in range(3 * DAY // 25):
        e = g.economy()
        for w in e["workers"]:
            if w["house"] == EVI_HOUSE and w["state"] in ERRAND:
                fetchers.add(w["id"])
        evi = g.worker(EVI, e)
        if evi is None:
            return False, "Evi died"
        seen.add(evi["state"])
        g.step(25)
    detail = "her job %s; her states %s; fetched for the house by %s" % (evi["job"], sorted(seen), sorted(fetchers))
    return evi["job"] == "old" and EVI in fetchers and bool(seen & WALK) and not (seen & WORK), detail


def case_death():
    load()
    #Short of his day by more than one wait's step, so the tick he dies on is watched.
    e = g.wait_until(lambda e: g.worker(WESSEL, e) is None or g.worker(WESSEL, e).get("dies_in_days", 1.0) <= 0.05,
                     4 * DAY, 25)
    if e is None:
        raise g.Setup("Wessel's day never came")
    #From his day, a tick at a time: what he was doing on the last tick he was seen.
    last = None
    out_after_day = 0
    for _ in range(2 * DAY):
        w = g.worker(WESSEL)
        if w is None:
            break
        last = w
        out_after_day += 0 if w["state"] == "at_home" else 1
        g.step(1)
    else:
        raise g.Setup("Wessel lived on two days past his day: %s" % g.describe(last))
    e = g.economy()
    dead = [d for d in e.get("dead", []) if d["id"] == WESSEL]
    body = fallen(WESSEL)
    told = g.call("chasm_stats", {"journal": -1})["journal"]
    told = [ev["text"] for ev in told if ev["kind"] == "died" and ev["person"] == WESSEL]
    detail = "the last tick seen %s%s, %d ticks seen out near his day; dead %s; fallen indoors %s; the journal %s" % (
        last and last["state"], " indoors" if last and last["indoors"] else "", out_after_day, dead,
        body and body.get("indoors"), told)
    return (bool(dead) and dead[0]["cause"] == "old age" and dead[0]["age"] >= 38 and last is not None
            and last["state"] == "at_home" and last["indoors"] and body is not None and body.get("indoors")
            and told == ["Wessel Veen died of old age"]), detail


def case_home_first():
    load()
    #43 is past anyone's day (LIFE_DIES_FROM + LIFE_DIES_SPAN): he dies the first time he is home, and not before.
    man = put_down(WESSEL_HOUSE, WESSEL_DOOR, age_years=43, female=False)
    trail = []
    for _ in range(3000):
        w = g.worker(man)
        if w is None:
            break
        trail.append((w["state"], w["legs_to_go"]))
        g.step(1)
    else:
        raise g.Setup("he never got home: %s" % g.describe(w))
    dead = [d for d in g.economy().get("dead", []) if d["id"] == man]
    body = fallen(man)
    states = sorted(set(s for s, _ in trail))
    detail = "%d ticks alive after he was put down, his states %s, the last (state, legs to go) %s; dead %s; " \
             "fallen indoors %s" % (len(trail), states, trail[-1] if trail else None, dead, body and body.get("indoors"))
    #Seen walking home every tick he lived, and fallen INDOORS - struck off the tick he arrived, never on the way.
    return (len(trail) > 50 and states == ["to_home"] and bool(dead) and dead[0]["cause"] == "old age"
            and body is not None and body.get("indoors")), detail


def case_orders():
    load()
    door = WESSEL_DOOR
    r = g.call("chasm_economy", {"op": "order", "person": EVI, "x": door[0] + 12.0, "z": door[1]})
    refused = r.get("refusal")
    #A day short of 36, put down and posted where he stands (add_person's `post`), living with the Veens.
    man = put_down(WESSEL_HOUSE, door, age_years=36 - 1.0 / 40, female=False, post=True)
    g.step(2)
    w = g.worker(man)
    posted = w.get("post") is not None
    e = wait_old(man, 2 * DAY)
    w = g.worker(man, e)
    released = w.get("post") is None
    home = g.wait_until(lambda e: g.worker(man, e)["state"] == "at_home", DAY, 5)
    detail = "an order to Evi: %r; the posted man posted %s, at 36 %s and %s, home %s" % (
        refused, posted, "released" if released else "STILL POSTED", w["job"], home is not None)
    return refused == "too old" and posted and released and w["job"] == "old" and home is not None, detail


def case_guard():
    """On the guard test's flat map (chasm_guard_test.fresh): a tower's guard - who keeps it through anything but the
    player (zombies_plan.md step 3b) - grows old: he comes down and goes home, and the spare goes up in his place."""
    ids = g.fresh(False)
    #The better archer takes the tower first (AssignJobs: archery, then precision); two days short of 36.
    guard = g.person(ids, dict(g.ARCHER, age_years=36 - 2.0 / 40))
    spare = g.person(ids, g.GUARD)
    ids["tower"] = g.paint("build_add", g.TOWER, kind="tower")
    if g.wait_until(g.on_tower(guard, ids["tower"]), 3000) is None:
        raise g.Setup("the guard never got onto his tower: " + g.describe(g.worker(guard)))
    e = wait_old(guard, 3 * DAY)
    w = g.worker(guard, e)
    home = g.wait_until(lambda e: g.worker(guard, e)["state"] == "at_home", DAY, 5)
    up = g.wait_until(g.on_tower(spare, ids["tower"]), 3000)
    detail = "at 36: %s; home %s; the spare %s" % (g.describe(w), home is not None, g.describe(g.worker(spare)))
    return w["job"] == "old" and w.get("post") is None and home is not None and up is not None, detail


def case_saved():
    load()
    wait_old(JELLE, 2 * DAY)
    g.step(10)

    #By `born`: his day is that and a hash of his id, so the same born is the same day. (dies_in_days would read the
    #load a tick on, and round across a hundredth.)
    def kept(e):
        return {w["id"]: (w["job"], w["stage"], w["born"]) for w in e["workers"] if w["stage"] == "old"}
    before = kept(g.economy())
    g.call("chasm_save", {"name": SAVE + "_saved"})
    g.call("chasm_load", {"name": SAVE + "_saved"})
    g.call("sim_pause", {"paused": True})
    after = kept(g.economy())
    #The save was only ever this case's: gone, so a run of the test leaves nothing in saves/.
    path = os.path.join(SAVES, SAVE + "_saved.json")
    if os.path.isfile(path):
        os.remove(path)
    return before == after and before.get(JELLE, ("",))[0] == "old", "before %s; after %s" % (before, after)


def trace():
    t = g.call("replay_trace", {})
    return [(row["t"], row["h"]) for row in t["trace"]], t.get("ticks_changed_between", 0)


def case_replay():
    load()
    if g.call("input_record", {"action": "status"}).get("recording"):
        g.call("input_record", {"action": "stop"})
    g.call("input_record", {"action": "start"})
    #Past Jelle's 36th birthday and Wessel's day, with the walk home he may have to make.
    run_ticks = 3 * DAY + DAY // 2
    g.step(run_ticks)
    rec = g.call("input_record", {"action": "stop"})
    e = g.economy()
    run = (g.worker(JELLE, e)["job"], g.worker(WESSEL, e) is None)
    if run != ("old", True):
        raise g.Setup("the run did not cover both (Jelle's job, Wessel gone): %s" % (run,))
    file = rec.get("last_recording")
    g.call("replay_trace", {"tick_starts": True})
    traces = []
    ends = []
    for _ in range(2):
        g.call("input_replay", {"action": "start", "file": file})
        g.step(run_ticks + 20)
        status = g.call("input_replay", {"action": "status"})
        if status.get("replaying"):
            raise g.Setup("the replay had not ended: %s" % status)
        traces.append(trace())
        e = g.economy()
        ends.append((g.worker(JELLE, e)["job"], g.worker(WESSEL, e) is None))
    if file and os.path.isfile(file):
        os.remove(file)
    (a, changed_a), (b, changed_b) = traces
    first = next((x[0] for x, y in zip(a, b) if x[1] != y[1]), None)
    detail = "run %s; replays %s and %s; %d and %d ticks traced, first differing %s, changed between %d/%d" % (
        run, ends[0], ends[1], len(a), len(b), first, changed_a, changed_b)
    return (a and len(a) == len(b) and first is None and not changed_a and not changed_b
            and ends[0] == ends[1] == run), detail


CASES = [("retire", case_retire), ("errands", case_errands), ("death", case_death), ("home_first", case_home_first),
         ("orders", case_orders), ("guard", case_guard), ("saved", case_saved), ("replay", case_replay)]


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
