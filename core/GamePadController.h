#ifndef _GAMEPADCONTROLLER_H_
#define _GAMEPADCONTROLLER_H_
class GamePadController;


#include "InputController.h"
#include <xinput.h>

//This class handles gamepad input by using the USB HID API on Windows.
//There is a newer GameInput API from Microsoft, this needs to be downloaded separately.
//Maybe in the future we can switch to that. For now this attempts to look like InputController.

#define GAMEPAD_MAX_ANALOG_VALUES 8

struct GamePadMap{
    int analog_index = -1;
    uint32_t mapped_keycode = 0;   // Our keycode
    int32_t zero_offset = 32768;
    int32_t dead_zone = 50;
    KeyState* state = NULL;
};

class GamePadController{
    public:
    GamePadController();

    void ListDevices();

    int dev_index = -1;


    void UpdateKeyState();
    void SendMotorData(int,int);

    int analog_values[GAMEPAD_MAX_ANALOG_VALUES] = {0}; //Example for 8 analog values
    int lmotor = 0;
    int rmotor = 0;
    std::vector<GamePadMap>keymap;

    GamePadMap* AddGamePadMap(int analog_index, uint32_t mapped);
    float GetNormalizedAnalogValue(uint32_t mapped_key);
};
#endif