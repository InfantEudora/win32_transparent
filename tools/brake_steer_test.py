#!/usr/bin/env python
"""Regression test: braking while steering turns the buggy the WRONG WAY.

Reported behaviour (2026-09-08), with the buggy set up as:

    suspension      3 Hz on every wheel
    brake bias      1.0  - only the FRONT wheels brake
    power split     0.0  - rear wheel drive

accelerate straight to ~3 m/s, release the throttle, then brake and steer at the same
time. The car turns hard AWAY from the steering input. Expected is either a mostly
straight slide or a gentle turn towards the steer; what happens is a ~1.2 rad/s
(~70 deg/s) rotation the other way.

WHY (measured by --sweep, see the numbers it prints):

The vehicle constraint splits each tire's grip into two directions fixed to the WHEEL's
own heading: longitudinal along where the tire points, lateral across it. The two share
one friction budget through a friction ellipse, and the longitudinal direction is solved
first (rp3d SolveVehicleSystem::solveLongitudinalFriction, then solveLateralFriction).

With all the braking on the front axle, the brake asks those two tires for more force
than their contact patch can transmit, so the longitudinal direction consumes the whole
budget and `remainingGripFactor` leaves the lateral direction nothing. The front tires
then generate EXACTLY ZERO sideways force while slipping at 45-60 degrees: they are
asked to corner and have no grip left to do it with.

That alone would just be understeer - a locked front wheel slides straight. The reversal
comes from where the remaining force points. It stays pinned to the steered wheel's
heading axis, and a braking force along a heading rotated by the steer angle d has a
sideways component of magnitude F*sin(d) pointing OUT of the turn. Applied at the front
axle, ahead of the centre of mass, that yaws the car away from the steer.

A real sliding tire's friction opposes its SLIP VELOCITY - which here points almost
straight down the road - so it would have almost no sideways component and produce almost
no reverse yaw. Pinning the force to the wheel's heading regardless of how much the tire
is actually sliding is the modelling error.

The sweep shows the sign flip tracking the front tires' spare grip exactly:

    bias 1.00  peak yaw -1.24   front lateral   0.0 N   slip 60.0 deg
    bias 0.80  peak yaw -0.73   front lateral  22.6 N   slip 45.6 deg
    bias 0.60  peak yaw +0.75   front lateral  36.2 N   slip  2.7 deg
    bias 0.50  peak yaw +0.95   front lateral  18.0 N   slip  2.9 deg

Usage:
    python tools/brake_steer_test.py            # the reported case, exits 1 if reversed
    python tools/brake_steer_test.py --sweep    # brake-bias sweep with the diagnosis
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vehicle_mcp import App

TARGET_SPEED = 3.0      # m/s to reach before braking, as described in the report
SETTLE_STEPS = 50       # ticks to let the suspension settle after a reset
BRAKE_STEPS = 60        # ticks to hold brake+steer for, in 5-tick slices


def yaw_rate(telemetry):
    """Angular velocity about the car's OWN up axis. Steering left is positive."""
    up = telemetry.get("up", [0, 1, 0])
    angular = telemetry.get("angular_velocity", [0, 0, 0])
    return sum(a * b for a, b in zip(angular, up))


def front_left(telemetry):
    for wheel in telemetry.get("wheels", []):
        if wheel.get("front") and wheel.get("side") == "left":
            return wheel
    return {}


def brake_and_steer(app, brake_bias, steer="left"):
    """Accelerate to TARGET_SPEED, then brake and steer together. Returns
    (peak yaw rate, telemetry at that peak, speed when braking began)."""
    app.tool("buggy_tune", suspension_hz=3.0, brake_split_front=brake_bias,
             power_split_front=0.0)
    app.tool("tank_pause", vehicle="buggy", paused=True)
    app.tool("tank_reset", vehicle="buggy")
    app.tool("tank_step", vehicle="buggy", num_steps=SETTLE_STEPS)

    # Straight-line acceleration. The hold is sized generously and released explicitly
    # below - a hold that outlives the steps it was meant for silently contaminates
    # whatever runs next.
    app.tool("tank_drive", vehicle="buggy", direction="forward", amount=1.0,
             duration_ms=4000)
    telemetry = None
    for _ in range(40):
        telemetry = app.tool("tank_step", vehicle="buggy", num_steps=5)
        if telemetry["forward_speed"] >= TARGET_SPEED:
            break
    entry_speed = telemetry["forward_speed"]

    # Release the throttle FIRST: "stop" calls ReleaseSynthetic, which drops every
    # scripted hold, so applying it after the brake/steer holds would wipe them out.
    app.tool("tank_drive", vehicle="buggy", direction="stop")
    app.tool("tank_drive", vehicle="buggy", direction="brake", amount=1.0,
             duration_ms=3000)
    app.tool("tank_steer", vehicle="buggy", direction=steer, amount=1.0,
             duration_ms=3000)

    peak, peak_telemetry = 0.0, telemetry
    for _ in range(BRAKE_STEPS // 5):
        telemetry = app.tool("tank_step", vehicle="buggy", num_steps=5)
        if abs(yaw_rate(telemetry)) > abs(peak):
            peak, peak_telemetry = yaw_rate(telemetry), telemetry
    return peak, peak_telemetry, entry_speed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sweep", action="store_true",
                        help="sweep the brake bias to show where the yaw flips sign")
    parser.add_argument("--exe", default=None, help="path to wind.exe")
    args = parser.parse_args()

    app = App(exe=args.exe)
    try:
        app.start()
        print("Steering LEFT: a POSITIVE yaw rate is into the turn, NEGATIVE is away from it.\n")

        if args.sweep:
            print("  bias | peak yaw | front lat |  slip  | long/budget")
            print("  " + "-" * 52)
            for bias in (1.0, 0.8, 0.6, 0.5):
                peak, telemetry, _ = brake_and_steer(app, bias)
                wheel = front_left(telemetry)
                budget = wheel.get("friction_budget", 0.0)
                used = abs(wheel.get("longitudinal_force", 0.0)) / budget if budget > 0 else 0.0
                print("  %4.2f | %+8.4f | %7.1f N | %5.1f deg |    %.3f" % (
                    bias, peak, wheel.get("lateral_force", 0.0),
                    wheel.get("lateral_slip_angle_deg", 0.0), used))
            return 0

        # The reported case.
        peak, telemetry, entry_speed = brake_and_steer(app, 1.0)
        wheel = front_left(telemetry)
        print("brake bias 1.0 (front only), RWD, 3 Hz suspension")
        print("  braking from       %.2f m/s" % entry_speed)
        print("  peak yaw rate      %+.4f rad/s (%+.1f deg/s)" % (peak, peak * 180.0 / 3.14159265))
        print("  front-left tire    longitudinal %.1f N, lateral %.1f N" % (
            wheel.get("longitudinal_force", 0.0), wheel.get("lateral_force", 0.0)))
        print("  front-left slip    %.1f deg" % wheel.get("lateral_slip_angle_deg", 0.0))

        if peak < -0.1:
            print("\nFAIL: steering left yawed the car RIGHT at %.2f rad/s." % peak)
            print("      The front tires carry all the braking, so their lateral force is")
            print("      starved to %.1f N while slipping %.1f deg - see this file's header." % (
                wheel.get("lateral_force", 0.0), wheel.get("lateral_slip_angle_deg", 0.0)))
            return 1
        print("\nPASS: the car yawed into the steer (%+.4f rad/s)." % peak)
        return 0
    finally:
        app.close()


if __name__ == "__main__":
    sys.exit(main())
