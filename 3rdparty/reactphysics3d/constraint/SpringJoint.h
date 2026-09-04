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

#ifndef REACTPHYSICS3D_SPRING_JOINT_H
#define REACTPHYSICS3D_SPRING_JOINT_H

// Libraries
#include <reactphysics3d/constraint/Joint.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <reactphysics3d/mathematics/mathematics.h>

namespace reactphysics3d {

// Structure SpringJointInfo
/**
 * This structure is used to gather the information needed to create a spring joint.
 * This structure will be used to create the actual spring joint.
 */
struct SpringJointInfo : public JointInfo {

    public :

        // -------------------- Attributes -------------------- //

        /// True if this object has been constructed using local-space anchors
        bool isUsingLocalSpaceAnchors;

        /// Anchor point on body 1 (in world-space coordinates)
        Vector3 anchorPointBody1WorldSpace;

        /// Anchor point on body 2 (in world-space coordinates)
        Vector3 anchorPointBody2WorldSpace;

        /// Anchor point on body 1 (in local-space coordinates)
        Vector3 anchorPointBody1LocalSpace;

        /// Anchor point on body 2 (in local-space coordinates)
        Vector3 anchorPointBody2LocalSpace;

        /// Rest length of the spring (distance between the two anchors at which the spring
        /// applies no force). A negative value means "the distance between the anchors at
        /// the time the joint is created".
        decimal restLength;

        /// The spring. Default: 2 Hz, damping ratio 0.5. A default-constructed SpringSettings
        /// (no stiffness, no damping) makes this a rigid rod of fixed length.
        SpringSettings springSettings;

        /// Constructor
        /**
         * @param rigidBody1 Pointer to the first body of the joint
         * @param rigidBody2 Pointer to the second body of the joint
         * @param initAnchorPointBody1WorldSpace The anchor point on body 1 in world-space coordinates
         * @param initAnchorPointBody2WorldSpace The anchor point on body 2 in world-space coordinates
         */
        SpringJointInfo(RigidBody* rigidBody1, RigidBody* rigidBody2,
                        const Vector3& initAnchorPointBody1WorldSpace,
                        const Vector3& initAnchorPointBody2WorldSpace)
                       : JointInfo(rigidBody1, rigidBody2, JointType::SPRINGJOINT),
                         isUsingLocalSpaceAnchors(false),
                         anchorPointBody1WorldSpace(initAnchorPointBody1WorldSpace),
                         anchorPointBody2WorldSpace(initAnchorPointBody2WorldSpace),
                         restLength(decimal(-1.0)),
                         springSettings(SpringSettings::fromFrequencyAndDampingRatio(decimal(2.0), decimal(0.5))) {}

        /// Constructor
        /**
         * @param rigidBody1 Pointer to the first body of the joint
         * @param rigidBody2 Pointer to the second body of the joint
         * @param initAnchorPointBody1WorldSpace The anchor point on body 1 in world-space coordinates
         * @param initAnchorPointBody2WorldSpace The anchor point on body 2 in world-space coordinates
         * @param initSpringSettings The spring (see SpringSettings)
         * @param initRestLength Rest length of the spring, negative for the initial anchor distance
         */
        SpringJointInfo(RigidBody* rigidBody1, RigidBody* rigidBody2,
                        const Vector3& initAnchorPointBody1WorldSpace,
                        const Vector3& initAnchorPointBody2WorldSpace,
                        const SpringSettings& initSpringSettings,
                        decimal initRestLength = decimal(-1.0))
                       : JointInfo(rigidBody1, rigidBody2, JointType::SPRINGJOINT),
                         isUsingLocalSpaceAnchors(false),
                         anchorPointBody1WorldSpace(initAnchorPointBody1WorldSpace),
                         anchorPointBody2WorldSpace(initAnchorPointBody2WorldSpace),
                         restLength(initRestLength),
                         springSettings(initSpringSettings) {}
};

// Class SpringJoint
/**
 * This class represents a spring (or, without spring settings, a rigid rod) between an
 * anchor point on each of two bodies. The joint only acts along the line through the two
 * anchors: it pulls them together when the distance exceeds the rest length and pushes them
 * apart when it is shorter, with the force of a damped spring. The bodies are otherwise free
 * to move and rotate, so the joint has five degrees of freedom.
 *
 * The spring is solved implicitly inside the constraint solver (see AxisConstraintPart), so
 * it stays stable for any stiffness and damping. In SpringMode::FREQUENCY_AND_DAMPING_RATIO
 * the settings are relative to the effective mass of the two bodies, which makes them easy
 * to tune by hand: a frequency of 2 Hz bounces twice a second whatever the masses.
 */
class SpringJoint : public Joint {

    private :

        // -------------------- Methods -------------------- //

        /// Return the number of bytes used by the joint
        virtual size_t getSizeInBytes() const override;

    public :

        // -------------------- Methods -------------------- //

        /// Constructor
        SpringJoint(Entity entity, PhysicsWorld& world, const SpringJointInfo& jointInfo);

        /// Destructor
        virtual ~SpringJoint() override = default;

        /// Deleted copy-constructor
        SpringJoint(const SpringJoint& constraint) = delete;

        /// Deleted assignment operator
        SpringJoint& operator=(const SpringJoint& constraint) = delete;

        /// Return the spring settings
        const SpringSettings& getSpringSettings() const;

        /// Set the spring settings
        void setSpringSettings(const SpringSettings& springSettings);

        /// Return the rest length of the spring
        decimal getRestLength() const;

        /// Set the rest length of the spring
        void setRestLength(decimal restLength);

        /// Return the current distance between the two anchor points
        decimal getCurrentLength() const;

        /// Return the anchor point on body 1 (in local-space coordinates of body 1)
        const Vector3& getLocalAnchorPointBody1() const;

        /// Return the anchor point on body 2 (in local-space coordinates of body 2)
        const Vector3& getLocalAnchorPointBody2() const;

        /// Return the force (in Newtons) on body 2 required to satisfy the joint constraint in world-space
        virtual Vector3 getReactionForce(decimal timeStep) const override;

        /// Return the torque (in Newtons * meters) on body 2 required to satisfy the joint constraint in world-space
        virtual Vector3 getReactionTorque(decimal timeStep) const override;

        /// Return a string representation
        virtual std::string to_string() const override;
};

// Return the number of bytes used by the joint
RP3D_FORCE_INLINE size_t SpringJoint::getSizeInBytes() const {
    return sizeof(SpringJoint);
}

}

#endif
