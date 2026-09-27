#ifndef _SOUND_H_
#define _SOUND_H_

/*
    Nothing in this header or in SoundSystem.cpp uses <windows.h> any more -- it is left over
    from the OpenAL version, which held an HINSTANCE for the soft_oal.dll it resolved entry
    points out of. Kept rather than deleted so a Windows TU that leaned on it by transit does
    not break, and guarded so the same file compiles for Android.
*/
#if defined(_WIN32)
#include <windows.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "BinaryAsset.h"

/*
    miniaudio_config.h before miniaudio.h, always, everywhere. Some of the defines in it change
    struct layout - MA_NO_RESOURCE_MANAGER removes a member from the middle of ma_engine - so a
    file that included miniaudio.h on its own would agree with libthirdparty.a about the name of
    every field and disagree about where it lives. That is not a link error. See the comment in
    3rdparty/miniaudio_config.h.
*/
#include "miniaudio_config.h"
#include "miniaudio/miniaudio.h"

#include "WaveFile.h"

#include <map>
#include <string>
#include <vector>

/*
    Wraps miniaudio. It used to wrap OpenAL, and the reason for the change was size: OpenAL cost
    about 2.4 MB in a linked binary against the fifteen functions this class actually used, and
    miniaudio covers the same ground for about 179 KB. See docs/engine_backlog.md items 80 and 85.

    The design below did not change with the backend, because it was never an OpenAL design.

    TWO THINGS WITH TWO DIFFERENT LIFETIMES, AND THE WHOLE DESIGN IS KEEPING THEM APART.

      A BUFFER is immutable PCM data, loaded once and named. "click.wav" is a buffer.
      A VOICE is a thing currently making noise: one playing of a buffer, with its own gain,
      pitch and play state. Three bricks breaking at once is one buffer and three voices.

    OpenAL modelled exactly that split - many sources may play one buffer - and this class used to
    throw it away, pairing one buffer to one source 1:1 and keying both off one name. So a name
    meant both WHAT to play and WHICH playing you meant, and a sound could not overlap itself:
    alSourcePlay on a source that is already playing REWINDS it, so the second brick cut the first
    off mid-attack. The only way to a second voice was a second source, the only way to that was a
    second name, and the only way to that was to load the same file twice - which is what
    ApplicationTetris and APP=Breakout both did, and what made the LoadFile heap corruption in
    backlog item 53 reachable at all.

    So:

      A NAME identifies a BUFFER - what to play. AppendFile registers one, and registering two
      names for one file costs one buffer, not two: they are deduplicated by filename.
      A HANDLE identifies a VOICE - which playing you mean. Play returns one.

    Fire and forget is the common case and costs nothing: ignore the handle, and the voice is
    recycled once it finishes. Keep the handle and you can pause, rewind, stop or ask after that
    one playing - and if you need it to survive a busy moment, start it with SOUND_KEEP.

    HOW THAT SPLIT IS SPELLED IN MINIAUDIO, because it is less obvious than OpenAL's was.
    A miniaudio data source carries its own read cursor, so two voices cannot share one: they
    would share a playback position. What they CAN share is the bytes. So a SoundBuffer owns the
    decoded PCM and nothing else, and each voice builds its own ma_audio_buffer_ref over those
    same bytes when it starts - a small POD with a cursor, no copy of the audio. That is the
    many-sources-one-buffer relationship, rebuilt out of the parts miniaudio gives.
*/

/*
    The limits. BUFFERS is only a guard against a runaway loop registering files - the buffers
    are a vector, and what a loaded sound really costs is its decoded PCM (a second of 48 kHz
    stereo is 188 KB), which AppendFile logs. 32 was hit on paper by the archer's cue plan before
    a single footstep was recorded: four variations on three surfaces is twelve files on its own.

    VOICES is the scarce one, but less scarce than it was. A voice costs nothing while idle and a
    resampler while playing, and 16 had to hold music, an ambience loop, narration kept against
    stealing, and every one-shot at once. A game that runs out of 32 wants a per-sound cap
    (the cue layer's max_instances), not more voices.
*/
#define  NUM_SOUND_BUFFERS 256  //distinct sound FILES that can be resident
#define  NUM_SOUND_VOICES  32   //sounds that can be audible AT ONCE. The scarce one.
#define  NUM_SOUND_BUSES   8    //mix groups, the master included - see AddBus

/*
    How long a gain change takes to arrive, on a voice or a bus.

    Without it a volume change lands on the next mixer block as a step, and a step in a waveform
    is a click. A game changes gains once a TICK - a creak following the swing, a duck easing in -
    so at 60 Hz that is a staircase of steps sixty times a second, heard as a buzz. 10 ms is under
    a 60 Hz tick, so a ramp set each tick is a smooth line and never falls behind. It does not
    delay a sound's START: miniaudio's first gain on a new voice is taken as-is, not ramped to.
*/
#define  SOUND_GAIN_SMOOTH_SECONDS 0.010f

