#ifndef _ISO_CAR_H_
#define _ISO_CAR_H_

#include "IsoCell.h"
#include "IsoPath.h"
#include "type_vec3.h"
#include "IsoDirection.h"
#include "SoundSystem.h"

class IsoCar;
class IsoCar : public virtual Object{
public:
    IsoCar();
    ~IsoCar();

    RRandom* randgen = NULL;

    IsoCell* current_cell = NULL;
    IsoCell* target_cell = NULL;
    IsoCell* next_cell = NULL;
    IsoPath path;
    bool f_waiting_for_road = false; // True when moving to the nearest road cell before following path
    bool f_following_path = false;   // True when following a road path
    vec3 final_target_position = vec3(0,0,0); // If target cell is not on a road, final approach position
    bool f_has_final_target = false;

    // Set a world-space target position for the car to drive to.
    void SetTargetPosition(const vec3& pos);
    void ClearTarget();
    bool HasTarget() const;
    vec3 GetTargetPosition() const;
    void SetTargetCell(IsoCell* cell);

    IsoCar* close_car = NULL;

    int direction = DIRECTION_NONE;

    SoundSystem* soundsystem = NULL; //For playing car sounds

    //Some function to move the car
    void Accelerate(float factor);
    void Brake(float factor);
    void SteerLeft(float factor);
    void SteerRight(float factor);
    void Reverse(float factor);
    void HonkHorn();

    // Override physics update to move towards target if set.
    void UpdatePhysicsState() override;

    float top_speed = 1.0f; // meters per second
    float speed = 0.0f;
    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;
    bool f_reverse = false;

    //Actions
    float reversing_time = 0.0f;
    float time_waiting_for_car_ahead = 0.0f;
    float time_waiting_threshold = 2.0f;

    vec3 target_position = vec3(0,0,0);
    bool f_has_target = false;
    bool f_has_reached_last_target = false;
protected:
};

#endif
