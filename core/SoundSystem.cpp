#include "SoundSystem.h"
#include "WaveFile.h"

#include "Debug.h"
static Debugger *debug = new Debugger("SoundSystem", DEBUG_INFO);

void SoundSystem::Initialise(){
    debug->Info("Initialising sound device with miniaudio\n");

    /*
        A default engine config, which means: default playback device, and the device's own
        sample rate rather than one we impose. Everything this engine loads gets resampled to
        whatever that turns out to be - see AppendFile on why that matters more than it looks.
    */
    ma_result result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS){
        //Not fatal. A machine with no sound device is a machine that should still run the game.
        debug->Err("ma_engine_init failed (%s) - continuing without sound\n",
                   ma_result_description(result));
        return;
    }

    debug->Ok("Got default device: %u Hz, %u channels\n",
              ma_engine_get_sample_rate(&engine),
              ma_engine_get_channels(&engine));
    debug->Info("%i sound voices, %i buffer slots\n",NUM_SOUND_VOICES,NUM_SOUND_BUFFERS);
    f_initialised = true;
}

/*
    Stops the mixer before anything it is reading goes away.

    The OpenAL version never did this - it had no destructor at all, and left the device open and
    a mixer thread running at process exit. That was survivable there and is not here: miniaudio's
    voices point straight at SoundBuffer::pcm, so a mixer thread still running while those vectors
    are destroyed is a read of freed memory on another thread. Voices first, then the engine.
*/
SoundSystem::~SoundSystem(){
    if (!f_initialised){
        return;
    }
    for (int i = 0;i < NUM_SOUND_VOICES;i++){
        ReleaseVoice(&voices[i]);
    }
    ma_engine_uninit(&engine);
    f_initialised = false;
}

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
    for (int i = 0;i < NUM_SOUND_VOICES;i++){
        if (voices[i].owner == handle){
            return &voices[i];
        }
    }
    //Not an error. The sound finished and its voice was recycled, which is the ordinary end of
    //every one-shot - see soundhandle_t on why holding a dead handle is safe.
    return NULL;
}

bool SoundSystem::VoiceIsPlaying(const SoundVoice* voice){
    if (!voice || !voice->f_active){
        return false;
    }
    //ma_sound_is_playing takes a non-const pointer, but only reads a flag.
    return ma_sound_is_playing((ma_sound*)&voice->sound) == MA_TRUE;
}

void SoundSystem::ReleaseVoice(SoundVoice* voice){
    if (voice->f_active){
        //Order matters: the sound reads through the ref, so the sound goes first.
        ma_sound_uninit(&voice->sound);
        ma_audio_buffer_ref_uninit(&voice->ref);
        voice->f_active = false;
    }
    voice->owner = SOUND_INVALID_HANDLE;
    voice->f_keep = false;
}

