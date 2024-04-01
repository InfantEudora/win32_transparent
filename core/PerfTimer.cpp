#include "PerfTimer.h"

//Returns a static map handle.
std::vector<PerfTimer*>* PerfTimer::GetTimers() {
    static std::vector<PerfTimer*> timers;
    return &timers;
}

PerfTimer::PerfTimer(const char* name){
    Start((char*)name);
}

PerfTimer::PerfTimer(char* name){
    Start(name);
}

void PerfTimer::Restart(){
    this->stopped = false;
    QueryPerformanceFrequency(&startfreq);
    QueryPerformanceCounter(&starttime);
}

void PerfTimer::Start(char* name){
    this->name = name;
    this->stopped = false;
    QueryPerformanceFrequency(&startfreq);
    QueryPerformanceCounter(&starttime);
    std::vector<PerfTimer*>* timers = GetTimers();
    timers->push_back(this);
}

PerfTimer::~PerfTimer(){
    if (!stopped){
        Stop();
    }
    printf("Profiler [%s] : %8.0f uS %.0f(ms)\n",name,delta,delta/1000LL);
}

double PerfTimer::GetdtUs(void){
    LARGE_INTEGER endtime;
    QueryPerformanceCounter(&endtime);
    elapsedtime.QuadPart = endtime.QuadPart - starttime.QuadPart;
    elapsedtime.QuadPart *= 1000000;
    elapsedtime.QuadPart /= startfreq.QuadPart;
    return elapsedtime.QuadPart;
}

double PerfTimer::Stop(){
    LARGE_INTEGER endtime;
    QueryPerformanceFrequency(&stopfreq);
    QueryPerformanceCounter(&endtime);
    elapsedtime.QuadPart = endtime.QuadPart - starttime.QuadPart;
    elapsedtime.QuadPart *= 1000000;
    elapsedtime.QuadPart /= startfreq.QuadPart;
    delta = elapsedtime.QuadPart;
    stopped = true;

    deltas.push_back(delta);
    num_deltas++;
    if (num_deltas>max_deltas){
        num_deltas = max_deltas;
        deltas.pop_front();
    }
    avg = 0;
    min = __FLT_MAX__;
    max = __FLT_MIN__;
    for (int i=0;i<num_deltas;i++){
        avg+= deltas[i];
        if (deltas[i] < min){
            min = deltas[i];
        }
        if (deltas[i] > max){
            max = deltas[i];
        }
    }
    avg /= (double)num_deltas;
    return delta;
}