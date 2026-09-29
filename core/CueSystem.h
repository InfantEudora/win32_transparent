#ifndef _CUESYSTEM_H_
#define _CUESYSTEM_H_

#include <stdint.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "CueLog.h"
#include "tinygltf/json.hpp"

/*
    How a game answers what happens in it: the cue layer. The design, and the reasons for it, are
    in apps/archer/cue_plan.md; this is the engine half, and it knows nothing about any game.

    THREE LAYERS. The game reports EVENTS - a signal with a payload, or a scope beginning or
    ending. A CUE is a row in a table saying how the game answers one: when, how often, how loud,
    what stops it. An ACTION is one thing the answer does: a sound, or anything else the app has
    registered a handler for (a shake, a rumble, a burst of particles). Cues only PRESENT. Nothing
    here may change what the next tick of the game computes, so anything that does - a flag, a
    locked control, a hitstop - belongs in the game's rules, which report it as an event.

    ONE THREAD, ONE PLACE. Everything is called on the simulation thread. Signals and scopes are
    collected through the tick and acted on together by Tick(), which the game calls once at the
    END of its tick, after everything that can signal has run - so the order of the log does not
    depend on which part of the game happened to find an event first.

    DETERMINISTIC. The clock is whatever tick the game passes to Tick(), which should be one a
    replay restores (the archer's Stage::ticks). Every random draw is a hash of that tick, the
    cue's key and the trigger's instance - never a stream - and every "is it still playing"
    question is answered from the sound's LENGTH, counted in ticks from when it started, not by
    asking the audio thread. So a replay decides exactly what the original decided, with or
    without a sound device, paused and stepped or not.

    NO SOUND DEPENDENCY. Sounds go through CueOutput, below. The engine's SoundSystem is compiled
    only into apps that ask for sound, while this is core and compiled once for every app, so it
    cannot call SoundSystem itself: CueSoundOutput.h is the adapter, header-only, compiled in the
    app. With no output at all the table still runs and still logs - lengths and peaks then read
    as zero.

    THE TABLE is JSON, normally a file under assets/cues/, and reloadable while the game runs:

    {
      "sounds": { "kick_hyaa": "sound/kick_hyaa.wav" },      name -> file, registered on load
      "buses":  { "effects": {}, "voice": { "gain": 1.0, "parent": "master" } },
      "groups": { "her": { "gap": 30 },
                  "narrator": { "gap": 0, "duck": { "effects": 0.4 }, "duck_ticks": 12 } },
      "cues": {
        "<name>": {
          TRIGGER - exactly one of:
            "signal": "shot"            a Signal of that name
            "begin":  "kick"            a scope of that name beginning
            "end":    "nocked"          ...or ending
          "scope":    "arrow"           what a signal cue belongs to, for cancelling (a begin cue
                                        belongs to the scope it began with); default the level
          "when":     [["points", "==", 10]]  all must hold on the payload; ops == != < <= > >=
          "once":     true              at most once per instance of its scope
          "chance":   0.4               drawn once per trigger
          "delay":    9                 ticks after the trigger
          "jitter":   6                 plus 0..jitter, drawn
          "delay_from": "strike_shift"  plus this payload value, rounded
          "forecast": "in"              the payload value is TICKS UNTIL a moment; the sound
                                        starts early enough that its loudest part lands on it -
                                        partway in, when the moment is nearer than that. Fires
                                        only once the moment is within reach; a forecast is
                                        expected every tick until then, and once with it
          "align":    "peak"            variants are shifted so each one's loudest part lands
                                        where the FIRST one's does; timings are authored on it
          "sounds":   ["a", "b"]        one is picked, by default never the same twice running
          "no_repeat": true
          "seed":     0                 the key its draws are hashed with; default from the name
          "gain":     0.8
          "gain_by":  [{ "value": "power", "in": [0, 1], "out": [0.55, 1.0], "min": 0, "max": 1 }]
                                        each factor a clamped linear map of a payload value;
                                        "distance" and "dx" are from the payload's "x" to the
                                        listener
          "pan_by":   { "value": "dx", "in": [-16, 16], "out": [-1, 1] }
          "pitch":    1.0
          "bus":      "effects"
          "looping":  false
          "follow":   { "value": "swing_speed", "gain": { "in": [0, 5], "out": [0, 1] },
                        "pitch": { "in": [0, 5], "out": [0.9, 1.1] } }
                                        a playing sound tracks a SetParameter every tick. Each of
                                        gain, pitch and pan may name a "value" of its own instead;
                                        "distance" and "dx" are from the trigger's "x" to the
                                        listener AS IT MOVES, so a loop standing in the level -
                        "pan": { "value": "dx", "in": [-20, 20], "out": [-0.7, 0.7] }
                                        - is steered as she walks past it. pan replaces pan_by.
          "group":    "her"             one line at a time per group
          "priority": 0
          "busy":     "skip"            the group is speaking: skip, queue (for max_wait ticks),
          "max_wait": 60                or interrupt a lower priority
          "gap":      0                 fewest ticks between two firings of this cue
          "max_instances": 0            most playing at once; 0 is no limit
          "on_end":   "drop"            when its scope ends: drop what is waiting (and let what is
                                        playing ring), stop (drop and cut), or keep (both go on)
          "actions":  [{ "kind": "shake", "offset": 0, ... }]
                                        handed to the app's handler for that kind, `offset` ticks
                                        after the cue fires, with the whole object as params
        }
      }
    }

    Every field but the trigger has a default, so most rows are a trigger and a sound.
*/

