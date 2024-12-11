#include "IsoTerrain.h"
#include "OBJLoader.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoTerrain",DEBUG_INFO);

//Generates the tile that make up the terrain
void IsoTerrain::CreateTerrain(int w, int d, int h){
    if (!assetmanager){
        debug->Err("IsoTerrain requires an asset manager\n");
        return;
    }
    debug->Info("Creating Terrain %i x %i x %i\n",w,d,h);
    cell_count = w * d * h;
    width = w;
    depth = d;
    height = h;

    center_offset = vec3(-w/2.0f + 0.5f,0,-d/2.0f + 0.5f);

    //If the terrain is 1x1, a cell of size 1 cell is at the origin, from -0.5 to 0.5.
    for (int y = 0;y<height;y++){
        for (int z = 0;z<depth;z++){
            for (int x = 0;x<width;x++){
                IsoCell* c = new IsoCell();
                c->coordinate.x = x;
                c->coordinate.y = z;
                c->coordinate.z = y;
                assetmanager->GetObjectFromAsset("tile_001",c);
                c->SetPosition(vec3(x,y,z) + center_offset);
                c->name = "IsoCell " + std::to_string(x) + "," + std::to_string(z);
                //These two lists should know/update when a cell/or child gets destroyed... somehow
                AttachChild(c);
                if (y != 0){
                    c->SetVisibility(false);
                }

                cells.push_back(c);
            }
        }
    }
}

//This maps a world position to a cell coordinate. Doest not accounf for rotation and scaling.
//So just 1x1 sized grid cells.
IsoCell* IsoTerrain::FindCellByWorldPosition(vec3& at){
    IsoCell* cell = NULL;

    vec3 coord = at - center_offset;
    if ((coord.x < 0) || (coord.z < 0)){
        return NULL;
    }
    if ((coord.x >= width) || (coord.z >= depth)){
        return NULL;
    }

    int index = (coord.y * width * depth) +  (coord.z * width) + coord.x;
    if (index < cells.size()){
        return cells.at(index);
    }
    return cell;
}