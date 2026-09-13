#include "SoundSystem.h"
#include "WaveFile.h"

#include "Debug.h"
static Debugger *debug = new Debugger("SoundSystem", DEBUG_INFO);

void SoundSystem::Initialise(){
    debug->Info("Initialising sound device with OpenAL\n");
    default_device = alcOpenDevice(NULL);
    if (default_device){
        debug->Ok("Got default device.\n");
        ctx = alcCreateContext(default_device,NULL);
        if (!ctx){
            debug->Err("Could not get context.\n");
        }
        debug->Info("CTX = %p\n",ctx);

        alcMakeContextCurrent(ctx);

        // Generate Buffers
        ALenum error = alGetError(); // clear error code
        debug->Ok("alGetError : %08X\n", error);
        alGenBuffers((ALsizei)NUM_AL_BUFFERS, al_buffers);
        if ((error = alGetError()) != AL_NO_ERROR){
            debug->Err("alGenBuffers : %08X\n", error);
            return;
        }else{
            debug->Ok("alGenBuffers : Generated %lu buffers\n", NUM_AL_BUFFERS);
        }

        // Check for EAX 2.0 support
        bool ext_EAX = alIsExtensionPresent("EAX2.0");
        if (ext_EAX){
            debug->Ok("EAX 2.0 support\n");
        }
        //The voice pool. Sources are the scarce thing - they are what can be AUDIBLE at
        //once - so they are counted separately from buffers now rather than sharing one
        //ceiling with them.
        ALuint pool[NUM_AL_SOURCES];
        alGenSources(NUM_AL_SOURCES, pool);
        for (int i = 0;i < NUM_AL_SOURCES;i++){
            voices[i].source = pool[i];
            voices[i].owner = SOUND_INVALID_HANDLE;
            voices[i].f_keep = false;
        }
        debug->Info("Generated %i sound sources\n",NUM_AL_SOURCES);
        f_initialised = true;
    }
}

//Loads file into memory and stores it by handle.
int SoundSystem::FindBufferByName(const char* handle_name){
    if (!handle_name){
        return -1;
    }
    std::map<std::string,int>::iterator it = map_handles.find(handle_name);
    if (it == map_handles.end()){
        debug->Err("No sound registered under the name '%s'\n",handle_name);
        return -1;
    }
    return it->second;
}

SoundSystem::SoundVoice* SoundSystem::FindVoice(soundhandle_t handle){
    if (handle == SOUND_INVALID_HANDLE){
        return NULL;
    }
    for (int i = 0;i < NUM_AL_SOURCES;i++){
        if (voices[i].owner == handle){
            return &voices[i];
        }
    }
    //Not an error. The sound finished and its source was recycled, which is the ordinary end of
    //every one-shot - see soundhandle_t on why holding a dead handle is safe.
    return NULL;
}

/*
    Picks the source a new sound will play on.

    Free first - a source nobody owns, or one whose one-shot has finished and which is therefore
    already nobody's. Only when there is none does anything get taken, and then it is the OLDEST
    one-shot: the sound that has been going longest is the one nearest its end and the least
    missed. A kept voice is never taken, which is the whole point of the flag.
*/
SoundSystem::SoundVoice* SoundSystem::AcquireVoice(){
    SoundVoice* oldest = NULL;

    for (int i = 0;i < NUM_AL_SOURCES;i++){
        SoundVoice* v = &voices[i];

        if (v->owner == SOUND_INVALID_HANDLE){
            return v;
        }

        //Reclaim a one-shot that has run out. A KEPT voice is left alone even when it has stopped:
        //its owner asked to hold the source and may yet rewind or resume it, and only Stop says
        //otherwise.
        if (!v->f_keep){
            int state = 0;
            alGetSourcei(v->source,AL_SOURCE_STATE,&state);
            if (state != AL_PLAYING){
                v->owner = SOUND_INVALID_HANDLE;
                return v;
            }
            if (!oldest || (v->started < oldest->started)){
                oldest = v;
            }
        }
    }

    if (oldest){
        alSourceStop(oldest->source);
        return oldest;
    }
    return NULL;
}

