#include "DozerButton.h"
#include "Debug.h"
static Debugger* debug = new Debugger("DozerButton", DEBUG_INFO);

DozerButton::DozerButton():Object(){

}

DozerButton::~DozerButton(){

}

void DozerButton::UpdatePhysicsState(){
    if (sliding_door && sliding_door->IsDestroyed()){
        debug->Warn("Someone destroyed our door!\n");
        sliding_door = NULL;
    }
    if (sliding_door && f_activated){
        //We massage the door state
        if (door_state == DOOR_STATE_CLOSED){
            debug->Info("Opening door...\n");
            door_state = DOOR_STATE_OPENING;
            //Record it's parameters... these better could be set depending on door type...?
            //Now a door has to start closed.
            door_height = sliding_door->GetMesh()->GetExtents().y;
            door_closed_pos_y = sliding_door->GetPosition().y;
            door_opened_pos_y = door_closed_pos_y - door_height;
            if (soundsystem){
                soundsystem->Play("door_opening");
            }
        }else if (door_state == DOOR_STATE_OPENING){
            float door_y = sliding_door->GetPosition().y;
            if (door_y > door_opened_pos_y){
                sliding_door->MoveUpBy(-0.01f);
            }else{
                door_state = DOOR_STATE_OPEN;
                debug->Info("Door is opened.\n");
                f_activated = false;
            }
        }else if (door_state == DOOR_STATE_OPEN){
            debug->Info("Closing door...\n");
            door_state = DOOR_STATE_CLOSING;
            if (soundsystem){
                soundsystem->Play("door_opening");
            }
        }else if (door_state == DOOR_STATE_CLOSING){
            float door_y = sliding_door->GetPosition().y;
            if (door_y < door_closed_pos_y){
                sliding_door->MoveUpBy(0.01f);
            }else{
                door_state = DOOR_STATE_CLOSED;
                debug->Info("Door is closed.\n");
                f_activated = false;
            }

        }
    }
    Object::UpdatePhysicsState();
}

void DozerButton::SetWasPressed(bool state){
    if (state){
        f_activated = true;
    }
}

void DozerButton::SetDoor(Object* door){
    sliding_door = door;
}

void DozerButton::SetSoundSystem(SoundSystem* system){
    soundsystem = system;
}