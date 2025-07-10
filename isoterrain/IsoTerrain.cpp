#include "IsoTerrain.h"
#include "OBJLoader.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoTerrain",DEBUG_INFO);

/*Coordinate mapping


X=WIDTH
Z=DEPTH
Y=UP=Height

But OpenGL is XYZ
Z=Height
*/

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
                c->assetmanager = assetmanager;
                c->terrain = this;

                c->SetPosition(vec3(x,y * height_factor,z) + center_offset);
                c->name = "IsoCell " + std::to_string(x) + "," + std::to_string(z);
                //These two lists should know/update when a cell/or child gets destroyed... somehow
                AttachChild(c);

                c->f_update_materials = true;
                assetmanager->GetObjectFromAsset(base_tile.c_str(),c);
                cells.push_back(c);
            }
        }
    }

    //We also allocate an array for the sides and corners, or if this were a quad, the vertices and edges.
    for (int y = 0;y<height;y++){
        for (int z = 0;z<depth+1;z++){
            for (int x = 0;x<width+1;x++){
                IsoWall* pillar = new IsoWall();
                pillar->pillar = PILLAR_UNINITIALISED;
                pillar->coordinate.x = x;
                pillar->coordinate.y = z;
                pillar->coordinate.z = y;
                pillars.push_back(pillar);
                //We just pre-allocate them.
                //TODO: Would be nice if they were created only when a wall is actually there.
            }
        }
    }
}

//This maps a world position to a cell coordinate. Doest not account for rotation and scaling.
//So just 1x1 sized grid cells.
IsoCell* IsoTerrain::FindCellByWorldPosition(vec3& at){
    vec3 coord = at - center_offset;
    if ((coord.x < 0) || (coord.z < 0)){
        return NULL;
    }
    if ((coord.x >= width) || (coord.z >= depth)){
        return NULL;
    }

    int index = (1/height_factor * coord.y * width * depth) +  (coord.z * width) + coord.x;
    if (index < cells.size()){
        return cells.at(index);
    }
    return NULL;
}

IsoCell* IsoTerrain::GetCellByCoordinate(int3 coord){
    //TODO: Less lazy lookup
    debug->Info("GetCellByCoordinate %i,%i\n",coord.x,coord.y);
    for (IsoCell* cell : cells){
        if (coord == cell->coordinate){
            return cell;
        }
    }
    return NULL;
}

IsoWall* IsoTerrain::GetPillarByCoordinate(int3 coord){
    //TODO: Less lazy lookup
    debug->Info("GetPillarCoordinate %i,%i\n",coord.x,coord.y);
    for (IsoWall* pillar : pillars){
        if (coord == pillar->coordinate){
            return pillar;
        }
    }
    return NULL;
}

void IsoTerrain::GetPillarsByCellCoordinate(int3 cell_coord, std::array<IsoWall*,4>&pillars){
    std::array<int3,4>pillar_coords;
    pillar_coords.at(0) = cell_coord;
    pillar_coords.at(1) = cell_coord + int3(1,0,0);
    pillar_coords.at(3) = cell_coord + int3(0,1,0);
    pillar_coords.at(2) = cell_coord + int3(1,1,0);

    for (int i =0;i<4;i++){
        int3 c = pillar_coords.at(i);
        IsoWall* pillar = GetPillarByCoordinate(c);
        pillars.at(i) = pillar;
    }
}