#ifndef _TANK_CHARACTER_H_
#define _TANK_CHARACTER_H_

#include "Object.h"

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

    //Speed in meters per second
    float top_speed = 1.0f;
    float speed = 0.0f;

    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;
    float steering_position = 0.0f; //From -1 to +1
    bool f_reverse = false;

    //Turret tracking: turns towards turret_target at a constant angular speed.
    Object* turret = NULL;
    Object* turret_target = NULL;
    float turret_turn_speed = 2.0f; //Radians per second
};

#endif
