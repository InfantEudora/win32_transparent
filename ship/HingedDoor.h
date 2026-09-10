#ifndef _HINGED_DOOR_H_
#define _HINGED_DOOR_H_

#include "AssetManager.h"
#include "Physics.h"
#include "Scene.h"
#include "ShipCollisionMasks.h"

//A door panel on a fixed vertical hinge - a test object for feeling out how the ship, the
//asteroids and the lasers push a jointed body around.
//
//Built from two of the primitives in data/ships.glb: the "cylinder" asset is the hinge post and
//the "cube" asset is scaled flat into the leaf.
//
//NOTE on which body this Object IS: unlike CraneCharacter (where `this` is the static base and
//the moving parts hang off it as separate scene objects), here `this` is the SWINGING PANEL and
//the static post is the separate scene object. The panel is what everything collides with, so
//making it `this` means a contact callback can identify a door with a single dynamic_cast on the
//body's user data - see ApplicationShip::onContact. The two are joined only by the hinge joint,
//never by AttachChild: a child's transform is driven by its parent's body each tick, which would
//just overwrite whatever the solver did.
class HingedDoor : public Object{
public:
    //hinge_position is where the post stands (its centre, at panel height); the panel extends
    //from there along +X rotated by `yaw` about world up. Angles in radians.
    HingedDoor(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,
               const vec3& hinge_position, float yaw = 0.0f, float width = 3.0f);
    ~HingedDoor();

    Object* post = NULL;                //the static cylinder the panel swivels around
    rp3d::HingeJoint* hinge = NULL;

    //Swing angle in radians, 0 = the pose the door was created in. Signed the same way
    //rp3d::HingeJoint::getAngle() is.
    float GetAngle();

    //A self-closing spring about the hinge, applied as a torque once per tick from
    //UpdatePhysicsState. 0 stiffness leaves the door swinging completely freely.
    float spring_stiffness = 60.0f;     //Nm per radian of swing
    float spring_damping   = 15.0f;     //Nm per rad/s of swing rate

    //Queued from the contact callback (which runs inside the physics step, where touching a body
    //is not safe), applied at the top of the next UpdatePhysicsState. A laser particle carries
    //almost no momentum of its own, so a hit that should visibly swing a door has to be given
    //one - knock_force is what a single hit is worth, in Newtons for one tick.
    void QueueKnock(const vec3& world_point, const vec3& world_direction);
    float knock_force = 1500.0f;

    //What the last knock was, and how many this door has taken - read by the door_telemetry MCP
    //tool. Diagnostic only, nothing in the simulation looks at these.
    vec3 last_knock_direction = {};
    vec3 last_knock_point = {};
    uint32_t knock_count = 0;

    void UpdatePhysicsState() override;

    //Puts the door back in its creation pose, dead still. Physics thread only.
    void Reset();

private:
    PhysicsWorld* physics_world = NULL; //kept so the destructor can take the joint back out
    vec3 hinge_axis = vec3(0,1,0);
    vec3 rest_position = {};
    quat rest_rotation = {};

    struct Knock{
        vec3 point;
        vec3 direction;
    };
    std::vector<Knock> queued_knocks;
};

#endif // _HINGED_DOOR_H_
