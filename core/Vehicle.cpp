#include "Vehicle.h"
#include "type_helpers.h"

void Vehicle::Accelerate(float factor){
    //No "brake to stop first" state machine needed - a real opposing force naturally
    //decelerates the body before it starts moving the other way, same as an actual vehicle.
    f_reverse = false;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void Vehicle::Brake(float factor){
    brake_pedal = clamp(factor,0.0f,1.0f);
    gas_pedal = 0.0f;
}

//Both clamp to the FULL [-1,+1] range, not to their own half of it. Clamping SteerLeft to
//[-1,0] (and SteerRight to [0,+1]) meant any input opposing the current deflection collapsed
//straight to centre instead of travelling there: held at +0.8 and pressing left gave
//clamp(0.7,-1,0) = 0, a full-lock-to-centre jump in a single tick. That showed up three ways -
//left and right held together snapped to centre, releasing full left and immediately pressing
//right snapped through centre before moving, and any quick direction change lost its whole
//travel time - all of which are the same one-line bug, and all of which are why the wheels
//moved gradually away from centre but instantly back to it.
//
//Crossing zero smoothly is the whole point: steering_position is a POSITION, and the only
//thing that should ever move it discontinuously is ResetState.
void Vehicle::SteerLeft(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position - delta,-1.0f,1.0f);
}

void Vehicle::SteerRight(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position + delta,-1.0f,1.0f);
}

void Vehicle::Reverse(float factor){
    f_reverse = true;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void Vehicle::HoldDrive(bool reverse,float amount,float duration_ms){
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

void Vehicle::HoldBrake(float amount,float duration_ms){
    brake_latch_amount = clamp(amount,0.0f,1.0f);
    brake_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    Brake(brake_latch_amount);
}

void Vehicle::HoldSteer(float signed_amount,float duration_ms){
    steer_latch_amount = clamp(signed_amount,-1.0f,1.0f);
    steer_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    if (steer_latch_amount < 0.0f){
        SteerLeft(-steer_latch_amount);
    }else{
        SteerRight(steer_latch_amount);
    }
}

void Vehicle::ReleaseInputs(){
    gas_latch_until_ms = 0;
    brake_latch_until_ms = 0;
    steer_latch_until_ms = 0;
    Brake(0.0f); //releases both gas and brake pedals immediately
}

void Vehicle::ApplyHoldLatches(){
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
}

void Vehicle::DecaySteering(float step){
    if (steering_position < 0){
        steering_position = clamp(steering_position + step,-1.0f,0.0f);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - step,0.0f,1.0f);
    }
}

void Vehicle::ResetState(const vec3& pos,const quat& rot){
    ReleaseInputs();
    steering_position = 0.0f; //ReleaseInputs only cancels latches - this normally decays
                               //toward 0 over several ticks (see DecaySteering), too slow for a
                               //reset that's supposed to be instant.
    f_reverse = false;

    SetPosition(pos);
    SetRotation(rot);

    if (Physics* physics = GetPhysics()){
        physics->SetVelocity(vec3());
        physics->SetAngularVelocity(vec3());
        physics->WakeUp(); //a sleeping body ignores this teleport's next tick of forces too -
                            //same issue as gas/brake/steer, see ApplyHoldLatches
    }

    for (Wheel& wheel : wheels){
        wheel.roll_angle = 0.0f;
        wheel.compression = 0.0f;
        wheel.grounded = false;
        wheel.steer_angle = 0.0f; //no-op for a non-steerable wheel; resets a buggy's front wheels
    }
}
