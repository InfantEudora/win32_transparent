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

#ifndef REACTPHYSICS3D_SPRING_SETTINGS_H
#define REACTPHYSICS3D_SPRING_SETTINGS_H

// Libraries
#include <reactphysics3d/configuration.h>

/// ReactPhysics3D namespace
namespace reactphysics3d {

/// How the two numbers in a SpringSettings are to be read
enum class SpringMode {

    /// The spring is described by its natural frequency (Hz) and a dimensionless damping
    /// ratio (0 = undamped, 1 = critically damped). The stiffness and damping coefficient are
    /// derived from the EFFECTIVE mass of the constrained system, so the same settings give the
    /// same visible behaviour whatever the masses and lever arms involved. This is the mode to
    /// use when tuning by hand.
    FREQUENCY_AND_DAMPING_RATIO,

    /// The spring is described by a stiffness k (N/m) and a damping coefficient c (N.s/m),
    /// as in the spring equation F = -k * x - c * v. Use this when the numbers come from a
    /// real spring or from another simulation.
    STIFFNESS_AND_DAMPING
};

// Structure SpringSettings
/**
 * Describes a linear spring + damper. Used by soft constraints (a joint or wheel suspension
 * that may deviate from its rest configuration and is pulled back by a spring force) rather
 * than a hard constraint that is enforced exactly.
 *
 * The spring is solved implicitly inside the constraint solver (see AxisConstraintPart), so
 * unlike a spring force applied explicitly every step it stays stable for any stiffness and
 * damping at any time step. The price is a small amount of extra numerical damping.
 *
 * A setting with neither stiffness/frequency nor damping describes a HARD constraint.
 */
struct SpringSettings {

    public :

        // -------------------- Attributes -------------------- //

        /// How frequency/stiffness and damping below are to be read
        SpringMode mode;

        /// Natural frequency in Hz (mode FREQUENCY_AND_DAMPING_RATIO) or stiffness k in N/m
        /// (mode STIFFNESS_AND_DAMPING). Zero means no spring: with a nonzero damping the
        /// constraint is a pure damper, with zero damping it is a hard constraint.
        decimal frequencyOrStiffness;

        /// Damping ratio (mode FREQUENCY_AND_DAMPING_RATIO, 1 = critical damping) or damping
        /// coefficient c in N.s/m (mode STIFFNESS_AND_DAMPING)
        decimal damping;

        // -------------------- Methods -------------------- //

        /// Constructor (a hard constraint)
        SpringSettings() : mode(SpringMode::FREQUENCY_AND_DAMPING_RATIO), frequencyOrStiffness(decimal(0.0)), damping(decimal(0.0)) {}

        /// Constructor
        SpringSettings(SpringMode springMode, decimal frequencyOrStiffnessValue, decimal dampingValue)
            : mode(springMode), frequencyOrStiffness(frequencyOrStiffnessValue), damping(dampingValue) {}

        /// A spring described by natural frequency (Hz) and damping ratio (1 = critical damping)
        static SpringSettings fromFrequencyAndDampingRatio(decimal frequency, decimal dampingRatio) {
            return SpringSettings(SpringMode::FREQUENCY_AND_DAMPING_RATIO, frequency, dampingRatio);
        }

        /// A spring described by stiffness k (N/m) and damping coefficient c (N.s/m)
        static SpringSettings fromStiffnessAndDamping(decimal stiffness, decimal dampingCoefficient) {
            return SpringSettings(SpringMode::STIFFNESS_AND_DAMPING, stiffness, dampingCoefficient);
        }

        /// Return true if the spring has a restoring force (a nonzero frequency or stiffness)
        bool hasStiffness() const {
            return frequencyOrStiffness > decimal(0.0);
        }

        /// Return true if these settings describe a soft constraint (spring and/or damper) rather
        /// than a hard one. Note that in FREQUENCY_AND_DAMPING_RATIO mode a damping ratio without
        /// a frequency has no meaning (there is nothing to be a ratio of), so that is hard too.
        bool isSoft() const {
            return hasStiffness() || (mode == SpringMode::STIFFNESS_AND_DAMPING && damping > decimal(0.0));
        }
};

}

#endif
