#ifndef _ROUTEOBJECT_H_
#define _ROUTEOBJECT_H_
#include "Bezier2D.h"
#include "Object.h"
#include "AssetManager.h"

class Route;

class Route{
    public:
    Route(){};
    Route(Route* route){
        start = route->start;
        end = route->end;
    };
    ~Route(){};

    void Setup(Object* start, Object* end);
    void Reverse();

    Object* start = NULL;
    Object* end = NULL;

    float GetDistance();
};

//We build a route for a ship to follow. Uses a 2D spline limited to a linear one for now.
class RouteObject : public Object{
public:
    RouteObject(){};
    ~RouteObject(){};

    Route* route = NULL;
    Bezier2D curve = Bezier2D(2);

    Object* GetStartObject();
    Object* GetEndObject();

    void SetupNewRoute(Object* from, Object* to, AssetManager* assetmanager);
    void UpdateRoute();
};

#endif