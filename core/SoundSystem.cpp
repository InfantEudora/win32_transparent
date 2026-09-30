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
    FinishInitialise();
}

bool SoundSystem::InitialiseOffline(uint32_t sample_rate, uint32_t channels){
    ma_engine_config config = ma_engine_config_init();
    //No device means no rate or channel count to inherit, so both have to be given.
    config.noDevice = MA_TRUE;
    config.sampleRate = sample_rate;
    config.channels = channels;
    ma_result result = ma_engine_init(&config, &engine);
    if (result != MA_SUCCESS){
        debug->Err("ma_engine_init (offline) failed (%s)\n",ma_result_description(result));
        return false;
    }
    f_offline = true;
    debug->Info("Offline sound engine: %u Hz, %u channels\n",sample_rate,channels);
    return FinishInitialise();
}

/*
    The master bus, which every sound feeds. Made here rather than on first use so that it is
    ALWAYS there: a voice with nowhere to attach would go straight to the endpoint and skip the
    game's volume, which is the kind of thing nobody notices until the settings slider does not
    turn one sound down.
*/
bool SoundSystem::FinishInitialise(){
    ma_sound_group_config config = ma_sound_group_config_init_2(&engine);
    config.volumeSmoothTimeInPCMFrames = SmoothFrames();
    SoundBus& master = buses[SOUND_BUS_MASTER];
    ma_result result = ma_sound_group_init_ex(&engine,&config,&master.group);
    if (result != MA_SUCCESS){
        debug->Err("Could not make the master sound bus (%s) - continuing without sound\n",
                   ma_result_description(result));
        ma_engine_uninit(&engine);
        return false;
    }
    master.f_active = true;
    master.name = "master";
    master.parent = -1;
    master.gain = 1.0f;
    debug->Info("%i sound voices, %i buffer slots, %i buses\n",NUM_SOUND_VOICES,NUM_SOUND_BUFFERS,NUM_SOUND_BUSES);
    f_initialised = true;
    return true;
}

ma_uint32 SoundSystem::SmoothFrames(){
    return (ma_uint32)(SOUND_GAIN_SMOOTH_SECONDS * (float)ma_engine_get_sample_rate(&engine));
}

