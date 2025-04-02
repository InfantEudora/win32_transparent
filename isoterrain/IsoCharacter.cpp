#include "IsoCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoCharacter",DEBUG_INFO);

IsoCharacter::IsoCharacter():Object(){
    // Setup allowed animation transitions:
    //AnimationSequence sequence;

    //sequence.start = "Idle";
    //sequence.Add("Sitting","Standup");
    //sequence.Add("Sitting","LayDown", "Situp");
    //sequence.Add("Walking");

    transition_time_max = 0.2;
}

IsoCharacter::~IsoCharacter(){

}

void IsoCharacter::AddAnimation(Animation* animation){
    if (animation){
        animations.push_back(animation);
        animation->LinkObjects(this);
    }
}

void IsoCharacter::SetAnimation(Animation* animation){
    current_animation = animation;
    if (!current_animation){
        return;
    }
    current_animation_time = 0.0f;
}

void IsoCharacter::UpdatePhysicsState(){
    if (f_animation_override){
        Object::UpdatePhysicsState();
        return;
    }

    float physics_delta = 0.02f;

    //What to play.
    if ((transition_time < transition_time_max) && current_animation && previous_animation){
        previous_animation->Lerp(current_animation,previous_animation_time,current_animation_time,transition_time / transition_time_max);
    }else if (current_animation){
        current_animation_time += physics_delta;
        if (f_switch_now || (current_animation_time >= current_animation->duration)){
            ReplayCurrentAnimation();
        }
        current_animation->ApplyInterval(current_animation_time);

    }else{
        //We should find idle.
        SetNextAnimation("Idle");
    }
    idle_time += physics_delta;

    if (idle_time > 5.0f){
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
}

Animation* IsoCharacter::FindAnimation(const std::string& name){
    for (Animation* animation:animations){
        if (animation->name.compare(name) == 0){
            return animation;
        }
    }
    return NULL;
}

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
}

void IsoCharacter::SetNextAnimation(const std::string& name){
    next_animation = FindAnimation(name);
}

void IsoCharacter::ReplayCurrentAnimation(){

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
    debug->Info("Animation Keyframes: %i. At %.2f / %.2f\n",hip_animation->keyframes.size(),current_animation_time,current_animation->duration);
    debug->Info("Replay: Hip Translation %.2f %.2f %.2f -> %.2f %.2f %.2f\n",s.x,s.y,s.z,e.x,e.y,e.z);

    vec3 delta = keyframe_end->position - keyframe_start->position;
    //Get the forward component;
    delta.x = 0;
    delta.y = 0;
    MoveBy(delta);

    if (current_animation->looped == false){
        SetNextAnimation("Idle");
    }
    if (next_animation != current_animation){
    }else{
        current_animation_time = 0;
    }
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveForward(){
    if (current_animation){
        debug->Info("MoveForward: Current animation %s : %.2f\n",current_animation->name.c_str(),current_animation_time);
    }
    //Load the move forward animation
    SetNextAnimation("Walking");
    SwitchAnimationNow();
    idle_time = 0;
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveBackward(){
    idle_time = 0;
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::TurnRight(){
    idle_time = 0;
    if (current_animation){
        debug->Info("TurnRight: Current animation %s : %.2f\n",current_animation->name.c_str(),current_animation_time);
    }
    SetNextAnimation("TurnRight");
    SwitchAnimationNow();

}

void IsoCharacter::TurnLeft(){
    idle_time = 0;
}