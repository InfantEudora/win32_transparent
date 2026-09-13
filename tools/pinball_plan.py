#!/usr/bin/env python3
"""Orbit Outpost - draw the table's plan at true scale and find out where the ball cannot go.

WHY THIS EXISTS
---------------
A pinball layout is a set of coordinates, and none of them says whether a ball FITS. A lane is
the gap between two walls defined in two different places; a ramp's mouth is where two rails and
a floor happen to meet the deck; "can a ball get from the plunger to the left wormhole" is a
question about everything on the table at once. The first draft of this table had an inlane a
ball could not enter, an orbit return that fed the outlane instead of the upper flipper, and an
inlane that delivered the ball PAST the flipper into the drain - and none of those was visible
in the numbers, because each was a relationship between things defined a hundred lines apart.

So this asks the question the way a ball would. It draws every wall, post, target and bumper the
app has built, fattens each by the ball's radius (a ball's CENTRE can go anywhere that is at
least one radius from every obstacle), floods that free space from where the plunger serves the
ball, and reports every feature the flood did not reach. Then it does it again with the ball
fattened to 1.5 diameters, and anything that drops out is TIGHT - passable, but with no margin
for a real machine's guides and rubbers.

It also does the one thing the deck-level flood cannot: for every ramp, it walks the centreline
and checks the headroom over everything the ramp passes above, because a saucer under a ramp is
a hole no ball can fall into and that was three of the first draft's ten faults.

WHERE THE NUMBERS COME FROM
---------------------------
From the running app, and from nowhere else. Every builder in ApplicationPinball.cpp records
what it built (see PinPlanEntry in ApplicationPinball.h) and `pinball_layout` emits the lot; this
script draws THAT. There is deliberately no copy of Table.h in here: the previous clearance
script kept one, and a check that can drift from the thing it checks is a check that fails
quietly. The cost is that the app has to be running - which for a layout tool is the right cost,
because the picture on screen and the plan on paper then come from the same build.

    apps/pinball/build/pinball.exe 2>stderr.log &
    python tools/pinball_plan.py                          # report + apps/pinball/build/plan.png
    python tools/pinball_plan.py --out where/plan.png
    python tools/pinball_plan.py --json saved_layout.json # offline, from a saved pinball_layout

READING THE PICTURE
-------------------
Up-table is at the top, the drain at the bottom, so it matches the camera. Grey is a wall at the
thickness it is DRAWN; the paler halo round it is the band the ball's centre cannot enter. Green
is where the ball can get to from the plunger; red is free deck the ball cannot reach - a red
lane is a lane with no way in. Yellow is reachable only just: a ball fits but 1.5 balls do not.
Ramps are drawn as their centreline: solid where the ball rides on them at deck level, dashed
where they are in the air and the deck under them is open.
"""

import argparse
import json
import math
import os
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

URL = "http://127.0.0.1:8765/mcp"
SCALE = 90                  # pixels per world unit; a ball is then ~24 px across
MARGIN = 0.50               # world units of deck drawn beyond the play area
HEADROOM_BALLS = 1.5        # a ramp with less than this much air over the deck beside a feature is noted
ROOM_BALLS = 1.4            # a lane narrower than this many balls is TIGHT; real lanes run 1.4-1.6

# --- fetching -----------------------------------------------------------------------------------


def fetch_layout(url):
    body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "pinball_layout", "arguments": {}}}
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        resp = json.loads(r.read().decode())
    if "error" in resp:
        raise SystemExit("pinball_layout failed: %s" % resp["error"])
    for block in resp["result"]["content"]:
        if block.get("type") == "text":
            return json.loads(block["text"])
    raise SystemExit("pinball_layout returned no text block")


# --- geometry helpers --------------------------------------------------------------------------


class Frame:
    """World (x, z) <-> image (px, py). +Z (down-table) is DOWN in the image."""

    def __init__(self, play):
        self.min_x = play["min_x"] - MARGIN
        self.min_z = play["min_z"] - MARGIN
        self.w = int((play["max_x"] - play["min_x"] + 2 * MARGIN) * SCALE)
        self.h = int((play["max_z"] - play["min_z"] + 2 * MARGIN) * SCALE)

    def px(self, x, z):
        return ((x - self.min_x) * SCALE, (z - self.min_z) * SCALE)

    def world(self, px, py):
        return (px / SCALE + self.min_x, py / SCALE + self.min_z)

    def r(self, units):
        return units * SCALE


