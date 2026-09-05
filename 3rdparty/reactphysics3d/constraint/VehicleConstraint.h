/********************************************************************************
* ReactPhysics3D physics library, http://www.reactphysics3d.com                 *
* Copyright (c) 2010-2026 Daniel Chappuis                                       *
*********************************************************************************
*                                                                               *
* This software is provided 'as-is', without any express or implied warranty.   *
* In no event will the authors be held liable for any damages arising from the  *
* use of this software.                                                         *
*                                                                               *
* Permission is granted to anyone to use this software for any purpose,         *
* including commercial applications, and to alter it and redistribute it        *
* freely, subject to the following restrictions:                                *
*                                                                               *
* 1. The origin of this software must not be misrepresented; you must not claim *
*    that you wrote the original software. If you use this software in a        *
*    product, an acknowledgment in the product documentation would be           *
*    appreciated but is not required.                                           *
*                                                                               *
* 2. Altered source versions must be plainly marked as such, and must not be    *
*    misrepresented as being the original software.                             *
*                                                                               *
* 3. This notice may not be removed or altered from any source distribution.    *
*                                                                               *
********************************************************************************/

#ifndef REACTPHYSICS3D_VEHICLE_CONSTRAINT_H
#define REACTPHYSICS3D_VEHICLE_CONSTRAINT_H

// Libraries
#include <reactphysics3d/configuration.h>
#include <reactphysics3d/mathematics/mathematics.h>
#include <reactphysics3d/containers/Array.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>

namespace reactphysics3d {

// Declarations
class PhysicsWorld;
class RigidBody;
class MemoryAllocator;

// Structure VehicleWheelSettings
/**
 * Describes one wheel of a VehicleConstraint: where its suspension attaches to the chassis,
 * which way it travels, how long it is, how it springs, how it steers and how its tire grips.
 * All positions and directions are in the local space of the chassis body.
 *
 * Directions: with the defaults (forward +z, up +y) the right side of the vehicle is -x, as in
 * any right-handed frame: right = forward x up.
 */
struct VehicleWheelSettings {

    public :

        // -------------------- Attributes -------------------- //

        /// Attachment point of the suspension on the chassis (the hard point the strut hangs from)
        Vector3 position;

        /// Direction the suspension extends in (points down for a normal car)
        Vector3 suspensionDirection;

        /// Axis the wheel steers about (points up for a normal car)
        Vector3 steeringAxis;

        /// Forward direction of the wheel when not steered (usually the vehicle forward direction).
        /// Together with wheelUp it defines the rolling direction of the tire.
        Vector3 wheelForward;

        /// Up direction of the wheel when not steered (usually the vehicle up direction)
        Vector3 wheelUp;

        /// Largest steer angle (rad) VehicleWheel::setSteerAngle() accepts, either way
        decimal maxSteerAngle;

        /// Suspension length at full compression (m), measured from position along suspensionDirection.
        /// Below this a hard stop takes over from the spring.
        decimal suspensionMinLength;

        /// Suspension length at full droop (m). This is also the rest length of the spring.
        decimal suspensionMaxLength;

        /// Extra spring compression at full droop (m). The natural length of the spring is
        /// suspensionMaxLength + suspensionPreloadLength, so the wheel already pushes at full
        /// droop. Note this makes touch-down a discontinuity and hence bouncier.
        decimal suspensionPreloadLength;

        /// The suspension spring. In SpringMode::FREQUENCY_AND_DAMPING_RATIO the coefficients are
        /// derived from the chassis mass and inertia as seen at this wheel, so all wheels of a
        /// car tuned to 1.5 Hz bounce at 1.5 Hz whatever the chassis. Default 1.5 Hz, ratio 0.5.
        SpringSettings suspensionSpring;

        /// Radius of the wheel (m). The tread, not the hub, touches the ground.
        decimal radius;

        /// Width of the wheel (m). Only used for rendering helpers.
        decimal width;

        /// Moment of inertia of the wheel about its axle (kg.m^2). For a solid cylinder this is
        /// 0.5 * mass * radius^2: 0.9 for a 20 kg wheel of radius 0.3 m.
        decimal inertia;

        /// Angular damping of the free-spinning wheel: dw/dt = -angularDamping * w (1/s)
        decimal angularDamping;

