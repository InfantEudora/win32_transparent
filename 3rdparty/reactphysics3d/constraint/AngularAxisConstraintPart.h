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

#ifndef REACTPHYSICS3D_ANGULAR_AXIS_CONSTRAINT_PART_H
#define REACTPHYSICS3D_ANGULAR_AXIS_CONSTRAINT_PART_H

// Libraries
#include <reactphysics3d/configuration.h>
#include <reactphysics3d/mathematics/mathematics.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>
#include <cassert>

/// ReactPhysics3D namespace
namespace reactphysics3d {

// Class AngularAxisConstraintPart
/**
 * The angular sibling of AxisConstraintPart: one scalar constraint on the RELATIVE ANGULAR
 * velocity of two bodies about a world-space axis n. A hinge limit, a hinge motor, a twist
 * limit or a body kept upright all reduce to this.
 *
 * Velocity level (the Jacobian J applied to v = [w1 w2]):
 *
 *      J v = n . (w2 - w1)
 *
 * A positive angular impulse lambda (N.m.s) spins body 2 about +n and body 1 about -n.
 *
 * Effective mass:
 *
 *      K = J M^-1 J^T = n . I1^-1 n + n . I2^-1 n
 *
 * HARD and SOFT (spring) variants exactly as in AxisConstraintPart, with the position error C
 * an angle (rad) and the spring a torsion spring T = -k theta - c w. In
 * SpringMode::FREQUENCY_AND_DAMPING_RATIO the coefficients are derived from K^-1, the
 * effective inertia of the constrained motion.
 *
 * The bodies are described with AxisConstraintBody views; only their angular velocity, inverse
 * inertia tensor and angular lock factors are used. A body that must not move (the world) is a
 * view with a zero inverse inertia tensor.
 */
class AngularAxisConstraintPart {

    private :

        // -------------------- Attributes -------------------- //

        /// The constraint axis n (world space, unit)
        Vector3 mAxis;

        /// I1^-1 n, the angular velocity change of body 1 per unit impulse
        Vector3 mInvI1Axis;

        /// I2^-1 n, the angular velocity change of body 2 per unit impulse
        Vector3 mInvI2Axis;

        /// (K + gamma)^-1. Zero means the part is inactive
        decimal mEffectiveMass;

        /// Softness gamma of the spring (zero for a hard constraint)
        decimal mSoftness;

        /// Velocity bias b
        decimal mBias;

        /// Accumulated impulse lambda (N.m.s) applied so far
        decimal mTotalLambda;

        // -------------------- Methods -------------------- //

        /// Compute the inverse effective mass K and cache the inertia products
        decimal computeInverseEffectiveMass(const AxisConstraintBody& body1, const AxisConstraintBody& body2, const Vector3& axis);

        /// Apply the angular impulse P = J^T lambda to the two bodies
        bool applyImpulse(const AxisConstraintBody& body1, const AxisConstraintBody& body2, decimal lambda) const;

        /// Set the effective mass, softness and bias from a spring given as stiffness and damping
        void setSpringProperties(decimal timeStep, decimal inverseEffectiveMass, decimal positionError,
                                 decimal stiffness, decimal damping, decimal bias);

    public :

        // -------------------- Methods -------------------- //

        /// Constructor (inactive part)
        AngularAxisConstraintPart();

        /// Make the part inactive. Also forgets the accumulated impulse.
        void deactivate();

        /// Return true if the part is active
        bool isActive() const;

        /// Return true if the part is currently a soft (spring) constraint
        bool isSoft() const;

        /// Prepare a HARD constraint about the unit axis n (world space), with an optional velocity bias
        void computeConstraintProperties(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                         const Vector3& axis, decimal bias = decimal(0.0));

