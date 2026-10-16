#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_
#include <cstdarg>
#include <map>
#include <mutex>
#include <stdint.h>
#include <string>

#define DEBUGGER 1

typedef uint64_t debug_t;

// Different levels
#define DEBUG_NONE 90
#define DEBUG_ALL 00
#define DEBUG_TRACE 10
#define DEBUG_DEBUG 20
#define DEBUG_INFO 30
#define DEBUG_OK 40
#define DEBUG_WARN 50
#define DEBUG_ERROR 60
#define DEBUG_FATAL 70

// Colors (consumed by the Win32 console backend only; ignored elsewhere)
#define CLR_BLACK 1
#define CLR_RED 2
#define CLR_GREEN 3
#define CLR_YELLOW 4
#define CLR_BLUE 5
#define CLR_MAGENTA 6
#define CLR_CYAN 7
#define CLR_WHITE 8
#define CLR_GREY 9
#define CLR_LIGHTCYAN 10
#define CLR_LIGHTGREEN 11
#define CLR_CANCEL 15

// Debugger is portable: the named-instance registry, per-instance severity
// filtering, and message formatting all live in Debug.cpp unchanged across
// platforms. Only "where does a finished line actually go" differs, so
// that's the only part pushed behind SetupConsole()/Flush()/EmitLine() --
// implemented once in Debug_win32.cpp (ANSI console output, this file's
// original behavior) and once in Debug_android.cpp (__android_log_print).
class Debugger {
public:
    static std::map<std::string, Debugger *> *GetHandles();
    static Debugger *FindHandle(std::string name);
    static void SetLevel(std::string name, int level);
    static void ListHandles();
    static std::mutex mutex;
    static bool setup_done;

    Debugger(const char *name);
    Debugger(const char *name, int level);
    Debugger(char *name);
    ~Debugger();

    int level = DEBUG_ALL;
    static bool enable_buffering; // Meaningful to the Win32 backend only.
    static bool enable_console;

    static void SetupConsole();
    static void Flush();
    /*
        THE LOG FILE, opt-in: an app's main opens it, and from then on every line logged goes there too,
        as stderr has it but without the colours and with the time of day in front. It exists for the exe
        someone double-clicks, whose stderr goes nowhere. The last run's file is kept beside it as
        <name>_previous<ext>, so the log of a crash survives the restart that follows. Each line is
        flushed as it is written, so a crash loses none; it stops at DEBUG_LOG_FILE_MOST bytes, saying so.
        A second copy of the app, finding the file in use, writes <name>_2<ext> instead, and so on.
        Any thread. False, and nothing logged to a file, if `path` cannot be written.
    */
    static bool OpenLogFile(const std::string& path);
    static std::string LogFilePath();               //empty with no log file open
    void Start(char *name);
    void SetLevel(debug_t type);

    void PrintLine(const char *format, ...); // PrintLine always outputs no matter what level
    void Trace(const char *format, ...);
    void Debug(const char *format, ...);
    void Info(const char *format, ...);
    void Ok(const char *format, ...);
    void Warn(const char *format, ...);
    void Err(const char *format, ...);
    void Fatal(const char *format, ...);
    void PrintLine(debug_t type, const char *format, ...);

private:
    char *name = NULL;
    void PrintLineva(debug_t type, const char *format, va_list arglist);

    // Platform hook: emit one already-formatted, bounds-safe message.
    static void EmitLine(debug_t type, const char *name, const char *message);
};

#if DEBUGGER
#define DEBUGGER_FILENAME() Debugger::Add(__FILE__);
#define DEBUGGER_FUNCTION() Debugger::Add(__PRETTY_FUNCTION__);
#endif

#endif
