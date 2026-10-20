"""
Chasm's kills test (ranks_plan.md, steps 1 and 2): each case built on a flat test map (test_maps_plan.md) from
nothing - the guard test's house and store (chasm_guard_test.py), the people it needs - and run paused, tick by tick,
so a case is the same every time. What it checks is the user's (2026-10-19): everyone counts the zombies they kill, by
bow and by hand, a zombie's kill is whoever's hit took the last of its health - and the RANKS those kills make.

    credit      two archers shooting at one zombie: the kill is the archer whose arrow landed as its last hit,
                watched tick by tick
    bow         two archers and four zombies: every zombie killed is someone's kill, the day's count agrees
    hand        a man with no bow fights a zombie off: one kill by hand
    after       an archer dies with his last arrow in the air: the kill is on his record among the dead
    saved       kills on the living, on the dead and in the day's count, saved and loaded again: the same
    ranks       an archer kills zombies one at a time: a rank a kill as the table has them, each told in the journal,
                his card saying it by his name
    given       someone put down with kills: his rank at once, counted in the history's kills_from, and on from there
    pace        a Master and an archer with no rank, the same archery, through a wave: the Master's turns shorter
                by his rank's pace, on average (each turn is jittered)
    one_shot    a Master through a wave: hits that kill outright, nobody shooting at a zombie one of them will fell,
                and one saved and loaded in the air
    replay      a fight with a ranked archer recorded, then replayed twice: the two traces equal on every tick, the
                kills where the run left them

Every case ends with the statistics' check (ChasmStatsCheck) agreeing on the zombies: those killed, counted, and the
kills people were put down with, against the kills of the living and the dead.

Needs a DEBUG chasm with its MCP server on the given port. It REPLACES whatever map is loaded, so run a copy, not
the game someone is playing:

    cp build/chasm_nophysics.exe build/chasm_test.exe
    ./build/chasm_test.exe --minimized --mcp-port 8775 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_kills_test.py --port 8775 [case ...]

Prints PASS or FAIL per case with what was seen; exit code 0 when every case passed.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chasm_guard_test as g

ZOMBIE_HEALTH = 60                  #ZOMBIE_HEALTH
ARROW_HIT = 20                      #ARROW_HIT
RANKS = ["", "Novice", "Expert", "Master"]     #EconomyRankName, by rank
RANK_KILLS = [0, 1, 2, 3]           #rank_kills (Economy.cpp)
RANK_PACE = [1.0, 1.05, 1.1, 1.15]  #rank_pace

ARCHER = {"skills": {"precision": 9, "strength": 5, "ingenuity": 5, "husbandry": 5}, "archery": 900000}
FIGHTER = {"skills": {"precision": 5, "strength": 8, "ingenuity": 6, "husbandry": 5}}
OPEN = (20.0, 10.0)                 #open ground east of the house
FAR = (37.0, 10.0)                  #...and 17 further east: in an archer's sight (SIGHT_RANGE 20), well out of reach


def zombie_status():
    return g.call("chasm_zombie", {"op": "status"})


def stats():
    return g.call("chasm_stats", {"journal": 0})


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


def kills_of(entry):
    k = (entry or {}).get("kills", {})
    return k.get("bow", 0), k.get("hand", 0)


def everyone(e=None):
    """Every kill the colony holds, the living's and the dead's: {id: (bow, hand)}."""
    e = e or g.economy()
    out = {}
    for w in e["workers"] + e.get("dead", []):
        out[w["id"]] = kills_of(w)
    return out


def until_clear(most, every=10):
    """Steps until no zombie is left; returns the ticks it took, or None."""
    done = 0
    while done <= most:
        if not zombie_status()["zombies"]:
            return done
        g.step(every)
        done += every
    return None


def checked(s=None):
    """The statistics' check on the zombies and the zombies killed, as it stands: (problems, totals' zombies, today's and
    the rows', the kills people were put down with). Only the check's zombies line: people put down by add_person were
    never born or settled in the history, so on a test map its people line always disagrees (zombies_plan.md, 4a) -
    not this test's business."""
    s = s or stats()
    rows = sum(d.get("zombies", 0) for d in s["days"]) + s["today"].get("zombies", 0)
    return [p for p in s["check"] if p.startswith("zombies")], s["totals"].get("zombies", 0), rows, s.get("kills_from", 0)


def tally(detail, s=None):
    """A case's last word: the check empty, and the count the totals and the rows give alike."""
    problems, total, rows, given = checked(s)
    held = sum(b + h for b, h in everyone().values())
    ok = not problems and total == rows == held - given
    return ok, "%s; killed %d (rows %d), kills held %d%s%s" % (detail, total, rows, held,
                                                               " (%d given)" % given if given else "",
                                                               "; CHECK: %s" % problems if problems else "")


