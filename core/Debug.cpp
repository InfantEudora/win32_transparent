#include "Debug.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#ifdef _WIN32
#include <share.h>
#else
#include <sys/file.h>   //flock - see OpenExclusive
#endif

std::mutex Debugger::mutex;
bool Debugger::setup_done = false;
bool Debugger::enable_buffering = false;
bool Debugger::enable_console = true;

#define DEBUG_LOG_FILE_MOST (32u * 1024u * 1024u)    //a log file's bytes at most: a run logging in a loop fills no disk

// The log file (Debug.h, OpenLogFile). Written only with Debugger::mutex held, as everything that emits is.
static FILE *g_log_file = NULL;
static std::string g_log_path;
static size_t g_log_bytes = 0;

static const char *LevelText(debug_t type) {
    switch (type) {
        case DEBUG_TRACE: return "trace";
        case DEBUG_DEBUG: return "debug";
        case DEBUG_INFO:  return " info";
        case DEBUG_OK:    return "  ok ";
        case DEBUG_WARN:  return " warn";
        case DEBUG_ERROR: return " err ";
        case DEBUG_FATAL: return "fatal";
        default:          return " -- ";
    }
}

static void LogFileLine(debug_t type, const char *name, const char *message) {
    if (!g_log_file || g_log_bytes >= DEBUG_LOG_FILE_MOST) {
        return;
    }
    char stamp[16] = "";
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local) {
        strftime(stamp, sizeof(stamp), "%H:%M:%S", local);
    }
    int n = fprintf(g_log_file, "%s [%s] %20s : %s", stamp, LevelText(type), name ? name : "", message);
    g_log_bytes += (n > 0) ? (size_t)n : 0;
    if (g_log_bytes >= DEBUG_LOG_FILE_MOST) {
        fprintf(g_log_file, "\n--- the log file is full: nothing more is written to it ---\n");
    }
    fflush(g_log_file);
}

// Held against other writers, so a second copy of the app running from the same folder can tell it is in use.
static FILE *OpenExclusive(const std::string &file, const char *mode) {
#ifdef _WIN32
    return _fsopen(file.c_str(), mode, _SH_DENYWR);
#else
    // POSIX has no share modes, so the same signal is an advisory lock that every copy of the app takes and
    // tests. Safe for the "w" open too: OpenLogFile probes with "r+b" first, so it never truncates a held file.
    FILE *f = fopen(file.c_str(), mode);
    if (f && flock(fileno(f), LOCK_EX | LOCK_NB) != 0) {
        fclose(f);
        return NULL;
    }
    return f;
#endif
}

bool Debugger::OpenLogFile(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_path.clear();
    g_log_bytes = 0;
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    bool f_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    std::string stem = f_ext ? path.substr(0, dot) : path;
    std::string ext = f_ext ? path.substr(dot) : std::string();
    // Another copy of the app running keeps its own: chasm.log in use -> chasm_2.log, and so on.
    for (int i = 1; i <= 9 && !g_log_file; i++) {
        std::string base = (i == 1) ? stem : stem + "_" + std::to_string(i);
        std::string file = base + ext;
        FILE *exists = fopen(file.c_str(), "rb");
        if (exists) {
            fclose(exists);
            FILE *probe = OpenExclusive(file, "r+b");
            if (!probe) {
                continue;
            }
            fclose(probe);
            // The last run's, kept: chasm.log -> chasm_previous.log.
            std::string previous = base + "_previous" + ext;
            remove(previous.c_str());
            rename(file.c_str(), previous.c_str());
        }
        g_log_file = OpenExclusive(file, "w");
        if (g_log_file) {
            g_log_path = file;
        }
    }
    return g_log_file != NULL;
}

std::string Debugger::LogFilePath() {
    std::lock_guard<std::mutex> lock(mutex);
    return g_log_path;
}

// Returns a static map handle.
std::map<std::string, Debugger *> *Debugger::GetHandles() {
    static std::map<std::string, Debugger *> map;
    return &map;
}

Debugger *Debugger::FindHandle(std::string name) {
    std::map<std::string, Debugger *> *handles = GetHandles();
    if (handles->count(name)) {
        return (*handles)[name];
    }
    return NULL;
}

void Debugger::SetLevel(std::string name, int level) {
    std::map<std::string, Debugger *> *handles = GetHandles();
    auto it = handles->find(name);
    if (it == handles->end()) {
        return;
    }
    Debugger *d = (*handles)[name];
    if (d) {
        d->SetLevel(level);
    }
}

void Debugger::ListHandles(void) {
    std::map<std::string, Debugger *> *handles = GetHandles();
    printf("Debugger Handles:\n");
    for (std::pair<std::string, Debugger *> kv : *handles) {
        printf("%s, ", kv.first.c_str());
    }
    printf("\n");
}