using json = nlohmann::json;

//What an event carries: a few named numbers. `x` is special - where it happened, for distance.
struct CuePayload{
    std::vector<std::pair<std::string,float>> values;
    CuePayload& Set(const std::string& name, float value);
    bool  Has(const std::string& name) const;
    float Get(const std::string& name, float fallback = 0.0f) const;
};

//One playing, as a cue asks for it. The sound system's own struct is not used here on purpose:
//this header must compile in an app that has no sound at all.
struct CuePlay{
    float gain = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    int   bus = 0;
    bool  f_looping = false;
    float from = 0.0f;      //seconds in
};

/*
    Where sounds go. Handles are the output's; 0 means "none". LoudestAt and LengthOf answer in
    seconds, and below zero for a name they do not know.
*/
class CueOutput{
public:
    virtual ~CueOutput(){}
    virtual void     RegisterSound(const char* name, const char* file) = 0;
    virtual float    LoudestAt(const char* name) = 0;
    virtual float    LengthOf(const char* name) = 0;
    virtual uint32_t Play(const char* name, const CuePlay& play) = 0;
    virtual void     Stop(uint32_t handle) = 0;
    virtual void     SetGain(uint32_t handle, float gain) = 0;
    virtual void     SetPitch(uint32_t handle, float pitch) = 0;
    //Only a following sound's pan moves once it has started. Not pure, so an output that has no
    //stereo to steer does not have to say so.
    virtual void     SetPan(uint32_t handle, float pan) { (void)handle; (void)pan; }
    //A bus by name, made if need be; `parent` empty or "master" for the master. -1 on failure.
    virtual int      AddBus(const char* name, const char* parent) = 0;
    virtual void     SetBusGain(int bus, float gain) = 0;
};

//A non-sound action being fired, as the app's handler receives it.
struct CueAction{
    std::string kind;
    std::string cue;
    uint64_t    tick = 0;
    /*
        The cue's GAIN - its `gain` times its `gain_by` curves, exactly what a sound of the same cue
        would be played at. An action scales itself by it, so "shake harder the harder she lands"
        is a curve in the table, written the way a sound's loudness is, and not a rule in code.
    */
    float       gain = 1.0f;
    CuePayload  payload;    //the trigger's
    json        params;     //the action's object from the table, kind and offset included
};

/*
    The draw every cue makes: uniform in [0,1) from a key, an instance, a tick and a salt.

    THE SAME MIXING as apps/archer/PlaceHash.h's Hash01 with the key and instance in place of the
    position, so that a cue with seed 0 and instance 0 draws exactly what the archer's hand-wired
    kick shout drew - which is what lets moving it onto a cue be proved against a recording.
    Salts: 1 the chance, 2 the jitter, 3 the variant.
*/
float CueHash01(uint32_t key, uint32_t instance, uint64_t tick, uint32_t salt);

class CueSystem{
public:
    //`tps` is the game's tick rate. `output` may be NULL, and may be set later; it is not owned.
    void Init(float tps, CueOutput* output);