#--- The cases -------------------------------------------------------------------------------------------------------

def case_credit():
    g.fresh(False)
    a = posted(OPEN, **ARCHER)
    b = posted((OPEN[0], OPEN[1] + 2.0), **ARCHER)
    drop(FAR)
    #Tick by tick: the arrows that land on the next tick are those one tick short of their flight. The first of them in
    #the arrows' order that takes the last of its health is the kill (Economy::Shoot lands them in that order).
    expected = None
    last = None
    for _ in range(3000):
        zs = zombie_status()
        if not zs["zombies"]:
            break
        m = zs["zombies"][0]
        health = m["health"]
        expected = None
        for arrow in zs["arrows"]:
            if arrow["zombie"] == m["id"] and arrow["hit"] and "landed" not in arrow and arrow["flown"] == arrow["ticks"] - 1:
                health -= ZOMBIE_HEALTH if arrow.get("fells") else ARROW_HIT
                if health <= 0:
                    expected = arrow["by"]
                    break
        last = (m["health"], m.get("body_distance"))
        g.step(1)
    else:
        raise g.Setup("the zombie never fell")
    who = {id: v for id, v in everyone().items() if sum(v)}
    ok, detail = tally("archers %s and %s; the last hit %s's arrow (the tick before: health %s, %s from its target); "
                       "kills %s" % (a, b, expected, last[0], last[1], who))
    return ok and expected is not None and who == {expected: (1, 0)}, detail


def case_bow():
    g.fresh(False)
    a = posted(OPEN, **ARCHER)
    b = posted((OPEN[0], OPEN[1] + 2.0), **ARCHER)
    drop(FAR, 4)
    took = until_clear(4000)
    if took is None:
        raise g.Setup("zombies left: %s" % zombie_status()["zombies"])
    k = everyone()
    ok, detail = tally("4 down in %d ticks; %s %s, %s %s (bow, hand)" % (took, a, k.get(a), b, k.get(b)))
    total = sum(sum(v) for v in k.values())
    bow = sum(v[0] for v in k.values())
    return ok and total == 4 and bow > 0, detail


def case_hand():
    g.fresh(False)
    man = posted(OPEN, **FIGHTER)
    drop((OPEN[0] + 3.0, OPEN[1]))
    took = until_clear(3000)
    if took is None:
        raise g.Setup("the zombie never fell")
    w = g.worker(man)
    if w is None:
        return False, "he died"
    ok, detail = tally("down in %d ticks; his kills %s (bow, hand)" % (took, kills_of(w)))
    return ok and kills_of(w) == (0, 1), detail


def case_after():
    g.fresh(False)
    archer = posted(OPEN, **ARCHER)
    drop(FAR)
    #Watched tick by tick: his arrow that will fell it in the air, and he not yet dead - then his life taken.
    killed = False
    for _ in range(3000):
        zs = zombie_status()
        if not zs["zombies"]:
            break
        m = zs["zombies"][0]
        if not killed:
            coming = [x for x in zs["arrows"] if x["zombie"] == m["id"] and x["hit"] and "landed" not in x]
            if coming and m["health"] <= ARROW_HIT * len(coming) and coming[0]["ticks"] - coming[0]["flown"] > 10:
                g.call("chasm_economy", {"op": "set_needs", "person": archer, "health_days": 0, "water_days": 0})
                killed = True
        g.step(1)
    else:
        raise g.Setup("the zombie never fell")
    if not killed:
        raise g.Setup("never caught an arrow in the air that would fell it")
    e = g.economy()
    if g.worker(archer, e) is not None:
        return False, "he was still alive when it fell"
    dead = [d for d in e.get("dead", []) if d["id"] == archer]
    ok, detail = tally("he died with his arrow in the air; his record among the dead %s" % (dead and dead[0]))
    return ok and dead and kills_of(dead[0]) == (1, 0), detail


def case_saved():
    g.fresh(False)
    archer = posted(OPEN, **ARCHER)
    man = posted((OPEN[0] - 8.0, OPEN[1] + 6.0), **FIGHTER)
    drop(FAR, 2)
    drop((OPEN[0] - 5.0, OPEN[1] + 6.0))
    if until_clear(4000) is None:
        raise g.Setup("zombies left: %s" % zombie_status()["zombies"])
    #The archer's life taken, so the dead hold kills too.
    g.call("chasm_economy", {"op": "set_needs", "person": archer, "health_days": 0, "water_days": 0})
    g.step(10)
    before_k = everyone()
    before_s = checked()
    g.call("chasm_save", {"name": "kills_test"})
    g.call("chasm_load", {"name": "kills_test"})
    g.call("sim_pause", {"paused": True})
    g.step(1)
    after_k = everyone()
    after_s = checked()
    same = before_k == after_k and before_s[1:] == after_s[1:]
    ok, detail = tally("before %s %s; after %s %s" % (before_k, before_s[1:], after_k, after_s[1:]))
    held_dead = any(d["id"] == archer and sum(kills_of(d)) for d in g.economy().get("dead", []))
    return ok and same and held_dead and sum(sum(v) for v in after_k.values()) == 3, detail


