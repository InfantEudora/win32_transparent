/*
    Windows only interface to PCAN API.

    Reads/writes CAN messages in a SDL_Thread and exchanges messages using the SDL Message loop.

*/
#include <stdio.h>
//#include <conio.h>
#include <string.h>
#include <math.h>

#include <time.h>
#include <stdint.h>

#include "PCANReader.h"

#include "Debug.h"
static Debugger* debug = new Debugger("PCANReader");

//typdef of Functions
typedef TPCANStatus (__stdcall *PCAN_Initialize)(TPCANHandle Channel, TPCANBaudrate Btr0Btr1, TPCANType HwType, DWORD IOPort, WORD Interrupt);
typedef TPCANStatus (__stdcall *PCAN_Uninitialize)( TPCANHandle Channel);
typedef TPCANStatus (__stdcall *PCAN_Reset)(TPCANHandle Channel);
typedef TPCANStatus (__stdcall *PCAN_GetStatus)(TPCANHandle Channel);
typedef TPCANStatus (__stdcall *PCAN_Read)(TPCANHandle Channel, TPCANMsg* MessageBuffer, TPCANTimestamp* TimestampBuffer);
typedef TPCANStatus (__stdcall *PCAN_Write)(TPCANHandle Channel, TPCANMsg* MessageBuffer);
typedef TPCANStatus (__stdcall *PCAN_FilterMessages)(TPCANHandle Channel, DWORD FromID, DWORD ToID, TPCANMode Mode);
typedef TPCANStatus (__stdcall *PCAN_GetValue)(TPCANHandle Channel, TPCANParameter Parameter, void* Buffer, DWORD BufferLength);
typedef TPCANStatus (__stdcall *PCAN_SetValue)(TPCANHandle Channel, TPCANParameter Parameter, void* Buffer, DWORD BufferLength);
typedef TPCANStatus (__stdcall *PCAN_GetErrorText)(TPCANStatus Error, WORD Language, LPSTR Buffer);

//declaration
PCAN_Initialize g_CAN_Initialize;
PCAN_Uninitialize g_CAN_Uninitialize;
PCAN_Reset g_CAN_Reset;
PCAN_GetStatus  g_CAN_GetStatus;
PCAN_Read g_CAN_Read;
PCAN_Write  g_CAN_Write;
PCAN_FilterMessages  g_CAN_FilterMessages;
PCAN_GetValue  g_CAN_GetValue;
PCAN_SetValue  g_CAN_SetValue;
PCAN_GetErrorText  g_CAN_GetErrorText;

HINSTANCE PCANReader::hdll = NULL;

PCANReader::PCANReader(){
    debug->SetLevel(DEBUG_ALL);
    Init(PCAN_BAUD_500K);
}

PCANReader::~PCANReader(){

}

//Called after creating an instance.
int PCANReader::Init(uint32_t _baudrate){
    int ret;
    int iBuffer;
    TPCANStatus CANStatus;
    TPCANMsg SendMessageBuffer;

    debug->Info("PCAN-Basic USB\n");
    //printf("PCAN-Basic using a USB Interface SimpleSample - (c)2019 PEAK-System Technik GmbH\n");
    //printf("using 500k\n");

    //Default USB Channel1
    channel = PCAN_USBBUS1;
    baudrate = _baudrate;

    // call the Load DLL function
    if(!PCANReader::LoadDLL()){
        debug->Fatal("Load DLL failed.\n");
    }
    return 1;
}

void PCANReader::Connect(){
    TPCANStatus CANStatus;
    // Init of PCANBasic Driver
    CANStatus = g_CAN_Initialize(channel, baudrate, 0, 0, 0);
    if(CANStatus!=PCAN_ERROR_OK){
        debug->Err("Error initialising CAN interface: 0x%x. Maybe it's busy? Turn it off and on again?\n",CANStatus);
        connected = false;
        return;
    }
    debug->Ok("Channel initialized\n");
    connected = true;
}

