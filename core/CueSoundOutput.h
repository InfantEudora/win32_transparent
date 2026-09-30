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

    A SCENE'S OUTPUT: given a `layer` bus, everything this output plays goes under it. A game with
    a cue table per scene gives each its own output over its own layer, and then one hold or one
    gain on the layer is that whole scene's sound - see SoundSystem::SetBusPaused.
      - the table's "master" is the layer, for a bus's parent and for a cue that names no bus;
      - the table's buses are made as "<layer>/<name>", because SoundSystem's names are global
        and AddBus hands back an existing bus by name: two scenes' "effects" would otherwise be
        one bus, and a duck in one scene would turn down the other.
    Without a layer (the master), it is exactly the one-table output it always was.
*/
class CueSoundOutput : public CueOutput{
public:
    explicit CueSoundOutput(SoundSystem* s = NULL, int layer_bus = SOUND_BUS_MASTER)
        : sound(s), layer(layer_bus) {}
    SoundSystem* sound;
    int layer;

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
        //CueSystem's bus 0 is "no bus of its own", which for a scene's output is the scene.
        p.bus = (play.bus == SOUND_BUS_MASTER) ? layer : play.bus;
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
    void SetPan(uint32_t handle, float pan) override {
        if (sound) sound->SetPan(handle,pan);
    }
    int AddBus(const char* name, const char* parent) override {
        if (!sound) return -1;
        int parent_id = layer;
        if (parent && parent[0] && strcmp(parent,"master") != 0){
            parent_id = sound->FindBus(Scoped(parent).c_str());
            if (parent_id < 0) return -1;
        }
        return sound->AddBus(Scoped(name).c_str(),parent_id);
    }
    void SetBusGain(int bus, float gain) override {
        if (sound) sound->SetBusGain(bus,gain);
    }

private:
    //A table's bus name as the SoundSystem knows it: under the layer's name, unless there is none.
    std::string Scoped(const char* name){
        if (layer == SOUND_BUS_MASTER) return name;
        return std::string(sound->GetBusName(layer)) + "/" + name;
    }
};

#endif
