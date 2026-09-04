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

#ifndef REACTPHYSICS3D_SPRING_JOINT_COMPONENTS_H
#define REACTPHYSICS3D_SPRING_JOINT_COMPONENTS_H

// Libraries
#include <reactphysics3d/mathematics/Transform.h>
#include <reactphysics3d/mathematics/Matrix3x3.h>
#include <reactphysics3d/engine/Entity.h>
#include <reactphysics3d/components/Components.h>
#include <reactphysics3d/containers/Map.h>
#include <reactphysics3d/constraint/SpringSettings.h>
#include <reactphysics3d/constraint/AxisConstraintPart.h>

// ReactPhysics3D namespace
namespace reactphysics3d {

// Class declarations
class MemoryAllocator;
class EntityManager;
class SpringJoint;
enum class JointType;

// Class SpringJointComponents
/**
 * This class represent the component of the ECS with data for the SpringJoint.
 */
class SpringJointComponents : public Components {

    private:

        // -------------------- Attributes -------------------- //

        /// Array of joint entities
        Entity* mJointEntities;

        /// Array of pointers to the joints
        SpringJoint** mJoints;

        /// Anchor point of body 1 (in local-space coordinates of body 1)
        Vector3* mLocalAnchorPointBody1;

        /// Anchor point of body 2 (in local-space coordinates of body 2)
        Vector3* mLocalAnchorPointBody2;

        /// Vector from center of body 1 to anchor point in world-space
        Vector3* mR1World;

        /// Vector from center of body 2 to anchor point in world-space
        Vector3* mR2World;

        /// Unit vector from anchor 1 to anchor 2 in world-space (the constraint axis)
        Vector3* mAxisWorld;

        /// Inverse inertia tensor of body 1 (in world-space coordinates)
        Matrix3x3* mI1;

        /// Inverse inertia tensor of body 2 (in world-space coordinates)
        Matrix3x3* mI2;

        /// Rest length of the spring
        decimal* mRestLength;

        /// Current distance between the anchors (computed at the start of each step)
        decimal* mCurrentLength;

        /// Spring settings
        SpringSettings* mSpringSettings;

        /// The single axis constraint doing the work (effective mass, softness, accumulated impulse)
        AxisConstraintPart* mConstraintPart;

        // -------------------- Methods -------------------- //

        /// Allocate memory for a given number of components
        virtual void allocate(uint32 nbComponentsToAllocate) override;

        /// Destroy a component at a given index
        virtual void destroyComponent(uint32 index) override;

        /// Move a component from a source to a destination index in the components array
        virtual void moveComponentToIndex(uint32 srcIndex, uint32 destIndex) override;

        /// Swap two components in the array
        virtual void swapComponents(uint32 index1, uint32 index2) override;

    public:

        /// Structure for the data of a spring joint component
        struct SpringJointComponent {

            decimal restLength;
            SpringSettings springSettings;

            /// Constructor
            SpringJointComponent(decimal restLength, const SpringSettings& springSettings)
                : restLength(restLength), springSettings(springSettings) {

            }
        };

        // -------------------- Methods -------------------- //

        /// Constructor
        SpringJointComponents(MemoryAllocator& allocator);

        /// Destructor
        virtual ~SpringJointComponents() override = default;

        /// Add a component
        void addComponent(Entity jointEntity, bool isDisabled, const SpringJointComponent& component);

        /// Return a pointer to a given joint
        SpringJoint* getJoint(Entity jointEntity) const;

        /// Set the joint pointer to a given joint
        void setJoint(Entity jointEntity, SpringJoint* joint) const;

        /// Return the local anchor point of body 1 for a given joint
        const Vector3& getLocalAnchorPointBody1(Entity jointEntity) const;

        /// Set the local anchor point of body 1 for a given joint
        void setLocalAnchorPointBody1(Entity jointEntity, const Vector3& localAnchorPointBody1);

        /// Return the local anchor point of body 2 for a given joint
        const Vector3& getLocalAnchorPointBody2(Entity jointEntity) const;

        /// Set the local anchor point of body 2 for a given joint
        void setLocalAnchorPointBody2(Entity jointEntity, const Vector3& localAnchorPointBody2);