    /*
        Loads, or reloads, the table. On any error the table in use is KEPT and `error` says
        what was wrong and where, so a typo in a file being tuned is a log line and not silence.
        A reload keeps every cue's history by name and lets what is playing ring; what was waiting
        is dropped, since its row may no longer say the same thing.
    */
    bool LoadTable(const char* asset_name, std::string& error);
    bool LoadTableText(const std::string& text, std::string& error, const std::string& where = "table");
    bool Reload(std::string& error);            //the last LoadTable's asset again
    const std::string& File() const { return file; };

    //--- What happened, during the tick -----------------------------------------------------------
    void Signal(const std::string& name, const CuePayload& payload = CuePayload(), int instance = 0);
    void BeginScope(const std::string& name, int instance = 0, const CuePayload& payload = CuePayload());
    void EndScope(const std::string& name, int instance = 0);
    bool IsScopeOpen(const std::string& name, int instance = 0) const;
    //A continuous value a following sound reads - swing speed, push speed. Keeps its last value.
    void SetParameter(const std::string& name, float value);
    //Where the listener is along x, for distance and pan. The archer's is her position.
    void SetListener(float x);

    //Acts on everything signalled since the last call, then fires whatever is due. Once a tick.
    void Tick(uint64_t now);

    //The level restarting: every scope ends by its cues' rules, everything waiting is dropped,
    //and every group falls silent. History is kept - a restart is not a new session.
    void Reset();

    void SetActionHandler(const std::string& kind, std::function<void(const CueAction&)> handler);

    /*
        What outlives a tick and matters to a replay: each cue's last pick and last firing, each
        group's last line. A recording's start state carries this, so a replay mid-session
        repeats the variations and gaps the original had rather than a fresh session's.
    */
    json CaptureHistory() const;
    void RestoreHistory(const json& history);

    CueLog log;

    //For panels and tools.
    std::vector<std::string> CueNames() const;
    /*
        A panel's PLAY button: the cue's sound and actions, at the next Tick, as if its trigger had
        come and every draw said yes - no condition, chance, delay, jitter, group, gap or scope, and
        every gain_by curve at its loudest, so what is heard is the row at full strength. Each press
        takes the next of its sounds in turn, so every variant can be heard. Logged as a play with
        the note "audition"; it touches no history, so it cannot change what a replay decides. A
        looping cue plays once.
    */
    void Audition(const std::string& cue);
    //Where a sound some cue plays is loudest, in seconds, as measured when the table loaded; -1
    //for one no cue plays. For a game that has to look that far ahead - the archer forecasts an
    //arrow's flight exactly as far as the swoosh's lead - without asking the sound system itself.
    float PeakOf(const std::string& sound) const;
    int NumWaiting() const { return (int)waiting.size(); };
    int NumPlaying() const { return (int)playing.size(); };

private:
    struct Curve{
        std::string value;
        float in0 = 0.0f, in1 = 1.0f, out0 = 0.0f, out1 = 1.0f;
        float lo = -1e30f, hi = 1e30f;
        bool  f_set = false;
    };
    struct Condition{
        std::string value;
        int   op = 0;           //0 == 1 != 2 < 3 <= 4 > 5 >=
        float rhs = 0.0f;
    };
    enum Trigger{ TRIGGER_SIGNAL, TRIGGER_BEGIN, TRIGGER_END };
    enum OnEnd{ END_DROP, END_STOP, END_KEEP };
    enum Busy{ BUSY_SKIP, BUSY_QUEUE, BUSY_INTERRUPT };

    struct Cue{
        std::string name;
        int         trigger = TRIGGER_SIGNAL;
        std::string on;             //the signal or scope name
        std::string scope;          //what it belongs to; empty is the level
        std::vector<Condition> when;
        bool  f_once = false;
        float chance = 1.0f;
        int   delay = 0;
        int   jitter = 0;
        std::string delay_from;
        std::string forecast;
        bool  f_align_peak = false;
        std::vector<std::string> sounds;
        bool  f_no_repeat = true;
        uint32_t key = 0;
        float gain = 1.0f;
        std::vector<Curve> gain_by;
        Curve pan_by;
        float pitch = 1.0f;
        std::string bus;
        int   bus_id = 0;
        bool  f_looping = false;
        Curve follow_gain;
        Curve follow_pitch;
        Curve follow_pan;
        std::string group;
        int   priority = 0;
        int   busy = BUSY_SKIP;
        int   max_wait = 60;
        int   gap = 0;
        int   max_instances = 0;
        int   on_end = END_DROP;
        json  actions = json::array();
    };
    struct Group{
        std::string name;
        int gap = 0;
        std::map<std::string,float> duck;   //bus name -> gain while this group speaks
        int duck_ticks = 1;
    };
    struct Bus{
        std::string name;
        std::string parent;
        float gain = 1.0f;
        int   id = -1;
        float duck = 1.0f;                  //where the duck currently is, eased per tick
    };
    //What outlives a firing, by cue name so a reload keeps it.
    struct CueHistory{
        int      last_pick = -1;
        int64_t  last_fired = -1;
    };
    struct GroupHistory{
        int64_t last_end = -1;              //the tick its last line ended
    };