def flipper_tip(pivot, length, angle_deg, mirrored):
    """A bat's tip. The bat lies along +X and a positive angle about +Y takes +X toward -Z, i.e.
    up-table - the convention documented at PIN_FLIPPER_REST_DEG. The MIRRORED bat is the same
    bat turned through 180 - angle, so its x flips and its z does not: at rest both tips point
    down-table. (The first draft flipped z too, and the right flipper stood at rest in its
    flipped pose.)"""
    a = math.radians(angle_deg)
    d = -1.0 if mirrored else 1.0
    return (pivot[0] + d * length * math.cos(a), pivot[2] - length * math.sin(a))


def box_corners(centre, size_x, size_z, yaw_deg):
    a = math.radians(yaw_deg)
    c, s = math.cos(a), math.sin(a)
    out = []
    for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        lx, lz = dx * size_x * 0.5, dz * size_z * 0.5
        # rotation about +Y: x' = x cos + z sin, z' = -x sin + z cos
        out.append((centre[0] + lx * c + lz * s, centre[2] - lx * s + lz * c))
    return out


def ramp_is_low(y, slab, ball_d):
    """True where the ramp floor's underside is too low for a ball to pass beneath - which is
    where the ramp is part of the deck rather than something above it."""
    return (y - slab) < ball_d + 0.02


def sample_polyline(points, step=0.02):
    out = []
    for i in range(len(points) - 1):
        a, b = points[i], points[i + 1]
        seg = math.dist(a, b)
        n = max(1, int(seg / step))
        for k in range(n):
            t = k / n
            out.append(tuple(a[j] + (b[j] - a[j]) * t for j in range(3)))
    out.append(tuple(points[-1]))
    return out


# --- the mask: where can a ball's CENTRE be? -----------------------------------------------------

FREE, BLOCKED, REACHED = 255, 0, 128


def draw_obstacles(mask, frame, layout, inflate, f_collider_thickness=None):
    """Paint every obstacle, grown by `inflate` world units, as BLOCKED onto `mask`."""
    d = ImageDraw.Draw(mask)
    ball_d = layout["ball_diameter"]

    def disc(x, z, radius):
        px, py = frame.px(x, z)
        rr = frame.r(radius + inflate)
        d.ellipse((px - rr, py - rr, px + rr, py + rr), fill=BLOCKED)

    def fat_line(pts, thickness):
        width = max(1, int(round(frame.r(thickness + 2 * inflate))))
        ipts = [frame.px(p[0], p[2]) for p in pts]
        if len(ipts) >= 2:
            d.line(ipts, fill=BLOCKED, width=width, joint="curve")
        for p in (pts[0], pts[-1]):
            disc(p[0], p[2], thickness * 0.5)

    def fat_polygon(corners_xz):
        ipts = [frame.px(x, z) for x, z in corners_xz]
        d.polygon(ipts, fill=BLOCKED)
        width = max(1, int(round(frame.r(2 * inflate))))
        d.line(ipts + [ipts[0]], fill=BLOCKED, width=width, joint="curve")
        for x, z in corners_xz:
            disc(x, z, 0.0)

    for e in layout["plan"]:
        kind, pts = e["kind"], e["points"]
        if kind == "wall":
            thickness = e["a"] if f_collider_thickness is None else f_collider_thickness
            fat_line(pts, thickness)
        elif kind == "post":
            disc(pts[0][0], pts[0][2], e["a"])
        elif kind == "disc":
            if e["a"] > 0.0:
                disc(pts[0][0], pts[0][2], e["a"])
        elif kind == "box":
            if e["name"].startswith("gate"):
                continue        # swings open for the ball; not an obstacle to reachability
            fat_polygon(box_corners(pts[0], e["a"], e["b"], e["d"]))
        elif kind == "flipper":
            tip = flipper_tip(pts[0], e["a"], e["b"], e["c"] > 0.5)
            fat_line([pts[0], (tip[0], 0.0, tip[1])], e["d"])
        elif kind == "ramp":
            # Where the ramp is part of the deck, its two rails are walls standing just OUTSIDE
            # the floor (BuildRamps puts them at half the width plus half a rail). Where it is in
            # the air, the deck under it is open and only the headroom check cares.
            half = e["a"] * 0.5 + 0.03
            for side in (-1.0, 1.0):
                run = []
                for i in range(len(pts)):
                    p = pts[i]
                    # heading: average of the segments either side, in the horizontal plane
                    hx = hz = 0.0
                    for q, r in ((pts[i - 1], p) if i > 0 else (None, None),
                                 (p, pts[i + 1]) if i + 1 < len(pts) else (None, None)):
                        if q is None:
                            continue
                        dx, dz = r[0] - q[0], r[2] - q[2]
                        L = math.hypot(dx, dz)
                        if L > 1e-6:
                            hx += dx / L
                            hz += dz / L
                    L = math.hypot(hx, hz)
                    if L < 1e-6:
                        continue
                    hx, hz = hx / L, hz / L
                    # left of travel is (up x heading) = (hz, 0, -hx)... in xz: (+hz, -hx)
                    ox, oz = hz * half * side, -hx * half * side
                    # The same rule BuildRamps splits the floor from the wires with: every point
                    # a ball on the deck could not pass under is part of the deck.
                    if ramp_is_low(p[1], e["c"], ball_d):
                        run.append((p[0] + ox, 0.0, p[2] + oz))
                    else:
                        if len(run) >= 2:
                            fat_line(run, 0.06)
                        run = []
                if len(run) >= 2:
                    fat_line(run, 0.06)

    # The cabinet. Everything outside the play area is solid.
    play = layout["play"]
    x0, z0 = frame.px(play["min_x"], play["min_z"])
    x1, z1 = frame.px(play["max_x"] + 0.0, play["max_z"])
    # The plunger lane is INSIDE the cabinet but outside "play" (its divider is a wall the plan
    # has); extend the right edge to the chute's outer wall, which is the cabinet's inner face.
    x1 = frame.px(layout["deck"]["max_x"] - 0.05, 0)[0]
    W, H = mask.size
    grow = frame.r(inflate)
    d.rectangle((0, 0, W, z0 + grow), fill=BLOCKED)
    d.rectangle((0, z1 - grow, W, H), fill=BLOCKED)
    d.rectangle((0, 0, x0 + grow, H), fill=BLOCKED)
    d.rectangle((x1 - grow, 0, W, H), fill=BLOCKED)


