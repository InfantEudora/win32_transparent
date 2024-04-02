#include "OBJLoader.h"

#include "Debug.h"
static Debugger *debug = new Debugger("OBJLoader", DEBUG_TRACE);

//Static function.
Mesh* OBJLoader::ParseOBJFile(const char* filename, std::vector<Material>*optional_mat_list_out){
    size_t size = 0;
    uint8_t* data = LoadFile(filename,&size);
    if (data){
        return ParseOBJFileData(data,size,filename,optional_mat_list_out);
    }
    return NULL;
}

//Static function. Filename is required so be can extract the base path from it.
Mesh* OBJLoader::ParseOBJFileData(uint8_t* data, size_t size, const char* filename, std::vector<Material>*optional_mat_list_out){
    StringView sv = {.count = size, .data = data};

    OBJLoader loader;

    int linecount = 0;
    while((sv.count > 0) && (linecount < 10000)){
        //debug->Info("Chopping line %i sv.count: %zu\n",linecount,sv.count);
        StringView line = sv_chop_by_delim(&sv,'\n');
        std::string str(reinterpret_cast<char*>(line.data), line.count);
        //debug->Info("Line: %s\n",str.c_str());
        linecount++;
		if (str.find("mtllib") != std::string::npos){
            const char* mtlfilename = str.c_str() + 7;
			debug->Ok("Found new material libfile: [%s]\n",mtlfilename);
            std::string base  = GetBasePath(filename);
            std::string path = base;
            debug->Info("Base path [%s]\n",base.c_str());
            if (path.length() > 0){
			    path.append("/");
            }
            path.append(mtlfilename);
            if (path.back() == '\r'){
                path.pop_back();
                debug->Info("Thanks GIT! for randomly adding \\r's everywhere!\n");
            }
            loader.ParseOBJMatFile(path.c_str());
            debug->Info(".mat file contains %i different materials\n",loader.materials.size());
            continue;
        }else if (str.find("usemtl") != std::string::npos){
            const char* matname = str.c_str() + 7;
            if (!loader.SwitchMaterialByName(matname)){
                debug->Err("Switching material: %s\n",matname);
            }else{
                debug->Info("Switching material: %s\n",matname);
            }
            continue;
        }

        if (!loader.ParseOBJLine(str.c_str())){
            debug->Err("Unable to parse line: [%s]\n",str.c_str());
            continue;
        }
    }
    debug->Info("Object has %i faces, %i vertices.\n",
    loader.face_vertexindexlist.size(),loader.face_vertexlist.size());

    if (optional_mat_list_out){
        optional_mat_list_out->insert(optional_mat_list_out->end(),loader.materials.begin(),loader.materials.end());
    }
    return loader.BuildMesh();
}

bool OBJLoader::ParseOBJLine(const char* line){
	if (line[0] == 'v' && line[1] == ' '){ //Vertex:
		vec3 v;
		int res = sscanf(line,"v %f %f %f",&v.x,&v.y,&v.z);
		if (res != 3){
			debug->Warn("Vertex does not have 3 coordinates?\n");
			return false;
		}
		//Store vertex in list.
		face_vertexlist.push_back(v);
		return true;
    }else if (line[0] == 'v' && line[1] == 'n'){ //Normals...
		vec3 v;
		int res = sscanf(line,"vn %f %f %f",&v.x,&v.y,&v.z);
		if (res != 3){
			debug->Warn("Vertex does not have 3 normal coordinates?\n");
			return false;
		}
		//Store it:
		face_normallist.push_back(v);
		return true;
    }else if (line[0] == 'v' && line[1] == 't'){ //UV or Texture Coordinates...
		vec2 v;
		int res = sscanf(line,"vt %f %f",&v.x,&v.y);
		if (res != 2){
			debug->Warn("Vertex does not have 2 Texture coordinates?\n");
			return false;
		}
		//Store it:
		v.y = 1-v.y;
		face_uvlist.push_back(v);
		return true;
    }else if (line[0] == 'f' && line[1] == ' '){ //Face
		int ia,ib,ic; //Index abc
		int na,nb,nc; //Normal abc
		int ta = -1, tb = -1, tc = -1; //Temp = UV abc

		//Attemt to parse line
		bool parsed = false;
		int res;
		//Parse line with vertex, texture and normals
		res = sscanf(line,"f %i/%i/%i %i/%i/%i %i/%i/%i",&ia,&ta,&na, &ib,&tb,&nb, &ic,&tc,&nc);
		if (res != 9){
			//Line without texture coordinates
			res = sscanf(line,"f %i//%i %i//%i %i//%i",&ia,&na,&ib,&nb,&ic,&nc);
			if (res != 6){
				debug->Warn("Unable to parse face. res = %i\n",res);
                return false;
			}
		}
		debug->Trace("Reading Face v: %i,%i,%i normals: %i,%i,%i uvs: %i,%i,%i\n",ia,ib,ic,na,nb,nc,ta,tb,tc);
		int3 vindex = {ia,ib,ic};
		int3 nindex = {na,nb,nc};
		int3 uvindex = {ta,tb,tc};

		face_vertexindexlist.push_back(vindex);
		face_normalindexlist.push_back(nindex);
		if (ta != -1){
			face_uvindexlist.push_back(uvindex);
		}

        //Face has the currently selected material:
        face_matlist.push_back(current_material_index);

		return true;
	}
    return true;
}

