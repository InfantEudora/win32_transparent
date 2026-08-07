// Android backend for Debugger: routes through __android_log_print instead
// of a console. logcat already colors/columns by priority and shows the
// tag, so there's no ANSI-color or "[type] name : " prefix to build here --
// just map debug_t to an Android log priority and hand off the message.
#if defined(__ANDROID__)
#include "Debug.h"
#include <android/log.h>

void Debugger::SetupConsole() {
    // No console on Android; nothing to set up.
    setup_done = true;
}

static android_LogPriority ToAndroidPriority(debug_t type) {
    switch (type) {
        case DEBUG_TRACE: return ANDROID_LOG_VERBOSE;
        case DEBUG_DEBUG: return ANDROID_LOG_DEBUG;
        case DEBUG_INFO:  return ANDROID_LOG_INFO;
        case DEBUG_OK:    return ANDROID_LOG_INFO;
        case DEBUG_WARN:  return ANDROID_LOG_WARN;
        case DEBUG_ERROR: return ANDROID_LOG_ERROR;
        case DEBUG_FATAL: return ANDROID_LOG_FATAL;
        default:          return ANDROID_LOG_INFO;
    }
}

void Debugger::EmitLine(debug_t type, const char *name, const char *message) {
    __android_log_print(ToAndroidPriority(type), name ? name : "Debugger", "%s", message);
}

void Debugger::Flush() {
    // __android_log_print already writes through immediately; nothing to flush.
}
#endif