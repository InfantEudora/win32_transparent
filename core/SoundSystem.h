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
#include <vector>

/*
    Maybe... use stb_ogg to load stuff.

    For now, wraps around some version of OpenAL.

    TWO THINGS WITH TWO DIFFERENT LIFETIMES, AND THE WHOLE DESIGN IS KEEPING THEM APART.

      A BUFFER is immutable PCM data, loaded once and named. "click.wav" is a buffer.
      A VOICE is a thing currently making noise: one playing of a buffer, with its own gain,
      pitch and play state. Three bricks breaking at once is one buffer and three voices.

    OpenAL models exactly that split - many sources may play one buffer - and this class used to
    throw it away, pairing one buffer to one source 1:1 and keying both off one name. So a name
    meant both WHAT to play and WHICH playing you meant, and a sound could not overlap itself:
    alSourcePlay on a source that is already playing REWINDS it, so the second brick cut the first
    off mid-attack. The only way to a second voice was a second source, the only way to that was a
    second name, and the only way to that was to load the same file twice - which is what
    ApplicationTetris and APP=Breakout both did, and what made the LoadFile heap corruption in
    backlog item 53 reachable at all.

    So now:

      A NAME identifies a BUFFER - what to play. AppendFile registers one, and registering two
      names for one file costs one buffer, not two: they are deduplicated by filename.
      A HANDLE identifies a VOICE - which playing you mean. Play returns one.

    Fire and forget is the common case and costs nothing: ignore the handle, and the voice is
    recycled once it finishes. Keep the handle and you can pause, rewind, stop or ask after that
    one playing - and if you need it to survive a busy moment, start it with SOUND_KEEP.
*/

#define  NUM_AL_BUFFERS 32      //distinct sound FILES that can be resident
#define  NUM_AL_SOURCES 16      //sounds that can be audible AT ONCE. Hardware voices; the scarce one.

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
    worse than a missed click. A kept voice holds its source until Stop, so an app that starts
    them and never stops them will run out - but note that is still strictly better than the old
    behaviour, where EVERY registered sound held a source for the life of the process.
*/
#define SOUND_ONESHOT   0
#define SOUND_KEEP      1

class SoundSystem{
public:
    SoundSystem(){};
    ~SoundSystem(){}

    HINSTANCE hdll = NULL;

    ALCdevice* default_device = NULL;
    ALCcontext* ctx = NULL;

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

        The same name may be played any number of times at once, up to NUM_AL_SOURCES - they
        overlap instead of cutting each other off. Returns SOUND_INVALID_HANDLE if the name is
        not registered, or if every source is busy with a kept voice.
    */
    soundhandle_t Play(const char* handle_name, bool looping = false, float gain = 1.0f, uint32_t flags = SOUND_ONESHOT);

    //All of these do nothing, harmlessly, for a handle whose voice is gone - see soundhandle_t.
    void Stop(soundhandle_t handle);        //ends it and returns the source to the pool
    void Pause(soundhandle_t handle);
    void Resume(soundhandle_t handle);      //carries on from where Pause left it
    void Rewind(soundhandle_t handle);

    //True when this voice is not currently producing sound - finished, stopped, recycled, never
    //existed, or paused. A handle nobody recognises answers true rather than false, because a
    //caller waiting for a sound to end must not be left waiting on one that is already gone.
    bool FinishedPlaying(soundhandle_t handle);

    //How many voices are audible right now. For telemetry and for finding out whether an app is
    //starving itself of sources.
    int GetNumPlaying();

private:
    //One playing of a buffer. `owner` is the handle that started it, or SOUND_INVALID_HANDLE when
    //this source is idle and free to take.
    struct SoundVoice{
        ALuint source = 0;
        soundhandle_t owner = SOUND_INVALID_HANDLE;
        bool f_keep = false;
        uint64_t started = 0;       //play counter at start, so "oldest" is answerable
    };

    //One loaded file. Named separately because several names may share one.
    struct SoundBuffer{
        ALuint buffer = 0;
        std::string filename;
    };

    SoundVoice voices[NUM_AL_SOURCES];
    std::vector<SoundBuffer> buffers;
    ALuint al_buffers[NUM_AL_BUFFERS];

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

    //A source to play on: a free one, else the oldest one-shot. NULL when every source is kept.
    SoundVoice* AcquireVoice();
};

#endif
