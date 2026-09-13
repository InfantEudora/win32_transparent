#!/usr/bin/env python3
"""Orbit Outpost - does anything on the deck sit underneath a ramp?

WHY THIS EXISTS
---------------
A pinball table is the one layout in this repo where "is A on top of B" is a question about a
PATH rather than about a pair of coordinates. Three separate features in the first draft of
apps/pinball/pinball_design.md were placed underneath a ramp - a standup target, the gravity-well
saucer and the whole MISSION drop bank - and each was invisible while reading the numbers,
because the numbers involved are in two different tables and one of them is a polyline.

A feature roofed by a ramp is not a cosmetic problem. It is a shot that cannot be made: the
saucer that starts every mission on the machine had 0.22 of headroom against a ball 0.27 across,
so the mission-start shot simply did not exist.

So: sample both ramp centrelines densely, and for every feature on the deck ask whether its
footprint falls inside the ramp's ribbon and, if so, how much room is left between the ramp's
underside and the top of the feature.

RUN IT AFTER MOVING ANYTHING - a ramp path, a feature, a post, a ramp's height. It takes no
arguments and needs nothing running:

    python tools/pinball_clearance.py

KEEPING IT HONEST
-----------------
The numbers below are a COPY of apps/pinball/Table.h and the ramp paths in
apps/pinball/ApplicationPinball.cpp, not a parse of them, so the two can drift. They are checked
against the headers at the end of this file, which prints what it believes so a reader can see
whether it still matches. Parsing the C++ would be more correct and much more fragile; a printed
assumption that someone can eyeball is the better trade for a check that runs in a second.

WHAT COUNTS AS A FAILURE
------------------------
Not every overlap is a bug. A ramp that crosses the SKIRT of a pop bumper is the design asking
for a ramp that "loops over the bumper nest", and no ball is meant to roll over a bumper cap - the
test there is only that nothing intersects. Those are listed as ALLOWED below, by name, so that a
new one has to be argued for rather than quietly absorbed.
"""

import math
import sys

BALL_DIAMETER = 0.27
RAMP_WIDTH = 0.46
RAMP_SLAB = 0.08        # how far the ramp floor hangs below its centreline

# --- the ramp centrelines, from ApplicationPinball.cpp ------------------------------------------
LEFT_RAMP = [
    (-2.05, 0.00,  2.00), (-2.16, 0.20,  1.10), (-2.27, 0.52,  0.10), (-2.30, 0.85, -1.00),
    (-2.00, 0.86, -1.55), (-1.55, 0.84, -1.45), (-1.45, 0.78, -0.70), (-1.50, 0.66,  0.40),
    (-1.55, 0.52,  1.60), (-1.62, 0.34,  2.80), (-1.70, 0.32,  3.70),
]
RIGHT_RAMP = [
    ( 1.45, 0.00,  2.20), ( 1.55, 0.18,  1.30), ( 1.64, 0.44,  0.40), ( 1.70, 0.70, -0.60),
    ( 1.35, 0.76, -1.45), ( 0.55, 0.86, -2.00), (-0.20, 0.86, -2.35), (-0.70, 0.80, -1.85),
    (-0.55, 0.70, -1.15), ( 0.10, 0.60, -0.70), ( 0.70, 0.46,  0.30), ( 0.95, 0.30,  1.70),
    ( 1.05, 0.34,  3.10), ( 1.10, 0.32,  3.70),
]