        /// Return the constraint axis in world-space for a given joint
        const Vector3& getAxisWorld(Entity jointEntity) const;

        /// Return the rest length for a given joint
        decimal getRestLength(Entity jointEntity) const;

        /// Set the rest length for a given joint
        void setRestLength(Entity jointEntity, decimal restLength);

        /// Return the current anchor distance for a given joint (as of the last step)
        decimal getCurrentLength(Entity jointEntity) const;

        /// Return the spring settings for a given joint
        const SpringSettings& getSpringSettings(Entity jointEntity) const;

        /// Set the spring settings for a given joint
        void setSpringSettings(Entity jointEntity, const SpringSettings& springSettings);

        /// Return the axis constraint part for a given joint
        const AxisConstraintPart& getConstraintPart(Entity jointEntity) const;

        // -------------------- Friendship -------------------- //

        friend class BroadPhaseSystem;
        friend class SolveSpringJointSystem;
};

// Return a pointer to a given joint
RP3D_FORCE_INLINE SpringJoint* SpringJointComponents::getJoint(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mJoints[mMapEntityToComponentIndex[jointEntity]];
}

// Set the joint pointer to a given joint
RP3D_FORCE_INLINE void SpringJointComponents::setJoint(Entity jointEntity, SpringJoint* joint) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    mJoints[mMapEntityToComponentIndex[jointEntity]] = joint;
}

// Return the local anchor point of body 1 for a given joint
RP3D_FORCE_INLINE const Vector3& SpringJointComponents::getLocalAnchorPointBody1(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mLocalAnchorPointBody1[mMapEntityToComponentIndex[jointEntity]];
}

// Set the local anchor point of body 1 for a given joint
RP3D_FORCE_INLINE void SpringJointComponents::setLocalAnchorPointBody1(Entity jointEntity, const Vector3& localAnchorPointBody1) {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    mLocalAnchorPointBody1[mMapEntityToComponentIndex[jointEntity]] = localAnchorPointBody1;
}

// Return the local anchor point of body 2 for a given joint
RP3D_FORCE_INLINE const Vector3& SpringJointComponents::getLocalAnchorPointBody2(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mLocalAnchorPointBody2[mMapEntityToComponentIndex[jointEntity]];
}

// Set the local anchor point of body 2 for a given joint
RP3D_FORCE_INLINE void SpringJointComponents::setLocalAnchorPointBody2(Entity jointEntity, const Vector3& localAnchorPointBody2) {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    mLocalAnchorPointBody2[mMapEntityToComponentIndex[jointEntity]] = localAnchorPointBody2;
}

// Return the constraint axis in world-space for a given joint
RP3D_FORCE_INLINE const Vector3& SpringJointComponents::getAxisWorld(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mAxisWorld[mMapEntityToComponentIndex[jointEntity]];
}

// Return the rest length for a given joint
RP3D_FORCE_INLINE decimal SpringJointComponents::getRestLength(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mRestLength[mMapEntityToComponentIndex[jointEntity]];
}

// Set the rest length for a given joint
RP3D_FORCE_INLINE void SpringJointComponents::setRestLength(Entity jointEntity, decimal restLength) {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    mRestLength[mMapEntityToComponentIndex[jointEntity]] = restLength;
}

// Return the current anchor distance for a given joint
RP3D_FORCE_INLINE decimal SpringJointComponents::getCurrentLength(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mCurrentLength[mMapEntityToComponentIndex[jointEntity]];
}

// Return the spring settings for a given joint
RP3D_FORCE_INLINE const SpringSettings& SpringJointComponents::getSpringSettings(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mSpringSettings[mMapEntityToComponentIndex[jointEntity]];
}

// Set the spring settings for a given joint
RP3D_FORCE_INLINE void SpringJointComponents::setSpringSettings(Entity jointEntity, const SpringSettings& springSettings) {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    mSpringSettings[mMapEntityToComponentIndex[jointEntity]] = springSettings;
}

// Return the axis constraint part for a given joint
RP3D_FORCE_INLINE const AxisConstraintPart& SpringJointComponents::getConstraintPart(Entity jointEntity) const {

    assert(mMapEntityToComponentIndex.containsKey(jointEntity));
    return mConstraintPart[mMapEntityToComponentIndex[jointEntity]];
}

}

#endif