bool canframetoPCAN(can_frame_t* frame_in, TPCANMsg* pcanmsg_out){
    if (!frame_in){
        return false;
    }
    if (!pcanmsg_out){
        return false;
    }

    //Always copy all 8 bytes?
    memcpy(pcanmsg_out->DATA,frame_in->data,8);
    pcanmsg_out->LEN = frame_in->can_dlc;
    pcanmsg_out->ID = frame_in->can_id;
    pcanmsg_out->MSGTYPE = PCAN_MESSAGE_STANDARD;

    return true;
}

bool PCANtocanframe(TPCANMsg* pcanmsg_in, can_frame_t* frame_out){
    if (!pcanmsg_in){
        return false;
    }
    if (!frame_out){
        return false;
    }

    //Always copy all 8 bytes?
    memcpy(frame_out->data,pcanmsg_in->DATA,8);
    frame_out->can_dlc = pcanmsg_in->LEN;
    frame_out->can_id = pcanmsg_in->ID;

    return true;
}

/*
    Reads all messages from interface until queue is empty and sticks them into the eventqueue
*/
void PCANReader::ReadMessages(void){
    if (!connected){
        return;
    }
    TPCANStatus canstatus;
    TPCANMsg rx_msg;
    while(1){
        //Transmit any pending messages
        CANMessage msg;
        if (out_queue.pop(msg)){
            TPCANMsg pcanmsg;
            canframetoPCAN(&msg.frame,&pcanmsg);
            WriteMessage(&pcanmsg);
        }
        //Timestamp and read messages
        TPCANTimestamp timestamp;
        canstatus = g_CAN_Read(channel,&rx_msg,&timestamp);
        if (canstatus == PCAN_ERROR_QRCVEMPTY){
            //Nothing to do.
            //debug->Info("No can frames to read.\n");

            timeBeginPeriod(1);
            Sleep(1);
            timeEndPeriod(1);
            continue;
        }else if (canstatus == PCAN_ERROR_BUSHEAVY){
            //debug->Warn("BUSHEAVY\n");

        }else if (canstatus == PCAN_ERROR_BUSLIGHT){
            //debug->Warn("BUSLIGHT\n");
        }else if (canstatus != PCAN_ERROR_OK){
            //debug->Info("Canstatus = %lu\n", canstatus);
        }else{
            //Convert PCAN message into can_frame_t in CANEvent
            //TODO: Stick in this Readers' queue
            //debug->Ok("CAN Massage!\n");


            CANMessage msg;
            bool res = PCANtocanframe(&rx_msg,&msg.frame);
            //debug->Info("Frame ID: 0x%08X\n",msg.frame.can_id);

            LARGE_INTEGER EndingTime,Microseconds;
            LARGE_INTEGER Frequency;
            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&EndingTime);
            Microseconds.QuadPart *= 1000000;
            Microseconds.QuadPart /= Frequency.QuadPart;
            msg.timestamp = Microseconds.QuadPart;
            msg.device = this;
            msg_queue.push(msg);
        }
    }
}

//Write a frame out directly?
//Better write to Queue and post from reader thread..?
void PCANReader::WriteCanFrame(can_frame_t* frame){
    if (!frame){
        return;
    }
    LARGE_INTEGER EndingTime,Microseconds;
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&EndingTime);
    Microseconds.QuadPart *= 1000000;
    Microseconds.QuadPart /= Frequency.QuadPart;

    CANMessage msg;
    msg.timestamp = Microseconds.QuadPart;
    msg.device = this;
    msg.frame = *frame;
    out_queue.push(msg);
}

//Function may only be called from within reader thread
void PCANReader::WriteMessage(TPCANMsg* msg){
    //SendMessageBuffer.ID = ulLoopCounter;
    //SendMessageBuffer.LEN = 0; // No Data - only ID
    //SendMessageBuffer.MSGTYPE = PCAN_MESSAGE_EXTENDED;
    TPCANStatus CANStatus = g_CAN_Write(channel, msg);
    if (CANStatus != PCAN_ERROR_OK){
        debug->Err("Sending CAN Frame, ends up with a error: %d\n",  CANStatus);
    }
}