uint64_t SoundSystem::Render(float* out, uint64_t frames){
    if (!f_initialised || !f_offline || !out){
        return 0;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&engine,out,frames,&read);
    return (uint64_t)read;
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
    //Children before parents - see SoundBus - and all of them before the engine they live in.
    for (int i = NUM_SOUND_BUSES - 1;i >= 0;i--){
        if (buses[i].f_active){
            ma_sound_group_uninit(&buses[i].group);
            buses[i].f_active = false;
        }
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
    //Held by SetPaused: stopped, not finished - see the declaration.
    if (voice->f_held){
        return true;
    }
    //ma_sound_is_playing takes a non-const pointer, but only reads a flag.
    return ma_sound_is_playing((ma_sound*)&voice->sound) == MA_TRUE;
}

void SoundSystem::ReleaseVoice(SoundVoice* voice){
    if (voice->f_active){
        //Order matters: the sound reads through the ref, so the sound goes first.
        ma_sound_uninit(&voice->sound);
        if (!voice->f_stream) ma_audio_buffer_ref_uninit(&voice->ref);
        voice->f_active = false;
    }
    voice->f_stream = false;
    voice->owner = SOUND_INVALID_HANDLE;
    voice->f_held = false;
    voice->f_keep = false;
    voice->buffer = -1;
    voice->name.clear();
    voice->bus = SOUND_BUS_MASTER;
    voice->gain = 1.0f;
    voice->pitch = 1.0f;
    voice->pan = 0.0f;
    voice->f_looping = false;
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
            //The SAME name for the same file is nothing new, and quiet: a game with a cue table
            //per scene registers every sound once per scene, and would log each one each time.
            auto known = map_handles.find(handle_name);
            if (known != map_handles.end() && known->second == (int)i){
                return;
            }
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

    //With the running total, because decoded PCM is what the buffers actually cost - see
    //NUM_SOUND_BUFFERS.
    size_t total = 0;
    for (size_t i = 0;i < buffers.size();i++){
        total += buffers[i].pcm.size();
    }
    debug->Info("Loaded sound '%s' (%s): %u Hz, %u channels, %llu frames, %zu KB (%zu KB in %zu sounds)\n",
                handle_name,filename,(ma_uint32)sample_rate,(ma_uint32)channels,
                (unsigned long long)buffers.back().frame_count,buffers.back().pcm.size() / 1024,
                total / 1024,buffers.size());
}

float SoundSystem::LengthOf(const char* handle_name){
    int buffer_index = FindBufferByName(handle_name);
    if (buffer_index < 0){
        return -1.0f;
    }
    const SoundBuffer& sb = buffers[buffer_index];
    return (sb.sample_rate > 0) ? (float)sb.frame_count / (float)sb.sample_rate : 0.0f;
}

float SoundSystem::LoudestAt(const char* handle_name, float window){
    int buffer_index = FindBufferByName(handle_name);
    if (buffer_index < 0){
        return -1.0f;
    }
    const SoundBuffer& sb = buffers[buffer_index];
    if (sb.channels == 0 || sb.sample_rate == 0 || sb.frame_count == 0){
        return -1.0f;
    }
    const int16_t* pcm = (const int16_t*)sb.pcm.data();
    ma_uint64 step = (ma_uint64)(window * (float)sb.sample_rate);
    if (step < 1){
        step = 1;
    }
    double best = -1.0;
    ma_uint64 best_at = 0;
    for (ma_uint64 start = 0; start + step <= sb.frame_count; start += step){
        double sum = 0.0;
        for (ma_uint64 f = start; f < start + step; f++){
            for (ma_uint32 c = 0; c < sb.channels; c++){
                double s = (double)pcm[f * sb.channels + c];
                sum += s * s;
            }
        }
        if (sum > best){
            best = sum;
            best_at = start;
        }
    }
    return (float)best_at / (float)sb.sample_rate;
}

soundhandle_t SoundSystem::Play(const char* handle_name, bool looping, float gain, uint32_t flags, float start_seconds){
    SoundParams params;
    params.f_looping = looping;
    params.gain = gain;
    params.flags = flags;
    params.start_seconds = start_seconds;
    return Play(handle_name,params);
}

ma_node* SoundSystem::BusNode(int& bus){
    if (bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        debug->Err("No sound bus %i - playing on the master\n",bus);
        bus = SOUND_BUS_MASTER;
    }
    return (ma_node*)&buses[bus].group;
}

soundhandle_t SoundSystem::Play(const char* handle_name, const SoundParams& params){
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
        Pan still works without it: miniaudio's panner is its own stage after the spatializer.
        Pitch is left enabled: it is the same resampler that does the rate conversion above.

        Attached to its bus rather than to the endpoint, and with its gain smoothed - see
        SOUND_GAIN_SMOOTH_SECONDS. The gain set just below is the first one, which miniaudio
        takes as-is, so the smoothing does not soften the attack.
    */
    int bus = params.bus;
    ma_sound_config config = ma_sound_config_init_2(&engine);
    config.pDataSource = &voice->ref;
    config.pInitialAttachment = BusNode(bus);
    config.initialAttachmentInputBusIndex = 0;
    config.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    config.volumeSmoothTimeInPCMFrames = SmoothFrames();
    result = ma_sound_init_ex(&engine,&config,&voice->sound);
    if (result != MA_SUCCESS){
        debug->Err("ma_sound_init_ex failed for '%s' (%s)\n",
                   handle_name,ma_result_description(result));
        ma_audio_buffer_ref_uninit(&voice->ref);
        return SOUND_INVALID_HANDLE;
    }
    voice->f_active = true;
    voice->buffer = buffer_index;
    voice->name = handle_name;
    voice->bus = bus;
    voice->f_looping = params.f_looping;

    voice->gain = params.gain;
    ma_sound_set_volume(&voice->sound,params.gain);
    ma_sound_set_looping(&voice->sound,params.f_looping ? MA_TRUE : MA_FALSE);
    voice->pitch = (params.pitch > 0.01f) ? params.pitch : 0.01f;
    if (voice->pitch != 1.0f){
        ma_sound_set_pitch(&voice->sound,voice->pitch);
    }
    voice->pan = (params.pan < -1.0f) ? -1.0f : ((params.pan > 1.0f) ? 1.0f : params.pan);
    if (voice->pan != 0.0f){
        ma_sound_set_pan(&voice->sound,voice->pan);
    }
    //Partway in - see Play. In the buffer's own frames, which is what the data source counts in.
    if (params.start_seconds > 0.0f){
        ma_uint64 frame = (ma_uint64)(params.start_seconds * (float)sb.sample_rate);
        if (frame >= sb.frame_count){
            frame = (sb.frame_count > 0) ? sb.frame_count - 1 : 0;
        }
        ma_sound_seek_to_pcm_frame(&voice->sound,frame);
    }

    if (!StartVoice(voice)){
        debug->Err("ma_sound_start failed for '%s'\n",handle_name);
        ReleaseVoice(voice);
        return SOUND_INVALID_HANDLE;
    }

    voice->owner = next_handle++;
    voice->f_keep = ((params.flags & SOUND_KEEP) != 0);
    voice->started = play_counter++;
    return voice->owner;
}

soundhandle_t SoundSystem::PlayStream(ma_data_source* source, float gain, int bus){
    if (!f_initialised || !source){
        return SOUND_INVALID_HANDLE;
    }
    SoundVoice* voice = AcquireVoice();
    if (!voice){
        debug->Err("No free sound voice for a stream: all %i are SOUND_KEEP voices\n",NUM_SOUND_VOICES);
        return SOUND_INVALID_HANDLE;
    }
    ReleaseVoice(voice);

    //The same config as Play, for the same reasons: nothing positional, on a bus, smoothed.
    ma_sound_config config = ma_sound_config_init_2(&engine);
    config.pDataSource = source;
    config.pInitialAttachment = BusNode(bus);
    config.initialAttachmentInputBusIndex = 0;
    config.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    config.volumeSmoothTimeInPCMFrames = SmoothFrames();
    ma_result result = ma_sound_init_ex(&engine,&config,&voice->sound);
    if (result != MA_SUCCESS){
        debug->Err("ma_sound_init_ex failed for a stream (%s)\n",ma_result_description(result));
        return SOUND_INVALID_HANDLE;
    }
    voice->f_active = true;
    voice->f_stream = true;
    voice->name = "(stream)";
    voice->bus = bus;
    voice->gain = gain;
    ma_sound_set_volume(&voice->sound,gain);

    if (!StartVoice(voice)){
        debug->Err("ma_sound_start failed for a stream\n");
        ReleaseVoice(voice);
        return SOUND_INVALID_HANDLE;
    }
    voice->owner = next_handle++;
    voice->f_keep = true;
    voice->started = play_counter++;
    return voice->owner;
}

uint32_t SoundSystem::GetSampleRate(){
    return f_initialised ? ma_engine_get_sample_rate(&engine) : 0;
}

void SoundSystem::Stop(soundhandle_t handle){
    SoundVoice* voice = FindVoice(handle);
    if (!voice){
        return;
    }
    //Released here and not merely stopped: this is the call that gives a kept voice back.
    ReleaseVoice(voice);
}

void SoundSystem::SetPaused(bool paused){
    SetBusPaused(SOUND_BUS_MASTER,paused);
}

void SoundSystem::SetBusPaused(int bus, bool paused){
    if (!f_initialised || bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        return;
    }
    //Called every pass by a game keeping sound in simulated time, and almost always a no-op.
    if (buses[bus].f_held == paused){
        return;
    }
    buses[bus].f_held = paused;
    ApplyHolds();
}

bool SoundSystem::IsBusPaused(int bus){
    if (bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        return false;
    }
    return buses[bus].f_held;
}

bool SoundSystem::BusHeld(int bus){
    //Parents are always lower slots than their children (AddBus), so this walk ends.
    while (bus >= 0 && bus < NUM_SOUND_BUSES && buses[bus].f_active){
        if (buses[bus].f_held){
            return true;
        }
        bus = buses[bus].parent;
    }
    return false;
}

void SoundSystem::ApplyHolds(){
    for (int i = 0; i < NUM_SOUND_VOICES; i++){
        SoundVoice* v = &voices[i];
        if (!v->f_active){
            continue;
        }
        bool f_hold = BusHeld(v->bus);
        //Only a voice that is PLAYING is held: one its owner paused stays that owner's to resume.
        if (f_hold && !v->f_held && ma_sound_is_playing(&v->sound) == MA_TRUE){
            //ma_sound_stop keeps the cursor, which is what makes this a hold rather than an end.
            ma_sound_stop(&v->sound);
            v->f_held = true;
        }else if (!f_hold && v->f_held){
            ma_sound_start(&v->sound);
            v->f_held = false;
        }
    }
}

/*
    The last step of Play and PlayStream. A voice whose bus is held is marked held and NOT started,
    so it waits at its first sample - or at start_seconds - for the bus to be released, exactly as
    if it had been playing when the hold came. Starting it and stopping it again at once would not
    do: the mixer thread can take a block in between.
*/
bool SoundSystem::StartVoice(SoundVoice* voice){
    if (BusHeld(voice->bus)){
        voice->f_held = true;
        return true;
    }
    return ma_sound_start(&voice->sound) == MA_SUCCESS;
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

void SoundSystem::SetGain(soundhandle_t handle, float gain){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    voice->gain = gain;
    ma_sound_set_volume(&voice->sound,gain);
}

void SoundSystem::SetPitch(soundhandle_t handle, float pitch){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    voice->pitch = (pitch > 0.01f) ? pitch : 0.01f;
    ma_sound_set_pitch(&voice->sound,voice->pitch);
}

void SoundSystem::SetPan(soundhandle_t handle, float pan){
    SoundVoice* voice = FindVoice(handle);
    if (!voice || !voice->f_active){
        return;
    }
    voice->pan = (pan < -1.0f) ? -1.0f : ((pan > 1.0f) ? 1.0f : pan);
    ma_sound_set_pan(&voice->sound,voice->pan);
}

int SoundSystem::FindBus(const char* name){
    if (!name){
        return -1;
    }
    for (int i = 0;i < NUM_SOUND_BUSES;i++){
        if (buses[i].f_active && buses[i].name == name){
            return i;
        }
    }
    return -1;
}

const char* SoundSystem::GetBusName(int bus){
    if (bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        return "";
    }
    return buses[bus].name.c_str();
}

int SoundSystem::AddBus(const char* name, int parent){
    if (!f_initialised || !name){
        return -1;
    }
    int existing = FindBus(name);
    if (existing >= 0){
        return existing;
    }
    if (parent < 0 || parent >= NUM_SOUND_BUSES || !buses[parent].f_active){
        debug->Err("Cannot add sound bus '%s': no parent bus %i\n",name,parent);
        return -1;
    }
    //The first free slot. Slots are never freed, so it is always above every existing bus -
    //which is what keeps the destructor's reverse-order teardown children-first.
    int slot = -1;
    for (int i = 1;i < NUM_SOUND_BUSES;i++){
        if (!buses[i].f_active){
            slot = i;
            break;
        }
    }
    if (slot < 0){
        debug->Err("Cannot add sound bus '%s': all %i are in use\n",name,NUM_SOUND_BUSES);
        return -1;
    }
    SoundBus& bus = buses[slot];
    ma_sound_group_config config = ma_sound_group_config_init_2(&engine);
    config.pInitialAttachment = (ma_node*)&buses[parent].group;
    config.initialAttachmentInputBusIndex = 0;
    config.volumeSmoothTimeInPCMFrames = SmoothFrames();
    ma_result result = ma_sound_group_init_ex(&engine,&config,&bus.group);
    if (result != MA_SUCCESS){
        debug->Err("Could not make sound bus '%s' (%s)\n",name,ma_result_description(result));
        return -1;
    }
    bus.f_active = true;
    bus.name = name;
    bus.parent = parent;
    bus.gain = 1.0f;
    debug->Info("Sound bus %i '%s', feeding '%s'\n",slot,name,buses[parent].name.c_str());
    return slot;
}

void SoundSystem::SetBusGain(int bus, float gain){
    if (bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        return;
    }
    buses[bus].gain = gain;
    ma_sound_group_set_volume(&buses[bus].group,gain);
}

float SoundSystem::GetBusGain(int bus){
    if (bus < 0 || bus >= NUM_SOUND_BUSES || !buses[bus].f_active){
        return 0.0f;
    }
    return buses[bus].gain;
}

void SoundSystem::ListVoices(std::vector<SoundVoiceInfo>& out){
    out.clear();
    for (int i = 0;i < NUM_SOUND_VOICES;i++){
        SoundVoice* v = &voices[i];
        if (v->owner == SOUND_INVALID_HANDLE || !VoiceIsPlaying(v)){
            continue;
        }
        SoundVoiceInfo info;
        info.handle = v->owner;
        info.name = v->name;
        info.bus = v->bus;
        info.gain = v->gain;
        info.pitch = v->pitch;
        info.pan = v->pan;
        info.f_looping = v->f_looping;
        info.f_keep = v->f_keep;
        info.f_held = v->f_held;
        //In the buffer's own frames, like the seek in Play - the data source counts in those.
        if (v->buffer >= 0 && v->buffer < (int)buffers.size()){
            const SoundBuffer& sb = buffers[v->buffer];
            ma_uint64 cursor = 0;
            ma_data_source_get_cursor_in_pcm_frames(&v->ref,&cursor);
            if (sb.sample_rate > 0){
                info.position = (float)cursor / (float)sb.sample_rate;
                info.length = (float)sb.frame_count / (float)sb.sample_rate;
            }
        }
        out.push_back(info);
    }
}
