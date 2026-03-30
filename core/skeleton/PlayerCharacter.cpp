#include "PlayerCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("PlayerCharacter",DEBUG_INFO);

PlayerCharacter::PlayerCharacter():Object(){
    animation_transition_time_max = 0.2;

}

PlayerCharacter::~PlayerCharacter(){

}

//Function checks input and applies correct animation and state.
void PlayerCharacter::ProcessInputState(){
    if (!(character_state.input_forward_down || character_state.input_backward_down ||
          character_state.input_left_down || character_state.input_right_down
        || character_state.input_jump || character_state.input_action)){
        //No input was down. We proceed to idle animation once.

        if (character_state.any_input_was_active){
            TransitionToAnimation("Idle");
            character_state.any_input_was_active = false;
        }
    }else{
        character_state.any_input_was_active = true;
    }
    if (character_state.input_action){
        TransitionToAnimation("ActionIdle");
        character_state.input_action = false;

        //On top of this animation, we want to rotate the hips to face the target
    }

    if (character_state.input_forward_down){
        TransitionToAnimation("Walking");
        MoveForwardBy(-0.025f * animation_transition_factor);
        character_state.input_forward_down = false;
    }
    if (character_state.input_backward_down){
        TransitionToAnimation("WalkBackwardInPlace");

        MoveForwardBy(0.025f * animation_transition_factor);
        character_state.input_backward_down = false;
    }
    if (character_state.input_left_down){
        float delta = 0.025f;
        if (f_rotation_animation){
            TransitionToAnimation("TurnLeftInPlace");
            delta = 0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_state.input_left_down = false;
    }
    if (character_state.input_right_down){
        float delta = -0.025f;
        if (f_rotation_animation){
            TransitionToAnimation("TurnRightInPlace");
            delta = -0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_state.input_right_down = false;
    }
    if (character_state.input_jump){
        TransitionToAnimation("JoyfullJump");
        character_state.input_jump = false;
    }
}

void PlayerCharacter::ApplyAnimation(float time_delta){
    if (f_animation_override){
        if (animation_override_ticks > 0){
            debug->Info("Stepping through animation. ticks = %i, time_delta = %.3f\n",animation_override_ticks,time_delta);
            animation_override_ticks--;
        }else{
            return;
        }
    }

    Bone* hip_bone = FindBone(root_bone_name);
    if (!hip_bone){
        debug->Err("Could not find root/hip bone [%s]\n",root_bone_name.c_str());
        //Try the first bone instead
        if (children.size() > 0){
            hip_bone = dynamic_cast<Bone*>(GetChild(0));
            if (hip_bone){
                debug->Warn("Using first child bone [%s] as root/hip bone instead.\n",hip_bone->name.c_str());
                root_bone_name = hip_bone->name;
            }else{
                debug->Err("First child is not a bone either.\n");
                return;
            }
        }
    }

    ProcessInputState();

    //Animation state starts off as invalid.
    if (!current_animation){
        animation_state = ANIMATION_STATE_INVALID;
    }
    if (animation_state == ANIMATION_STATE_INVALID){
        if (current_transition){
            current_animation = current_transition->to;
            animation_state = ANIMATION_STATE_TRANSITION;
            animation_transition_time = animation_transition_time_max;
            animation_transition_factor = 1.0f;
        }
    }
    if (animation_state == ANIMATION_STATE_LOAD_DEFAULT_POSE){
        //Load the default pose into the skeleton.
        std::vector<Bone*> bones;
        GetAllBones(this,bones);
        for (Bone* bone:bones){
            bone->SetRotation(bone->reference_rotation);
            bone->SetPosition(bone->reference_position);
        }
        animation_state = ANIMATION_STATE_INVALID;
        current_animation = NULL;
    }

    if (animation_state == ANIMATION_STATE_LOOPING){
        //Loop the same animation
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            if (!current_animation->looped){
                current_animation->time_index = current_animation->duration;
                debug->Info("Animation %s ended.\n",current_animation->name.c_str());
                //We need to find a transition to a new animation:
                AnimationTransition* transition = animation_graph->FindTransitionFrom(current_animation);
                if (!transition){
                    debug->Info("No transition found from %s. Pausing animation.\n",current_animation->name.c_str());
                    animation_state = ANIMATION_STATE_PAUSED;
                }else{
                    //Check if we need to do something at the end of this transition:
                    if (current_transition &&  current_transition->f_hips_rotated){
                        quat r = hip_bone->GetRotation() - hip_bone->reference_rotation;
                        float z_angle = r.get_yaw();
                        debug->Info("Applying Hip Rotation. Z-Rotation : %.2f Degrees\n",todegrees(z_angle));

                        //Get the hip bone
                        RotateBy(current_transition->hip_rotation);
                        hip_bone->SetRotation(hip_bone->reference_rotation);
                        hip_bone->animation_mask = 0;

                    }
                    current_transition = transition;
                    debug->Info("Transition found from %s to %s. Starting transition.\n",current_animation->name.c_str(),current_transition->to->name.c_str());
                    TransitionToAnimation(current_transition->to);
                }
            }else{
                //Animation has ended. We play a frame close to 0.
                current_animation->time_index -= current_animation->duration;
            }
            //We need to correct the body postion and orientation from what is currently displayed,
            //to what will be displayed.
            if (!f_movement_animation_inplace){
                update_hip_position = true;
            }
            hip_posistion_start = hip_bone->GetPosition();
            hip_fwd_start = hip_bone->GetWorldForward();
        }
        current_animation->ApplyInterval(current_animation->time_index);


        if (f_update_hip_position && update_hip_position){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hip_posistion_start - hippos_end;
            //debug->Info("Looping: Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            d.y = 0;
            //MoveForwardBy(-d.z);

            //Now, have we turned around maybe?
            vec3 hip_fwd_end = hip_bone->GetWorldForward();
            //debug->Info("Looping: Hip fwd start | end %.3f %.3f %.3f | %.3f %.3f %.3f\n",hip_fwd_start.x,hip_fwd_start.y,hip_fwd_start.z,hip_fwd_end.x,hip_fwd_end.y,hip_fwd_end.z);
            MoveBy(GetRotation()*d);

            //Compute the angle the hip has rotate in the XZ plane.
            vec3 xz_start = vec3(hip_fwd_start.xz()).normalize();
            vec3 xz_end = vec3(hip_fwd_end.xz()).normalize();

            float dot = xz_start.dot(xz_end);
            dot = clamp(dot,-1.0,1.0);
            //debug->Info("Dot product: %.3f. acos = %.3f\n",dot,acos(dot));
            quat r = quat(vec3(0,-1,0),acos(dot));
            //debug->Info("Q = %.3f %.3f %.3f %.3f\n",r.x,r.y,r.z,r.w);
            RotateBy(r);


            update_hip_position = false;
        }else if (f_movement_animation_inplace){
            //Each frame we update depending on foot placement.
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION_START){
        //In this state, we need to record the character position to where the hips currently are.
        //We are going to transition by playing this animation
        if (current_transition == NULL){
            debug->Ok("Transition start from %s to NULL\n",current_animation->name.c_str());
        }else{
            debug->Ok("Transition start from %s to %s\n",current_animation->name.c_str(),current_transition->to->name.c_str());
        }
        animation_state = ANIMATION_STATE_TRANSITION;

        //
    }
    /*
        If we are transitioning between two animations, and during that transition
        decide we want to transition to a different animation. What to do?

        We could snapshot the current transition as a single frame animation,
        and blend that to the new next.
    */
    if (animation_state == ANIMATION_STATE_TRANSITION){
        //If there is no next animation, we can't proceed.
        if (!current_transition){
            debug->Warn("AnimationSampler: Transition to next = NULL\n");
            animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;

            return;
        }
        if (current_transition->to == current_animation){
            debug->Warn("AnimationSampler: Next is identical to current\n");
            animation_state = ANIMATION_STATE_LOOPING;
            return;
        }

        //We need to play the current and the next animation,
        //and loop both if we transistion longer than the animation is.
        //If the current animation is a non-looping animation, we need to make sure we don't play past the end of it.

        if (current_transition->f_only_last_frame){
            hip_bone->animation_mask = 0;
        }
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            if (!current_animation->looped){
                current_animation->time_index = current_animation->duration;
            }else{
                current_animation->time_index -= current_animation->duration;
            }
            //update_hip_position = true;
            //hip_posistion_start = hip_bone->GetPosition();
            //hip_fwd_start = hip_bone->GetWorldForward();
            //debug->Info("Transition: current_animation rewind.\n");
        }
        //Rewind next animation as well.
        current_transition->to->time_index += time_delta;
        if (current_transition->to->time_index > current_transition->to->duration){
            current_transition->to->time_index -= current_transition->to->duration;
            //debug->Info("Transition: next_animation rewind.\n");
        }

        float chosen_max_blend_time = 0;
        if (current_transition->blend_time >= 0){
            chosen_max_blend_time = current_transition->blend_time;
        }else{
            chosen_max_blend_time = animation_transition_time_max;
        }

        animation_transition_factor =  animation_transition_time / chosen_max_blend_time;
        if (chosen_max_blend_time == 0){
            animation_transition_factor = 0;
        }

        //We might need to undo any hip rotation by applying the reverse to the parent.
        if (f_update_hip_position){
            hip_fwd_start = hip_bone->GetWorldForward();
            current_animation->Lerp(current_transition->to,current_animation->time_index,current_transition->to->time_index,animation_transition_factor, vec3());
            vec3 hip_fwd_end = hip_bone->GetWorldForward();

            //Compute the angle the hip has rotate in the XZ plane.
            vec3 xz_start = vec3(hip_fwd_start.xz()).normalize();
            vec3 xz_end = vec3(hip_fwd_end.xz()).normalize();
            float dot = xz_start.dot(xz_end);
            dot = clamp(dot,-1.0,1.0);
            //debug->Info("Dot product: %.3f. acos = %.3f\n",dot,acos(dot));
            quat r = quat(vec3(0,-1,0),acos(dot));
            //debug->Info("Q = %.3f %.3f %.3f %.3f\n",r.x,r.y,r.z,r.w);
            RotateBy(r);
        }else{
            current_animation->Lerp(current_transition->to,current_animation->time_index,current_transition->to->time_index,animation_transition_factor, vec3());
        }

        if (f_update_hip_position && update_hip_position){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hip_posistion_start - hippos_end;
            //debug->Info("Transition: Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            d.y = 0;
            //MoveBy(GetRotation()*d);
            update_hip_position = false;
        }

        animation_transition_time += time_delta;
        if (animation_transition_time >= chosen_max_blend_time){
            animation_transition_time = chosen_max_blend_time;
            current_animation = current_transition->to;
            animation_state = ANIMATION_STATE_LOOPING;
            //update_hip_position = true;
            hip_posistion_start = hip_bone->GetPosition();
            hip_bone->animation_mask = 1;
        }
    }

    //Apply additional rotation
    {
        Bone* neck = FindBone("mixamorig:Neck");
        if (neck){
            quat q = neck->GetRotation();
            quat r,r1,r2;
            vec3 left = neck->GetLeft();
            r1.set_rotation(left,head_turn_direction_ud*0.2f);
            r2.set_rotation(vec3(0,1,0),head_turn_direction_lr*0.2f);
            r = r2 * r1;
            if (animation_state == ANIMATION_STATE_INVALID){
                neck->SetRotation(r);
            }else{
                neck->RotateBy(r);
            }
        }
        Bone* head = FindBone("mixamorig:Head");
        if (head){
            quat q = head->GetRotation();
            quat r,r1,r2;
            vec3 left = neck->GetLeft();
            r1.set_rotation(left,head_turn_direction_ud*0.8f);
            r2.set_rotation(vec3(0,1,0),head_turn_direction_lr*0.8f);
            r = r2 * r1;
            if (animation_state == ANIMATION_STATE_INVALID){
                head->SetRotation(r);
            }else{
                head->RotateBy(r);
            }
        }

        Bone* hips = FindBone("mixamorig:Spine");
        if (hips){
            quat q = hips->GetRotation();
            quat r;
            r.set_rotation(vec3(0,1,0),hips_turn_direction);

            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
                hips->SetRotation(r);
            }else{
                hips->RotateBy(r);
            }
        }

    }

    //Update the foot trackers
    if (foot_tracker_l && tracked_foot_l){
        foot_tracker_l->SetPosition(tracked_foot_l->GetWorldPosition(STATE_ACCESS_PHYSICS));
    }
    if (foot_tracker_r && tracked_foot_r){
        foot_tracker_r->SetPosition(tracked_foot_r->GetWorldPosition(STATE_ACCESS_PHYSICS));
    }
}

