#include "IsoCar.h"
#include <cmath>
#include "Debug.h"
static Debugger* debug = new Debugger("IsoCar", DEBUG_INFO);

IsoCar::IsoCar(){
    // default values
    speed = 1.0f;
    f_has_target = false;
}

IsoCar::~IsoCar(){
}

void IsoCar::SetTargetCell(IsoCell* cell){
    if (cell){
        target_position = cell->GetWorldPosition();
        f_has_target = true;
        target_cell = cell;
    }
}

void IsoCar::SetTargetPosition(const vec3& pos){
    target_position = pos;
    f_has_target = true;
}

void IsoCar::ClearTarget(){
    f_has_target = false;
}

bool IsoCar::HasTarget() const{
    return f_has_target;
}

vec3 IsoCar::GetTargetPosition() const{
    return target_position;
}


void IsoCar::UpdatePhysicsState(){
    // Simple physics: move directly towards the target position at constant speed.
    if(!f_has_target){
        Object::UpdatePhysicsState();
        return;
    }

    vec3 cur = GetPosition(STATE_ACCESS_PHYSICS);
    vec3 diff = vec3(target_position.x - cur.x, target_position.y - cur.y, target_position.z - cur.z);

    float dist = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
    if(dist < 0.001f){
        // reached target
        SetPosition(target_position);
        f_has_target = false;
        Object::UpdatePhysicsState();
        return;
    }

    // Move by speed * timestep. Use a small fixed timestep approximation here; the engine
    // should call UpdatePhysicsState at a fairly regular rate. Clamp so we don't overshoot.
    float timestep = 0.02f; // conservative default; if called more frequently it's fine
    float move = speed * timestep;
    if(move >= dist){
        SetPosition(target_position);
        f_has_target = false;
    } else {
        vec3 dir = vec3(diff.x/dist, diff.y/dist, diff.z/dist);
        vec3 newpos = vec3(cur.x + dir.x*move, cur.y + dir.y*move, cur.z + dir.z*move);
        SetPosition(newpos);

        //Rotate towards movement direction
        quat r = quat::getquat(dir,GetForward(),GetUp());
        r.normalize();

        quat current_r = GetRotation();
        r = current_r.slerp(current_r,r,0.05f);
        SetRotation(r);
    }

    //Determine if car is over target cell
    if (target_cell){
        vec3 car_pos = GetPosition(STATE_ACCESS_PHYSICS);
        IsoCell* cell_under_car = target_cell->terrain->FindCellByWorldPosition(car_pos);
        if (cell_under_car == target_cell){
            //We are over the target cell

            debug->Info("Car entered target cell %i,%i\n",target_cell->coordinate.x,target_cell->coordinate.y);
        }
    }

    Object::UpdatePhysicsState();
}