def journal_ranks(person):
    """What the journal tells of `person` rising, in order."""
    s = g.call("chasm_stats", {"journal": -1})
    return [ev["text"] for ev in s["journal"] if ev["kind"] == "ranked" and ev["person"] == person]


def card_of(person):
    """His card's subtitle and kills line - read in play mode, where cards are, and back out of it."""
    g.call("chasm_view", {"play": True})
    g.call("chasm_select", {"op": "select", "person": person})
    card = g.call("chasm_select", {"op": "status"}).get("card", {})
    g.call("chasm_view", {"play": False})
    return card.get("subtitle", ""), next((l for l in card.get("lines", []) if isinstance(l, str) and l.startswith("Kills")), "")


def case_ranks():
    g.fresh(False)
    archer = posted(OPEN, **ARCHER)
    seen = []
    #A zombie at a time, so he shoots each down alone, until the last rank's kills.
    for n in range(1, RANK_KILLS[-1] + 1):
        drop(FAR)
        if until_clear(3000, 5) is None:
            raise g.Setup("zombie %d never fell" % n)
        w = g.worker(archer)
        if w is None:
            return False, "he died at zombie %d" % n
        seen.append((sum(kills_of(w)), w.get("rank", "")))
    want = [(n, RANKS[max(r for r in range(len(RANKS)) if RANK_KILLS[r] <= n)]) for n in range(1, RANK_KILLS[-1] + 1)]
    told = journal_ranks(archer)
    subtitle, line = card_of(archer)
    ok, detail = tally("kills and rank after each zombie %s (the table's %s); the journal %s; his card %r, %r" % (
        seen, want, told, subtitle, line))
    told_ok = [t.split(" rose to ")[-1] for t in told] == RANKS[1:]
    return ok and seen == want and told_ok and subtitle.startswith(RANKS[-1] + ", age"), detail


def case_given():
    g.fresh(False)
    vet = posted(OPEN, kills=RANK_KILLS[2], **ARCHER)
    at_once = g.worker(vet).get("rank")
    drop(FAR)
    if until_clear(3000, 5) is None:
        raise g.Setup("the zombie never fell")
    w = g.worker(vet)
    told = journal_ranks(vet)
    s = stats()
    ok, detail = tally("put down with %d kills: %s at once; after one more %s, %s; the journal %s; kills_from %s" % (
        RANK_KILLS[2], at_once, kills_of(w), w.get("rank"), told, s.get("kills_from")), s)
    return ok and at_once == RANKS[2] and w.get("rank") == RANKS[3] and len(told) == 1 \
        and s.get("kills_from") == RANK_KILLS[2], detail


def first_turn(kills):
    """A fresh map, one archer with `kills`, one zombie: the length of his first turn of the bow. Same map, same ids,
    same tick - so the same jitter (EconomyShotTicks), and only his rank between two of them."""
    g.fresh(False)
    archer = posted(OPEN, kills=kills, **ARCHER)
    drop(FAR)
    for _ in range(600):
        sh = (g.worker(archer) or {}).get("shooting")
        if sh and sh.get("turn_length"):
            return sh["turn_length"]
        g.step(1)
    raise g.Setup("he never began a turn")


def hand_fight(kills):
    """A fresh map, a fighter with `kills` and a zombie by him: the ticks until it is down."""
    g.fresh(False)
    man = posted(OPEN, kills=kills, **FIGHTER)
    drop((OPEN[0] + 3.0, OPEN[1]))
    took = until_clear(3000, 1)
    if took is None or g.worker(man) is None:
        raise g.Setup("the fight did not end with him standing")
    return took


def case_pace():
    plain = first_turn(0)
    champ = first_turn(RANK_KILLS[3])
    want = plain / RANK_PACE[3]
    hand_plain = hand_fight(0)
    hand_champ = hand_fight(RANK_KILLS[3])
    ok, detail = tally("first turn of the bow %d ticks with no rank, %d as a Master (the pace says %.1f); a fight by "
                       "hand %d ticks with no rank, %d as a Master" % (plain, champ, want, hand_plain, hand_champ))
    return ok and abs(champ - want) <= 1.0 and hand_champ < hand_plain, detail