/*
    Registers a sound under a name.

    DEDUPLICATED BY FILENAME, which is the point. Registering "brick_a", "brick_b" and "brick_c"
    for one wav - the old way to get three simultaneous voices out of a 1:1 system - now costs one
    buffer and one load rather than three of each. Nothing has to change at the call site for that
    to be true, and the duplicate load that made backlog item 53's heap corruption reachable is
    simply not performed.
*/
void SoundSystem::AppendFile(const char* filename, const char* handle_name){
    if (!filename || !handle_name){
        return;
    }

    //Already loaded under some other name? Then this name is another way to say the same buffer.
    for (size_t i = 0;i < buffers.size();i++){
        if (buffers[i].filename.compare(filename) == 0){
            map_handles[handle_name] = (int)i;
            debug->Info("Sound '%s' shares the already loaded %s\n",handle_name,filename);
            return;
        }
    }

    if (buffers.size() >= NUM_AL_BUFFERS){
        //Was debug->Fatal("I'm lazy: no more sound buffers"), which ended the process on the
        //seventeenth registration. Deduplication above removes most of the pressure that made
        //that reachable, and a sound that cannot load is not a reason to stop running.
        debug->Err("No room for sound '%s' (%s): all %i buffers are in use\n",
                   handle_name,filename,NUM_AL_BUFFERS);
        return;
    }

    WaveFile wav;
    if (!wav.LoadWaveFile(filename)){
        debug->Err("Could not load sound file %s for '%s'\n",filename,handle_name);
        return;
    }

    ALenum format = (wav.GetNumChannels() == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    int index = (int)buffers.size();
    alBufferData(al_buffers[index],format,wav.wav_data,wav.GetDataLength(),wav.GetSampleRate());

    SoundBuffer sb;
    sb.buffer = al_buffers[index];
    sb.filename = filename;
    buffers.push_back(sb);

    map_handles[handle_name] = index;
}

soundhandle_t SoundSystem::Play(const char* handle_name, bool looping, float gain, uint32_t flags){
    int buffer_index = FindBufferByName(handle_name);
    if (buffer_index < 0){
        return SOUND_INVALID_HANDLE;
    }

    SoundVoice* voice = AcquireVoice();
    if (!voice){
        //Every source is held by a kept voice, so there is nothing to take without cutting off
        //something whose owner explicitly asked that it not be cut off.
        debug->Err("No free sound source for '%s': all %i are SOUND_KEEP voices\n",
                   handle_name,NUM_AL_SOURCES);
        return SOUND_INVALID_HANDLE;
    }

    voice->owner = next_handle++;
    voice->f_keep = ((flags & SOUND_KEEP) != 0);
    voice->started = play_counter++;

    alSourcei(voice->source,AL_BUFFER,buffers[buffer_index].buffer);
    alSourcei(voice->source,AL_LOOPING,looping ? AL_TRUE : AL_FALSE);
    alSourcef(voice->source,AL_GAIN,gain);
    alSourcePlay(voice->source);

    return voice->owner;
}

void SoundSystem::Stop(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    alSourceStop(voice->source);
    //Released here and not merely stopped: this is the call that gives a kept source back.
    voice->owner = SOUND_INVALID_HANDLE;
    voice->f_keep = false;
}

void SoundSystem::Pause(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    alSourcePause(voice->source);
}

void SoundSystem::Resume(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    alSourcePlay(voice->source);
}

void SoundSystem::Rewind(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    alSourceRewind(voice->source);
}

bool SoundSystem::FinishedPlaying(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        //Nothing is playing under a handle nobody holds. Reporting "still playing" would hang any
        //caller that waits on this before moving on.
        return true;
    }
    int state = 0;
    alGetSourcei(voice->source,AL_SOURCE_STATE,&state);
    return (state != AL_PLAYING);
}

int SoundSystem::GetNumPlaying(){
    int num = 0;
    for (int i = 0;i < NUM_AL_SOURCES;i++){
        if (voices[i].owner == SOUND_INVALID_HANDLE){
            continue;
        }
        int state = 0;
        alGetSourcei(voices[i].source,AL_SOURCE_STATE,&state);
        if (state == AL_PLAYING){
            num++;
        }
    }
    return num;
}
