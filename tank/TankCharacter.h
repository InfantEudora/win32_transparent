#ifndef _TANK_CHARACTER_H_
#define _TANK_CHARACTER_H_

#include "Object.h"
#include <windows.h>

class TankCharacter;

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

    //Soft cap (m/s): stop adding more drive force once real physics velocity reaches this.
    float top_speed = 1.0f;

    //Tunable force magnitudes - drive/brake are applied to the rigidbody via AddLocalForce/
    //AddWorldForceAt, not by teleporting position; steering is still a direct rotation (see
    //UpdatePhysicsState).
    float engine_force = 2000.0f; //Newtons
    float brake_force = 3000.0f;  //Newtons

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
