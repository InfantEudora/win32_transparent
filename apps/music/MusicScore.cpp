#include "MusicScore.h"

#include "File.h"
#include "Debug.h"
#include "tinygltf/json.hpp"
using json = nlohmann::json;

#include <cctype>
#include <cstdint>
#include <cstring>

static Debugger* debug = new Debugger("MusicScore", DEBUG_ALL);

//--- modes ---------------------------------------------------------------------------------

namespace {

struct ModeInfo{
    const char* name;
    std::vector<int> intervals;
};

//The pentatonics first because they are the safe default for ambience: no semitone steps, so no
//note in them can clash with another, and a random walk over one never sounds wrong - only plain.
const ModeInfo kModes[MUSIC_MODE_COUNT] = {
    {"pentatonic_minor", {0, 3, 5, 7, 10}},
    {"aeolian",          {0, 2, 3, 5, 7, 8, 10}},
    {"dorian",           {0, 2, 3, 5, 7, 9, 10}},       //minor with a raised 6th - brighter
    {"phrygian",         {0, 1, 3, 5, 7, 8, 10}},       //minor with a flat 2nd - darker
    {"pentatonic_major", {0, 2, 4, 7, 9}},
    {"ionian",           {0, 2, 4, 5, 7, 9, 11}},
};

const char* kPitchNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} //namespace

const char* MusicModeName(int mode){
    return (mode >= 0 && mode < MUSIC_MODE_COUNT) ? kModes[mode].name : "?";
}

int MusicModeFromName(const std::string& name){
    for (int m = 0; m < MUSIC_MODE_COUNT; m++) if (name == kModes[m].name) return m;
    if (name == "minor") return MUSIC_MODE_AEOLIAN;
    if (name == "major") return MUSIC_MODE_IONIAN;
    return -1;
}

const std::vector<int>& MusicModeIntervals(int mode){
    return kModes[(mode >= 0 && mode < MUSIC_MODE_COUNT) ? mode : 0].intervals;
}

int MusicPitchClassFromName(const std::string& name){
    if (name.empty()) return -1;
    int pc = -1;
    switch (std::toupper((unsigned char)name[0])){
        case 'C': pc = 0; break;  case 'D': pc = 2; break;  case 'E': pc = 4; break;
        case 'F': pc = 5; break;  case 'G': pc = 7; break;  case 'A': pc = 9; break;
        case 'B': pc = 11; break;
        default: return -1;
    }
    for (size_t i = 1; i < name.size(); i++){
        if (name[i] == '#') pc++;
        else if (name[i] == 'b') pc--;
        else break;
    }
    return (pc % 12 + 12) % 12;
}

int MusicMidiFromName(const std::string& name){
    const int natural = MusicPitchClassFromName(name.substr(0, 1));
    if (natural < 0) return -1;
    size_t i = 1;
    int offset = 0;
    for (; i < name.size() && (name[i] == '#' || name[i] == 'b'); i++) offset += (name[i] == '#') ? 1 : -1;
    if (i >= name.size() || !(std::isdigit((unsigned char)name[i]) || name[i] == '-')) return -1;
    const int octave = std::atoi(name.c_str() + i);
    //The octave belongs to the NATURAL note and the accidental is applied after it, because an
    //accidental can carry a note across the octave line: B#3 is C4, and Cb4 is B3.
    return (octave + 1) * 12 + natural + offset;
}

std::string MusicPitchClassName(int pc){
    return kPitchNames[(pc % 12 + 12) % 12];
}

std::string MusicNoteName(int midi){
    const int pc = (midi % 12 + 12) % 12;
    return std::string(kPitchNames[pc]) + std::to_string((midi - pc) / 12 - 1);
}

const MusicSample* MusicScore::Sample(const std::string& name) const{
    auto it = samples.find(name);
    return it == samples.end() ? nullptr : it->second.get();
}

//--- loading -------------------------------------------------------------------------------

