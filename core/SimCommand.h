#ifndef _SIM_COMMAND_H_
#define _SIM_COMMAND_H_

#include <stdint.h>
#include <type_traits>

#include "type_vec3.h"
#include "type_quat.h"
#include "Object.h"        //objectid_t / OBJECTID_INVALID
#include "AssetManager.h"  //assetid_t / ASSETID_INVALID

/*
    A SimCommand is a request to CHANGE the simulation that is not input: create an object,
    teleport one, destroy one, reset a vehicle. Input (a key, an axis) already goes through
    InputController as a tick-stamped event; this is the other half - the things the debug UI and
    the MCP "command" tools do, which no amount of button-pressing can express.

    Why a queue at all: these callers are all on the WRONG THREAD. An MCP tool handler runs on the
    MCP server's thread and the debug UI runs on the render thread, while the simulation is stepped
    by the physics thread. Mutating an object from either of those lands in the middle of a step -
    which in this engine has been observed as a vehicle's wheels spinning at hundreds of rad/s with
    no torque on them, after a teleport arrived mid-solve (see Vehicle::ResetState, which used to
    carry a private deferral queue of its very own for exactly this). The queue makes it land at one
    place in the tick, on the physics thread, on a known tick number. That is also exactly what a
    replay needs: an hour of simulation is reproducible from the input stream plus this command
    stream, and nothing else.

    WHY THIS IS A PLAIN STRUCT AND NOT A CLASS HIERARCHY. The obvious C++ shape for "a command"
    is a base class with a virtual Apply(), one subclass per command. That cannot be recorded: a
    vtable pointer is not data, so a queue of them can be executed but never written to a file and
    read back. So the split here is deliberate and it is the whole design:

      - the COMMAND is data: this fixed-size, trivially-copyable struct. It can be memcpy'd,
        fwritten, and replayed. It never contains a pointer to anything.
      - the HANDLER is code: a std::function registered against a command type on the Scene
        (see Scene::RegisterCommandHandler). It is never recorded, so it can be anything.

    That is what lets an app extend the command set while core stays ignorant of it. ApplicationTank
    registers a handler for its own TANK_CMD_VEHICLE_RESET, closing over controlled_tank - a type
    core has never heard of - and core's queue still carries, orders, counts and (later) records
    that command like any other, because to core it is just 90-odd bytes with a type number on it.

    THE COST, ACCEPTED KNOWINGLY: a fixed payload means every command must express itself in the
    fields below. A command needing something genuinely different (a name string, a variable-length
    list) does not fit and must either encode into `value`/an id, or the struct has to grow - and
    growing it is a breaking change to any recording. Hence `version`.
*/

//Bump on ANY change to the layout below - a widened field, a reordered field, a new field. A
//replay reader checks this FIRST and refuses (or migrates) a stream it does not understand, rather
//than silently misreading every following byte at the wrong offset. Bump it for a change in
//MEANING too (a field that keeps its type but starts meaning something else): the struct would
//look identical, and that is precisely the case a reader cannot otherwise detect.
#define SIM_COMMAND_VERSION 1

//Command types. Core owns everything below SIM_CMD_LAST; an app numbers its own from SIM_CMD_LAST
//upwards (SIM_CMD_LAST+0, +1, ...), the same convention the input keycodes use with INPUT_LAST.
//Never renumber an existing one - a recording refers to these by value.
enum SimCommandType : uint16_t{
    SIM_CMD_NONE = 0,
    //Teleport: set position/rotation/scale, whichever SIM_CMD_FLAG_* say are present. The
    //body goes with it (no push-out) - the command form of object_set_transform.
    SIM_CMD_OBJECT_SET_TRANSFORM,
    //Build an object from `asset` and add it to the scene at position/rotation/scale. The id of
    //the object created is reported back to the submitter - see Scene::GetCommandResult. No
    //physics body: this is the command form of the debug UI's Add > Asset menu, which does the
    //same. An app that wants a collider on what it spawns registers its own command type.
    SIM_CMD_OBJECT_SPAWN_ASSET,
    //There is deliberately no OBJECT_DESTROY. Nothing in the engine removes an object from a
    //scene yet - not the UI, not the MCP tools - so there is no teardown path to call (physics
    //body, children, the renderer's list, selected_object/hovered_object, mesh references). A
    //command type with no working handler is worse than no command type, so it is left out until
    //removal exists. Its number can be added at the end here when it does.
    SIM_CMD_LAST
};

//Which optional payload fields this command actually carries. A command that only wants to rotate
//an object must not also write a zeroed position over it, so "present" has to be explicit rather
//than inferred from a value being non-zero.
#define SIM_CMD_FLAG_POSITION   0x0001
#define SIM_CMD_FLAG_ROTATION   0x0002
#define SIM_CMD_FLAG_SCALE      0x0004

struct SimCommand{
    //FIRST, deliberately: a reader must be able to validate this before it trusts any other
    //offset in the struct. Every other field's position is only meaningful once this matches.
    uint16_t version = SIM_COMMAND_VERSION;
    uint16_t type = SIM_CMD_NONE;       //a SimCommandType, or an app's own SIM_CMD_LAST+n

    objectid_t target = OBJECTID_INVALID; //the object this is about. NEVER a pointer: a pointer is
                                          //not replayable, and it may have been freed by the time
                                          //the physics thread gets here.
    assetid_t asset = ASSETID_INVALID;    //AssetIDFromName(name) - see AssetManager.h for why a
                                          //hash of the name rather than an index or a counter.
    uint32_t flags = 0;                   //SIM_CMD_FLAG_*

    vec3 position = {};
    quat rotation = quat(0,0,0,1);
    vec3 scale = vec3(1,1,1);

    //Whatever else a command needs, in four scalars. Deliberately unnamed: this is the escape
    //hatch that lets an app add a command without touching this struct (and so without bumping
    //SIM_COMMAND_VERSION and invalidating recordings). Each command type documents its own use.
    float value[4] = {0,0,0,0};
};

//The recordability of a command is the entire point of the design above, so it is worth failing
//the BUILD rather than discovering at replay time that someone put a std::string in here.
static_assert(std::is_trivially_copyable<SimCommand>::value,
              "SimCommand must stay trivially copyable - it has to be writable to a replay log");

#endif
