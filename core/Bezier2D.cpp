#include "Bezier2D.h"

Bezier2D::Bezier2D(int _maxorder){
    max_order = _maxorder;
}

Bezier2D::~Bezier2D(){

}

int Bezier2D::GetOrder(){
    return control_points.size();
}

vec2* Bezier2D::AddNewPoint(const vec2& point){
    if (GetOrder() >= max_order){
        return NULL;
    }
    control_points.push_back(point);
    return &control_points.back();
}

vec2* Bezier2D::GetLastPoint(){
    if (control_points.size() > 0){
        return &control_points.back();
    }
    return NULL;
}

vec2* Bezier2D::GetFirstPoint(){
    if (control_points.size() > 0){
        return &control_points.front();
    }
    return NULL;
}

//Lerp by T value
vec2 Bezier2D::Lerp(float t){
    //We loop over the points, and computer intermediate points
    int neworder = GetOrder();
    std::vector<vec2> points = control_points;
    std::vector<vec2> new_controlpoints;
    vec2 p = vec2();
    //Get the default point.
    if (points.size() == 1){
        return points.at(0);
    }
    while(neworder > 1){
        new_controlpoints.clear();
        for (int i=0;i<points.size() - 1;i++){
            vec2 p1 = points.at(i);
            vec2 p2 = points.at(i+1);
            p = p1.lerp(p2,t);
            new_controlpoints.push_back(p);
        }
        points = new_controlpoints;
        neworder--;
    }
    //Points now contains the last point.
    return p;
}

//Returns the angle -PI ... PI of the curve at lerped position k.
float Bezier2D::GetAngle(float k){
    float dk = 0.01f;

    if (k < dk){
        k = 0;
    }
    if (k-dk > 1-dk){
        k = 1-dk;
    }

    vec2 a = Lerp(k);
    vec2 b = Lerp(k+dk);

    float theta = (b-a).angle();

    return theta;
}

//Each segment in the curve can have a different lenght.
float Bezier2D::CalculateLength(int steps){
    steps = clamp(steps,1,steps);
    float dt = 1.0 / (float)steps;
    float t= 0;
    vec2 p1 = Lerp(t);
    length = 0;
    for (int i=0;i<steps;i++){
        t+= dt;
        vec2 p2 = Lerp(t);
        float len = p1.distance(p2);
        length+=len;
        p1 = p2;
    }
    return length;
}

//Returns a point on the bezier closest to the supplied point
//This can be optimised to something less loopy.
vec2 Bezier2D::FindClosest(vec2 point, int steps){
    float mindist = 99999;
    vec2* first =  GetFirstPoint();
    if (!first){
        return vec2();
    }
    vec2 closest = *first;

    for (int i=0;i<steps;i++){
        float k = (float)i / (float)(steps-1);
        vec2 p = Lerp(k);
        float dist = point.distance(p);
        //printf("FindClosest: %2i: from %.2f,%.2f -> p = %.2f, %.2f k=%.3f d=%.3f\n",i,point.x,point.y,p.x,p.y,k,dist);
        if (dist < mindist){
            closest = p;
            mindist = dist;
        }
    }

    return closest;
}

//TODO: A method for getting the point as a percentage of the interpolated length instead of the k factor.