# --- what is on the deck, from Table.h ----------------------------------------------------------
# name, x, z, footprint radius in plan, height above the deck
FEATURES = [
    ("flipper_left",    -1.15,  5.35, 0.80, 0.20),
    ("flipper_right",    0.55,  5.35, 0.80, 0.20),
    ("flipper_upper",   -2.35, -1.75, 0.62, 0.20),
    ("sling_left",      -1.45,  4.55, 0.50, 0.34),
    ("sling_right",      0.85,  4.55, 0.50, 0.34),
    ("inlane_left",     -1.60,  4.60, 0.13, 0.02),
    ("inlane_right",     1.00,  4.60, 0.13, 0.02),
    ("save_left",       -2.05,  5.70, 0.15, 0.02),
    ("save_right",       1.45,  5.70, 0.15, 0.02),
    ("plunger",          2.575, 6.30, 0.14, 0.27),
    ("skill_low",        2.575,-3.00, 0.13, 0.02),
    ("skill_mid",        2.575,-4.20, 0.13, 0.02),
    ("skill_high",       2.575,-5.00, 0.13, 0.02),
    ("gate_orbit",       2.35, -5.30, 0.20, 0.35),
    ("fuel_f",          -1.50, -5.15, 0.15, 0.02),
    ("fuel_u",          -0.30, -5.15, 0.15, 0.02),
    ("fuel_e",           0.90, -5.15, 0.15, 0.02),
    ("wormhole_left",   -1.90, -4.10, 0.24, 0.07),
    ("wormhole_centre", -0.30, -4.20, 0.24, 0.07),
    ("wormhole_right",   1.30, -4.10, 0.24, 0.07),
    ("bumper_left",     -1.35, -2.55, 0.42, 0.63),
    ("bumper_right",     0.15, -2.55, 0.42, 0.63),
    ("bumper_low",      -0.60, -3.45, 0.42, 0.63),
    ("drop_m",           2.05, -3.30, 0.22, 0.30),
    ("drop_i",           2.05, -3.90, 0.22, 0.30),
    ("drop_s",           2.05, -4.50, 0.22, 0.30),
    ("standup_upper",    1.75, -1.90, 0.21, 0.32),
    ("standup_lower",    1.75, -2.55, 0.21, 0.32),
    ("gravity_well",    -0.40,  0.70, 0.24, 0.07),
    ("post_well_a",     -0.95,  0.30, 0.11, 0.48),
    ("post_well_b",      0.15,  0.30, 0.11, 0.48),
    ("post_drop_a",      1.80, -3.00, 0.11, 0.48),
    ("post_drop_b",      1.80, -4.80, 0.11, 0.48),
    ("post_ramp_l",     -2.62,  2.90, 0.11, 0.48),
    ("post_ramp_r",      1.55,  2.60, 0.11, 0.48),
    ("post_mid_l",      -1.00,  2.00, 0.11, 0.48),
    ("post_mid_r",       0.00,  2.00, 0.11, 0.48),
]

# The spinner is deliberately absent. It hangs IN the mouth of the left ramp - the ball passes
# through it on the way in - so "is it under the ramp" is the wrong question to ask about it.

# (ramp, feature) pairs where an overlap is the design rather than a fault. Each needs a reason.
ALLOWED = {
    ("right", "bumper_right"):
        "the design asks the right ramp to loop over the bumper nest; this crosses the bumper's "
        "SKIRT, which is art. Nothing intersects and no ball rolls over a bumper cap.",
}


def sample(path, per_segment=40):
    """Dense (x, y, z) samples along a centreline."""
    out = []
    for i in range(len(path) - 1):
        a, b = path[i], path[i + 1]
        for k in range(per_segment + 1):
            t = k / per_segment
            out.append(tuple(a[j] + (b[j] - a[j]) * t for j in range(3)))
    return out


def check(ramp_name, path):
    """Returns (failures, notes) for one ramp."""
    points = sample(path)
    failures, notes = [], []
    for name, fx, fz, frad, fh in FEATURES:
        worst = None
        for (px, py, pz) in points:
            if math.hypot(px - fx, pz - fz) < frad + RAMP_WIDTH / 2:
                headroom = (py - RAMP_SLAB) - fh
                if worst is None or headroom < worst[0]:
                    worst = (headroom, px, py, pz)
        if worst is None:
            continue
        headroom, px, py, pz = worst
        if headroom > BALL_DIAMETER * 1.5:
            continue
        line = ("%-6s %-16s headroom %+.3f (%.2f balls)  ramp at (%.2f, %.2f, %.2f)"
                % (ramp_name, name, headroom, headroom / BALL_DIAMETER, px, py, pz))
        if (ramp_name, name) in ALLOWED:
            notes.append(line + "\n           allowed: " + ALLOWED[(ramp_name, name)])
        elif headroom > BALL_DIAMETER:
            failures.append("TIGHT   " + line)
        else:
            failures.append("BLOCKED " + line)
    return failures, notes


def main():
    print("Orbit Outpost clearance check")
    print("  ball %.2f across, ramps %.2f wide with a %.2f slab under the centreline"
          % (BALL_DIAMETER, RAMP_WIDTH, RAMP_SLAB))
    print("  %i features checked against 2 ramps\n" % len(FEATURES))

    failures, notes = [], []
    for name, path in (("left", LEFT_RAMP), ("right", RIGHT_RAMP)):
        f, n = check(name, path)
        failures += f
        notes += n

    for line in notes:
        print("note:    " + line)
    if notes:
        print()

    if failures:
        for line in failures:
            print(line)
        print("\n%i problem(s). A BLOCKED feature is a shot that cannot be made." % len(failures))
        return 1

    print("clear - nothing on the deck is roofed over.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
