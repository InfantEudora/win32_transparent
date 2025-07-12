#include "IsoCell.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoCell", DEBUG_INFO);

std::map<int,int> IsoCell::terrain_material_map;

IsoCell::IsoCell():Object(){
    //SetScale(vec3(0.5,0.5,0.5));
}

IsoCell::~IsoCell(){

}

void IsoCell::ApplyPreset(int preset){
    if (preset == 0){

    }
}

void IsoCell::SetTerrainType(int newtype){
    terrain_type = newtype;
    material_slot[0] = 0;
    if (terrain_type == CELL_TERRAIN_WATER){
        //Right now, just change the material
        material_slot[0] = 1;
        assetmanager->GetObjectFromAsset("Tile.Water",this);
    }else if (terrain_type == CELL_TERRAIN_NONE){
        // Get the correct mesh
        assetmanager->GetObjectFromAsset("Tile.1111",this);
    }else if (terrain_type == CELL_TERRAIN_GRASS){
        // Get the correct mesh
        assetmanager->GetObjectFromAsset("Tile.1111.Grass",this);
    }
}

Object* IsoCell::PlaceFloor(const std::string& asset_name){
    if (object_floor == NULL){
        object_floor = assetmanager->GetObjectFromAsset(asset_name.c_str());
        if (object_floor && AttachChild(object_floor)){
            debug->Ok("Placed some floor decoration\n");
            object_floor->f_update_materials = true;
            object_floor->name = "Floor @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);
        }
    }
    return object_floor;
}

Object* IsoCell::PlaceTree(const std::string& asset_name){
    Object* prop = NULL;
    if (prop_index == -1){
        prop = assetmanager->GetObjectFromAsset(asset_name.c_str());
        if (prop && AttachChild(prop)){
            prop_index = children.size() - 1;
            debug->Ok("Placed a new tree\n");
            prop->f_update_materials = true;
            vec3 s = vec3(0.5);
            prop->name = "Tree @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);
        }
    }
    return prop;
}

//Find our neighbouring cell
IsoCell* IsoCell::GetNeighbour(int direction){
    if (!terrain){
        return NULL;
    }
    int3 add = {};
    if (direction == DIRECTION_NORTH){
        add.y = -1;
    }else if (direction == DIRECTION_EAST){
        add.x = 1;
    }else if (direction == DIRECTION_SOUTH){
        add.y = 1;
    }else if (direction == DIRECTION_WEST){
        add.x = -1;
    }

    return terrain->GetCellByCoordinate(coordinate + add);
}

Object* IsoCell::RaiseTerrain(){
    if (!terrain){
        return NULL;
    }
    IsoCell* neighbour_up = terrain->GetCellByCoordinate(coordinate + int3(0,0,1));
    if (!neighbour_up){
        return NULL;
    }

    //Replace the up neighbour's mesh
    Object* n = assetmanager->GetObjectFromAsset(terrain->base_tile.c_str(),neighbour_up);
    if (n){
        debug->Ok("Raied terrain\n");
        n->SetVisibility(true);
    }
    return n;
}

// Stairs to go 1 up in terrain height. For now these are possible in 4 directions
IsoStairs* IsoCell::PlaceStairs(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        return NULL;
    }
    if (!terrain){
        return NULL;
    }

    //There should be a level above us.
    IsoCell* neighbour_up = terrain->GetCellByCoordinate(coordinate + int3(0,0,1));
    if (!neighbour_up){
        return NULL;
    }

    //Replace the up neighbour's mesh
    IsoStairs* stairs = new IsoStairs();
    if (assetmanager->GetObjectFromAsset(asset_name.c_str(),stairs)){
        AttachChild(stairs);
        if (direction == DIRECTION_NORTH){
            quat q; q.set_rotation(ref_up,toradians(0));
            stairs->SetRotation(q);
        }else if (direction == DIRECTION_EAST){
            quat q; q.set_rotation(ref_up,toradians(-90));
            stairs->SetRotation(q);
        }else if (direction == DIRECTION_SOUTH){
            quat q; q.set_rotation(ref_up,toradians(180));
            stairs->SetRotation(q);
        }else if (direction == DIRECTION_WEST){
            quat q; q.set_rotation(ref_up,toradians(90));
            stairs->SetRotation(q);
        }
    }else{
        debug->Err("Unable to create stairs\n");
        delete stairs;
        return NULL;
    }

    return stairs;
}


