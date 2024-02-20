#include "Debug.h"

#define NEED_COLOR_FIX

// Buffer sizes
#define DEBUG_IO_BUFFERSIZE 8192
#define DEBUG_IO_HIGHWM 4096

char Debugger::buffer[DEBUG_IO_BUFFERSIZE] = {0};
int Debugger::boffset = 0;
int Debugger::lines_buffered = 0;
// HANDLE Debugger::writemutex = NULL;

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
    return (*handles)[name];
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

void Debugger::SetupConsole(void) {
#ifdef NEED_COLOR_FIX
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(handle, mode);
        }
    }
#endif
    setup_done = true;
}

Debugger::Debugger(const char *name) {
    Start((char *)name);
    // Default, suppress all but warn and error:
    SetLevel(DEBUG_WARN);
}

Debugger::Debugger(const char *name, int level) {
    Start((char *)name);
    // Default, suppress all but warn and error:
    SetLevel(level);
}

Debugger::Debugger(char *name) {
    Start(name);
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
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_TRACE, format, arglist);
    va_end(arglist);
}

void Debugger::Debug(const char *format, ...) {
    if (level >= DEBUG_DEBUG) {
        return;
    }
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_DEBUG, format, arglist);
    va_end(arglist);
}

void Debugger::Info(const char *format, ...) {
    if (level >= DEBUG_INFO) {
        return;
    }
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_INFO, format, arglist);
    va_end(arglist);
}

void Debugger::Ok(const char *format, ...) {
    if (level >= DEBUG_OK) {
        return;
    }
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_OK, format, arglist);
    va_end(arglist);
}

void Debugger::Warn(const char *format, ...) {
    if (level >= DEBUG_WARN) {
        return;
    }
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_WARN, format, arglist);
    va_end(arglist);
}

void Debugger::Err(const char *format, ...) {
    if (level >= DEBUG_ERROR) {
        return;
    }
    std::lock_guard lock(mutex);
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_ERROR, format, arglist);
    va_end(arglist);
}

void Debugger::Fatal(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_FATAL, format, arglist);
    va_end(arglist);
    Flush();
    exit(1);
}

// PrintLine always outputs no matter what level
void Debugger::PrintLine(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(DEBUG_INFO, format, arglist);
    va_end(arglist);
}

void Debugger::PrintLine(debug_t type, const char *format, ...) {
    if (level >= type) {
        return;
    }
    va_list arglist;
    va_start(arglist, format);
    PrintLineva(type, format, arglist);
    va_end(arglist);
}

void Debugger::color_tobuffer(int color) {

    switch (color) {
        case CLR_BLACK:
            boffset += sprintf(&buffer[boffset], "\x1b[30m");
            break;
        case CLR_RED:
            boffset += sprintf(&buffer[boffset], "\x1b[31m");
            break;
        case CLR_GREEN:
            boffset += sprintf(&buffer[boffset], "\x1b[32m");
            break;
        case CLR_LIGHTGREEN:
            boffset += sprintf(&buffer[boffset], "\x1b[92m");
            break;
        case CLR_YELLOW:
            boffset += sprintf(&buffer[boffset], "\x1b[33m");
            break;
        case CLR_BLUE:
            boffset += sprintf(&buffer[boffset], "\x1b[34m");
            break;
        case CLR_MAGENTA:
            boffset += sprintf(&buffer[boffset], "\x1b[35m");
            break;
        case CLR_CYAN:
            boffset += sprintf(&buffer[boffset], "\x1b[36m");
            break;
        case CLR_LIGHTCYAN:
            boffset += sprintf(&buffer[boffset], "\x1b[96m");
            break;
        case CLR_GREY: // Default set by Cancel.
            boffset += sprintf(&buffer[boffset], "\x1b[90m");
            break;
        case CLR_WHITE:
            boffset += sprintf(&buffer[boffset], "\x1b[97m");
            break;
        case CLR_CANCEL:
            boffset += sprintf(&buffer[boffset], "\x1b[39m");
            break;
        default:
            break;
    }
}

void Debugger::PrintLineva(debug_t type, const char *format, va_list arglist) {
    if (!enable_console) {
        return;
    }
    buffer[boffset++] = '[';

    if (type == DEBUG_TRACE) {
        color_tobuffer(CLR_GREY);
        boffset += sprintf(&buffer[boffset], "trace");
    } else if (type == DEBUG_DEBUG) {
        color_tobuffer(CLR_WHITE);
        boffset += sprintf(&buffer[boffset], "debug");
    } else if (type == DEBUG_INFO) {
        color_tobuffer(CLR_CYAN);
        boffset += sprintf(&buffer[boffset], " info");
    } else if (type == DEBUG_OK) {
        color_tobuffer(CLR_GREEN);
        boffset += sprintf(&buffer[boffset], "  ok ");
    } else if (type == DEBUG_WARN) {
        color_tobuffer(CLR_YELLOW);
        boffset += sprintf(&buffer[boffset], " warn");
    } else if (type == DEBUG_ERROR) {
        color_tobuffer(CLR_RED);
        boffset += sprintf(&buffer[boffset], " err ");
    } else if (type == DEBUG_FATAL) {
        color_tobuffer(CLR_RED);
        boffset += sprintf(&buffer[boffset], "fatal");
    } else {
        boffset += sprintf(&buffer[boffset], " -- ");
    }
    color_tobuffer(CLR_WHITE);
    boffset += sprintf(&buffer[boffset], "] %20s : ", name);
    color_tobuffer(CLR_CANCEL);

    boffset += vsprintf(&buffer[boffset], format, arglist);
    if (boffset > (DEBUG_IO_BUFFERSIZE - 1)) {
        printf("Wrote past buffer\n");
        exit(1);
    }
    lines_buffered++;
    if (enable_buffering && (boffset > DEBUG_IO_HIGHWM)) {
        Flush();
    } else if (!enable_buffering) {
        Flush();
    }
}

// This should be called once a while.
void Debugger::Flush() {
    if (boffset) {
        fputs(buffer, stdout);
        memset(buffer, 0, boffset + 1);
        boffset = 0;
        lines_buffered = 0;
    }
}

Debugger::~Debugger() {
}
