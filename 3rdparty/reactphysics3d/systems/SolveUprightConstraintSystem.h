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

#ifndef REACTPHYSICS3D_SOLVE_UPRIGHT_CONSTRAINT_SYSTEM_H
#define REACTPHYSICS3D_SOLVE_UPRIGHT_CONSTRAINT_SYSTEM_H

// Libraries
#include <reactphysics3d/utils/Profiler.h>
#include <reactphysics3d/components/RigidBodyComponents.h>
#include <reactphysics3d/components/TransformComponents.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>
#include <reactphysics3d/containers/Array.h>

namespace reactphysics3d {

class PhysicsWorld;
class UprightConstraint;

// Class SolveUprightConstraintSystem
/**
 * This class is responsible to solve the UprightConstraint constraints. Like the vehicles they
 * are single-body constraints against the world, kept in an array owned by the world rather
 * than as ECS components.
 */
class SolveUprightConstraintSystem {

    private :

        // -------------------- Constants -------------------- //

        /// A body within this angle (rad) of the cone edge is treated as being at the limit
        static const decimal LIMIT_TOLERANCE;

        // -------------------- Attributes -------------------- //

        /// Physics world
        PhysicsWorld& mWorld;

        /// Reference to the rigid body components
        RigidBodyComponents& mRigidBodyComponents;

        /// Reference to transform components
        TransformComponents& mTransformComponents;

        /// Reference to the upright constraints of the world
        Array<UprightConstraint*>& mConstraints;

        /// The world as the first body of every part: no inertia, no velocity
        Matrix3x3 mZeroInertia;
        Vector3 mZeroVelocity;

        /// Current time step of the simulation
        decimal mTimeStep;

        /// True if warm starting of the solver is active
        bool mIsWarmStartingActive;

#ifdef IS_RP3D_PROFILING_ENABLED

        /// Pointer to the profiler
        Profiler* mProfiler;
#endif

        // -------------------- Methods -------------------- //

        /// Angle between the body axis and the world axis, and the axis a positive rotation about
        /// which tilts the body back. Returns false if the body is upright (no righting axis).
        static bool computeTilt(const Quaternion& orientation, const Vector3& localAxis, const Vector3& worldAxis,
                                decimal& outAngle, Vector3& outRotationAxis);

        /// Build the view of the world the part solves against
        AxisConstraintBody makeWorldBody();

        /// Build the view of the constrained body
        AxisConstraintBody makeBody(const UprightConstraint& constraint);

    public :

        // -------------------- Methods -------------------- //

        /// Constructor
        SolveUprightConstraintSystem(PhysicsWorld& world, RigidBodyComponents& rigidBodyComponents,
                                     TransformComponents& transformComponents, Array<UprightConstraint*>& constraints);

        /// Destructor
        ~SolveUprightConstraintSystem() = default;

        /// Measure the tilt of each body and initialize the constraints before solving
        void initBeforeSolve();

        /// Warm start the constraints
        void warmstart();

        /// Solve the velocity constraints
        void solveVelocityConstraint();

        /// Solve the position constraints (hard cones only)
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
RP3D_FORCE_INLINE void SolveUprightConstraintSystem::setProfiler(Profiler* profiler) {
    mProfiler = profiler;
}

#endif

// Set the time step
RP3D_FORCE_INLINE void SolveUprightConstraintSystem::setTimeStep(decimal timeStep) {
    assert(timeStep > decimal(0.0));
    mTimeStep = timeStep;
}

// Set to true to enable warm starting
RP3D_FORCE_INLINE void SolveUprightConstraintSystem::setIsWarmStartingActive(bool isWarmStartingActive) {
    mIsWarmStartingActive = isWarmStartingActive;
}

}

#endif
