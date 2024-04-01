#ifndef _PERFTIMER_H_
#define _PERFTIMER_H_
#include <string>
#include <chrono>
#include <deque>
#include <map>
#include <vector>

#include <Windows.h>

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
    LARGE_INTEGER starttime;
    LARGE_INTEGER startfreq;
    LARGE_INTEGER stopfreq;
    LARGE_INTEGER elapsedtime;

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