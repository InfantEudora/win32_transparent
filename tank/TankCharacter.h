#ifndef _TANK_CHARACTER_H_
#define _TANK_CHARACTER_H_

#include "Object.h"
#include <windows.h>
#include <vector>

class TankCharacter;

//One raycast-sampled ground-contact point along a track. A spring+damper suspension force and
//this wheel's side's drive force are both applied here instead of through a single rigid
//capsule collider resting on the terrain - see TankCharacter::UpdatePhysicsState.
struct TankWheel{
    vec3 local_offset;          //mount point relative to the hull origin, in local space
    bool is_left_side = false;  //local_offset.x > 0 - matches Object::GetLeft()/ref_left's convention
    float compression = 0.0f;   //current spring compression in metres, 0 = extended/airborne
    bool grounded = false;
    float roll_angle = 0.0f;    //accumulated wheel spin (radians), unused for now - kept so a
                                 //future visual wheel Object can be driven from the same state
    Object* visual = NULL;      //optional child Object placed at local_offset for visual reference -
                                 //set up by ApplicationTank::Init, positioned once at setup for now

    //Per-tick force diagnostics. Written by TankCharacter::UpdatePhysicsState (rewritten from
    //scratch every tick, left at zero for a wheel that never reached the force stage) and read
    //by ApplicationTank::GetTankTelemetry, which reports them over MCP as a "wheels" array.
    //Nothing in the simulation reads them back. They exist because hull-level telemetry
    //(position/velocity/angular_velocity) only ever says THAT something is wrong, never which
    //contact is doing it - a wheel saturating a clamp, pushing the wrong way, or grounded but
    //silently contributing nothing all look identical from outside, which is exactly what made
    //several of the bugs documented below present as driving/input bugs instead of suspension
    //bugs. Read from the MCP thread while the physics thread writes them; unsynchronized, same
    //as the pedal inputs already are, and a torn single frame of a debug readout is harmless.
    float point_speed = 0.0f;        //m/s magnitude of this contact's velocity BEFORE the
                                     //max_point_speed clamp. If this routinely reads above that
                                     //clamp, the clamp is load-bearing - i.e. the tuning
                                     //underneath it is diverging, not merely being trimmed.
    float compression_rate = 0.0f;   //m/s into the ground along the contact normal
    float spring_force = 0.0f;       //N along the ground normal, post-clamp, as actually applied
    float drive_force = 0.0f;        //N along the hull's forward axis from engine thrust, signed
    float longitudinal_force = 0.0f; //N along forward from passive grip when undriven, signed
    float lateral_force = 0.0f;      //N along the hull's left axis from lateral_friction, signed
    float friction_budget = 0.0f;    //N, friction_coefficient * spring_force - the most this
                                     //contact can transmit to the ground in any direction
    bool friction_saturated = false; //true when the tick's combined longitudinal+lateral demand
                                     //exceeded friction_budget and had to be scaled back
};

class TankCharacter : public Object{
public:
    TankCharacter();
    ~TankCharacter();

    void UpdatePhysicsState() override;

    //Manual movement, same controls as IsoCar but without any terrain/pathing/sound coupling.
    void Accelerate(float factor);
    void Brake(float factor);
    void SteerLeft(float factor);
    void SteerRight(float factor);
    void Reverse(float factor);

    //For scripted/MCP control: a single external call can't realistically out-pace the physics
    //tick rate (gas_pedal/brake_pedal/steering_position all decay back to idle every tick unless
    //re-asserted), so these latch the equivalent input as "held" for duration_ms of real time,
    //re-asserting it every tick until the latch expires - the same effect a human's briefest key
    //tap already has, just made explicit instead of accidental.
    void HoldDrive(bool reverse, float amount, float duration_ms);
    void HoldBrake(float amount, float duration_ms);
    void HoldSteer(float signed_amount, float duration_ms); //negative = left, positive = right
    void ReleaseInputs(); //cancels all latches and releases the pedals immediately

    //Lays out wheels_per_side raycast contact points evenly along each track, from
    //-half_length to +half_length in local Z, at +-track_offset_x in local X and mount_height
    //above the hull origin in local Y. Called once from ApplicationTank::Init(), after the
    //hull's own (mass/incidental-collision-only) collider is set up.
    void SetupWheels(float track_offset_x, float half_length, float mount_height, int wheels_per_side);

    //Soft cap (m/s): stop adding more drive force once real physics velocity reaches this.
    float top_speed = 1.0f;

    //Engine force is split evenly across however many of one side's wheels are grounded, so
    //total available thrust per side stays ~engine_force regardless of how many are touching
    //down. Braking still applies once at the hull's centre, same as before - it's a resistive
    //force against whatever the hull's actual velocity is, not tied to any one wheel.
    float engine_force = 2000.0f; //Newtons
    float brake_force = 3000.0f;  //Newtons

    //Suspension tuning - see UpdatePhysicsState for how these become a per-wheel spring+damper
    //force. rest_length is normally set from the tank_tracks mesh's own geometry (see
    //ApplicationTank::Init) so the hull floats at the same height the old rigid capsules did.
    float suspension_rest_length = 0.15f; //metres from mount point to ground at equilibrium
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
    //point_velocity's magnitude already clamped in UpdatePhysicsState, the legitimate maximum
    //is provably compression_max*stiffness + clamp*damping = 0.23*6000 + 2.0*900 = 3180N, so
    //this only ever engages if stiffness/damping/travel are tuned upward later without
    //updating it too. Set too low once before (800N) and it silently ate the damping force
    //needed to arrest a normal landing impact, leaving the hull bouncing indefinitely instead
    //of settling - a wheel that's "grounded but never applying real force" looks identical to
    //one that's airborne from the outside, which made that bug look like a driving/input bug.
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
    //max_point_speed's saturation is now reported per wheel (TankWheel::point_speed) so it can
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

    std::vector<TankWheel> wheels;

    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;
    float steering_position = 0.0f; //From -1 to +1
    bool f_reverse = false;

    //Hold-latches backing HoldDrive/HoldBrake/HoldSteer, timestamped with GetTickCount64().
    float gas_latch_amount = 0.0f;
    bool gas_latch_reverse = false;
    unsigned long long gas_latch_until_ms = 0;
    float brake_latch_amount = 0.0f;
    unsigned long long brake_latch_until_ms = 0;
    float steer_latch_amount = 0.0f;
    unsigned long long steer_latch_until_ms = 0;

    //Turret tracking: turns towards turret_target at a constant angular speed.
    Object* turret = NULL;
    Object* turret_target = NULL;
    float turret_turn_speed = 2.0f; //Radians per second
};

#endif
