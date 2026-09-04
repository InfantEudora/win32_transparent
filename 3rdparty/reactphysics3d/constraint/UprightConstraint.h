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

#ifndef REACTPHYSICS3D_UPRIGHT_CONSTRAINT_H
#define REACTPHYSICS3D_UPRIGHT_CONSTRAINT_H

// Libraries
#include <reactphysics3d/configuration.h>
#include <reactphysics3d/mathematics/mathematics.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <reactphysics3d/constraint/AngularAxisConstraintPart.h>

namespace reactphysics3d {

// Declarations
class PhysicsWorld;
class RigidBody;

// Structure UprightConstraintSettings
/**
 * Settings of an UprightConstraint: which body axis to keep pointing which way, how far it may
 * tilt, and whether the limit is a hard cone or a spring.
 */
struct UprightConstraintSettings {

    public :

        // -------------------- Attributes -------------------- //

        /// The axis of the body (local space) that must stay up
        Vector3 localAxis;

        /// The direction (world space) it must stay close to. Given explicitly, not taken from
        /// gravity, so this also works in a world without gravity.
        Vector3 worldAxis;

        /// Half angle (rad) of the cone the body axis may tilt within before the constraint acts.
        /// 0 keeps the body exactly upright (with a spring: springs it back to upright). PI disables it.
        decimal maxAngle;

        /// How the tilt beyond maxAngle is corrected. Default-constructed (hard): a rigid stop with
        /// position correction, the body never tilts further. Soft: a torsion spring-damper pulls
        /// the axis back towards the cone (or to upright when maxAngle is 0), tuned as a frequency
        /// and damping ratio relative to the effective inertia of the body.
        SpringSettings spring;

        /// Damping of the spin about the body axis (yaw), as a rate in 1/s: dw/dt = -spinDamping * w
        /// for the component of the angular velocity along the body axis. 0 (default) leaves the
        /// spin alone; 1 halves it in about 0.7 s. Independent of the inertia of the body, solved
        /// implicitly so any value is stable. Rotation about the other axes is not touched, so it
        /// does not interfere with the righting spring; use RigidBody::setAngularDamping() to damp
        /// all axes instead.
        decimal spinDamping;

        // -------------------- Methods -------------------- //

        /// Constructor
        UprightConstraintSettings()
            : localAxis(0, 1, 0), worldAxis(0, 1, 0), maxAngle(decimal(30.0) * PI_RP3D / decimal(180.0)), spring(),
              spinDamping(decimal(0.0)) {}
};

// Class UprightConstraint
/**
 * Keeps one axis of a rigid body within a cone around a fixed world direction, leaving rotation
 * about that axis (yaw) free. Solved as an AngularAxisConstraintPart between the body and the
 * world about the axis that would tilt the body back, so it only ever rights the body and never
 * fights its yaw.
 *
 * Two uses: an arcade safety net (a hard cone that stops a car or a ship from rolling over) or a
 * self-righting spring (a soft constraint with maxAngle 0 that lets a ship bank under thrust and
 * settle back upright). Because the reference direction is explicit, it works without gravity.
 *
 * Create with PhysicsWorld::createUprightConstraint(), destroy with destroyUprightConstraint().
 * Destroying the body destroys the constraint too.
 */
class UprightConstraint {

    private :

        // -------------------- Attributes -------------------- //

        /// Reference to the physics world
        PhysicsWorld& mWorld;

        /// The constrained body
        RigidBody* mBody;

        /// Settings
        UprightConstraintSettings mSettings;

        /// Cosine of the max angle
        decimal mCosMaxAngle;

        /// Current angle (rad) between the body axis and the world axis (as of the last step)
        decimal mCurrentAngle;

        /// Axis (world space) about which a positive rotation tilts the body back towards the world axis
        Vector3 mRotationAxis;

        /// The angular constraint doing the work
        AngularAxisConstraintPart mPart;

        /// Body axis in world space (as of the last step), the axis the spin damping acts about
        Vector3 mSpinAxis;

        /// The optional damper on the spin about the body axis
        AngularAxisConstraintPart mSpinDampingPart;

        /// Index of the body in the rigid body components (valid for the current step only)
        uint32 mBodyComponentIndex;

        /// True if the constraint is being solved this step (body dynamic and awake)
        bool mIsActiveThisStep;

        // -------------------- Methods -------------------- //

        /// Constructor
        UprightConstraint(PhysicsWorld& world, RigidBody* body, const UprightConstraintSettings& settings);