/*
    Picks the voice a new sound will play on.

    Free first - a voice nobody owns, or one whose one-shot has finished and which is therefore
    already nobody's. Only when there is none does anything get taken, and then it is the OLDEST
    one-shot: the sound that has been going longest is the one nearest its end and the least
    missed. A kept voice is never taken, which is the whole point of the flag.
*/
SoundSystem::SoundVoice* SoundSystem::AcquireVoice(){
    SoundVoice* oldest = NULL;

    for (int i = 0;i < NUM_SOUND_VOICES;i++){
        SoundVoice* v = &voices[i];

        if (v->owner == SOUND_INVALID_HANDLE){
            return v;
        }

        //Reclaim a one-shot that has run out. A KEPT voice is left alone even when it has stopped:
        //its owner asked to hold the voice and may yet rewind or resume it, and only Stop says
        //otherwise.
        if (!v->f_keep){
            if (!VoiceIsPlaying(v)){
                ReleaseVoice(v);
                return v;
            }
            if (!oldest || (v->started < oldest->started)){
                oldest = v;
            }
        }
    }

    if (oldest){
        ReleaseVoice(oldest);
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
    if (!f_initialised){
        debug->Err("Cannot register sound '%s': no audio device\n",handle_name);
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

    if (buffers.size() >= NUM_SOUND_BUFFERS){
        //Was debug->Fatal("I'm lazy: no more sound buffers"), which ended the process on the
        //seventeenth registration. Deduplication above removes most of the pressure that made
        //that reachable, and a sound that cannot load is not a reason to stop running.
        debug->Err("No room for sound '%s' (%s): all %i buffers are in use\n",
                   handle_name,filename,NUM_SOUND_BUFFERS);
        return;
    }

    WaveFile wav;
    if (!wav.LoadWaveFile(filename)){
        debug->Err("Could not load sound file %s for '%s'\n",filename,handle_name);
        return;
    }

    /*
        16-bit only, and now it says so.

        The OpenAL version picked AL_FORMAT_MONO16 or AL_FORMAT_STEREO16 purely off the channel
        count and never looked at the bit depth, so an 8-bit or 24-bit wav was handed over as
        though it were 16-bit and came out as noise. Nothing in the repo is anything but 16-bit,
        which is why that was never noticed; the check costs one comparison.
    */
    const int bits = wav.header ? wav.header->bits_per_sample : 0;
    if (bits != 16){
        debug->Err("Sound '%s' (%s) is %i-bit; only 16-bit PCM is supported\n",
                   handle_name,filename,bits);
        return;
    }

    const long data_length = wav.GetDataLength();
    const int channels = wav.GetNumChannels();
    const long sample_rate = wav.GetSampleRate();
    if (!wav.wav_data || data_length <= 0 || channels <= 0 || sample_rate <= 0){
        debug->Err("Sound '%s' (%s) has no usable audio data\n",handle_name,filename);
        return;
    }

    SoundBuffer sb;
    /*
        COPIED, not borrowed. WaveFile frees its buffer in its destructor and this one goes out of
        scope at the end of this function, while ma_audio_buffer_ref stores the pointer it is
        given and reads through it for as long as a voice is playing. Borrowing here would be a
        use-after-free on the mixer thread, which is about the worst shape a bug can have.
    */
    sb.pcm.assign(wav.wav_data, wav.wav_data + data_length);
    sb.channels = (ma_uint32)channels;
    sb.sample_rate = (ma_uint32)sample_rate;
    sb.frame_count = (ma_uint64)(data_length / (channels * 2));   //2 bytes per sample, 16-bit
    sb.filename = filename;

    map_handles[handle_name] = (int)buffers.size();
    buffers.push_back(std::move(sb));

    debug->Info("Loaded sound '%s' (%s): %u Hz, %u channels, %llu frames\n",
                handle_name,filename,(ma_uint32)sample_rate,(ma_uint32)channels,
                (unsigned long long)buffers.back().frame_count);
}

soundhandle_t SoundSystem::Play(const char* handle_name, bool looping, float gain, uint32_t flags){
    if (!f_initialised){
        return SOUND_INVALID_HANDLE;
    }

    int buffer_index = FindBufferByName(handle_name);
    if (buffer_index < 0){
        return SOUND_INVALID_HANDLE;
    }

    SoundVoice* voice = AcquireVoice();
    if (!voice){
        //Every voice is held by a kept one, so there is nothing to take without cutting off
        //something whose owner explicitly asked that it not be cut off.
        debug->Err("No free sound voice for '%s': all %i are SOUND_KEEP voices\n",
                   handle_name,NUM_SOUND_VOICES);
        return SOUND_INVALID_HANDLE;
    }

    ReleaseVoice(voice);        //idempotent; AcquireVoice may hand back a still-loaded slot

    const SoundBuffer& sb = buffers[buffer_index];

    ma_result result = ma_audio_buffer_ref_init(ma_format_s16, sb.channels,
                                                sb.pcm.data(), sb.frame_count, &voice->ref);
    if (result != MA_SUCCESS){
        debug->Err("ma_audio_buffer_ref_init failed for '%s' (%s)\n",
                   handle_name,ma_result_description(result));
        return SOUND_INVALID_HANDLE;
    }
    /*
        The ref's own rate, which ma_audio_buffer_ref_init does not take and leaves at zero.

        This line is the whole of sample-rate handling and it is not optional: the assets are a
        mix of 44100, 48000 and - shared_assets/sound/hax.wav - 6000 Hz, while the device runs at
        whatever it runs at. Left at zero the data source reports no rate of its own, the engine
        assumes its own, and a 6000 Hz file plays about seven times too fast. With it set,
        miniaudio's data converter resamples per voice.
    */
    voice->ref.sampleRate = sb.sample_rate;

    /*
        NO_SPATIALIZATION because nothing here is positional - there is no listener, no
        AL_POSITION was ever set, and the spatializer would otherwise run per voice for nothing.
        Pitch is left enabled: it is the same resampler that does the rate conversion above.
    */
    result = ma_sound_init_from_data_source(&engine, &voice->ref,
                                            MA_SOUND_FLAG_NO_SPATIALIZATION,
                                            NULL, &voice->sound);
    if (result != MA_SUCCESS){
        debug->Err("ma_sound_init_from_data_source failed for '%s' (%s)\n",
                   handle_name,ma_result_description(result));
        ma_audio_buffer_ref_uninit(&voice->ref);
        return SOUND_INVALID_HANDLE;
    }
    voice->f_active = true;

    ma_sound_set_volume(&voice->sound,gain);
    ma_sound_set_looping(&voice->sound,looping ? MA_TRUE : MA_FALSE);

    result = ma_sound_start(&voice->sound);
    if (result != MA_SUCCESS){
        debug->Err("ma_sound_start failed for '%s' (%s)\n",
                   handle_name,ma_result_description(result));
        ReleaseVoice(voice);
        return SOUND_INVALID_HANDLE;
    }

    voice->owner = next_handle++;
    voice->f_keep = ((flags & SOUND_KEEP) != 0);
    voice->started = play_counter++;
    return voice->owner;
}

void SoundSystem::Stop(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    //Released here and not merely stopped: this is the call that gives a kept voice back.
    ReleaseVoice(voice);
}

void SoundSystem::Pause(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    //ma_sound_stop leaves the cursor where it is, which is a pause - Resume starts from here.
    ma_sound_stop(&voice->sound);
}

void SoundSystem::Resume(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    ma_sound_start(&voice->sound);
}

void SoundSystem::Rewind(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    //alSourceRewind stopped the source AND returned it to the start, so this does both.
    ma_sound_stop(&voice->sound);
    ma_sound_seek_to_pcm_frame(&voice->sound,0);
}

bool SoundSystem::FinishedPlaying(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        //Nothing is playing under a handle nobody holds. Reporting "still playing" would hang any
        //caller that waits on this before moving on.
        return true;
    }
    return !VoiceIsPlaying(voice);
}

int SoundSystem::GetNumPlaying(){
    int num = 0;
    for (int i = 0;i < NUM_SOUND_VOICES;i++){
        if (voices[i].owner == SOUND_INVALID_HANDLE){
            continue;
        }
        if (VoiceIsPlaying(&voices[i])){
            num++;
        }
    }
    return num;
}
