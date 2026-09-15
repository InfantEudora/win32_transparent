#ifndef _PERFTIMER_H_
#define _PERFTIMER_H_
#include <string>
#include <chrono>
#include <deque>
#include <map>
#include <vector>

#define PROFILER 1

class PerfTimer{
public:
    std::deque<double> deltas;
    char* name;
    PerfTimer(const char* name);
    PerfTimer(char* name);
    static std::vector<PerfTimer*>* GetTimers(); //Returns a list of all available timers.

    ~PerfTimer();

    void Restart();
    double GetdtUs(void);
    double Stop();
    int num_deltas = 0;
    int max_deltas = 60;
    double delta = 0;
    double min = 0;
    double max = 0;
    double avg = 0;
private:
    /*
        std::chrono, not QueryPerformanceCounter, since 2026-09-15 - and the numbers did not move.

        This class held four LARGE_INTEGERs and called QueryPerformanceFrequency/Counter directly,
        which meant <Windows.h> in a header that Application.h and Renderer.h both include. That
        one include was the ONLY thing standing between Scene.cpp and ParticleEmitter.cpp and
        compiling for Android - each failed on nothing but "unknown type name 'LARGE_INTEGER'",
        four times, from here.

        The measurement is unchanged, not merely equivalent: on Windows libstdc++ backs
        steady_clock with QueryPerformanceCounter, so this reads the same counter it always did,
        and ElapsedUs still truncates to whole microseconds the way the old integer arithmetic
        did. steady_clock rather than system_clock because only the DIFFERENCE between two reads
        means anything here, and system_clock can be stepped by NTP mid-measurement.

        One member where there were four: startfreq and stopfreq were the counter frequency, which
        chrono's duration_cast now applies for us, and stopfreq was written but never read.
    */
    std::chrono::steady_clock::time_point starttime;

    //Microseconds since the last Start/Restart. Shared by GetdtUs and Stop so the two cannot
    //drift apart, which they were free to do while each did its own arithmetic.
    double ElapsedUs();

    bool stopped;
    void Start(char* name);
};

#if PROFILER
    #define PROFILER_START(name) PerfTimer* timer = new PerfTimer(name);
    #define PROFILER_STOP(name) delete timer;
	#define PROFILER_SCOPE(name) PerfTimer timer##__LINE__(name);
	#define PROFILER_FUNCTION() PROFILER_SCOPE(__PRETTY_FUNCTION__);
#else
	#define PROFILE_SCOPE(name)
	#define PROFILE_FUNCTION()
#endif

#endif
