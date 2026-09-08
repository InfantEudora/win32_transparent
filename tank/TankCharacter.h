#ifndef _TANK_CHARACTER_H_
#define _TANK_CHARACTER_H_

#include "Vehicle.h"
#include "type_helpers.h" //clamp(), used by TrackInput below
#include <windows.h>
#include <vector>

class TankCharacter;

class TankCharacter : public Vehicle{
public:
    TankCharacter();
    ~TankCharacter();

    void UpdatePhysicsState() override;

    //Teleports the hull to pos/rot as Vehicle::ResetState does, then also zeroes the turret's
    //own reset-worthy state (recoil offset) that Vehicle knows nothing about.
    void ResetState(const vec3& pos, const quat& rot) override;

    //Lays out wheels_per_side road wheels evenly along each track, from -half_length to
    //+half_length in local Z, at +-track_offset_x in local X, with their suspension anchored
    //mount_height above the hull origin in local Y and hanging straight down from it. Called
    //once from ApplicationTank::Init(), after the hull's own (mass/incidental-collision-only)
    //collider.
    void SetupWheels(float track_offset_x, float half_length, float mount_height, int wheels_per_side);

    //Adds one raised wheel per side - the idler (front) or drive sprocket (rear) that sit above
    //the road-wheel band in the tank_tracks mesh. Appends rather than clears, so call after
    //SetupWheels, once per raised wheel position.
    //
    //Takes the wheel's resting HUB HEIGHT rather than its anchor, and derives the anchor from
    //it (anchor = hub - axis*rest_length): where these wheels have to sit is read off the
    //mesh's own humps, so letting the caller state that directly is what removes the old
    //hand-computed offset that existed purely to cancel the suspension's hang. radius <= 0
    //falls back to wheel_radius, as everywhere else.
    void AddRaisedWheel(float track_offset_x, float z, float hub_rest_height, float rest_length,
                        float travel, const vec3& axis, float radius = 0.0f);

    //Turns a Wheel's per-field 0s into real numbers by falling back to the tank's own shared
    //defaults below - see Wheel's own comment for why the fields are 0-means-inherit in the
    //first place. Every read of per-wheel geometry/suspension tuning (physics, visuals, debug
    //UI, telemetry) goes through this (via Vehicle::WheelRadius/WheelRestLength/WheelTravel),
    //so a 0 override means "use the default" identically everywhere.
    WheelTuning ResolveTuning(const Wheel& wheel) const override{
        WheelTuning t;
        t.radius = wheel.radius > 0.0f ? wheel.radius : wheel_radius;
        t.rest_length = wheel.rest_length > 0.0f ? wheel.rest_length : suspension_rest_length;
        t.travel = wheel.travel > 0.0f ? wheel.travel : suspension_travel;
        t.stiffness = wheel.stiffness > 0.0f ? wheel.stiffness : suspension_stiffness;
        t.damping = wheel.damping > 0.0f ? wheel.damping : suspension_damping;
        t.friction_coefficient = wheel.friction_coefficient > 0.0f ? wheel.friction_coefficient : friction_coefficient;
        t.lateral_friction = wheel.lateral_friction > 0.0f ? wheel.lateral_friction : lateral_friction;
        t.sliding_friction_ratio = wheel.sliding_friction_ratio > 0.0f ? wheel.sliding_friction_ratio : sliding_friction_ratio;
        t.peak_slip_ratio = wheel.peak_slip_ratio > 0.0f ? wheel.peak_slip_ratio : peak_slip_ratio;
        t.mass = wheel.mass > 0.0f ? wheel.mass : wheel_mass;
        return t;
    }

    //Default rolling radius, derived once from the tank_wheel asset's own mesh extents (see
    //ApplicationTank::Init), not hand-tuned. Used for any wheel that doesn't override it, and
    //it is real geometry: it sets how far each ray reaches and how high above the terrain the
    //hub rests. 0 degrades to a point-contact model, with the wheels visually static.
    float wheel_radius = 0.0f;

    //Suspension tuning - handed to the rp3d VehicleConstraint per wheel (see Vehicle::
    //MakeWheelSettings). Defaults for any wheel that doesn't override them (Wheel::rest_length/
    //travel/etc, resolved through ResolveTuning above).
    //
    //rest_length is the anchor-to-HUB distance at full extension, not anchor-to-ground: the
    //wheel's own radius sits below the hub on top of it, so a wheel touches down when its
    //anchor is (rest_length + radius) above the terrain. ApplicationTank::Init sets it from the
    //tank_tracks mesh's geometry MINUS the wheel radius for exactly that reason, which leaves
    //the hull floating at the same height it always did while the tread meets the ground
    //instead of the axle sinking to it.
    float suspension_rest_length = 0.15f; //metres from the anchor to the hub at full extension
    float suspension_travel = 0.08f;      //extra compressible range beyond rest_length
    //6000 N/m over 12 road wheels carries the 82 kg hull at ~0.011 m of compression - the ride
    //height the wheel and track meshes were placed against, so it stays what it was.
    float suspension_stiffness = 6000.0f; //N per metre of compression
    //Damping ratio for the whole assembly is c_total / (2 sqrt(k_total m)) = 4800 / 4440 = ~1.1,
    //i.e. about critical - a landing settles in one swing without bouncing. This value has a
    //history: at 900 the OLD explicit per-tick force integration diverged (a velocity-
    //proportional force integrated explicitly is only stable while c*dt/m < 2 summed over the
    //wheels), which is what dragged it down to 400. The constraint solves the spring implicitly
    //and is stable for any value, so this is now purely a feel choice, not a stability bound.
    float suspension_damping = 400.0f;    //N per (m/s) of compression rate