def flood(mask, frame, layout):
    """Flood REACHED from the ball's start, then through every ramp whose mouth was reached."""
    sx, _, sz = layout["ball_start"]
    seeds = [frame.px(sx, sz)]
    done_ramps = set()
    while seeds:
        for px, py in seeds:
            px, py = int(px), int(py)
            if 0 <= px < mask.size[0] and 0 <= py < mask.size[1] and mask.getpixel((px, py)) == FREE:
                ImageDraw.floodfill(mask, (px, py), REACHED)
        seeds = []
        # A ramp carries the ball from its mouth to its exit. If the mouth is reached, the exit is.
        for e in layout["plan"]:
            if e["kind"] != "ramp" or e["name"] in done_ramps:
                continue
            mouth = e["points"][0]
            if reached_near(mask, frame, mouth[0], mouth[2], 0.30):
                done_ramps.add(e["name"])
                exit_p = e["points"][-1]
                # The ball drops off the end and lands a little further along its heading.
                prev = e["points"][-2]
                dx, dz = exit_p[0] - prev[0], exit_p[2] - prev[2]
                L = math.hypot(dx, dz) or 1.0
                seeds.append(frame.px(exit_p[0] + dx / L * 0.25, exit_p[2] + dz / L * 0.25))


def reached_near(mask, frame, x, z, radius):
    """Is any REACHED cell within `radius` world units of (x, z)?"""
    cx, cy = frame.px(x, z)
    rr = int(frame.r(radius)) + 1
    W, H = mask.size
    for py in range(max(0, int(cy) - rr), min(H, int(cy) + rr + 1)):
        for px in range(max(0, int(cx) - rr), min(W, int(cx) + rr + 1)):
            if (px - cx) ** 2 + (py - cy) ** 2 <= rr * rr and mask.getpixel((px, py)) == REACHED:
                return True
    return False


# --- the checks ---------------------------------------------------------------------------------


