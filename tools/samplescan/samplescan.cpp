/*
    samplescan - catalogues a folder of audio samples for the music system.

    Walks a sample library, measures every file it can decode (see analyse.h), reads whatever
    the file's name and folders say about it, makes a category guess, and writes one CSV row
    per file. The point is to turn "a few hundred files scattered about" into a table that can
    be sorted by key, length and kind, and corrected by hand where the guess is wrong.

        samplescan                      scans ../../apps/music/samples
        samplescan <folder>             scans that folder instead
        samplescan --new [folder]       measures only files the catalog has no row for yet
        samplescan --file <audio>       measures one file and prints every field

    The catalog is <folder>/catalog.csv. Four of its columns are YOURS - category, instrument,
    root and comment - and a re-scan keeps what you typed in them, matched by path. Everything
    else is rewritten from the audio every time, so a changed heuristic re-measures the whole
    library rather than leaving old guesses behind. --new is the exception: it measures only the
    files that have no row yet and copies every other row as it is.

    Read the guess as a first sort, not an answer. The thresholds in GuessCategory were chosen
    on synthetic test tones and the handful of effects already in the repo; they want tuning on
    the real library once it is here.
*/
#include "analyse.h"
#include "export.h"

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
    std::string instruments;    //every source or form word found, joined with '+'
    std::string moods;          //every mood word found - "creepy+horror"
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
    "loop", "transition", "stinger", "whoosh",
};

