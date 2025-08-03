
#include "SoundSystem.h"
#include "WaveFile.h"



#include "Debug.h"
static Debugger *debug = new Debugger("SoundSystem", DEBUG_INFO);

//Funtion pointers that we load from DLL
LPALCCREATECONTEXT alcCreateContext;
LPALCOPENDEVICE alcOpenDevice;
LPALGENBUFFERS alGenBuffers;
LPALGETERROR alGetError;
LPALCMAKECONTEXTCURRENT alcMakeContextCurrent;
LPALISEXTENSIONPRESENT alIsExtensionPresent;
LPALBUFFERDATA alBufferData;

LPALGENSOURCES alGenSources;
LPALSOURCEI alSourcei;
LPALGETSOURCEI alGetSourcei;

LPALSOURCEPLAY alSourcePlay;
LPALSOURCEPAUSE alSourcePause;
LPALSOURCEREWIND alSourceRewind;

bool GetALFunctions(HINSTANCE hdll){
    if (hdll == NULL)
        return false;

    alcCreateContext = (LPALCCREATECONTEXT)GetProcAddress(hdll, "alcCreateContext");
    if (alcCreateContext == NULL)
        return false;

    alcMakeContextCurrent = (LPALCMAKECONTEXTCURRENT)GetProcAddress(hdll, "alcMakeContextCurrent");
    if (alcMakeContextCurrent == NULL)
        return false;

    alIsExtensionPresent = (LPALISEXTENSIONPRESENT)GetProcAddress(hdll, "alIsExtensionPresent");
    if (alIsExtensionPresent == NULL)
        return false;

    alcOpenDevice = (LPALCOPENDEVICE)GetProcAddress(hdll, "alcOpenDevice");
    if (alcOpenDevice == NULL)
        return false;

    alGenBuffers = (LPALGENBUFFERS)GetProcAddress(hdll, "alGenBuffers");
    if (alGenBuffers == NULL)
        return false;

    alBufferData = (LPALBUFFERDATA)GetProcAddress(hdll, "alBufferData");
    if (alBufferData == NULL)
        return false;

    alGetError = (LPALGETERROR)GetProcAddress(hdll, "alGetError");
    if (alGetError == NULL)
        return false;

    //Sources
    alGenSources = (LPALGENSOURCES)GetProcAddress(hdll, "alGenSources");
    if (alGenSources == NULL)
        return false;
    alSourcei = (LPALSOURCEI)GetProcAddress(hdll, "alSourcei");
    if (alSourcei == NULL)
        return false;
    alGetSourcei = (LPALGETSOURCEI)GetProcAddress(hdll, "alGetSourcei");
    if (alGetSourcei == NULL)
        return false;
    alSourcePlay = (LPALSOURCEPLAY)GetProcAddress(hdll, "alSourcePlay");
    if (alSourcePlay == NULL)
        return false;
    alSourcePause = (LPALSOURCEPAUSE)GetProcAddress(hdll, "alSourcePause");
    if (alSourcePause == NULL)
        return false;
    alSourceRewind = (LPALSOURCEREWIND)GetProcAddress(hdll, "alSourceRewind");
    if (alSourceRewind == NULL)
        return false;


    return true;
}

void SoundSystem::Initialise(){
    const char* dllname = "soft_oal.dll";

    hdll = LoadLibrary(dllname);
    if (hdll == NULL) {
        debug->Err("Could not load %s\n",dllname);
        return;
    }else{
        debug->Ok("DLL Handle: %p\n", hdll);
        // call the getFunction to read the Function Pointers of the DLL
        if (GetALFunctions(hdll) == true) {
            debug->Ok("Loaded function adresses for %s\n",dllname);
        }else{
            debug->Err("Could not load function adresses\n");
            return;
        }
    }

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
        alGenBuffers((ALsizei)NUM_AL_BUFFERS, buffers);
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


        //create a source

        alGenSources(NUM_AL_BUFFERS, sources);
        debug->Info("Generated %i sources\n",NUM_AL_BUFFERS);

        //alSourcei(sources[0], AL_BUFFER, buffers[0]);
        //alSourcei(sources[0],AL_LOOPING,AL_TRUE);

        //buffer_index++;

    }
}

//Loads file into memory and stores it by handle.
void SoundSystem::AppendFile(const char* filename, const char* handle_name){
    if (buffer_index >= NUM_AL_BUFFERS){
        debug->Fatal("I'm lazy: no more sound buffers\n");
    }

    WaveFile wav;
    if (wav.LoadWaveFile(filename)){
        ALenum format;
        if (wav.GetNumChannels() == 1){
            format = AL_FORMAT_MONO16;
        }else{
            format = AL_FORMAT_STEREO16;
        }
        //Load into buffer
        alBufferData(buffers[buffer_index],AL_FORMAT_STEREO16,wav.wav_data,wav.GetDataLength(),wav.GetSampleRate());

        map_handles[handle_name] = buffer_index;

        buffer_index++;
    }
}

void SoundSystem::Play(const char* handle_name, bool looping){
    int handle = map_handles[handle_name];
    debug->Trace("Lookup handle %s -> %i\n",handle_name,handle);
    alSourcei(sources[handle], AL_BUFFER, buffers[handle]);

    alSourcei(sources[handle],AL_LOOPING,looping);
    alSourcePlay(sources[handle]);
}

void SoundSystem::Pause(const char* handle_name){
    int handle = map_handles[handle_name];
    debug->Trace("Lookup handle %s -> %i\n",handle_name,handle);
    alSourcePause(sources[handle]);
}
void SoundSystem::Rewind(const char* handle_name){
    int handle = map_handles[handle_name];
    debug->Trace("Lookup handle %s -> %i\n",handle_name,handle);
    alSourceRewind(sources[handle]);
}

bool SoundSystem::FinishedPlaying(const char* handle_name){
    int handle = map_handles[handle_name];
    debug->Trace("Lookup handle %s -> %i\n",handle_name,handle);
    int state = 0;
    alGetSourcei(sources[handle],AL_SOURCE_STATE,&state);
    if (state != AL_PLAYING){
        return true;
    }
    return false;
}