def feature_targets(layout):
    """(name, x, z, reach_radius) for everything the ball has to be able to get to. A point
    feature (a rollover, a saucer) must be reachable by the ball's centre; a solid one (a bumper,
    a target, a post) must have reachable space next to its face."""
    ball_r = layout["ball_radius"]
    out = []
    for f in layout["features"]:
        if f["kind"] in ("rollover", "saucer", "drain", "kicker", "ramp_entry", "spinner"):
            out.append((f["name"], f["x"], f["z"], 0.20))
        elif f["kind"] == "plunger":
            continue
    for e in layout["plan"]:
        p = e["points"][0]
        if e["kind"] == "disc" and e["a"] > 0.0:
            out.append((e["name"], p[0], p[2], e["a"] + ball_r + 0.08))
        elif e["kind"] == "box" and not e["name"].startswith("gate"):
            out.append((e["name"], p[0], p[2], max(e["a"], e["b"]) * 0.5 + ball_r + 0.10))
        elif e["kind"] == "flipper":
            tip = flipper_tip(p, e["a"], e["b"], e["c"] > 0.5)
            mid = ((p[0] + tip[0]) * 0.5, (p[2] + tip[1]) * 0.5)
            out.append((e["name"], mid[0], mid[1], e["a"] * 0.5 + ball_r + 0.10))
        elif e["kind"] == "post":
            out.append((e["name"], p[0], p[2], e["a"] + ball_r + 0.08))
    return out


def check_reach(layout, frame, inflate, f_collider_thickness=None):
    mask = Image.new("L", (frame.w, frame.h), FREE)
    draw_obstacles(mask, frame, layout, inflate, f_collider_thickness)
    flood(mask, frame, layout)
    missing = []
    for name, x, z, radius in feature_targets(layout):
        if not reached_near(mask, frame, x, z, radius):
            missing.append(name)
    return mask, missing


def check_headroom(layout):
    """For every ramp, what does it pass over and with how much room? Returns (blocked, notes).

    Two different questions, and the first draft of this check confused them:
      - Does the ramp INTERSECT the thing? Its underside must clear the thing's top. True for
        everything, walls included.
      - Can a BALL still get to the thing? Only for things a ball visits - a saucer, a target, a
        flipper, a post's rubber - and there the ramp's underside must clear a ball standing on
        the DECK, not the thing's top: a 0.32 standup under a floor 0.35 up is intact but
        unhittable. A wall's top is never visited, so a habitrail skimming a divider by 0.03 is
        fine, and reporting it as "roofed" hid the real faults among false ones.
    """
    ball_d = layout["ball_diameter"]
    obstacles = []      # (name, x, z, footprint radius, height, ball visits it)
    for e in layout["plan"]:
        p = e["points"][0]
        if e["kind"] == "post":
            obstacles.append((e["name"], p[0], p[2], e["a"], e["b"], True))
        elif e["kind"] == "disc":
            obstacles.append((e["name"], p[0], p[2], e["b"], e["c"], True))
        elif e["kind"] == "box":
            obstacles.append((e["name"], p[0], p[2], max(e["a"], e["b"]) * 0.5, e["c"], True))
        elif e["kind"] == "flipper":
            tip = flipper_tip(p, e["a"], e["b"], e["c"] > 0.5)
            obstacles.append((e["name"], (p[0] + tip[0]) / 2, (p[2] + tip[1]) / 2, e["a"] / 2, 0.26, True))
        elif e["kind"] == "wall":
            for q in sample_polyline(e["points"], 0.10):
                obstacles.append((e["name"], q[0], q[2], e["a"] * 0.5, e["b"], False))
    blocked, notes = [], []
    for ramp in (e for e in layout["plan"] if e["kind"] == "ramp"):
        half = ramp["a"] * 0.5
        worst = {}
        for px, py, pz in sample_polyline(ramp["points"], 0.03):
            if ramp_is_low(py, ramp["c"], ball_d):
                continue        # part of the deck here; the flood handles it
            underside = py - ramp["c"]
            for name, ox, oz, orad, oh, f_visited in obstacles:
                if math.hypot(px - ox, pz - oz) < orad + half:
                    clearance = underside - oh              # over the thing itself
                    air = underside - ball_d if f_visited else None   # over a ball beside it
                    if name not in worst or clearance < worst[name][0]:
                        worst[name] = (clearance, air, px, py, pz)
        for name, (clearance, air, px, py, pz) in sorted(worst.items(), key=lambda kv: kv[1][0]):
            where = "at (%.2f, %.2f, %.2f)" % (px, py, pz)
            if clearance < 0.0:
                blocked.append("%-11s INTERSECTS %-20s by %.3f %s" % (ramp["name"], name, -clearance, where))
            elif air is not None and air < 0.0:
                blocked.append("%-11s roofs %-20s: underside %.2f, a ball is %.2f %s" % (
                    ramp["name"], name, py - ramp["c"], ball_d, where))
            elif air is not None and air < ball_d * (HEADROOM_BALLS - 1.0):
                notes.append("%-11s over %-20s %.2f balls of air above the deck %s" % (
                    ramp["name"], name, (py - ramp["c"]) / ball_d, where))
    return blocked, notes


