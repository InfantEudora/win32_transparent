#ifndef _CAN_READER_H_
#define _CAN_READER_H_

#include <stdint.h>
#include <string>
#include "canframe.h"
#include "TQueue.h"

//A generic class that implements reading can messages into a queue.
//Multiple messages from different physical can devices can be put in the same queue.
//In order to know where they came from, the message is wrapped and timestamped.

class CANReader;

struct CANMessage{
    can_frame_t frame;  //Holds the can frame.
    uint64_t timestamp;
    CANReader* device;
};

class CANReader{
public:
    CANReader(){};
    ~CANReader(){};

    std::string name;

    bool connected = false;

    ThreadSafeQueue<CANMessage> msg_queue;
    ThreadSafeQueue<CANMessage> out_queue;

    virtual void ReadMessages();
    virtual void Connect();
    virtual void Disconnect();
    virtual void Reset();
    bool GetCanMessage(CANMessage& message); //Return message from internal queue
};

#endif