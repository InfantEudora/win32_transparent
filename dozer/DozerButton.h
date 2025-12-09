#ifndef _DOZER_BUTTON_H_
#define _DOZER_BUTTON_H_
#include "Object.h"
#include "type_int3.h"
#include "SoundSystem.h"

typedef enum DoorState{
    DOOR_STATE_CLOSED = 0,
    DOOR_STATE_OPENING = 1,
    DOOR_STATE_OPEN = 2,
    DOOR_STATE_CLOSING = 3
}DoorStateType;

class DozerButton : public virtual Object{
public:
    DozerButton();
    ~DozerButton();

    //An associated door that we will massage
    Object* sliding_door = NULL;
    void SetDoor(Object* door);
    void SetSoundSystem(SoundSystem* system);
    void SetWasPressed(bool state);
    void UpdatePhysicsState() override;

private:
    bool f_activated            = false;
    DoorStateType door_state    = DOOR_STATE_CLOSED;
    float door_height           = 0;
    float door_opened_pos_y     = 0;
    float door_closed_pos_y     = 0;
    SoundSystem* soundsystem    = NULL;
};


#endif