#ifndef _CUESOUNDOUTPUT_H_
#define _CUESOUNDOUTPUT_H_

#include <string.h>

#include "CueSystem.h"
#include "SoundSystem.h"

/*
    The cue layer's sounds, played on the engine's SoundSystem.

    HEADER-ONLY ON PURPOSE. CueSystem is core and compiled once for every app; SoundSystem is
    compiled only into apps built with USE_SOUND. So the one piece that names both lives here,
    compiled inside the app that includes it - an app with sound includes this and hands
    CueSystem::Init an instance, an app without it passes NULL and the cues still decide and log.

    A NULL SoundSystem is allowed too, for a sound app whose device failed: every call answers as
    though the sound did not exist, which is the same thing CueSystem does with no output.
*/
class CueSoundOutput : public CueOutput{
public:
    explicit CueSoundOutput(SoundSystem* s = NULL) : sound(s) {}
    SoundSystem* sound;

    void RegisterSound(const char* name, const char* file) override {
        if (sound) sound->AppendFile(file,name);
    }
    float LoudestAt(const char* name) override {
        return sound ? sound->LoudestAt(name) : -1.0f;
    }
    float LengthOf(const char* name) override {
        return sound ? sound->LengthOf(name) : -1.0f;
    }
    uint32_t Play(const char* name, const CuePlay& play) override {
        if (!sound) return SOUND_INVALID_HANDLE;
        SoundParams p;
        p.gain = play.gain;
        p.pitch = play.pitch;
        p.pan = play.pan;
        p.bus = play.bus;
        p.f_looping = play.f_looping;
        p.start_seconds = play.from;
        return sound->Play(name,p);
    }
    void Stop(uint32_t handle) override {
        if (sound) sound->Stop(handle);
    }
    void SetGain(uint32_t handle, float gain) override {
        if (sound) sound->SetGain(handle,gain);
    }
    void SetPitch(uint32_t handle, float pitch) override {
        if (sound) sound->SetPitch(handle,pitch);
    }
    int AddBus(const char* name, const char* parent) override {
        if (!sound) return -1;
        int parent_id = SOUND_BUS_MASTER;
        if (parent && parent[0] && strcmp(parent,"master") != 0){
            parent_id = sound->FindBus(parent);
            if (parent_id < 0) return -1;
        }
        return sound->AddBus(name,parent_id);
    }
    void SetBusGain(int bus, float gain) override {
        if (sound) sound->SetBusGain(bus,gain);
    }
};

#endif