    //A scope instance, open.
    struct Scope{
        std::string name;
        int         instance = 0;
        uint64_t    begun = 0;
        CuePayload  payload;
        std::vector<std::string> fired_once;
        uint64_t    serial = 0;             //tells two openings of the same name+instance apart
    };
    //An event waiting for Tick.
    struct Pending{
        int         kind = 0;               //0 signal, 1 begin, 2 end, 3 audition
        std::string name;
        int         instance = 0;
        CuePayload  payload;
    };
    //A decided cue waiting for its tick, or an action waiting for its offset.
    struct Waiting{
        std::string cue;
        uint64_t    due = 0;
        uint64_t    decided = 0;
        int         pick = 0;
        float       from = 0.0f;
        int         instance = 0;
        CuePayload  payload;
        uint64_t    scope_serial = 0;       //0 is the level
        int         action = -1;            //>= 0: this is that action of the cue, not the cue
        float       gain = 1.0f;            //for an action: the gain its cue fired with
        uint64_t    queued_in_group = 0;    //non-zero: a line queued behind its group, until this
    };
    //A sound this system started and is still counting.
    struct Playing{
        std::string cue;
        std::string sound;
        uint32_t    handle = 0;
        uint64_t    started = 0;
        uint64_t    ends = 0;               //UINT64_MAX for a loop
        uint64_t    scope_serial = 0;
        int         on_end = END_DROP;
        std::string group;
        int         priority = 0;
        Curve       follow_gain;
        Curve       follow_pitch;
        Curve       follow_pan;
        float       base_gain = 1.0f;
        float       base_pitch = 1.0f;
        //Where its trigger said it was, for a follow of "distance" or "dx".
        bool        f_has_x = false;
        float       x = 0.0f;
    };

    float tps = 60.0f;
    CueOutput* output = NULL;
    std::string file;
    float listener_x = 0.0f;
    uint64_t now = 0;
    uint64_t next_serial = 1;
    int      auditions = 0;                 //Audition's turn through a cue's sounds

    std::vector<Cue> cues;
    std::map<std::string,int> cue_index;
    std::vector<Group> groups;
    std::vector<Bus> buses;
    std::map<std::string,CueHistory> history;
    std::map<std::string,GroupHistory> group_history;
    std::map<std::string,float> parameters;
    std::map<std::string,std::function<void(const CueAction&)>> handlers;
    //Seconds, per sound name, measured when the table loads.
    std::map<std::string,float> peaks;
    std::map<std::string,float> lengths;

    std::vector<Pending> pending;
    std::vector<Scope> scopes;
    std::vector<Waiting> waiting;
    std::vector<Playing> playing;

    bool Parse(const json& j, const std::string& where, std::string& error);
    const Cue* FindCue(const std::string& name) const;
    const Group* FindGroup(const std::string& name) const;
    Scope* FindScope(const std::string& name, int instance);
    const Scope* FindScopeBySerial(uint64_t serial) const;

    void Trigger(const Cue& cue, const CuePayload& payload, int instance, uint64_t scope_serial);
    void Fire(Waiting& w);
    void FireAction(const Cue& cue, int action, const Waiting& w);
    void EndScopeNow(const std::string& name, int instance);
    void AuditionNow(const Cue& cue);
    void StopPlaying(Playing& p);
    void UpdateDucks();
    void Retire();

    float Value(const std::string& name, const CuePayload& payload) const;
    //What a playing sound's follow reads right now: "distance" and "dx" from where it started to
    //the listener, anything else the parameter of that name.
    float FollowValue(const std::string& name, const Playing& p) const;
    float Eval(const Curve& c, float v) const;
    bool  GroupBusy(const std::string& group, uint64_t at, const Playing** speaking) const;
    uint64_t LengthTicks(const std::string& sound, float from, float pitch) const;
    void  Skip(const std::string& cue, const std::string& sound, const char* why);
};

#endif
