#include "BackgroundWork.h"

#if defined(_WIN32)
#include <windows.h>
#endif

/*
    A function-local static: made on first use, so an app that never asks for background work
    never starts the thread, and destroyed after main returns, which is where it is joined. Every
    job only holds what it captured (rule 1), so nothing it touches has gone by then.
*/
BackgroundWorker& BackgroundWorker::Shared(){
    static BackgroundWorker worker;
    return worker;
}

BackgroundWorker::BackgroundWorker() : thread(&BackgroundWorker::Run,this) {}

BackgroundWorker::~BackgroundWorker(){
    {
        std::lock_guard<std::mutex> lock(mutex);
        f_stop = true;
        jobs.clear();
    }
    wake.notify_all();
    if (thread.joinable()){
        thread.join();
    }
}

void BackgroundWorker::Submit(std::function<void()> job){
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (f_stop){
            return;
        }
        jobs.push_back(std::move(job));
    }
    wake.notify_one();
}

void BackgroundWorker::Run(){
#if defined(_WIN32)
    //Below the render and physics threads, so a long solve takes the time they leave over rather
    //than a frame's worth of theirs.
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
    for (;;){
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock,[this](){ return f_stop || !jobs.empty(); });
            if (f_stop){
                return;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        job();
    }
}