namespace {

/*
    A PCM16 wav, by walking its chunks - not WaveFile, for two reasons. WaveFile goes through
    LoadFile, which exits the process on a missing file and caches for the life of the process,
    and a score is something that is edited and reloaded while the app runs: a typo in a sample
    name must be an error on screen, and a re-exported sample must be re-read. And WaveFile
    assumes the data starts at byte 44, which is true of what samplescan writes and not of every
    wav an editor saves (a LIST chunk in front of the data is common).
*/
bool LoadWav(const std::string& asset, MusicSample& out, std::string& error){
    std::string path;
    if (!ResolveAssetPath(asset.c_str(), path)){
        error = "no such sample: " + asset + " (run `make samples` in apps/music?)";
        return false;
    }
    std::string bytes;
    if (!ReadFileToString(path.c_str(), bytes) || bytes.size() < 12 ||
        bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0){
        error = asset + " is not a wav";
        return false;
    }
    auto u16 = [&](size_t at){ uint16_t v; memcpy(&v, bytes.data() + at, 2); return v; };
    auto u32 = [&](size_t at){ uint32_t v; memcpy(&v, bytes.data() + at, 4); return v; };

    int format = 0, bits = 0;
    size_t data_at = 0, data_len = 0;
    for (size_t at = 12; at + 8 <= bytes.size();){
        const uint32_t len = u32(at + 4);
        if (bytes.compare(at, 4, "fmt ") == 0 && len >= 16){
            format = u16(at + 8);
            out.channels = u16(at + 10);
            out.rate = (int)u32(at + 12);
            bits = u16(at + 22);
        }
        else if (bytes.compare(at, 4, "data") == 0){
            data_at = at + 8;
            data_len = std::min<size_t>(len, bytes.size() - data_at);
        }
        at += 8 + len + (len & 1);      //chunks are word-aligned
    }
    if (format != 1 || bits != 16 || out.channels < 1 || out.channels > 2 || out.rate <= 0 || data_at == 0){
        error = asset + ": only 16-bit PCM mono or stereo is read here - export it with samplescan";
        return false;
    }
    out.name = asset;
    out.frames = data_len / (2 * out.channels);
    out.pcm.resize(out.frames * out.channels);
    for (size_t i = 0; i < out.pcm.size(); i++){
        int16_t s;
        memcpy(&s, bytes.data() + data_at + i * 2, 2);
        out.pcm[i] = s / 32768.0f;
    }
    return true;
}

/*
    Typed reads that cannot abort. This codebase builds with JSON_NOEXCEPTION, under which a
    json::value() of the wrong type is not an exception but std::abort - so a score with "gain":
    "loud" in it would take the app down. Every read checks the type first and falls back.
*/
float Num(const json& j, const char* key, float fallback){
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}
std::string Str(const json& j, const char* key, const std::string& fallback = ""){
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

bool Note(const json& j, const char* key, int fallback, int& out, const std::string& where, std::string& error){
    const std::string s = Str(j, key);
    if (s.empty()){ out = fallback; return true; }
    out = MusicMidiFromName(s);
    if (out < 0){ error = where + ": '" + s + "' is not a note like A3 or F#4"; return false; }
    return true;
}

} //namespace

bool LoadMusicScore(const char* asset_name, MusicScore& out, std::string& error){
    std::string path, text;
    if (!ResolveAssetPath(asset_name, path) || !ReadFileToString(path.c_str(), text)){
        error = std::string("no such score: ") + asset_name;
        return false;
    }
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()){
        error = std::string(asset_name) + " is not valid JSON";
        return false;
    }

    MusicScore s;
    s.file = asset_name;
    s.name = Str(j, "name", asset_name);
    s.root_pc = MusicPitchClassFromName(Str(j, "root", "A"));
    s.mode = MusicModeFromName(Str(j, "mode", "pentatonic_minor"));
    if (s.root_pc < 0 || s.mode < 0){
        error = "root must be a note name and mode one of pentatonic_minor, aeolian, dorian, phrygian, pentatonic_major, ionian";
        return false;
    }
    s.bpm = Num(j, "bpm", 60.0f);
    s.tension_max = Num(j, "tension_max", 0.3f);

