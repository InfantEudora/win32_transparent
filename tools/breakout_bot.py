#!/usr/bin/env python3
"""
Plays APP=Breakout through its MCP tools, with no hands on the keyboard.

    python tools/breakout_bot.py play      --seed 7 --rounds 60
    python tools/breakout_bot.py probe     --speeds 15,30,60,120,240,480
    python tools/breakout_bot.py angles
    python tools/breakout_bot.py determinism --seed 11

This is not a demo. It is the instrument the engineering report was written from:

  play        drives the paddle from the ball's predicted landing point and reports what a
              real rally does to the game - score, combos, shield saves, power-ups, how many
              rigid bodies the debris leaves lying around.
  probe       the tunnelling measurement. Pins the ball's speed and counts ESCAPES, which is
              what a missed collision looks like from outside the arena. See section 8 of
              docs/breakout_findings.md.
  angles      plays a rally and reads back, for each paddle bounce, where on the paddle the ball
              struck against the angle it left at. Checks that the DESIGNED bounce is the one
              the rules actually produce, which is the whole reason the ball is not a rigid
              body.
  capsules    chases and catches a falling power-up capsule, which is the one gameplay object
              reactphysics3d places rather than the rules - so it is the only thing that
              exercises the contact callback end to end.
  determinism runs the same seed and the same scripted input twice and diffs the outcome.

USE 127.0.0.1, NEVER localhost. The server binds IPv4 only; where localhost resolves to ::1
first, every call pays a failed IPv6 connect - measured at 2,058 ms against 15 ms.
"""

import argparse
import json
import math
import sys
import urllib.request

URL = "http://127.0.0.1:8765/mcp"

# Mirrors breakout/Field.h. Duplicated rather than parsed out of the header because a test that
# silently follows the thing it is testing cannot catch it changing.
FIELD_LEFT = 0.0
FIELD_RIGHT = 22.0
PADDLE_Y = 2.60
BALL_RADIUS = 0.40
TPS = 60.0

_next_id = [0]


def call(name, args=None, timeout=180):
    _next_id[0] += 1
    body = json.dumps({
        "jsonrpc": "2.0",
        "id": _next_id[0],
        "method": "tools/call",
        "params": {"name": name, "arguments": args or {}},
    }).encode()
    req = urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        payload = json.loads(response.read().decode())
    if "error" in payload:
        raise RuntimeError(payload["error"])
    for block in payload["result"]["content"]:
        if block.get("type") == "text":
            return json.loads(block["text"])
    return {}


def live_balls(state):
    return [b for b in state.get("balls", []) if not b.get("stuck")]


def predict_landing(ball):
    """Where a ball will cross the paddle's plane, reflecting off the side walls on the way.

    Straight-line, because that is what the rules do between collisions - and deliberately
    ignoring the bricks, because a bot that modelled them would be a second implementation of
    the game rather than a player of it.
    """
    x, y, vx, vy = ball["x"], ball["y"], ball["vx"], ball["vy"]
    if vy >= 0.0:
        return None
    travel = (y - (PADDLE_Y + BALL_RADIUS)) / -vy
    x += vx * travel
    # Fold the path back into the arena, as many times as it takes.
    span = (FIELD_RIGHT - BALL_RADIUS) - (FIELD_LEFT + BALL_RADIUS)
    if span <= 0:
        return x
    folded = (x - (FIELD_LEFT + BALL_RADIUS)) % (2.0 * span)
    if folded > span:
        folded = 2.0 * span - folded
    return FIELD_LEFT + BALL_RADIUS + folded


def steer_towards(state, target_x, ticks):
    """One steering decision, held for `ticks` simulation ticks."""
    error = target_x - state["paddle_x"]
    # Proportional, clamped. The paddle has its own acceleration model in the rules, so a bang-bang
    # controller overshoots badly and a proportional one does not.
    value = max(-1.0, min(1.0, error * 0.45))
    return call("breakout_steer", {"value": value, "ticks": ticks})


