#ifndef _SPRING_HINGE_H_
#define _SPRING_HINGE_H_

#include "PhysicsWorld.h"

/*
    A hinge with a spring about it: one rp3d::HingeJoint, plus a restoring torque applied once per
    tick that pulls the swinging body back to the angle it was built at.

    rp3d has no spring joint, so every springy thing in this repo has been a hinge or slider with a
    hand-written `-k * angle - c * rate` on top - apps/ship/HingedDoor, the pinball plunger, and
    now archer's straw man. This is that pattern once.

    TUNED BY FREQUENCY AND DAMPING RATIO, NOT BY k AND c. Those two depend on the body's inertia
    about the hinge, which moves whenever its mass, colliders or scale do - none of which has
    anything to do with how the swing should feel. What should feel a certain way is the swing, so
    that is what is asked for, and k and c are worked out from the body as it is built:

        k = I w^2 + k_gravity        c = 2 zeta I w        (w = 2 pi hz)

    K_GRAVITY IS THE PART THAT IS EASY TO LEAVE OUT. A body whose centre of mass is ABOVE its pivot
    is an inverted pendulum, and gravity tips it further by about m g d per radian (d = the centre
    of mass's height over the pivot). Without the term it rings slower than asked, and a soft
    setting simply falls over. Folded in, the frequency asked for is the one seen. For a HANGING
    body the same term comes out negative - gravity is already helping - and the formula still
    holds, down to k = 0 when the pendulum's own swing is already the frequency asked for.

    ANCHORED TO A BODY OR TO THE WORLD. With anchor NULL it makes a collider-less static body at
    the pivot and owns it, which is what a thing staked into the ground wants - there is no
    obvious body to hang it from, and hanging it from a level block would tie its life to that
    block's.

    Physics thread only, like every joint: build, Tick and destroy inside a tick or under the
    physics mutex. Destroy it BEFORE the bodies it joins - the joint holds raw body pointers.
*/
class SpringHinge{
public:
    struct Settings{
        vec3  pivot = vec3(0.0f,0.0f,0.0f);     //world space
        vec3  axis = vec3(0.0f,0.0f,1.0f);      //world space; normalised on the way in
        float hz = 1.0f;                        //the swing's frequency, what is seen
        float damping_ratio = 0.1f;             //0 rings forever, 1 is critical - no overshoot
        //Stops, in radians from the build pose, signed about `axis` (right-handed). Off by default.
        bool  f_limits = false;
        float min_angle = -1.0f;
        float max_angle = 1.0f;
    };

    SpringHinge(PhysicsWorld* world, rp3d::RigidBody* anchor, rp3d::RigidBody* body, const Settings& settings);
    ~SpringHinge();

    //Once per simulation tick, before the step. rp3d clears forces after every step, so the torque
    //is re-evaluated against the new angle each tick rather than set once.
    void Tick();

    //New frequency / damping ratio, against the inertia measured at build time.
    void Retune(float hz, float damping_ratio);

    //Radians from the build pose, signed about the axis; measured off the two bodies.
    float GetAngle() const;
    //The same angle as rp3d's joint reports it - a cross-check on the sign GetAngle and the
    //limits are assumed to share.
    float GetJointAngle() const;
    //Radians per second about the axis, body relative to anchor.
    float GetRate() const;
    //The hinge axis in world space now: it turns with the anchor.
    vec3 GetAxis() const;

    rp3d::HingeJoint* joint = NULL;

    //What the settings came out as, measured off the body at build time. Read-only.
    float inertia = 0.0f;               //about the hinge axis through the pivot
    float gravity_stiffness = 0.0f;     //k_gravity above; positive when gravity tips it over
    float stiffness = 0.0f;             //k, per radian
    float damping = 0.0f;               //c, per rad/s
    float hz = 0.0f;
    float damping_ratio = 0.0f;

private:
    PhysicsWorld* world = NULL;
    rp3d::RigidBody* anchor = NULL;
    rp3d::RigidBody* body = NULL;
    bool  f_owns_anchor = false;
    rp3d::Vector3 axis_in_anchor;       //the axis in the anchor's frame
    rp3d::Quaternion rest_relative;     //anchor^-1 * body at build time
};

#endif // _SPRING_HINGE_H_
