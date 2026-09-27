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

void MusicLibrary::SetPaths(const std::string& samples, const std::string& tool){
    samples_dir = samples;
    samplescan_exe = tool;
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

    std::lock_guard<std::mutex> lock(mutex);
    header = new_header;
    entries = rows;
    return true;
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
