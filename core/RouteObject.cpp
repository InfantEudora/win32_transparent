#include "RouteObject.h"
#include "Debug.h"

static Debugger *debug = new Debugger("RouteObject", DEBUG_ALL);

void Route::Setup(Object* _start, Object* _end){
    start = _start;
    end = _end;
}

float Route::GetDistance(){
    if ((!start) || (!end))
        return 0;
    return (end->GetWorldPosition() - start->GetWorldPosition()).length();
}

void Route::Reverse(){
    Object* t = end;
    end = start;
    start = t;
}

//Creates a route from a to b. Adding sufficient path pieces to cover the path.
void RouteObject::SetupNewRoute(Object* from, Object* to, AssetManager* assetmanager){
    if ((from == NULL) && (to != NULL)){
        name = "Route NULL -> " + to->name;
    }
    if ((from != NULL) && (to == NULL)){
        name = "Route " + from->name + " -> NULL";
    }
    if ((from == NULL) && (to == NULL)){
        name = "Empty Route";
    }

    //Make sure the start or end is not us or one of our children.
    std::vector<Object*> all_objects;
    GetAllSubObjects(all_objects);

    for (Object* obj : all_objects){
        if (obj == from){
            debug->Err("Cannot create route: start object is this RouteObject or one of its children\n");
            return;
        }
        if (obj == to){
            debug->Err("Cannot create route: end object is this RouteObject or one of its children\n");
            return;
        }
    }

    //We create a new route
    route = new Route();
    route->Setup(from,to);

    float dist = route->GetDistance();
    debug->Info("Route Distance = %.2f\n",dist);

    //Create the segments
    int num_required_segments = (dist / 2) + 1;
    if (dist == 0){
        num_required_segments = 0;
    }

    int num_segments = children.size();
    while (num_segments > num_required_segments){
        Object* child = GetLastChild();
        if (child){
            DetachChild(child);
            debug->Info("Removing segments\n");
            delete child;
        }
        num_segments--;
    }

    if (num_required_segments > num_segments){
        int new_segments = num_required_segments - num_segments;
        for (int s=0;s<new_segments;s++){
            float k = float(s) / (float)new_segments;
            //Use a bezier to place objects along the path.
            //Alternatively, we use a single square to the bezier extents, and use a shader.
            Object* segment = assetmanager->GetObjectFromAsset("plane");
            if(!segment){
                debug->Fatal("Unable to load asset for route\n");
            }
            AttachChild(segment);
        }
    }

    UpdateRoute();
}

Object* RouteObject::GetStartObject(){
    if (!route)
        return NULL;
    return route->start;
}

Object* RouteObject::GetEndObject(){
    if (!route)
        return NULL;
    return route->end;
}

//Does not rebuild segments, only updates their positions and rotation
void RouteObject::UpdateRoute(){
    if ((!route->start) || (!route->end)){
        return;
    }

    //Assume all the children are segments.
    int num_segments = children.size();

    Bezier2D b = Bezier2D(2);
    b.AddNewPoint(vec2(0,0));

    vec2 e2 = route->end->GetWorldPosition().xz();
    vec2 s2 = route->start->GetWorldPosition().xz();

    b.AddNewPoint(e2 - s2);

    //We start at from.
    SetPosition(vec3(s2.x,0,s2.y));

    for (int s=0;s<num_segments;s++){
        Object* segment = GetChild(s);
        if (!segment){
            debug->Fatal("Died while iterating over children\n");
        }
        float k = float(s) / (float)num_segments;

        segment->SetScale(vec3(0.4,1,0.12));
        vec2 p  = b.Lerp(k);
        segment->SetPosition(vec3(p.x,0,p.y));

        float theta = b.GetAngle(k);
        quat q; q.set_rotation(vec3(0,-1,0),theta);
        segment->SetRotation(q);
    }
}
