#include "WaveFile.h"

#include "Debug.h"
static Debugger *debug = new Debugger("WaveFile", DEBUG_ALL);

WaveFile::WaveFile(){

}

WaveFile::~WaveFile(){

}

bool WaveFile::LoadWaveFile(const char* filename){
    file_data = LoadFile(filename, &file_size);
    if (file_data && file_size >= sizeof(wavefile_header_t)){
        wav_data = file_data + sizeof(wavefile_header_t);
        return true;
    }
    return false;
}

long WaveFile::GetDataLength(){
    if (file_data && file_size >= sizeof(wavefile_header_t)){
        wavefile_header_t* header = (wavefile_header_t*)file_data;
        return header->data_length;
    }
    return -1;
}