        /// Destructor
        ~UprightConstraint() = default;

    public :

        // -------------------- Methods -------------------- //

        /// Deleted copy-constructor
        UprightConstraint(const UprightConstraint& constraint) = delete;

        /// Deleted assignment operator
        UprightConstraint& operator=(const UprightConstraint& constraint) = delete;

        /// Return the constrained body
        RigidBody* getBody() const;

        /// Return the body axis (local space) that must stay up
        const Vector3& getLocalAxis() const;

        /// Set the body axis (local space) that must stay up
        void setLocalAxis(const Vector3& localAxis);

        /// Return the world direction the body axis must stay close to
        const Vector3& getWorldAxis() const;

        /// Set the world direction the body axis must stay close to
        void setWorldAxis(const Vector3& worldAxis);

        /// Return the half angle (rad) of the allowed cone
        decimal getMaxAngle() const;

        /// Set the half angle (rad) of the allowed cone (PI disables the constraint)
        void setMaxAngle(decimal maxAngle);

        /// Return the spring settings
        const SpringSettings& getSpringSettings() const;

        /// Set the spring settings (default-constructed for a hard cone)
        void setSpringSettings(const SpringSettings& spring);

        /// Return the spin damping rate (1/s)
        decimal getSpinDamping() const;

        /// Set the spin damping rate (1/s, >= 0, 0 for none)
        void setSpinDamping(decimal spinDamping);

        /// Return the current angle (rad) between the body axis and the world axis
        decimal getCurrentAngle() const;

        /// Return true if the constraint applied a righting impulse this step (the body is outside the cone)
        bool isActive() const;

        /// Return the torque (N.m) applied to the body this step to right it, in world space
        Vector3 getReactionTorque(decimal timeStep) const;

        /// Return the torque (N.m) the spin damping applied to the body this step, in world space
        Vector3 getSpinDampingTorque(decimal timeStep) const;

        // -------------------- Friendship -------------------- //

        friend class PhysicsWorld;
        friend class SolveUprightConstraintSystem;
};

// Return the constrained body
RP3D_FORCE_INLINE RigidBody* UprightConstraint::getBody() const {
    return mBody;
}

// Return the body axis that must stay up
RP3D_FORCE_INLINE const Vector3& UprightConstraint::getLocalAxis() const {
    return mSettings.localAxis;
}

// Set the body axis that must stay up
RP3D_FORCE_INLINE void UprightConstraint::setLocalAxis(const Vector3& localAxis) {
    mSettings.localAxis = localAxis.getUnit();
}

// Return the world direction the body axis must stay close to
RP3D_FORCE_INLINE const Vector3& UprightConstraint::getWorldAxis() const {
    return mSettings.worldAxis;
}

// Set the world direction the body axis must stay close to
RP3D_FORCE_INLINE void UprightConstraint::setWorldAxis(const Vector3& worldAxis) {
    mSettings.worldAxis = worldAxis.getUnit();
}

// Return the half angle of the allowed cone
RP3D_FORCE_INLINE decimal UprightConstraint::getMaxAngle() const {
    return mSettings.maxAngle;
}

// Set the half angle of the allowed cone
RP3D_FORCE_INLINE void UprightConstraint::setMaxAngle(decimal maxAngle) {
    mSettings.maxAngle = clamp(maxAngle, decimal(0.0), PI_RP3D);
    mCosMaxAngle = std::cos(mSettings.maxAngle);
}

// Return the spring settings
RP3D_FORCE_INLINE const SpringSettings& UprightConstraint::getSpringSettings() const {
    return mSettings.spring;
}

// Set the spring settings
RP3D_FORCE_INLINE void UprightConstraint::setSpringSettings(const SpringSettings& spring) {
    mSettings.spring = spring;
}

// Return the spin damping rate
RP3D_FORCE_INLINE decimal UprightConstraint::getSpinDamping() const {
    return mSettings.spinDamping;
}

// Set the spin damping rate
RP3D_FORCE_INLINE void UprightConstraint::setSpinDamping(decimal spinDamping) {
    mSettings.spinDamping = std::max(decimal(0.0), spinDamping);
}

// Return the current angle between the body axis and the world axis
RP3D_FORCE_INLINE decimal UprightConstraint::getCurrentAngle() const {
    return mCurrentAngle;
}

// Return true if the constraint applied an impulse this step
RP3D_FORCE_INLINE bool UprightConstraint::isActive() const {
    return mPart.isActive();
}

}

#endif
