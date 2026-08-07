#include "Debug.h"
#include <cstdio>
#include <cstdlib>

std::mutex Debugger::mutex;
bool Debugger::setup_done = false;
bool Debugger::enable_buffering = false;
bool Debugger::enable_console = true;

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
    PrintLine(DEBUG_INFO, "Debugger [%s] started\n", name);
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
    if (level >= type) {
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
    if (!enable_console) {
        return;
    }
    char message[4096];
    vsnprintf(message, sizeof(message), format, arglist);
    EmitLine(type, name, message);
}