//Words that say how a sample FEELS. The music system will be asked for suspense, not for
//"sounds like a violin", and a pack's own name for its mood is the best first answer to that.
const char* kMoodWords[] = {
    "creepy", "eerie", "eery", "scary", "scare", "spooky", "horror", "halloween", "suspense", "tense",
    "dark", "ominous", "mysterious", "mystery", "haunting", "sad", "melancholic",
    "calm", "peaceful", "relaxing", "dreamy", "gentle", "soft", "meditation",
    "happy", "comedy", "funny", "playful", "epic", "dramatic", "heroic", "tribal", "magical",
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
        auto collect = [&](const char* const* list, size_t count, std::string& into){
            for (size_t i = 0; i < count; i++){
                const std::string k = list[i];
                if (!(w == k || w == k + "s" || w == k + "es" || letters == k || letters == k + "s")) continue;
                if (("+" + into + "+").find("+" + k + "+") != std::string::npos) continue;     //already listed
                if (!into.empty()) into += "+";
                into += k;
            }
        };
        collect(kInstrumentWords, sizeof kInstrumentWords / sizeof *kInstrumentWords, h.instruments);
        collect(kMoodWords, sizeof kMoodWords / sizeof *kMoodWords, h.moods);

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
      rhythm          a pulse - drums, a percussion loop
      phrase          several notes - a melody, an arpeggio, a chord change
      mix             long, busy material with no single role - a finished ambience
*/
/*
    Name words that settle what the audio cannot. Only ever as a tie-break, and each for one
    specific confusion seen on the first real library:

      texture words: a dark wind and an unpitched rumble measure the same, and a WHISTLING wind
        is genuinely pitched - "creepy wind gust" came back a 23-note phrase. Not applied when
        the name also names a melodic instrument, so "flute rain loop" stays a flute.
      drum words: slow atmospheric drums have too few hits in five seconds to show a pulse.
*/
const char* kTextureWords[] = {"wind", "rain", "water", "stream", "leaves", "leaf", "whisper", "breath",
                               "insect", "cricket", "cicada", "forest", "jungle", "noise", "texture"};
const char* kMelodicWords[] = {"kalimba", "marimba", "xylophone", "vibraphone", "glockenspiel", "celesta", "bell",
                               "chime", "piano", "rhodes", "harp", "guitar", "koto", "sitar", "mbira", "flute",
                               "panflute", "shakuhachi", "ocarina", "whistle", "choir", "voice", "string",
                               "violin", "cello", "organ", "synth", "pad"};
const char* kDrumWords[] = {"drum", "perc", "percussion", "shaker", "rattle"};

//Octave-band flatness above which a sound counts as noise even if YIN found a period in it -
//breath and wind can hold a faint whistle. See noisiness in analyse.h.
const double kNoisy = 0.35;

template <size_t N>
bool NamesAny(const NameHints& h, const char* const (&words)[N]){
    //Whole '+'-separated entries, not substrings.
    const std::string list = "+" + h.instruments + "+";
    for (const char* w : words) if (list.find("+" + std::string(w) + "+") != std::string::npos) return true;
    return false;
}
bool NamesTexture(const NameHints& h){ return NamesAny(h, kTextureWords) && !NamesAny(h, kMelodicWords); }

/*
    Could this play end to end, round and round, without a click or a gap? No silence at either
    end, and the last 200 ms within 3 dB of the first. It does not listen to the seam - a level
    match is necessary, not sufficient - but it finds the candidates worth listening to.
*/
bool Loopable(const SampleAnalysis& a){
    return a.f_ok && a.duration_s >= 2.0 && a.lead_ms <= 50 && a.tail_ms <= 50 && std::fabs(a.head_tail_db) <= 3.0;
}

std::string GuessCategory(const SampleAnalysis& a, const NameHints& h, std::string& why){
    char buf[200];
    const double active_s = a.duration_s - (a.lead_ms + a.tail_ms) / 1000.0;
    if (a.pitch_kind == "single"){
        const bool f_held = a.envelope == "sustained" || a.envelope == "swell";
        //Long counts as well as low: eight seconds of one held note is a bed to lay things
        //over, whatever its register - which is the role "drone" names.
        if (f_held && (a.pitch_hz < 110.0 || active_s >= 8.0)){
            snprintf(buf, sizeof buf, "one pitch held %.0f s at %.0f Hz", active_s, a.pitch_hz);
            why = buf;
            return "drone";
        }
        snprintf(buf, sizeof buf, "one pitch (%.0f%% voiced, +-%.0f c), %s, %d note%s", a.voiced * 100, a.stability_cents,
                 a.envelope.c_str(), a.note_count, a.note_count == 1 ? "" : "s");
        why = buf;
        return f_held ? "note_sustained" : "note_struck";
    }
    //A pulse, and not a melody: drums, shakers, a loop. Checked before the noise tests because
    //a drum loop is unpitched too, and "texture" would bury the one thing that matters about it.
    if (a.onsets >= 8 && a.beat_strength >= 0.3 && a.voiced < 0.6){
        snprintf(buf, sizeof buf, "%d onsets, pulse %.0f bpm (strength %.2f)", a.onsets, a.bpm, a.beat_strength);
        why = buf;
        return "rhythm";
    }
    if (a.onsets >= 3 && NamesAny(h, kDrumWords)){
        snprintf(buf, sizeof buf, "%d onsets, name says %s", a.onsets, h.instruments.c_str());
        why = buf;
        return "rhythm";
    }
    if (a.envelope != "short" && NamesTexture(h)){
        snprintf(buf, sizeof buf, "name says %s; %.0f%% voiced%s", h.instruments.c_str(), a.voiced * 100,
                 a.notes.find('~') != std::string::npos ? ", gliding" : "");
        why = buf;
        return "texture";
    }
    if (a.pitch_kind == "none" || a.noisiness > kNoisy){
        if (a.envelope == "short" || (a.envelope == "decaying" && a.duration_s < 2.0)){
            why = "no pitch, " + a.envelope;
            return "hit";
        }
        if (a.low_share > 0.6){
            snprintf(buf, sizeof buf, "no pitch, %.0f%% of energy under 200 Hz", a.low_share * 100);
            why = buf;
            return "hum";
        }
        snprintf(buf, sizeof buf, "no pitch, noisiness %.2f, %s", a.noisiness, a.envelope.c_str());
        why = buf;
        return "texture";
    }
    //Pitched, but not one pitch.
    if (a.duration_s > 30.0){
        snprintf(buf, sizeof buf, "long, %d notes over %s", a.note_count, a.pitch_classes.c_str());
        why = buf;
        return "mix";
    }
    snprintf(buf, sizeof buf, "%d notes over %s (%.0f%% voiced)", a.note_count, a.pitch_classes.c_str(), a.voiced * 100);
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

//The catalog's columns, in order. A --new scan copies old rows across by these names.
const char* kColumns = "file,category,instrument,root,comment"
    ",guess,why,status,loopable,name_instrument,name_mood,name_note,name_vs_pitch,name_key,name_bpm"
    ",pitch_kind,pitch_note,pitch_cents,pitch_hz,voiced,stability_cents,key,key_r,key_margin"
    ",duration_s,channels,sample_rate,peak_db,rms_db,active_rms_db,clipped"
    ",lead_ms,tail_ms,envelope,attack_ms,decay20_ms,sustain_db,head_tail_db"
    ",centroid_hz,noisiness,low_share,onsets,onset_rate,bpm,beat_strength,note_count,notes,pitch_classes";

typedef std::map<std::string, std::map<std::string, std::string>> PreviousRows;    //file -> column -> value

//Every row of the catalog as it stands, by column name - so it reads back whatever order and
//separator it was last saved with.
PreviousRows ReadPreviousCatalog(const fs::path& csv){
    PreviousRows out;
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
        if (line.empty()) continue;
        const std::vector<std::string> row = CsvSplit(line, sep);
        std::map<std::string, std::string> cells;
        for (size_t c = 0; c < header.size() && c < row.size(); c++) cells[header[c]] = row[c];
        const std::string file = cells["file"];
        if (!file.empty()) out[file] = cells;
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
    printf("  spectrum    centroid %.0f Hz, noisiness %.3f, under 200 Hz %.0f%%\n", a.centroid_hz, a.noisiness, a.low_share * 100);
    printf("  rhythm      %d onsets (%.1f/s), pulse %.0f bpm at strength %.2f\n", a.onsets, a.onset_rate, a.bpm, a.beat_strength);
    if (!a.onset_s.empty()){
        printf("  onsets     ");
        for (size_t i = 0; i < a.onset_s.size() && i < 40; i++){
            printf(" %.2fs+%.0f", a.onset_s[i], a.onset_rise_db[i]);
        }
        printf(a.onset_s.size() > 40 ? " ...\n" : "\n");
    }
    printf("  notes       %d: %s  [%s]\n", a.note_count, a.notes.c_str(), a.pitch_classes.c_str());
    if (a.pitch_kind != "none"){
        const int n = (int)std::lround(a.pitch_midi);
        printf("  pitch       %s: %s %+.0f c (%.2f Hz), %.0f%% voiced, +-%.0f c\n", a.pitch_kind.c_str(), NoteName(n).c_str(),
               (a.pitch_midi - n) * 100, a.pitch_hz, a.voiced * 100, a.stability_cents);
    }
    else printf("  pitch       none (%.0f%% voiced)\n", a.voiced * 100);
    printf("  key         %s (r %.2f, margin %.2f)\n", a.key.c_str(), a.key_r, a.key_margin);
    printf("  name says   instrument '%s', mood '%s', note '%s', key '%s', bpm %d\n", h.instruments.c_str(), h.moods.c_str(),
           h.note.c_str(), h.key.c_str(), h.bpm);
    printf("  loopable    %s\n", Loopable(a) ? "yes" : "no");
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

/*
    Catalogues every audio file under root. With f_only_new, only the files the catalog has no row
    for are measured, and every existing row - missing files included - is copied across as it
    stands: the quick scan after dropping a few files into unsorted/, which leaves the rest of the
    library alone. The full scan is the one to run after a heuristic changes.
*/
int ScanFolder(const fs::path& root, bool f_only_new){
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
    const PreviousRows previous = ReadPreviousCatalog(csv);

    std::vector<Row> rows;
    std::vector<std::string> kept;          //--new: files whose old row is copied, not measured
    std::map<std::string, int> counts;
    for (const fs::path& p : files){
        Row r;
        r.file = fs::relative(p, root, ec).generic_u8string();
        if (f_only_new && previous.count(r.file)){
            kept.push_back(r.file);
            continue;
        }
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
    out << kColumns << "\n";

    //Old rows first when only the new files were measured: in a --new scan that is every file
    //already catalogued, on disk or not, re-laid in this version's column order.
    if (f_only_new){
        const std::vector<std::string> columns = CsvSplit(kColumns, ',');
        for (const auto& old : previous){
            for (size_t c = 0; c < columns.size(); c++){
                auto v = old.second.find(columns[c]);
                out << (c ? "," : "") << CsvField(v != old.second.end() ? v->second : "");
            }
            out << "\n";
        }
    }

    for (const Row& r : rows){
        const SampleAnalysis& a = r.a;
        out << CsvField(r.file);
        auto was = previous.find(r.file);
        for (int h = 0; h < kNumHuman; h++){
            std::string v;
            if (was != previous.end()){ auto x = was->second.find(kHumanColumns[h]); if (x != was->second.end()) v = x->second; }
            out << "," << CsvField(v);
        }
        out << "," << r.guess << "," << CsvField(r.why) << "," << CsvField(r.status)
            << "," << (a.f_ok && Loopable(a) ? "yes" : "")
            << "," << r.hints.instruments << "," << r.hints.moods << "," << CsvField(r.hints.note);

        const bool f_single = a.f_ok && a.pitch_kind == "single";
        const int pitch_n = f_single ? (int)std::lround(a.pitch_midi) : 0;
        //A difference of exactly 12 is nearly always the other octave convention (C3 as middle C,
        //which Yamaha and Ableton use), not a mislabelled file.
        out << "," << (f_single && r.hints.note_midi >= 0 ? std::to_string(pitch_n - r.hints.note_midi) : "");
        out << "," << CsvField(r.hints.key) << "," << (r.hints.bpm ? std::to_string(r.hints.bpm) : "");

        if (!a.f_ok){
            out << std::string(33, ',') << "\n";
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
            << "," << Num(a.centroid_hz, 0) << "," << Num(a.noisiness, 3) << "," << Num(a.low_share, 2)
            << "," << a.onsets << "," << Num(a.onset_rate, 2)
            << "," << (a.beat_strength > 0 ? Num(a.bpm, 1) : "") << "," << Num(a.beat_strength, 2)
            << "," << a.note_count << "," << CsvField(a.notes) << "," << CsvField(a.pitch_classes)
            << "\n";
    }

    //MusicLibrary shows this line, up to " files".
    if (f_only_new) printf("\n%zu new, %zu files -> %s\n", rows.size(), rows.size() + previous.size(), csv.u8string().c_str());
    else printf("\n%zu files -> %s\n", rows.size(), csv.u8string().c_str());
    for (const auto& c : counts) printf("  %-15s %d\n", c.first.c_str(), c.second);
    return 0;
}

} //namespace

int main(int argc, char** argv){
    if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")){
        printf("samplescan [folder]         catalogue every audio file under folder (default ../../apps/music/samples)\n"
               "samplescan --new [folder]   measure only the files the catalog has no row for; keep every other row\n"
               "samplescan --file <audio>   measure one file and print every field\n"
               "samplescan --export <audio> <out.wav>        write it as the PCM16 wav the engine loads\n"
               "samplescan --export-trim <audio> <out.wav>   the same, with the silence cut off both ends\n");
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--file") return ScanOne(fs::u8path(argv[2]));
    if (argc >= 4 && (std::string(argv[1]) == "--export" || std::string(argv[1]) == "--export-trim")){
        std::string error;
        printf("%s -> %s\n", argv[2], argv[3]);
        if (!ExportWav(fs::u8path(argv[2]).wstring(), fs::u8path(argv[3]).wstring(), std::string(argv[1]) == "--export-trim", error)){
            fprintf(stderr, "samplescan: %s: %s\n", argv[2], error.c_str());
            return 1;
        }
        return 0;
    }
    const char* kDefault = "../../apps/music/samples";
    if (argc >= 2 && std::string(argv[1]) == "--new") return ScanFolder(fs::u8path(argc >= 3 ? argv[2] : kDefault), true);
    return ScanFolder(fs::u8path(argc >= 2 ? argv[1] : kDefault), false);
}
