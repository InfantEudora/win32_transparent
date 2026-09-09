#include "PrecisionSleeper.h"
#include "Debug.h"

static Debugger* debug = new Debugger("PrecisionSleeper",DEBUG_INFO);

PrecisionSleeper::PrecisionSleeper(){
    handle = CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    if (handle){
        f_high_resolution = true;
        return;
    }

    //Pre-1803 Windows: CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is rejected outright, so fall back
    //to an ordinary waitable timer. That one is only as accurate as the system timer resolution,
    //so raise it ONCE here and drop it once in the destructor - never around each individual
    //wait. Each call is a syscall that recomputes the system-wide timer, and the raise does not
    //even reliably take effect in time to help the sleep immediately after it.
    handle = CreateWaitableTimerExW(NULL,NULL,0,TIMER_ALL_ACCESS);
    if (!handle){
        debug->Err("No waitable timer at all (error %lu), falling back to Sleep()\n",GetLastError());
    }

    //Loaded by name rather than linked: the engine links winmm only when sound is built in.
    winmm = LoadLibraryA("winmm.dll");
    if (winmm){
        timeperiod_fn begin = (timeperiod_fn)(void*)GetProcAddress(winmm,"timeBeginPeriod");
        timeperiod_fn end   = (timeperiod_fn)(void*)GetProcAddress(winmm,"timeEndPeriod");
        //Only remember the matching release if the raise actually succeeded - timeEndPeriod is
        //reference counted, and an unmatched call would lower someone else's request.
        if (begin && end && begin(1) == 0){
            time_end_period = end;
        }
    }
    debug->Warn("No high-resolution waitable timer; using a 1ms system timer period instead\n");
}

PrecisionSleeper::~PrecisionSleeper(){
    if (handle){
        CloseHandle(handle);
        handle = NULL;
    }
    if (time_end_period){
        time_end_period(1);
        time_end_period = NULL;
    }
    if (winmm){
        FreeLibrary(winmm);
        winmm = NULL;
    }
}

static double GetQpcTicksPerSecond(){
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return (double)freq.QuadPart;
}

double PrecisionSleeper::NowUs(){
    //Fixed for the life of the process, so it is only ever queried once.
    static const double ticks_per_second = GetQpcTicksPerSecond();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    //Scaled before the multiply, not after: counter * 1000000 would run out of double mantissa
    //on a machine that has been up long enough.
    return (double)now.QuadPart * (1000000.0 / ticks_per_second);
}

void PrecisionSleeper::SleepUs(double us){
    if (us <= 0){
        return;
    }

    if (handle){
        LARGE_INTEGER due;
        //A negative due time is RELATIVE, counted in 100ns units.
        due.QuadPart = -(LONGLONG)(us * 10.0);
        if (SetWaitableTimer(handle,&due,0,NULL,NULL,FALSE)){
            WaitForSingleObject(handle,INFINITE);
            return;
        }
    }

    //Last resort. Rounded up so this never returns early, which is the one thing a caller
    //pacing a loop cannot recover from.
    Sleep((DWORD)((us + 999.0) / 1000.0));
}

void PrecisionSleeper::SleepUntilNextTick(double period_us){
    if (period_us <= 0){
        return;
    }

    deadline_us += period_us;
    double now = NowUs();
    double wait = deadline_us - now;

    if (wait > 0){
        SleepUs(wait);
        return;
    }

    //Already past the deadline. A small overrun is deliberately left alone: the deadline stays
    //where it is, the next tick gets a correspondingly shorter sleep, and the average period
    //comes out exact. Being more than a whole period behind means something actually stalled the
    //thread (a breakpoint, a mid-run asset load), and working through that backlog would run the
    //loop flat out with no sleeping at all - resync to now instead of trying to catch up.
    if (wait < -period_us){
        deadline_us = now;
    }
}

void PrecisionSleeper::ResetSchedule(){
    deadline_us = NowUs();
}
