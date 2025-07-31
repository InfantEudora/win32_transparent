#ifndef _DOZER_CHARACTER_H_
#define _DOZER_CHARACTER_H_

#include "AssetManager.h"
#include "Physics.h"

/*
    A character that you control.
*/

class DozerCharacter;

class DozerCharacter : public Object{
    public:
    DozerCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld);
    ~DozerCharacter();

    Object* armobject = NULL;

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();
    void ArmUp();
    void ArmDown();
};

#endif