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
        SetTargetPosition(cell->GetWorldPosition());
        f_following_path = false;
        iso_path.Clear();
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // Determine starting cell under car
    vec3 carpos = GetPosition(STATE_ACCESS_PHYSICS);
    current_cell = terrain->FindCellByWorldPosition(carpos);

    IsoCell* startRoad = current_cell && current_cell->road_object ? current_cell : IsoPath::FindClosestRoadCell(terrain, current_cell ? current_cell : cell);
    IsoCell* endRoad = cell->road_object ? cell : IsoPath::FindClosestRoadCell(terrain, cell);

    // If no road-based route available, fallback to simple target
    if (!startRoad || !endRoad){
        SetTargetPosition(GetWorldPosition());
        iso_path.Clear();
        f_following_path = false;
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // Build road-only path
    iso_path.Clear();
    bool ok = IsoPath::BuildPath(terrain, startRoad, endRoad, iso_path);
    if (!ok){
        // Fallback to direct
        SetTargetPosition(cell->GetWorldPosition());
        f_has_target = true;
        f_following_path = false;
        f_waiting_for_road = false;
        f_has_final_target = false;
        return;
    }

    // If car is not currently on the road start, first move to the startRoad cell center, then follow path
    if (current_cell != startRoad){
        // Get the first cell in the path to determine lane offset for approach
        IsoCell* first_path_cell = iso_path.cells.empty() ? nullptr : iso_path.cells[0];
        SetTargetPosition(GetLaneAdjustedPosition(startRoad, first_path_cell));
        f_has_target = true;
        f_waiting_for_road = true;
        f_following_path = false;
        f_has_final_target = !cell->road_object;
        if (f_has_final_target) final_target_position = cell->GetWorldPosition();
        debug->Info("Car will move to nearest road cell %i,%i then follow path to %i,%i\n", startRoad->coordinate.x, startRoad->coordinate.y, endRoad->coordinate.x, endRoad->coordinate.y);
    }else{
        // Start following path immediately
        f_waiting_for_road = false;
        f_following_path = true;
        f_has_target = false; // we'll set it below when moving to first cell
        f_has_final_target = !cell->road_object;
        if (f_has_final_target) final_target_position = cell->GetWorldPosition();
        IsoCell* next = iso_path.PopNext();
        if (next){
            next_cell = next;
            // Look ahead to next cell after this one for lane calculation
            IsoCell* peek_next = iso_path.cells.empty() ? nullptr : iso_path.cells[0];
            SetTargetPosition(GetLaneAdjustedPosition(next_cell, peek_next));
            f_has_target = true;
        }
        debug->Info("Car starting path follow to %i,%i (endRoad %i,%i)\n", cell->coordinate.x, cell->coordinate.y, endRoad->coordinate.x, endRoad->coordinate.y);
    }
}

