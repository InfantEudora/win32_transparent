#include "PerfTimer.h"
//printf, which used to arrive here through the <Windows.h> in PerfTimer.h. See the note on
//PerfTimer::starttime for why that include is gone.
#include <cstdio>

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

double PerfTimer::ElapsedUs(){
    //duration_cast TRUNCATES, which is what the old (end-start)*1000000/freq integer arithmetic
    //did too - so a timer that read 349 us before this change still reads 349 us.
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - starttime).count();
}

void PerfTimer::Restart(){
    this->stopped = false;
    starttime = std::chrono::steady_clock::now();
}

void PerfTimer::Start(char* name){
    this->name = name;
    this->stopped = false;
    starttime = std::chrono::steady_clock::now();
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
    return ElapsedUs();
}

double PerfTimer::Stop(){
    AddSample(ElapsedUs());
    return delta;
}

//The statistics half of what Stop() used to do, split out so a measurement taken by something
//other than this class's clock gets the same treatment - see the comment in PerfTimer.h.
void PerfTimer::AddSample(double us){
    delta = us;
    //Marks the timer settled, so a GPU-fed timer (which never calls Restart/Stop) does not get a
    //meaningless wall-clock sample appended by the destructor on the way out.
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
}
