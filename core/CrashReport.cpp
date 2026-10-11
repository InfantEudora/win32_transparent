/*
    See CrashReport.h.

    Written for a process that is already broken, so it leans on none of the engine: no Debugger,
    no engine state, Win32 and the C library only.
*/
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "CrashReport.h"

static void WriteText(HANDLE file, const char* text){
    DWORD written = 0;
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
}

#ifdef DEBUG
//PATH first, then where the toolchain lives on this desk - it is not on PATH by default (CLAUDE.md).
static bool FindGdb(char* out, DWORD size){
    if (SearchPathA(NULL, "gdb.exe", NULL, size, out, NULL)){
        return true;
    }
    const char* fallback = "C:\\msys64\\mingw64\\bin\\gdb.exe";
    if (GetFileAttributesA(fallback) == INVALID_FILE_ATTRIBUTES){
        return false;
    }
    snprintf(out, size, "%s", fallback);
    return true;
}

/*
    gdb attaches, appends every thread's stack to the report, and detaches - a batch session that
    attached to a process detaches from it on the way out rather than killing it. This thread sits
    in WaitForSingleObject meanwhile, so its own stack is in there too, under this function.

    `where` gets named first, by function and by line. For a crash that is the faulting
    instruction, which a stack taken from inside the exception filter may not unwind back to.

    debuginfod off because MSYS2's gdb otherwise goes looking online for symbols of every system
    DLL in the process, which turns seconds into minutes.
*/
static void AppendAllStacks(HANDLE report, const void* where){
    char gdb[MAX_PATH];
    if (!FindGdb(gdb, sizeof(gdb))){
        WriteText(report, "No gdb.exe on PATH or in C:\\msys64\\mingw64\\bin, so no stacks.\r\n");
        return;
    }

    SECURITY_ATTRIBUTES inherit = {(DWORD)sizeof(inherit), NULL, TRUE};
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);

    //0x%llx, not %p: MinGW's %p has no 0x, and gdb takes a bare hex string for a bad number.
    char named[128] = "";
    if (where){
        unsigned long long address = (unsigned long long)(ULONG_PTR)where;
        snprintf(named, sizeof(named), " -ex \"info symbol 0x%llx\" -ex \"info line *0x%llx\"", address, address);
    }
    char cmd[MAX_PATH + 384];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -batch -nx -iex \"set debuginfod enabled off\" -iex \"set print thread-events off\""
             " -p %lu%s -ex \"thread apply all bt\"",
             gdb, GetCurrentProcessId(), named);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = report;
    si.hStdError = report;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)){
        //Seconds for a debug exe's symbols; the limit is only so a hung attach still ends.
        WaitForSingleObject(pi.hProcess, 120 * 1000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }else{
        char line[128];
        snprintf(line, sizeof(line), "Could not start gdb (error %lu), so no stacks.\r\n", GetLastError());
        WriteText(report, line);
    }
    if (nul != INVALID_HANDLE_VALUE){
        CloseHandle(nul);
    }
}
#endif

void CrashReport(const char* what, const void* where){
    //A race tends to fail on two threads at once. The first one reports; any other waits for it
    //to end the program, and shows up in its stacks while it does.
    static volatile LONG f_reporting = 0;
    if (InterlockedExchange(&f_reporting, 1)){
        Sleep(INFINITE);
    }

    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();
    fprintf(stderr, "\n%s\non thread %lu (0x%lx)\n", what, tid, tid);
    fflush(stderr);

    if (IsDebuggerPresent()){
        DebugBreak();
        abort();
    }

    //Beside the exe, where imgui.ini and the saves already go. The working directory if that fails.
    char path[MAX_PATH + 64] = "";
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = (len > 0 && len < MAX_PATH) ? strrchr(path, '\\') : NULL;
    char* name = slash ? slash + 1 : path;
    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
    snprintf(name, sizeof(path) - (name - path), "crash_%s.txt", stamp);

    //Inheritable, because gdb writes into this same handle - and so into the same file position.
    SECURITY_ATTRIBUTES inherit = {(DWORD)sizeof(inherit), NULL, TRUE};
    HANDLE report = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (report == INVALID_HANDLE_VALUE){
        fprintf(stderr, "Could not write %s (error %lu)\n", path, GetLastError());
        abort();
    }

    char header[1600];
    snprintf(header, sizeof(header), "%s\r\non thread %lu (0x%lx) - gdb lists it as Thread <n> (Thread %lu.0x%lx)\r\n\r\n",
             what, tid, tid, pid, tid);
    WriteText(report, header);
#ifdef DEBUG
    AppendAllStacks(report, where);
#else
    (void)where;
    WriteText(report, "Release build: stripped, so no stacks. The debug build of the same app writes them.\r\n");
#endif
    CloseHandle(report);

    //And the whole report to stderr, which is where everything else that went wrong is.
    FILE* file = fopen(path, "r");
    if (file){
        char buffer[4096];
        size_t got;
        while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0){
            fwrite(buffer, 1, got, stderr);
        }
        fclose(file);
    }
    fprintf(stderr, "\nCrash report: %s\n", path);
    fflush(stderr);
    abort();
}

#ifdef DEBUG
/*
    Without this a crash in a debug build just ends the process - no message, nothing in stderr.
    The ImGui input-queue race (item 98, fixed) did exactly that when the queue reallocated under
    it, as well as tripping the IM_ASSERT it was better known by.
*/
static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info){
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    unsigned long long at = (unsigned long long)(ULONG_PTR)record->ExceptionAddress;
    char what[256];
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2){
        ULONG_PTR kind = record->ExceptionInformation[0];
        snprintf(what, sizeof(what), "Access violation %s address 0x%llx, at instruction 0x%llx",
                 kind == 0 ? "reading" : (kind == 8 ? "executing" : "writing"),
                 (unsigned long long)record->ExceptionInformation[1], at);
    }else{
        snprintf(what, sizeof(what), "Unhandled exception 0x%08lx at instruction 0x%llx",
                 record->ExceptionCode, at);
    }
    CrashReport(what, record->ExceptionAddress);
}
#endif

void CrashReportInstall(){
#ifdef DEBUG
    //Replaces the MinGW runtime's filter, which offers these to signal handlers this engine never
    //installs and then to Windows, which ends the process without a word.
    SetUnhandledExceptionFilter(OnUnhandledException);
#endif
}
