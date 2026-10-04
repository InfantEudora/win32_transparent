#ifndef _CHASM_GRID_PICK_H_
#define _CHASM_GRID_PICK_H_

#include <memory>
#include <vector>
#include "Grid.h"

/*
    WHAT IS UNDER A POINT ON THE GROUND: the fine cell, its coarse parent, and the plot.

    A PLOT IS A FINE VERTEX (grid_plan.md section 2) - what the player clicks and, from step 5,
    paints. On the ground it is the polygon around that vertex: from each fine quad sharing the
    vertex, the quarter cut off by the quad's centre and its two edge midpoints at that corner. So a
    point is in plot v when it is in quad q AND in q's quarter at the corner that is v. Plots meet
    3 to 6 quads and so differ in size, which is why PlotArea exists.

    Built once per grid and immutable after, like the Grid it points at, so it is shared between
    threads the same way (ApplicationChasm, "THREADS").
*/

struct GridPick{
    bool f_hit = false;
    vec2 at;                    //the point picked, x and world z
    int fine_quad = -1;
    int coarse_quad = -1;
    int plot = -1;              //the fine vertex whose plot holds the point
    int corner = -1;            //which corner of fine_quad that vertex is

    bool operator==(const GridPick& o) const{
        return f_hit == o.f_hit && fine_quad == o.fine_quad && plot == o.plot;
    }
    bool operator!=(const GridPick& o) const{ return !(*this == o); }
};

class GridPicker{
public:
    explicit GridPicker(std::shared_ptr<const Grid> g);

    GridPick Pick(const vec2& p) const;

    //Plot v's outline, as segments (pairs of points): two per quad around it.
    void PlotOutline(int v, std::vector<vec2>& segments) const;
    float PlotArea(int v) const;
    //How many fine quads share vertex v - the plot's number of sides.
    int PlotQuadCount(int v) const { return vert_start[v + 1] - vert_start[v]; }
    //The i-th of them, as quad * 4 + the corner of that quad which is v. i < PlotQuadCount(v).
    int PlotQuadCorner(int v, int i) const { return vert_quads[vert_start[v] + i]; }

    //A coarse quad's outline as fine segments, bends and all (Grid.h: its shape is its children).
    void CoarseOutline(int c, std::vector<vec2>& segments) const;
    float CoarseArea(int c) const;
    float FineArea(int q) const;

    const Grid& GetGrid() const { return *grid; }
    std::shared_ptr<const Grid> GetGridPtr() const { return grid; }

private:
    std::shared_ptr<const Grid> grid;

    //Fine quads by a uniform bucket grid over the map; each quad is in every bucket its box touches.
    vec2 origin;
    float bucket_size = 1.0f;
    int buckets_x = 0;
    int buckets_z = 0;
    std::vector<int> bucket_start;      //CSR: buckets_x * buckets_z + 1
    std::vector<int> bucket_quads;

    //Per fine vertex, the quads around it as quad * 4 + corner (CSR).
    std::vector<int> vert_start;
    std::vector<int> vert_quads;

    //The quarter of fine quad q at corner k: corner, next midpoint, centre, previous midpoint.
    void Quarter(int q, int k, vec2 out[4]) const;
};

#endif
