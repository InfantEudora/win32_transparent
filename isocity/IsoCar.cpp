#include "IsoCar.h"
#include "Debug.h"
static Debugger* debug = new Debugger("IsoCar", DEBUG_INFO);

IsoCar::IsoCar(){
    // default values
    speed = 0.0f;
    top_speed = 1.0f;
    f_has_target = false;
}

IsoCar::~IsoCar(){
}

void IsoCar::SetTargetCell(IsoCell* cell){
    if (!cell) return;
    target_cell = cell;
    IsoTerrain* terrain = cell->terrain;
    if (!terrain){
        // Fallback to simple world position target
        target_position = cell->GetWorldPosition();
        f_has_target = true;
        f_following_path = false;
        path.Clear();
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // Determine starting cell under car
    vec3 carpos = GetPosition(STATE_ACCESS_PHYSICS);
    current_cell = terrain->FindCellByWorldPosition(carpos);

    IsoCell* startRoad = current_cell && current_cell->object_road ? current_cell : IsoPath::FindClosestRoadCell(terrain, current_cell ? current_cell : cell);
    IsoCell* endRoad = cell->object_road ? cell : IsoPath::FindClosestRoadCell(terrain, cell);

    // If no road-based route available, fallback to simple target
    if (!startRoad || !endRoad){
        target_position = cell->GetWorldPosition();
        f_has_target = true;
        path.Clear();
        f_following_path = false;
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // Build road-only path
    path.Clear();
    bool ok = IsoPath::BuildPath(terrain, startRoad, endRoad, path);
    if (!ok){
        // Fallback to direct
        target_position = cell->GetWorldPosition();
        f_has_target = true;
        f_following_path = false;
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // If car is not currently on the road start, first move to the startRoad cell center, then follow path
    if (current_cell != startRoad){
        target_position = startRoad->GetWorldPosition();
        f_has_target = true;
        f_waiting_for_road = true;
        f_following_path = false;
        f_has_final_target = !cell->object_road;
        if (f_has_final_target) final_target_position = cell->GetWorldPosition();
        debug->Info("Car will move to nearest road cell %i,%i then follow path to %i,%i\n", startRoad->coordinate.x, startRoad->coordinate.y, endRoad->coordinate.x, endRoad->coordinate.y);
    }else{
        // Start following path immediately
        f_waiting_for_road = false;
        f_following_path = true;
        f_has_target = false; // we'll set it below when moving to first cell
        f_has_final_target = !cell->object_road;
        if (f_has_final_target) final_target_position = cell->GetWorldPosition();
        IsoCell* next = path.PopNext();
        if (next){
            next_cell = next;
            target_position = next_cell->GetWorldPosition();
            f_has_target = true;
        }
        debug->Info("Car starting path follow to %i,%i (endRoad %i,%i)\n", cell->coordinate.x, cell->coordinate.y, endRoad->coordinate.x, endRoad->coordinate.y);
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
    //Update direction
    direction = IsoDirection::NormalToDirection(GetForward());
    float timestep = 0.02f; // conservative default; if called more frequently it's fine

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
    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;

    float move_delta =  reverse_multiplier * speed * timestep;
    if (move_delta != 0.0){
        MoveForwardBy(move_delta);
    }
    brake_pedal = 0.1f;
    gas_pedal = 0.0f;

    if (f_has_reached_last_target) {
        brake_pedal = 1.0f;
        if (speed <= 0.0f){
            speed = 0.0f;
            f_has_reached_last_target = false;
        }
    }

    if(reversing_time > 0.0f){
        reversing_time -= timestep;
        Reverse(1.0f);
        //If we are reversing, don't try to reach target
        Object::UpdatePhysicsState();
        return;
    }

    if (close_car){
        //debug->Info("Car [%s] is close to car [%s]\n",name.c_str(),close_car->name.c_str());
        //Figure out which one will yield
        vec3 diff = close_car->GetPosition(STATE_ACCESS_PHYSICS) - GetPosition(STATE_ACCESS_PHYSICS);
        diff.normalize();
        int other_dir = IsoDirection::NormalToDirection(diff);
        //debug->Info("Our direction: (%s) Other car direction %d (%s)\n",IsoDirection::ToString(direction).c_str(),other_dir,IsoDirection::ToString(other_dir).c_str());
        if (direction == other_dir){
            //We yield to the other car
            //debug->Info("Car [%s] braking for car [%s] ahead.\n",name.c_str(),close_car->name.c_str());

        }
        Brake(1.0f);

        time_waiting_for_car_ahead += timestep;
        if (time_waiting_for_car_ahead > time_waiting_threshold){
            debug->Info("Car [%s] waited too long for car [%s], honking\n",name.c_str(),close_car->name.c_str());

            time_waiting_for_car_ahead = 0.0f;
            time_waiting_threshold = randgen->GetFloat(3.0f,5.0f);
            HonkHorn();

            bool dir_change = randgen->Roll(0.4f);
            if (dir_change){
                debug->Info("Car [%s] is annoyed and changing direction to avoid car [%s]: %s\n",name.c_str(),close_car->name.c_str(),dir_change?"left":"right");
                reversing_time = 0.5;
            }else{
                debug->Info("Car [%s] is annoyed but keeps waiting to avoid car [%s]\n",name.c_str(),close_car->name.c_str());
            }
        }
        close_car = NULL;
        //close_car should be reset each frame by the proximity trigger system
        Object::UpdatePhysicsState();
        return;
    }else{
        time_waiting_for_car_ahead = 0.0f;
    }

    if(!f_has_target){
        Object::UpdatePhysicsState();
        //speed = 0.0f;
        return;
    }

    debug->Debug("Car moving towards target position (%5.2f,%5.2f,%5.2f)\n",target_position.x,target_position.y,target_position.z);

    vec3 cur = GetPosition(STATE_ACCESS_PHYSICS);
    vec3 diff = vec3(target_position.x - cur.x, target_position.y - cur.y, target_position.z - cur.z);

    float dist = diff.length();
    // Use a slightly larger arrival threshold so cell centers can be reliably reached
    const float arrival_threshold = 0.1f;
    if (dist > arrival_threshold){
        Accelerate(1.0f);
    }else {
        debug->Ok("Car reached target\n");
        //SetPosition(target_position);
        f_has_target = false;
        // If we were moving to reach the road before following the path
        if (f_waiting_for_road){
            f_waiting_for_road = false;
            f_following_path = true;
            // Start following the path we already built
            IsoCell* next = path.PopNext();
            if (next){
                next_cell = next;
                target_position = next_cell->GetWorldPosition();
                f_has_target = true;
            }
            Object::UpdatePhysicsState();
            return;
        }

        // If we are following a path, advance to the next cell
        if (f_following_path){
            if (next_cell){
                // We have arrived at next_cell; advance
                if (!path.Empty()){
                    IsoCell* next = path.PopNext();
                    next_cell = next;
                    if (next_cell){
                        target_position = next_cell->GetWorldPosition();
                        f_has_target = true;
                        Object::UpdatePhysicsState();
                        return;
                    }
                }
                // Path finished
                f_following_path = false;
                next_cell = NULL;
                // If final target is off-road, go there now
                if (f_has_final_target){
                    target_position = final_target_position;
                    f_has_target = true;
                    f_has_final_target = false;
                    Object::UpdatePhysicsState();
                    return;
                }
            }
        }

        debug->Ok("Car reached last target\n");
        f_has_reached_last_target = true;

        Object::UpdatePhysicsState();
        return;
    }

    // Move by speed * timestep. Use a small fixed timestep approximation here; the engine
    // should call UpdatePhysicsState at a fairly regular rate. Clamp so we don't overshoot.

    if(move_delta >= dist){
        f_has_target = false;
    }

    //Rotate towards movement direction
    vec3 dir = vec3(diff.x/dist, diff.y/dist, diff.z/dist);

    quat r = quat::getquat(dir,GetForward(),vec3(0,1,0));
    r.normalize();

    quat current_r = GetRotation();
    r = current_r.slerp(current_r,r,0.05f);
    SetRotation(r);


    //Determine if car is over target cell
    if (target_cell){
        vec3 car_pos = GetPosition(STATE_ACCESS_PHYSICS);
        IsoCell* cell_under_car = target_cell->terrain->FindCellByWorldPosition(car_pos);
        if (cell_under_car == target_cell){
            //We are over the target cell
            //debug->Info("Car entered target cell %i,%i\n",target_cell->coordinate.x,target_cell->coordinate.y);
        }
    }

    Object::UpdatePhysicsState();
}

void IsoCar::Accelerate(float factor){
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
void IsoCar::Brake(float factor){
    brake_pedal = clamp(factor,0.0f,1.0f);
    gas_pedal = 0.0f;
}
void IsoCar::SteerLeft(float factor){
    RotateAroundAxis(GetUp(), 0.1f * factor);
}
void IsoCar::SteerRight(float factor){
    RotateAroundAxis(GetUp(), -0.1f * factor);
}

void IsoCar::Reverse(float factor){
    if (!f_reverse && (speed > 0.0f)){
        //Brake to stop first
        brake_pedal = clamp(factor,0.0f,1.0f);
        gas_pedal = 0.0f;
        //debug->Info("Car [%s] braking to stop before reversing\n",name.c_str());
        return;
    }
    //We are stopped, go into reverse
    brake_pedal = 0.0f;
    f_reverse = true;
    gas_pedal = clamp(factor,0.0f,1.0f);
    //debug->Info("Car [%s] is now reversing\n",name.c_str());
}

void IsoCar::HonkHorn(){
    if (soundsystem){
        soundsystem->Rewind("car_horn_1");
        soundsystem->Play("car_horn_1");
    }
}