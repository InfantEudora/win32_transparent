#include <math.h>
#include <algorithm>

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
    RopeWeights(s,seg,0,links - 1,bones,weights);
}

void RopeWeights(float s, float seg, int first, int last, int3& bones, vec3& weights){
    int idx[3] = { first, first, first };
    float w[3] = { 0.0f, 0.0f, 0.0f };
    if (last <= first || seg <= 0.0f){
        bones = int3(first,first,first);
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
    //pins the top of the rope to the first link and the tassel to the last, and a cut end to the
    //last link on its own side.
    int used = 0;
    for (int k = 0; k < 3; k++){
        int b = raw[k];
        if (b < first){ b = first; }
        if (b > last){ b = last; }
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

/*
    Appends a rigid piece at distance `s` down the bind rope, in the rope's frame, scaled. `f_flip`
    turns it end over end - half a turn about ACROSS, so +Z runs UP the rope - which is a rotation
    and not a mirror, so the winding survives it. `section` is recorded for each vertex it adds.
*/
static void AddPiece(const std::vector<vertex>& piece, float scale, float s, const RopeMeshInput& in,
                     std::vector<vertex>& out, std::vector<int>& sections, int section,
                     bool f_flip = false){
    vec3 at = in.anchor + ROPE_DOWN * s;
    vec3 front = f_flip ? ROPE_FRONT * -1.0f : ROPE_FRONT;
    vec3 down = f_flip ? ROPE_DOWN * -1.0f : ROPE_DOWN;
    for (size_t i = 0; i < piece.size(); i++){
        vertex v = piece[i];
        const vec3& p = piece[i].pos;
        v.pos = at + ROPE_ACROSS * (p.x * scale) + front * (p.y * scale) + down * (p.z * scale);
        const vec3& n = piece[i].normal;
        v.normal = ROPE_ACROSS * n.x + front * n.y + down * n.z;
        const vec3& t = piece[i].tangent;
        v.tangent = ROPE_ACROSS * t.x + front * t.y + down * t.z;
        out.push_back(v);
        sections.push_back(section);
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
    //-1: the tile, which is sorted into sections triangle by triangle below. A piece is placed
    //whole, into the section it was put on.
    std::vector<int> sections(flat.size(),-1);

    /*
        The sections, as the first link of each: 0, then every cut strictly inside the chain,
        sorted and once each. Section k runs from link starts[k] to starts[k + 1] - 1.
    */
    float seg = in.length / (float)in.links;
    std::vector<int> starts(1,0);
    for (size_t i = 0; i < in.cuts.size(); i++){
        int j = in.cuts[i];
        if (j > 0 && j < in.links){
            starts.push_back(j);
        }
    }
    std::sort(starts.begin(),starts.end());
    starts.erase(std::unique(starts.begin(),starts.end()),starts.end());
    int count = (int)starts.size();
    starts.push_back(in.links);
    //The section a distance down the bind rope falls in: the last one starting at or above it.
    auto section_at = [&](float s){
        int k = 0;
        while (k + 1 < count && s >= (float)starts[k + 1] * seg){
            k++;
        }
        return k;
    };

    if (in.ring){
        AddPiece(*in.ring,in.ring_scale,0.0f,in,flat,sections,0);
    }
    if (in.collar){
        for (size_t i = 0; i < in.collar_at.size(); i++){
            AddPiece(*in.collar,in.collar_scale,in.collar_at[i],in,flat,sections,
                     section_at(in.collar_at[i]));
        }
    }
    if (in.tassel){
        AddPiece(*in.tassel,in.tassel_scale,in.length,in,flat,sections,count - 1);
    }
    if (in.cut_end){
        for (int k = 1; k < count; k++){
            float s = (float)starts[k] * seg;
            AddPiece(*in.cut_end,in.cut_end_scale,s,in,flat,sections,k - 1);
            AddPiece(*in.cut_end,in.cut_end_scale,s,in,flat,sections,k,true);
        }
    }

    //Weighted by how far down the rope each vertex is. The bind rope is straight, so that is just
    //its height below the anchor - for the pieces as much as the tile.
    out.reserve(flat.size());
    for (size_t t = 0; t + 2 < flat.size(); t += 3){
        /*
            ONE SECTION PER TRIANGLE, from its middle, so a tile copy that straddles a cut is
            parted along its own edges rather than stretched across the gap. The cut end is then
            ragged by up to one row of the tile's triangles, which the cut_end piece covers.
        */
        int section = sections[t];
        if (section < 0){
            vec3 mid = (flat[t].pos + flat[t + 1].pos + flat[t + 2].pos) * (1.0f / 3.0f);
            section = section_at((mid - in.anchor).dot(ROPE_DOWN));
        }
        for (size_t c = t; c < t + 3; c++){
            const vertex& v = flat[c];
            skinned_vertex o;
            o.pos = v.pos;
            o.normal = v.normal;
            o.tangent = v.tangent;
            o.uv = v.uv;
            o.matid = v.matid;
            float s = (v.pos - in.anchor).dot(ROPE_DOWN);
            RopeWeights(s,seg,starts[section],starts[section + 1] - 1,o.bones,o.weights);
            out.push_back(o);
        }
    }
    return true;
}
