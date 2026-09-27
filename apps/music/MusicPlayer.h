#ifndef _MUSIC_PLAYER_H_
#define _MUSIC_PLAYER_H_

#include "MusicEngine.h"
#include "SoundSystem.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

/*
    A MusicEngine made audible: the engine behind a miniaudio data source, on one kept
    SoundSystem voice (SoundSystem::PlayStream), plus the thread handling that makes it safe to
    steer from anywhere.

    THREE KINDS OF THREAD TOUCH THIS, and the rule is that only one of them touches the engine.

      the AUDIO thread     miniaudio's mixer, calling OnRead. It owns `engine` outright.
      the RENDER thread    the ImGui panel, and Start/Stop - which call SoundSystem, and
                           SoundSystem has no lock of its own, so exactly one thread may.
      MCP threads          the music_* tools, which only ever call the thread-safe half below.

    Everything that crosses to the audio thread goes through `mutex`: parameters are copied in and
    events queued, and the audio thread takes them with try_lock at the top of each block. It
    never WAITS for the lock - a block that finds it held just runs on last block's parameters and
    picks the change up ten milliseconds later, which nobody can hear, where a blocked audio
    thread is a click everybody can. Status comes back out the same way.
*/
class MusicPlayer{
public:
    MusicPlayer();
    ~MusicPlayer();

    //RENDER THREAD. Starts playing `score` from the top, taking it over. Stops whatever was playing.
    bool Start(SoundSystem* sound, std::shared_ptr<MusicScore> score, uint32_t seed = 1);
    void Stop();
    bool IsPlaying() const { return handle != SOUND_INVALID_HANDLE; }

    //--- any thread ---------------------------------------------------------------------------
    void SetParams(const MusicParams& p);
    MusicParams GetParams();
    void Post(const MusicEvent& e);
    MusicStatus GetStatus();
    //Plays one sample as it is, beside the music, replacing any audition already playing. Null stops it.
    void Audition(std::shared_ptr<const MusicSample> sample);
    //The score playing now, shared: a caller on another thread keeps it alive for as long as it
    //holds the pointer, so a reload in the middle of an offline render cannot free it underneath.
    std::shared_ptr<const MusicScore> GetScore();

    /*
        Renders `seconds` of the current score to a stereo PCM16 wav, WITHOUT touching what is
        playing: a second engine, the same score, the given parameters and seed, at 48 kHz. Events
        in `timeline` are posted when their time comes - that is how a key change is tested.
        Returns false with `error` set if the file cannot be written.
    */
    struct TimedEvent{
        double at_s;
        MusicEvent event;
    };
    bool RenderToFile(const std::string& path, double seconds, const MusicParams& p, uint32_t seed,
                      const std::vector<TimedEvent>& timeline, MusicStatus& final_status, std::string& error);

private:
    //The data source miniaudio reads. `base` must be first: miniaudio casts the pointer.
    struct Source{
        ma_data_source_base base;
        MusicPlayer* owner;
    };
    Source source;
    static ma_result OnRead(ma_data_source* ds, void* out, ma_uint64 frames, ma_uint64* read);
    static ma_result OnSeek(ma_data_source* ds, ma_uint64 frame);
    static ma_result OnGetDataFormat(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                                     ma_uint32* rate, ma_channel* map, size_t map_cap);
    static ma_data_source_vtable vtable;

    SoundSystem* sound = nullptr;
    soundhandle_t handle = SOUND_INVALID_HANDLE;
    std::shared_ptr<MusicScore> score;     //swapped under `mutex`; the audio thread reads it only between Start and Stop
    int rate = 48000;

    MusicEngine engine;             //AUDIO THREAD only, once started

    std::mutex mutex;               //guards everything below
    MusicParams params;
    bool f_params_dirty = false;
    std::vector<MusicEvent> queue;
    /*
        The last few auditioned samples, held HERE as well as by the engine. When the engine lets
        go of one - the next audition replaces it - this is what stops that being the last
        reference, which would free a few megabytes of PCM on the audio thread in the middle of a
        block. It is freed here instead, on whichever thread auditions the one after.
    */
    std::shared_ptr<const MusicSample> audition_keep[3];
    MusicStatus status;
};

#endif