# --- the picture -------------------------------------------------------------------------------


def render(layout, frame, mask_fit, mask_room, out_path, f_collider_thickness=None):
    im = Image.new("RGB", (frame.w, frame.h), (14, 27, 42))
    d = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("consola.ttf", 11)
    except OSError:
        font = ImageFont.load_default()

    # Reachability underlay from the two masks: red = free but unreachable with a bare ball,
    # yellow = reachable with a bare ball but not with 1.5 balls, green = comfortable.
    fit = mask_fit.load()
    room = mask_room.load()
    px_im = im.load()
    for y in range(frame.h):
        for x in range(frame.w):
            a = fit[x, y]
            if a == REACHED:
                px_im[x, y] = (40, 120, 70) if room[x, y] == REACHED else (150, 130, 40)
            elif a == FREE:
                px_im[x, y] = (150, 45, 45)

    # One-unit grid, faint.
    play = layout["play"]
    for gx in range(int(math.floor(play["min_x"])) - 1, int(math.ceil(play["max_x"])) + 2):
        x0, _ = frame.px(gx, 0)
        d.line((x0, 0, x0, frame.h), fill=(30, 45, 62))
    for gz in range(int(math.floor(play["min_z"])) - 1, int(math.ceil(play["max_z"])) + 2):
        _, y0 = frame.px(0, gz)
        d.line((0, y0, frame.w, y0), fill=(30, 45, 62))

    # The walls as drawn, and (optionally) as the collider plan would have them.
    for e in layout["plan"]:
        if e["kind"] != "wall":
            continue
        pts = [frame.px(p[0], p[2]) for p in e["points"]]
        if f_collider_thickness:
            d.line(pts, fill=(70, 80, 95), width=int(frame.r(f_collider_thickness)), joint="curve")
        d.line(pts, fill=(200, 205, 215), width=max(2, int(frame.r(e["a"]))), joint="curve")

    for e in layout["plan"]:
        p = e["points"][0]
        if e["kind"] == "post":
            cx, cy = frame.px(p[0], p[2])
            rr = frame.r(e["a"])
            d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=(230, 230, 235), outline=(0, 0, 0))
        elif e["kind"] == "disc":
            cx, cy = frame.px(p[0], p[2])
            rb, ra = frame.r(e["b"]), frame.r(e["a"])
            colour = (230, 110, 50) if e["a"] > 0 else (40, 130, 130)
            d.ellipse((cx - rb, cy - rb, cx + rb, cy + rb), outline=colour, width=2)
            if e["a"] > 0:
                d.ellipse((cx - ra, cy - ra, cx + ra, cy + ra), fill=colour)
            else:
                d.ellipse((cx - rb * 0.7, cy - rb * 0.7, cx + rb * 0.7, cy + rb * 0.7), fill=(0, 0, 0))
        elif e["kind"] == "box":
            corners = [frame.px(x, z) for x, z in box_corners(p, e["a"], e["b"], e["d"])]
            d.polygon(corners, fill=(60, 170, 170) if "standup" in e["name"] else (235, 230, 210))
        elif e["kind"] == "flipper":
            rest = flipper_tip(p, e["a"], e["b"], e["c"] > 0.5)
            up_angle = e["b"] + 64.0 if e["a"] > 0.7 else e["b"] + 62.0
            up = flipper_tip(p, e["a"], up_angle, e["c"] > 0.5)
            w = max(2, int(frame.r(e["d"])))
            d.line([frame.px(p[0], p[2]), frame.px(*rest)], fill=(235, 90, 30), width=w)
            d.line([frame.px(p[0], p[2]), frame.px(*up)], fill=(120, 60, 30), width=2)
        elif e["kind"] == "ramp":
            pts = e["points"]
            for i in range(len(pts) - 1):
                a, b = pts[i], pts[i + 1]
                low = ramp_is_low(a[1], e["c"], layout["ball_diameter"])
                colour = (120, 180, 220) if low else (90, 120, 160)
                pa, pb = frame.px(a[0], a[2]), frame.px(b[0], b[2])
                if low:
                    d.line([pa, pb], fill=colour, width=int(frame.r(e["a"])))
                else:
                    # dashed: in the air
                    n = max(1, int(math.dist(pa, pb) / 8))
                    for k in range(0, n, 2):
                        t0, t1 = k / n, min(1.0, (k + 1) / n)
                        d.line([(pa[0] + (pb[0] - pa[0]) * t0, pa[1] + (pb[1] - pa[1]) * t0),
                                (pa[0] + (pb[0] - pa[0]) * t1, pa[1] + (pb[1] - pa[1]) * t1)],
                               fill=colour, width=3)
            for pnt in pts:
                cx, cy = frame.px(pnt[0], pnt[2])
                d.text((cx + 3, cy - 6), "%.2f" % pnt[1], fill=(150, 190, 230), font=font)

    # Feature labels, and the ball where it starts.
    for f in layout["features"]:
        cx, cy = frame.px(f["x"], f["z"])
        d.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), fill=(255, 255, 255))
        d.text((cx + 4, cy - 5), f["name"], fill=(200, 235, 240), font=font)
    bx, _, bz = layout["ball_start"]
    cx, cy = frame.px(bx, bz)
    rr = frame.r(layout["ball_radius"])
    d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=(240, 240, 250), outline=(0, 0, 0))

    # Scale: one ball, and one unit.
    x0, y0 = 10, frame.h - 18
    d.rectangle((x0, y0, x0 + frame.r(layout["ball_diameter"]), y0 + 6), fill=(255, 255, 255))
    d.text((x0, y0 - 14), "1 ball", fill=(255, 255, 255), font=font)
    d.rectangle((x0 + 60, y0, x0 + 60 + SCALE, y0 + 6), fill=(180, 180, 180))
    d.text((x0 + 60, y0 - 14), "1 unit", fill=(180, 180, 180), font=font)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    im.save(out_path)
    return out_path


