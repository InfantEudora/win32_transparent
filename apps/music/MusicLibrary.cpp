#include "MusicLibrary.h"

#include "Debug.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

#include <windows.h>

static Debugger* debug = new Debugger("MusicLibrary", DEBUG_ALL);

namespace fs = std::filesystem;

namespace {

//What samplescan catalogues: the three it decodes, and the ones it lists as unsupported so they
//can be seen and converted. A file with any other extension is not a sample.
const char* kAudioExtensions[] = {".wav", ".flac", ".mp3", ".ogg", ".aif", ".aiff", ".m4a", ".aac", ".opus", ".wma"};

const char* kHumanColumns[] = {"category", "instrument", "root", "comment"};

std::string Lower(std::string s){
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

//The same reader samplescan has, for the same reason: Excel in a Dutch locale saves with ';'.
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

std::string CsvField(const std::string& s){
    if (s.find_first_of(",;\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s){ if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

std::wstring Widen(const std::string& s){
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

} //namespace

std::string MusicLibrary::Entry::Get(const std::string& column) const{
    auto it = fields.find(column);
    return it == fields.end() ? std::string() : it->second;
}

const std::vector<std::string>& MusicLibrary::Categories(){
    static const std::vector<std::string> list = {
        "note_struck", "note_sustained", "drone", "hum", "texture", "hit", "rhythm", "phrase", "mix", "stinger"
    };
    return list;
}

const char* MusicLibrary::StateName(State s){
    switch (s){
        case NOT_SCANNED:  return "not scanned";
        case UNCLASSIFIED: return "unclassified";
        case CLASSIFIED:   return "classified";
        case MISSING:      return "missing";
    }
    return "?";
}

MusicLibrary::~MusicLibrary(){
    if (scan_thread.joinable()) scan_thread.join();
}

void MusicLibrary::SetPaths(const std::string& samples, const std::string& tool, const std::string& sound){
    samples_dir = samples;
    samplescan_exe = tool;
    sound_dir = sound;
}

bool MusicLibrary::Refresh(std::string& error){
    const fs::path root = fs::u8path(samples_dir);
    std::vector<std::string> new_header;
    std::vector<Entry> rows;

    std::ifstream in(root / "catalog.csv", std::ios::binary);
    if (in){
        std::string line;
        char sep = ',';
        while (std::getline(in, line)){
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);     //BOM
            if (line.rfind("sep=", 0) == 0) continue;
            if (new_header.empty()){
                sep = std::count(line.begin(), line.end(), ';') > std::count(line.begin(), line.end(), ',') ? ';' : ',';
                new_header = CsvSplit(line, sep);
                continue;
            }
            if (line.empty()) continue;
            const std::vector<std::string> cells = CsvSplit(line, sep);
            Entry e;
            for (size_t c = 0; c < new_header.size() && c < cells.size(); c++) e.fields[new_header[c]] = cells[c];
            e.file = e.Get("file");
            if (e.file.empty()) continue;
            rows.push_back(e);
        }
    }

    //What is really on disk. A catalogued file that is gone is MISSING; one on disk that the
    //catalog has never seen is NOT_SCANNED - the new arrivals the panel exists to show.
    std::set<std::string> on_disk;
    std::error_code ec;
    if (!fs::is_directory(root, ec)){
        error = "no samples folder at " + samples_dir;
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)){
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().u8string());
        bool f_audio = false;
        for (const char* a : kAudioExtensions) f_audio |= (ext == a);
        if (f_audio) on_disk.insert(fs::relative(it->path(), root, ec).generic_u8string());
    }
    std::set<std::string> catalogued;
    for (Entry& e : rows){
        catalogued.insert(e.file);
        if (!on_disk.count(e.file)) e.state = MISSING;
        else e.state = e.Get("category").empty() ? UNCLASSIFIED : CLASSIFIED;
    }
    for (const std::string& f : on_disk){
        if (catalogued.count(f)) continue;
        Entry e;
        e.file = f;
        e.state = NOT_SCANNED;
        e.fields["file"] = f;
        rows.push_back(e);
    }
    //New arrivals first, then what still wants a category, then the rest - each by name.
    std::stable_sort(rows.begin(), rows.end(), [](const Entry& a, const Entry& b){
        if (a.state != b.state) return a.state < b.state;
        return a.file < b.file;
    });

    //Which of them are exported as what. No exports.csv is not an error: nothing is exported yet.
    std::vector<ExportDef> new_exports;
    std::ifstream ex(root / "exports.csv", std::ios::binary);
    if (ex){
        std::string line;
        bool f_header = true;
        while (std::getline(ex, line)){
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
            if (line.empty() || line == "\r" || line.rfind("sep=", 0) == 0) continue;
            const char sep = std::count(line.begin(), line.end(), ';') > std::count(line.begin(), line.end(), ',') ? ';' : ',';
            const std::vector<std::string> cells = CsvSplit(line, sep);
            if (f_header){ f_header = false; continue; }
            if (cells.size() < 2 || cells[0].empty()) continue;
            ExportDef d;
            d.name = cells[0];
            d.file = cells[1];
            d.f_trim = cells.size() > 2 && Lower(cells[2]) == "yes";
            d.f_on_disk = fs::is_regular_file(fs::u8path(sound_dir) / fs::u8path(d.name + ".wav"), ec);
            new_exports.push_back(d);
        }
    }
    for (Entry& e : rows){
        for (const ExportDef& d : new_exports) if (d.file == e.file) e.exported_as.push_back(d.name);
    }

    std::lock_guard<std::mutex> lock(mutex);
    header = new_header;
    entries = rows;
    exports = new_exports;
    return true;
}

std::vector<MusicLibrary::ExportDef> MusicLibrary::Exports(){
    std::lock_guard<std::mutex> lock(mutex);
    return exports;
}

std::vector<MusicLibrary::Entry> MusicLibrary::Snapshot(){
    std::lock_guard<std::mutex> lock(mutex);
    return entries;
}

std::string MusicLibrary::Message(){
    std::lock_guard<std::mutex> lock(mutex);
    return message;
}

//--- scanning ------------------------------------------------------------------------------

bool MusicLibrary::StartScan(bool f_only_new){
    if (f_scanning.exchange(true)) return false;
    if (scan_thread.joinable()) scan_thread.join();
    scan_thread = std::thread([this, f_only_new](){
        const auto t0 = std::chrono::steady_clock::now();
        std::string output;
        const int code = f_only_new ? RunTool({"--new", samples_dir}, output) : RunTool({samples_dir}, output);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::string error;
        Refresh(error);

        //samplescan ends with "N files -> <catalog>" ("K new, N files" for --new), which is the
        //line worth showing.
        std::string summary;
        const size_t at = output.rfind(" files -> ");
        if (at != std::string::npos){
            const size_t nl = output.rfind('\n', at);
            const size_t start = (nl == std::string::npos) ? 0 : nl + 1;
            summary = output.substr(start, at + 6 - start);         //"20 files", without the path
        }
        if (!error.empty()) summary += " (" + error + ")";
        char buf[512];
        if (code == 0) snprintf(buf, sizeof buf, "Scanned in %.1f s: %s", took, summary.c_str());
        else if (code < 0) snprintf(buf, sizeof buf, "Could not run %s - build tools/samplescan first", samplescan_exe.c_str());
        else snprintf(buf, sizeof buf, "samplescan failed (exit %d)", code);
        {
            std::lock_guard<std::mutex> lock(mutex);
            message = buf;
        }
        debug->Info("%s\n", buf);
        f_scanning = false;
    });
    return true;
}

bool MusicLibrary::WaitForScan(int timeout_ms){
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (f_scanning){
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

int MusicLibrary::RunTool(const std::vector<std::string>& args, std::string& output){
    //Every argument quoted: this repo lives under "Mijn Documenten", so every path has a space.
    std::wstring cmd = L"\"" + Widen(samplescan_exe) + L"\"";
    for (const std::string& a : args) cmd += L" \"" + Widen(a) + L"\"";

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE read_end = NULL, write_end = NULL;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return -1;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(L'\0');
    //CREATE_NO_WINDOW: a console flashing up over the game on every scan is exactly the kind of
    //interruption --minimized exists to prevent.
    const BOOL f_started = CreateProcessW(NULL, line.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(write_end);
    if (!f_started){
        CloseHandle(read_end);
        return -1;
    }
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buf, sizeof buf, &got, NULL) && got > 0) output.append(buf, got);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

//--- classifying ---------------------------------------------------------------------------

bool MusicLibrary::Classify(const std::string& file, const std::map<std::string, std::string>& human, std::string& error){
    if (f_scanning){
        error = "a scan is running - classify once it has finished";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    Entry* e = nullptr;
    for (Entry& x : entries) if (x.file == file) e = &x;
    if (!e){
        error = "no such file in the library: " + file;
        return false;
    }
    if (e->state == NOT_SCANNED){
        error = file + " has not been scanned yet - scan first, so it has a row to classify";
        return false;
    }
    for (const auto& kv : human){
        bool f_known = false;
        for (const char* c : kHumanColumns) f_known |= (kv.first == c);
        if (!f_known){
            error = "only category, instrument, root and comment are set by hand - not " + kv.first;
            return false;
        }
        e->fields[kv.first] = kv.second;
    }
    if (e->state != MISSING) e->state = e->Get("category").empty() ? UNCLASSIFIED : CLASSIFIED;
    if (!WriteCatalog(error)) return false;
    message = "Saved " + file;
    return true;
}

bool MusicLibrary::WriteCatalog(std::string& error){
    if (header.empty()){
        error = "the catalog has no header - scan first";
        return false;
    }
    //Written whole, to a temporary beside it and then moved over it, so a crash halfway through
    //leaves the old catalog rather than half of a new one.
    const fs::path root = fs::u8path(samples_dir);
    const fs::path tmp = root / "catalog.csv.tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out){
            error = "cannot write the catalog";
            return false;
        }
        for (size_t c = 0; c < header.size(); c++) out << (c ? "," : "") << CsvField(header[c]);
        out << "\n";
        for (const Entry& e : entries){
            if (e.state == NOT_SCANNED) continue;       //samplescan adds those, with their measurements
            for (size_t c = 0; c < header.size(); c++) out << (c ? "," : "") << CsvField(e.Get(header[c]));
            out << "\n";
        }
    }
    std::error_code ec;
    fs::rename(tmp, root / "catalog.csv", ec);
    if (ec){
        error = "cannot replace the catalog: " + ec.message();
        return false;
    }
    return true;
}

bool MusicLibrary::Export(const std::string& file, const std::string& out_path, bool f_trim, std::string& error){
    std::string output;
    const std::string in_path = (fs::u8path(samples_dir) / fs::u8path(file)).u8string();
    const int code = RunTool({f_trim ? "--export-trim" : "--export", in_path, out_path}, output);
    if (code != 0){
        error = code < 0 ? "could not run samplescan - build tools/samplescan first" : "samplescan could not export " + file;
        return false;
    }
    return true;
}

//--- exporting to the score's folder ---------------------------------------------------------

bool MusicLibrary::ValidExportName(const std::string& name){
    //It becomes a file name and an asset name, and `make samples` reads it back with a shell loop,
    //so: letters, digits, '_' and '-', and nothing that is a path.
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    return true;
}

std::string MusicLibrary::SuggestExportName(const Entry& e){
    if (!e.exported_as.empty()) return e.exported_as[0];
    //The instrument as typed, else as the name says, else the role - first word only, since
    //"flute+transition" names the thing and then says what it is doing.
    std::string what = e.Get("instrument");
    if (what.empty()) what = e.Get("name_instrument");
    if (what.empty()) what = e.Get("category");
    if (what.empty()) what = e.Get("guess");
    what = what.substr(0, what.find_first_of("+ ,"));
    std::string name;
    for (char c : Lower(what)) name += std::isalnum((unsigned char)c) ? c : '_';
    //"F#3" -> "Fs3", the way kalimba_Fs3.wav already is: '#' does not belong in a file name.
    std::string root;
    for (char c : e.Get("root")){
        if (c == '#') root += 's';
        else if (std::isalnum((unsigned char)c)) root += c;
    }
    if (!root.empty()) name += (name.empty() ? "" : "_") + root;
    return name.empty() ? "sample" : name;
}

bool MusicLibrary::SuggestTrim(const Entry& e){
    //A loop's seam is its first and last sample, so a loop is exported whole. Everything else is
    //trimmed: silence in front of a note is latency on every note, and silence after a bed is a
    //gap in it every time round.
    return e.Get("loopable") != "yes";
}

bool MusicLibrary::ExportToSound(const std::string& file, const std::string& name, bool f_trim, bool f_replace, std::string& error){
    if (!ValidExportName(name)){
        error = "'" + name + "' is not a usable name - letters, digits, _ and - only";
        return false;
    }
    //`make samples` splits exports.csv on ',' and ';' with no quoting, so a file whose name has
    //either could be exported here and never again.
    if (file.find_first_of(",;\"") != std::string::npos){
        error = "rename " + file + " first: a comma, semicolon or quote in it breaks exports.csv";
        return false;
    }
    std::lock_guard<std::mutex> export_lock(export_mutex);
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool f_known = false;
        for (const Entry& e : entries) f_known |= (e.file == file && e.state != MISSING);
        if (!f_known){
            error = "no such file in the library: " + file;
            return false;
        }
        for (const ExportDef& d : exports){
            if (d.name == name && d.file != file && !f_replace){
                error = "music/sounds/" + name + ".wav is already " + d.file + " - pick another name, or replace it";
                return false;
            }
        }
    }

    std::error_code ec;
    fs::create_directories(fs::u8path(sound_dir), ec);
    const std::string out_path = (fs::u8path(sound_dir) / fs::u8path(name + ".wav")).u8string();
    const std::string in_path = (fs::u8path(samples_dir) / fs::u8path(file)).u8string();
    std::string output;
    const int code = RunTool({f_trim ? "--export-trim" : "--export", in_path, out_path}, output);
    if (code != 0){
        error = code < 0 ? "could not run samplescan - build tools/samplescan first" : "samplescan could not export " + file;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    //One row per name: a replace re-points it, a re-export updates the trim.
    ExportDef d;
    d.name = name;
    d.file = file;
    d.f_trim = f_trim;
    d.f_on_disk = true;
    bool f_found = false;
    for (ExportDef& x : exports) if (x.name == name){ x = d; f_found = true; }
    if (!f_found) exports.push_back(d);
    for (Entry& e : entries){
        e.exported_as.erase(std::remove(e.exported_as.begin(), e.exported_as.end(), name), e.exported_as.end());
        if (e.file == file) e.exported_as.push_back(name);
    }
    if (!WriteExports(error)) return false;

    //samplescan's last line is "  2 ch, 44100 Hz, 3.20 s (trimmed)" - the part worth showing.
    std::string summary = output;
    while (!summary.empty() && (summary.back() == '\n' || summary.back() == '\r')) summary.pop_back();
    summary = summary.substr(summary.rfind('\n') == std::string::npos ? 0 : summary.rfind('\n') + 1);
    summary.erase(0, summary.find_first_not_of(' '));
    message = "Exported music/sounds/" + name + ".wav: " + summary;
    debug->Info("%s\n", message.c_str());
    return true;
}

int MusicLibrary::ExportMissing(std::string& error){
    int written = 0;
    for (const ExportDef& d : Exports()){
        if (d.f_on_disk) continue;
        if (!ExportToSound(d.file, d.name, d.f_trim, false, error)) return written;
        written++;
    }
    std::lock_guard<std::mutex> lock(mutex);
    message = written ? "Exported " + std::to_string(written) + " missing wav" + (written == 1 ? "" : "s") : "Every export is on disk";
    return written;
}

bool MusicLibrary::WriteExports(std::string& error){
    //As the catalog: whole, to a temporary, then moved over the old one.
    const fs::path root = fs::u8path(samples_dir);
    const fs::path tmp = root / "exports.csv.tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out){
            error = "cannot write exports.csv";
            return false;
        }
        out << "name,file,trim\n";
        for (const ExportDef& d : exports) out << d.name << "," << d.file << "," << (d.f_trim ? "yes" : "no") << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, root / "exports.csv", ec);
    if (ec){
        error = "cannot replace exports.csv: " + ec.message();
        return false;
    }
    return true;
}
