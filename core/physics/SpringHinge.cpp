#include "SpringHinge.h"
#include "Debug.h"

#include <math.h>

static Debugger *debug = new Debugger("SpringHinge", DEBUG_ALL);

SpringHinge::SpringHinge(PhysicsWorld* _world, rp3d::RigidBody* _anchor, rp3d::RigidBody* _body,
                         const Settings& settings){
    world = _world;
    anchor = _anchor;
    body = _body;
    if (!world || !world->rp_world || !body){
        debug->Err("SpringHinge: no world or no body to hinge\n");
        return;
    }

    rp3d::Vector3 pivot = (const rp3d::Vector3&)settings.pivot;
    rp3d::Vector3 axis = (const rp3d::Vector3&)settings.axis;
    if (axis.length() < 0.0001f){
        axis = rp3d::Vector3(0.0f,0.0f,1.0f);
    }
    axis.normalize();

    if (!anchor){
        //No colliders on it, so nothing can touch it or raycast it - it exists only for the joint
        //to hold on to. User data left NULL: there is no Object behind it.
        anchor = world->rp_world->createRigidBody(rp3d::Transform(pivot,rp3d::Quaternion::identity()));
        anchor->setType(rp3d::BodyType::STATIC);
        f_owns_anchor = true;
    }

    const rp3d::Quaternion q1 = anchor->getTransform().getOrientation();
    const rp3d::Quaternion q2 = body->getTransform().getOrientation();
    axis_in_anchor = q1.getInverse() * axis;
    rest_relative = q1.getInverse() * q2;

    /*
        THE INERTIA ABOUT THE HINGE, which is what the frequency is set against. rp3d keeps the
        body's inertia as a diagonal in its own frame about the centre of mass; the axis's share of
        that, plus m r^2 for the centre of mass sitting off the axis (parallel axis theorem).
    */
    const float m = body->getMass();
    rp3d::Vector3 a_local = q2.getInverse() * axis;
    const rp3d::Vector3& diag = body->getLocalInertiaTensor();
    float i_com = diag.x * a_local.x * a_local.x + diag.y * a_local.y * a_local.y + diag.z * a_local.z * a_local.z;
    rp3d::Vector3 com = body->getTransform() * body->getLocalCenterOfMass();
    rp3d::Vector3 r = com - pivot;
    rp3d::Vector3 r_perp = r - axis * r.dot(axis);
    inertia = i_com + m * r_perp.lengthSquare();

    /*
        GRAVITY'S OWN STIFFNESS: how much torque about the axis a small turn of the body adds, per
        radian. Turning by t moves the centre of mass by t (a x r), and gravity's torque about the
        axis is a . (r x m g), so the derivative is a . ((a x r) x m g). Positive when the centre of
        mass is above the pivot (it tips further), negative when it hangs below.
    */
    gravity_stiffness = 0.0f;
    if (body->isGravityEnabled()){
        rp3d::Vector3 g = world->rp_world->getGravity();
        gravity_stiffness = axis.dot(axis.cross(r).cross(g * m));
    }

    Retune(settings.hz,settings.damping_ratio);

    rp3d::HingeJointInfo info(anchor,body,pivot,axis);
    //Whatever the anchor is, the swinging body starts touching it at the pivot by construction, and
    //the joint is what holds the two together - a contact between them would fight it every tick.
    info.isCollisionEnabled = false;
    info.isLimitEnabled = settings.f_limits;
    info.minAngleLimit = settings.min_angle;
    info.maxAngleLimit = settings.max_angle;
    joint = dynamic_cast<rp3d::HingeJoint*>(world->rp_world->createJoint(info));
    if (!joint){
        debug->Err("SpringHinge: rp3d would not make the hinge joint\n");
    }

    //A sleeping body ignores its torque, so a spring that could fall asleep mid-swing would stop
    //being one. Only a handful of these exist, so the cost of never sleeping is nothing.
    body->setIsAllowedToSleep(false);

    debug->Info("SpringHinge: %.2f Hz, zeta %.2f -> I %.3f, k %.1f (gravity %.1f), c %.2f\n",
                hz,damping_ratio,inertia,stiffness,gravity_stiffness,damping);
}

SpringHinge::~SpringHinge(){
    if (!world || !world->rp_world){
        return;
    }
    if (joint){
        world->rp_world->destroyJoint(joint);
        joint = NULL;
    }
    if (f_owns_anchor && anchor){
        world->rp_world->destroyRigidBody(anchor);
        anchor = NULL;
    }
}

void SpringHinge::Retune(float _hz, float _damping_ratio){
    hz = _hz;
    damping_ratio = _damping_ratio;
    const float w = 2.0f * 3.14159265358979f * hz;
    stiffness = inertia * w * w + gravity_stiffness;
    if (stiffness < 0.0f){
        //A hanging body asked to swing SLOWER than it does on its own: a spring cannot pull the
        //other way, so it swings at its own rate instead.
        debug->Warn("SpringHinge: %.2f Hz is below this body's own swing - the spring does nothing\n",hz);
        stiffness = 0.0f;
    }
    damping = 2.0f * damping_ratio * inertia * w;
}

vec3 SpringHinge::GetAxis() const{
    if (!anchor){
        return vec3(0.0f,0.0f,1.0f);
    }
    rp3d::Vector3 a = anchor->getTransform().getOrientation() * axis_in_anchor;
    return vec3(a.x,a.y,a.z);
}

/*
    From the two bodies' orientations, rather than from the joint. The body's turn relative to the
    anchor, less the turn it was built with, is a rotation in the anchor's frame; its component
    about the axis is the angle. The quaternion's sign is chosen with w >= 0 so the answer is the
    short way round, in (-pi, pi].
*/
float SpringHinge::GetAngle() const{
    if (!anchor || !body){
        return 0.0f;
    }
    const rp3d::Quaternion q1 = anchor->getTransform().getOrientation();
    const rp3d::Quaternion q2 = body->getTransform().getOrientation();
    rp3d::Quaternion d = (q1.getInverse() * q2) * rest_relative.getInverse();
    float s = d.x * axis_in_anchor.x + d.y * axis_in_anchor.y + d.z * axis_in_anchor.z;
    float w = d.w;
    if (w < 0.0f){
        s = -s;
        w = -w;
    }
    return 2.0f * atan2f(s,w);
}

float SpringHinge::GetJointAngle() const{
    return joint ? joint->getAngle() : 0.0f;
}

float SpringHinge::GetRate() const{
    if (!anchor || !body){
        return 0.0f;
    }
    rp3d::Vector3 a = anchor->getTransform().getOrientation() * axis_in_anchor;
    return (body->getAngularVelocity() - anchor->getAngularVelocity()).dot(a);
}

void SpringHinge::Tick(){
    if (!joint || !body){
        return;
    }
    rp3d::Vector3 a = anchor->getTransform().getOrientation() * axis_in_anchor;
    float torque = -(stiffness * GetAngle()) - (damping * GetRate());
    body->applyWorldTorque(a * torque);
    //Equal and opposite, when the anchor is something that can be pushed back.
    if (anchor->getType() == rp3d::BodyType::DYNAMIC){
        anchor->applyWorldTorque(a * -torque);
    }
}
