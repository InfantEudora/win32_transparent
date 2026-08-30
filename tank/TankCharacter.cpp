#include "TankCharacter.h"
#include "type_helpers.h"
#include <cmath>

TankCharacter::TankCharacter(){
    speed = 0.0f;
    top_speed = 1.0f;
}

TankCharacter::~TankCharacter(){
}

void TankCharacter::UpdatePhysicsState(){
    float timestep = 0.02f; // conservative default; if called more frequently it's fine

    //Steering converges toward 0.
    if (steering_position < 0){
        steering_position = clamp(steering_position + 0.05f,-1.0f,0.0f);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - 0.05f,0.0f,1.0f);
    }

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;
    //Apply steering. At low speeds we steer better.
    if (steering_position != 0){
        float speed_factor = speed / top_speed;
        float steer_amount = 1.0f - speed_factor;
        steer_amount = fmap(steer_amount,0.0f,1.0f,0.5f,1.5f);

        if (speed_factor > 0){
            RotateAroundAxis(vec3(0,-reverse_multiplier,0),steer_amount * speed_factor * steering_position / 10.0f);
        }
    }

    float acceleration = gas_pedal * timestep;
    float braking = brake_pedal * 2.0f * timestep;
    speed += acceleration;
    speed -= braking;
    if (speed < 0.0f){
        speed = 0.0f;
    }
    if (speed > top_speed){
        speed = top_speed;
    }

    float move_delta = reverse_multiplier * speed * timestep;
    if (move_delta != 0.0f){
        MoveForwardBy(move_delta);
        //Clear the y position to 0, or in future the level..?
        vec3 p = GetPosition();
        p.y = 0;
        SetPosition(p);
    }

    brake_pedal = 0.1f;
    gas_pedal = 0.0f;

    if (turret && turret_target){
        //Same facing-angle-difference approach as PlayerCharacter::ComputeFacingAngles,
        //reimplemented here since that method lives on PlayerCharacter, not on Object.
        vec3 forward = turret->GetWorldForward(STATE_ACCESS_PHYSICS);
        forward.y = 0;
        forward.normalize();
        float facing = atan2(forward.x,-forward.z) + TYPE_PI/2;

        vec3 to_target = turret_target->GetWorldPosition(STATE_ACCESS_PHYSICS) - turret->GetWorldPosition(STATE_ACCESS_PHYSICS);
        to_target.y = 0;
        to_target.normalize();
        float target_angle = atan2(to_target.x,-to_target.z) + TYPE_PI/2;

        //Normalize into [-PI, PI] so we always turn the short way round.
        float diff = fmod(target_angle - facing + 3 * TYPE_PI, 2 * TYPE_PI) - TYPE_PI;

        //Constant angular speed: rotate by a fixed step towards target, clamped so we don't overshoot.
        float max_step = turret_turn_speed * timestep;
        float turn_by = clamp(diff,-max_step,max_step);
        turret->RotateAroundAxis(vec3(0,1,0),-turn_by);
    }

    Object::UpdatePhysicsState();
}

void TankCharacter::Accelerate(float factor){
    if (f_reverse && (speed > 0.0f)){
        //Brake to stop first
        brake_pedal = clamp(factor,0.0f,1.0f);
        gas_pedal = 0.0f;
        return;
    }
    f_reverse = false;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void TankCharacter::Brake(float factor){
    brake_pedal = clamp(factor,0.0f,1.0f);
    gas_pedal = 0.0f;
}

void TankCharacter::SteerLeft(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position - delta,-1.0f,0.0f);
}

void TankCharacter::SteerRight(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position + delta,0.0f,1.0f);
}

void TankCharacter::Reverse(float factor){
    if (!f_reverse && (speed > 0.0f)){
        //Brake to stop first
        brake_pedal = clamp(factor,0.0f,1.0f);
        gas_pedal = 0.0f;
        return;
    }
    //We are stopped, go into reverse
    brake_pedal = 0.0f;
    f_reverse = true;
    gas_pedal = clamp(factor,0.0f,1.0f);
}
