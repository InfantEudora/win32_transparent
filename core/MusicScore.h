#ifndef _MUSIC_SCORE_H_
#define _MUSIC_SCORE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

/*
    A score: WHAT the music is made of, as opposed to what it is doing right now.

    It is a JSON file (assets/music/<name>.json) rather than code, because the whole point of this
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
      MOTIFS    a fixed phrase - the one thing in a score that is WRITTEN rather than generated -
                played now and then on a bar line by one sampled instrument. Its notes are scale
                DEGREES, not pitches, so the same phrase is heard in whatever key and mode the
                music is in: bright in ionian, bittersweet in dorian, sad in aeolian. That is what
                lets one theme carry across sections whose mood differs.

    SECTIONS are sets of beds and voices that take turns, for variation over minutes rather than
    bars: the same theme, played by a different band. Beds, voices and motifs at the top level of
    the file play in every section; those listed inside a section play only while it is on. The
    engine moves to the next section on a bar line - after the section's `bars`, or when asked -
    crossfading the beds over section_fade_s. A score without sections is one section, as before.
    A section may name its own root and mode, which it takes on its first downbeat.

    The key, mode and tempo here are only where the music STARTS; after that they are the
    engine's state, changed by key events. Every number is documented beside its default in
    MusicScore.cpp, which is also where the parser says what it will not accept.
*/

/*
    Interleaved PCM16, exactly as in the wav it was loaded from. Kept as 16-bit rather than float
    because a game holds a whole score's worth - the jungle's 42 samples are 67 MB this way and
    were 134 MB as floats - and the mixer's one conversion per read (MusicEngine::Mix) gives the
    very floats a float copy held, since 2^-15 is exact.
*/
struct MusicSample{
    std::string name;
    std::vector<int16_t> pcm;
    int channels = 0;
    int rate = 0;
    size_t frames = 0;
    float zcr_hz = 0;               //how bright it is, as a frequency - see LoadMusicSampleFile
};

struct MusicBedDef{
    std::string name;
    std::string sample;
    float gain_calm = 0.5f;
    float gain_tense = 0.5f;
    int root_midi = -1;             //-1 = unpitched, never transposed
    int degree = 0;                 //semitones above the key's root that the root should sound at
    float fade_s = 3.0f;            //loop crossfade, and the crossfade on a key change
    float height = -1.0f;           //0 low .. 1 high, for the brightness slider; -1 = work it out
    int section = -1;               //index into MusicScore::sections; -1 = plays in every section
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
    float height = -1.0f;           //0 low .. 1 high, for the brightness slider; -1 = from the register
    int section = -1;               //as MusicBedDef::section
    /*
        Plays the sample as recorded - no key, no scale, no register - but rolled against its
        density like any voice: birdsong, a woodpecker, a far-off voice, dropped in now and then.
        Better than a bed for a recording that fades out at its end, whose loop would have a hole
        in it, and better than a stinger for something nobody has to fire.
    */
    bool f_unpitched = false;
};

struct MusicSectionDef{
    std::string name;
    int bars = 0;                   //how long it plays before the next one takes over; 0 = until asked
    /*
        False for a section the game asks for by name - a title screen, a cave - rather than one
        that takes its turn: the rotation never moves INTO it, and it holds until asked to move
        (section_bars does not apply; its own `bars` still can, and then it hands back to the
        rotation). "rotation": false in the score.
    */
    bool f_rotation = true;
    /*
        The key the section plays in, taken on its first downbeat: a winter section in aeolian, a
        summer one in ionian, so moving between them is the one event and the mood comes with it.
        -1 = keep whatever is playing. A key change asked for later still applies, until the next
        section that names one.
    */
    int root_pc = -1;
    int mode = -1;
};

//One note of a motif: where it starts and how long it lasts, in the motif's steps.
struct MusicMotifNote{
    int step = 0;
    int length = 1;
    int degree = 0;                 //0 = the root, 4 = the fifth, 7 = the root an octave up, -3 = the fifth below
};

struct MusicMotifDef{
    std::string name;
    std::string sample;
    int root_midi = 60;             //the note the sample plays at its own speed
    int home_midi = 60;             //degree 0 sounds on the key's root nearest this
    /*
        The phrase, as text: one token per step, separated by spaces. 1-7 is a scale degree, with
        ' after it for each octave up and , for each octave down (ABC notation's marks); - holds
        the note before for another step; . is a step of rest; | is ignored, for marking bars.
        "1 - 2 3 | 5 - 6 5" is two bars at one beat a step.

        Degrees are of a SEVEN-note scale, so a phrase means the same thing in every mode. In a
        pentatonic mode, a degree the scale lacks moves to its nearest neighbour: 4 and 7 of the
        major pentatonic land on 3 and the octave, 2 and 6 of the minor on its 3 and 5.
    */
    std::vector<MusicMotifNote> notes;
    int steps = 0;                  //the phrase's whole length, rests included
    float step_beats = 1.0f;        //how long one step is
    int every_bars = 8;             //it may start on every Nth bar - of its section, for a section's
                                    //motif, so it comes in on the section's first downbeat
    float chance_calm = 0.5f;       //chance it plays when its bar comes, at suspense 0 and 1
    float chance_tense = 0.5f;
    float gain = 0.5f;
    float release_s = 0.5f;
    bool f_ring = false;            //let each note ring out as the sample does, rather than release
                                    //it at its written length - for plucked and struck instruments
    float spread = 0.2f;            //random pan per note, 0 = centre
    float height = -1.0f;           //as MusicVoiceDef::height; -1 = from home
    int section = -1;               //as MusicBedDef::section
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
    std::vector<MusicMotifDef> motifs;
    std::vector<MusicSectionDef> sections;                          //empty = one section, everything in it
    float section_fade_s = 6.0f;    //the beds' crossfade from one section to the next
    bool f_section_random = false;  //next section: a random other one, rather than the next in order
    std::map<std::string, std::shared_ptr<MusicSample>> samples;    //by asset name, shared by parts

    const MusicSample* Sample(const std::string& name) const;
    int SectionIndex(const std::string& name) const;                //-1 if there is none by that name
};

//One PCM16 wav from a file path (not an asset name), under `name`. For auditioning a library file
//that no score names; LoadMusicScore uses the same reader.
bool LoadMusicSampleFile(const std::string& path, const std::string& name, MusicSample& out, std::string& error);

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
    MUSIC_MODE_LYDIAN,              //major with a raised 4th - wonder, open space
    MUSIC_MODE_MIXOLYDIAN,          //major with a flat 7th - between ionian and dorian
    MUSIC_MODE_PHRYGIAN_DOMINANT,   //phrygian with a major 3rd: maqam hijaz - heat, the desert
    MUSIC_MODE_COUNT
};
const char* MusicModeName(int mode);
int MusicModeFromName(const std::string& name);                 //-1 if unknown
std::string MusicModeList();                                    //"pentatonic_minor, aeolian, ...", for messages
const std::vector<int>& MusicModeIntervals(int mode);           //semitones above the root
//Semitones above the root for a degree of the mode's SEVEN-note frame (any octave: 7 is the root
//an octave up, -1 the seventh below). See MusicMotifDef::notes for what a pentatonic mode does.
int MusicModeDegree(int mode, int degree);

//"A", "F#", "Bb" -> 0..11, and "A3" -> 57. -1 if it cannot be read.
int MusicPitchClassFromName(const std::string& name);
int MusicMidiFromName(const std::string& name);
std::string MusicPitchClassName(int pc);
std::string MusicNoteName(int midi);                            //"-" below 0: an unpitched voice's note

#endif
