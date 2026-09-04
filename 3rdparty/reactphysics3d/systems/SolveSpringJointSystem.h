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

#ifndef REACTPHYSICS3D_SOLVE_SPRING_JOINT_SYSTEM_H
#define REACTPHYSICS3D_SOLVE_SPRING_JOINT_SYSTEM_H

// Libraries
#include <reactphysics3d/utils/Profiler.h>
#include <reactphysics3d/components/RigidBodyComponents.h>
#include <reactphysics3d/components/JointComponents.h>
#include <reactphysics3d/components/SpringJointComponents.h>
#include <reactphysics3d/components/TransformComponents.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>

namespace reactphysics3d {

class PhysicsWorld;

// Class SolveSpringJointSystem
/**
 * This class is responsible to solve the SpringJoint constraints. Each joint is a single
 * AxisConstraintPart along the line between its two anchors; this system only computes the
 * per-step geometry (lever arms, axis, current length) and hands the rest to the part.
 */
class SolveSpringJointSystem {

    private :

        // -------------------- Constants -------------------- //

        // Beta value for the bias factor of position correction (hard springs, Baumgarte only)
        static const decimal BETA;

        // -------------------- Attributes -------------------- //

        /// Physics world
        PhysicsWorld& mWorld;

        /// Reference to the rigid body components
        RigidBodyComponents& mRigidBodyComponents;

        /// Reference to transform components
        TransformComponents& mTransformComponents;

        /// Reference to the joint components
        JointComponents& mJointComponents;

        /// Reference to the spring joint components
        SpringJointComponents& mSpringJointComponents;

        /// Current time step of the simulation
        decimal mTimeStep;

        /// True if warm starting of the solver is active
        bool mIsWarmStartingActive;

#ifdef IS_RP3D_PROFILING_ENABLED

        /// Pointer to the profiler
        Profiler* mProfiler;
#endif

        // -------------------- Methods -------------------- //

        /// Build the view of a body the AxisConstraintPart solves against (constrained velocities)
        AxisConstraintBody makeConstraintBody(uint32 componentIndexBody, const Matrix3x3* inverseInertiaTensorWorld);

    public :

        // -------------------- Methods -------------------- //

        /// Constructor
        SolveSpringJointSystem(PhysicsWorld& world, RigidBodyComponents& rigidBodyComponents,
                               TransformComponents& transformComponents,
                               JointComponents& jointComponents,
                               SpringJointComponents& springJointComponents);

        /// Destructor
        ~SolveSpringJointSystem() = default;

        /// Initialize before solving the constraint
        void initBeforeSolve();

        /// Warm start the constraint (apply the previous impulse at the beginning of the step)
        void warmstart();

        /// Solve the velocity constraint
        void solveVelocityConstraint();

        /// Solve the position constraint (for a rigid rod, i.e. a spring with no stiffness/damping)
        void solvePositionConstraint();

        /// Set the time step
        void setTimeStep(decimal timeStep);

        /// Set to true to enable warm starting
        void setIsWarmStartingActive(bool isWarmStartingActive);

#ifdef IS_RP3D_PROFILING_ENABLED

        /// Set the profiler
        void setProfiler(Profiler* profiler);

#endif

};

#ifdef IS_RP3D_PROFILING_ENABLED

// Set the profiler
RP3D_FORCE_INLINE void SolveSpringJointSystem::setProfiler(Profiler* profiler) {
    mProfiler = profiler;
}

#endif

// Set the time step
RP3D_FORCE_INLINE void SolveSpringJointSystem::setTimeStep(decimal timeStep) {
    assert(timeStep > decimal(0.0));
    mTimeStep = timeStep;
}

// Set to true to enable warm starting
RP3D_FORCE_INLINE void SolveSpringJointSystem::setIsWarmStartingActive(bool isWarmStartingActive) {
    mIsWarmStartingActive = isWarmStartingActive;
}

// Build the view of a body the AxisConstraintPart solves against
RP3D_FORCE_INLINE AxisConstraintBody SolveSpringJointSystem::makeConstraintBody(uint32 componentIndexBody, const Matrix3x3* inverseInertiaTensorWorld) {
    return AxisConstraintBody(&mRigidBodyComponents.mConstrainedLinearVelocities[componentIndexBody],
                              &mRigidBodyComponents.mConstrainedAngularVelocities[componentIndexBody],
                              mRigidBodyComponents.mInverseMasses[componentIndexBody],
                              inverseInertiaTensorWorld,
                              mRigidBodyComponents.mLinearLockAxisFactors[componentIndexBody],
                              mRigidBodyComponents.mAngularLockAxisFactors[componentIndexBody]);
}

}

#endif
