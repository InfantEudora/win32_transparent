#ifndef _BEZIER_H_
#define _BEZIER_H_
#include <stdint.h>
#include <string.h>
#include <vector>
#include "type_vec2.h"

/*
    Class bezier ideally should have a curve with equal spacing.

    Order 1 = Single point
    Order 2 = Line
    Order 3 = Quadratic Bezier
    Order 4 = Cubic Bezier

    This generic bezier can be any of the 4 orders, but does a standard fit on Lerp();
    Could be extended to use a lookup table when the bezier is fixed.
*/
class Bezier2D{
public:
    Bezier2D(int max_order = 4);
    ~Bezier2D();

    std::vector<vec2>control_points;

    vec2* GetFirstPoint();
    vec2* GetLastPoint();
    vec2* AddNewPoint(const vec2& point);
    vec2 Lerp(float t); //Returns a point on the Bezier by Lerping
    int GetOrder();
    float CalculateLength(int steps);
    vec2 FindClosest(vec2 point, int steps=20);
    float GetAngle(float k);

private:
    float length = 0;
    int max_order = 4;
};

#endif