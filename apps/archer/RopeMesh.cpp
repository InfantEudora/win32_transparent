#include <math.h>

#include "RopeMesh.h"
#include "Spline.h"
#include "SplineDeform.h"

/*
    See RopeMesh.h. Like Vine.cpp, nothing here beyond core's maths and the two spline files -
    `make rules` links it into stage_test.exe with no engine.
*/

//The rope's frame in its bind pose: down it, toward the camera, and across (side = normal x
//tangent, the same handedness as SplineFrame, so a piece keeps its winding).
static const vec3 ROPE_DOWN   = vec3(0.0f,-1.0f,0.0f);
static const vec3 ROPE_FRONT  = vec3(0.0f,0.0f,1.0f);
static const vec3 ROPE_ACROSS = vec3(1.0f,0.0f,0.0f);

vec3 RopeLinkBindCentre(const RopeMeshInput& in, int link){
    float seg = in.length / (float)((in.links > 0) ? in.links : 1);
    return in.anchor + ROPE_DOWN * (seg * ((float)link + 0.5f));
}

void RopeWeights(float s, float seg, int links, int3& bones, vec3& weights){
    int idx[3] = { 0, 0, 0 };
    float w[3] = { 0.0f, 0.0f, 0.0f };
    if (links <= 1 || seg <= 0.0f){
        bones = int3(0,0,0);
        weights = vec3(1.0f,0.0f,0.0f);
        return;
    }
    //In link-centre units: t = 0 at link 0's centre. The nearest centre j, and where s sits
    //between it and its neighbours, f in [-0.5, 0.5).
    float t = s / seg - 0.5f;
    int j = (int)floorf(t + 0.5f);
    float f = t - (float)j;
    float wa = 0.5f * (0.5f - f) * (0.5f - f);
    float wb = 0.75f - f * f;
    float wc = 0.5f * (0.5f + f) * (0.5f + f);
    int raw[3] = { j - 1, j, j + 1 };
    float rw[3] = { wa, wb, wc };
    //Clamped into the chain, merging whatever falls off an end onto the end link - which is what
    //pins the top of the rope to the first link and the tassel to the last.
    int used = 0;
    for (int k = 0; k < 3; k++){
        int b = raw[k];
        if (b < 0){ b = 0; }
        if (b > links - 1){ b = links - 1; }
        int slot = -1;
        for (int q = 0; q < used; q++){
            if (idx[q] == b){ slot = q; }
        }
        if (slot < 0){
            slot = used++;
            idx[slot] = b;
        }
        w[slot] += rw[k];
    }
    bones = int3(idx[0],idx[1],idx[2]);
    weights = vec3(w[0],w[1],w[2]);
}

//Appends a rigid piece at distance `s` down the bind rope, in the rope's frame, scaled.
static void AddPiece(const std::vector<vertex>& piece, float scale, float s, const RopeMeshInput& in,
                     std::vector<vertex>& out){
    vec3 at = in.anchor + ROPE_DOWN * s;
    for (size_t i = 0; i < piece.size(); i++){
        vertex v = piece[i];
        const vec3& p = piece[i].pos;
        v.pos = at + ROPE_ACROSS * (p.x * scale) + ROPE_FRONT * (p.y * scale) + ROPE_DOWN * (p.z * scale);
        const vec3& n = piece[i].normal;
        v.normal = ROPE_ACROSS * n.x + ROPE_FRONT * n.y + ROPE_DOWN * n.z;
        const vec3& t = piece[i].tangent;
        v.tangent = ROPE_ACROSS * t.x + ROPE_FRONT * t.y + ROPE_DOWN * t.z;
        out.push_back(v);
    }
}

bool BuildRopeMesh(const RopeMeshInput& in, std::vector<skinned_vertex>& out){
    out.clear();
    if (!in.tile || in.tile->size() < 3 || in.length <= 0.0f || in.links < 1){
        return false;
    }
    std::vector<vertex> flat;

    //The middle: the tile deformed down a straight curve, which is only a spline so that the
    //stretch-to-fit and the tile convention are the vine's, not a second copy of them.
    Spline line;
    line.points = { in.anchor, in.anchor + ROPE_DOWN * in.length };
    line.up = ROPE_FRONT;
    line.Build(0.05f);
    SplineDeformParams d;
    d.scale = in.tile_scale;
    if (DeformAlongSpline(line,*in.tile,d,flat) <= 0){
        return false;
    }

    if (in.ring){
        AddPiece(*in.ring,in.ring_scale,0.0f,in,flat);
    }
    if (in.collar){
        for (size_t i = 0; i < in.collar_at.size(); i++){
            AddPiece(*in.collar,in.collar_scale,in.collar_at[i],in,flat);
        }
    }
    if (in.tassel){
        AddPiece(*in.tassel,in.tassel_scale,in.length,in,flat);
    }

    //Weighted by how far down the rope each vertex is. The bind rope is straight, so that is just
    //its height below the anchor - for the pieces as much as the tile.
    float seg = in.length / (float)in.links;
    out.reserve(flat.size());
    for (size_t i = 0; i < flat.size(); i++){
        const vertex& v = flat[i];
        skinned_vertex o;
        o.pos = v.pos;
        o.normal = v.normal;
        o.tangent = v.tangent;
        o.uv = v.uv;
        o.matid = v.matid;
        float s = (v.pos - in.anchor).dot(ROPE_DOWN);
        RopeWeights(s,seg,in.links,o.bones,o.weights);
        out.push_back(o);
    }
    return true;
}
