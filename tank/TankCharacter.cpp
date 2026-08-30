#include "TankCharacter.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger *debug = new Debugger("Tank", DEBUG_ALL);

TankCharacter::TankCharacter(){
    top_speed = 1.0f;
}

TankCharacter::~TankCharacter(){
}

void TankCharacter::UpdatePhysicsState(){
    float timestep = 0.02f; // conservative default; if called more frequently it's fine

    //Re-assert any still-active hold-latch, exactly as if RunLogic had just called
    //Accelerate/Reverse/Brake/SteerLeft/SteerRight this tick from a held key - see
    //HoldDrive/HoldBrake/HoldSteer below for why this exists.
    unsigned long long now_ms = GetTickCount64();
    if (now_ms < gas_latch_until_ms){
        if (gas_latch_reverse){
            Reverse(gas_latch_amount);
        }else{
            Accelerate(gas_latch_amount);
        }
    }
    if (now_ms < brake_latch_until_ms){
        Brake(brake_latch_amount);
    }
    if (now_ms < steer_latch_until_ms){
        if (steer_latch_amount < 0.0f){
            SteerLeft(-steer_latch_amount);
        }else{
            SteerRight(steer_latch_amount);
        }
    }

    //Steering converges toward 0.
    if (steering_position < 0){
        steering_position = clamp(steering_position + 0.05f,-1.0f,0.0f);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - 0.05f,0.0f,1.0f);
    }

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;

    //Everything below applies forces/torques to the rigidbody - that's what the physics engine
    //expects a dynamic body to be driven by. We no longer track our own "speed" or teleport
    //position/rotation: mass, friction, gravity and collision against the terrain now genuinely
    //determine how the hull moves, including on slopes.
    if (Physics* physics = GetPhysics()){
        //Forces/torques applied to a sleeping rigidbody appear to do nothing (confirmed empirically:
        //two AddLocalForce calls in a row left velocity at exactly zero and is_sleeping still true).
        //Any actual input intent needs to wake it first, or the tank goes completely unresponsive
        //the moment it's sat still long enough for rp3d to put it to sleep - not just an issue for
        //scripted/MCP control, this would affect normal keyboard play too.
        if (gas_pedal > 0.0f || brake_pedal > 0.0f || steering_position != 0.0f){
            physics->WakeUp();
        }

        vec3 velocity = physics->GetVelocity();
        float forward_speed = velocity.dot(GetForward());

        //Drive/reverse: local-space force along the hull's forward axis. Soft-capped by simply
        //not pushing further once real velocity along that axis reaches top_speed.
        if (gas_pedal > 0.0f && fabs(forward_speed) < top_speed){
            physics->AddLocalForce(Object::ref_forward * reverse_multiplier * gas_pedal * engine_force);
        }

        //Braking: oppose whatever the current horizontal velocity actually is (not just facing),
        //same as a real brake would.
        vec3 horizontal_velocity = vec3(velocity.x,0,velocity.z);
        float horizontal_speed = horizontal_velocity.length();
        if (brake_pedal > 0.0f && horizontal_speed > 0.01f){
            vec3 brake_dir = horizontal_velocity * (-1.0f / horizontal_speed);
            physics->AddWorldForceAt(brake_dir * brake_pedal * brake_force,physics->GetBodyWorldPosition());
        }

        //Steering: direct rotation around the hull's up axis, same as before physics was
        //introduced. Steadier (larger steer_amount) at low speed. Tried driving this via
        //AddLocalTorque instead, but couldn't get it to produce visible turning at any
        //reasonable torque magnitude - kinematic rotation is what's actually in use.
        if (steering_position != 0.0f){
            float speed_factor = clamp(fabs(forward_speed) / top_speed,0.0f,1.0f);
            float steer_amount = fmap(1.0f - speed_factor,0.0f,1.0f,0.5f,1.5f);
            float steer_factor = -reverse_multiplier * steering_position * steer_amount;
            RotateAroundAxis(GetUp(),steer_factor*0.1f);
        }
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
    //No "brake to stop first" state machine needed anymore - a real opposing force naturally
    //decelerates the hull before it starts moving the other way, same as an actual vehicle.
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
    f_reverse = true;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void TankCharacter::HoldDrive(bool reverse,float amount,float duration_ms){
    gas_latch_amount = clamp(amount,0.0f,1.0f);
    gas_latch_reverse = reverse;
    gas_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    //Apply immediately too, rather than waiting for the next tick's latch check.
    if (reverse){
        Reverse(gas_latch_amount);
    }else{
        Accelerate(gas_latch_amount);
    }
}

void TankCharacter::HoldBrake(float amount,float duration_ms){
    brake_latch_amount = clamp(amount,0.0f,1.0f);
    brake_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    Brake(brake_latch_amount);
}

void TankCharacter::HoldSteer(float signed_amount,float duration_ms){
    steer_latch_amount = clamp(signed_amount,-1.0f,1.0f);
    steer_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    if (steer_latch_amount < 0.0f){
        SteerLeft(-steer_latch_amount);
    }else{
        SteerRight(steer_latch_amount);
    }
}

void TankCharacter::ReleaseInputs(){
    gas_latch_until_ms = 0;
    brake_latch_until_ms = 0;
    steer_latch_until_ms = 0;
    Brake(0.0f); //releases both gas and brake pedals immediately
}
