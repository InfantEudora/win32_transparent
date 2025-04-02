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

    //We play our active animation
    if (current_animation){
        current_animation_time += physics_delta;
        if (current_animation_time >= current_animation->duration){
            ReplayCurrentAnimation();
        }

        current_animation->ApplyInterval(current_animation_time);

    }else{
        //We should find idle.
        SwitchAnimation("Idle");
    }
    idle_time += physics_delta;

    if (idle_time > 2.0f){
        //Wait for the current animation to reset.
        if (current_animation && (current_animation_time == 0) && current_animation->name.compare("Idle") != 0){
            SwitchAnimation("Idle");
        }
    }

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

void IsoCharacter::SwitchAnimation(const std::string& name){
    current_animation = FindAnimation(name);
    current_animation_time = 0;
}

void IsoCharacter::ReplayCurrentAnimation(){
    current_animation_time = 0;

    //We need to know how far the hip has moved between the first and last frame.
    ObjectAnimationKeyFrame* keyframe_start;
    ObjectAnimationKeyFrame* keyframe_end;

    ObjectAnimation* hip_animation = current_animation->FindObjectAnimation("Hips");
    if (!hip_animation){
        return;
    }

    keyframe_start = hip_animation->GetFirstKeyframe();
    keyframe_end = hip_animation->GetLastKeyframe();

    vec3 s = keyframe_start->position;
    vec3 e = keyframe_end->position;
    debug->Info("Animation Keyframes: %i\n",hip_animation->keyframes.size());
    debug->Info("Replay: Hip Translation %.2f %.2f %.2f -> %.2f %.2f %.2f\n",s.x,s.y,s.z,e.x,e.y,e.z);

    vec3 delta = keyframe_end->position - keyframe_start->position;
    //Get the forward component;
    delta.x = 0;
    delta.y = 0;
    MoveBy(delta);
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveForward(){
    if (current_animation){
        debug->Info("MoveForward: Current animation %s : %.2f\n",current_animation->name.c_str(),current_animation_time);

    }
    //Load the move forward animation
    SwitchAnimation("Walking");


    idle_time = 0;
}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveBackward(){
    idle_time = 0;
}