//The bus every sound plays on unless told otherwise, and which every other bus feeds.
#define  SOUND_BUS_MASTER  0

/*
    Names a single playing of a sound. Zero is never handed out, so a zero-initialised member is
    "I am not holding a voice" without anybody having to say so.

    IT IS A COUNT, NOT A SLOT INDEX, and that is what makes it safe to hold on to. Handle 987 is
    the 987th sound this process started and refers to that playing and no other; the voice it ran
    on has long since been someone else's. Every call below looks for the voice whose owner is
    still exactly this handle, so a handle for a sound that has finished, been stopped or been
    recycled is INERT rather than dangerous. Without that, a stale handle would quietly control
    whatever sound now occupies that slot - pausing somebody else's explosion - which is the same
    class of bug as the map_handles operator[] one in FindBufferByName, and just as baffling.
*/
typedef uint32_t soundhandle_t;
#define SOUND_INVALID_HANDLE 0

/*
    Flags for Play.

    SOUND_ONESHOT is the default and means "this voice is disposable": when every source is busy
    and something new has to play, the oldest one-shot is taken. That is the right answer for a
    brick or a footstep, where dropping the oldest is the least bad outcome and nobody notices.

    SOUND_KEEP means "do not take this one". For music, an engine loop, anything long: a voice
    stolen halfway through would restart from the beginning at an arbitrary moment, which is far
    worse than a missed click. A kept voice holds its slot until Stop, so an app that starts
    them and never stops them will run out - but note that is still strictly better than the old
    behaviour, where EVERY registered sound held a source for the life of the process.
*/
#define SOUND_ONESHOT   0
#define SOUND_KEEP      1

/*
    Everything a single playing can be started with. The long Play overload below takes the same
    things one argument at a time and is kept for the calls that already use it.

    `pan` is a stereo balance, not a position in a world: there is still no listener and no
    spatializer, and a side-view game places a sound by turning its screen x into -1..1. A mono
    file panned hard left is silent on the right; a stereo file keeps its own image and is
    weighted toward one side.
*/
struct SoundParams{
    float    gain = 1.0f;
    float    pitch = 1.0f;          //playback rate: 2 is an octave up and half as long
    float    pan = 0.0f;            //-1 left .. 0 centre .. +1 right
    int      bus = SOUND_BUS_MASTER;
    bool     f_looping = false;
    uint32_t flags = 0;             //SOUND_ONESHOT or SOUND_KEEP, below
    float    start_seconds = 0.0f;  //see the long Play
};

//One voice as ListVoices reports it: what is making noise right now, for telemetry and logs.
struct SoundVoiceInfo{
    soundhandle_t handle = 0;
    std::string   name;             //the name it was played under; "(stream)" for PlayStream
    int      bus = SOUND_BUS_MASTER;
    float    gain = 1.0f;
    float    pitch = 1.0f;
    float    pan = 0.0f;
    float    position = 0.0f;       //seconds into the sound
    float    length = 0.0f;         //seconds, 0 for a stream
    bool     f_looping = false;
    bool     f_keep = false;
    bool     f_held = false;        //held by SetPaused
};

class SoundSystem{
public:
    SoundSystem(){};
    ~SoundSystem();

    bool f_initialised = false;
    void Initialise();
    /*
        The same engine with NO DEVICE: nothing is heard, and the mix is pulled out by Render
        instead, as fast as the caller asks. For tests that measure what was mixed - that a pan
        emptied a channel, that a bus at zero is silent - rather than listening for it, and for
        rendering a run's audio to a file. Every other call behaves exactly as with a device.
    */
    bool InitialiseOffline(uint32_t sample_rate = 48000, uint32_t channels = 2);
    //Mixes the next `frames` frames into `out` (interleaved float, the engine's channel count).
    //Offline only - with a device the mixer thread owns the mix. Returns the frames written.
    uint64_t Render(float* out, uint64_t frames);

    /*
        Loads a wav and gives it a name to play it by. Registering several names for one file is
        free - the file is loaded once and the names share the buffer - so naming sounds after
        what they MEAN ("brick_hit", "paddle_hit") rather than after the file is the intended use.

        No longer fatal when full: a sound that will not load is not a reason to end the process.
    */
    void AppendFile(const char* filename, const char* handle);

