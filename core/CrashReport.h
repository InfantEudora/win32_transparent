#ifndef _CRASHREPORT_H_
#define _CRASHREPORT_H_

#include <stddef.h>

/*
    A report for the failure that will not happen twice on demand: what failed, on which thread,
    and every thread's stack at that moment. Written beside the exe as crash_<date>_<time>.txt
    and copied to stderr.

    The stacks come from gdb, attached to this process from inside it. That is the cheap way to
    get them with names on here - the debug exe carries DWARF, which Windows' own DbgHelp cannot
    read - and it gives every thread, not only the failing one.

    What it cannot give is the OTHER side of a race as it was. The failing thread waits for gdb,
    but the rest run on for the second or so it takes to attach, so they show where they got to.
    Measured on the ImGui input-queue race (item 98 in docs/engine_backlog_done.md): the report
    had the render thread faulting inside FindLatestInputEvent, with the main thread already back
    in its message loop. Freezing the others first would keep them, and was rejected: the main
    thread was caught inside free() in a run under gdb, and a thread frozen holding the heap lock
    deadlocks this report. When the partner matters, run the app under gdb, which stops
    everything at the fault.

    The stacks from gdb are DEBUG builds' only. Release is stripped, so gdb would print addresses
    and nothing else; there the report carries the failing thread's stack as module+offset, which
    the build's linker map (build/<exe>.map) names, and a minidump crash_<date>_<time>.dmp beside
    it with every thread. Under a debugger there is no report at all - it breaks instead, since a
    process gets one debugger and the one already attached beats a text file.
*/

//Reports and ends the program. `where`, if given, is a code address gdb also names by function and line.
[[noreturn]] void CrashReport(const char* what, const void* where = NULL);

//Routes crashes nobody handles - an access violation, mostly - to CrashReport. Every build; once, early.
void CrashReportInstall();

#endif
