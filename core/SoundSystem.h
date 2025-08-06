#ifndef _SOUND_H_
#define _SOUND_H_

#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "BinaryAsset.h"

#include "al.h"
#include "alc.h"
//This magical fun feature
#define INITGUID
#include "knownfolders.h"

#include "WaveFile.h"

#include <map>
#include <string>

/*
    Maybe... use stb_ogg to load stuff.

    For now, wraps around some version of OpenAL.
*/

#define  NUM_AL_BUFFERS 8

class SoundSystem{
public:
    SoundSystem(){};
    ~SoundSystem(){}

    HINSTANCE hdll = NULL;

    ALCdevice* default_device = NULL;
    ALCcontext* ctx = NULL;
    int buffer_index = 0;

    ALuint sources[NUM_AL_BUFFERS];
    ALuint buffers[NUM_AL_BUFFERS];

    std::map<std::string, int>map_handles;


    bool f_initialised;
    void Initialise();

    void AppendFile(const char* filename, const char* handle);
    void Play(const char* handle_name, bool looping = false, float gain=1.0f);
    void Pause(const char* handle_name);
    void Rewind(const char* handle_name);
    bool FinishedPlaying(const char* handle_name);
};

#endif