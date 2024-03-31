#include "OBJLoader.h"


#include "Debug.h"
static Debugger *debug = new Debugger("OBJLoader", DEBUG_INFO);

//Static function.
Mesh* OBJLoader::ParseOBJFile(const char* filename){
    size_t size = 0;
    uint8_t* data = LoadFile(filename,&size);
    if (data){
        return ParseOBJFileData(data,size);
    }
    return NULL;
}

//Static function.
Mesh* OBJLoader::ParseOBJFileData(uint8_t* data, size_t size){
    StringView sv = {.count = size, .data = data};

    OBJLoader loader;

    int linecount = 0;
    while((sv.count > 0) && (linecount < 10000)){
        //debug->Info("Chopping line %i sv.count: %zu\n",linecount,sv.count);
        StringView line = sv_chop_by_delim(&sv,'\n');
        std::string str(reinterpret_cast<char*>(line.data), line.count);
        //debug->Info("Line: %s\n",str.c_str());
        linecount++;
        if (!loader.ParseOBJLine(str.c_str())){
            debug->Err("Unable to parse line: [%s]\n",str.c_str());
            break;
        }
    }
    debug->Info("Object has %i faces, %i vertices.\n",
    loader.face_vertexindexlist.size(),loader.face_vertexlist.size());

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

    if ((vertex_count != normal_count) || (vertex_count != uv_count)){
        debug->Err("OBJ face/normal/uv count difference\n");
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
        vert.matid = 0;
        verts.push_back(vert);
        vert.pos = face_vertexlist.at(v.y-1);
        vert.normal = face_normallist.at(n.y-1);
        vert.uv = face_uvlist.at(u.y-1);
        vert.matid = 0;
        verts.push_back(vert);
        vert.pos = face_vertexlist.at(v.x-1);
        vert.normal = face_normallist.at(n.x-1);
        vert.uv = face_uvlist.at(u.x-1);
        vert.matid = 0;
        verts.push_back(vert);
    }
    debug->Info("Generated %i vertices\n",verts.size());
    Mesh* mesh = new Mesh();
    mesh->SetMeshData(&verts.at(0),verts.size());
    return mesh;
}