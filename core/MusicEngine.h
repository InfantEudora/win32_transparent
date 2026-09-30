#ifndef _MUSIC_ENGINE_H_
#define _MUSIC_ENGINE_H_

#include "MusicScore.h"

#include <cstdint>
#include <memory>
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

    BRIGHTNESS is the second axis, beside suspense: 0 is rumble and bass, 1 is high and bright,
    0.5 is the score exactly as written. Every part has a HEIGHT, 0..1 from C2 to C6: its note for
    a pitched bed, the middle of its register for a voice, and for an unpitched bed how bright the
    recording is (its zero-crossing rate); a score can set `height` itself. Brightness then does
    three things, all centred on 0.5 so the middle of the slider changes nothing: parts are
    weighted by height (up to 9 dB, low parts up when dark and high parts up when bright), each
    voice's notes are confined to the lower or upper half of its register, and the whole mix gets
    a tilt around 500 Hz that cuts the far side by up to 6 dB. Brightness re-weights the parts
    against each other and leaves the overall level where it was.

    KEY CHANGES ARE QUANTISED TO THE BAR, as the design called for: a key event sets a pending key
    and it takes effect on the next downbeat, so a game can ask at any moment and the music moves
    at a musical one. Bars are four beats. A key event with f_now skips the wait.

    SECTION CHANGES ARE TOO, and for the same reason. On the downbeat the new section's voices
    start rolling and the old one's stop (their last notes ring out); the beds cross over more
    slowly, over the score's section_fade_s, equal-power so the level does not dip in the middle.
    A bed coming in starts its recording from the top; one going out stops once it is silent, so a
    section that is not playing costs nothing. A section moves on by itself after its `bars`, or
    when a SECTION event asks - by name, or the next one if the name is empty.

    A PAUSE fades the music out over pause_fade_s and then HOLDS it: once it is silent the engine
    stops rendering altogether, so its clock, the beat, every bed's place in its loop and every
    note's place in its ring stop exactly where the fade ended. A resume carries on from there,
    fading back in over resume_fade_s - after the fade, the music is sample for sample what it
    would have been had the pause never happened, only later. It is the game's pause, not a mute:
    muting would let the music run on, and come back somewhere else in the phrase. Events posted
    while held are taken in but heard only after the resume - a stinger sounds as it comes back,
    a key change still waits for a bar line - except the audition, which is the library's and not
    the music's, and plays through.

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
    float brightness = 0.5f;        //0 dark - rumble, bass .. 1 bright - high, shimmering. 0.5 = as scored
    float pause_fade_s = 1.5f;      //a PAUSE fades the music out over this long, then holds it
    float resume_fade_s = 0.5f;     //and a resume brings it back over this - quicker, since the player is waiting
};

struct MusicEvent{
    enum Type{ KEY, STINGER, SUSPENSE, BRIGHTNESS, AUDITION, SECTION, PAUSE };
    Type type = KEY;
    float value = 0;                //SUSPENSE, BRIGHTNESS: the new value - for scripting a render's arc.
                                    //PAUSE: 1 pauses, 0 resumes
    int root_pc = -1;               //KEY: new root, or -1 to keep it
    int mode = -1;                  //KEY: new mode, or -1 to keep it
    bool f_now = false;             //KEY, SECTION: skip the wait for the downbeat. PAUSE: skip the fade
    std::string name;               //STINGER: which. SECTION: which, or empty for the next one
    std::shared_ptr<const MusicSample> sample;  //AUDITION: what to play, or null to stop
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
    std::vector<float> bed_heights;                 //0 low .. 1 high, what brightness weighs them by
    std::vector<int> bed_sections;                  //the section each bed belongs to, -1 = every one
    std::vector<std::string> voice_names;
    std::vector<float> voice_heights;
    std::vector<int> voice_sections;
    std::vector<std::string> section_names;         //empty if the score has no sections
    int section = 0;                                //the one playing
    int pending_section = -1;                       //waiting for the bar line: -1 none, -2 the next one
                                                    //(not known yet when the order is random)
    int section_bar = 0;                            //whole bars it has played so far
    int section_bars = 0;                           //how many it plays before moving on, 0 = until asked
    std::string auditioning;                        //the sample being auditioned, or empty
    bool f_paused = false;                          //asked to pause; still fading out until f_held
    bool f_held = false;                            //silent, and the clock stopped where the fade ended
    float pause_gain = 1;                           //what the pause fade is multiplying the music by
    int notes_sounding = 0;
    int notes_total = 0;
    std::vector<MusicNoteLog> recent;               //newest last, at most 24
    float peak_db = -120, rms_db = -120;            //of the output, over the last second
    int clipped = 0;                                //samples the limiter had to catch, ever
};

class MusicEngine{
public:
    //The score must outlive the engine. Resets everything: clock, key, every playing sound.
    //`start_section` is the section it opens in, by index; out of range is the first.
    void Init(const MusicScore* score, int out_rate, uint32_t seed, int start_section = 0);

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

    //--- sections -------------------------------------------------------------------------
    int section = 0;
    int pending_section = -1;       //-2 = the next one, whichever that is; -1 = none
    int section_bar = 0;
    bool InSection(int part_section) const { return part_section < 0 || part_section == section; }
    int NextSection();
    void ApplySection(int s);

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
        float presence = 1;         //0..1 through a section crossfade, linear; heard as a sine
        int transpose = 0;
        float height = 0.5f;
    };
    std::vector<Bed> beds;
    int BedTranspose(const Bed& b) const;
    void StartBedPlay(Bed& b, double pos, float fade_s);
    void RenderBeds(float* out, int frames, int64_t block_start);

    //--- voices ---------------------------------------------------------------------------
    struct Voice{
        const MusicVoiceDef* def = nullptr;
        int last_midi = -1;
        float height = 0.5f;
    };
    std::vector<Voice> voices;
    std::vector<Play> notes;        //fixed pool; a note steals the oldest when it is full
    void OnBeat(int64_t beat_frame);
    int ChooseNote(Voice& v, bool& f_tension);
    void StartNote(const MusicSample* s, int root_midi, int midi, float gain, float pan, int64_t start,
                   int64_t release_after, float release_s);

    //--- pause ----------------------------------------------------------------------------
    bool f_paused = false;
    float pause_pos = 1;            //0 silent .. 1 full, linear in time; heard through PauseCurve
    static float PauseCurve(float pos);
    //Everything of one block that a hold stops: beats, beds, notes, the tilt, the pause fade.
    void RenderMusic(float* out, int frames);

    //--- output meter ---------------------------------------------------------------------
    double meter_sq = 0;
    float meter_peak = 0;
    int meter_frames = 0;

    //--- brightness -----------------------------------------------------------------------
    static float HeightFromMidi(float midi);
    float HeightGain(float height) const;
    float tilt_lp[2] = {0, 0};      //the master tilt's one-pole low band, per channel

    //--- audition -------------------------------------------------------------------------
    //One library file played as it is, beside the music - its own slot, so no note steals it.
    Play audition;
    std::shared_ptr<const MusicSample> audition_sample;
};

#endif
