#include <stdio.h>
#include "DeltaSM15K.h"

#include "Debug.h"
static Debugger *debug = new Debugger("DeltaSM15K", DEBUG_ALL);

DeltaSM15K::DeltaSM15K(){

}

void DeltaSM15K::Connect(){
    std::string ipstring = "192.168.0." + std::to_string(ipv4[3]);
    std::string portstring = "8462";
    socket.Connect(ipstring, portstring);
}

bool DeltaSM15K::IsConnected(){
    return socket.IsConnected();
}

void DeltaSM15K::SendDeltaMessage(){
    //Socket should be in connected state...

    std::string query = "*IDN?\n";
    socket.SendString(query);
}

void DeltaSM15K::SetOutputVoltage(float voltage){
    std::string query = "SOURce:VOLtage " + std::to_string(voltage) + "\n";
    socket.SendString(query);

    query = "MEASure:VOLtage?\n";
    socket.SendString(query);
}

void DeltaSM15K::SetOutputCurrent(float current){
    std::string query = "SOURce:CURrent " + std::to_string(current) + "\n";
    socket.SendString(query);

    query = "MEASure:CURrent?\n";
    socket.SendString(query);
}

void DeltaSM15K::EnableOutput(bool enable){
    std::string query = "OUTPut " + std::to_string((int)enable) + "\n";
    socket.SendString(query);
}

//Empty read
void DeltaSM15K::Flush(){
    //Socket should be in connected state...
    char rsp[256] = {};
    size_t rspsz = 256;

    int res = socket.Receive(rsp,rspsz);
    if (res > 0){
        debug->Info("Flushed %i bytes\n");
    }else{
        debug->Info("No data was flushed\n");
    }
}

void DeltaSM15K::ReadDeltaMessage(){
    char rsp[256];
    size_t rspsz = 256;

    int res = socket.Receive(rsp,rspsz);
    if (res > 0){
        debug->Info("Res = %i | Response: %s",res, rsp);
        if (query_state == 1){
            try{
                output_voltage = std::stof(rsp);
            }catch(...)
            {}
        }else if (query_state == 3){
            try{
                output_current = std::stof(rsp);
            }catch(...)
            {}
        }
    }else{
        debug->Info("No response res == %i\n",res);
    }
}

void DeltaSM15K::SetRemoteState(){
    std::string query = "SYSTem:REMote:CV:STAtus Ethernet\n";
    socket.SendString(query);

    query = "SYSTem:REMote:CV:STAtus?\n";
    socket.SendString(query);

    query = "SYSTem:REMote:CC:STAtus Ethernet\n";
    socket.SendString(query);
}

void DeltaSM15K::UpdateDevice(){

    if (query_state == 0){
        //Set and read output voltage
        Flush();
        SetOutputVoltage(output_voltage_set);
        query_state++;
    }else if (query_state == 1){
        ReadDeltaMessage();
        query_state++;
    }else if (query_state == 2){
        Flush();
        SetOutputCurrent(output_current_set);
        query_state++;
    }else if (query_state == 3){
        ReadDeltaMessage();
        query_state++;
    }else{
        query_state = 0;
    }

}

/*
    SYSTem:REMote:CV:STAtus Ethernet
    SYSTem:REMote:CV:STAtus?

*/