def cmd_play(args):
    state = call("breakout_restart", {"seed": args.seed})
    print(f"seed {state['seed']}  level {state['level']}  {state['bricks_left']} bricks")
    for row in state["wall"]:
        print("   " + row)

    peak = dict(score=0, combo=0, debris=0, capsules=0)
    for round_index in range(args.rounds):
        if state["phase"] == "gameover":
            break
        if state["phase"] == "ready":
            # Park the paddle under the resting ball, then serve.
            balls = state.get("balls", [])
            if balls:
                state = steer_towards(state, balls[0]["x"], 6)
            state = call("breakout_launch")
            continue

        balls = live_balls(state)
        if not balls:
            state = call("breakout_steer", {"value": 0.0, "ticks": 10})
            continue

        # Chase whichever ball is coming down soonest; ignore the ones going up.
        descending = [b for b in balls if b["vy"] < 0.0]
        target = None
        if descending:
            descending.sort(key=lambda b: b["y"])
            target = predict_landing(descending[0])
        if target is None:
            target = 11.0
        state = steer_towards(state, target, args.ticks)

        peak["score"] = max(peak["score"], state["score"])
        peak["combo"] = max(peak["combo"], state["best_combo"])
        peak["debris"] = max(peak["debris"], state["debris_bodies"])
        peak["capsules"] = max(peak["capsules"], state["capsules_falling"])

        if round_index % 5 == 0 or state["phase"] != "playing":
            print(f"  t={state['tick']:<7} {state['phase']:<13} score={state['score']:<6} "
                  f"lvl={state['level']} bricks={state['bricks_left']:<3} "
                  f"balls={state['balls_left']} shield={state['shield_charge']:.2f} "
                  f"saves={state['shield_saves']} caught={state['powerups_caught']} "
                  f"debris={state['debris_bodies']}")

    print()
    print(f"final  score={state['score']}  level={state['level']}  phase={state['phase']}")
    print(f"       bricks broken={state['bricks_broken']}  best combo={peak['combo']}")
    print(f"       shield saves={state['shield_saves']}  power-ups caught={state['powerups_caught']}")
    print(f"       peak live bodies: {peak['debris']} debris, {peak['capsules']} capsules")
    return 0


def cmd_probe(args):
    speeds = [float(s) for s in args.speeds.split(",")]
    print(f"{'speed':>8} {'u/tick':>8} {'ticks':>7} {'escapes':>8} {'bricks':>7} {'overruns':>9}  verdict")
    print("-" * 72)
    for speed in speeds:
        call("breakout_restart", {"seed": args.seed})
        call("breakout_launch")
        result = call("breakout_ball_probe", {"speed": speed, "ticks": args.ticks}, timeout=300)
        probe = result["probe"]
        escapes = probe["escapes"]
        bricks = probe.get("bricks_broken", 0)
        overruns = probe.get("resolution_overruns", 0)
        per_tick = speed / TPS
        if escapes:
            verdict = f"LEAKED {escapes}x"
        elif not bricks:
            verdict = "no bricks hit?"
        elif overruns:
            verdict = f"clean, {overruns} capped ticks"
        else:
            verdict = "clean"
        print(f"{speed:8.0f} {per_tick:8.2f} {args.ticks:7} {escapes:8} {bricks:7} {overruns:9}  {verdict}")
    print()
    print("escapes  a ball found outside the arena: a collision the sweep missed entirely.")
    print("bricks   that the sweep is still HITTING things rather than merely staying in the box.")
    print("overruns ticks where the ball hit six impacts and the rest of its travel was dropped.")
    print()
    print("A brick is 2.0 x 1.0 world units and the ball's radius is 0.4, so anything past")
    print("60 units/second moves more than a brick's height in a single tick.")
    return 0


