#include <windows.h> //Sleep
#include <stdlib.h>
#include "CANReader.h"


//A function that may be overwritten by extending this class.
void CANReader::ReadMessages(){

    //Generate test CAN data
    while(1){

        can_frame_t frame;
        frame.can_dlc = rand()%8;
        for (int i=0;i<frame.can_dlc;i++){
            frame.data[i] = rand()%255;
        }

        frame.can_id = rand()%0x7FF;

        CANMessage msg;
        msg.frame = frame;
        LARGE_INTEGER EndingTime,Microseconds;
        LARGE_INTEGER Frequency;
        QueryPerformanceFrequency(&Frequency);
        QueryPerformanceCounter(&EndingTime);
        Microseconds.QuadPart *= 1000000;
        Microseconds.QuadPart /= Frequency.QuadPart;
        msg.timestamp = Microseconds.QuadPart;
        msg.device = this;

        //Generate the message only when connected.
        if (connected){
            msg_queue.push(msg);
        }

        Sleep(100);
    }
}

bool CANReader::GetCanMessage(CANMessage& message){
    return msg_queue.pop(message);
}

void CANReader::Connect(){
    connected = true;
}

void CANReader::Disconnect(){
    connected = false;
}

void CANReader::Reset(){

}