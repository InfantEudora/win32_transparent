#ifndef _INPUTRECORDING_H_
#define _INPUTRECORDING_H_
/*
    A recorded run of input, and the text file it lives in.

    WHAT IS IN ONE: the input events an InputController applied between the start and the end of a
    recording, each stamped with the tick it was applied before, plus a small header saying where
    the recording started. Nothing else - no positions, no physics state. A replay reproduces a run
    by reproducing its INPUT, so it is only as faithful as the simulation is deterministic, and it
    only means something from the same starting point. The header's `scene` and `state` are how it
    gets back there: see Application::CaptureRecordingState.

    TEXT, ONE EVENT PER LINE, because the person who made the recording is expected to open it and
    cut it down, and a format that needs a tool to read cannot be trimmed with an editor. A file
    looks like this:

        input_recording 1
        app Archer
        scene Archer
        tick_rate 50
        recorded 2026-09-25 14:03:11
        begin 0
        end 612
        state {"x":12.5,"y":0.9,"facing":1.0}
        # tick  event  action  value
        0 down right key=D
        148 axis move 0.734112
        150 up right key=D

    Events are `down`/`up` (a key, with the physical key it came from), `axis` (a scalar, the stick
    value), `abs` and `rel` (integer axes). The action is its name if the app named it
    (InputController::NameAction) and its number otherwise. `key=` is optional: without it the
    event drives the action's first mapping, which is what a scripted hold does.

    TRIMMING is changing `begin` and `end`: only that window of ticks replays. Events before
    `begin` are applied together on the first replayed tick, so a key held across the cut stays
    held, and everything still down at `end` is released. Deleting lines outside the window is
    tidying, not required. Ticks are counted from the start of the recording and stay that way
    after a trim, so line numbers in a bug report still match the file.

    Lines starting with # are comments, and so is anything after a # on an event line.
*/
#include <string>
#include <vector>
#include <stdint.h>
#include "InputController.h"
#include "SimCommand.h"
#include "tinygltf/json.hpp"

/*
    A COMMAND IN A RECORDING: a SimCommand flagged SIM_CMD_FLAG_RECORD - a player's intent the
    input alone cannot reproduce (see that flag) - and the tick it was applied before, counted like
    the events'. A line of its own among the events:

        120 cmd type=1001 subtype=62423 flags=0x8000 value=1,0,0,0

    Only the fields that differ from a default SimCommand are written, every float with %.9g so it
    reads back exactly. Trimmed like the events: commands before `begin` are applied on the first
    replayed tick, commands at or after `end` are not applied.
*/
struct RecordedCommand{
    uint32_t tick = 0;
    SimCommand cmd;
};

struct InputRecording{
    int         version = 1;
    std::string app;
    std::string scene;          //the Scene it was recorded in, by name; empty = don't care
    std::string recorded_at;    //local time, for the person reading it
    float       tick_rate = 0.0f;
    uint32_t    begin = 0;      //the window that replays, in the recording's own ticks
    uint32_t    end = 0;
    //Whatever the app wrote down when the recording started - see
    //Application::CaptureRecordingState. Opaque to core.
    nlohmann::json state = nlohmann::json::object();
    std::vector<RecordedInputEvent> events;
    std::vector<RecordedCommand> commands;      //by tick

    //`names` supplies the action names written and read; NULL writes and accepts numbers only.
    bool Save(const std::string& path, const InputController* names, std::string& error) const;
    bool Load(const std::string& path, const InputController* names, std::string& error);
};

//The physical key as written after `key=` - "D", "SPACE", "PAD_A", "0x5B" - and back. Parse
//accepts every form Format produces, plus plain decimal.
std::string FormatSystemKey(uint32_t system_keycode);
bool ParseSystemKey(const std::string& text, uint32_t& out);

#endif
