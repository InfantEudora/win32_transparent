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
    material_slot[0] = terrain_material_map[newtype];
    terrain_type = newtype;
}

Object* IsoCell::PlaceTree(const std::string& asset_name){
    Object* prop = NULL;
    if (prop_index == -1){
        prop = assetmanager->GetObjectFromAsset(asset_name.c_str());
        if (prop && AttachChild(prop)){
            prop_index = children.size() - 1;
            debug->Ok("Placed a new tree\n");
            prop->material_names[0] = "Tree_tex";
            prop->f_update_materials = true;
            vec3 s = vec3(0.5);
            prop->SetScale(s);
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

//Stairs to go 1 up in terrain height
Object* IsoCell::PlaceStairs(const std::string& asset_name,int direction){
    if (!IsoDirection::DirectionIsValid(direction)){
        return NULL;
    }
    if (!terrain){
        return NULL;
    }
    IsoCell* neighbour_up = terrain->GetCellByCoordinate(coordinate + int3(0,0,1));
    if (!neighbour_up){
        return NULL;
    }

    //Replace the up neighbour's mesh
    Object* n = assetmanager->GetObjectFromAsset(asset_name.c_str(),neighbour_up);
    if (n){
        debug->Ok("Placed some stairs\n");
        n->SetVisibility(true);
    }
    return n;
}

//Place a wall at one of the 4 coordinates.
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