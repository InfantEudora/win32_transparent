/*
    See CrashReport.h.

    Written for a process that is already broken, so it leans on none of the engine: no Debugger,
    no engine state, Win32 and the C library only.

    Three arms: Win32 here, Linux after it (signals, gdb, backtrace), and a bare one for anything
    else - Android has its own.
*/
#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "CrashReport.h"
#ifndef DEBUG
#include <dbghelp.h>
#endif

static void WriteText(HANDLE file, const char* text){
    DWORD written = 0;
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
}

//Beside the exe, where imgui.ini and the saves already go - the working directory if that fails - as
//crash_<date>_<time>.<ext>: one stamp per crash, so a report's .txt and .dmp share it.
static void ReportPath(char* path, size_t size, const char* ext){
    static char stamp[32] = "";
    if (!stamp[0]){
        time_t now = time(NULL);
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
    }
    path[0] = 0;
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)size);
    char* slash = (len > 0 && len < size) ? strrchr(path, '\\') : NULL;
    char* name = slash ? slash + 1 : path;
    snprintf(name, size - (name - path), "crash_%s.%s", stamp, ext);
}

#ifndef DEBUG
/*
    RELEASE BUILDS (demo_plan.md, step 4: a tester's crash used to leave nothing). The exe is stripped, so there are no
    names to be had here; what is written instead is what a name can be found from later:
      - a MINIDUMP, crash_<stamp>.dmp - every thread's stack and registers, which a debugger reads;
      - in the report, the failing thread's stack as exe+offset, walked with the unwind tables every x64 exe carries.
        The linker's map of the same build (build/<exe>.map, engine.mk's RLINK) names the function at each offset.
    The dump goes first: walking a broken stack could itself fault, and the dump is the part that must not be lost.
*/
static char release_detail[6144] = "";

static void DetailLine(const char* text){
    size_t used = strlen(release_detail);
    snprintf(release_detail + used, sizeof(release_detail) - used, "%s", text);
}

//"  #3  chasm.exe+0x1a2b3c", the module found from the address.
static void DetailAddress(int frame, DWORD64 pc){
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(ULONG_PTR)pc, &module) && module){
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(module, path, MAX_PATH);
        const char* slash = (len > 0 && len < MAX_PATH) ? strrchr(path, '\\') : NULL;
        snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
    }
    char line[MAX_PATH + 64];
    snprintf(line, sizeof(line), "  #%-2d %s+0x%llx\r\n", frame, name,
             (unsigned long long)(pc - (DWORD64)(ULONG_PTR)module));
    DetailLine(line);
}

static void WalkStack(CONTEXT context){
#if defined(__x86_64__) || defined(_M_X64)
    DetailLine("\r\nThe failing thread's stack, innermost first:\r\n");
    for (int frame = 0; frame < 48 && context.Rip; frame++){
        DWORD64 pc = context.Rip;
        DetailAddress(frame, pc);
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, NULL);
        if (function){
            PVOID handler_data = NULL;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(0, image_base, pc, function, &context, &handler_data, &establisher, NULL);
        }else{
            //A leaf function, with no unwind entry: its return address is on top of the stack.
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery((LPCVOID)(ULONG_PTR)context.Rsp, &m, sizeof(m)) || m.State != MEM_COMMIT){
                break;
            }
            context.Rip = *(DWORD64*)(ULONG_PTR)context.Rsp;
            context.Rsp += 8;
        }
    }
#else
    (void)context;
#endif
}

typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                           PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

//dbghelp.dll is loaded only now, so the exe has no dependency on it until it has crashed.
static void WriteMinidump(EXCEPTION_POINTERS* info){
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    MiniDumpWriteDumpFn write = dbghelp ? (MiniDumpWriteDumpFn)(void*)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    char path[MAX_PATH + 64];
    ReportPath(path, sizeof(path), "dmp");
    const char* slash = strrchr(path, '\\');
    char line[MAX_PATH + 96];
    if (!write){
        DetailLine("No dbghelp.dll, so no minidump.\r\n");
        return;
    }
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE){
        snprintf(line, sizeof(line), "Could not write the minidump (error %lu).\r\n", GetLastError());
        DetailLine(line);
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception;
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;
    BOOL f_ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, info ? &exception : NULL, NULL, NULL);
    CloseHandle(file);
    snprintf(line, sizeof(line), f_ok ? "Minidump: %s\r\n" : "The minidump %s could not be written.\r\n",
             slash ? slash + 1 : path);
    DetailLine(line);
}
#endif

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

#ifndef DEBUG
    //Called directly rather than from the filter (which has done this already): this thread's stack from here.
    if (!release_detail[0]){
        WriteMinidump(NULL);
        CONTEXT context;
        RtlCaptureContext(&context);
        WalkStack(context);
    }
