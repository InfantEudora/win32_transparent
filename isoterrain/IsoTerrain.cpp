#include "IsoTerrain.h"
#include "OBJLoader.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoTerrain",DEBUG_INFO);

//Generates the tile that make up the terrain
void IsoTerrain::CreateTerrain(int w, int d){
    debug->Info("Creating Terrain %i x %i\n",w,d);
    cell_count = w * d;
    width = w;
    depth = d;

    //If the terrain is 1x1, a cell of size 1 cell is at the origin, from -0.5 to 0.5.
    for (int z = 0;z<depth;z++){
        for (int x = 0;x<width;x++){
            IsoCell* c = new IsoCell();

            //Object* tile= new Object();
            c->SetMesh(OBJLoader::ParseOBJFile("isoterrain/data/tile_terrain.obj"));
            c->SetPosition(vec3(x,0,z));
            AttachChild(c);
        }
    }

}