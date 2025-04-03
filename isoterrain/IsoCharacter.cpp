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
        previous_animation->Lerp(current_animation,previous_animation_time,current_animation_time,transition_time / transition_time_max);
        vec3 hippos_end = hip_bone->GetPosition();
        //How much has the hip moved?
        vec3 d = hippos_start - hippos_end;
        debug->Info("Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);



        d.y = 0;
        MoveBy(d);

    }else if (current_animation){
        current_animation_time += physics_delta;
        if (f_switch_now || (current_animation_time >= current_animation->duration)){
            TransitionAnimation();
        }
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
        MoveBy(delta);
    }

}

//Going to play a move forward animation based on whatever animation its in.
void IsoCharacter::MoveForward(){
    if (current_animation){
        debug->Info("MoveForward: Current animation %s : %.2f\n",current_animation->name.c_str(),current_animation_time);
    }
    //Load the move forward animation
    SetNextAnimation("CatwalkForward");
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