#ifndef _PCAN_READER_H_
#define _PCAN_READER_H_

#include "CANReader.h"
#include "PCAN.h"

can_frame_t PCANtocanframe(TPCANMsg* pcanmsg);

bool GetFunctionAdress(HINSTANCE h_module);

class PCANReader : public CANReader{
public:
    PCANReader();
    ~PCANReader();

    //DLL Functions
    static int LoadDLL();
    static int UnloadDLL();
    static HINSTANCE hdll;

    int Init(uint32_t baudrate);
    void Connect() override;
    void Reset();

    //Functions for reading/writing
    void ReadMessages();
    void WriteCanFrame(can_frame_t* frame);
    void WriteMessage(TPCANMsg* msg);


    // TPCANHandle
    TPCANHandle channel;
    TPCANBaudrate baudrate;

};

#endif