    /*
        Starts a sound and returns the handle of the voice playing it. Ignore the handle for
        anything fire-and-forget.

        The same name may be played any number of times at once, up to NUM_SOUND_VOICES - they
        overlap instead of cutting each other off. Returns SOUND_INVALID_HANDLE if the name is
        not registered, or if every voice is busy with a kept one.

        `start_seconds` starts it that far in rather than at its first sample - for a sound that
        leads up to a moment (a whoosh that peaks on an impact) when the moment is nearer than
        the sound's lead: playing its tail keeps the peak on the moment. Clamped to the sound.
    */
    soundhandle_t Play(const char* handle_name, bool looping = false, float gain = 1.0f, uint32_t flags = SOUND_ONESHOT,
                       float start_seconds = 0.0f);
    //The same, with everything a playing can start with - pitch, pan and bus included.
    soundhandle_t Play(const char* handle_name, const SoundParams& params);

    /*
        Plays audio that is MADE rather than loaded: any miniaudio data source the caller owns -
        music being generated, a synthesiser - on a kept voice, until Stop.

        The caller owns `source` and must keep it alive until Stop has returned; the voice reads
        it on miniaudio's mixer thread, so the source's read callback has to be safe to call from
        there. Its format is whatever it reports - use GetSampleRate to produce at the device's
        own rate and skip a resample. Always SOUND_KEEP: a generated stream stolen for a
        footstep would fall silent and never come back.
    */
    soundhandle_t PlayStream(ma_data_source* source, float gain = 1.0f, int bus = SOUND_BUS_MASTER);

    //The device's sample rate, 0 before a successful Initialise.
    uint32_t GetSampleRate();

    /*
        Where a registered sound is loudest: the start, in seconds, of its loudest `window` (RMS
        over every channel). For lining a sound's moment up with a game's - measured off the file
        rather than typed, so a re-cut sound re-measures itself. -1 for a name not registered.
    */
    float LoudestAt(const char* handle_name, float window = 0.01f);
    //How long a registered sound is, in seconds at its own rate (so before any pitch). -1 for a
    //name not registered. For a subtitle that stays up as long as its line, or a gap measured
    //from a sound's end rather than its start.
    float LengthOf(const char* handle_name);

    /*
        HOLDS every sound where it is (true), or lets them all carry on (false) - for keeping sound
        in SIMULATED time while an app's simulation is paused: hold on a pass that does not tick,
        let go on one that does, and a single step plays a single tick of every sound, the way a
        video editor scrubs a frame. Without it a sound started on a stepped tick plays out in
        wall-clock time while the picture waits for the next step - a swoosh timed to peak on an
        impact finishes long before the arrow arrives.

        A held voice still COUNTS AS PLAYING (FinishedPlaying, GetNumPlaying, and the reclaiming
        of one-shots), since it has not finished: it has stopped, and it will go on. That is the
        difference from Pause, which is its owner's decision. Cheap enough to call every pass.
    */
    void SetPaused(bool paused);

    //All of these do nothing, harmlessly, for a handle whose voice is gone - see soundhandle_t.
    void Stop(soundhandle_t handle);        //ends it and returns the voice to the pool
    void Pause(soundhandle_t handle);
    void Resume(soundhandle_t handle);      //carries on from where Pause left it
    void Rewind(soundhandle_t handle);

    /*
        CHANGED WHILE IT PLAYS. For a sound that follows something - a rope's creak by its swing
        speed, a scrape by how fast the crate is going, a sound panned after the thing it belongs
        to as the camera moves. Gain is smoothed (SOUND_GAIN_SMOOTH_SECONDS), so setting it every
        tick is the intended use. Pitch and pan are not: both are continuous already, and a small
        change per tick is not a step anyone hears.
    */
    void SetGain(soundhandle_t handle, float gain);
    void SetPitch(soundhandle_t handle, float pitch);  //clamped above zero
    void SetPan(soundhandle_t handle, float pan);      //clamped to -1..1

    /*
        BUSES: groups of voices mixed together, with one gain for the lot. Effects, ambience,
        voice and music on buses of their own is what lets one thing be turned down under another
        - narration ducking the effects - and gives a settings screen its sliders.

        A bus feeds its parent, and every chain ends at SOUND_BUS_MASTER, which exists from
        Initialise on and is the one every sound uses by default. Its gain is the game's overall
        volume. Adding a name that already exists returns that bus rather than a second one, so an
        app need not remember whether it has. -1 when there is no device or no room (NUM_SOUND_BUSES);
        a sound asked to play on a bus that does not exist plays on the master, and says so.

        Bus gains are smoothed exactly as voice gains are, so a duck is a gain set each tick.
    */
    int   AddBus(const char* name, int parent = SOUND_BUS_MASTER);
    int   FindBus(const char* name);         //-1 if there is none by that name
    void  SetBusGain(int bus, float gain);
    float GetBusGain(int bus);               //what it was set to, not where the smoothing is

    /*
        Every voice making noise, and what it is. `sounds_playing` said that something had
        started; this says what, on which bus, how loud and how far in - which is what a cue log
        and a replay comparison need. Voices held by SetPaused are listed, finished ones are not.
    */
    void ListVoices(std::vector<SoundVoiceInfo>& out);

