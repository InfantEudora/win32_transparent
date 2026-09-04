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

#ifndef REACTPHYSICS3D_SOLVE_VEHICLE_SYSTEM_H
#define REACTPHYSICS3D_SOLVE_VEHICLE_SYSTEM_H

// Libraries
#include <reactphysics3d/utils/Profiler.h>
#include <reactphysics3d/components/RigidBodyComponents.h>
#include <reactphysics3d/components/TransformComponents.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>
#include <reactphysics3d/containers/Array.h>

namespace reactphysics3d {

class PhysicsWorld;
class VehicleConstraint;
class VehicleWheel;

// Class SolveVehicleSystem
/**
 * This class is responsible to solve the VehicleConstraint constraints. At the start of a
 * step it casts the wheel rays to find the ground under each wheel and sets up the suspension,
 * hard stop and tire friction AxisConstraintParts per touching wheel; during the velocity
 * iterations it solves them, and in the position iterations it keeps fully compressed
 * suspensions from sinking through the ground.
 *
 * Unlike the joints, vehicles are not ECS components: the number of wheels varies per vehicle
 * and the second body of every wheel constraint (the ground) changes from step to step, so
 * the system simply walks the array of vehicles owned by the world.
 */
class SolveVehicleSystem {

    private :

        // -------------------- Attributes -------------------- //

        /// Physics world
        PhysicsWorld& mWorld;

        /// Reference to the rigid body components
        RigidBodyComponents& mRigidBodyComponents;

        /// Reference to transform components
        TransformComponents& mTransformComponents;

        /// Reference to the vehicles of the world
        Array<VehicleConstraint*>& mVehicles;

        /// Zero inertia tensor, for contact bodies that must not receive impulses
        Matrix3x3 mZeroInertia;

        /// Current time step of the simulation
        decimal mTimeStep;

        /// True if warm starting of the solver is active
        bool mIsWarmStartingActive;

#ifdef IS_RP3D_PROFILING_ENABLED

        /// Pointer to the profiler
        Profiler* mProfiler;
#endif

        // -------------------- Methods -------------------- //

        /// Find the ground under a wheel and set up its constraints
        void initWheel(VehicleConstraint& vehicle, VehicleWheel& wheel, const Transform& bodyTransform, const Vector3& worldUp);

        /// Solve the suspension spring and hard stop of the wheels of a vehicle
        void solveSuspension(VehicleConstraint& vehicle, const AxisConstraintBody& chassis);

        /// Solve the longitudinal tire friction of the wheels of a vehicle (and update their spin)
        void solveLongitudinalFriction(VehicleConstraint& vehicle, const AxisConstraintBody& chassis);

        /// Solve the lateral tire friction of the wheels of a vehicle
        void solveLateralFriction(VehicleConstraint& vehicle, const AxisConstraintBody& chassis);

        /// Build the view of the chassis the AxisConstraintPart solves against
        AxisConstraintBody makeChassisBody(const VehicleConstraint& vehicle);

        /// Build the view of the ground body under a wheel
        AxisConstraintBody makeGroundBody(const VehicleWheel& wheel);

    public :

        // -------------------- Methods -------------------- //

        /// Constructor
        SolveVehicleSystem(PhysicsWorld& world, RigidBodyComponents& rigidBodyComponents,
                           TransformComponents& transformComponents, Array<VehicleConstraint*>& vehicles);

        /// Destructor
        ~SolveVehicleSystem() = default;

        /// Cast the wheel rays and initialize the constraints before solving
        void initBeforeSolve();

        /// Warm start the constraints (apply the previous impulses at the beginning of the step)
        void warmstart();

        /// Solve the velocity constraints
        void solveVelocityConstraint();

        /// Solve the position constraints (hard stop of a fully compressed suspension)
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
RP3D_FORCE_INLINE void SolveVehicleSystem::setProfiler(Profiler* profiler) {
    mProfiler = profiler;
}

#endif

// Set the time step
RP3D_FORCE_INLINE void SolveVehicleSystem::setTimeStep(decimal timeStep) {
    assert(timeStep > decimal(0.0));
    mTimeStep = timeStep;
}

// Set to true to enable warm starting
RP3D_FORCE_INLINE void SolveVehicleSystem::setIsWarmStartingActive(bool isWarmStartingActive) {
    mIsWarmStartingActive = isWarmStartingActive;
}

}

#endif
