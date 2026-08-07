// Win32 console backend for Debugger: ANSI-colored output, matching the
// original behavior. Buffer handling is confined to this file (not class
// members), and every write goes through buf_append(), which -- unlike the
// original's raw sprintf/vsprintf into a shared static buffer -- can never
// write past the end of it.
#include "Debug.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

#define DEBUG_IO_BUFFERSIZE 8192
#define DEBUG_IO_HIGHWM 4096

// EmitLine() only ever runs while Debugger::mutex is held (every public
// method that reaches PrintLineva takes it first), so this file-local
// buffer needs no locking of its own.
static char g_buffer[DEBUG_IO_BUFFERSIZE] = {0};
static int g_boffset = 0;

static void buf_append(const char *fmt, ...) {
    if (g_boffset >= (int)sizeof(g_buffer) - 1) {
        return; // buffer full; drop rather than overflow
    }
    int remaining = (int)sizeof(g_buffer) - g_boffset;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(&g_buffer[g_boffset], remaining, fmt, args);
    va_end(args);
    if (written > 0) {
        g_boffset += (written < remaining) ? written : (remaining - 1);
    }
}

static void color_tobuffer(int color) {
    switch (color) {
        case CLR_BLACK:      buf_append("\x1b[30m"); break;
        case CLR_RED:        buf_append("\x1b[31m"); break;
        case CLR_GREEN:      buf_append("\x1b[32m"); break;
        case CLR_LIGHTGREEN: buf_append("\x1b[92m"); break;
        case CLR_YELLOW:     buf_append("\x1b[33m"); break;
        case CLR_BLUE:       buf_append("\x1b[34m"); break;
        case CLR_MAGENTA:    buf_append("\x1b[35m"); break;
        case CLR_CYAN:       buf_append("\x1b[36m"); break;
        case CLR_LIGHTCYAN:  buf_append("\x1b[96m"); break;
        case CLR_GREY:       buf_append("\x1b[90m"); break;
        case CLR_WHITE:      buf_append("\x1b[97m"); break;
        case CLR_CANCEL:     buf_append("\x1b[39m"); break;
        default: break;
    }
}

void Debugger::SetupConsole() {
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(handle, mode);
        }
    }
    setup_done = true;
}

void Debugger::EmitLine(debug_t type, const char *name, const char *message) {
    buf_append("[");
    if (type == DEBUG_TRACE) {
        color_tobuffer(CLR_GREY);
        buf_append("trace");
    } else if (type == DEBUG_DEBUG) {
        color_tobuffer(CLR_WHITE);
        buf_append("debug");
    } else if (type == DEBUG_INFO) {
        color_tobuffer(CLR_CYAN);
        buf_append(" info");
    } else if (type == DEBUG_OK) {
        color_tobuffer(CLR_GREEN);
        buf_append("  ok ");
    } else if (type == DEBUG_WARN) {
        color_tobuffer(CLR_YELLOW);
        buf_append(" warn");
    } else if (type == DEBUG_ERROR) {
        color_tobuffer(CLR_RED);
        buf_append(" err ");
    } else if (type == DEBUG_FATAL) {
        color_tobuffer(CLR_RED);
        buf_append("fatal");
    } else {
        buf_append(" -- ");
    }
    color_tobuffer(CLR_WHITE);
    buf_append("] %20s : ", name ? name : "");
    color_tobuffer(CLR_CANCEL);
    buf_append("%s", message);

    if (Debugger::enable_buffering && g_boffset < DEBUG_IO_HIGHWM) {
        return; // still under the high-water mark, keep buffering
    }
    Debugger::Flush();
}

void Debugger::Flush() {
    if (g_boffset > 0) {
        fputs(g_buffer, stdout);
        memset(g_buffer, 0, g_boffset);
        g_boffset = 0;
    }
}
