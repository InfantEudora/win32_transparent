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
void IsoTerrain::CreateTerrain(PhysicsWorld* world_in, RRandom* randgen_in, int w, int d, int h){
    if (!assetmanager){
        debug->Err("IsoTerrain requires an asset manager\n");
        return;
    }
    if (!randgen_in){
        debug->Err("IsoTerrain requires a random generator\n");
        return;
    }
    randgen = randgen_in;
    debug->Info("Creating Terrain %i x %i x %i\n",w,d,h);
    cell_count = w * d * h;
    width = w;
    depth = d;
    height = h;

    physicsworld = world_in;

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
                c->AddPhysics(world_in);
                Physics* physics = c->GetPhysics();
                if (physics){
                    physics->AddBoxCollider(vec3(0.5,0.25,0.5),vec3(0,-0.25,0),quat().identity());
                    physics->SetStatic(true);
                }

                c->name = "IsoCell " + std::to_string(x) + "," + std::to_string(z);
                //These two lists should know/update when a cell/or child gets destroyed... somehow
                AttachChild(c);

                c->f_update_materials = true;
                assetmanager->GetObjectFromAsset(base_tile.c_str(),c);
                cells.push_back(c);

                //Each cell will lookup walls like this
                std::array<int3,4>wall_coords;

                //This works. It's logical. And it's magic.
                int3 cell_coord = c->coordinate;
                int3 c2 = int3(cell_coord.y*2,cell_coord.x*2,cell_coord.z);
                wall_coords.at(0) = c2 + int3(0,1,0);
                wall_coords.at(1) = c2 + int3(1,2,0);
                wall_coords.at(2) = c2 + int3(2,1,0);
                wall_coords.at(3) = c2 + int3(1,0,0);

                for (int i =0;i<4;i++){
                    int3 c = wall_coords.at(i);

                    IsoWall* wall = GetWallByCoordinate(c);
                    if (!wall){
                        IsoWall* wall = new IsoWall();
                        wall->pillar = WALL_UNINITIALISED;
                        wall->coordinate.x = c.x;
                        wall->coordinate.y = c.y;
                        wall->coordinate.z = c.z;
                        walls.push_back(wall);
                    }

                }
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

void IsoTerrain::ClearUpdateCounts(){
    for (IsoCell* cell : cells){
        cell->update_count = 0;
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
    coord += vec3(0.5f,0,0.5f); //Offset to cell center

    int3 cell_coord = int3(int(floorf(coord.x)),int(floorf(coord.z)),int(floorf(coord.y/height_factor)));
    //debug->Info("FindCellByWorldPosition %f,%f,%f = coord %i,%i,%i\n",at.x,at.y,at.z,cell_coord.x,cell_coord.y,cell_coord.z);


    int index = (cell_coord.z * width * depth) +  (cell_coord.y * width) + cell_coord.x;
    //debug->Info("FindCellByWorldPosition %i,%i,%i = index %i\n",cell_coord.x,cell_coord.y,cell_coord.z,index);
    if (index < cells.size()){
        return cells.at(index);
    }
    return NULL;
}

IsoCell* IsoTerrain::GetCellByCoordinate(int3 coord){
    //TODO: Less lazy lookup
    //debug->Info("GetCellByCoordinate %i,%i\n",coord.x,coord.y);
    for (IsoCell* cell : cells){
        if (coord == cell->coordinate){
            return cell;
        }
    }
    return NULL;
}

IsoWall* IsoTerrain::GetWallByCoordinate(int3 coord){
    //TODO: Less lazy lookup
    //debug->Info("GetWallByCoordinate %i,%i\n",coord.x,coord.y);
    for (IsoWall* wall : walls){
        if (coord == wall->coordinate){
            return wall;
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

void IsoTerrain::GetWallsByCellCoordinate(int3 cell_coord, std::array<IsoWall*,4>&walls){
    std::array<int3,4>wall_coords;

    //This works. It's logical. And it's magic.
    int3 c2 = int3(cell_coord.y*2,cell_coord.x*2,cell_coord.z);
    wall_coords.at(0) = c2 + int3(0,1,0);
    wall_coords.at(1) = c2 + int3(1,2,0);
    wall_coords.at(2) = c2 + int3(2,1,0);
    wall_coords.at(3) = c2 + int3(1,0,0);

    for (int i =0;i<4;i++){
        int3 c = wall_coords.at(i);
        IsoWall* wall = GetWallByCoordinate(c);
        walls.at(i) = wall;
    }
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

IsoStairs* IsoTerrain::GetStairsByCellCoordinate(int3 cell_coordinate){
    for (IsoStairs* stair : stairs){
        if (!stair->cell){
            debug->Warn("Stairs is missing a cell reference\n");
            continue;
        }
        if (cell_coordinate == stair->cell->coordinate){
            return stair;
        }
    }
    return NULL;
}

void IsoTerrain::PlaceStairs(IsoStairs* stair, IsoCell* cell){
    if (!stair || !cell){
        return;
    }
    stair->cell = cell;
    stairs.push_back(stair);
    stair->SetPosition(cell->GetPosition());
    AttachChild(stair);
}