def case_one_shot():
    g.fresh(False)
    champ = posted(OPEN, kills=RANK_KILLS[3], **ARCHER)
    fells_seen = {}         #every arrow flagged to kill outright, by (by, zombie, flight): its zombie's health then
    wasted = []             #arrows loosed at a zombie one of those was already flying at
    saved = None
    known = set()
    for n in range(8):
        drop(FAR)
        health = {}
        fells_flying = set()            #zombies a flagged arrow flies at, as the last look saw it
        for _ in range(3000 // 3):
            zs = zombie_status()
            if not zs["zombies"]:
                break
            for m in zs["zombies"]:
                health[m["id"]] = m["health"]
            flying_now = set()
            for a in zs["arrows"]:
                key = (a["by"], a["zombie"], a["ticks"])     #its flight is fixed at the loose; a hit's distance follows the zombie
                in_air = "landed" not in a
                if a.get("fells") and in_air:
                    flying_now.add(a["zombie"])
                if key in known:
                    continue
                #New since the last look: loosed at a zombie a flagged arrow was already flying at is a waste.
                known.add(key)
                if a["zombie"] in fells_flying:
                    wasted.append(key)
                if a.get("fells"):
                    fells_seen[key] = health.get(a["zombie"])
                    #...and one saved and loaded in the air: still flying, still flagged.
                    if saved is None and in_air and a["ticks"] - a["flown"] > 10:
                        g.call("chasm_save", {"name": "kills_test"})
                        g.call("chasm_load", {"name": "kills_test"})
                        g.call("sim_pause", {"paused": True})
                        back = [b for b in zombie_status()["arrows"]
                                if (b["by"], b["zombie"], b["ticks"]) == key and b["flown"] == a["flown"]]
                        saved = bool(back) and bool(back[0].get("fells"))
            fells_flying = flying_now
            g.step(3)
        else:
            raise g.Setup("zombie %d never fell" % n)
    w = g.worker(champ)
    if w is None:
        return False, "he died"
    #Loosed at a zombie with more left than one ordinary hit takes: one that kills before a third hit could have.
    outright = [h for h in fells_seen.values() if h is not None and h > ARROW_HIT]
    ok, detail = tally("%d hits flagged to kill outright, the zombie's health as each was loosed %s; %d arrows wasted "
                       "on a zombie already as good as down; one saved and loaded in the air: %s; his kills %s" % (
                           len(fells_seen), list(fells_seen.values()), len(wasted), saved, kills_of(w)))
    return ok and bool(outright) and not wasted and bool(saved), detail


def trace():
    t = g.call("replay_trace", {})
    return [(row["t"], row["h"], row.get("parts", {})) for row in t["trace"]], t.get("ticks_changed_between", 0)


def case_replay():
    g.fresh(False)
    #A Expert: his pace and his hits that kill outright in the replay too.
    posted(OPEN, kills=RANK_KILLS[2], **ARCHER)
    posted((OPEN[0], OPEN[1] + 2.0), **FIGHTER)
    #A recording of its own from here: any running one stopped first (the demo's always-on recording).
    if g.call("input_record", {"action": "status"}).get("recording"):
        g.call("input_record", {"action": "stop"})
    g.call("input_record", {"action": "start"})
    drop(FAR, 3)
    g.step(1500)
    rec = g.call("input_record", {"action": "stop"})
    run = everyone()
    if not sum(sum(v) for v in run.values()):
        raise g.Setup("nobody killed anything in the run")
    file = rec.get("last_recording")
    g.call("replay_trace", {"tick_starts": True})
    traces = []
    ends = []
    for _ in range(2):
        g.call("input_replay", {"action": "start", "file": file})
        g.step(1520)
        status = g.call("input_replay", {"action": "status"})
        if status.get("replaying"):
            raise g.Setup("the replay had not ended: %s" % status)
        traces.append(trace())
        ends.append(everyone())
    #The recording was only ever this case's: gone, so a run of the test leaves nothing in recordings/.
    if file and os.path.isfile(file):
        os.remove(file)
    (a, changed_a), (b, changed_b) = traces
    first = next((x[0] for x, y in zip(a, b) if x[1] != y[1]), None)
    ok, detail = tally("run kills %s; replays %s and %s; %d and %d ticks traced, first differing %s, changed between %d/%d" % (
        run, ends[0], ends[1], len(a), len(b), first, changed_a, changed_b))
    return ok and a and len(a) == len(b) and first is None and not changed_a and not changed_b \
        and ends[0] == ends[1] == run, detail


CASES = [("credit", case_credit), ("bow", case_bow), ("hand", case_hand), ("after", case_after), ("saved", case_saved),
         ("ranks", case_ranks), ("given", case_given), ("pace", case_pace), ("one_shot", case_one_shot),
         ("replay", case_replay)]


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
        print("%s  %-8s %s" % ("PASS" if ok else "FAIL", name, detail))
        sys.stdout.flush()
        if not ok:
            failed.append(name)
    print("failed: %d %s" % (len(failed), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
