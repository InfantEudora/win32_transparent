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
    if (props.size() < max_props){
        prop = assetmanager->GetObjectFromAsset(asset_name.c_str());
        AttachChild(prop);

        prop->f_update_materials = true;

        prop->name = "Tree @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);

        //Random position in cell
        vec3 p = terrain->randgen->GetVec3(-0.5,0.5);
        p.y = 0;
        prop->SetPosition(p);
        float scale = terrain->randgen->GetFloat(0.5f,1.0f);
        prop->SetScale(vec3(scale));
        debug->Ok("Placed a new tree prop at position %.3f,%.3f,%.3f\n", p.x, p.y, p.z);
        props.push_back(prop);
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
        n->Show();
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
        debug->Warn("Cannot place stairs on topmost level\n");
        return NULL;
    }

    //We need to place the stairs in the terrian, and make sure no stairs at the same coordinate exists:

    IsoStairs* stairs = terrain->GetStairsByCellCoordinate(coordinate);
    if (!stairs){
        stairs = new IsoStairs();
    }else{
        debug->Warn("Already stairs there\n");
        stairs->Show();
    }

    if (stairs && assetmanager->GetObjectFromAsset(asset_name.c_str(),stairs)){
        terrain->PlaceStairs(stairs,this);
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

// Place a wall at one of the 4 coordinates.
IsoWall* IsoCell::PlaceDoor(const std::string& asset_name,int direction){
    //This should do place wall with the wall being a door.

    /*
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
    */
    return NULL;
}

//TODO: Implement. You supply a wall, and this returns which direction it is relative to this cell.
int IsoCell::GetWallDirection(IsoWall* wall){
    return DIRECTION_NONE;
}

//Only a single road object per cell.
IsoRoad* IsoCell::PlaceRoad(const std::string& asset_name){
    std::string new_asset_name = asset_name;
    RoadType new_road_type = RoadType::STRAIGHT;
    quat roadq; roadq.identity();
    //We need find out if our neighbours have roads, and keep track of the road type we need to use.
    int bitmask = 0;
    IsoCell* neighbours[4] = {NULL,NULL,NULL,NULL};
    neighbours[0] = GetNeighbour(DIRECTION_NORTH);
    if (neighbours[0] && neighbours[0]->road_object){
        debug->Info("Neighbour north has road\n");
        bitmask |= 1;
    }
    neighbours[1] = GetNeighbour(DIRECTION_EAST);
    if (neighbours[1] && neighbours[1]->road_object){
        debug->Info("Neighbour east has road\n");
        bitmask |= 2;
    }
    neighbours[2] = GetNeighbour(DIRECTION_SOUTH);
    if (neighbours[2] && neighbours[2]->road_object){
        debug->Info("Neighbour south has road\n");
        bitmask |= 4;
    }
    neighbours[3] = GetNeighbour(DIRECTION_WEST);
    if (neighbours[3] && neighbours[3]->road_object){
        debug->Info("Neighbour west has road\n");
        bitmask |= 8;
    }
    debug->Info("Road bitmask %i\n",bitmask);
    if (bitmask == 15){
        //Adjust asset name based on bitmask
        new_asset_name = "road_cross";
        new_road_type = RoadType::CROSS_JUNCTION;
        debug->Info("Adjusting road asset from %s to %s\n",asset_name.c_str(),new_asset_name.c_str());
    }else if ((bitmask == 3) || (bitmask == 6) || (bitmask == 12) || (bitmask == 9)){
        new_asset_name = "road_corner";
        new_road_type = RoadType::CURVE;
        debug->Info("Adjusting road asset from %s to %s\n",asset_name.c_str(),new_asset_name.c_str());

        if (bitmask == 3){
            roadq.set_rotation(ref_up,toradians(0));
        }else if (bitmask == 6){
            roadq.set_rotation(ref_up,toradians(-90));
        }else if (bitmask == 9){
            roadq.set_rotation(ref_up,toradians(90));
        }else if (bitmask == 12){
            roadq.set_rotation(ref_up,toradians(180));
        }
    }else if ((bitmask == 1) || (bitmask == 4) || (bitmask == 5)){
        roadq.set_rotation(ref_up,toradians(90));
    }else if ((bitmask == 0) || (bitmask == 2) || (bitmask == 10)){
        //Keep default
    }else if ((bitmask == 7) || (bitmask == 11) || (bitmask == 13) || (bitmask == 14)){
        new_asset_name = "road_tjunction";
        new_road_type = RoadType::T_JUNCTION;
        debug->Info("Adjusting road asset from %s to %s\n",asset_name.c_str(),new_asset_name.c_str());

        if (bitmask == 7){
            roadq.set_rotation(ref_up,toradians(-90));
        }else if (bitmask == 11){
            roadq.set_rotation(ref_up,toradians(0));
        }else if (bitmask == 14){
            roadq.set_rotation(ref_up,toradians(180));
        }else if (bitmask == 13){
            roadq.set_rotation(ref_up,toradians(90));
        }
    }

    if (!road_object){
        road_object = new IsoRoad();
        assetmanager->GetObjectFromAsset(new_asset_name.c_str(),road_object);
        if (road_object && AttachChild(road_object)){
            debug->Ok("Placed some road decoration\n");
            road_object->f_update_materials = true;
            //road_object->name = "Road @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.y);
            road_object->name = new_asset_name;
            road_object->SetRotation(roadq);
            road_object->road_type = new_road_type;
        }
    }else{
        //Check if we have the correct road type loaded. TODO
        debug->Info("Updating existing road from %s to %s\n",road_object->name.c_str(),new_asset_name.c_str());
        if (road_object->name != new_asset_name){
            assetmanager->GetObjectFromAsset(new_asset_name.c_str(),road_object);
            debug->Ok("Updated road asset\n");
            road_object->f_update_materials = true;
            road_object->name = new_asset_name;
            road_object->road_type = new_road_type;
            road_object->SetRotation(roadq);
        }else{
            road_object->SetRotation(roadq);
        }
    }
    update_count++;
    debug->Info("Road update count %i. Updating neighbours\n",update_count);

    //Each neighbour with a road needs to update their road too.
    for (int i =0;i<4;i++){
        if (neighbours[i] && neighbours[i]->road_object && (neighbours[i]->update_count == 0)){
            neighbours[i]->PlaceRoad(asset_name);
        }
    }

    return road_object;
}

// Place a wall at one of the 4 coordinates.
IsoWall* IsoCell::PlaceWall(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        return NULL;
    }

    // We need to query the terrain where our 4 pillars are stored.
    std::array<IsoWall*,4>walls = {NULL,NULL,NULL,NULL};
    terrain->GetWallsByCellCoordinate(coordinate,walls);

    IsoWall* wall = walls.at(direction);
    if (!wall){
        debug->Warn("No wall for cell\n");
        return NULL;
    }

    if (wall->pillar == WALL_UNINITIALISED){
        debug->Info("Setup wall for cell\n");
        //Use our assetmanager to setup pillar
        assetmanager->GetObjectFromAsset(asset_name.c_str(),wall);
        wall->f_update_materials = true;
        wall->name = "Wall " + std::to_string(wall->coordinate.x) + "," + std::to_string(wall->coordinate.y);

        //Add a collider to the wall:
        wall->AddPhysics(terrain->physicsworld);
        Physics* physics = wall->GetPhysics();
        if (physics){
            physics->AddBoxCollider(vec3(0.5,0.3,0.03),vec3(0,0.3,0),quat().identity());
            physics->SetStatic(true);
        }

        vec3 pos_offset = vec3();
        quat orientation = quat().identity();

        if (direction == DIRECTION_NORTH){
            //wall->SetPosition(vec3(0,0,-0.5));
            pos_offset = vec3(0,0,-0.5);
        }else if (direction == DIRECTION_EAST){
            //wall->SetPosition(vec3(0.5,0,0));
            pos_offset = vec3(0.5,0,0);
            //quat q; q.set_rotation(ref_up,toradians(90));
            //wall->SetRotation(q);
            orientation.set_rotation(ref_up,toradians(90));
        }else if (direction == DIRECTION_SOUTH){
            //wall->SetPosition(vec3(0,0,0.5));
            pos_offset = vec3(0.0,0,0.5);
        }else if (direction == DIRECTION_WEST){
            //wall->SetPosition(vec3(-0.5,0,0));
            pos_offset = vec3(-0.5,0,0);
            //quat q; q.set_rotation(ref_up,toradians(-90));
            //wall->SetRotation(q);
            orientation.set_rotation(ref_up,toradians(-90));
        }

        wall->SetPosition(pos_offset + vec3(coordinate.x,coordinate.z * terrain->height_factor,coordinate.y) + terrain->center_offset);
        wall->SetRotation(orientation);

        terrain->AttachChild(wall);
        wall->pillar = WALL_VALID;
    }else if (wall->pillar == WALL_VALID){
        debug->Info("Wall already set up direction %i from %i x %i x %i\n",direction,coordinate.x,coordinate.y,coordinate.z);
        assetmanager->GetObjectFromAsset(asset_name.c_str(),wall);
        wall->Show();
    }else{
        debug->Err("Wall not setup as a wall...?\n");
    }
    return wall;

/*
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
    */
}