void PlayerCharacter::MoveForward(){
    character_state.input_forward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::MoveBackward(){
    character_state.input_backward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::TurnRight(){
    character_state.input_right_down = true;
}

void PlayerCharacter::TurnLeft(){
    character_state.input_left_down = true;
}

void PlayerCharacter::ToIdle(){

}

void PlayerCharacter::Action(){
    character_state.input_action = true;
}

void PlayerCharacter::Jump(){
    character_state.input_jump = true;
}

void PlayerCharacter::TurnLookLeft(){
    head_turn_direction_lr = clamp(head_turn_direction_lr+0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookRight(){
    head_turn_direction_lr = clamp(head_turn_direction_lr-0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookUp(){
    head_turn_direction_ud = clamp(head_turn_direction_ud+0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookDown(){
    head_turn_direction_ud = clamp(head_turn_direction_ud-0.05f,-1.0,1.0);
}

void PlayerCharacter::ComputeFacingAngles(ObjectStateAccessType state_access, const vec3& target, float& out_facing, float& out_diff){
    //We get the direction forward in the zx plane.
    vec3 forward = GetForward(state_access);
    forward.y = 0;
    forward.normalize();

    // Convert facing direction to the shader's atan2(uv.y, uv.x) space.
    float facing = atan2(forward.x, -forward.z) + TYPE_PI/2;
    //Get the direction to the target
    vec3 to_target = GetPosition(state_access) - target;
    to_target.y = 0;
    to_target.normalize();
    float target_angle = atan2(to_target.x,-to_target.z) + TYPE_PI/2;

    // Normalize target offset into [-PI, PI] relative to facing.
    // This avoids any wrap-around comparison by keeping both ends numerically close.
    float diff = fmod(target_angle - facing + 3 * TYPE_PI, 2 * TYPE_PI) - TYPE_PI;
    out_facing = facing;
    out_diff  = diff;
}