        /// Tire friction coefficient in the rolling direction. The longitudinal impulse a wheel can
        /// transmit per step is at most this times the normal (suspension + hard stop) impulse.
        decimal longitudinalFriction;

        /// Tire friction coefficient sideways. The lateral impulse a wheel can transmit per step
        /// is at most this times the normal impulse.
        decimal lateralFriction;

        /// If true, tire forces are applied at suspensionForcePoint (fixed on the chassis) instead
        /// of at the contact point. Less accurate against dynamic ground, more stable.
        bool enableSuspensionForcePoint;

        /// Where tire forces are applied when enableSuspensionForcePoint is set (chassis local
        /// space). A good default is the wheel centre at mid travel.
        Vector3 suspensionForcePoint;

        /// Number of ground samples cast per wheel, each a ray parallel to suspensionDirection but
        /// offset along the (steered) rolling direction, approximating points around the rim of the
        /// tire rather than just its very bottom. The one that touches down soonest is the wheel's
        /// contact for the step. 1 (default) is a single ray straight down through the attachment
        /// point, exactly as before. A higher count catches a kerb, pothole edge or speed bump that
        /// the tire's curved profile would reach before a single ray does, at that many raycasts'
        /// cost per wheel. Values <= 1 behave as 1.
        uint32 numContactSamples;

        /// Half-angle (rad) of the sample fan about the wheel's lateral axis: with numContactSamples
        /// samples, the outermost ones are offset from the attachment point by radius * sin of this
        /// angle, forward and backward along the rolling direction (bounded by radius itself, never
        /// reaching further than the tire's own rim could). Unused when numContactSamples <= 1.
        decimal contactSampleHalfAngle;

        // -------------------- Methods -------------------- //

        /// Constructor
        VehicleWheelSettings()
            : position(0, 0, 0), suspensionDirection(0, -1, 0), steeringAxis(0, 1, 0), wheelForward(0, 0, 1), wheelUp(0, 1, 0),
              maxSteerAngle(decimal(70.0) * PI_RP3D / decimal(180.0)),
              suspensionMinLength(decimal(0.3)), suspensionMaxLength(decimal(0.5)), suspensionPreloadLength(decimal(0.0)),
              suspensionSpring(SpringSettings::fromFrequencyAndDampingRatio(decimal(1.5), decimal(0.5))),
              radius(decimal(0.3)), width(decimal(0.1)), inertia(decimal(0.9)), angularDamping(decimal(0.2)),
              longitudinalFriction(decimal(1.0)), lateralFriction(decimal(1.0)),
              enableSuspensionForcePoint(false), suspensionForcePoint(0, 0, 0),
              numContactSamples(1), contactSampleHalfAngle(PI_RP3D / decimal(4.0)) {}
};

// Class VehicleWheel
/**
 * Runtime state of one wheel of a VehicleConstraint: its settings, the driver inputs that act
 * on it (steer angle, drive torque, brake torque), what the solver found this step (ground
 * contact, suspension length, impulses) and the spin of the wheel.
 */
class VehicleWheel {

    private :

        // -------------------- Attributes -------------------- //

        /// Settings of the wheel (may be changed at any time)
        VehicleWheelSettings mSettings;

        /// Steer angle (rad) about the steering axis, positive turns the wheel to the left
        decimal mSteerAngle;

        /// Torque (N.m) driving the wheel, positive rolls the vehicle forward
        decimal mDriveTorque;

        /// Torque (N.m, >= 0) the brake applies against the rotation of the wheel
        decimal mBrakeTorque;

        /// Impulse (N.s) the brake can transmit to the ground this step: brake torque * dt / radius
        decimal mBrakeImpulse;

        /// Body the wheel is touching (nullptr if in the air)
        RigidBody* mContactBody;

        /// Index of the contact body in the rigid body components (valid for the current step only)
        uint32 mContactBodyComponentIndex;

        /// True if the contact body cannot receive impulses this step (static, kinematic or sleeping)
        bool mIsContactBodyFixed;

        /// Contact point on the ground (world space)
        Vector3 mContactPoint;

        /// Contact normal (world space, pointing from the ground towards the vehicle)
        Vector3 mContactNormal;

        /// Rolling direction of the tire in the contact plane (world space)
        Vector3 mContactLongitudinal;

