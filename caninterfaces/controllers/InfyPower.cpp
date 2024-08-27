#include <stdio.h>
#include "InfyPower.h"

#include "Debug.h"
static Debugger *debug = new Debugger("InfyPower", DEBUG_ALL);


//The ID is used to encode some values:
//Error 3 bits, dev. no 4 bits, command no 6 bits, dest 8,  source8

bool InfyPower::HandleCANMessage(can_frame_t* frame){
    InfyPower_CanMessageHeader header;
    header.error_code = (frame->can_id & (0b111 << 26)) >> 26;
    header.device_no = (frame->can_id & (0b1111 << 22)) >> 22;
    header.command_no = (frame->can_id & (0b111111 << 16)) >> 16;
    header.destination = (frame->can_id & (0xFF<<8)) >> 8;
    header.source = (frame->can_id & 0xFF);
    debug->Info("Frame: ER: %02X DEVNO: %02X COMND: %02X SRC: %02X DST: %02X DATA: %02X %02X %02X %02X\n",header.error_code,header.device_no,header.command_no,header.source,header.destination,
    frame->data[0],frame->data[1],frame->data[2],frame->data[3]);


    if (header.command_no == 0x03){
        output_voltage = ParseFloat(&frame->data[0]);
        output_current = ParseFloat(&frame->data[4]);
    }

    return false;
};


//Fill the Queue with some predetermined test messages
void InfyPower::GenerateTestMessages(){
    //02 83 F0 00 43 FA 00 00 40 60 00 00
    can_frame_t frame;
    frame.can_dlc = 8;
    frame.can_id = 0x0283F000;
    uint8_t data[8] = {0x43, 0xFA, 0x00, 0x00, 0x40, 0x60, 0x00, 0x00};
    memcpy(frame.data,data,8);
    HandleCANMessage(&frame);
}

//Parse float data from a CAN frame
float InfyPower::ParseFloat(uint8_t* data){
    float a = 0;
    uint8_t* ap = (uint8_t*)&a;
    ap[0] = data[3];
    ap[1] = data[2];
    ap[2] = data[1];
    ap[3] = data[0];

    return a;
}