#endif
    char path[MAX_PATH + 64];
    ReportPath(path, sizeof(path), "txt");

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
    (void)where;    //the first line of the stack below
    WriteText(report, "Release build: stripped, so places are given as module+offset - the build's map file names them.\r\n");
    WriteText(report, release_detail);
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

/*
    Without this a crash just ends the process - no message, nothing in stderr. The ImGui input-queue
    race (item 98, fixed) did exactly that when the queue reallocated under it, as well as tripping
    the IM_ASSERT it was better known by. In a release build it is all a tester's crash leaves.
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
#ifndef DEBUG
    WriteMinidump(info);
    WalkStack(*info->ContextRecord);
#endif
    CrashReport(what, record->ExceptionAddress);
}

void CrashReportInstall(){
    //Replaces the MinGW runtime's filter, which offers these to signal handlers this engine never
    //installs and then to Windows, which ends the process without a word.
    SetUnhandledExceptionFilter(OnUnhandledException);
}

#elif defined(__linux__) && !defined(__ANDROID__)
/*
    LINUX. The same report, beside the exe, the same way round: the failing thread says what failed,
    gdb attaches from inside and lists every thread (debug), or the failing thread's own stack is
    written as module+offset (release, stripped). A fault arrives as a SIGNAL, on the faulting thread,
    so the handler below plays the part of Windows' unhandled-exception filter.

    No minidump: Linux's equivalent is the kernel's core dump, which systemd-coredump keeps on both
    Debian and Arch (coredumpctl list / coredumpctl gdb) without the process doing anything.
*/
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#ifndef DEBUG
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include "CrashReport.h"

static void WriteText(int file, const char* text){
    size_t left = strlen(text);
    while (left > 0){
        ssize_t n = write(file, text, left);
        if (n <= 0){
            return;
        }
        text += n;
        left -= (size_t)n;
    }
}

static void ReportPath(char* path, size_t size, const char* ext){
    static char stamp[32] = "";
    if (!stamp[0]){
        time_t now = time(NULL);
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
    }
    path[0] = 0;
    ssize_t len = readlink("/proc/self/exe", path, size - 1);
    path[(len > 0) ? len : 0] = 0;
    char* slash = (len > 0) ? strrchr(path, '/') : NULL;
    char* name = slash ? slash + 1 : path;
    snprintf(name, size - (name - path), "crash_%s.%s", stamp, ext);
}

//Linux's IsDebuggerPresent: a process being traced says by whom in its status.
static bool BeingDebugged(){
    char buffer[2048];
    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0){
        return false;
    }
    ssize_t got = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (got <= 0){
        return false;
    }
    buffer[got] = 0;
    const char* tracer = strstr(buffer, "TracerPid:");
    return tracer && atoi(tracer + 10) != 0;
}

#ifdef DEBUG
/*
    As on Windows: gdb attaches, appends every thread's stack, detaches. One thing Windows does not
    have to ask for: under Yama (ptrace_scope 1, the default on Ubuntu and common elsewhere) a process
    may trace its own descendants only, and gdb is our CHILD - so this process says, for the length of
    the report, that anyone of its own user may attach. A crashing process gives nothing away by it.
*/
static void AppendAllStacks(int report, const void* where){
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    char pid_text[32];
    snprintf(pid_text, sizeof(pid_text), "%ld", (long)getpid());
    char info_symbol[64] = "";
    char info_line[64] = "";
    if (where){
        unsigned long long address = (unsigned long long)(uintptr_t)where;
        snprintf(info_symbol, sizeof(info_symbol), "info symbol 0x%llx", address);
        snprintf(info_line, sizeof(info_line), "info line *0x%llx", address);
    }
    const char* argv[24];
    int n = 0;
    argv[n++] = "gdb";
    argv[n++] = "-batch";
    argv[n++] = "-nx";
    argv[n++] = "-iex"; argv[n++] = "set debuginfod enabled off";
    argv[n++] = "-iex"; argv[n++] = "set print thread-events off";
    argv[n++] = "-p"; argv[n++] = pid_text;
    if (where){
        argv[n++] = "-ex"; argv[n++] = info_symbol;
        argv[n++] = "-ex"; argv[n++] = info_line;
    }
    argv[n++] = "-ex"; argv[n++] = "thread apply all bt";
    argv[n] = NULL;

    pid_t child = fork();
    if (child == 0){
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0){
            dup2(nul, 0);
        }
        dup2(report, 1);
        dup2(report, 2);
        execvp("gdb", (char* const*)argv);
        _exit(127);
    }
    if (child < 0){
        WriteText(report, "Could not start gdb, so no stacks.\n");
        return;
    }
    //Seconds for a debug exe's symbols; the limit is only so a hung attach still ends.
    int status = 0;
    for (int waited_ms = 0; waited_ms < 120 * 1000; waited_ms += 50){
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child){
            if (WIFEXITED(status) && WEXITSTATUS(status) == 127){
                WriteText(report, "No gdb on PATH, so no stacks.\n");
            }
            return;
        }
        usleep(50 * 1000);
    }
    kill(child, SIGKILL);
}
#else
//The failing thread's own stack as "  #3  chasm+0x1a2b3c": a stripped exe has no names, and the
//offset is from where the module was loaded, which is what the link map and addr2line both count in.
static void AppendOwnStack(int report){
    void* frames[48];
    int count = backtrace(frames, 48);
    WriteText(report, "\nThe failing thread's stack, innermost first:\n");
    for (int i = 0; i < count; i++){
        Dl_info info;
        char line[512];
        if (dladdr(frames[i], &info) && info.dli_fname){
            const char* slash = strrchr(info.dli_fname, '/');
            snprintf(line, sizeof(line), "  #%-2d %s+0x%llx\n", i, slash ? slash + 1 : info.dli_fname,
                     (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)info.dli_fbase));
        }else{
            snprintf(line, sizeof(line), "  #%-2d 0x%llx\n", i, (unsigned long long)(uintptr_t)frames[i]);
        }
        WriteText(report, line);
    }
}
#endif

