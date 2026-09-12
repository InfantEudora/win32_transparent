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

    //Cell info
    IsoCell* current_cell = NULL;
    IsoCell* cell_ahead = NULL;


    //Cells in it's route
    IsoCell* target_cell = NULL;
    IsoCell* next_cell = NULL;

    IsoPath iso_path;

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
    void UpdateCellReferences();

    IsoCar* close_car = NULL;

    Object* target_vis = NULL;

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

    //Speed in meters per second
    float top_speed = 1.0f;
    float speed = 0.0f;
    float target_speed = 0.5f;

    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;
    float steering_position = 0.0f; //From -1 to +1
    bool f_reverse = false;

    //Actions
    float reversing_time = 0.0f;
    float time_waiting_for_car_ahead = 0.0f;
    float time_waiting_threshold = 2.0f;

    vec3 target_position = vec3(0,0,0);
    bool f_has_target = false;
    bool f_has_reached_last_target = false;

    float lane_offset = 0.25f; // Offset from cell center for right lane (positive = right side)

    // Calculate lane-adjusted position based on travel direction
    vec3 GetLaneAdjustedPosition(IsoCell* cell, IsoCell* next_cell);

    // Find a new random destination at least min_distance tiles away
    void FindNewDestination(int min_distance = 5);
protected:
};

#endif
