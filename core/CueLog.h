#ifndef _CUELOG_H_
#define _CUELOG_H_

#include <stdint.h>
#include <stdio.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

/*
    What the game's reactions did, one line per decision, in the order they were made.

    The cue layer's log (apps/archer/docs/cue_plan.md), and it exists BEFORE the cue layer on purpose:
    moving a game's hand-wired sounds onto cues is only a refactor if nothing heard changes, and
    the way to show that is to print these same lines from the old code, replay a recording, and
    diff them against the new code's lines for the same replay. So the format is the contract -
    change it and every baseline recorded against it stops comparing.

    WHY THE LINES ARE WHAT THEY ARE:

      - the tick is the LEVEL's (Stage::ticks in the archer), which a replay restores, so two
        replays of one recording number their lines identically. The scene's physics tick keeps
        counting across restarts and would differ on every run;
      - numbers are rounded to what an ear could tell apart, so two replays on one machine print
        the same text and a diff shows only real changes;
      - a gain is the CUE's gain, before the game's master volume - which is a bus, or a slider,
        and not part of what a cue decided. Muting a run for an agent must not change its log.

    Written on the simulation thread, read by MCP handlers on theirs, so it carries its own lock.
    It holds text, not game state, and the lock is held only for a push or a copy.
*/
struct CueLogEntry{
    uint64_t    tick = 0;
    std::string cue;            //which reaction decided this
    std::string what;           //"play", "stop", or later "skip"
    std::string sound;          //the name played or stopped
    float gain = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    float from = 0.0f;          //seconds into the sound it starts at
    std::string note;           //why, for a skip; empty otherwise
};

class CueLog{
public:
    //Most lines kept. A replay of a minute of play is a few dozen; this is headroom, not a budget.
    static constexpr size_t CAPACITY = 1024;

    void Add(const CueLogEntry& e){
        std::string line = Format(e);
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(line);
        if (lines.size() > CAPACITY){
            lines.pop_front();
        }
        total++;
    }

    //The last `max` lines, oldest first; 0 for all of them.
    std::vector<std::string> Lines(size_t max = 0){
        std::lock_guard<std::mutex> lock(mutex);
        size_t n = (max == 0 || max > lines.size()) ? lines.size() : max;
        return std::vector<std::string>(lines.end() - (long)n,lines.end());
    }

    //Lines ever added, including those that have scrolled out.
    uint64_t Total(){
        std::lock_guard<std::mutex> lock(mutex);
        return total;
    }

    void Clear(){
        std::lock_guard<std::mutex> lock(mutex);
        lines.clear();
        total = 0;
    }

    /*
        One line. Fixed columns so a diff lines up; a stop has nothing to say past the sound's
        name, and a play leaves out a pitch, pan or start that is at its default, which is most
        of them, so a line says only what is particular about that playing.
    */
    static std::string Format(const CueLogEntry& e){
        char buf[256];
        snprintf(buf,sizeof(buf),"%8llu  %-14s %-5s %-14s",(unsigned long long)e.tick,
                 e.cue.c_str(),e.what.c_str(),e.sound.c_str());
        std::string s(buf);
        //An action has a gain too - the cue's, which it scales itself by (a shake's strength).
        if (e.what == "act"){
            snprintf(buf,sizeof(buf)," gain %.3f",e.gain);
            s += buf;
        }
        if (e.what == "play"){
            snprintf(buf,sizeof(buf)," gain %.3f",e.gain);
            s += buf;
            if (e.pitch != 1.0f){
                snprintf(buf,sizeof(buf)," pitch %.2f",e.pitch);
                s += buf;
            }
            if (e.pan != 0.0f){
                snprintf(buf,sizeof(buf)," pan %+.2f",e.pan);
                s += buf;
            }
            if (e.from > 0.0f){
                snprintf(buf,sizeof(buf)," from %.3f",e.from);
                s += buf;
            }
        }
        if (!e.note.empty()){
            s += "  (" + e.note + ")";
        }
        while (!s.empty() && s.back() == ' '){
            s.pop_back();
        }
        return s;
    }

private:
    std::mutex mutex;
    std::deque<std::string> lines;
    uint64_t total = 0;
};

#endif
