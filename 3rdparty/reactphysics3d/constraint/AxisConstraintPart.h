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

#ifndef REACTPHYSICS3D_AXIS_CONSTRAINT_PART_H
#define REACTPHYSICS3D_AXIS_CONSTRAINT_PART_H

// Libraries
#include <reactphysics3d/configuration.h>
#include <reactphysics3d/mathematics/mathematics.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <cassert>

/// ReactPhysics3D namespace
namespace reactphysics3d {

// Structure AxisConstraintBody
/**
 * What an AxisConstraintPart needs to know about one of its two bodies while solving: where
 * the (constrained) velocities live and the mass properties. It only ever reads the mass
 * properties and reads/writes the two velocities through the pointers.
 *
 * For a STATIC or KINEMATIC body pass a zero inverse mass and a zero inverse inertia tensor:
 * the part then never changes the velocity of that body, exactly like the joint solvers do.
 */
struct AxisConstraintBody {

    public :

        // -------------------- Attributes -------------------- //

        /// Linear velocity of the body (read and written)
        Vector3* linearVelocity;

        /// Angular velocity of the body (read and written)
        Vector3* angularVelocity;

        /// Inverse mass of the body (0 for a static or kinematic body)
        decimal inverseMass;

        /// Inverse inertia tensor of the body in world space (zero for a static or kinematic body)
        const Matrix3x3* inverseInertiaTensorWorld;

        /// Per-axis factor (0 or 1) applied to linear velocity changes (RigidBody::setLinearLockAxisFactor)
        Vector3 linearLockAxisFactor;

        /// Per-axis factor (0 or 1) applied to angular velocity changes (RigidBody::setAngularLockAxisFactor)
        Vector3 angularLockAxisFactor;

        // -------------------- Methods -------------------- //

        /// Constructor
        AxisConstraintBody(Vector3* linearVelocityPtr, Vector3* angularVelocityPtr, decimal bodyInverseMass,
                           const Matrix3x3* bodyInverseInertiaTensorWorld,
                           const Vector3& linearLock = Vector3(1, 1, 1), const Vector3& angularLock = Vector3(1, 1, 1))
            : linearVelocity(linearVelocityPtr), angularVelocity(angularVelocityPtr), inverseMass(bodyInverseMass),
              inverseInertiaTensorWorld(bodyInverseInertiaTensorWorld), linearLockAxisFactor(linearLock),
              angularLockAxisFactor(angularLock) {}
};

// Class AxisConstraintPart
/**
 * One scalar constraint between two bodies along a world-space axis n: it controls the
 * relative velocity of a point p1 on body 1 and a point p2 on body 2 along n. This is the
 * building block that a joint limit, a joint motor, a wheel suspension spring or a tire
 * friction force all reduce to; keeping it in one place means the Jacobian, the effective
 * mass, the accumulated-impulse clamping and the soft (spring) variant are written and
 * tested once.
 *
 * Constraint (position level, only needed for the spring variant):
 *
 *      C = (p2 - p1) . n  -  restLength
 *
 * Velocity level (the Jacobian J applied to v = [v1 w1 v2 w2]):
 *
 *      J v = n . (v2 - v1) + (r2 x n) . w2 - (r1 x n) . w1
 *
 * with r1 = p1 - x1 and r2 = p2 - x2 the lever arms from the centre of mass of each body. A
 * positive impulse lambda pushes body 2 along +n and body 1 along -n, i.e. it increases C.
 *
 * Effective mass:
 *
 *      K = J M^-1 J^T = 1/m1 + 1/m2 + (r1 x n) . I1^-1 (r1 x n) + (r2 x n) . I2^-1 (r2 x n)
 *
 * HARD constraint: each solver iteration applies  lambda = -K^-1 (J v + b)  with b an
 * optional velocity bias (Baumgarte position correction), accumulating lambda and clamping
 * the accumulated value to [minLambda, maxLambda] (e.g. [0, inf) for a one-sided limit or
 * [-mu N, mu N] for friction).
 *
 * SOFT constraint (spring F = -k x - c v, solved implicitly so it is stable for any k, c and
 * time step, after "Soft Constraints: Reinventing The Spring", Erin Catto, GDC 2011): the
 * spring is folded into the constraint as a softness gamma and a bias, with
 *
 *      gamma  = 1 / (dt * (c + dt * k))            (impulse units, hence the 1/dt)
 *      bias   = dt * k * gamma * C
 *      lambda = -(K + gamma)^-1 (J v + bias + gamma * lambda_accumulated)
 *
 * In SpringMode::FREQUENCY_AND_DAMPING_RATIO, k and c are derived from K^-1, the effective
 * mass of the system actually being constrained (k = m_eff omega^2, c = 2 m_eff zeta omega),
 * so a frequency of 1 Hz oscillates at 1 Hz regardless of the masses and lever arms.
 *
 * Note that implicit integration adds numerical damping of its own, so a damping ratio of 0
 * does not give a perfectly undamped oscillation.
 *
 * This class is meant for use inside the constraint solver, where the velocities it sees are
 * the constrained velocities of the solver. It has no knowledge of bodies or entities: callers
 * describe the two bodies with an AxisConstraintBody each.
 */
class AxisConstraintPart {

