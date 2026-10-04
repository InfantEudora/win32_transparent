#ifndef _CHASM_MESH_BUILD_H_
#define _CHASM_MESH_BUILD_H_

#include <algorithm>
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

//A well-mixed hash, for choices that must depend on a place and nothing else.
inline uint32_t MeshHash(uint32_t a, uint32_t b = 0, uint32_t c = 0){
    uint32_t h = a * 0x8DA6B343u ^ b * 0xD8163841u ^ c * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

#endif
