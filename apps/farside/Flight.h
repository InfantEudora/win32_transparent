#ifndef _FARSIDE_FLIGHT_H_
#define _FARSIDE_FLIGHT_H_

#include "Field.h"

/*
    The player ship's flight: 2D steering over the field, and a continuous altitude held above the
    ground (README, "Altitude: continuous", decided 2026-10-19). Its own integrator rather than a
    rigid body: the bench is about scale and sight, and a few lines of arithmetic say exactly what
    the ship does, where a body plus correcting forces would need tuning before it said anything.

    The ship holds HEIGHT_SET above whatever is under it - the plane, or the cap of a planet - so
    flying onto a planet climbs the dome by itself, capped by the ceiling. Descending to 0, slowly,
    over ground that is not too steep lands it; landed, it turns with the body it is on.
*/

struct FlightTuning{
    float accel = 14.0f;            //ship lengths/s^2 at full thrust
    //Top speed at the ground and at the ceiling - flying faster high is a candidate tuning, not
    //a rule (README). Equal values switch it off.
    float speed_low = 6.0f;
    float speed_high = 14.0f;
    float turn_rate = 3.0f;         //radians/s at full stick
    float drag = 1.2f;              //1/s - how quickly speed dies with no thrust
    float climb_rate = 6.0f;        //how fast the climb input moves the height asked for, lengths/s
    float follow_rate = 5.0f;       //1/s - how hard the ship chases that height
    float max_vertical_speed = 14.0f;
    float clearance = 0.15f;        //centre above the ground when landed
    float radius = 0.4f;            //kept this far from obstacles
    float land_speed = 1.5f;        //slower than this to land
    float land_slope_deg = 35.0f;   //steeper ground cannot be landed on
};

struct FlightInput{
    float thrust = 0.0f;            //-1..1
    float turn = 0.0f;              //-1 left .. 1 right
    float climb = 0.0f;             //-1..1, moves height_set at climb_rate
    float climb_step = 0.0f;        //an instant change of height_set (the mouse wheel)
};

struct ShipState{
    vec3 position = {0,2,0};
    vec3 velocity = {};             //horizontal only - height is the servo's
    float yaw = 0.0f;               //radians about +y; 0 flies toward -z, the engine's forward
    float height_set = 2.0f;        //the altitude asked for, above the ground
    bool f_landed = false;
    int landed_on = -1;             //index into Field::spheres
    vec3 landed_local = {};         //position in that sphere's frame
    vec3 landed_forward = {};       //nose in that sphere's frame
    float bank = 0.0f;              //roll into a turn, for the look only

    //Outputs of the last step, for the view and the HUD.
    quat orientation = quat(0,0,0,1);   //engine convention: local -z along the flight direction
    float height = 0.0f;            //actual height above the ground
    float vertical_speed = 0.0f;
    GroundSample ground;
};

//One tick. `ceiling` is the flat lid's y.
void StepFlight(ShipState& s, const FlightInput& in, const Field& field, const FlightTuning& t,
                float ceiling, float dt);

#endif