    //True when this voice is not currently producing sound - finished, stopped, recycled, never
    //existed, or paused. A handle nobody recognises answers true rather than false, because a
    //caller waiting for a sound to end must not be left waiting on one that is already gone.
    bool FinishedPlaying(soundhandle_t handle);

    //How many voices are audible right now. For telemetry and for finding out whether an app is
    //starving itself of voices.
    int GetNumPlaying();

private:
    //miniaudio's engine: the device, the mixing graph and the mixer thread.
    ma_engine engine;

    /*
        One playing of a buffer.

        `sound` and `ref` are only meaningful while f_active, and they are torn down and rebuilt
        on every Play rather than kept around. That is deliberate: a ma_sound is bound to the data
        source it was initialised from, so reusing one for a different buffer is not a thing that
        can be expressed. Rebuilding is a small allocation inside miniaudio, at the rate a game
        starts sound effects, and it keeps the lifetime rule to one line - if f_active, uninit
        before doing anything else.

        `owner` is the handle that started it, or SOUND_INVALID_HANDLE when this voice is idle.
    */
    struct SoundVoice{
        ma_sound sound;
        ma_audio_buffer_ref ref;
        bool f_active = false;                  //sound and ref are initialised
        soundhandle_t owner = SOUND_INVALID_HANDLE;
        bool f_keep = false;
        bool f_held = false;                    //stopped by SetPaused, and to be restarted by it
        bool f_stream = false;                  //reads a caller's data source; `ref` is unused
        uint64_t started = 0;                   //play counter at start, so "oldest" is answerable
        //What it is playing and how, kept for ListVoices - miniaudio can be asked most of these,
        //but not what name the caller used.
        int buffer = -1;                        //index into `buffers`, -1 for a stream
        std::string name;
        int bus = SOUND_BUS_MASTER;
        float gain = 1.0f;
        float pitch = 1.0f;
        float pan = 0.0f;
        bool f_looping = false;
    };

    /*
        One mix group. A FIXED ARRAY, not a vector: every voice on a bus holds a pointer to its
        ma_sound_group inside miniaudio's node graph, so a bus must never move once it exists.
        Created in order and a parent always before its children, so tearing down in reverse
        index order never leaves a node feeding one that is gone.
    */
    struct SoundBus{
        ma_sound_group group;
        bool f_active = false;
        std::string name;
        int parent = -1;
        float gain = 1.0f;
    };
    SoundBus buses[NUM_SOUND_BUSES];
    //The node a voice on `bus` attaches to: that bus, or the master for one that does not exist.
    ma_node* BusNode(int& bus);
    //Initialise's second half, with or without a device: the master bus and the limits line.
    bool FinishInitialise();
    //The frames a gain change is smoothed over, at the engine's rate.
    ma_uint32 SmoothFrames();
    bool f_offline = false;

    /*
        One loaded file: the decoded PCM and what it takes to interpret it.

        The BYTES ARE OWNED HERE and outlive every voice playing them, which they have to -
        ma_audio_buffer_ref does not copy what it is given, it points at it. WaveFile frees its
        own buffer when it goes out of scope, so AppendFile copies out rather than borrowing.
    */
    struct SoundBuffer{
        std::vector<uint8_t> pcm;
        ma_uint32 channels = 0;
        ma_uint32 sample_rate = 0;
        ma_uint64 frame_count = 0;
        std::string filename;
    };

    SoundVoice voices[NUM_SOUND_VOICES];
    std::vector<SoundBuffer> buffers;

    std::map<std::string, int>map_handles;      //name -> index into `buffers`

    soundhandle_t next_handle = 1;              //0 is reserved for "no voice"
    uint64_t play_counter = 0;

    //Looks a name up, or returns -1 after logging which name failed.
    //
    //The play calls used to index map_handles with operator[], which DEFAULT-CONSTRUCTS a
    //missing key: a typo'd or never-registered name silently became handle 0 and played whatever
    //sound was registered first, forever, with no way to tell from the outside. It also grew the
    //map on every such call. A wrong sound is a baffling thing to debug; a log line is not.
    int FindBufferByName(const char* handle_name);

    //The voice this handle still owns, or NULL if it owns none any more.
    SoundVoice* FindVoice(soundhandle_t handle);

    //A voice to play on: a free one, else the oldest one-shot. NULL when every voice is kept.
    SoundVoice* AcquireVoice();

    //Tears down whatever this voice was playing and marks it idle. Safe on an idle voice.
    void ReleaseVoice(SoundVoice* voice);

    //Is this voice actually making noise? Idle voices answer false without being asked.
    bool VoiceIsPlaying(const SoundVoice* voice);
};

#endif