        /// Sideways direction of the tire in the contact plane (world space, to the right)
        Vector3 mContactLateral;

        /// Lever arm from the ground centre of mass to the point where the tire forces act (world space)
        Vector3 mR1;

        /// Lever arm from the chassis centre of mass to the point where the tire forces act (world space)
        Vector3 mR2;

        /// Plane constant of the axle at the moment of contact: mContactNormal . axlePosition.
        /// The axle at minimum suspension length may not go below this plane (hard stop).
        decimal mAxlePlaneConstant;

        /// Current suspension length (m) from the attachment point along the suspension direction
        decimal mSuspensionLength;

        /// Rotation speed of the wheel about its axle (rad/s), positive when it rolls the vehicle forward
        decimal mAngularVelocity;

        /// Rotation angle of the wheel about its axle (rad, in [0, 2 pi])
        decimal mRotationAngle;

        /// The suspension spring constraint along the contact normal
        AxisConstraintPart mSuspensionPart;

        /// Hard constraint along the contact normal, active when the suspension is fully compressed
        AxisConstraintPart mHardStopPart;

        /// Tire friction along the rolling direction
        AxisConstraintPart mLongitudinalPart;

        /// Tire friction sideways
        AxisConstraintPart mLateralPart;

    public :

        // -------------------- Methods -------------------- //

        /// Constructor
        VehicleWheel(const VehicleWheelSettings& settings);

        /// Return the settings of the wheel
        const VehicleWheelSettings& getSettings() const;

        /// Return the settings of the wheel (writable, changes apply from the next step)
        VehicleWheelSettings& getSettings();

        /// Return the steer angle (rad), positive to the left
        decimal getSteerAngle() const;

        /// Set the steer angle (rad) about the steering axis, positive to the left. Clamped to
        /// +/- VehicleWheelSettings::maxSteerAngle.
        void setSteerAngle(decimal angle);

        /// Return the drive torque (N.m) on the wheel
        decimal getDriveTorque() const;

        /// Set the torque (N.m) driving the wheel, positive rolls the vehicle forward. Stays until changed.
        void setDriveTorque(decimal torque);

        /// Return the brake torque (N.m) on the wheel
        decimal getBrakeTorque() const;

        /// Set the torque (N.m, >= 0) the brake applies against the rotation of the wheel. Stays until changed.
        void setBrakeTorque(decimal torque);

        /// Return true if the wheel is touching something
        bool hasContact() const;

        /// Return the body the wheel is touching (nullptr if none)
        RigidBody* getContactBody() const;

        /// Return the contact point in world space (only meaningful if hasContact())
        const Vector3& getContactPoint() const;

        /// Return the contact normal in world space (only meaningful if hasContact())
        const Vector3& getContactNormal() const;

        /// Return the rolling direction of the tire in world space (only meaningful if hasContact())
        const Vector3& getContactLongitudinal() const;

        /// Return the sideways (right) direction of the tire in world space (only meaningful if hasContact())
        const Vector3& getContactLateral() const;

        /// Return the current suspension length (m)
        decimal getSuspensionLength() const;

        /// Return true if the suspension is fully compressed and the hard stop is engaged
        bool hasHitHardStop() const;

        /// Return the impulse (N.s) the suspension spring applied to the chassis this step
        decimal getSuspensionImpulse() const;

        /// Return the impulse (N.s) the hard stop applied to the chassis this step
        decimal getHardStopImpulse() const;

        /// Return the total normal impulse (N.s) this step: suspension spring plus hard stop
        decimal getNormalImpulse() const;

        /// Return the impulse (N.s) applied along the rolling direction this step (positive pushes the vehicle forward)
        decimal getLongitudinalImpulse() const;

        /// Return the impulse (N.s) applied sideways this step (positive pushes the vehicle to the right)
        decimal getLateralImpulse() const;

        /// Return the rotation speed of the wheel about its axle (rad/s), positive when rolling the vehicle forward
        decimal getAngularVelocity() const;

        /// Set the rotation speed of the wheel about its axle (rad/s)
        void setAngularVelocity(decimal angularVelocity);

        /// Return the rotation angle of the wheel about its axle (rad, in [0, 2 pi])
        decimal getRotationAngle() const;

        /// Set the rotation angle of the wheel about its axle (rad)
        void setRotationAngle(decimal angle);