//Builds the mesh from the loaded index lists.
Mesh* OBJLoader::BuildMesh(){
    std::vector<vertex>verts;

    //These should be an equal amount of face,normal and uv indices
    int vertex_count = face_vertexindexlist.size();
    int normal_count = face_normalindexlist.size();
    int uv_count = face_uvindexlist.size();
    int mat_count = face_matlist.size();

    if ((vertex_count != normal_count) || (vertex_count != uv_count) || (vertex_count != mat_count)){
        debug->Err("OBJ face/normal/uv/mat count difference\n");
        return NULL;
    }

    //This will generate 3 vertices constisting of (vertex,normal,uv)
    //Assuming these all are correct
    //Mind the face order.
    for (int i=0;i<face_vertexindexlist.size();i++){
        int3 v = face_vertexindexlist.at(i);
        int3 n = face_normalindexlist.at(i);
        int3 u = face_uvindexlist.at(i);
        vertex vert;
        vert.pos = face_vertexlist.at(v.z-1);
        vert.normal = face_normallist.at(n.z-1);
        vert.uv = face_uvlist.at(u.z-1);
        vert.matid = face_matlist.at(i);
        verts.push_back(vert);
        vert.pos = face_vertexlist.at(v.y-1);
        vert.normal = face_normallist.at(n.y-1);
        vert.uv = face_uvlist.at(u.y-1);
        vert.matid = face_matlist.at(i);
        verts.push_back(vert);
        vert.pos = face_vertexlist.at(v.x-1);
        vert.normal = face_normallist.at(n.x-1);
        vert.uv = face_uvlist.at(u.x-1);
        vert.matid = face_matlist.at(i);
        verts.push_back(vert);
    }
    debug->Info("Generated %i vertices\n",verts.size());
    Mesh* mesh = new Mesh();
    mesh->SetMeshData(&verts.at(0),verts.size());
    mesh->num_materials = materials.size();
    return mesh;
}

/*
    Material File Functions
*/
void OBJLoader::ParseOBJMatFile(const char* filename){
    size_t size = 0;
    uint8_t* data = LoadFile(filename,&size);
    if (data){
        ParseOBJMatFileData(data,size);
    }
}

//Static function.
void OBJLoader::ParseOBJMatFileData(uint8_t* data, size_t size){
    StringView sv = {.count = size, .data = data};
    int linecount = 0;
    while((sv.count > 0) && (linecount < 10000)){
        StringView svline = sv_chop_by_delim(&sv,'\n');
        linecount++;
        std::string line(reinterpret_cast<char*>(svline.data), svline.count);
        debug->Trace("Line: %s\n",line.c_str());
		if (line.find("newmtl") != std::string::npos){
            const char* matname = line.c_str() + 7;
            if (!SwitchMaterialByName(matname)){
                debug->Ok("Found new material: [%s]\n",matname);
                Material m;
                m.name = matname;
                m.glsl_material = {};
                materials.push_back(m);
                SwitchMaterialByName(matname);
            }else{
                debug->Warn(".mat file contains duplicate material names: [%s]\n",matname);
            }
        }else if (line.find("Kd") != std::string::npos){
            debug->Trace("Found diffuse value: %s\n",line.c_str()+3);
            vec3 f;
            if (sscanf(line.c_str()+3,"%f %f %f",&f.x,&f.y,&f.z) == 3){
                debug->Info(" diffuse color : %.2f %.2f %.2f\n",f.x,f.y,f.z);
                if (current_material){
                    current_material->glsl_material.color = vec4(f,1.0);
                }else{
                    debug->Err("No current material while parsing diffuse value.\n");
                }
            }
        }else if (line.find("d") != std::string::npos){
            debug->Trace("Found alpha value: %s\n",line.c_str()+2);
            float alpha = 1.0;
            if (sscanf(line.c_str()+2,"%f",&alpha) == 1){
                debug->Info(" alpha value   : %.2f\n",alpha);
                if (current_material){
                    current_material->glsl_material.color.w = alpha;
                }else{
                    debug->Err("No current material while parsing diffuse value.\n");
                }
            }
        }else if (line.find("map_Kd") != std::string::npos){
            char* diff_name = (char*)line.c_str()+7;
		    debug->Trace("Found diffuse texture: %s\n",diff_name);
            if (current_material){
                current_material->diff_texture = diff_name;
                //TODO: Load the texture...
            }else{
                debug->Err("No current material while parsing diffuse value.\n");
            }
        }
    }
}

//Returns NULL when the material does not exist
Material* OBJLoader::SwitchMaterialByName(const char* name){
    for (int i =0;i<materials.size();i++){
		if (materials[i].name.compare(name) == 0){
            current_material = &materials[i];
            current_material_index = i;
			return current_material;
		}
	}
    current_material_index = -1;
	return NULL;
}