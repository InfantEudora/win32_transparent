#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_
#include <map>
#include <stdint.h>
#include <string>
#include <windows.h>
#include <mutex>

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

// Colors
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

class Debugger {
public:
    static std::map<std::string, Debugger *> *GetHandles();
    static Debugger *FindHandle(std::string name);
    static void SetLevel(std::string name, int level);
    static void ListHandles();
    static char buffer[8192];
    static int boffset;
    static int lines_buffered;
    static void Flush();
    static std::mutex mutex;
    static bool setup_done;
    Debugger(const char *name);
    Debugger(const char *name, int level);
    Debugger(char *name);
    ~Debugger();

    int level = DEBUG_ALL;
    static bool enable_buffering; // Enable if you need a high amount of output.
    static bool enable_console;

    static void SetupConsole();
    void Start(char *name);
    void SetLevel(debug_t type);

    void color_tobuffer(int color);
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
};

#if DEBUGGER
#define DEBUGGER_FILENAME() Debugger::Add(__FILE__);
#define DEBUGGER_FUNCTION() Debugger::Add(__PRETTY_FUNCTION__);
#endif

#endif