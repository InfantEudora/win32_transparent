#include "WaveFile.h"

#include "Debug.h"
static Debugger *debug = new Debugger("WaveFile", DEBUG_ALL);

WaveFile::WaveFile(){

}

WaveFile::~WaveFile(){
    if (file_data){
        free(file_data);
    }
}

bool WaveFile::LoadWaveFile(const char* filename){
    file_data = LoadFile(filename, &file_size);
    if (file_data && file_size >= sizeof(wavefile_header_t)){
        wav_data = file_data + sizeof(wavefile_header_t);
        header = (wavefile_header_t*)file_data;
        debug->Info("Loaded %s: %i %i-bit channels at %i Hz\n",filename,(int)header->num_channels,(int)header->bits_per_sample,(int)header->sample_rate);
        return true;
    }
    return false;
}

long WaveFile::GetDataLength(){
    if (header){
        return header->data_length;
    }
    return -1;
}

long WaveFile::GetSampleRate(){
    if (header){
        return header->sample_rate;
    }
    return 0;
}

int WaveFile::GetNumChannels(){
    if (header){
        return header->num_channels;
    }
    return 0;
}