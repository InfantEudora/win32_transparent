#ifndef _MUSIC_SCORE_H_
#define _MUSIC_SCORE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

/*
    A score: WHAT the music is made of, as opposed to what it is doing right now.

    It is a JSON file (assets/music/*.json) rather than code, because the whole point of this
    system is authoring music by stating a key, a tempo, a set of instruments and how each of them
    responds to suspense - not by writing a sequence. A score names three kinds of part:

      BEDS      looped recordings under everything - wind, jungle, a drone. Each has a gain at
                suspense 0 and at suspense 1 and is crossfaded between them. A bed with a `root`
                is PITCHED: it is transposed to keep its note on a scale degree of the current
                key, so a drone follows a key change instead of clashing with it.
      VOICES    sampled instruments that play generated notes - one recorded note, repitched to
                whatever the scale asks for. Each has a register, and a density (chance of a note
                per beat) at calm and at tense.
      STINGERS  one-off hits the game fires on an event.

    The key, mode and tempo here are only where the music STARTS; after that they are the
    engine's state, changed by key events. Every number is documented beside its default in
    MusicScore.cpp, which is also where the parser says what it will not accept.
*/

//Interleaved float PCM, as loaded from one PCM16 wav.
struct MusicSample{
    std::string name;
    std::vector<float> pcm;
    int channels = 0;
    int rate = 0;
    size_t frames = 0;
};

struct MusicBedDef{
    std::string name;
    std::string sample;
    float gain_calm = 0.5f;
    float gain_tense = 0.5f;
    int root_midi = -1;             //-1 = unpitched, never transposed
    int degree = 0;                 //semitones above the key's root that the root should sound at
    float fade_s = 3.0f;            //loop crossfade, and the crossfade on a key change
};

struct MusicVoiceDef{
    std::string name;
    std::string sample;
    int root_midi = 60;             //the note the sample plays at its own speed
    int low_midi = 57;              //register the generated notes stay inside
    int high_midi = 76;
    float density_calm = 0.2f;      //chance of a note per beat, at suspense 0 and 1
    float density_tense = 0.2f;
    float gain = 0.5f;
    float length_beats = 0.0f;      //0 = let the sample ring out; else release after this long
    float release_s = 0.3f;
    float offbeat = 0.25f;          //share of notes placed half a beat late
    float spread = 0.3f;            //random pan, 0 = centre
};

struct MusicStingerDef{
    std::string name;
    std::string sample;
    float gain = 0.8f;
};

struct MusicScore{
    std::string name;
    std::string file;               //the asset it was loaded from, for a reload
    int root_pc = 9;                //A
    int mode = 0;
    float bpm = 60.0f;
    float tension_max = 0.3f;       //chance, at suspense 1, that a note is a tension note (b2, tritone)
    std::vector<MusicBedDef> beds;
    std::vector<MusicVoiceDef> voices;
    std::vector<MusicStingerDef> stingers;
    std::map<std::string, std::shared_ptr<MusicSample>> samples;    //by asset name, shared by parts

    const MusicSample* Sample(const std::string& name) const;
};

//Loads a score and every sample it names. On failure returns false with `error` saying which
//line of the score or which file is at fault; a score is never half-loaded.
bool LoadMusicScore(const char* asset_name, MusicScore& out, std::string& error);

//--- modes ---------------------------------------------------------------------------------
enum MusicMode{
    MUSIC_MODE_PENTATONIC_MINOR = 0,
    MUSIC_MODE_AEOLIAN,
    MUSIC_MODE_DORIAN,
    MUSIC_MODE_PHRYGIAN,
    MUSIC_MODE_PENTATONIC_MAJOR,
    MUSIC_MODE_IONIAN,
    MUSIC_MODE_COUNT
};
const char* MusicModeName(int mode);
int MusicModeFromName(const std::string& name);                 //-1 if unknown
const std::vector<int>& MusicModeIntervals(int mode);           //semitones above the root

//"A", "F#", "Bb" -> 0..11, and "A3" -> 57. -1 if it cannot be read.
int MusicPitchClassFromName(const std::string& name);
int MusicMidiFromName(const std::string& name);
std::string MusicPitchClassName(int pc);
std::string MusicNoteName(int midi);

#endif
