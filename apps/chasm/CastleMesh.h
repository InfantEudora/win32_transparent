#ifndef _CHASM_CASTLE_MESH_H_
#define _CHASM_CASTLE_MESH_H_

#include <algorithm>
#include <cmath>
#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    THE DEBUG CASTLE, drawn (docs/ramparts_plan.md, step 2): grey stone, one fine cell at a time, a quarter for each
    corner with a piece on it - as a building's (BuildingMesh.h), so neighbouring pieces always meet.

    A piece's top is the deck its walkers stand on, and is drawn where they walk (ApplicationChasmWorkersView.cpp,
    WorkerHeight): at its plot's point the layer's height over the piece's level (ZoneCastleBase, whole storeys as a
    terrace's top), and at the edge toward a neighbour the height where the two meet (Walkers.h, WalkJoin) - so a
    stair is a ramp from the height below to its own, and blocks of one level make one level walk over a hill. Where a
    top meets a terrace's top or the hill level with it, the walk goes on onto it. Where it meets nothing - open
    ground, a lower or higher piece - it keeps its own height there, and a block or an arch has BATTLEMENTS along that
    edge (castle_plan.md step 3): corbelled out over the wall, a low wall and merlons by turns with crenels - but not
    against a wall standing a sill's height over the deck, which guards it already. Along an outer wall's foot on open
    ground, a darker PLINTH; in a block's outer wall a storey and a half tall, an arrow slit. Battlements and plinths
    are drawn over a whole cell at once (Outlines), so where two meet - round a corner, into an inner one, straight on
    into the next plot's - they are mitred to one line, and where one ends its end is closed.

      - BLOCK: solid from the ground to its top.
      - ARCH: every side of its plot opens in a round arch, crowned where the fine edge to that neighbour crosses it,
        on a pier at each corner; under its deck a groin vault, each arch's barrel running in to the plot's point. A
        neighbour's own wall closes a side where one stands; an opening onto ground no piece stands on has a ring of
        voussoirs and a keystone (the user's, 2026-10-24: "it should look like a real arch").
      - A STAIR (castle_plan.md "The stairs"): a FLIGHT's steps cut from one plane over its plot (CastleFlight),
        square to its way up, its open sides a string wall over the treads with a BALUSTRADE on it; a LANDING a deck
        with a balustrade along its open edges; an open TURRET a spiral of steps round a round core.
    A piece over a piece stands on its top: an arch's piers on it, a stair's sides on the arch it climbs from - an
    arch a stair sits on draws neither its top nor its battlements.

    Its quarters meet where a building's walls cross the fine edges (BuildingWallCrossing), so a piece and a house or a
    terrace beside it meet wall on wall. A STONE TOWER's quarters are drawn here too (BuildStoneTowerQuarter,
    castle_plan.md), so a tower and the wall beside it share their edges; a deck meeting one of its floors goes in by a
    doorway and has no battlements across it.
*/
void BuildCastleCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

/*
    A stair's FLIGHT as drawn and walked (castle_plan.md "The stairs"): ONE PLANE over its plot, square to its way up,
    down at its foot by the nearest crossing on its way down and up at its top by the nearest on its way up, level past
    either. Its steps are cut from it, so they run straight across the flight and line up from cell to cell, and a
    walker on it walks the plane (RouteHeight).
*/
struct CastleFlight{
    vec2 at;                    //its plot's point
    vec2 up;                    //its way up, unit
    float s_foot = 0.0f;        //along `up` from `at`: where it is down at its foot...
    float s_top = 0.0f;         //...and up at its top
    float foot_y = 0.0f;
    float top_y = 0.0f;
    float Along(const vec2& p) const{
        return (p - at).dot(up);
    }
    float HeightAt(const vec2& p) const{
        float t = (Along(p) - s_foot) / (s_top - s_foot);
        return foot_y + (top_y - foot_y) * std::max(0.0f,std::min(1.0f,t));
    }
};
//Plot `plot`'s flight - its piece, or a flight over an arch; false with none.
bool CastleFlightOf(const ChasmWorld& w, const ZoneState& z, int plot, CastleFlight& out);

/*
    An open TURRET as drawn and walked (castle_plan.md "The stairs"): its steps round a round core, from its level's
    ground to its top - half a turn a storey, an odd number of half turns in all, so its foot faces away from its way
    up - its last step coming out at its EXIT, the crossing on its way up. Angles are in the ground's plane, x and z,
    and climb anticlockwise; a walker goes round the steps' middle (RouteHeight).
*/
struct CastleTurret{
    vec2 at;                    //its plot's point, the core's middle
    float core = 0.0f;          //the core's radius...
    float outer = 0.0f;         //...and its steps' outer edge's
    float base_y = 0.0f;        //its level...
    float top_y = 0.0f;         //...and its top
    int steps = 0;
    float start = 0.0f;         //the first step's angle...
    float sweep = 0.0f;         //...and how far round to the last one's end, toward its exit
    vec2 exit;
    vec2 Round(float angle, float r) const{
        return at + vec2(std::cos(angle),std::sin(angle)) * r;
    }
};
bool CastleTurretOf(const ChasmWorld& w, const ZoneState& z, int plot, CastleTurret& out);

#endif
