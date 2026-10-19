#ifndef _CHASM_MESH_BUILD_H_
#define _CHASM_MESH_BUILD_H_

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <vector>
#include "type_vertex.h"
#include "type_vec2.h"
#include "type_vec3.h"
#include "Palette.h"

/*
    The small pieces every generated mesh in chasm is made of - the zones' houses, crops and
    boundaries (ZoneMesh.h and the files it calls). One place, so they all wind and colour alike.
*/

//One flat-shaded triangle, wound so its face normal agrees with `want`: the renderer culls back
//faces, and which way round a generated triangle comes out is otherwise an accident of the order its
//corners were found in. Coloured by the palette cell (column, row) - Palette.h.
inline void MeshTri(std::vector<vertex>& out, vec3 a, vec3 b, vec3 c, const vec3& want, int column,
                    int row = PAL_TEMPERATE){
    vec3 n = (b - a).cross(c - a);
    if (n.dot(want) < 0.0f){
        std::swap(b,c);
        n = n * -1.0f;
    }
    if (n.length() < 1e-9f){
        return;
    }
    n.normalize();
    vec3 tangent = b - a;
    if (tangent.length() > 1e-9f){
        tangent.normalize();
    }
    vertex v = {};
    v.normal = n;
    v.tangent = tangent;
    v.uv = PaletteUV(column,row);
    v.matid = 0;
    v.pos = a; out.push_back(v);
    v.pos = b; out.push_back(v);
    v.pos = c; out.push_back(v);
}

//A quad a-b-c-d (in order round it) as two triangles facing `want`.
inline void MeshQuad(std::vector<vertex>& out, const vec3& a, const vec3& b, const vec3& c, const vec3& d,
                     const vec3& want, int column, int row = PAL_TEMPERATE){
    MeshTri(out,a,b,c,want,column,row);
    MeshTri(out,a,c,d,want,column,row);
}

//Bilinear: the point at (u,v) of the quad p0 p1 p2 p3, with p0 at (0,0), p1 (1,0), p2 (1,1), p3 (0,1).
inline vec2 MeshBilinear(const vec2* p, float u, float v){
    vec2 a = p[0] + (p[1] - p[0]) * u;
    vec2 b = p[3] + (p[2] - p[3]) * u;
    return a + (b - a) * v;
}

//A box of half-width `half` from y0 to y1 round p, its top and sides - a stake, a pole.
inline void MeshPost(std::vector<vertex>& out, const vec2& p, float half, float y0, float y1, int column){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3(p.x + ((i & 1) ? half : -half),(i & 4) ? y1 : y0,p.y + ((i & 2) ? half : -half));
    }
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,0,-1),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,0,1),column);
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
}

/*
    A SITE STAKED OUT - a field's (CropMesh.h), a garden's or lot's (ZoneMesh.cpp): a stake MESH_SITE_STAKE over the
    ground where its outline turns, and a cord MESH_SITE_CORD over it from one to the next. The cord is a thin
    ribbon standing on edge, faced both ways so it reads from either side.
*/
#define MESH_SITE_STAKE     0.55f
#define MESH_SITE_CORD      0.32f
inline void MeshCord(std::vector<vertex>& out, const vec3& a, const vec3& b, int column){
    vec3 drop(0.0f,0.03f,0.0f);
    vec3 side(a.z - b.z,0.0f,b.x - a.x);
    MeshQuad(out,a - drop,b - drop,b + drop,a + drop,side,column);
    MeshQuad(out,b - drop,a - drop,a + drop,b + drop,side * -1.0f,column);
}

//Turns what was built from `first` on by `angle` (radians) about the line through `pivot` along the unit `axis` - a
//log knocked askew, a door on its hinge - its normals with it. The right hand's way: a quarter turn about up takes
//+x to -z.
inline void MeshTurn(std::vector<vertex>& out, size_t first, const vec3& pivot, const vec3& axis, float angle){
    float c = std::cos(angle);
    float s = std::sin(angle);
    auto turn = [&](const vec3& v){
        return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.0f - c));
    };
    for (size_t i = first; i < out.size(); i++){
        out[i].pos = pivot + turn(out[i].pos - pivot);
        out[i].normal = turn(out[i].normal);
        out[i].tangent = turn(out[i].tangent);
    }
}

//...and moves it by `by`.
inline void MeshShift(std::vector<vertex>& out, size_t first, const vec3& by){
    for (size_t i = first; i < out.size(); i++){
        out[i].pos = out[i].pos + by;
    }
}

//A well-mixed hash, for choices that must depend on a place and nothing else.
inline uint32_t MeshHash(uint32_t a, uint32_t b = 0, uint32_t c = 0){
    uint32_t h = a * 0x8DA6B343u ^ b * 0xD8163841u ^ c * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

#endif