def cmd_angles(args):
    """The designed-bounce check.

    Plays a normal rally and reads back what the game recorded for each paddle bounce: where on
    the paddle the ball struck (-1..+1) against the angle it left at. The rules claim that angle
    is offset * BREAKOUT_PADDLE_MAX_DEFLECT plus a share of the paddle's own motion, and that
    claim is the entire reason the ball is not a rigid body - so it is worth checking rather than
    asserting.

    Read back from the snapshot rather than measured here, because a bounce exists on exactly one
    tick and no polling tool can be guaranteed to see it.
    """
    MAX_DEFLECT_DEG = math.degrees(1.12)   # BREAKOUT_PADDLE_MAX_DEFLECT in breakout/Field.h

    state = call("breakout_restart", {"seed": args.seed})
    state = call("breakout_launch")

    samples = []
    seen_ticks = set()
    for _ in range(args.rounds):
        balls = live_balls(state)
        target = 11.0
        descending = [b for b in balls if b["vy"] < 0.0]
        if descending:
            descending.sort(key=lambda b: b["y"])
            predicted = predict_landing(descending[0])
            if predicted is not None:
                # Deliberately aim OFF centre by a varying amount, so the samples cover the
                # paddle rather than clustering at the middle where a good player would keep them.
                bias = (len(samples) % 5 - 2) * 0.36 * state["paddle_half_width"]
                target = predicted + bias
        state = steer_towards(state, target, args.ticks)

        if state["phase"] == "ready":
            state = call("breakout_launch")
            continue

        hit = state.get("last_paddle_hit", {})
        if hit.get("tick") and hit["tick"] not in seen_ticks and hit["speed"] > 0.0:
            seen_ticks.add(hit["tick"])
            samples.append(hit)
        if len(samples) >= args.samples:
            break

    if not samples:
        print("No paddle bounces recorded - did the ball ever come back?")
        return 1

    samples.sort(key=lambda h: h["offset"])
    print(f"{'offset':>8} {'measured':>10} {'expected':>10} {'delta':>8} {'speed':>8}")
    print("-" * 50)
    worst = 0.0
    for hit in samples:
        expected = hit["offset"] * MAX_DEFLECT_DEG
        delta = hit["angle_deg"] - expected
        worst = max(worst, abs(delta))
        print(f"{hit['offset']:8.3f} {hit['angle_deg']:10.2f} {expected:10.2f} "
              f"{delta:8.2f} {hit['speed']:8.2f}")
    print()
    print(f"{len(samples)} bounces, worst deviation from the designed angle {worst:.2f} deg.")
    print()
    print("Expected = offset * BREAKOUT_PADDLE_MAX_DEFLECT. Two deliberate mechanisms account for")
    print("every deviation you will see, and neither is an error:")
    print("  - BREAKOUT_PADDLE_ENGLISH adds a share of the paddle's own movement, so a bounce taken")
    print("    while sweeping hard to reach the ball lands a degree or two off.")
    print("  - BREAKOUT_MIN_UX floors the horizontal component, so a near-dead-centre hit is pushed")
    print("    out to asin(0.12) = 6.89 deg rather than going vertical and bouncing between two")
    print("    points forever. Any sample sitting at exactly +/-6.89 is that clamp, not a miss.")
    return 0