void IsoCar::SetTargetPosition(const vec3& pos){
    target_position = pos;
    f_has_target = true;
    if (target_vis){
        target_vis->SetPosition(pos);
    }
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


void IsoCar::UpdateCellReferences(){
    vec3 pos = GetPosition();
    pos.y = 0;

    //Update current cell based on car position
    IsoCell* helper_cell = current_cell;
    if (!helper_cell){
        helper_cell = target_cell;
    }
    if (helper_cell){
        IsoCell* cell_under_car = current_cell->terrain->FindCellByWorldPosition(pos);
        if (cell_under_car){
            current_cell = cell_under_car;
        }

        //Find the cell ahead of us:
        int3 offset = int3(0,0,0);
        switch(direction){
            case DIRECTION_NORTH:
                offset = int3(0, -1, 0);
                break;
            case DIRECTION_EAST:
                offset = int3(1, 0, 0);
                break;
            case DIRECTION_SOUTH:
                offset = int3(0, 1, 0);
                break;
            case DIRECTION_WEST:
                offset = int3(-1, 0, 0);
                break;
            default:
                break;
        }

        if (offset.x != 0 || offset.y != 0){
            int3 ahead_coord = int3(
                current_cell->coordinate.x + offset.x,
                current_cell->coordinate.y + offset.y,
                current_cell->coordinate.z
            );
            cell_ahead = current_cell->terrain->GetCellByCoordinate(ahead_coord);
            if (cell_ahead && cell_ahead->road_object){
                IsoRoad* road_ahead = cell_ahead->road_object;
                if (road_ahead->road_type == RoadType::STRAIGHT){
                    target_speed = top_speed;
                }else{
                    target_speed = 0.5f;
                }
            }
        }
    }
}

void IsoCar::UpdatePhysicsState(){
    //Update direction
    direction = IsoDirection::NormalToDirection(GetForward());
    float timestep = 0.02f; // conservative default; if called more frequently it's fine

    //Steering
    if (steering_position < 0){
        //Converge toward 0.
        steering_position = clamp(steering_position + 0.05f,-1.0,0);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - 0.05f,0.0,1.0);
    }

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;
    //Apply steering. At low speeds
    if (steering_position !=0){
        float speed_factor = speed / top_speed;
        //At low speeds we steer better.
        float steer_amount = 1.0 - speed_factor;
        steer_amount = fmap(steer_amount,0.0,1.0,0.5,1.5);

        if (speed_factor > 0){
            RotateAroundAxis(vec3(0,-reverse_multiplier,0),steer_amount * speed_factor *  steering_position / 10.0f);
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

    float move_delta =  reverse_multiplier * speed * timestep;
    if (move_delta != 0.0){
        MoveForwardBy(move_delta);
        //Clear the y position to 0, or in future the level..?
        vec3 p = GetPosition();
        p.y = 0;
        SetPosition(p);

        UpdateCellReferences();
    }

    brake_pedal = 0.1f;
    gas_pedal = 0.0f;

    if (f_has_reached_last_target) {
        brake_pedal = 1.0f;
        if (speed <= 0.0f){
            speed = 0.0f;
            f_has_reached_last_target = false;
            debug->Ok("Looking for a new place to drive to\n");
            FindNewDestination(5);
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
            //HonkHorn();

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

    vec3 current_position = GetPosition(STATE_ACCESS_PHYSICS);
    vec3 diff = target_position - current_position;

    float dist = diff.length();
    // Use a slightly larger arrival threshold so cell centers can be reliably reached
    const float arrival_threshold = 0.1f;
    if (dist > arrival_threshold){
        if (speed < target_speed){
            Accelerate(1.0f);
        }
    }else {
        //debug->Ok("Car reached target\n");
        //SetPosition(target_position);
        f_has_target = false;
        // If we were moving to reach the road before following the path
        if (f_waiting_for_road){
            f_waiting_for_road = false;
            f_following_path = true;
            // Start following the path we already built
            IsoCell* next = iso_path.PopNext();
            if (next){
                next_cell = next;
                // Look ahead to next cell for lane calculation
                IsoCell* peek_next = iso_path.cells.empty() ? nullptr : iso_path.cells[0];
                SetTargetPosition(GetLaneAdjustedPosition(next_cell, peek_next));
            }
            Object::UpdatePhysicsState();
            return;
        }

        // If we are following a path, advance to the next cell
        if (f_following_path){
            if (next_cell){
                // We have arrived at next_cell; advance
                if (!iso_path.Empty()){
                    IsoCell* next = iso_path.PopNext();
                    next_cell = next;
                    if (next_cell){
                        // Look ahead to next cell for lane calculation
                        IsoCell* peek_next = iso_path.cells.empty() ? nullptr : iso_path.cells[0];
                        SetTargetPosition(GetLaneAdjustedPosition(next_cell, peek_next));
                        Object::UpdatePhysicsState();
                        return;
                    }
                }
                // Path finished
                f_following_path = false;
                next_cell = NULL;
                // If final target is off-road, go there now
                if (f_has_final_target){
                    SetTargetPosition(final_target_position);
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

    //Rotate towards target direction
    diff.y = 0.0f;
    vec3 dir = diff.normalize();

    //Figure out if direction is to our right or to our left.
    vec3 forward = GetForward();
    forward.y = 0.0f;
    forward.normalize();

    // Cross product of forward and target direction
    // If result is positive (pointing up), target is to the right
    // If result is negative (pointing down), target is to the left
    vec3 cross = forward.cross(dir);
    float side = cross.y;  // Positive = right, Negative = left
    if (side < 0){
        SteerRight(1.0f);
    }else{
        SteerLeft(1.0f);
    }
/*
    quat r = quat::getquat(dir,GetForward(),vec3(0,1,0));
    r.normalize();

    quat current_r = GetRotation();
    r = current_r.slerp(current_r,r,0.05f);
    SetRotation(r);
*/
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
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position - delta,-1.0f,0.0f);
    //RotateAroundAxis(GetUp(), 0.1f * factor);
}

void IsoCar::SteerRight(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position + delta,0.0f,1.0f);
    //RotateAroundAxis(GetUp(), -0.1f * factor);
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

//This sets the target position to a position in the current cell in the lane that car is traveling on.
vec3 IsoCar::GetLaneAdjustedPosition(IsoCell* cell, IsoCell* next_cell){
    if (!cell){
        return vec3(0,0,0);
    }

    lane_offset = 0.1f;

    vec3 base_pos = cell->GetWorldPosition();

    // If we don't have a next cell, we use our current heading.
    vec3 next_pos;
    if (!next_cell){
        next_pos = base_pos + GetForward();
    }else{
        next_pos = next_cell->GetWorldPosition();
    }

    // Calculate direction vector from current cell to next cell
    vec3 forward = next_pos - base_pos;
    forward.y = 0; // Ignore vertical component
    float dist = forward.length();

    if (dist < 0.001f){
        return base_pos; // Cells are at same position, no offset
    }
    forward.normalize();

    //If we are not on a straight road, we should aim to stop just after we crossed the cell border
    if (cell->road_object && cell->road_object->road_type != RoadType::STRAIGHT){
        base_pos -= GetForward()/4.0f;
    }

    // Calculate right vector (perpendicular to forward)
    // In a right-hand coordinate system: right = forward × up
    vec3 up = vec3(0, 1, 0);
    vec3 right = forward.cross(up);
    right.normalize();
    //debug->Info("AdjustPos: Forward : %.3f %.3f %.3f\n",forward.x,forward.y,forward.z);

    // Apply lane offset to the right
    return base_pos + (right * lane_offset);
}

void IsoCar::FindNewDestination(int min_distance){
    if (!current_cell || !current_cell->terrain || !randgen){
        debug->Warn("Car cannot find new destination: missing terrain or random generator\n");
        return;
    }

    IsoTerrain* terrain = current_cell->terrain;

    // Try to find a valid destination cell at least min_distance away
    int max_attempts = 50;
    for (int attempt = 0; attempt < max_attempts; attempt++){
        // Pick a random cell in the terrain
        int rand_x = randgen->GetInt(0, terrain->width - 1);
        int rand_z = randgen->GetInt(0, terrain->depth - 1);

        IsoCell* candidate = terrain->GetCellByCoordinate(int3(rand_x, rand_z, 0));

        if (!candidate) continue;
        if (!candidate->road_object) continue;

        // Calculate Manhattan distance
        int dx = abs(candidate->coordinate.x - current_cell->coordinate.x);
        int dz = abs(candidate->coordinate.y - current_cell->coordinate.y);
        int manhattan_dist = dx + dz;

        // Check if far enough away
        if (manhattan_dist >= min_distance){
            debug->Info("Car [%s] found new destination at %i,%i (distance: %i tiles)\n",
                       name.c_str(), candidate->coordinate.x, candidate->coordinate.y, manhattan_dist);
            SetTargetCell(candidate);
            return;
        }
    }

    debug->Warn("Car [%s] couldn't find destination after %i attempts\n", name.c_str(), max_attempts);
}