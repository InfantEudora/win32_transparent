#ifndef _MUSIC_ENGINE_H_
#define _MUSIC_ENGINE_H_

#include "MusicScore.h"

#include <cstdint>
#include <string>
#include <vector>

/*
    The music itself: a score (MusicScore.h) turned into stereo samples, one block at a time.

    PURE DSP, AND THAT IS A DESIGN DECISION. Nothing in here knows about miniaudio, threads,
    the engine or a clock: Render(out, frames) is a function of the score, the parameters, the
    events posted and the seed. So the SAME code plays live through MusicPlayer and renders
    offline to a wav, and an offline render of a given seed is exactly what it is - which is how
    a change to this file is checked: render it, and measure the render with tools/samplescan.

    WHAT IT DOES WITH EACH PART, per score:

      beds       loop with a crossfade at the seam, and cross over to a transposed copy on a key
                 change (pitched beds only). Their gain follows suspense, smoothed.
      voices     on every beat, each voice rolls against its density for a note; the note is a
                 random walk over the current scale inside its register, sometimes half a beat
                 late. With suspense, a share of notes (up to the score's tension_max) become
                 tension notes - the flat 2nd or the tritone above the root - which is the
                 cheapest reliable way to make a scale sound uneasy.
      stingers   play when posted.

    KEY CHANGES ARE QUANTISED TO THE BAR, as the design called for: a key event sets a pending key
    and it takes effect on the next downbeat, so a game can ask at any moment and the music moves
    at a musical one. Bars are four beats. A key event with f_now skips the wait.

    Time here is output FRAMES, never seconds or ticks - it is presentation, not simulation, and
    it runs on the audio device's clock. It draws from its own random stream, never the
    engine's shared Application::rrand: a draw from another thread would shift the simulation's
    stream out from under it (see core/RRandom.h).
*/

struct MusicParams{
    float bpm = 60.0f;
    float suspense = 0.2f;          //0 calm .. 1 tense
    float master = 0.8f;
    float bed_gain = 1.0f;          //scales every bed
    float voice_gain = 1.0f;        //scales every voice
};

struct MusicEvent{
    enum Type{ KEY, STINGER, SUSPENSE };
    Type type = KEY;
    float value = 0;                //SUSPENSE: the new suspense - for scripting a render's arc
    int root_pc = -1;               //KEY: new root, or -1 to keep it
    int mode = -1;                  //KEY: new mode, or -1 to keep it
    bool f_now = false;             //KEY: skip the wait for the downbeat
    std::string name;               //STINGER: which
};

//One note that sounded, for the log a person or an agent reads to see what the music is doing.
struct MusicNoteLog{
    double time_s;
    std::string voice;
    int midi;
    bool f_tension;
};

struct MusicStatus{
    double time_s = 0;
    int bar = 0, beat = 0;
    int root_pc = 9, mode = 0;
    int pending_root_pc = -1, pending_mode = -1;    //-1 = no key change waiting
    std::vector<float> bed_gains;                   //each bed's current gain, after smoothing
    std::vector<std::string> bed_names;
    std::vector<int> bed_transpose;                 //semitones each bed is shifted by
    int notes_sounding = 0;
    int notes_total = 0;
    std::vector<MusicNoteLog> recent;               //newest last, at most 24
    float peak_db = -120, rms_db = -120;            //of the output, over the last second
    int clipped = 0;                                //samples the limiter had to catch, ever
};

class MusicEngine{
public:
    //The score must outlive the engine. Resets everything: clock, key, every playing sound.
    void Init(const MusicScore* score, int out_rate, uint32_t seed);

    void SetParams(const MusicParams& p){ params = p; }
    const MusicParams& GetParams() const { return params; }
    void Post(const MusicEvent& e){ events.push_back(e); }

    //Stereo interleaved, `frames` frames. Never fails. Allocates nothing per sample; the note
    //log's strings are the one allocation, a few per second at most.
    void Render(float* out, int frames);

    const MusicStatus& Status() const { return status; }

private:
    const MusicScore* score = nullptr;
    int rate = 48000;
    MusicParams params;
    std::vector<MusicEvent> events;
    MusicStatus status;

    uint32_t rng = 1;
    float Random();                 //0..1
    int RandomInt(int n);           //0..n-1

    //--- clock ----------------------------------------------------------------------------
    uint64_t clock = 0;             //output frames rendered
    double next_beat = 0;           //the frame the next beat falls on
    int beat_count = 0;             //beats since Init; a bar is four

    //--- key ------------------------------------------------------------------------------
    int root_pc = 9, mode = 0;
    int pending_root = -1, pending_mode = -1;
    void ApplyKey(int new_root, int new_mode);

    //--- one playing of one sample --------------------------------------------------------
    struct Play{
        bool f_on = false;
        const MusicSample* sample = nullptr;
        double pos = 0;             //in the sample's own frames
        double step = 1;            //sample frames per output frame: rate ratio times pitch
        float fade = 0, fade_target = 1, fade_step = 0;     //linear, per output frame
        float gain = 1, pan_l = 1, pan_r = 1;
        bool f_loop_spawned = false;    //beds: has this one handed over to its successor yet
        int64_t release_at = -1;        //voices: output frame to start the release, -1 = never
        float release_frames = 0;
        int64_t start_at = 0;           //voices: output frame the note starts on
    };
    //Adds one playing's next `frames` frames into out. Returns false once it has finished.
    bool Mix(Play& p, float* out, int frames, int64_t block_start);

    //--- beds -----------------------------------------------------------------------------
    struct Bed{
        const MusicBedDef* def = nullptr;
        Play plays[3];              //the current one, plus whatever is crossfading out
        float gain = 0;             //smoothed towards the target the suspense asks for
        int transpose = 0;
    };
    std::vector<Bed> beds;
    int BedTranspose(const Bed& b) const;
    void StartBedPlay(Bed& b, double pos, float fade_s);
    void RenderBeds(float* out, int frames, int64_t block_start);

    //--- voices ---------------------------------------------------------------------------
    struct Voice{
        const MusicVoiceDef* def = nullptr;
        int last_midi = -1;
    };
    std::vector<Voice> voices;
    std::vector<Play> notes;        //fixed pool; a note steals the oldest when it is full
    void OnBeat(int64_t beat_frame);
    int ChooseNote(Voice& v, bool& f_tension);
    void StartNote(const MusicSample* s, int root_midi, int midi, float gain, float pan, int64_t start,
                   int64_t release_after, float release_s);

    //--- output meter ---------------------------------------------------------------------
    double meter_sq = 0;
    float meter_peak = 0;
    int meter_frames = 0;
};

#endif
