#pragma once
#include <string>
#include <vector>

/*
    Everything samplescan measures about one audio file, from the audio alone.

    None of it is a verdict. These are the numbers a person would otherwise read off a waveform
    and a tuner, and the category guess in samplescan.cpp is built on top of them - so when a
    guess is wrong, the column that misled it is right here to look at.

    Levels are dBFS. Times are milliseconds. "Active" means louder than the silence threshold
    (45 dB under the file's own peak, and never below -65 dBFS), so a clip with half a second
    of dead air at each end is measured on its sound and not on its padding.
*/
struct SampleAnalysis{
    bool f_ok = false;
    std::string error;

    int channels = 0;
    int sample_rate = 0;
    double duration_s = 0;
    bool f_truncated = false;           //longer than the analysis cap; measured on the start

    double peak_db = -200;              //sample peak over every channel
    double rms_db = -200;               //over the whole file, silence included
    double active_rms_db = -200;        //over the active part only
    int clipped = 0;                    //samples at or past full scale

    double lead_ms = 0;                 //silence before the sound starts
    double tail_ms = 0;                 //silence after it ends
    double attack_ms = 0;               //sound start to its first peak (within the first 3 s)
    double decay20_ms = -1;             //envelope peak to 20 dB below it; -1 if it never gets there
    double sustain_db = 0;              //median active level, relative to the envelope peak
    double head_tail_db = 0;            //last 200 ms against the first 200 ms: near 0 on a loop
    std::string envelope;               //short / decaying / sustained / swell

    double centroid_hz = 0;             //brightness
    double noisiness = 0;               //spectral flatness within octaves: ~0 a tone, ~0.5 noise
    double low_share = 0;               //fraction of energy below 200 Hz

    int onsets = 0;                     //note or hit starts, from the envelope
    std::vector<double> onset_s;        //when each one is
    std::vector<double> onset_rise_db;  //and how far the level rose into it
    double onset_rate = 0;              //per second of active sound
    double bpm = 0;                     //strongest beat period between 60 and 180 bpm
    double beat_strength = 0;           //0 = no pulse at all, towards 1 = a metronome

    int note_count = 0;                 //pitched segments, from the pitch track and the onsets
    std::string notes;                  //"E5 G5 A5 ..." in order, capped
    std::string pitch_classes;          //the distinct notes used, "A C D E G"

    double voiced = 0;                  //fraction of analysed frames with a clear period
    double pitch_hz = 0;                //median over voiced frames
    double pitch_midi = 0;              //the same, as a fractional MIDI note
    double stability_cents = 0;         //median distance from that median
    std::string pitch_kind;             //single / multi / none

    std::string key;                    //"A minor" - from the pitch-class profile
    double key_r = 0;                   //its correlation with the key profile
    double key_margin = 0;              //how far ahead of the runner-up it is
};

//Decodes and measures one file. Returns false with `error` set if it cannot be read.
bool AnalyseFile(const std::wstring& path, SampleAnalysis& out);

//"C4" for 60 - scientific pitch, the convention where middle C is C4.
std::string NoteName(int midi);
