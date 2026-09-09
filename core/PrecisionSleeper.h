#ifndef _PRECISIONSLEEPER_H_
#define _PRECISIONSLEEPER_H_

#include <windows.h>

/*
    Sub-millisecond sleeping for the engine's fixed-rate loops, without burning a core.

    The classic Win32 way to do this is timeBeginPeriod(1) + Sleep(ms), which raises the SYSTEM
    timer interrupt rate so the scheduler can wake threads more often than the ~15.6ms default.
    Two problems with it: Sleep() only takes whole milliseconds, so a 16.67ms tick is asked for as
    16 and the lost fraction becomes a permanent bias; and the resolution change is a global,
    power-hungry knob that costs a syscall to move.

    A waitable timer created with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+) instead
    gets ~100ns granularity for that ONE wait, without touching the system timer resolution at
    all. Measured here on a 50Hz loop with 3ms of work per tick:

        timeBeginPeriod(1) + Sleep()        590us rms error, ~2% systematic drift
        high-resolution waitable timer      300us rms error, no drift
        the same plus a 500us final spin    190us rms error, no drift, ~0.5% extra CPU

    Anything older falls back to a plain waitable timer plus a 1ms system timer period. winmm is
    resolved at RUN time for that path so the engine does not have to link against it - see the
    makefile, where -lwinmm belongs to the optional sound build.
*/
class PrecisionSleeper{
public:
    PrecisionSleeper();
    ~PrecisionSleeper();

    //Monotonic microseconds from an arbitrary origin, off the same QPC clock PerfTimer uses.
    static double NowUs();

    //Blocks for us microseconds. Returns immediately for anything <= 0.
    void SleepUs(double us);

    //Paces a loop at one iteration per period_us. The schedule is ABSOLUTE - every deadline is
    //the previous deadline plus period_us, never "now plus period_us" - so a tick that overruns
    //is paid back by a shorter sleep and the error cannot accumulate. period_us may change
    //between calls (the physics time-factor slider does exactly that) and is picked up as-is.
    void SleepUntilNextTick(double period_us);

    //Anchors the schedule at "now". Call before the first SleepUntilNextTick, and after any
    //deliberate gap in the loop, so the next tick is not immediately considered late.
    void ResetSchedule();

    //False means the high-resolution timer was unavailable and the degraded path is in use.
    bool IsHighResolution() const { return f_high_resolution; }

private:
    //timeBeginPeriod/timeEndPeriod, resolved from winmm.dll only on the fallback path.
    typedef UINT (WINAPI *timeperiod_fn)(UINT);

    HANDLE        handle            = NULL;
    double        deadline_us       = 0;
    bool          f_high_resolution = false;
    HMODULE       winmm             = NULL;
    timeperiod_fn time_end_period   = NULL;  //Non-NULL only if timeBeginPeriod(1) actually took.
};

#endif