    auto want_sample = [&](const std::string& name, const std::string& where) -> bool {
        if (name.empty()){ error = where + " names no sample"; return false; }
        if (s.samples.count(name)) return true;
        auto sample = std::make_shared<MusicSample>();
        if (!LoadWav(name, *sample, error)) return false;
        s.samples[name] = sample;
        return true;
    };

    if (j.contains("beds") && j["beds"].is_array()){
        for (const json& b : j["beds"]){
            MusicBedDef d;
            d.name = Str(b, "name", Str(b, "sample"));
            d.sample = Str(b, "sample");
            d.gain_calm = Num(b, "gain_calm", 0.5f);
            d.gain_tense = Num(b, "gain_tense", d.gain_calm);
            d.degree = (int)Num(b, "degree", 0);
            d.fade_s = Num(b, "fade_s", 3.0f);
            const std::string where = "bed '" + d.name + "'";
            if (!Note(b, "root", -1, d.root_midi, where, error)) return false;
            if (!want_sample(d.sample, where)) return false;
            s.beds.push_back(d);
        }
    }
    if (j.contains("voices") && j["voices"].is_array()){
        for (const json& v : j["voices"]){
            MusicVoiceDef d;
            d.name = Str(v, "name", Str(v, "sample"));
            d.sample = Str(v, "sample");
            const std::string where = "voice '" + d.name + "'";
            if (!Note(v, "root", 60, d.root_midi, where, error)) return false;
            if (!Note(v, "low", d.root_midi - 3, d.low_midi, where, error)) return false;
            if (!Note(v, "high", d.root_midi + 12, d.high_midi, where, error)) return false;
            if (d.high_midi < d.low_midi){ error = where + ": high is below low"; return false; }
            d.density_calm = Num(v, "density_calm", 0.2f);
            d.density_tense = Num(v, "density_tense", d.density_calm);
            d.gain = Num(v, "gain", 0.5f);
            d.length_beats = Num(v, "length_beats", 0.0f);
            d.release_s = Num(v, "release_s", 0.3f);
            d.offbeat = Num(v, "offbeat", 0.25f);
            d.spread = Num(v, "spread", 0.3f);
            //A sample pushed more than an octave either way stops sounding like its instrument -
            //an octave up a kalimba is a music box, two down it is mud. Said, not refused: the
            //score may want exactly that.
            if (d.low_midi < d.root_midi - 12 || d.high_midi > d.root_midi + 12){
                debug->Warn("%s: register %s..%s is more than an octave from its sample's %s\n", where.c_str(),
                            MusicNoteName(d.low_midi).c_str(), MusicNoteName(d.high_midi).c_str(),
                            MusicNoteName(d.root_midi).c_str());
            }
            if (!want_sample(d.sample, where)) return false;
            s.voices.push_back(d);
        }
    }
    if (j.contains("stingers") && j["stingers"].is_array()){
        for (const json& t : j["stingers"]){
            MusicStingerDef d;
            d.name = Str(t, "name", Str(t, "sample"));
            d.sample = Str(t, "sample");
            d.gain = Num(t, "gain", 0.8f);
            if (!want_sample(d.sample, "stinger '" + d.name + "'")) return false;
            s.stingers.push_back(d);
        }
    }

    debug->Ok("Score '%s': %s %s at %.0f bpm, %zu beds, %zu voices, %zu stingers, %zu samples\n", s.name.c_str(),
              MusicPitchClassName(s.root_pc).c_str(), MusicModeName(s.mode), s.bpm,
              s.beds.size(), s.voices.size(), s.stingers.size(), s.samples.size());
    out = std::move(s);
    return true;
}