        // -------------------- Friendship -------------------- //

        friend class VehicleConstraint;
        friend class SolveVehicleSystem;
};

// Structure VehicleConstraintSettings
/**
 * Vehicle-wide settings of a VehicleConstraint. Wheels are added after creation with
 * VehicleConstraint::addWheel().
 */
struct VehicleConstraintSettings {

    public :

        // -------------------- Attributes -------------------- //

        /// Up direction of the vehicle in chassis local space
        Vector3 up;

        /// Forward direction of the vehicle in chassis local space
        Vector3 forward;

        /// Steepest surface (angle from horizontal, radians) the wheels treat as ground. Steeper
        /// surfaces hit by a wheel ray are ignored so a wall is not mistaken for the road.
        decimal maxSlopeAngle;

        /// Collision category bits the wheel rays test against (see Collider::setCollisionCategoryBits)
        unsigned short raycastCategoryMaskBits;

        // -------------------- Methods -------------------- //

        /// Constructor
        VehicleConstraintSettings()
            : up(0, 1, 0), forward(0, 0, 1), maxSlopeAngle(decimal(80.0) * PI_RP3D / decimal(180.0)),
              raycastCategoryMaskBits(0xFFFF) {}
};

// Class VehicleConstraint
/**
 * A wheeled vehicle: one chassis rigid body plus any number of wheels, each a raycast
 * suspension. Every step the solver casts a ray (or, if VehicleWheelSettings::numContactSamples
 * is more than 1, a small fan of them across the tire's footprint) from the attachment point of
 * each wheel along its suspension direction; where it hits the ground, constraints between the
 * chassis and the ground body are solved together with the other constraints of the world:
 *
 *  - a spring-damper along the contact normal (AxisConstraintPart, implicit, stable for any
 *    stiffness and time step), pushing only;
 *  - a hard stop along the normal once the suspension is fully compressed, with position
 *    correction like the joints;
 *  - tire friction along the rolling direction and sideways, each an impulse clamped to a
 *    friction coefficient times the normal impulse. The longitudinal one couples the spin of
 *    the wheel to the ground: a free wheel spins up to roll without slipping, a driven wheel
 *    pushes the vehicle (or spins if the tire cannot hold the torque) and a braked wheel stops
 *    the vehicle up to what the brake and the tire can transmit.
 *
 * Driving is done per wheel: VehicleWheel::setSteerAngle(), setDriveTorque() and
 * setBrakeTorque(). How an engine, gearbox and differential distribute torque between the
 * wheels is left to the application (or a later controller class), which keeps this class a
 * pure constraint.
 *
 * The wheels themselves have no collider: the chassis is the only body, and the wheel is a
 * ray. This is the usual arcade/simulation compromise (see Jolt's VehicleConstraint, which
 * this follows): cheap, stable, and it interacts with static and dynamic ground alike.
 *
 * Create with PhysicsWorld::createVehicle(), destroy with PhysicsWorld::destroyVehicle().
 * Destroying the chassis body destroys the vehicle too.
 */
class VehicleConstraint {

    private :

        // -------------------- Attributes -------------------- //

        /// Reference to the physics world
        PhysicsWorld& mWorld;

        /// The chassis body
        RigidBody* mBody;

        /// Up direction of the vehicle in chassis local space
        Vector3 mUp;

        /// Forward direction of the vehicle in chassis local space
        Vector3 mForward;

        /// Cosine of the max slope angle
        decimal mCosMaxSlopeAngle;

        /// Collision category bits the wheel rays test against
        unsigned short mRaycastCategoryMaskBits;

        /// The wheels
        Array<VehicleWheel> mWheels;

        /// Index of the chassis in the rigid body components (valid for the current step only)
        uint32 mBodyComponentIndex;

        /// True if the constraint is being solved this step (chassis dynamic and awake)
        bool mIsActiveThisStep;

        // -------------------- Methods -------------------- //

        /// Constructor
        VehicleConstraint(PhysicsWorld& world, RigidBody* body, const VehicleConstraintSettings& settings,
                          MemoryAllocator& allocator);

        /// Destructor
        ~VehicleConstraint() = default;

    public :

        // -------------------- Methods -------------------- //