def cmd_capsules(args):
    """Catch a falling power-up capsule, which is the one gameplay object the SOLVER places.

    The capsule is a real rigid body: it falls under gravity, it can clip a brick on the way down,
    and the catch is resolved by reactphysics3d reporting a contact between it and the paddle's
    kinematic body. So this exercises the contact-callback path end to end, which nothing else in
    the app does.
    """
    state = call("breakout_restart", {"seed": args.seed})
    state = call("breakout_launch")
    caught_at_start = state["powerups_caught"]
    seen = 0

    for _ in range(args.rounds):
        if state["phase"] == "gameover":
            break
        if state["phase"] == "ready":
            state = call("breakout_launch")
            continue

        falling = state.get("capsules", [])
        if falling:
            seen = max(seen, len(falling))
            # Go for the lowest one; it is the one about to be out of reach.
            falling.sort(key=lambda c: c["y"])
            target = falling[0]["x"]
            state = steer_towards(state, target, args.ticks)
            if state["powerups_caught"] > caught_at_start:
                print(f"  caught one at tick {state['tick']}: "
                      f"wide={state['powerup_wide_ticks']} slow={state['powerup_slow_ticks']} "
                      f"shield={state['shield_charge']:.2f} balls={len(state['balls'])} "
                      f"score={state['score']}")
                caught_at_start = state["powerups_caught"]
            continue

        balls = live_balls(state)
        descending = [b for b in balls if b["vy"] < 0.0]
        target = 11.0
        if descending:
            descending.sort(key=lambda b: b["y"])
            target = predict_landing(descending[0]) or 11.0
        state = steer_towards(state, target, args.ticks)

    print()
    print(f"capsules seen falling at once (peak): {seen}")
    print(f"power-ups caught: {state['powerups_caught']}")
    print(f"final score {state['score']}, level {state['level']}, phase {state['phase']}")
    return 0 if state["powerups_caught"] > 0 else 1


def cmd_determinism(args):
    """Same seed, same scripted input, twice. Anything that differs is not reproducible."""
    runs = []
    for attempt in (1, 2):
        call("breakout_restart", {"seed": args.seed})
        state = call("breakout_launch")
        # A fixed input script rather than the chasing bot: the bot reacts to state, so two runs
        # of it would issue different commands and prove nothing.
        script = [(0.0, 40), (0.8, 25), (-0.9, 30), (0.4, 35), (0.0, 40), (-0.6, 30), (0.7, 40)]
        for value, ticks in script:
            state = call("breakout_steer", {"value": value, "ticks": ticks})
        runs.append(state)
        print(f"  run {attempt}: score={state['score']} bricks_left={state['bricks_left']} "
              f"shield={state['shield_charge']:.4f} saves={state['shield_saves']} "
              f"ball0={state['balls'][0] if state['balls'] else None}")

    keys = ("score", "bricks_left", "shield_charge", "shield_saves", "balls_left",
            "bricks_broken", "powerups_caught", "wall")
    differences = [k for k in keys if runs[0][k] != runs[1][k]]
    print()
    if differences:
        print("NOT reproducible. Differs in: " + ", ".join(differences))
        for k in differences:
            print(f"   {k}: {runs[0][k]!r}  vs  {runs[1][k]!r}")
        return 1
    print("Reproducible across both runs on every field checked.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("play", help="play a game and report what happened")
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--rounds", type=int, default=60)
    p.add_argument("--ticks", type=int, default=10, help="ticks per steering decision")
    p.set_defaults(func=cmd_play)

    p = sub.add_parser("probe", help="the tunnelling measurement")
    p.add_argument("--speeds", default="15,30,60,120,240,480")
    p.add_argument("--ticks", type=int, default=1800)
    p.add_argument("--seed", type=int, default=3)
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("angles", help="check the designed paddle bounce")
    p.add_argument("--seed", type=int, default=5)
    p.add_argument("--rounds", type=int, default=400)
    p.add_argument("--ticks", type=int, default=8)
    p.add_argument("--samples", type=int, default=14)
    p.set_defaults(func=cmd_angles)

    p = sub.add_parser("capsules", help="catch a falling power-up (the contact-callback path)")
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--rounds", type=int, default=500)
    p.add_argument("--ticks", type=int, default=8)
    p.set_defaults(func=cmd_capsules)

    p = sub.add_parser("determinism", help="same seed and inputs twice, then diff")
    p.add_argument("--seed", type=int, default=11)
    p.set_defaults(func=cmd_determinism)

    args = parser.parse_args()
    try:
        return args.func(args)
    except urllib.error.URLError as error:
        print(f"Could not reach {URL}: {error}", file=sys.stderr)
        print("Is wind.exe running, built with APP=Breakout?", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
