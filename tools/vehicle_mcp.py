#!/usr/bin/env python
"""Drive the Tank app's MCP tools over stdio from a script.

Spawns wind.exe (built with APP=Tank) with its stdin/stdout piped, talks JSON-RPC to the
embedded MCP server (see docs/mcp_server.md), runs a scenario deterministically by pausing the
physics and stepping it tick by tick, and kills the process again at the end. stdio rather than
HTTP so it works whether or not port 8765 is free (another app in this project may hold it).

    python tools/vehicle_mcp.py list
    python tools/vehicle_mcp.py telemetry --vehicle buggy
    python tools/vehicle_mcp.py baseline --vehicle tank --out tools/baseline_tank.json
    python tools/vehicle_mcp.py compare tools/baseline_tank.json tools/after_tank.json

The baseline scenario settles the vehicle, drives it, brakes it and turns it, recording the
numbers that characterise the suspension and drivetrain (ride height, per-wheel compression,
speed reached, yaw rate). Run it before and after a physics change and compare.
"""
import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class App:
    """One wind.exe process and a JSON-RPC session on its stdio."""

    def __init__(self, exe=None, log=None):
        exe = exe or os.path.join(REPO, "wind.exe")
        log = log or os.path.join(REPO, "wind_stderr.log")
        self.log = open(log, "w")
        self.proc = subprocess.Popen([exe], cwd=REPO, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log, bufsize=0)
        self.next_id = 1
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    def _read_loop(self):
        for raw in self.proc.stdout:
            self.lines.put(raw.decode("utf-8", "replace"))
        self.lines.put(None)

    def _send(self, message):
        data = (json.dumps(message) + "\n").encode("utf-8")
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def call(self, method, params=None, timeout=90.0):
        """Send a request and wait for its response. The first one blocks until the app has
        finished Init() and started the MCP reader thread (several seconds)."""
        request_id = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        self._send(message)
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("no response to %s within %.0fs" % (method, timeout))
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError("wind.exe closed its stdout (exit code %s)" % self.proc.poll())
            line = line.strip()
            if not line:
                continue
            try:
                response = json.loads(line)
            except ValueError:
                sys.stderr.write("non-JSON line on stdout: %r\n" % line[:200])
                continue
            if response.get("id") == request_id:
                if "error" in response:
                    raise RuntimeError("%s failed: %s" % (method, response["error"]))
                return response.get("result")

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self._send(message)

    def start(self):
        self.call("initialize", {})
        self.notify("notifications/initialized")
        tools = self.call("tools/list")
        return [t["name"] for t in tools["tools"]]

    def tool(self, name, **args):
        """Call a tool and return its JSON payload (tools here return one text content block
        holding JSON)."""
        result = self.call("tools/call", {"name": name, "arguments": args})
        content = result.get("content", [])
        if content and content[0].get("type") == "text":
            text = content[0]["text"]
            try:
                return json.loads(text)
            except ValueError:
                return text
        return result

    def close(self):
        try:
            self.proc.kill()
        except OSError:
            pass
        self.proc.wait(timeout=10)
        self.log.close()


# ---------------------------------------------------------------------------------------------
# Scenario helpers

def summarize(telemetry):
    """The handful of numbers worth comparing between two physics implementations."""
    wheels = telemetry.get("wheels", [])
    grounded = [w for w in wheels if w.get("grounded")]
    return {
        "position": [round(x, 3) for x in telemetry["position"]],
        "position_y": telemetry["position"][1],
        "up_y": telemetry["up"][1],
        "speed": telemetry.get("speed"),
        "forward_speed": telemetry.get("forward_speed"),
        "yaw_rate": telemetry.get("yaw_rate"),
        "wheels_grounded": telemetry.get("wheels_grounded"),
        "total_spring_force": telemetry.get("total_spring_force"),
        "weight": telemetry.get("mass_kg", 0.0) * 9.81,
        "compression": [round(w["compression"], 4) for w in wheels],
        "spring_force": [round(w["spring_force"], 1) for w in wheels],
        "wheel_angular_velocity": [round(w.get("angular_velocity", 0.0), 2) for w in wheels],
        "saturated": [w.get("friction_saturated") for w in grounded],
    }


# Per-vehicle scenario parameters. The test ground plane is 20 x 20 m around the origin (the
# heightmap terrain starts beyond its -z edge) and the buggy does 6 m/s flat out, so its phases
# are shorter and gentler than the tank's or it drives off the edge mid-run - at half throttle
# for 100 ticks plus a coast it already covered 12 m.
# The buggy turns LEFT: a right-hand circle from its spawn point runs it into the crane test rig
# parked at (3.5, 0, 2), which showed up as an unexplained 28 m/s^2 deceleration mid-coast.
SCENARIO = {
    "tank": {"drive_amount": 1.0, "drive_steps": 150, "coast_steps": 100, "brake_steps": 60,
             "turn_prestart_steps": 0, "turn_steps": 100, "turn_direction": "right"},
    "buggy": {"drive_amount": 0.3, "drive_steps": 75, "coast_steps": 50, "brake_steps": 60,
              "turn_prestart_steps": 40, "turn_steps": 100, "turn_direction": "left"},
}

# Ticks to let a vehicle come to rest after a reset before a phase starts. It is teleported to
# its spawn pose, slightly above the ground, and has to drop onto its suspension first.
RESETTLE_STEPS = 150


