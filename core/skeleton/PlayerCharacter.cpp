#include "PlayerCharacter.h"
#include "type_helpers.h"
#include "Debug.h"
static Debugger* debug = new Debugger("PlayerCharacter",DEBUG_INFO);

PlayerCharacter::PlayerCharacter():Object(){
    animation_transition_time_max = 0.2;

}

PlayerCharacter::~PlayerCharacter(){

}

//Function checks input and applies correct animation and state.
void PlayerCharacter::ProcessInputState(){
    if (!(character_input_state.input_forward_down || character_input_state.input_backward_down ||
         /* character_input_state.input_left_down || character_input_state.input_right_down ||*/
        character_input_state.input_jump || character_input_state.input_action ||
        character_input_state.input_action_active)){
        //No input was down. We proceed to idle animation once.

        if (character_input_state.any_input_was_active){
            TransitionToAnimation("Idle");
            character_input_state.any_input_was_active = false;
            character_animation_state.Clear();
            character_animation_state.idle = true;
        }
    }else{
        character_input_state.any_input_was_active = true;
    }
    if (character_input_state.input_action){
        TransitionToAnimation("Boxing");
        character_input_state.input_action = false;
    }else if (character_input_state.input_action_active){
        if (f_handgun_drawn){
            TransitionToAnimation("PistolIdle");
        }else{
            if (FindAnimation("ActionIdle")){
                TransitionToAnimation("ActionIdle");
            }else{
                TransitionToAnimation("CrossJumps");
            }
        }
        character_input_state.input_action_active = false;
        //On top of this animation, we want to rotate the hips to face the target
    }
    if (character_input_state.input_backward_down){
        TransitionToAnimation("WalkingBackward");

        //MoveForwardBy(0.025f * animation_transition_factor);
        character_input_state.input_backward_down = false;
    }
    if (character_input_state.input_left_down){
        float delta = 0.025f;
        if (f_rotation_animation && !character_input_state.input_forward_down){
            TransitionToAnimation("TurnLeftInPlace");
            delta = 0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_input_state.input_left_down = false;
    }
    if (character_input_state.input_right_down){
        float delta = -0.025f;
        if (f_rotation_animation && !character_input_state.input_forward_down){
            TransitionToAnimation("TurnRightInPlace");
            delta = -0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_input_state.input_right_down = false;
    }
    if (character_input_state.input_forward_down){
        TransitionToAnimation("WalkingForward");
        //If the animation is transitioning to walking, we move by a factor.
        //If the animation is looping, that is the full speed.
        float factor = animation_transition_factor;
        if (animation_state == ANIMATION_STATE_LOOPING){
            factor = 1;
        }
        //MoveForwardBy(-0.025f * factor);
        character_input_state.input_forward_down = false;
        character_animation_state.Clear();
        character_animation_state.moving_forward = true;
    }
    if (character_input_state.input_jump){
        if (character_animation_state.moving_forward){
            TransitionToAnimation("JumpForward");
        }else{
            TransitionToAnimation("Jump");
        }
        character_input_state.input_jump = false;
    }
    if (character_input_state.input_interact){
        TransitionToAnimation("Pushing");
        character_input_state.input_interact = false;
    }
    if (character_input_state.input_toggle_handgun){
        f_handgun_drawn = !f_handgun_drawn;
        character_input_state.input_toggle_handgun = false;
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

    ProcessInputState();

    //Animation state starts off as invalid.
    if (!current_animation){
        animation_state = ANIMATION_STATE_INVALID;
    }
    if (animation_state == ANIMATION_STATE_INVALID){
        if (transition_to){
            current_animation = transition_to;
            animation_state = ANIMATION_STATE_TRANSITION;
            animation_transition_time = animation_transition_blend_time;
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
        character_animation_state.Clear();
        character_animation_state.t_pose = true;
    }

    if (animation_state == ANIMATION_STATE_LOOPING){
        //Loop the same animation
        bool did_rewind = false;
        float last_time_index = current_animation->time_index;
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            if (!current_animation->looped){
                current_animation->time_index = current_animation->duration;

                debug->Info("Animation %s ended.\n",current_animation->name.c_str());
                if (!current_animation->auto_continue_to){
                    debug->Info("No auto-continue set for %s. Pausing animation.\n",current_animation->name.c_str());
                    animation_state = ANIMATION_STATE_PAUSED;
                }else{
                    debug->Info("Auto-continuing from %s to %s.\n",current_animation->name.c_str(),current_animation->auto_continue_to->name.c_str());
                    TransitionToAnimation(current_animation->auto_continue_to);
                }
            }else{
                //Animation has ended. We play a frame close to 0.
                current_animation->time_index -= current_animation->duration;
                did_rewind = true;
            }
        }

        //Extract this tick's world-motion delta (position/yaw) from the clip's root bone track, and
        //write the corrected (pinned/swing-only) pose onto the root bone for the new time index.
        //On a loop wraparound we sample a zero-width window so the bone pose is still refreshed for
        //the new (wrapped) time, without contributing a spurious delta from the wrap itself.
        float sample_prev_time = did_rewind ? current_animation->time_index : last_time_index;
        RootMotionDelta delta = current_animation->SampleRootMotion(sample_prev_time, current_animation->time_index);
        current_animation->ApplyInterval(current_animation->time_index);

        if (delta.yaw != 0.0f){
            RotateBy(quat(vec3(0,1,0),delta.yaw));
        }
        if (delta.position.length() > 0){
            MoveBy(GetRotation()*delta.position);
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION_START){
        //In this state, we need to record the character position to where the hips currently are.
        //We are going to transition by playing this animation
        if (transition_to == NULL){
            debug->Ok("Transition start from %s to NULL\n",current_animation->name.c_str());
        }else{
            debug->Ok("Transition start from %s to %s\n",current_animation->name.c_str(),transition_to->name.c_str());
        }
        animation_state = ANIMATION_STATE_TRANSITION;
    }else if (animation_state == ANIMATION_STATE_TRANSITION){
        //If there is no next animation, we can't proceed.
        if (!transition_to){
            debug->Warn("AnimationSampler: Transition to next = NULL\n");
            animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        }
        if (transition_to == current_animation){
            debug->Warn("AnimationSampler: Next is identical to current\n");
            animation_state = ANIMATION_STATE_LOOPING;

        }

        //We need to play the current and the next animation,
        //and loop both if we transistion longer than the animation is.
        //If the current animation is a non-looping animation, we need to make sure we don't play past the end of it.
        float from_last_time_index = current_animation->time_index;
        bool from_did_rewind = false;
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            if (!current_animation->looped){
                current_animation->time_index = current_animation->duration;
            }else{
                current_animation->time_index -= current_animation->duration;
                from_did_rewind = true;
            }
        }
        //Rewind next animation as well.
        float to_last_time_index = transition_to->time_index;
        bool to_did_rewind = false;
        transition_to->time_index += time_delta;
        if (transition_to->time_index > transition_to->duration){
            transition_to->time_index -= transition_to->duration;
            to_did_rewind = true;
        }

        animation_transition_factor = animation_transition_time / animation_transition_blend_time;
        if (animation_transition_blend_time == 0){
            animation_transition_factor = 1.0;
        }

        debug->Info("Transitioning from %s to %s. Time = %.3f / %.3f (%.2f%%)\n",current_animation->name.c_str(),transition_to->name.c_str(),animation_transition_time,animation_transition_blend_time,animation_transition_factor*100.0f);

        //Blend the per-frame root motion of both clips (same zero-width-window handling on rewind as
        //ANIMATION_STATE_LOOPING) and write the blended pose onto the shared root bone.
        RootMotionDelta delta = current_animation->LerpRootMotion(transition_to,
            from_did_rewind ? current_animation->time_index : from_last_time_index, current_animation->time_index,
            to_did_rewind ? transition_to->time_index : to_last_time_index, transition_to->time_index,
            animation_transition_factor);
        current_animation->Lerp(transition_to,current_animation->time_index,transition_to->time_index,animation_transition_factor);

        if (delta.yaw != 0.0f){
            RotateBy(quat(vec3(0,1,0),delta.yaw));
        }
        if (delta.position.length() > 0){
            MoveBy(GetRotation()*delta.position);
        }

        animation_transition_time += time_delta;
        if (animation_transition_time >= animation_transition_blend_time){
            animation_transition_time = animation_transition_blend_time;
            //Reset the animation that we have transitioned from:
            current_animation->time_index = 0;
            current_animation = transition_to;
            transition_to = NULL;
            animation_state = ANIMATION_STATE_LOOPING;
            debug->Info("Transition complete. Now at %s\n",current_animation->name.c_str());
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION_BACK){
        //We rewind the transition if we are aborting the transition.
        if (!transition_to){
            debug->Warn("No current transition to transition back from.\n");
            animation_state = ANIMATION_STATE_PAUSED;
        }else{
            current_animation->time_index -= time_delta;
            if (current_animation->time_index < 0){
                if (!current_animation->looped){
                    current_animation->time_index = 0;
                }else{
                    current_animation->time_index += current_animation->duration;
                }
            }
            //Rewind next animation as well.
            transition_to->time_index -= time_delta;
            if (transition_to->time_index < 0){
                transition_to->time_index += transition_to->duration;
            }

            debug->Info("Rewinding transition from %s to %s. Time = %.3f (%.2f%%)\n",current_animation->name.c_str(),transition_to->name.c_str(),animation_transition_time,animation_transition_factor*100.0f);
            //We just need to rewind the current transition.
            animation_transition_time -= time_delta;
            if (animation_transition_time <= 0){
                animation_transition_time = 0;
                //Reset current animation (we've rewound back to it; current_animation was always the "from")
                current_animation->time_index = 0;
                transition_to = NULL;
                animation_state = ANIMATION_STATE_LOOPING;
                debug->Info("Transition rewind complete. Now at %s\n",current_animation->name.c_str());
            }else{
                animation_transition_factor = animation_transition_time / animation_transition_blend_time;
                if (animation_transition_blend_time == 0){
                    animation_transition_factor = 0;
                }
            }

            if (transition_to){
                current_animation->Lerp(transition_to,current_animation->time_index,transition_to->time_index,animation_transition_factor);
            }
        }
    }

    //Addition animation layering on top of the current animation.
    if (blink_animation){
        blink_interval = clamp(blink_interval - time_delta, 0.0f, 100.0f);
        if (blink_interval == 0.0f){
            blink_animation->Play(time_delta*2);
        }
        if (blink_animation->HasFinished()){
            blink_animation->time_index = 0;
            //Use RRand if you want a random interval.
            blink_interval = 4.0f;//RandFloat(2.0f,5.0f);
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
            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
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
            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
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
    character_input_state.input_forward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::MoveBackward(){
    character_input_state.input_backward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::TurnRight(){
    character_input_state.input_right_down = true;
}

void PlayerCharacter::TurnLeft(){
    character_input_state.input_left_down = true;
}

void PlayerCharacter::ToIdle(){

}

void PlayerCharacter::ActionActive(){
    character_input_state.input_action_active = true;
}

void PlayerCharacter::Action(){
    character_input_state.input_action = true;
}

void PlayerCharacter::Jump(){
    character_input_state.input_jump = true;
}

void PlayerCharacter::Interact(){
    character_input_state.input_interact = true;
}

void PlayerCharacter::ToggleHandgun(){
    character_input_state.input_toggle_handgun = true;
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