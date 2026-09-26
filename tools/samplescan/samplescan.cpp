/*
    samplescan - catalogues a folder of audio samples for the music system.

    Walks a sample library, measures every file it can decode (see analyse.h), reads whatever
    the file's name and folders say about it, makes a category guess, and writes one CSV row
    per file. The point is to turn "a few hundred files scattered about" into a table that can
    be sorted by key, length and kind, and corrected by hand where the guess is wrong.

        samplescan                      scans ../../apps/music/samples
        samplescan <folder>             scans that folder instead
        samplescan --file <audio>       measures one file and prints every field

    The catalog is <folder>/catalog.csv. Four of its columns are YOURS - category, instrument,
    root and comment - and a re-scan keeps what you typed in them, matched by path. Everything
    else is rewritten from the audio every time, so a changed heuristic re-measures the whole
    library rather than leaving old guesses behind.

    Read the guess as a first sort, not an answer. The thresholds in GuessCategory were chosen
    on synthetic test tones and the handful of effects already in the repo; they want tuning on
    the real library once it is here.
*/
#include "analyse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

//What miniaudio decodes for us. Anything in the second list is catalogued as unsupported
//rather than skipped, so it is visible in the table and can be converted.
const char* kDecodable[] = {".wav", ".flac", ".mp3"};
const char* kKnownAudio[] = {".ogg", ".aif", ".aiff", ".m4a", ".aac", ".opus", ".wma"};

bool ExtIn(const std::string& ext, const char* const* list, size_t n){
    for (size_t i = 0; i < n; i++) if (ext == list[i]) return true;
    return false;
}