# --- main --------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", default=URL)
    ap.add_argument("--json", help="read a saved pinball_layout result instead of asking the app")
    ap.add_argument("--out", default=os.path.join("apps", "pinball", "build", "plan.png"))
    ap.add_argument("--collider", type=float, default=None,
                    help="pretend every wall is this thick (e.g. 0.6, the collider plan) and see "
                         "what closes")
    ap.add_argument("--save-json", help="also save the layout the app returned, for --json later")
    args = ap.parse_args()

    layout = json.load(open(args.json)) if args.json else fetch_layout(args.url)
    if args.save_json:
        json.dump(layout, open(args.save_json, "w"), indent=1)

    frame = Frame(layout["play"])
    ball_r = layout["ball_radius"]
    ball_d = layout["ball_diameter"]

    print("Orbit Outpost plan check - ball %.2f across, %i shapes on the deck%s" % (
        ball_d, len(layout["plan"]),
        ", every wall pretended %.2f thick" % args.collider if args.collider else ""))

    mask_fit, missing_fit = check_reach(layout, frame, ball_r, args.collider)
    mask_room, missing_room = check_reach(layout, frame, ball_r * ROOM_BALLS, args.collider)
    blocked, notes = check_headroom(layout)

    problems = 0
    if missing_fit:
        problems += len(missing_fit)
        print("\nUNREACHABLE - a ball from the plunger can never get here:")
        for name in missing_fit:
            print("   " + name)
    tight = [n for n in missing_room if n not in missing_fit]
    if tight:
        print("\nTIGHT - reachable, but not by a ball %.1f diameters across (the margin a real "
              "lane has):" % ROOM_BALLS)
        for name in tight:
            print("   " + name)
    if blocked:
        problems += len(blocked)
        print("\nROOFED - a ramp intersects this, or leaves no room for a ball beside it:")
        for line in blocked:
            print("   " + line)
    if notes:
        print("\nLOW - a ramp passes over this with less than %.1f balls of air above the deck:"
              % HEADROOM_BALLS)
        for line in notes:
            print("   " + line)

    if "clearances" in layout:
        print("\nderived clearances (from the app):")
        for k, v in layout["clearances"].items():
            print("   %-24s %.3f" % (k, v))

    out = render(layout, frame, mask_fit, mask_room, args.out, args.collider)
    print("\nplan drawn to %s" % out)
    if problems:
        print("%i problem(s)." % problems)
        return 1
    print("clear - every feature is reachable and nothing is roofed over.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