        /// Deleted copy-constructor
        VehicleConstraint(const VehicleConstraint& vehicle) = delete;

        /// Deleted assignment operator
        VehicleConstraint& operator=(const VehicleConstraint& vehicle) = delete;

        /// Return the chassis body
        RigidBody* getBody() const;

        /// Add a wheel and return its index
        uint32 addWheel(const VehicleWheelSettings& settings);

        /// Return the number of wheels
        uint32 getNbWheels() const;

        /// Return a wheel
        const VehicleWheel& getWheel(uint32 index) const;

        /// Return a wheel (writable, to change its settings, inputs or spin)
        VehicleWheel& getWheel(uint32 index);

        /// Return the up direction of the vehicle in chassis local space
        const Vector3& getLocalUp() const;

        /// Return the forward direction of the vehicle in chassis local space
        const Vector3& getLocalForward() const;

        /// Return the max slope angle (radians)
        decimal getMaxSlopeAngle() const;

        /// Set the max slope angle (radians)
        void setMaxSlopeAngle(decimal maxSlopeAngle);

        /// Return the collision category bits the wheel rays test against
        unsigned short getRaycastCategoryMaskBits() const;

        /// Set the collision category bits the wheel rays test against
        void setRaycastCategoryMaskBits(unsigned short maskBits);

        /// Return the world-space centre of a wheel (attachment point + suspension length along
        /// the suspension direction)
        Vector3 getWheelCenterWorld(uint32 index) const;

        /// Return the world-space transform of a wheel for rendering: at the wheel centre, steered
        /// and rotated by the rotation angle. In the space of this transform the wheel points its
        /// wheelForward forward and wheelUp up, and spins about their cross product (the axle):
        /// with the defaults, a wheel model along +z with +y up, turning about +x.
        Transform getWheelWorldTransform(uint32 index) const;

        /// Return the total force (N) the suspension (spring and hard stop) applied to the chassis
        /// this step, summed over all wheels along their contact normals
        decimal getTotalSuspensionForce(decimal timeStep) const;

        // -------------------- Friendship -------------------- //

