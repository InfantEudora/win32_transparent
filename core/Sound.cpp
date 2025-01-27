#include <windows.h>
#include "Sound.h"
#include "WaveFile.h"

#include "Debug.h"
static Debugger *debug = new Debugger("SoundSystem", DEBUG_ALL);

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
LPALSOURCEPLAY alSourcePlay;

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
    alSourcePlay = (LPALSOURCEPLAY)GetProcAddress(hdll, "alSourcePlay");
    if (alSourcePlay == NULL)
        return false;


    return true;
}

void SoundSystem::Initialise(){
    HINSTANCE hdll = NULL;

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
    ALCdevice* default_device = alcOpenDevice(NULL);
    if (default_device){
        debug->Ok("Got default device.\n");
        ALCcontext* ctx = alcCreateContext(default_device,NULL);
        if (!ctx){
            debug->Err("Could not get context.\n");
        }
        debug->Info("CTX = %p\n",ctx);

        alcMakeContextCurrent(ctx);

        // Generate Buffers


        #define  NUM_BUFFERS 8
        ALuint buffers[NUM_BUFFERS];
        ALenum error = alGetError(); // clear error code
        debug->Ok("alGetError : %08X\n", error);
        alGenBuffers((ALsizei)NUM_BUFFERS, buffers);
        if ((error = alGetError()) != AL_NO_ERROR){
            debug->Err("alGenBuffers : %08X\n", error);
            return;
        }else{
            debug->Ok("alGenBuffers : Generated %lu buffers\n", NUM_BUFFERS);
        }


        // Check for EAX 2.0 support
        bool ext_EAX = alIsExtensionPresent("EAX2.0");
        if (ext_EAX){
            debug->Ok("EAX 2.0 support\n");
        }

        //sound* bleep = load_wav("data/sound/bleep.wav",1);

        WaveFile wav;
        wav.LoadWaveFile("data/sound/bleep.wav");

        alBufferData(buffers[0],AL_FORMAT_STEREO16,wav.wav_data,wav.GetDataLength(),44100);

        if ((error = alGetError()) != AL_NO_ERROR){
            debug->Err("alGenBuffers : %08X\n", error);
            return;
        }else{
            debug->Ok("alBufferData : Loaded\n");
        }

        //create a source
        ALuint source;
        alGenSources(1, &source);
        debug->Info("Source = %lu\n",source);

        alSourcei(source, AL_BUFFER, buffers[0]);
        //alSourcei(source,AL_LOOPING,AL_TRUE);

        alSourcePlay(source);

        /*
        // Generate Buffers
        alGetError(); // clear error code
        alGenBuffers(NUM_BUFFERS, g_Buffers);
        if ((error = alGetError()) != AL_NO_ERROR)
        {
        DisplayALError("alGenBuffers :", error);
        return;
        }
        // Load test.wav
        loadWAVFile("test.wav",&format,&data,&size,&freq,&loop);
        if ((error = alGetError()) != AL_NO_ERROR)
        {
        DisplayALError("alutLoadWAVFile test.wav : ", error);
        alDeleteBuffers(NUM_BUFFERS, g_Buffers);
        return;
        }
        // Copy test.wav data into AL Buffer 0
        alBufferData(g_Buffers[0],format,data,size,freq);
        if ((error = alGetError()) != AL_NO_ERROR)
        {
        DisplayALError("alBufferData buffer 0 : ", error);
        alDeleteBuffers(NUM_BUFFERS, g_Buffers);
        return;
        }
        // Unload test.wav
        unloadWAV(format,data,size,freq);
        if ((error = alGetError()) != AL_NO_ERROR)
        {
        DisplayALError("alutUnloadWAV : ", error);
        alDeleteBuffers(NUM_BUFFERS, g_Buffers);
        return;
        }
        // Generate Sources
        alGenSources(1,source);
        if ((error = alGetError()) != AL_NO_ERROR)
        {
        DisplayALError("alGenSources 1 : ", error);
        r
        */


    }
}