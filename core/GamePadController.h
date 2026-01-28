#ifndef _GAMEPADCONTROLLER_H_
#define _GAMEPADCONTROLLER_H_
class GamePadController;

#include "UsbHidIO.h"
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

    //Interface to USB HID
    USHORT NumHIDDevices = 0;
    PSP_DEVICE_INTERFACE_DETAIL_DATA	arrayDetailData[RD_MAXHIDDEVICES];	// Array of "Path structures" with every HID Device
	HIDD_ATTRIBUTES						arrayAttributes[RD_MAXHIDDEVICES];	// Array of "Attributes structures" with every HID Device
	HIDP_CAPS							arrayValueCaps[RD_MAXHIDDEVICES];	// Array of "Capabilities structures" with every HID Device
    CUsbHidIO* hid_input = NULL;

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