std::string Lower(std::string s){
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/*
    What a file's path says about it.

    Sample packs are named far more consistently than one would hope - "Kalimba_C4.wav",
    "Pad Am 90bpm.wav", "Whispers/whisper_03.wav" - and that is information the audio cannot
    give: no measurement says "kalimba". Folder names count as well as the file name, because
    packs usually put the instrument in the folder and just the note in the file.
*/
struct NameHints{
    std::string instruments;    //every keyword found, joined with '+'
    std::string note;           //"C4" as written in the name
    int note_midi = -1;
    std::string key;            //"A minor"
    int bpm = 0;
};

//Words that name a source. Matched against whole words (and their plural), never substrings,
//so "pad" does not fire on "paddle" and "pan" does not fire on "panning".
const char* kInstrumentWords[] = {
    "kalimba", "marimba", "xylophone", "vibraphone", "glockenspiel", "celesta", "bell", "chime",
    "piano", "rhodes", "harp", "guitar", "pluck", "koto", "sitar", "mbira",
    "flute", "panflute", "shakuhachi", "ocarina", "whistle", "didgeridoo",
    "choir", "voice", "vox", "whisper", "breath", "chant",
    "pad", "drone", "hum", "string", "violin", "cello", "bass", "synth", "organ",
    "drum", "perc", "percussion", "shaker", "rattle", "clap", "wood", "stick", "log",
    "rain", "water", "stream", "wind", "leaves", "leaf", "forest", "jungle",
    "bird", "insect", "cricket", "cicada", "frog",
    "ambience", "ambient", "atmos", "atmosphere", "texture", "noise", "riser", "swell", "hit", "impact",
};

std::vector<std::string> Words(const std::string& s){
    std::vector<std::string> out;
    std::string cur;
    for (char c : s){
        //'#' stays in a word so "C#4" survives as one token.
        if (std::isalnum((unsigned char)c) || c == '#') cur += c;
        else if (!cur.empty()){ out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int PitchClass(char letter){
    switch (std::tolower((unsigned char)letter)){
        case 'c': return 0;  case 'd': return 2;  case 'e': return 4;  case 'f': return 5;
        case 'g': return 7;  case 'a': return 9;  case 'b': return 11;
    }
    return -1;
}

NameHints ReadName(const std::string& rel_path){
    NameHints h;
    std::string stem = rel_path;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    const std::vector<std::string> words = Words(stem);

    for (size_t wi = 0; wi < words.size(); wi++){
        const std::string& raw = words[wi];
        const std::string w = Lower(raw);

        //Letters and digits run together ("kalimba01") hide the word, so the letters are also
        //tried on their own.
        std::string letters;
        for (char c : w) if (std::isalpha((unsigned char)c)) letters += c; else break;
        for (const char* kw : kInstrumentWords){
            const std::string k = kw;
            if (w == k || w == k + "s" || w == k + "es" || letters == k || letters == k + "s"){
                if (h.instruments.find(k) == std::string::npos){
                    if (!h.instruments.empty()) h.instruments += "+";
                    h.instruments += k;
                }
            }
        }

        //A note: letter, optional accidental, one octave digit. "C4", "c#3", "Bb2", "Fs1".
        if (h.note.empty() && w.size() >= 2 && w.size() <= 3 && PitchClass(w[0]) >= 0 && std::isdigit((unsigned char)w.back())){
            int pc = PitchClass(w[0]);
            bool f_valid = true;
            if (w.size() == 3){
                if (w[1] == '#' || w[1] == 's') pc += 1;
                else if (w[1] == 'b') pc -= 1;
                else f_valid = false;
            }
            if (f_valid){
                const int octave = w.back() - '0';
                h.note = raw;
                h.note_midi = (octave + 1) * 12 + pc;
            }
        }

        //A key: "Am", "C#m", "Ebmaj", "Dmin", "Fminor".
        if (h.key.empty() && PitchClass(w[0]) >= 0 && w.size() >= 2){
            size_t i = 1;
            int pc = PitchClass(w[0]);
            if (w[i] == '#'){ pc += 1; i++; }
            else if (w[i] == 'b' && i + 1 < w.size()){ pc -= 1; i++; }
            const std::string mode = w.substr(i);
            const char* mode_name = nullptr;
            if (mode == "m" || mode == "min" || mode == "minor") mode_name = "minor";
            else if (mode == "maj" || mode == "major") mode_name = "major";
            if (mode_name){
                h.key = NoteName(60 + ((pc % 12) + 12) % 12);
                h.key = h.key.substr(0, h.key.size() - 1) + " " + mode_name;     //drop the octave
            }
        }

        //A tempo: "90bpm", or "90 bpm" as two words.
        if (h.bpm == 0){
            if (w.size() > 3 && w.compare(w.size() - 3, 3, "bpm") == 0) h.bpm = std::atoi(w.c_str());
            else if (wi + 1 < words.size() && Lower(words[wi + 1]) == "bpm") h.bpm = std::atoi(w.c_str());
        }
    }
    return h;
}

/*
    A first sort, from the measurements alone. The order of the tests is the logic: a clear
    single pitch is checked first because it is the strongest evidence there is, and the
    noise-like and long-mix cases only get a look once that has failed.

    The categories are the roles a sample plays in the music system, not what made the sound:

      note_struck     one pitched note that dies away - kalimba, marimba, bell, pluck
      note_sustained  one pitched note that holds - flute, pad, voice, bowed string
      drone           a low held note (under ~110 Hz) - the hum under everything
      hum             low and held but with no clear pitch
      texture         unpitched and held - wind, rain, leaves, whispers, insects
      hit             unpitched and short - a knock, a shaker, a twig
      phrase          several notes - a melody, an arpeggio, a chord change
      mix             long, busy material with no single role - a finished ambience
*/
//Sources that are textures however they measure. Used only to settle hum-versus-texture, which
//the audio genuinely cannot: a dark wind recording and an unpitched rumble have the same numbers.
const char* kTextureWords[] = {"wind", "rain", "water", "stream", "leaves", "leaf", "whisper", "breath",
                               "insect", "cricket", "cicada", "forest", "jungle", "noise", "texture"};

bool NamesTexture(const NameHints& h){
    for (const char* w : kTextureWords){
        const std::string k = w;
        //Whole '+'-separated entries, not substrings.
        const std::string list = "+" + h.instruments + "+";
        if (list.find("+" + k + "+") != std::string::npos) return true;
    }
    return false;
}

std::string GuessCategory(const SampleAnalysis& a, const NameHints& h, std::string& why){
    char buf[160];
    if (a.pitch_kind == "single"){
        const bool f_held = a.envelope == "sustained" || a.envelope == "swell";
        if (f_held && a.pitch_hz < 110.0){
            snprintf(buf, sizeof buf, "one held pitch at %.0f Hz", a.pitch_hz);
            why = buf;
            return "drone";
        }
        snprintf(buf, sizeof buf, "one pitch (%.0f%% voiced, +-%.0f c), %s", a.voiced * 100, a.stability_cents, a.envelope.c_str());
        why = buf;
        return f_held ? "note_sustained" : "note_struck";
    }
    if (a.pitch_kind == "none" || a.flatness > 0.3){
        if (a.envelope == "short" || (a.envelope == "decaying" && a.duration_s < 2.0)){
            why = "no pitch, " + a.envelope;
            return "hit";
        }
        if (a.low_share > 0.6 && NamesTexture(h)){
            snprintf(buf, sizeof buf, "no pitch, low (%.0f%% under 200 Hz), name says %s", a.low_share * 100, h.instruments.c_str());
            why = buf;
            return "texture";
        }
        if (a.low_share > 0.6){
            snprintf(buf, sizeof buf, "no pitch, %.0f%% of energy under 200 Hz", a.low_share * 100);
            why = buf;
            return "hum";
        }
        if (a.duration_s > 30.0 && a.key_r > 0.7){
            snprintf(buf, sizeof buf, "long, noisy but key-like (r %.2f)", a.key_r);
            why = buf;
            return "mix";
        }
        snprintf(buf, sizeof buf, "no pitch, flatness %.2f, %s", a.flatness, a.envelope.c_str());
        why = buf;
        return "texture";
    }
    //Pitched, but not one pitch.
    if (a.duration_s > 30.0){
        why = "long, several pitches";
        return "mix";
    }
    snprintf(buf, sizeof buf, "several pitches (%.0f%% voiced, +-%.0f c)", a.voiced * 100, a.stability_cents);
    why = buf;
    return "phrase";
}

//---------------------------------------------------------------------------------------
// CSV
//---------------------------------------------------------------------------------------

std::string CsvField(const std::string& s){
    if (s.find_first_of(",;\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s){ if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

//Splits one line. The separator is passed in rather than assumed, because Excel in a Dutch
//locale saves CSV with ';' - a catalog opened, edited and saved there must still read back.
std::vector<std::string> CsvSplit(const std::string& line, char sep){
    std::vector<std::string> out;
    std::string cur;
    bool f_quoted = false;
    for (size_t i = 0; i < line.size(); i++){
        const char c = line[i];
        if (f_quoted){
            if (c == '"'){
                if (i + 1 < line.size() && line[i + 1] == '"'){ cur += '"'; i++; }
                else f_quoted = false;
            }
            else cur += c;
        }
        else if (c == '"') f_quoted = true;
        else if (c == sep){ out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

//The hand-kept columns. Their names are the contract with whoever edits the file.
const char* kHumanColumns[] = {"category", "instrument", "root", "comment"};
const int kNumHuman = 4;

typedef std::map<std::string, std::vector<std::string>> HumanFields;    //file -> the four values

HumanFields ReadPreviousCatalog(const fs::path& csv){
    HumanFields out;
    std::ifstream in(csv, std::ios::binary);
    if (!in) return out;
    std::string line;
    std::vector<std::string> header;
    char sep = ',';
    while (std::getline(in, line)){
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);     //BOM
        if (line.rfind("sep=", 0) == 0) continue;
        if (header.empty()){
            sep = std::count(line.begin(), line.end(), ';') > std::count(line.begin(), line.end(), ',') ? ';' : ',';
            header = CsvSplit(line, sep);
            continue;
        }
        const std::vector<std::string> row = CsvSplit(line, sep);
        std::string file;
        std::vector<std::string> kept(kNumHuman);
        for (size_t c = 0; c < header.size() && c < row.size(); c++){
            if (header[c] == "file") file = row[c];
            for (int h = 0; h < kNumHuman; h++) if (header[c] == kHumanColumns[h]) kept[h] = row[c];
        }
        if (!file.empty()) out[file] = kept;
    }
    return out;
}

std::string Num(double v, int decimals){
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

struct Row{
    std::string file;
    std::string status;
    std::string guess, why;
    NameHints hints;
    SampleAnalysis a;
};

void PrintDetail(const std::string& name, const SampleAnalysis& a, const NameHints& h, const std::string& guess, const std::string& why){
    printf("%s\n", name.c_str());
    if (!a.f_ok){ printf("  error: %s\n", a.error.c_str()); return; }
    printf("  format      %d ch, %d Hz, %.3f s%s\n", a.channels, a.sample_rate, a.duration_s, a.f_truncated ? " (truncated)" : "");
    printf("  level       peak %.1f dBFS, rms %.1f, active rms %.1f, %d clipped\n", a.peak_db, a.rms_db, a.active_rms_db, a.clipped);
    printf("  silence     lead %.0f ms, tail %.0f ms\n", a.lead_ms, a.tail_ms);
    printf("  envelope    %s: attack %.0f ms, decay-20dB %.0f ms, sustain %.1f dB, tail-vs-head %.1f dB\n",
           a.envelope.c_str(), a.attack_ms, a.decay20_ms, a.sustain_db, a.head_tail_db);
    printf("  spectrum    centroid %.0f Hz, flatness %.3f, under 200 Hz %.0f%%\n", a.centroid_hz, a.flatness, a.low_share * 100);
    if (a.pitch_kind != "none"){
        const int n = (int)std::lround(a.pitch_midi);
        printf("  pitch       %s: %s %+.0f c (%.2f Hz), %.0f%% voiced, +-%.0f c\n", a.pitch_kind.c_str(), NoteName(n).c_str(),
               (a.pitch_midi - n) * 100, a.pitch_hz, a.voiced * 100, a.stability_cents);
    }
    else printf("  pitch       none (%.0f%% voiced)\n", a.voiced * 100);
    printf("  key         %s (r %.2f, margin %.2f)\n", a.key.c_str(), a.key_r, a.key_margin);
    printf("  name says   instrument '%s', note '%s', key '%s', bpm %d\n", h.instruments.c_str(), h.note.c_str(), h.key.c_str(), h.bpm);
    printf("  guess       %s - %s\n", guess.c_str(), why.c_str());
}

int ScanOne(const fs::path& file){
    SampleAnalysis a;
    AnalyseFile(file.wstring(), a);
    //The parent folder too, since packs put the instrument there - the same as a folder scan sees.
    const NameHints h = ReadName((file.parent_path().filename() / file.filename()).generic_u8string());
    std::string why, guess = a.f_ok ? GuessCategory(a, h, why) : "";
    PrintDetail(file.u8string(), a, h, guess, why);
    return a.f_ok ? 0 : 1;
}

int ScanFolder(const fs::path& root){
    std::error_code ec;
    if (!fs::is_directory(root, ec)){
        fprintf(stderr, "samplescan: '%s' is not a folder\n", root.u8string().c_str());
        return 1;
    }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)){
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().u8string());
        if (ExtIn(ext, kDecodable, 3) || ExtIn(ext, kKnownAudio, 7)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    const fs::path csv = root / "catalog.csv";
    const HumanFields previous = ReadPreviousCatalog(csv);

    std::vector<Row> rows;
    std::map<std::string, int> counts;
    for (const fs::path& p : files){
        Row r;
        r.file = fs::relative(p, root, ec).generic_u8string();
        r.hints = ReadName(r.file);
        const std::string ext = Lower(p.extension().u8string());
        if (!ExtIn(ext, kDecodable, 3)) r.status = "unsupported " + ext;
        else if (!AnalyseFile(p.wstring(), r.a)) r.status = r.a.error;
        else{
            r.status = "ok";
            r.guess = GuessCategory(r.a, r.hints, r.why);
        }
        counts[r.guess.empty() ? "(not read)" : r.guess]++;

        std::string tonal = "-";
        if (r.a.f_ok && r.a.pitch_kind == "single") tonal = NoteName((int)std::lround(r.a.pitch_midi));
        else if (r.a.f_ok && r.a.pitch_kind == "multi") tonal = r.a.key;
        printf("%-15s %-9s %7.2fs %6.1fdB  %s\n", r.guess.empty() ? r.status.c_str() : r.guess.c_str(),
               tonal.c_str(), r.a.duration_s, r.a.peak_db, r.file.c_str());
        rows.push_back(r);
    }

    std::ofstream out(csv, std::ios::binary);
    if (!out){
        fprintf(stderr, "samplescan: cannot write %s\n", csv.u8string().c_str());
        return 1;
    }
    out << "file";
    for (int h = 0; h < kNumHuman; h++) out << "," << kHumanColumns[h];
    out << ",guess,why,status,name_instrument,name_note,name_vs_pitch,name_key,name_bpm"
           ",pitch_kind,pitch_note,pitch_cents,pitch_hz,voiced,stability_cents,key,key_r,key_margin"
           ",duration_s,channels,sample_rate,peak_db,rms_db,active_rms_db,clipped"
           ",lead_ms,tail_ms,envelope,attack_ms,decay20_ms,sustain_db,head_tail_db"
           ",centroid_hz,flatness,low_share\n";

    for (const Row& r : rows){
        const SampleAnalysis& a = r.a;
        out << CsvField(r.file);
        auto kept = previous.find(r.file);
        for (int h = 0; h < kNumHuman; h++) out << "," << CsvField(kept != previous.end() ? kept->second[h] : "");
        out << "," << r.guess << "," << CsvField(r.why) << "," << CsvField(r.status)
            << "," << r.hints.instruments << "," << CsvField(r.hints.note);

        const bool f_single = a.f_ok && a.pitch_kind == "single";
        const int pitch_n = f_single ? (int)std::lround(a.pitch_midi) : 0;
        //A difference of exactly 12 is nearly always the other octave convention (C3 as middle C,
        //which Yamaha and Ableton use), not a mislabelled file.
        out << "," << (f_single && r.hints.note_midi >= 0 ? std::to_string(pitch_n - r.hints.note_midi) : "");
        out << "," << CsvField(r.hints.key) << "," << (r.hints.bpm ? std::to_string(r.hints.bpm) : "");

        if (!a.f_ok){
            out << std::string(26, ',') << "\n";
            continue;
        }
        out << "," << a.pitch_kind
            << "," << (f_single ? NoteName(pitch_n) : "")
            << "," << (f_single ? Num((a.pitch_midi - pitch_n) * 100, 0) : "")
            << "," << (a.pitch_kind != "none" ? Num(a.pitch_hz, 2) : "")
            << "," << Num(a.voiced, 2)
            << "," << (a.pitch_kind != "none" ? Num(a.stability_cents, 0) : "")
            << "," << CsvField(a.key) << "," << Num(a.key_r, 2) << "," << Num(a.key_margin, 2)
            << "," << Num(a.duration_s, 3) << "," << a.channels << "," << a.sample_rate
            << "," << Num(a.peak_db, 1) << "," << Num(a.rms_db, 1) << "," << Num(a.active_rms_db, 1) << "," << a.clipped
            << "," << Num(a.lead_ms, 0) << "," << Num(a.tail_ms, 0) << "," << a.envelope
            << "," << Num(a.attack_ms, 0) << "," << Num(a.decay20_ms, 0) << "," << Num(a.sustain_db, 1) << "," << Num(a.head_tail_db, 1)
            << "," << Num(a.centroid_hz, 0) << "," << Num(a.flatness, 3) << "," << Num(a.low_share, 2)
            << "\n";
    }

    printf("\n%zu files -> %s\n", rows.size(), csv.u8string().c_str());
    for (const auto& c : counts) printf("  %-15s %d\n", c.first.c_str(), c.second);
    return 0;
}

} //namespace

int main(int argc, char** argv){
    if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")){
        printf("samplescan [folder]         catalogue every audio file under folder (default ../../apps/music/samples)\n"
               "samplescan --file <audio>   measure one file and print every field\n");
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--file") return ScanOne(fs::u8path(argv[2]));
    return ScanFolder(fs::u8path(argc >= 2 ? argv[1] : "../../apps/music/samples"));
}
