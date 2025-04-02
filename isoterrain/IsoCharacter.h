#ifndef _ISO_CHARACTER_H_
#define _ISO_CHARACTER_H_
#include "ObjectAnimation.h"
#include "skeleton/Skeleton.h"
/*
    A character that you control.

    TODO:

    - Play an idle animation.
    - Goto a walking animation when pressing forward.
     The animation can be done in place, but it makes more sence to get the animation with position changes.

     When lerping to a different animation, that may be in place we'd need to move the root, or hip from local transform to parent.


*/
class IsoCharacter;

class IsoCharacter : public virtual Skeleton{
    public:
    IsoCharacter();
    ~IsoCharacter();

    void UpdatePhysicsState() override;

    //Does each character need a copy of all animations?
    //It does when you don't want to lookup the objects every single time.
    std::vector<Animation*>animations;
    Animation* current_animation = NULL;

    void AddAnimation(Animation* animation);
    void SetAnimation(Animation* animation);
    Animation* FindAnimation(const std::string& name); //Finds it by name
    void SwitchAnimation(const std::string& name);
    void ReplayCurrentAnimation();

    float current_animation_time = 0.0f;
    float idle_time = 0.0f;

    bool f_animation_override = false;

    //There also needs to be a list, or a tree linked list thing with all allowed actions from a current one.
    //eg. When haning of a ledge, you can't transistion to a walking animation. You first need climb up

    void MoveForward();
    void MoveBackward();
};

#endif