/***************************************************************************************

 Dynamic Load of the DLL and all function pointer

****************************************************************************************/
//
// Function: Load DLL
// Parameter: none
// ret value: 0 if OK, -1 if DLL not found or can not open, -2 if function pointer not found
//
// load the DLL and get function pointers
//
int PCANReader::LoadDLL(){
    if(hdll==NULL){
        hdll = LoadLibrary("PCANBasic");
        if(hdll == NULL){
            debug->Err("Could not load PCANBasic.dll");
            return -1;
        }else{
            debug->Ok("DLL Handle: %p\n",hdll);
            // call the getFunction to read the Function Pointers of the DLL
            if(GetFunctionAdress(hdll)==true){
                debug->Ok("Loaded function adresses for PCANBasic.dll\n");
                return 1;
            }else{
                debug->Err("Could not load function adresses\n");
                return -2;
            }
        }
    }else{
        //Reload dll?
        return 1;
    }
    return 0;
}

//
// Function: GetFunctionAdress
// Parameter: instance of DLL
// ret value: true if OK false if pointer not vallid
//
// load the function pointer from the DLL spec. by handle
//
bool GetFunctionAdress(HINSTANCE h_module){
    //Lade alle Funktionen
    if(h_module == NULL)
        return false;

    g_CAN_Initialize = (PCAN_Initialize) GetProcAddress(h_module, "CAN_Initialize");
    if(g_CAN_Initialize == NULL)
        return false;

    g_CAN_Uninitialize = (PCAN_Uninitialize) GetProcAddress(h_module, "CAN_Uninitialize");
    if(g_CAN_Uninitialize == NULL)
        return false;

    g_CAN_Reset = (PCAN_Reset) GetProcAddress(h_module, "CAN_Reset");
    if(g_CAN_Reset == NULL)
        return false;

    g_CAN_GetStatus = (PCAN_GetStatus) GetProcAddress(h_module, "CAN_GetStatus");
    if(g_CAN_GetStatus == NULL)
        return false;

    g_CAN_Read = (PCAN_Read) GetProcAddress(h_module, "CAN_Read");
    if(g_CAN_Read == NULL)
        return false;

    g_CAN_Write = (PCAN_Write) GetProcAddress(h_module, "CAN_Write");
    if(g_CAN_Write == NULL)
        return false;

    g_CAN_FilterMessages = (PCAN_FilterMessages) GetProcAddress(h_module, "CAN_FilterMessages");
    if(g_CAN_FilterMessages == NULL)
        return false;

    g_CAN_GetValue = (PCAN_GetValue) GetProcAddress(h_module, "CAN_GetValue");
    if(g_CAN_GetValue == NULL)
        return false;

    g_CAN_SetValue = (PCAN_SetValue) GetProcAddress(h_module, "CAN_SetValue");
    if(g_CAN_SetValue == NULL)
        return false;

    g_CAN_GetErrorText = (PCAN_GetErrorText) GetProcAddress(h_module, "CAN_GetErrorText");
    if(g_CAN_GetErrorText == NULL)
        return false;

    return true;
}
//
// Function: Unload DLL
// Parameter: none
// ret value: 0 if OK
//
// unload the DLL and free all pointers
//

int PCANReader::UnloadDLL(){
    if(hdll){
        FreeLibrary(hdll);
        hdll = NULL;
        g_CAN_Initialize = NULL;
        g_CAN_Uninitialize = NULL;
        g_CAN_Reset = NULL;
        g_CAN_GetStatus = NULL;
        g_CAN_Read = NULL;
        g_CAN_Write = NULL;
        g_CAN_FilterMessages = NULL;
        g_CAN_GetValue = NULL;
        g_CAN_SetValue = NULL;
        g_CAN_GetErrorText = NULL;
        return 1;
    }
    return 0;
}
