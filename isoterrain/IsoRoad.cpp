#include "IsoRoad.h"
#include "Debug.h"
static Debugger* debug = new Debugger("IsoRoad", DEBUG_INFO);



IsoRoad::IsoRoad(){
}

IsoRoad::~IsoRoad(){
}

Object* IsoRoad::PlaceNewMarker(AssetManager* assetmanager, RoadMarkerType type, vec3 position){
    if (!assetmanager){
        debug->Warn("Cannot place marker without asset manager\n");
        return NULL;
    }

    Object* marker = assetmanager->GetObjectFromAsset("marker");
    marker->SetScale(vec3(0.1,0.2,0.1));
    marker->SetPosition(position + vec3(0,0.05,0));
    marker->name = "Road Marker";
    marker->UpdatePhysicsState();
    AttachChild(marker);
    return marker;
}