    private :

        // -------------------- Attributes -------------------- //

        /// (r1 x n), the angular part of the Jacobian for body 1
        Vector3 mR1CrossAxis;

        /// (r2 x n), the angular part of the Jacobian for body 2
        Vector3 mR2CrossAxis;

        /// I1^-1 (r1 x n), the angular velocity change of body 1 per unit impulse
        Vector3 mInvI1R1CrossAxis;

        /// I2^-1 (r2 x n), the angular velocity change of body 2 per unit impulse
        Vector3 mInvI2R2CrossAxis;

        /// (K + gamma)^-1. Zero means the part is inactive (both bodies static, or deactivated)
        decimal mEffectiveMass;

        /// Softness gamma of the spring (zero for a hard constraint)
        decimal mSoftness;

        /// Velocity bias b (position correction, plus the bias of the spring)
        decimal mBias;

        /// Accumulated impulse lambda (N.s) applied so far (kept across steps for warm starting)
        decimal mTotalLambda;

        // -------------------- Methods -------------------- //

        /// Compute the inverse effective mass K and cache the cross products used by the Jacobian
        decimal computeInverseEffectiveMass(const AxisConstraintBody& body1, const Vector3& r1,
                                            const AxisConstraintBody& body2, const Vector3& r2,
                                            const Vector3& axis);

        /// Apply the impulse P = J^T lambda to the two bodies. Return true if anything was applied.
        bool applyImpulse(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                          const Vector3& axis, decimal lambda) const;

        /// Set the effective mass, softness and bias from a spring given as stiffness and damping
        void setSpringProperties(decimal timeStep, decimal inverseEffectiveMass, decimal positionError,
                                 decimal stiffness, decimal damping, decimal bias);

    public :

        // -------------------- Methods -------------------- //

        /// Constructor (inactive part)
        AxisConstraintPart();

        /// Make the part inactive: solving it does nothing. Also forgets the accumulated impulse.
        void deactivate();

        /// Return true if the part is active (solving it can apply impulses)
        bool isActive() const;

        /// Return true if the part is currently a soft (spring) constraint
        bool isSoft() const;

        /// Prepare a HARD constraint. Call once per step before solving.
        /// @param body1 First body
        /// @param r1 Lever arm from the centre of mass of body 1 to the constrained point p1 (world space)
        /// @param body2 Second body
        /// @param r2 Lever arm from the centre of mass of body 2 to the constrained point p2 (world space)
        /// @param axis Unit constraint axis n (world space)
        /// @param bias Velocity bias b, e.g. beta/dt * C for Baumgarte position correction (default 0)
        void computeConstraintProperties(const AxisConstraintBody& body1, const Vector3& r1,
                                         const AxisConstraintBody& body2, const Vector3& r2,
                                         const Vector3& axis, decimal bias = decimal(0.0));

        /// Prepare a SOFT (spring) constraint. Call once per step before solving. Falls back to a
        /// hard constraint if the settings have neither stiffness nor damping.
        /// @param timeStep Time step of the simulation
        /// @param body1 First body
        /// @param r1 Lever arm from the centre of mass of body 1 to the constrained point p1 (world space)
        /// @param body2 Second body
        /// @param r2 Lever arm from the centre of mass of body 2 to the constrained point p2 (world space)
        /// @param axis Unit constraint axis n (world space)
        /// @param positionError Current value of C = (p2 - p1) . n - restLength (positive = stretched)
        /// @param spring The spring settings
        /// @param bias Extra velocity bias b added on top of the bias of the spring (default 0)
        void computeSpringConstraintProperties(decimal timeStep, const AxisConstraintBody& body1, const Vector3& r1,
                                               const AxisConstraintBody& body2, const Vector3& r2,
                                               const Vector3& axis, decimal positionError,
                                               const SpringSettings& spring, decimal bias = decimal(0.0));

        /// Forget the accumulated impulse (call at the start of a step when warm starting is off)
        void resetTotalLambda();

