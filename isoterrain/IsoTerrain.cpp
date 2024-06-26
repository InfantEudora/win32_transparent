#include "IsoTerrain.h"
#include "OBJLoader.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoTerrain",DEBUG_INFO);

//Generates the tile that make up the terrain
void IsoTerrain::CreateTerrain(int w, int d){
    if (!assetmanager){
        debug->Err("IsoTerrain requires an asset manager\n");
        return;
    }
    debug->Info("Creating Terrain %i x %i\n",w,d);
    cell_count = w * d;
    width = w;
    depth = d;

    vec3 centre_offset = vec3(-w/2.0f + 0.5f,0,-d/2.0f + 0.5f);

    //If the terrain is 1x1, a cell of size 1 cell is at the origin, from -0.5 to 0.5.
    for (int z = 0;z<depth;z++){
        for (int x = 0;x<width;x++){
            IsoCell* c = new IsoCell();
            c->coordinate.x = x;
            c->coordinate.y = z;
            assetmanager->GetObjectFromAsset("tile_001",c);
            c->SetPosition(vec3(x,0,z) + centre_offset);
            c->name = "IsoCell " + std::to_string(x) + "," + std::to_string(z);
            //These two lists should know/update when a cell/or child gets destroyed... somehow
            AttachChild(c);
            cells.push_back(c);
        }
    }
}