        friend class PhysicsWorld;
        friend class SolveVehicleSystem;
};

// Return the settings of the wheel
RP3D_FORCE_INLINE const VehicleWheelSettings& VehicleWheel::getSettings() const {
    return mSettings;
}

// Return the settings of the wheel (writable)
RP3D_FORCE_INLINE VehicleWheelSettings& VehicleWheel::getSettings() {
    return mSettings;
}

// Return the steer angle
RP3D_FORCE_INLINE decimal VehicleWheel::getSteerAngle() const {
    return mSteerAngle;
}

// Set the steer angle
RP3D_FORCE_INLINE void VehicleWheel::setSteerAngle(decimal angle) {
    mSteerAngle = clamp(angle, -mSettings.maxSteerAngle, mSettings.maxSteerAngle);
}

// Return the drive torque
RP3D_FORCE_INLINE decimal VehicleWheel::getDriveTorque() const {
    return mDriveTorque;
}

// Set the drive torque
RP3D_FORCE_INLINE void VehicleWheel::setDriveTorque(decimal torque) {
    mDriveTorque = torque;
}

// Return the brake torque
RP3D_FORCE_INLINE decimal VehicleWheel::getBrakeTorque() const {
    return mBrakeTorque;
}

// Set the brake torque
RP3D_FORCE_INLINE void VehicleWheel::setBrakeTorque(decimal torque) {
    assert(torque >= decimal(0.0));
    mBrakeTorque = std::max(decimal(0.0), torque);
}

// Return true if the wheel is touching something
RP3D_FORCE_INLINE bool VehicleWheel::hasContact() const {
    return mContactBody != nullptr;
}

// Return the body the wheel is touching
RP3D_FORCE_INLINE RigidBody* VehicleWheel::getContactBody() const {
    return mContactBody;
}

// Return the contact point in world space
RP3D_FORCE_INLINE const Vector3& VehicleWheel::getContactPoint() const {
    return mContactPoint;
}

// Return the contact normal in world space
RP3D_FORCE_INLINE const Vector3& VehicleWheel::getContactNormal() const {
    return mContactNormal;
}

// Return the rolling direction of the tire in world space
RP3D_FORCE_INLINE const Vector3& VehicleWheel::getContactLongitudinal() const {
    return mContactLongitudinal;
}

// Return the sideways direction of the tire in world space
RP3D_FORCE_INLINE const Vector3& VehicleWheel::getContactLateral() const {
    return mContactLateral;
}

// Return the current suspension length
RP3D_FORCE_INLINE decimal VehicleWheel::getSuspensionLength() const {
    return mSuspensionLength;
}

// Return true if the suspension is fully compressed and the hard stop is engaged
RP3D_FORCE_INLINE bool VehicleWheel::hasHitHardStop() const {
    return mHardStopPart.isActive();
}

// Return the impulse the suspension spring applied this step
RP3D_FORCE_INLINE decimal VehicleWheel::getSuspensionImpulse() const {
    return mSuspensionPart.getTotalLambda();
}

// Return the impulse the hard stop applied this step
RP3D_FORCE_INLINE decimal VehicleWheel::getHardStopImpulse() const {
    return mHardStopPart.getTotalLambda();
}

// Return the total normal impulse this step
RP3D_FORCE_INLINE decimal VehicleWheel::getNormalImpulse() const {
    return mSuspensionPart.getTotalLambda() + mHardStopPart.getTotalLambda();
}

// Return the impulse applied along the rolling direction this step
RP3D_FORCE_INLINE decimal VehicleWheel::getLongitudinalImpulse() const {
    return mLongitudinalPart.getTotalLambda();
}

// Return the impulse applied sideways this step
RP3D_FORCE_INLINE decimal VehicleWheel::getLateralImpulse() const {
    return mLateralPart.getTotalLambda();
}

// Return the rotation speed of the wheel about its axle
RP3D_FORCE_INLINE decimal VehicleWheel::getAngularVelocity() const {
    return mAngularVelocity;
}

// Set the rotation speed of the wheel about its axle
RP3D_FORCE_INLINE void VehicleWheel::setAngularVelocity(decimal angularVelocity) {
    mAngularVelocity = angularVelocity;
}

// Return the rotation angle of the wheel about its axle
RP3D_FORCE_INLINE decimal VehicleWheel::getRotationAngle() const {
    return mRotationAngle;
}

// Set the rotation angle of the wheel about its axle
RP3D_FORCE_INLINE void VehicleWheel::setRotationAngle(decimal angle) {
    mRotationAngle = angle;
}

// Return the chassis body
RP3D_FORCE_INLINE RigidBody* VehicleConstraint::getBody() const {
    return mBody;
}

// Return the number of wheels
RP3D_FORCE_INLINE uint32 VehicleConstraint::getNbWheels() const {
    return static_cast<uint32>(mWheels.size());
}

// Return a wheel
RP3D_FORCE_INLINE const VehicleWheel& VehicleConstraint::getWheel(uint32 index) const {
    assert(index < mWheels.size());
    return mWheels[index];
}

// Return a wheel (writable)
RP3D_FORCE_INLINE VehicleWheel& VehicleConstraint::getWheel(uint32 index) {
    assert(index < mWheels.size());
    return mWheels[index];
}

// Return the up direction of the vehicle in chassis local space
RP3D_FORCE_INLINE const Vector3& VehicleConstraint::getLocalUp() const {
    return mUp;
}

// Return the forward direction of the vehicle in chassis local space
RP3D_FORCE_INLINE const Vector3& VehicleConstraint::getLocalForward() const {
    return mForward;
}

// Return the max slope angle (radians)
RP3D_FORCE_INLINE decimal VehicleConstraint::getMaxSlopeAngle() const {
    return std::acos(mCosMaxSlopeAngle);
}

// Set the max slope angle (radians)
RP3D_FORCE_INLINE void VehicleConstraint::setMaxSlopeAngle(decimal maxSlopeAngle) {
    mCosMaxSlopeAngle = std::cos(maxSlopeAngle);
}

// Return the collision category bits the wheel rays test against
RP3D_FORCE_INLINE unsigned short VehicleConstraint::getRaycastCategoryMaskBits() const {
    return mRaycastCategoryMaskBits;
}

// Set the collision category bits the wheel rays test against
RP3D_FORCE_INLINE void VehicleConstraint::setRaycastCategoryMaskBits(unsigned short maskBits) {
    mRaycastCategoryMaskBits = maskBits;
}

}

#endif
