#ifndef _SOUND_H_
#define _SOUND_H_

#include <windows.h>
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

#define  NUM_SOUND_BUFFERS 32   //distinct sound FILES that can be resident
#define  NUM_SOUND_VOICES  16   //sounds that can be audible AT ONCE. The scarce one.

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

class SoundSystem{
public:
    SoundSystem(){};
    ~SoundSystem();

    bool f_initialised = false;
    void Initialise();

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
    */
    soundhandle_t Play(const char* handle_name, bool looping = false, float gain = 1.0f, uint32_t flags = SOUND_ONESHOT);

    //All of these do nothing, harmlessly, for a handle whose voice is gone - see soundhandle_t.
    void Stop(soundhandle_t handle);        //ends it and returns the voice to the pool
    void Pause(soundhandle_t handle);
    void Resume(soundhandle_t handle);      //carries on from where Pause left it
    void Rewind(soundhandle_t handle);

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
        uint64_t started = 0;                   //play counter at start, so "oldest" is answerable
    };

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
