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
//
//VERSION 2 (2026-09-10): grew `subtype`, `velocity`, `angular_velocity`, `bool_values`,
//`collision_category_bits` and `collide_with_bits` so the debug UI's physics controls could move
//onto the queue. Nothing had been recorded yet, so the bump cost nothing - which will not be true
//of the next one.
#define SIM_COMMAND_VERSION 2

//Command types. Core owns everything below SIM_CMD_LAST; an app numbers its own from SIM_CMD_LAST
//upwards (SIM_CMD_LAST+0, +1, ...), the same convention the input keycodes use with INPUT_LAST.
//Never renumber an existing one - a recording refers to these by value.
enum SimCommandType : uint16_t{
    SIM_CMD_NONE = 0,
    //Teleport: set position/rotation/scale, whichever SIM_CMD_FLAG_* say are present. The
    //body goes with it (no push-out) - the command form of object_set_transform.
    SIM_CMD_OBJECT_SET_TRANSFORM,
    //Every physics property the Inspector can change on one object, in one command: the three
    //boolean flags, the two collision masks, the two velocities and the two collider surface
    //properties. One type rather than nine because they share a shape - "here is a set of
    //properties, the flags say which ones I mean" - exactly like SET_TRANSFORM. Fields used:
    //SIM_CMD_FLAG_STATIC/GRAVITY/ACTIVE (values in `bool_values`), _WAKE_UP (no value, it is a
    //trigger), _CATEGORY_BITS/_COLLIDE_BITS, _VELOCITY/_ANGULAR_VELOCITY, and _FRICTION/
    //_BOUNCINESS (in value[0]/value[1]).
    SIM_CMD_OBJECT_SET_PHYSICS,
    //Build an object from `asset` and add it to the scene at position/rotation/scale. The id of
    //the object created is reported back to the submitter - see Scene::GetCommandResult. No
    //physics body: this is the command form of the debug UI's Add > Asset menu, which does the
    //same. An app that wants a collider on what it spawns registers its own command type.
    SIM_CMD_OBJECT_SPAWN_ASSET,
    //Add one of the engine's own built-in object types - `subtype` is a SimPrimitiveKind. The
    //counterpart of SPAWN_ASSET for the things that come from no asset at all (an empty, a
    //camera, a light).
    SIM_CMD_OBJECT_SPAWN_PRIMITIVE,
    //Copy `target` (mesh, materials, colliders - see Object's copy constructor) and add the copy
    //to the scene. Its new id comes back through Scene::GetCommandResult. `bool_values` bit
    //SIM_CMD_FLAG_ACTIVE decides whether the copy's physics starts active; the UI clears it, so
    //the duplicate can be placed before it starts falling.
    SIM_CMD_OBJECT_DUPLICATE,
    //Mark `target` destroyed and wake the world so neighbours re-settle without it. NOTE this
    //only MARKS: Object::Destroy sets a flag and Renderer::DeleteDestroyedObjects does the actual
    //deleting, which only two apps (Dozer, Ship) currently call. So in every other app a
    //destroyed object stops rendering but its rigid body stays in the world. That is a
    //pre-existing engine gap, not something this command can fix from here - reaping has to
    //happen where the render thread is not walking the object tree at the same time.
    SIM_CMD_OBJECT_DESTROY,
    //Set the physics world's gravity vector (in `velocity`, being the only vec3 slot that isn't
    //part of the transform triple).
    SIM_CMD_WORLD_SET_GRAVITY,
    //Add an ObjectCollider gizmo hooked onto one of `target`'s colliders, so the collider's own
    //size and offset can be dragged in the viewport. `subtype` is the COLLIDER INDEX on that
    //body, deliberately not an rp3d::Collider* - a pointer is neither recordable nor safe to
    //carry across a tick boundary, and the index is what the caller can actually see.
    SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO,
    SIM_CMD_LAST
};

//`subtype` values for SIM_CMD_OBJECT_SPAWN_PRIMITIVE. Never renumber, same as the command types.
enum SimPrimitiveKind : uint32_t{
    SIM_PRIMITIVE_EMPTY = 0,
    SIM_PRIMITIVE_CAMERA,
    SIM_PRIMITIVE_DIRECTIONAL_LIGHT,
    SIM_PRIMITIVE_POINT_LIGHT
};

//Which optional payload fields this command actually carries. A command that only wants to rotate
//an object must not also write a zeroed position over it, so "present" has to be explicit rather
//than inferred from a value being non-zero.
//
//The three boolean properties are a pair of bits each: the flag here says "I am setting this one",
//and the bit at the SAME position in `bool_values` says what to set it to. Keeping the positions
//aligned is what lets a handler write `if (flags & F) Set(bool_values & F)` instead of carrying a
//parallel table of which value belongs to which flag.
#define SIM_CMD_FLAG_POSITION         0x0001
#define SIM_CMD_FLAG_ROTATION         0x0002
#define SIM_CMD_FLAG_SCALE            0x0004
#define SIM_CMD_FLAG_STATIC           0x0008
#define SIM_CMD_FLAG_GRAVITY          0x0010
#define SIM_CMD_FLAG_ACTIVE           0x0020
#define SIM_CMD_FLAG_WAKE_UP          0x0040 //a trigger, not a value - nothing in bool_values
#define SIM_CMD_FLAG_CATEGORY_BITS    0x0080
#define SIM_CMD_FLAG_COLLIDE_BITS     0x0100
#define SIM_CMD_FLAG_VELOCITY         0x0200
#define SIM_CMD_FLAG_ANGULAR_VELOCITY 0x0400
#define SIM_CMD_FLAG_FRICTION         0x0800 //value[0]
#define SIM_CMD_FLAG_BOUNCINESS       0x1000 //value[1]

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
    //A small discriminator WITHIN a command type, for a command that has variants rather than
    //separate types (SPAWN_PRIMITIVE's SimPrimitiveKind). Not a second type field: dispatch is
    //always on `type` alone, so a handler that ignores this still runs.
    uint32_t subtype = 0;

    vec3 position = {};
    quat rotation = quat(0,0,0,1);
    vec3 scale = vec3(1,1,1);

    //Not part of the transform triple above, hence separate fields rather than reusing `position`
    //- a struct that has to serve every command type rots the moment one field means two things.
    vec3 velocity = {};
    vec3 angular_velocity = {};

    //Booleans, at the same bit positions as their SIM_CMD_FLAG_* - see the flags comment.
    uint32_t bool_values = 0;
    uint32_t collision_category_bits = 0;
    uint32_t collide_with_bits = 0;

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