        /// Prepare a SOFT (torsion spring) constraint about the unit axis n (world space).
        /// positionError is the angle error C (rad, positive when the bodies are turned past the
        /// rest angle about +n). Falls back to a hard constraint for hard spring settings.
        void computeSpringConstraintProperties(decimal timeStep, const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                               const Vector3& axis, decimal positionError, const SpringSettings& spring,
                                               decimal bias = decimal(0.0));

        /// Forget the accumulated impulse
        void resetTotalLambda();

        /// Apply the accumulated impulse of the previous step (warm starting)
        void warmStart(const AxisConstraintBody& body1, const AxisConstraintBody& body2) const;

        /// One solver iteration: clamp the accumulated impulse to [minLambda, maxLambda] and apply the difference
        bool solveVelocityConstraint(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                     decimal minLambda, decimal maxLambda);

        /// Return the accumulated impulse (N.m.s) applied by this part this step
        decimal getTotalLambda() const;

        /// Overwrite the accumulated impulse
        void setTotalLambda(decimal totalLambda);

        /// Return (K + gamma)^-1 (zero if inactive)
        decimal getEffectiveMass() const;
};

// Constructor
RP3D_FORCE_INLINE AngularAxisConstraintPart::AngularAxisConstraintPart()
    : mEffectiveMass(decimal(0.0)), mSoftness(decimal(0.0)), mBias(decimal(0.0)), mTotalLambda(decimal(0.0)) {

}

// Make the part inactive
RP3D_FORCE_INLINE void AngularAxisConstraintPart::deactivate() {
    mEffectiveMass = decimal(0.0);
    mSoftness = decimal(0.0);
    mBias = decimal(0.0);
    mTotalLambda = decimal(0.0);
}

// Return true if the part is active
RP3D_FORCE_INLINE bool AngularAxisConstraintPart::isActive() const {
    return mEffectiveMass != decimal(0.0);
}

// Return true if the part is currently a soft (spring) constraint
RP3D_FORCE_INLINE bool AngularAxisConstraintPart::isSoft() const {
    return mSoftness != decimal(0.0);
}

// Compute the inverse effective mass K and cache the inertia products
RP3D_FORCE_INLINE decimal AngularAxisConstraintPart::computeInverseEffectiveMass(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                                                  const Vector3& axis) {
    assert(approxEqual(axis.lengthSquare(), decimal(1.0), decimal(0.0001)));

    mAxis = axis;
    mInvI1Axis = (*body1.inverseInertiaTensorWorld) * axis;
    mInvI2Axis = (*body2.inverseInertiaTensorWorld) * axis;

    return axis.dot(mInvI1Axis) + axis.dot(mInvI2Axis);
}

// Apply the angular impulse P = J^T lambda to the two bodies
RP3D_FORCE_INLINE bool AngularAxisConstraintPart::applyImpulse(const AxisConstraintBody& body1, const AxisConstraintBody& body2, decimal lambda) const {
    if (lambda == decimal(0.0)) return false;

    // Body 1 spins about -n, body 2 about +n
    *body1.angularVelocity -= body1.angularLockAxisFactor * (lambda * mInvI1Axis);
    *body2.angularVelocity += body2.angularLockAxisFactor * (lambda * mInvI2Axis);

    return true;
}

// Set the effective mass, softness and bias from a spring given as stiffness and damping
RP3D_FORCE_INLINE void AngularAxisConstraintPart::setSpringProperties(decimal timeStep, decimal inverseEffectiveMass, decimal positionError,
                                                                      decimal stiffness, decimal damping, decimal bias) {
    assert(stiffness > decimal(0.0) || damping > decimal(0.0));

    // See AxisConstraintPart::setSpringProperties: gamma = 1 / (dt (c + dt k)), b = dt k gamma C
    mSoftness = decimal(1.0) / (timeStep * (damping + timeStep * stiffness));
    mBias = bias + timeStep * stiffness * mSoftness * positionError;
    mEffectiveMass = decimal(1.0) / (inverseEffectiveMass + mSoftness);
}

// Prepare a HARD constraint
RP3D_FORCE_INLINE void AngularAxisConstraintPart::computeConstraintProperties(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                                              const Vector3& axis, decimal bias) {
    const decimal inverseEffectiveMass = computeInverseEffectiveMass(body1, body2, axis);
    if (inverseEffectiveMass <= MACHINE_EPSILON) {
        deactivate();
        return;
    }

    mEffectiveMass = decimal(1.0) / inverseEffectiveMass;
    mSoftness = decimal(0.0);
    mBias = bias;
}

// Prepare a SOFT (torsion spring) constraint
RP3D_FORCE_INLINE void AngularAxisConstraintPart::computeSpringConstraintProperties(decimal timeStep, const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                                                    const Vector3& axis, decimal positionError, const SpringSettings& spring,
                                                                                    decimal bias) {
    assert(timeStep > decimal(0.0));

    if (!spring.isSoft()) {
        computeConstraintProperties(body1, body2, axis, bias);
        return;
    }

    const decimal inverseEffectiveMass = computeInverseEffectiveMass(body1, body2, axis);
    if (inverseEffectiveMass <= MACHINE_EPSILON) {
        deactivate();
        return;
    }

    decimal stiffness, damping;
    if (spring.mode == SpringMode::FREQUENCY_AND_DAMPING_RATIO) {

        // k = I_eff omega^2 and c = 2 I_eff zeta omega, with I_eff = K^-1 the effective inertia
        const decimal effectiveInertia = decimal(1.0) / inverseEffectiveMass;
        const decimal omega = decimal(2.0) * PI_RP3D * spring.frequencyOrStiffness;
        stiffness = effectiveInertia * omega * omega;
        damping = decimal(2.0) * effectiveInertia * spring.damping * omega;
    }
    else {
        stiffness = spring.frequencyOrStiffness;
        damping = spring.damping;
    }

    setSpringProperties(timeStep, inverseEffectiveMass, positionError, stiffness, damping, bias);
}

// Forget the accumulated impulse
RP3D_FORCE_INLINE void AngularAxisConstraintPart::resetTotalLambda() {
    mTotalLambda = decimal(0.0);
}

// Apply the accumulated impulse of the previous step (warm starting)
RP3D_FORCE_INLINE void AngularAxisConstraintPart::warmStart(const AxisConstraintBody& body1, const AxisConstraintBody& body2) const {
    if (!isActive()) return;
    applyImpulse(body1, body2, mTotalLambda);
}

// One solver iteration
RP3D_FORCE_INLINE bool AngularAxisConstraintPart::solveVelocityConstraint(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                                          decimal minLambda, decimal maxLambda) {
    if (!isActive()) return false;

    // J v = n . (w2 - w1)
    const decimal Jv = mAxis.dot(*body2.angularVelocity - *body1.angularVelocity);

    // lambda = -(K + gamma)^-1 (J v + b + gamma * lambda_accumulated)
    decimal deltaLambda = mEffectiveMass * (-Jv - mBias - mSoftness * mTotalLambda);

    const decimal newTotalLambda = clamp(mTotalLambda + deltaLambda, minLambda, maxLambda);
    deltaLambda = newTotalLambda - mTotalLambda;
    mTotalLambda = newTotalLambda;

    return applyImpulse(body1, body2, deltaLambda);
}

// Return the accumulated impulse
RP3D_FORCE_INLINE decimal AngularAxisConstraintPart::getTotalLambda() const {
    return mTotalLambda;
}

// Overwrite the accumulated impulse
RP3D_FORCE_INLINE void AngularAxisConstraintPart::setTotalLambda(decimal totalLambda) {
    mTotalLambda = totalLambda;
}

// Return the effective mass the solver uses
RP3D_FORCE_INLINE decimal AngularAxisConstraintPart::getEffectiveMass() const {
    return mEffectiveMass;
}

}

#endif
