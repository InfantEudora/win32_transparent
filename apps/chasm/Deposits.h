#ifndef _CHASM_DEPOSITS_H_
#define _CHASM_DEPOSITS_H_

#include <stdint.h>
#include <vector>
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"

/*
    DEPOSITS (docs/stone_plan.md, step 4): ground holding a good to be dug - by the MINE, step 5.

    GENERATED, like the forest: from the seed, the grid and the terrain alone, so never saved, and a map always has
    the same ones. What has been dug out of one will be the simulation's, and comes with the mine. Three kinds:
      - a floatstone SEAM in a balcony's wall: a run of DEPOSIT_SEAM_PLOTS plots along the wall's foot, holding
        DEPOSIT_SEAM_FLOATSTONE. At most one a balcony. Drawn as crystal across the wall's lowest stratum along the
        run (TerrainMesh.cpp); on every plot of the run, a pile of its crystal lies fallen - rubble of floatstone,
        picked up as any rubble is (Economy's PlaceRubble).
      - THE MOUNTAIN, one deposit that never runs out: stone, from a mine at its foot - its `plots` are the plots
        touching it, on the plateau and round the pockets' floors.
      - OUTCROPS, beds of rock on a pocket's floor - the forest's `outcrop` prop, where the pockets' rock clusters
        stood - never running out either: stone, from a pit.
    Decided per balcony and per plot by a hash of the seed, not a random stream, so one does not move another.
    Kinds are numbered in the order they were added - never renumber.
*/
#define DEPOSIT_SEAM        0
#define DEPOSIT_MOUNTAIN    1
#define DEPOSIT_OUTCROP     2
#define DEPOSIT_KINDS       3
const char* DepositKindName(int kind);

//Current tuning (stone_plan.md; tuning numbers, not rules).
#define DEPOSIT_SEAM_CHANCE     0.5f    //of the balconies with a wall long enough, the share holding a seam
#define DEPOSIT_SEAM_PLOTS      4       //how far a seam runs along its wall's foot, in plots
#define DEPOSIT_SEAM_FLOATSTONE 50      //what a seam holds (the user's figure)
#define DEPOSIT_OUTCROP_CHANCE  0.06f   //of a pocket's flat plots, before the spacing: the old rock clusters' share
#define DEPOSIT_OUTCROP_APART   8.0f    //world units: no outcrop nearer another than this
#define DEPOSIT_ENDLESS         -1      //what `holds` says for a deposit that never runs out


struct Deposit{
    int kind = DEPOSIT_SEAM;
    int good = 0;               //GOOD_FLOATSTONE, GOOD_STONE
    int holds = DEPOSIT_ENDLESS;
    //A seam: the plots along its wall's foot, from where it was started outward. An outcrop: its one plot. The
    //mountain: every plot at its foot.
    std::vector<int> plots;
    std::vector<int> wall;      //a seam: the plots above its foot whose walls face it - the faces its band is drawn on
    int band = -1;              //a seam: the stratum it shows in, the one just above its foot (TerrainMesh's Band)
    int balcony = -1;           //a seam: its balcony, as a patch (Terrain::patch)
    vec2 centre;                //the middle of `plots`
};

struct DepositData{
    std::vector<Deposit> deposits;
    std::vector<int> of_plot;   //per fine vertex: the deposit it is a plot of - a seam's, an outcrop's, the mountain's foot - -1 none
    int balconies = 0;          //how many balconies off the mountain the map has, seam or not
    float build_ms = 0.0f;
    //The deposit plot `v` is a plot of, -1 none: what a mine there would dig (stone_plan.md step 5).
    int At(int v) const { return (v >= 0 && v < (int)of_plot.size()) ? of_plot[v] : -1; }
    //The seam whose foot plot `v` is, -1 none.
    int SeamAt(int v) const{
        int d = At(v);
        return (d >= 0 && deposits[d].kind == DEPOSIT_SEAM) ? d : -1;
    }
};

void BuildDeposits(const Grid& g, const GridPicker& picker, const Terrain& t, DepositData& out);
/*
    The way a mine on plot `v` digs: toward its seam's wall or into the mountain's slope - the corner round the plot
    highest above it - or (0,0) on an outcrop, which is dug where it lies. Unit, on the ground (x, z). For the mine's
    portal to face and its miner to stand before (stone_plan.md step 5).
*/
vec2 DepositDigWay(const Grid& g, const GridPicker& picker, const Terrain& t, const DepositData& d, int v);

#endif