IsoWall* IsoCell::PlacePillar(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        return NULL;
    }

    // We need to query the terrain where our 4 pillars are stored.
    std::array<IsoWall*,4>pillars = {NULL,NULL,NULL,NULL};
    terrain->GetPillarsByCellCoordinate(coordinate,pillars);

    IsoWall* pillar = pillars.at(direction);
    if (!pillar){
        debug->Warn("No pillar for cell\n");
        return NULL;
    }

    if (pillar->pillar == PILLAR_UNINITIALISED){
        debug->Info("Setup pillar for cell\n");
        //Use our assetmanager to setup pillar
        assetmanager->GetObjectFromAsset(asset_name.c_str(),pillar);
        pillar->f_update_materials = true;
        pillar->name = "Pillar " + std::to_string(pillar->coordinate.x) + "," + std::to_string(pillar->coordinate.y);

        pillar->SetPosition(vec3(pillar->coordinate.x - 0.5f,pillar->coordinate.z * terrain->height_factor,pillar->coordinate.y - 0.5f) + terrain->center_offset);
        terrain->AttachChild(pillar);
        pillar->pillar = PILLAR_VALID;
    }else{
        debug->Info("Pillar already set up\n");
    }
    return pillar;
}

// Adds pillars to all four corners. They are added as seperate wall objects to the terrain, not the tile.
IsoWall* IsoCell::PlacePillars(const std::string& asset_name){

    // We need to query the terrain where our 4 pillars are stored.
    std::array<IsoWall*,4>pillars = {NULL,NULL,NULL,NULL};
    terrain->GetPillarsByCellCoordinate(coordinate,pillars);

    for (int i=0;i<4;i++){
        IsoWall* pillar = pillars.at(i);
        if (!pillar){
            debug->Warn("No pillar for cell\n");
            return NULL;
        }

        if (pillar->pillar == PILLAR_UNINITIALISED){
            debug->Info("Setup pillar for cell\n");
            //Use our assetmanager to setup pillar
            assetmanager->GetObjectFromAsset(asset_name.c_str(),pillar);
            pillar->f_update_materials = true;
            pillar->name = "Pillar " + std::to_string(pillar->coordinate.x) + "," + std::to_string(pillar->coordinate.y);

            pillar->SetPosition(vec3(pillar->coordinate.x - 0.5f,pillar->coordinate.z * terrain->height_factor,pillar->coordinate.y - 0.5f) + terrain->center_offset);
            terrain->AttachChild(pillar);
            pillar->pillar = PILLAR_VALID;
        }else{
            debug->Info("Pillar already set up\n");
        }
    }
    return NULL;
}


// Place a wall at one of the 4 coordinates.
IsoWall* IsoCell::PlaceDoor(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        debug->Err("Invalid direction for door placement.\n");
        return NULL;
    }
    if (wall_indices[direction] == -1){
        int n_dir = (direction + 2) % 4;
        //If adjacent cell has a wall, we cant place one.
        IsoCell* neighbour = GetNeighbour(direction);
        if (neighbour)
            debug->Info("Neighbour for %s in direction %i = %s\n",name.c_str(),direction,neighbour->name.c_str());
        else
            debug->Info("Neighbour for %s in direction %i = NULL\n",name.c_str(),direction);
        if (neighbour && neighbour->wall_indices[n_dir] != -1){
            debug->Warn("Neighbour already has a wall in that direction\n");
            Object* child = neighbour->GetChild(neighbour->wall_indices[n_dir]);
            child->Show();
            return dynamic_cast<IsoWall*>(child);
        }

        //Create a new one.
        IsoWall* wall = new IsoWall();

        if (assetmanager->GetObjectFromAsset(asset_name.c_str(),wall) && AttachChild(wall)){
            wall->material_names[0] = "Bricks";
            wall->material_names[1] = "Concrete";
            wall->f_update_materials = true;
            wall->cell = this;
            //The last one if the index.
            int child_index = children.size() - 1;
            wall_indices[direction] = child_index;
            debug->Ok("Placed a new wall\n");
            wall->name = "Wall @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);

            if (direction == DIRECTION_NORTH){
                wall->SetPosition(vec3(0,0,-0.5));
            }else if (direction == DIRECTION_EAST){
                wall->SetPosition(vec3(0.5,0,0));
                quat q; q.set_rotation(ref_up,toradians(90));
                wall->SetRotation(q);
            }else if (direction == DIRECTION_SOUTH){
                wall->SetPosition(vec3(0,0,0.5));
            }else if (direction == DIRECTION_WEST){
                wall->SetPosition(vec3(-0.5,0,0));
                quat q; q.set_rotation(ref_up,toradians(-90));
                wall->SetRotation(q);
            }
        }else{
            debug->Ok("Unable to place new door\n");
        }
        wall->UpdatePhysicsState();
        return wall;
    }else{
        debug->Warn("Already a prop at that location. Updating it to be a door.\n");
        Object* child = GetChild(wall_indices[direction]);
        assetmanager->GetObjectFromAsset(asset_name.c_str(),child);
        child->Show();
        return dynamic_cast<IsoWall*>(child);
    }
    return NULL;
}

//TODO: Implement. You supply a wall, and this returns which direction it is relative to this cell.
int IsoCell::GetWallDirection(IsoWall* wall){
    return DIRECTION_NONE;
}

// Place a wall at one of the 4 coordinates.
IsoWall* IsoCell::PlaceWall(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        return NULL;
    }
    if (wall_indices[direction] == -1){
        int n_dir = (direction + 2) % 4;
        //If adjacent cell has a wall, we cant place one.
        IsoCell* neighbour = GetNeighbour(direction);
        if (neighbour)
            debug->Info("Neighbour for %s in direction %i = %s\n",name.c_str(),direction,neighbour->name.c_str());
        else
            debug->Info("Neighbour for %s in direction %i = NULL\n",name.c_str(),direction);
        if (neighbour && neighbour->wall_indices[n_dir] != -1){
            debug->Warn("Neighbour already has a wall in that direction\n");
            Object* child = neighbour->GetChild(neighbour->wall_indices[n_dir]);
            child->Show();
            return dynamic_cast<IsoWall*>(child);
        }

        //Create a new one.
        IsoWall* wall = new IsoWall();

        if (assetmanager->GetObjectFromAsset(asset_name.c_str(),wall) && AttachChild(wall)){
            wall->material_names[0] = "Bricks";
            wall->material_names[1] = "Concrete";
            wall->f_update_materials = true;
            wall->cell = this;
            //The last one if the index.
            int child_index = children.size() - 1;
            wall_indices[direction] = child_index;
            debug->Ok("Placed a new wall\n");
            wall->name = "Wall @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);

            //Add a collider to the wall:
            wall->AddPhysics(terrain->physicsworld);
            Physics* physics = wall->GetPhysics();
            if (physics){
                physics->AddBoxCollider(vec3(0.5,0.3,0.03),vec3(0,0,0),quat().identity());
                physics->SetStatic(true);
            }

            if (direction == DIRECTION_NORTH){
                wall->SetPosition(vec3(0,0,-0.5));
            }else if (direction == DIRECTION_EAST){
                wall->SetPosition(vec3(0.5,0,0));
                quat q; q.set_rotation(ref_up,toradians(90));
                wall->SetRotation(q);
            }else if (direction == DIRECTION_SOUTH){
                wall->SetPosition(vec3(0,0,0.5));
            }else if (direction == DIRECTION_WEST){
                wall->SetPosition(vec3(-0.5,0,0));
                quat q; q.set_rotation(ref_up,toradians(-90));
                wall->SetRotation(q);
            }


        }else{
            debug->Ok("Unable to place new wall\n");
        }
        wall->UpdatePhysicsState();
        return wall;
    }else{
        debug->Warn("Already a prop at that location\n");
        Object* child = GetChild(wall_indices[direction]);
        child->Show();
        return dynamic_cast<IsoWall*>(child);
    }
    return NULL;
}