        /// Apply the accumulated impulse of the previous step (warm starting). Call once per step
        /// after compute*Properties and before the first solveVelocityConstraint. `ratio` scales
        /// the carried-over impulse first (1 = keep all of it). Below 1 it lets an impulse the
        /// solver can no longer see decay away: several hard constraints on one body can hold an
        /// equal-and-opposite set of impulses that produce no net velocity error at all, which a
        /// full warm start would otherwise carry forward unchanged for ever.
        void warmStart(const AxisConstraintBody& body1, const AxisConstraintBody& body2, const Vector3& axis,
                       decimal ratio = decimal(1.0));

        /// One solver iteration: compute the impulse that makes J v + b = 0 (or the soft equivalent),
        /// clamp the ACCUMULATED impulse to [minLambda, maxLambda] and apply the difference.
        /// Return true if a nonzero impulse was applied.
        bool solveVelocityConstraint(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                     const Vector3& axis, decimal minLambda, decimal maxLambda);

        /// Return the accumulated impulse (N.s) applied by this part this step
        decimal getTotalLambda() const;

        /// Overwrite the accumulated impulse (e.g. to share one budget between several parts)
        void setTotalLambda(decimal totalLambda);

        /// Return (K + gamma)^-1, the effective mass the solver uses (zero if inactive)
        decimal getEffectiveMass() const;
};

// Constructor
RP3D_FORCE_INLINE AxisConstraintPart::AxisConstraintPart()
    : mEffectiveMass(decimal(0.0)), mSoftness(decimal(0.0)), mBias(decimal(0.0)), mTotalLambda(decimal(0.0)) {

}

// Make the part inactive
RP3D_FORCE_INLINE void AxisConstraintPart::deactivate() {
    mEffectiveMass = decimal(0.0);
    mSoftness = decimal(0.0);
    mBias = decimal(0.0);
    mTotalLambda = decimal(0.0);
}

// Return true if the part is active
RP3D_FORCE_INLINE bool AxisConstraintPart::isActive() const {
    return mEffectiveMass != decimal(0.0);
}

// Return true if the part is currently a soft (spring) constraint
RP3D_FORCE_INLINE bool AxisConstraintPart::isSoft() const {
    return mSoftness != decimal(0.0);
}

// Compute the inverse effective mass K and cache the cross products used by the Jacobian
RP3D_FORCE_INLINE decimal AxisConstraintPart::computeInverseEffectiveMass(const AxisConstraintBody& body1, const Vector3& r1,
                                                                           const AxisConstraintBody& body2, const Vector3& r2,
                                                                           const Vector3& axis) {
    assert(approxEqual(axis.lengthSquare(), decimal(1.0), decimal(0.0001)));

    mR1CrossAxis = r1.cross(axis);
    mR2CrossAxis = r2.cross(axis);
    mInvI1R1CrossAxis = (*body1.inverseInertiaTensorWorld) * mR1CrossAxis;
    mInvI2R2CrossAxis = (*body2.inverseInertiaTensorWorld) * mR2CrossAxis;

    return body1.inverseMass + body2.inverseMass +
           mR1CrossAxis.dot(mInvI1R1CrossAxis) + mR2CrossAxis.dot(mInvI2R2CrossAxis);
}

// Apply the impulse P = J^T lambda to the two bodies
RP3D_FORCE_INLINE bool AxisConstraintPart::applyImpulse(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                        const Vector3& axis, decimal lambda) const {
    if (lambda == decimal(0.0)) return false;

    // Body 1 is pushed along -n, body 2 along +n
    *body1.linearVelocity -= body1.inverseMass * body1.linearLockAxisFactor * (lambda * axis);
    *body1.angularVelocity -= body1.angularLockAxisFactor * (lambda * mInvI1R1CrossAxis);
    *body2.linearVelocity += body2.inverseMass * body2.linearLockAxisFactor * (lambda * axis);
    *body2.angularVelocity += body2.angularLockAxisFactor * (lambda * mInvI2R2CrossAxis);

    return true;
}

// Set the effective mass, softness and bias from a spring given as stiffness and damping
RP3D_FORCE_INLINE void AxisConstraintPart::setSpringProperties(decimal timeStep, decimal inverseEffectiveMass, decimal positionError,
                                                               decimal stiffness, decimal damping, decimal bias) {
    assert(stiffness > decimal(0.0) || damping > decimal(0.0));

    // Softness gamma = 1 / (dt * (c + dt * k)). The 1/dt is there because we work with
    // impulses rather than forces.
    mSoftness = decimal(1.0) / (timeStep * (damping + timeStep * stiffness));

    // Position correction of the spring: beta = dt * k / (c + dt * k) = dt * k^2 * gamma,
    // and the velocity bias is beta / dt * C = dt * k * gamma * C
    mBias = bias + timeStep * stiffness * mSoftness * positionError;

    // The softness acts like extra inverse mass: (K + gamma)^-1
    mEffectiveMass = decimal(1.0) / (inverseEffectiveMass + mSoftness);
}

// Prepare a HARD constraint
RP3D_FORCE_INLINE void AxisConstraintPart::computeConstraintProperties(const AxisConstraintBody& body1, const Vector3& r1,
                                                                       const AxisConstraintBody& body2, const Vector3& r2,
                                                                       const Vector3& axis, decimal bias) {
    const decimal inverseEffectiveMass = computeInverseEffectiveMass(body1, r1, body2, r2, axis);
    if (inverseEffectiveMass <= MACHINE_EPSILON) {
        // Neither body can move along this axis: nothing to solve
        deactivate();
        return;
    }

    mEffectiveMass = decimal(1.0) / inverseEffectiveMass;
    mSoftness = decimal(0.0);
    mBias = bias;
}

// Prepare a SOFT (spring) constraint
RP3D_FORCE_INLINE void AxisConstraintPart::computeSpringConstraintProperties(decimal timeStep, const AxisConstraintBody& body1, const Vector3& r1,
                                                                             const AxisConstraintBody& body2, const Vector3& r2,
                                                                             const Vector3& axis, decimal positionError,
                                                                             const SpringSettings& spring, decimal bias) {
    assert(timeStep > decimal(0.0));

    if (!spring.isSoft()) {
        computeConstraintProperties(body1, r1, body2, r2, axis, bias);
        return;
    }

    const decimal inverseEffectiveMass = computeInverseEffectiveMass(body1, r1, body2, r2, axis);
    if (inverseEffectiveMass <= MACHINE_EPSILON) {
        deactivate();
        return;
    }

    decimal stiffness, damping;
    if (spring.mode == SpringMode::FREQUENCY_AND_DAMPING_RATIO) {

        // k = m_eff omega^2 and c = 2 m_eff zeta omega, with m_eff = K^-1 the effective mass
        // of the system that is actually being constrained (not just the mass of one body)
        const decimal effectiveMass = decimal(1.0) / inverseEffectiveMass;
        const decimal omega = decimal(2.0) * PI_RP3D * spring.frequencyOrStiffness;
        stiffness = effectiveMass * omega * omega;
        damping = decimal(2.0) * effectiveMass * spring.damping * omega;
    }
    else {
        stiffness = spring.frequencyOrStiffness;
        damping = spring.damping;
    }

    setSpringProperties(timeStep, inverseEffectiveMass, positionError, stiffness, damping, bias);
}

// Forget the accumulated impulse
RP3D_FORCE_INLINE void AxisConstraintPart::resetTotalLambda() {
    mTotalLambda = decimal(0.0);
}

// Apply the accumulated impulse of the previous step (warm starting)
RP3D_FORCE_INLINE void AxisConstraintPart::warmStart(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                     const Vector3& axis, decimal ratio) {
    if (!isActive()) return;
    mTotalLambda *= ratio;
    applyImpulse(body1, body2, axis, mTotalLambda);
}

// One solver iteration
RP3D_FORCE_INLINE bool AxisConstraintPart::solveVelocityConstraint(const AxisConstraintBody& body1, const AxisConstraintBody& body2,
                                                                   const Vector3& axis, decimal minLambda, decimal maxLambda) {
    if (!isActive()) return false;

    // J v = n . (v2 - v1) + (r2 x n) . w2 - (r1 x n) . w1
    const decimal Jv = axis.dot(*body2.linearVelocity - *body1.linearVelocity) +
                       mR2CrossAxis.dot(*body2.angularVelocity) - mR1CrossAxis.dot(*body1.angularVelocity);

    // lambda = -(K + gamma)^-1 (J v + b + gamma * lambda_accumulated)
    decimal deltaLambda = mEffectiveMass * (-Jv - mBias - mSoftness * mTotalLambda);

    // Clamp the accumulated impulse, not the increment
    const decimal newTotalLambda = clamp(mTotalLambda + deltaLambda, minLambda, maxLambda);
    deltaLambda = newTotalLambda - mTotalLambda;
    mTotalLambda = newTotalLambda;

    return applyImpulse(body1, body2, axis, deltaLambda);
}

// Return the accumulated impulse
RP3D_FORCE_INLINE decimal AxisConstraintPart::getTotalLambda() const {
    return mTotalLambda;
}

// Overwrite the accumulated impulse
RP3D_FORCE_INLINE void AxisConstraintPart::setTotalLambda(decimal totalLambda) {
    mTotalLambda = totalLambda;
}

// Return the effective mass the solver uses
RP3D_FORCE_INLINE decimal AxisConstraintPart::getEffectiveMass() const {
    return mEffectiveMass;
}

}

#endif
