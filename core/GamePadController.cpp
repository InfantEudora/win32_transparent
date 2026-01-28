#include "GamePadController.h"
#include "Debug.h"
static Debugger* debug = new Debugger("GamePad",DEBUG_INFO);


GamePadController::GamePadController(){
    hid_input = new CUsbHidIO();
}

void GamePadController::ListDevices(){
    hid_input->GetHIDCollectionDevices(NumHIDDevices, arrayDetailData, arrayAttributes, arrayValueCaps);
    for (int i=0; i<NumHIDDevices;i++){
        debug->Info("HID Device %d: VID: %04X PID: %04X\n",i,arrayAttributes[i].VendorID,arrayAttributes[i].ProductID);
        debug->Info(" Device Path: %s\n",arrayDetailData[i]->DevicePath);
        debug->Info(" Usage Page: %04X Usage: %04X\n",arrayValueCaps[i].UsagePage,arrayValueCaps[i].Usage);
        //We will be looking for device VID 045E and PID 028E
        if ((arrayAttributes[i].VendorID == 0x045E) && (arrayAttributes[i].ProductID == 0x028E)){
            dev_index = i;
            debug->Ok("Found controller 0x045E/0x028E at index %i\n",i);
        }
    }

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    if (XInputGetState(0, &state) == ERROR_SUCCESS){
        debug->Info("X-Input: Controller at 0\n");
        XINPUT_CAPABILITIES cap;

        if (XInputGetCapabilities(0,0,&cap) == ERROR_SUCCESS){
            debug->Info("X-Input: Got Capabilities\n");
            debug->Info("X-Input:  - Flags %lu\n",cap.Flags);
            debug->Info("X-Input:  - wLeftMotorSpeed %lu\n",cap.Vibration.wLeftMotorSpeed);
            debug->Info("X-Input:  - wRightMotorSpeed %lu\n",cap.Vibration.wRightMotorSpeed);
        }
    }
}

void GamePadController::UpdateKeyState(){
    if (dev_index == -1){
        return;
    }

    //We make a list of Inputvalues caps
    USHORT numInputValues = arrayValueCaps[dev_index].NumberInputValueCaps;
    HIDP_CAPS device_caps = arrayValueCaps[dev_index];
    PHIDP_VALUE_CAPS pInputValueCaps = (PHIDP_VALUE_CAPS)calloc (device_caps.NumberInputValueCaps, sizeof (HIDP_VALUE_CAPS));
    HRESULT res = hid_input->GetHIDValueCaps (arrayDetailData[dev_index] ,HidP_Input,pInputValueCaps,numInputValues,NULL);
    // allocate memory for the list of values
    PULONG aUsageValue =(PULONG)calloc (numInputValues, sizeof (ULONG));
    DWORD waitMsec = 10;
    __timeb64			AdquiredAt;
    res = hid_input->GetHIDUsagesValues (arrayDetailData[dev_index], HidP_Input, pInputValueCaps, numInputValues, aUsageValue, waitMsec, &AdquiredAt,NULL);
    if (res== HIDP_STATUS_SUCCESS){
        for (int i=0; i<device_caps.NumberInputValueCaps;i++){
            //debug->Info("  Input Value Cap %i: Value: %lu\n",i,aUsageValue[i]);
            analog_values[i] = (int)aUsageValue[i];
        }
    }

    //We update the motor speed
    bool send_disable = false;
    if (lmotor > 0){
        lmotor = lmotor - 1000;
        lmotor = clamp(lmotor,0,65000);
        if (lmotor == 0){
            send_disable  = true;
        }
    }
    if (rmotor > 0){
        rmotor = rmotor - 1000;
        rmotor = clamp(rmotor,0,65000);
        if (rmotor == 0){
            send_disable  = true;
        }
    }
    if ((rmotor >0 ) || (lmotor > 0) || send_disable){
        SendMotorData(lmotor,rmotor);
    }
}

GamePadMap* GamePadController::AddGamePadMap(int analog_index, uint32_t mapped){
    bool new_mapping = true;
    GamePadMap* same_map = NULL;
    for (GamePadMap& map: keymap){
        if (map.mapped_keycode == mapped){
            //Alread have a mapping for it.
            new_mapping = false;
            same_map = &map;
            break;
        }
    }
    GamePadMap m;
    m.analog_index = analog_index;
    m.mapped_keycode = mapped;
    if (new_mapping){
        m.state = new KeyState();
    }else{
        //Reuse existing state
        m.state = same_map->state;
    }
    keymap.push_back(m);
    return &keymap.back();
}

float GamePadController::GetNormalizedAnalogValue(uint32_t mapped_key){
    for (GamePadMap& map: keymap){
        if (map.mapped_keycode == mapped_key){
            if (map.analog_index >=0 && map.analog_index < GAMEPAD_MAX_ANALOG_VALUES){
                int raw_value = analog_values[map.analog_index];
                int32_t zero_offset = map.zero_offset;
                int32_t dead_zone = map.dead_zone;
                float normalized = 0.0f;
                raw_value = raw_value - zero_offset;
                if (raw_value > dead_zone){
                    normalized = (float)(raw_value - dead_zone) / (32767.0f - dead_zone);
                }else if (raw_value < -dead_zone){
                    normalized = (float)(raw_value + dead_zone) / (32768.0f - dead_zone);
                }

                return normalized;
            }
        }
    }
    return 0.0f;
}

//Conclusion: Sending things with the HID interface does not work.
void GamePadController::SendMotorData(int l, int r){
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = l;
    v.wRightMotorSpeed = r;
    XInputSetState(0,&v);
}