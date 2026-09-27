#ifndef _BACKGROUNDWORK_H_
#define _BACKGROUNDWORK_H_

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

/*
    Work too slow for a tick or a frame, whose answer is allowed to arrive late: a flow field solved
    for a changed level, and later perhaps a fluid or a remesh. One worker thread, shared, below
    normal priority - there are already the main, render, physics and input threads competing for
    the cores, and none of them should lose time to this.

    Use it through LatestResult<T>. The rules it is built around:

      1. INPUTS ARE COPIED IN. A build captures BY VALUE what it needs, taken at a tick boundary,
         and touches nothing live - no Stage, Scene, Object, GL, and no `this`. It may outlive the
         thing that asked for it; the capture is all it has.
      2. RESULTS ARE PUBLISHED WHOLE, AND NEVER CHANGED AFTER. A reader holds a shared_ptr<const T>
         for as long as it reads - a tick, a frame - and sees one consistent answer, with no lock
         held while it reads. To retune a published result, copy it, change the copy, Set() it.
      3. THE LATEST REQUEST WINS. One build per slot runs at a time; asking again while it runs
         replaces what waits behind it, so five requests in a burst cost two builds, not five.
      4. RESULTS ARE ADOPTED BY THE OWNER, AT A POINT IT CHOOSES - Adopt(), on its own thread, not
         a callback on the worker. Anything that needs GL is uploaded there.
      5. WHEN A RESULT LANDS IS NOT DETERMINISTIC. Something only drawn can adopt whenever. Anything
         the SIMULATION reads must not pick up a result mid-run by whenever it finished: either
         build it synchronously, or adopt it at a tick decided by the request (asked at tick N,
         used from N+K, waiting if it is late), or a replay silently decides differently.
*/

class BackgroundWorker{
public:
    //The worker every LatestResult shares: started on first use, joined at exit. Jobs still
    //queued then are dropped; the one running finishes first.
    static BackgroundWorker& Shared();

    void Submit(std::function<void()> job);

    ~BackgroundWorker();

private:
    BackgroundWorker();
    void Run();

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    bool f_stop = false;
    std::thread thread;             //last, so everything it reads exists before it starts
};

/*
    One product built in the background - see the rules above. Request and Adopt belong to ONE
    owner thread; Get is safe from any.
*/
template<class T>
class LatestResult{
public:
    //Starts out holding `initial` (an empty T by default), so Get never returns null.
    explicit LatestResult(std::shared_ptr<const T> initial = std::make_shared<const T>())
        : slot(std::make_shared<Slot>()), current(std::move(initial)) {}

    //Builds `build()` on the worker. Replaces a request still waiting behind a running one.
    void Request(std::function<T()> build){
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->f_running){
            slot->pending = std::move(build);
            return;
        }
        slot->f_running = true;
        std::shared_ptr<Slot> s = slot;
        BackgroundWorker::Shared().Submit([s, build = std::move(build)]() mutable {
            Run(s,std::move(build));
        });
    }

    //Takes the newest finished result, if there is one; true when it did.
    bool Adopt(){
        std::shared_ptr<const T> r;
        {
            std::lock_guard<std::mutex> lock(slot->mutex);
            r = std::move(slot->ready);
        }
        if (!r){
            return false;
        }
        Set(std::move(r));
        return true;
    }

    //Publishes a result the owner made itself - a retuned copy of the current one, typically.
    void Set(std::shared_ptr<const T> value){
        std::lock_guard<std::mutex> lock(current_mutex);
        current = std::move(value);
    }

    std::shared_ptr<const T> Get() const {
        std::lock_guard<std::mutex> lock(current_mutex);
        return current;
    }

    //A build running, waiting, or finished and not yet adopted.
    bool IsBusy() const {
        std::lock_guard<std::mutex> lock(slot->mutex);
        return slot->f_running || slot->ready;
    }

private:
    //What the worker's job shares with the owner. Held by shared_ptr so a job still running when
    //its LatestResult is destroyed has somewhere harmless to put its answer.
    struct Slot{
        std::mutex mutex;
        bool f_running = false;
        std::function<T()> pending;
        std::shared_ptr<const T> ready;
    };

    /*
        Every finished build is handed over, even one a newer request has already overtaken: it is
        still newer than what is published, and a level being kicked apart faster than it builds
        would otherwise never get a field at all.
    */
    static void Run(std::shared_ptr<Slot> s, std::function<T()> build){
        for (;;){
            std::shared_ptr<const T> r = std::make_shared<const T>(build());
            std::lock_guard<std::mutex> lock(s->mutex);
            s->ready = std::move(r);
            if (!s->pending){
                s->f_running = false;
                return;
            }
            build = std::move(s->pending);
            s->pending = nullptr;
        }
    }

    std::shared_ptr<Slot> slot;
    mutable std::mutex current_mutex;
    std::shared_ptr<const T> current;
};

#endif
