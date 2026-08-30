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
        TransitionToAnimation("WalkingBackwards");

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
        TransitionToAnimation("Walking");
        //If the animation is transitioning to walking, we move by a factor.
        //If the animation is looping, that is the full speed.
        float factor = animation_transition_factor;
        if (animation_state == ANIMATION_STATE_LOOPING){
            factor = 1;
        }
        MoveForwardBy(-0.025f * factor);
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
                AnimationTransition* transition = animation_graph ? animation_graph->FindTransitionFrom(current_animation) : NULL;
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
                did_rewind = true;
            }
        }
        //If the animation modifies the root object, we need to keep track of how much it woule have moved.
        vec3 pos_delta = vec3();
        if (current_animation->modifies_root_object && !did_rewind){
            //We need to get the difference in postion from the current keyframe
            //to the target keyframe.

            //Should be the hip bone.
            Bone* root_bone = FindBone("mixamorig:Hips");

            ObjectAnimation* root_anim = current_animation->FindObjectAnimation(root_bone);
            if (root_anim){

                ObjectAnimationKeyFrame* frame = root_anim->GetClosestKeyframe(last_time_index);
                if (frame && frame->f_position){
                    vec3 pos_start = frame->position;
                    frame = root_anim->GetClosestKeyframe(current_animation->time_index);
                    if (frame && frame->f_position){
                        vec3 pos_end = frame->position;
                        pos_delta = pos_end - pos_start;
                        debug->Info("Animation modifies root object. Time Index: %.3f, Delta = %.3f %.3f %.3f\n", current_animation->time_index, pos_delta.x, pos_delta.y, pos_delta.z);
                    }
                }
            }
        }
        current_animation->ApplyInterval(current_animation->time_index);

        if (current_animation->modifies_root_object && (pos_delta.length() > 0)){
            //We manually apply the position change, but rotated by our current orientation.
            MoveBy(GetRotation()*pos_delta);
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
    }else if (animation_state == ANIMATION_STATE_TRANSITION){
        //If there is no next animation, we can't proceed.
        if (!current_transition){
            debug->Warn("AnimationSampler: Transition to next = NULL\n");
            animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        }
        if (current_transition->to == current_animation){
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
        float to_last_time_index = current_transition->to->time_index;
        bool to_did_rewind = false;
        current_transition->to->time_index += time_delta;
        if (current_transition->to->time_index > current_transition->to->duration){
            current_transition->to->time_index -= current_transition->to->duration;
            to_did_rewind = true;
        }

        float chosen_max_blend_time = 0;
        if (current_transition->blend_time >= 0){
            chosen_max_blend_time = current_transition->blend_time;
        }else{
            chosen_max_blend_time = animation_transition_time_max;
        }

        animation_transition_factor =  animation_transition_time / chosen_max_blend_time;
        if (chosen_max_blend_time == 0){
            animation_transition_factor = 1.0;
        }

        //Either the current animation or the one we are transitioning to can have modified the root object.
        //We compute the per-frame root movement of each animation the same way ANIMATION_STATE_LOOPING does
        //(the difference between the keyframe position at the last time index and at the new one), then blend
        //the two deltas by how far we are through the transition.
        vec3 delta = vec3();
        if (current_animation->modifies_root_object || current_transition->to->modifies_root_object){


            Bone* root_bone = FindBone("mixamorig:Hips");

            vec3 from_delta = vec3();
            if (current_animation->modifies_root_object && !from_did_rewind){
                ObjectAnimation* from_root_anim = current_animation->FindObjectAnimation(root_bone);
                if (from_root_anim){
                    ObjectAnimationKeyFrame* frame = from_root_anim->GetClosestKeyframe(from_last_time_index);
                    if (frame && frame->f_position){
                        vec3 pos_start = frame->position;
                        frame = from_root_anim->GetClosestKeyframe(current_animation->time_index);
                        if (frame && frame->f_position){
                            from_delta = frame->position - pos_start;
                        }
                    }
                }
            }

            vec3 to_delta = vec3();
            if (current_transition->to->modifies_root_object && !to_did_rewind){
                ObjectAnimation* to_root_anim = current_transition->to->FindObjectAnimation(root_bone);
                if (to_root_anim){
                    ObjectAnimationKeyFrame* frame = to_root_anim->GetClosestKeyframe(to_last_time_index);
                    if (frame && frame->f_position){
                        vec3 pos_start = frame->position;
                        frame = to_root_anim->GetClosestKeyframe(current_transition->to->time_index);
                        if (frame && frame->f_position){
                            to_delta = frame->position - pos_start;
                        }
                    }
                }
            }

            delta = from_delta.lerp(to_delta,animation_transition_factor);
            debug->Info("Transition to/from animation with root modification. Delta: %.3f %.3f %.3f\n",delta.x,delta.y,delta.z);
        }
        debug->Info("Transitioning from %s to %s. Time = %.3f / %.3f (%.2f%%)\n",current_animation->name.c_str(),current_transition->to->name.c_str(),animation_transition_time,chosen_max_blend_time,animation_transition_factor*100.0f);

        current_animation->Lerp(current_transition->to,current_animation->time_index,current_transition->to->time_index,animation_transition_factor);

        if (delta.length() > 0){
            delta = GetRotation()*delta;
            debug->Info("Delta: %.3f %.3f %.3f\n",delta.x,delta.y,delta.z);

            //We manually apply the position change, but rotated by our current orientation.
            MoveBy(delta);
        }

        animation_transition_time += time_delta;
        if (animation_transition_time >= chosen_max_blend_time){
            animation_transition_time = chosen_max_blend_time;
            //Reset the animation that we have transitioned from:
            current_animation->time_index = 0;
            current_animation = current_transition->to;
            animation_state = ANIMATION_STATE_LOOPING;
            debug->Info("Transition complete. Now at %s\n",current_animation->name.c_str());
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION_BACK){
        //We rewind the transition if we are aborting the transition.
        if (!current_transition){
            debug->Warn("No current transition to transition back from.\n");
            animation_state = ANIMATION_STATE_PAUSED;
        }
        if (current_transition->from == current_animation){

            current_animation->time_index -= time_delta;
            if (current_animation->time_index < 0){
                if (!current_animation->looped){
                    current_animation->time_index = 0;
                }else{
                    current_animation->time_index += current_animation->duration;
                }
            }
            //Rewind next animation as well.
            current_transition->to->time_index -= time_delta;
            if (current_transition->to->time_index < 0){
                current_transition->to->time_index += current_transition->to->duration;
            }

            debug->Info("Rewinding transition from %s to %s. Time = %.3f (%.2f%%)\n",current_animation->name.c_str(),current_transition->to->name.c_str(),animation_transition_time,animation_transition_factor*100.0f);
            //We just need to rewind the current transition.
            animation_transition_time -= time_delta;
            if (animation_transition_time <= 0){
                animation_transition_time = 0;
                //Reset current animation
                current_animation->time_index = 0;
                current_animation = current_transition->from;
                animation_state = ANIMATION_STATE_LOOPING;
                debug->Info("Transition rewind complete. Now at %s\n",current_animation->name.c_str());
            }else{
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
            }

            current_animation->Lerp(current_transition->to,current_animation->time_index,current_transition->to->time_index,animation_transition_factor);

        }else{
            debug->Warn("Current animation is not the target of the current transition. Cannot transition back.\n");
            animation_state = ANIMATION_STATE_PAUSED;
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
            blink_interval = RandFloat(2.0f,5.0f);
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

    //Update character position to y target
    //Disabled for now.
    float current_y = GetPosition().y;
    if (abs(current_y - target_y_location) > 0.001f){
        //debug->Info("Lerping character to Y=%.3f position\n",target_y_location);
        vec3 target = GetPosition();
        target.y = target_y_location;
        vec3 p = GetPosition();
        p = p.lerp(target,0.1f * target_location_factor);
        SetPosition(p);
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