void Debugger::SetLevel(debug_t type) {
    level = type - 1;
}

Debugger::Debugger(const char *name) {
    Start((char *)name);
    // Default, suppress all but warn and error:
    SetLevel(DEBUG_WARN);
}

Debugger::Debugger(const char *name, int level) {
    Start((char *)name);
    SetLevel(level);
}

Debugger::Debugger(char *name) {
    Start(name);
}

Debugger::~Debugger() {
}

void Debugger::Start(char *name) {
    if (!setup_done && enable_console) {
        SetupConsole();
    }
    this->name = name;
    /*
        No "Debugger [x] started" line here any more, and it is not coming back.

        Every Debugger in this project is a file-scope global, and engine.mk compiles ALL of
        core/ into every app, so each one announced roughly fifty subsystems before main() -
        GLTFLoader, Physics, SoundSystem and forty-odd others - in apps that never call any of
        them. apps/ui is a bare ImGui harness and printed the lot.

        It could not even be turned off where it mattered: the line was emitted from here,
        BEFORE the constructor's SetLevel() runs, so a Debugger built at DEBUG_NONE announced
        itself just as loudly. tools/lockd had to resort to a priority-101 static constructor
        to silence three of them for its --list output; that hack is gone with this line.

        A subsystem is visible in the log the moment it logs something, which is the only
        moment its existence is interesting. Debugger::ListHandles() still enumerates them all
        on demand.
    */
    std::string n = name;
    std::map<std::string, Debugger *> *handles = GetHandles();

    handles->insert(std::pair<std::string, Debugger *>(n, this));
}

void Debugger::Trace(const char *format, ...) {
    if (level >= DEBUG_TRACE) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_TRACE, format, arglist);
    va_end(arglist);
}

void Debugger::Debug(const char *format, ...) {
    if (level >= DEBUG_DEBUG) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_DEBUG, format, arglist);
    va_end(arglist);
}

void Debugger::Info(const char *format, ...) {
    if (level >= DEBUG_INFO) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_INFO, format, arglist);
    va_end(arglist);
}

void Debugger::Ok(const char *format, ...) {
    if (level >= DEBUG_OK) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_OK, format, arglist);
    va_end(arglist);
}

void Debugger::Warn(const char *format, ...) {
    if (level >= DEBUG_WARN) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_WARN, format, arglist);
    va_end(arglist);
}

void Debugger::Err(const char *format, ...) {
    if (level >= DEBUG_ERROR) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_ERROR, format, arglist);
    va_end(arglist);
}

// Fatal always prints, then terminates -- no level check, but still needs
// the lock like every other emitting path (the original left this one
// (and both PrintLine overloads below) unlocked, a real race against any
// concurrent Trace/Debug/Info/Ok/Warn/Err call on another thread).
void Debugger::Fatal(const char *format, ...) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        va_list arglist;
        va_start(arglist, format);
        PrintLineva(DEBUG_FATAL, format, arglist);
        va_end(arglist);
    }
    Flush();
    exit(1);
}

// PrintLine always outputs no matter what level
void Debugger::PrintLine(const char *format, ...) {
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_INFO, format, arglist);
    va_end(arglist);
}

void Debugger::PrintLine(debug_t type, const char *format, ...) {
    /*
        (int64_t)type, not type. `level` is an int and debug_t is uint64_t, so the bare
        comparison converted level to unsigned - and SetLevel(debug_t) computes
        `level = type - 1`, which for DEBUG_ALL (0) leaves level at -1. As unsigned that is
        0xFFFFFFFFFFFFFFFF, so the test was always true and this overload silently printed
        NOTHING on any debugger constructed with DEBUG_ALL - which is most of them.

        It never bit because nothing outside this file calls this overload; the level-named
        entry points (Info, Warn, ...) compare against the plain int macros and were always
        correct. This was the -Wsign-compare warning on this line, which was a real bug
        rather than noise.
    */
    if (level >= (int64_t)type) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(type, format, arglist);
    va_end(arglist);
}

// Formats into a local, bounded, per-call buffer -- never a shared static
// one -- so there's no cross-instance/cross-thread overflow hazard and no
// way to walk past the end of it. The original built the message with raw
// sprintf/vsprintf into a fixed 8192-byte buffer *shared by every Debugger
// instance*, only checking for overflow after the unbounded write already
// happened. vsnprintf can't overrun its destination by construction, and
// a local buffer means concurrent callers (even without the mutex above)
// can't corrupt each other's in-progress line.
void Debugger::PrintLineva(debug_t type, const char *format, va_list arglist) {
    if (!enable_console && !g_log_file) {
        return;
    }
    char message[4096];
    vsnprintf(message, sizeof(message), format, arglist);
    LogFileLine(type, name, message);
    if (enable_console) {
        EmitLine(type, name, message);
    }
}