def baseline(app, vehicle, settle_steps=300):
    """Settle, drive, coast, brake, turn - each phase from a fresh reset to the spawn pose, so
    the phases do not depend on each other and the vehicle never runs out of ground plane.
    Physics is paused throughout and advanced by tank_step, so every run simulates exactly the
    same ticks regardless of machine speed."""
    prm = SCENARIO[vehicle]
    out = {"vehicle": vehicle, "settle_steps": settle_steps, "scenario": prm}
    app.tool("tank_pause", vehicle=vehicle, paused=True)

    def reset(steps=RESETTLE_STEPS):
        app.tool("tank_reset", vehicle=vehicle)
        return app.tool("tank_step", vehicle=vehicle, num_steps=steps)

    def drive(amount):
        app.tool("tank_drive", vehicle=vehicle, direction="forward", amount=amount, duration_ms=15000)

    def stop():
        app.tool("tank_drive", vehicle=vehicle, direction="stop")

    # Settle from the spawn pose.
    t = reset(settle_steps)
    out["settled"] = summarize(t)
    out["tuning"] = t.get("tuning")
    out["mass_kg"] = t.get("mass_kg")

    # Drive forward. The throttle is latched for longer than the steps take (they run at 50
    # ticks/s of real time while paused-stepping) and released explicitly afterwards.
    reset()
    drive(prm["drive_amount"])
    t = app.tool("tank_step", vehicle=vehicle, num_steps=prm["drive_steps"])
    out["driving"] = summarize(t)
    pos_before_coast = t["position"]

    # Coast: release everything and see how far it rolls on its own.
    stop()
    t = app.tool("tank_step", vehicle=vehicle, num_steps=prm["coast_steps"])
    out["coast"] = summarize(t)
    out["coast_distance"] = sum((a - b) ** 2 for a, b in zip(t["position"], pos_before_coast)) ** 0.5

    # Brake from speed.
    reset()
    drive(prm["drive_amount"])
    app.tool("tank_step", vehicle=vehicle, num_steps=prm["drive_steps"])
    app.tool("tank_drive", vehicle=vehicle, direction="brake", amount=1.0, duration_ms=15000)
    t = app.tool("tank_step", vehicle=vehicle, num_steps=prm["brake_steps"])
    out["braked"] = summarize(t)
    stop()

    # Turn. The tank pivots on its tracks from standstill; the buggy has to be rolling first.
    reset()
    if prm["turn_prestart_steps"] > 0:
        drive(prm["drive_amount"])
        app.tool("tank_step", vehicle=vehicle, num_steps=prm["turn_prestart_steps"])
    app.tool("tank_steer", vehicle=vehicle, direction=prm["turn_direction"], amount=1.0, duration_ms=15000)
    t = app.tool("tank_step", vehicle=vehicle, num_steps=prm["turn_steps"])
    out["turning"] = summarize(t)
    out["turning"]["direction"] = prm["turn_direction"]
    out["turning"]["steering_position"] = t.get("steering_position")
    out["turning"]["heading"] = t["forward"]
    stop()
    t = app.tool("tank_step", vehicle=vehicle, num_steps=150)
    out["after_turn_rest"] = summarize(t)
    out["final_telemetry"] = t
    return out


def compare(a, b):
    """Print the summary sections of two baseline files side by side."""
    keys = ["settled", "driving", "coast", "braked", "turning", "after_turn_rest"]
    for key in keys:
        sa, sb = a.get(key, {}), b.get(key, {})
        print("== %s" % key)
        for field in sorted(set(sa) | set(sb)):
            va, vb = sa.get(field), sb.get(field)
            flag = "" if va == vb else "   <-- differs"
            print("  %-24s %-40s %-40s%s" % (field, _fmt(va), _fmt(vb), flag))
    print("coast_distance: %s vs %s" % (a.get("coast_distance"), b.get("coast_distance")))


def _fmt(v):
    if isinstance(v, float):
        return "%.4f" % v
    if isinstance(v, list):
        return "[" + ", ".join(_fmt(x) for x in v) + "]"
    return str(v)


# ---------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=None, help="path to wind.exe (default: repo root)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="start the app, list its MCP tools, quit")

    p = sub.add_parser("telemetry", help="settle briefly, print telemetry, quit")
    p.add_argument("--vehicle", default="tank", choices=["tank", "buggy"])
    p.add_argument("--steps", type=int, default=100)

    p = sub.add_parser("baseline", help="run the settle/drive/brake/turn scenario")
    p.add_argument("--vehicle", default="tank", choices=["tank", "buggy"])
    p.add_argument("--settle-steps", type=int, default=300)
    p.add_argument("--out", default=None, help="write the results as JSON here")

    p = sub.add_parser("compare", help="diff two baseline JSON files")
    p.add_argument("a")
    p.add_argument("b")

    args = parser.parse_args()

    if args.command == "compare":
        compare(json.load(open(args.a)), json.load(open(args.b)))
        return

    app = App(exe=args.exe)
    try:
        tools = app.start()
        if args.command == "list":
            print("\n".join(tools))
        elif args.command == "telemetry":
            app.tool("tank_pause", vehicle=args.vehicle, paused=True)
            t = app.tool("tank_step", vehicle=args.vehicle, num_steps=args.steps)
            print(json.dumps(t, indent=1))
        elif args.command == "baseline":
            result = baseline(app, args.vehicle, args.settle_steps)
            text = json.dumps(result, indent=1)
            if args.out:
                with open(args.out, "w") as f:
                    f.write(text)
                print("wrote %s" % args.out)
            summary = {k: v for k, v in result.items() if k != "final_telemetry"}
            print(json.dumps(summary, indent=1))
    finally:
        app.close()


if __name__ == "__main__":
    main()