void CrashReport(const char* what, const void* where){
    //As on Windows: the first thread to fail reports, any other waits for it to end the program.
    static volatile sig_atomic_t f_reporting = 0;
    if (__sync_lock_test_and_set(&f_reporting, 1)){
        for (;;){
            pause();
        }
    }

    long pid = (long)getpid();
    long tid = (long)syscall(SYS_gettid);
    fprintf(stderr, "\n%s\non thread %ld\n", what, tid);
    fflush(stderr);

    //abort() below must end the process rather than come back into this handler as SIGABRT.
    signal(SIGABRT, SIG_DFL);
    if (BeingDebugged()){
        raise(SIGTRAP);
        abort();
    }

    char path[4096 + 64];
    ReportPath(path, sizeof(path), "txt");
    int report = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (report < 0){
        fprintf(stderr, "Could not write %s (%s)\n", path, strerror(errno));
        abort();
    }

    char header[1600];
    snprintf(header, sizeof(header), "%s\non thread %ld - gdb lists it as Thread <n> (Thread 0x... (LWP %ld))\n\n",
             what, tid, tid);
    WriteText(report, header);
#ifdef DEBUG
    AppendAllStacks(report, where);
#else
    (void)where;    //the first line of the stack below
    (void)pid;
    WriteText(report, "Release build: stripped, so places are given as module+offset - the build's map file names them.\n");
    AppendOwnStack(report);
#endif
    close(report);

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

static void OnFatalSignal(int number, siginfo_t* info, void* context){
    const void* at = NULL;
#if defined(__x86_64__)
    if (context){
        at = (const void*)((ucontext_t*)context)->uc_mcontext.gregs[REG_RIP];
    }
#else
    (void)context;
#endif
    const char* name = number == SIGSEGV ? "Segmentation fault (SIGSEGV)" : number == SIGBUS ? "Bus error (SIGBUS)"
                     : number == SIGILL ? "Illegal instruction (SIGILL)" : "Arithmetic exception (SIGFPE)";
    char what[256];
    snprintf(what, sizeof(what), "%s at address 0x%llx, at instruction 0x%llx", name,
             (unsigned long long)(uintptr_t)(info ? info->si_addr : NULL), (unsigned long long)(uintptr_t)at);
    CrashReport(what, at);
}

void CrashReportInstall(){
    /*
        ITS OWN STACK, because the fault worth reporting most is a stack overflow, and a handler that
        ran on the overflowed stack would fault again before writing a word. RESETHAND, so a fault
        inside the report itself ends the process instead of looping.
    */
    static char alternate[64 * 1024];
    stack_t stack = {};
    stack.ss_sp = alternate;
    stack.ss_size = sizeof(alternate);
    sigaltstack(&stack, NULL);
#ifndef DEBUG
    //backtrace loads libgcc's unwinder on its first call; do that now, not in a broken process.
    void* warm[1];
    backtrace(warm, 1);
#endif
    struct sigaction action = {};
    action.sa_sigaction = OnFatalSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGFPE, &action, NULL);
}

#else
#include <stdio.h>
#include <stdlib.h>
#include "CrashReport.h"

void CrashReport(const char* what, const void* where){
    (void)where;
    fprintf(stderr, "\n%s\n", what);
    abort();
}

void CrashReportInstall(){
}
#endif
