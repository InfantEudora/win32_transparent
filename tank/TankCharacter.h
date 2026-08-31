#ifndef _TANK_CHARACTER_H_
#define _TANK_CHARACTER_H_

#include "Vehicle.h"
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
        t.max_force = wheel.max_force > 0.0f ? wheel.max_force : max_wheel_force;
        t.max_point_speed = wheel.max_point_speed > 0.0f ? wheel.max_point_speed : max_point_speed;
        t.friction_coefficient = wheel.friction_coefficient > 0.0f ? wheel.friction_coefficient : friction_coefficient;
        return t;
    }

    //Default rolling radius, derived once from the tank_wheel asset's own mesh extents (see
    //ApplicationTank::Init), not hand-tuned. Used for any wheel that doesn't override it, and
    //it is real geometry now rather than only a visual spin rate: it sets how far each ray
    //reaches and how high above the terrain the hub rests. 0 degrades gracefully to the old
    //point-contact model, with the wheels visually static as before.
    float wheel_radius = 0.0f;

    //Soft cap (m/s): stop adding more drive force once real physics velocity reaches this.
    float top_speed = 1.0f;

    //Engine force is split evenly across however many of one side's wheels are grounded, so
    //total available thrust per side stays ~engine_force regardless of how many are touching
    //down. Braking still applies once at the hull's centre, same as before - it's a resistive
    //force against whatever the hull's actual velocity is, not tied to any one wheel.
    float engine_force = 2000.0f; //Newtons
    float brake_force = 3000.0f;  //Newtons

    //Suspension tuning - see UpdatePhysicsState for how these become a per-wheel spring+damper
    //force via WheelSuspension::UpdateContact. Defaults for any wheel that doesn't override them
    //(Wheel::rest_length/travel/etc, resolved through ResolveTuning above).
    //
    //rest_length is the anchor-to-HUB distance at full extension, not anchor-to-ground: the
    //wheel's own radius sits below the hub on top of it, so a wheel touches down when its
    //anchor is (rest_length + radius) above the terrain. ApplicationTank::Init sets it from the
    //tank_tracks mesh's geometry MINUS the wheel radius for exactly that reason, which leaves
    //the hull floating at the same height it always did while the tread now meets the ground
    //instead of the axle sinking to it.
    float suspension_rest_length = 0.15f; //metres from the anchor to the hub at full extension
    float suspension_travel = 0.08f;      //extra compressible range beyond rest_length
    float suspension_stiffness = 6000.0f; //N per metre of compression
    //900 before, which was past the point where an explicit integrator can represent it at all.
    //A velocity-proportional force -c*v integrated explicitly gives v_next = v * (1 - c*dt/m),
    //so it is only stable while c*dt/m < 2, and only free of ringing while c*dt/m < 1. Every
    //grounded wheel contributes its own c to the same hull, so what matters is the TOTAL:
    //
    //    c_per_wheel < m / (num_wheels * dt) = 82 / (10 * 0.02) = ~410
    //
    //At 900 the total was 9000 N/(m/s), i.e. c*dt/m = 2.20 - over the stability limit, so the
    //damping term grew tick over tick instead of decaying. Measured on the way down: a contact
    //compressed by 0.0006m (a 3.6 N spring term) produced a 1800 N damping term, and the ten of
    //them together applied 16025 N to a hull that weighs 804 N - 20x its own weight in a single
    //tick, which threw it 6 m/s straight up. The result looked like a bouncy suspension; it was
    //actually the integrator diverging, and no amount of spring tuning would have fixed it.
    //
    //400 also lands just under critical damping for the assembly as a whole
    //(c_crit = 2*sqrt(k_total*m) = 2*sqrt(60000*82) = ~4440 total, ~444 per wheel), so the
    //physical and the numerical criterion happen to agree here. If num_wheels, the mass, or
    //the tick rate change, rerun the division above - this value is not independent of them.
    float suspension_damping = 400.0f;    //N per (m/s) of compression rate
    //Hard per-wheel force ceiling - a last-resort sanity check, not a routine limiter: with
    //point_velocity's magnitude already clamped in WheelSuspension::UpdateContact, the
    //legitimate maximum is provably compression_max*stiffness + clamp*damping =
    //0.23*6000 + 2.0*900 = 3180N, so this only ever engages if stiffness/damping/travel are
    //tuned upward later without updating it too. Set too low once before (800N) and it
    //silently ate the damping force needed to arrest a normal landing impact, leaving the hull
    //bouncing indefinitely instead of settling - a wheel that's "grounded but never applying
    //real force" looks identical to one that's airborne from the outside, which made that bug
    //look like a driving/input bug.
    float max_wheel_force = 4000.0f; //Newtons
    //Subject to the same kind of explicit-integration limit as suspension_damping above, but
    //to a TIGHTER one, and this is the subtlety that cost the most time here. Suspension force
    //is vertical and the wheels are spread horizontally, so it damps a linear mode and the
    //bound is against the hull's MASS. Lateral force is horizontal and the wheels sit 0.279m
    //BELOW the centre of mass, so it feeds a roll mode, and the bound is against the hull's
    //roll INERTIA, which is far smaller in the units that matter:
    //
    //    c_per_wheel * num_wheels * r_y^2 * dt / I_roll  <  1
    //
    //Measured rather than derived, since I_roll comes out of rp3d's collider integration: at
    //400 a level, fully-grounded hull diverged with lateral force alternating sign every tick
    //and growing x1.77 (+0.1, -0.1, +0.2, -0.3, +0.5, -1.0, +1.7, -3.0, +5.3, -9.5, +16.7...),
    //which puts the ratio above at 2.77 and needs roughly a 3x reduction. 120 lands it near
    //0.83, i.e. monotonic decay with no alternation at all. 4000 - the original value - was
    //past even the loose linear bound by an order of magnitude.
    //
    //Lowering this costs much less grip than it looks like it should, because since the
    //friction budget below exists this value is only the SLOPE of the approach to saturation,
    //not the ceiling. Maximum grip is friction_coefficient * normal load either way; all that
    //changes is how much slip it takes to get there (~0.67 m/s at 120, vs ~0.2 m/s at 400).
    //
    //The robust fix, if this ever needs revisiting: solve the lateral force implicitly - pick
    //the force that exactly cancels this contact's slip velocity over one timestep
    //(-slip * mass_share / dt), then clip THAT to the friction budget. It is unconditionally
    //stable at any timestep and removes this whole tuning cliff, at the cost of needing the
    //real dt (currently hardcoded to 0.02 at the top of UpdatePhysicsState) plumbed in.
    float lateral_friction = 120.0f;      //N per (m/s) of sideways slip, per grounded wheel

    //Coulomb friction coefficient. A track can only transmit force to the ground in proportion
    //to how hard it is pressed into it, so each contact's drive/grip/lateral forces share a
    //single budget of friction_coefficient * spring_force - see the friction circle in
    //UpdatePhysicsState. Without that coupling, a wheel in rebound reporting spring_force = 0
    //still applied full lateral force (traced at 1660 N from a wheel carrying zero vertical
    //load), which is not merely unphysical: it is self-reinforcing, because the wheels a roll
    //UNLOADS were exactly the ones still pushing hardest against being reloaded.
    //
    //1.0 is high for real steel-on-dirt (0.5-0.7 would be typical) but the tracks are wide and
    //this keeps the hull from sliding on the terrain's slopes. Note the whole vehicle is now
    //traction-limited rather than engine-limited: total available thrust is roughly
    //friction_coefficient * weight = 804 N against engine_force's 2000 N, which is realistic
    //for a tracked vehicle and still reaches top_speed in about a tenth of a second.
    float friction_coefficient = 1.0f;

    //The other two clamps, previously function-local consts in UpdatePhysicsState. Out here
    //because whether they engage is the central tuning question, not an implementation detail:
    //max_point_speed's saturation is now reported per wheel (Wheel::point_speed) so it can
    //actually be observed rather than inferred, and both want to be adjustable alongside the
    //stiffness/damping/friction values above when tuning.
    float max_point_speed = 2.0f; //m/s ceiling on a contact point's velocity, applied once
                                  //before any force derives from it
    float max_roll_speed = 3.0f;  //rad/s ceiling on the hull's angular speed, applied after all
                                  //of a tick's forces are in

    //How strongly steering_position biases each side's drive command relative to the other.
    //1.0 = full authority: opposite full-power tracks (gas_pedal 0, steering_position +-1)
    //pivot the hull in place, same as a real tank turning on its tracks.
    float steer_authority = 1.0f;

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
