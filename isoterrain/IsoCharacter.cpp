#include "IsoCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoCharacter",DEBUG_INFO);

IsoCharacter::IsoCharacter():Object(){
    animation_transition_time_max = 0.2;
}

IsoCharacter::~IsoCharacter(){

}

void IsoCharacter::ApplyAnimation(float time_delta){
    if (!current_animation){
        current_animation = next_animation;
        next_animation = NULL;
    }
    if (!current_animation){
        debug->Warn("AnimationSampler: No current animation to play.\n");
        return;
    }

    Bone* hip_bone = FindBone(root_bone_name);
    if (!hip_bone){
        debug->Fatal("Could not find root/hip bone '%s;.\n",root_bone_name.c_str());
    }

    if (animation_state == ANIMATION_STATE_LOOPING){
        //Loop the same animation
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            current_animation->time_index -= current_animation->duration;
            //We need to correct the body postion and orientation from what is currently displayed,
            //to what will be displayed.
            update_hippos = true;
            hippos_start = hip_bone->GetPosition();
        }
        current_animation->ApplyInterval(current_animation->time_index);

        if (update_hippos){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hippos_start - hippos_end;
            debug->Trace("Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            //d.y = 0;
            MoveForwardBy(-d.z);
            update_hippos = false;
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION){
        //If there is no next animation, we can't proceed.
        if (!next_animation){
            debug->Warn("AnimationSampler: Transition to next = NULL\n");
            animation_state = ANIMATION_STATE_LOOPING;
            return;
        }
        if (next_animation == current_animation){
            debug->Warn("AnimationSampler: Next is identical to current\n");
            animation_state = ANIMATION_STATE_LOOPING;
            return;
        }

        //We need to play the current and the next animation
        //Loop the same animation
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            current_animation->time_index -= current_animation->duration;
            update_hippos = true;
            hippos_start = hip_bone->GetPosition();
        }

        next_animation->time_index += time_delta;
        if (next_animation->time_index > next_animation->duration){
            next_animation->time_index -= next_animation->duration;
        }

        current_animation->Lerp(next_animation,current_animation->time_index,next_animation->time_index,animation_transition_time / animation_transition_time_max, vec3());

        if (update_hippos){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hippos_start - hippos_end;
            debug->Trace("Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            //d.y = 0;
            MoveForwardBy(-d.z);
            update_hippos = false;
        }

        animation_transition_time += time_delta;
        if (animation_transition_time > animation_transition_time_max){
            animation_transition_time = animation_transition_time;
            current_animation = next_animation;
            animation_state = ANIMATION_STATE_LOOPING;
            update_hippos = true;
            hippos_start = hip_bone->GetPosition();
        }
    }
}

/*
void IsoCharacter::UpdatePhysicsState(){
    if (f_animation_override){
        Object::UpdatePhysicsState();
        return;
    }

    float physics_delta = 0.02f;

    if (f_manual_animation_time){
        physics_delta = 0;
        if (manual_animation_time > 0){
            physics_delta = manual_animation_time;
            manual_animation_time = 0;
        }
    }

    //What to play.
    if ((transition_time < transition_time_max) && current_animation && previous_animation){
        //This lerps including position
        Bone* hip_bone = FindBone("Hips");
        if (!hip_bone){
            debug->Fatal("Could not find hip bone.\n");
        }
        vec3 hippos_start = hip_bone->GetPosition();
        vec3 hip_forward_start = hip_bone->GetForward();


        previous_animation->Lerp(current_animation,previous_animation_time,current_animation_time,transition_time / transition_time_max);
        vec3 hippos_end = hip_bone->GetPosition();

        //How much has the hip moved?
        vec3 d = hippos_start - hippos_end;
        debug->Info("Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
        d.y = 0;
        MoveForwardBy(-d.z);

        vec3 hip_forward_end = hip_bone->GetForward();

        //Flatten the forward in the XZ plane
        hip_forward_start.y = 0;
        hip_forward_start.normalize();
        hip_forward_end.y = 0;
        hip_forward_end.normalize();
        d = hip_forward_end - hip_forward_start;

        float angle_start = atan2(hip_forward_start.z, hip_forward_start.x);
        float angle_end = atan2(hip_forward_end.z, hip_forward_end.x);
        float delta = angle_end - angle_start;

        debug->Info("Hips Forward rotated by %.2f\n",todegrees(delta));
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
    }else if (current_animation){

        if (f_switch_now || (current_animation_time >= current_animation->duration)){
            TransitionAnimation();
        }
        current_animation_time += physics_delta;
        current_animation->ApplyInterval(current_animation_time);
    }else{
        //We should find idle.
        SetNextAnimation("Idle");
    }
    idle_time += physics_delta;

    if (idle_time > idle_time_max){
        //Wait for the current animation to reset.
        SetNextAnimation("Idle");
    }

    if (transition_time < transition_time_max){
        transition_time += physics_delta;
    }else{
        transition_time = transition_time_max;
    }

    CheckSwitchAnimation();

    Object::UpdatePhysicsState();
}*/



/*
void IsoCharacter::SwitchAnimationNow(){
    if (next_animation != current_animation){
        f_switch_now = true;
        debug->Info("Switching now at %.2f \n",current_animation_time);
    }
}

void IsoCharacter::CheckSwitchAnimation(){
    //On start... or no loaded animation..
    if (!current_animation){
        current_animation = next_animation;
        previous_animation = current_animation;
        current_animation_time = 0;
        transition_time = transition_time_max;
        return;
    }
    //Wait for the current animation to reset.
    if (f_switch_now || (current_animation_time >= current_animation->duration)){
        if (current_animation != next_animation){
            transition_time = 0.0f;
            previous_animation = current_animation;
            current_animation = next_animation;
            previous_animation_time = current_animation_time;
            current_animation_time = 0;
            next_animation = NULL;
        }
        f_switch_now = false;
    }
}*/



/*
void IsoCharacter::TransitionAnimation(){
    //We need to know how far the hip has moved between the first and last frame.
    ObjectAnimationKeyFrame* keyframe_start;
    ObjectAnimationKeyFrame* keyframe_end;

    ObjectAnimation* hip_animation = current_animation->FindObjectAnimation("Hips");
    if (!hip_animation){
        return;
    }

    keyframe_start = hip_animation->GetFirstKeyframe();
    keyframe_end = hip_animation->GetClosestKeyframe(current_animation_time);

    vec3 s = keyframe_start->position;
    vec3 e = keyframe_end->position;
    debug->Info("TransitionAnimation:\n");
    debug->Info(" Animation Keyframes: %i. At %.2f / %.2f\n",hip_animation->keyframes.size(),current_animation_time,current_animation->duration);
    debug->Info(" Hip Translation %.2f %.2f %.2f -> %.2f %.2f %.2f\n",s.x,s.y,s.z,e.x,e.y,e.z);

    vec3 delta = e - s;
    //Get the forward component;
    delta.x = 0;
    delta.y = 0;


    if (current_animation->looped == false){
        SetNextAnimation("Idle");
    }
    if (next_animation != current_animation){

    }else{
        //This replays it, so we insta correct for hip position
        current_animation_time = 0;
        MoveForwardBy(-delta.z);
    }
}
*/

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveForward(){
    //Load the move forward animation
    //SetNextAnimation("CatwalkForward");
    //ProceedToNextAnimation();
    idle_time = 0;
    //MoveForwardBy(0.025f);
    if (physics){
        physics->AddLocalForce(vec3(0,0,-1));
    }
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveBackward(){
    //debug->Info("Current animation state : %i\n",animation_state);
    if (animation_state == ANIMATION_STATE_LOOPING){
        SetNextAnimation("IdleStanding");
        ProceedToNextAnimation();
    }
    idle_time = 0;
    MoveForwardBy(-0.01f);
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::TurnRight(){
    if (f_rotation_animation){
        //Pick an animation that will rotate us.
        idle_time = 0;
        SetNextAnimation("TurnRight");
        ProceedToNextAnimation();
    }else{
        //We rotate within current animation
        float delta = -0.05f;
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
    }
}

void IsoCharacter::TurnLeft(){
    if (f_rotation_animation){
        idle_time = 0;
        SetNextAnimation("TurnLeft");
        ProceedToNextAnimation();
    }else{
        //We rotate within current animation
        float delta = 0.05f;
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
    }
}