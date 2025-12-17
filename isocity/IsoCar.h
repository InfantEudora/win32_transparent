#ifndef _ISO_CAR_H_
#define _ISO_CAR_H_

#include "IsoCell.h"
#include "type_vec3.h"

class IsoCar : public virtual Object{
public:
    IsoCar();
    ~IsoCar();

    IsoCell* target_cell = NULL;
    IsoCell* next_cell = NULL;

    // Set a world-space target position for the car to drive to.
    void SetTargetPosition(const vec3& pos);
    void ClearTarget();
    bool HasTarget() const;
    vec3 GetTargetPosition() const;
    void SetTargetCell(IsoCell* cell);


    // Override physics update to move towards target if set.
    void UpdatePhysicsState() override;

protected:
    vec3 target_position = vec3(0,0,0);
    bool f_has_target = false;
    float speed = 1.0f; // meters per second
};

#endif