    //Track drag, in Newtons, shared over the wheels: what a track resists rolling with when its
    //own lever sits at neutral, fading to nothing as that lever is pushed (see UpdatePhysicsState).
    //This is friction, not a brake input - it stands for track tension, road wheel bearings and
    //the ground being churned, all of which a real track has whether or not the driver asks for
    //anything. It does two jobs: it is what lets ONE lever forward swing the nose, because the
    //loose track drags while the driven one pushes, and it is what brings the tank to rest when
    //both levers come back to neutral. Too low and the tank coasts like a car and a single lever
    //barely turns it; too high and it feels like driving through mud and fights its own engine on
    //the way off neutral. Tune against engine_force (2000 N) and the ~804 N of available traction.
    float rolling_resistance = 150.0f;

    //Coulomb friction coefficient along the track: the most drive or brake force each contact
    //can put into the ground is this times its current normal load. 1.0 is high for real
    //steel-on-dirt (0.5-0.7 would be typical) but the tracks are wide and this keeps the hull
    //from sliding on the terrain's slopes. Note the whole vehicle is traction-limited rather
    //than engine-limited: total available thrust is roughly friction_coefficient * weight =
    //804 N against engine_force's 2000 N, which is realistic for a tracked vehicle and still
    //reaches top_speed in well under a second.
    float friction_coefficient = 1.0f;
    //Coulomb friction SIDEWAYS, i.e. how hard the tracks resist scrubbing across the ground.
    //This is the knob for how a skid-steered vehicle turns: a pivot turn drives the two tracks
    //against each other and every wheel has to slide sideways for the hull to rotate at all, so
    //the yaw torque the tracks can produce (friction_coefficient * weight * half the track
    //spacing) has to beat the yaw torque this resists with (lateral_friction * weight * the
    //wheels' mean distance from the centre). Equal coefficients would make that a near thing
    //on this hull (0.28 m against ~0.22 m); 0.5 turns it briskly without letting it drift on a
    //slope. (Before the move into rp3d this was a viscous rate whose value was an integrator
    //stability limit, not a grip choice - see Wheel::lateral_friction.)
    float lateral_friction = 0.5f;
    //How much of its grip a tire keeps once it is SLIDING rather than gripping, and the slip at
    //which grip peaks (rp3d's slidingFrictionRatio/peakSlipRatio - it scales the friction budget
    //down past the peak instead of holding it flat). This is what makes breaking traction cost
    //something: a locked wheel stops the car less well than one braked right at the limit, and a
    //spinning one pushes less hard. 1.0 restores a single coefficient that never falls off, which
    //is the behaviour from before the falloff existed - useful for an A/B.
    float sliding_friction_ratio = 0.8f;
    float peak_slip_ratio = 0.12f;

    //Last-resort safety net on the hull's roll/pitch rate, applied after each tick - see the
    //comment where it is applied in UpdatePhysicsState. Dead code in any drivable configuration.
    float max_roll_speed = 3.0f;  //rad/s ceiling on the hull's roll+pitch angular speed

    //--- Direct per-track control (test rig) ----------------------------------------------
    //Bypasses the throttle/steer mixing in UpdatePhysicsState entirely and commands each track
    //from its own stick, so a differential can be dialled in exactly rather than being inferred
    //from a mix. This is the harness for the open "won't yaw on a same-direction differential"
    //question: with it on you can hold, say, left 1.0 / right 0.6 indefinitely and watch whether
    //any yaw develops, which the mixed path cannot express cleanly (throttle 0.8 + steer 0.2
    //gets there, but clamping and the steering decay both muddy what you actually commanded).
    //
    //Off by default - normal driving is unaffected until it is switched on.
    bool direct_track_control = true;

    //Signed command per track, -1 (full reverse) to +1 (full forward), used in place of the
    //mixed left/right commands while direct_track_control is set. These are the SAME quantity
    //the mix produces, so everything downstream - the governor, track drag, the idle brake -
    //behaves identically; only where the number comes from changes.
    float left_track_input = 0.0f;
    float right_track_input = 0.0f;

    //Clears the track commands on top of the pedals/steering Vehicle releases, so switching
    //away from the tank or resetting it does not leave a track driving.
    void ReleaseInputs() override;

    //Set both track commands at once. Clamped like every other Vehicle input.
    void TrackInput(float left, float right){
        left_track_input = clamp(left,-1.0f,1.0f);
        right_track_input = clamp(right,-1.0f,1.0f);
    }

    //Turret tracking: turns towards turret_target at a constant angular speed.
    Object* turret = NULL;
    Object* turret_target = NULL;
    float turret_turn_speed = 2.0f; //Radians per second

    //Firing: kicks the barrel back along its own bore axis (springing back over subsequent
    //ticks) and gives the hull an instant shove opposite the turret's aim. Physics has no
    //ApplyImpulse, so the hull kick is a direct velocity add rather than a one-tick force -
    //a single AddWorldForceAt call integrated over one timestep would be far too weak to feel.
    void Fire();
    float recoil_kick_speed = 2.5f;            //m/s added to hull velocity, opposite the turret's aim, per shot
    float turret_recoil_kick = 0.3f;           //metres the barrel snaps back on firing
    float turret_recoil_recover_speed = 1.5f;  //metres/second the barrel springs back to rest
    float turret_recoil_offset = 0.0f;         //current backward displacement along the turret's local forward axis
    vec3 turret_rest_local_pos = {};           //turret's local position before any recoil offset, captured once
    bool turret_rest_pos_captured = false;
};

#endif
