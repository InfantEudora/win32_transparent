#ifndef _CHASM_CASTLE_MESH_H_
#define _CHASM_CASTLE_MESH_H_

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
      - STAIR: solid, its top the ramp.
    A piece over a piece stands on its top: an arch's piers on it, a stair's sides on the arch it climbs from - an
    arch a stair sits on draws neither its top nor its battlements.

    Its quarters meet where a building's walls cross the fine edges (BuildingWallCrossing), so a piece and a house or a
    terrace beside it meet wall on wall. A STONE TOWER's quarters are drawn here too (BuildStoneTowerQuarter,
    castle_plan.md), so a tower and the wall beside it share their edges; a deck meeting one of its floors goes in by a
    doorway and has no battlements across it.
*/
void BuildCastleCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif
