#include <math.h>
#include <stdio.h>

#include "Stage.h"
#include "StateHash.h"

/*
    The rules. See Stage.h for what is in here and what is reactphysics3d's.

    Nothing in this file includes an engine header, which is checkable and worth keeping that way:
    `mingw32-make.exe rules` builds it against stage_test.cpp ALONE - no core, no window, no GPU -
    and the moment it needs the engine it has stopped being testable that way.
*/

//--- Small helpers ------------------------------------------------------------------------------
//Local rather than from core/types: see the no-engine-types note at the top of Stage.h.
static float ClampF(float v, float lo, float hi){
    if (v < lo){ return lo; }
    if (v > hi){ return hi; }
    return v;
}

static float MoveToward(float value, float target, float step){
    float d = target - value;
    if (d > step){  return value + step; }
    if (d < -step){ return value - step; }
    return target;
}

static const float STAGE_EPS = 0.001f;
static const float STAGE_DEG2RAD = 3.14159265358979f / 180.0f;

//Does the archer's body box, centred at (cx,cy) when standing, overlap this rectangle? `head_drop`
//takes that much off the TOP - the kneeling box keeps its feet (see KNEEL_HALF_H).
static bool BoxOverlapsRect(float cx, float cy, float head_drop, float left, float right, float bottom, float top){
    if (cx + ARCHER_HALF_W <= left)   { return false; }
    if (cx - ARCHER_HALF_W >= right)  { return false; }
    if (cy + ARCHER_HALF_H - head_drop <= bottom) { return false; }
    if (cy - ARCHER_HALF_H >= top)    { return false; }
    return true;
}

static bool BoxOverlapsBlock(float cx, float cy, float head_drop, const StageBlock& b){
    return BoxOverlapsRect(cx,cy,head_drop,b.Left(),b.Right(),b.Bottom(),b.Top());
}

//Every place the archer is stopped by a prop asks this, and only those - the kick sweep tests its
//own box against every obstacle, blocking or not, which is what lets a straw man be kicked while
//she walks straight through it. See StageObstacle::f_blocks.
static bool ObstacleStopsBox(float cx, float cy, float head_drop, const StageObstacle& o){
    return o.f_blocks && BoxOverlapsRect(cx,cy,head_drop,o.Left(),o.Right(),o.Bottom(),o.Top());
}

//--- Construction -------------------------------------------------------------------------------

Stage::Stage(){
    Reset();
}

void Stage::Reset(){
    blocks.clear();
    props.clear();
    signs.clear();
    scenery.clear();
    waters.clear();
    biomes.clear();
    trees.clear();
    spring_plants.clear();
    branches.clear();
    ramps.clear();
    bridges.clear();
    webs.clear();
    zones.clear();
    crumble_groups.clear();
    pending_effects.clear();
    BuildLevel();
    BuildTrees();
    //In none of them yet: the first tick finds the one she lands in and reports it entered.
    zone_inside.assign(zones.size(),0);
    for (StageSpringPlant& p : spring_plants){
        p.q = p.prev_q = p.Rest();
        p.qd = 0.0f;
    }
    //An editor's moves, laid back over the code's. See KeepBlockLayout.
    if (kept_layout.size() == blocks.size()){
        for (size_t i = 0; i < blocks.size(); i++){
            blocks[i].x  = kept_layout[i].x;
            blocks[i].y  = kept_layout[i].y;
            blocks[i].hw = kept_layout[i].hw;
            blocks[i].hh = kept_layout[i].hh;
            blocks[i].z  = kept_layout[i].z;
            blocks[i].depth = kept_layout[i].depth;
        }
    }

    //Above the start ground, so the first thing the archer does is land - which exercises the
    //landing path on tick one rather than leaving it untested until the first jump.
    pos = StartPosition();
    vel = v2(0.0f,0.0f);
    mode = MODE_AIR;
    facing = 1.0f;
    f_on_ground = false;
    coyote_ticks = 0;
    buffer_ticks = 0;
    spring_on = -1;
    ramp_on = -1;
    bridge_on = -1;
    launch_lift = 0.0f;
    stomp_ticks = 0;
    spring_left = -1;
    spring_air_ticks = -1;
    f_swung = false;
    prev_aim_axis = 0.0f;
    spring_boost_seen = 0.0f;
    branch_on = -1;
    hang_branch = -1;
    lean = 0.0f;
    lean_rate = 0.0f;
    balance_ticks = 0;
    balance_entries = 0;
    vitals = StageVitals();

    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    aim_deg = BOW_AIM_NEUTRAL_DEG;
    aim_roam_ticks = 0;

    hang_block = -1;
    hang_side = -1.0f;
    climb_ticks = 0;
    climb_from = v2();
    climb_to = v2();
    grab_cooldown = 0;
    kick_ticks = 0;
    kick_cooldown = 0;
    kick_kind = KICK_FRONT;
    kneel_phase = KNEEL_LOWERING;
    kneel_ticks = 0;
    getup_ticks = 0;
    rope_id = -1;
    rope_ticks = 0;
    rope_cooldown = 0;
    rope_s = 0.0f;
    rope_climb = 0;
    rope_climbed = 0.0f;
    rope_pump = 0.0f;
    rope_points.clear();

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        arrows[i] = Arrow();
    }
    next_arrow = 0;

    ticks = 0;
    arrows_shot = 0;
    draws_cancelled = 0;
    sway_ticks = 0;
    draws_started = 0;
    arrows_hit_blocks = 0;

    //Last, over the level as built, trees' arms included. See StageEdge.
    RebuildEdges();
}

/*
    The level, in world units, with the ground's surface at y = 0.

    Hand-coded rather than loaded, which is the right trade for a prototype: the vocabulary of what
    a level even CONTAINS is still being decided, and a parser written before that is settled is a
    parser rewritten twice.

    LAID OUT AROUND THE NUMBERS IN Stage.h RATHER THAN BY EYE, which is the part worth keeping true
    as those numbers get tuned. From the constants there, a full-run jump clears 6.54 units of gap
    and lifts the feet 3.20:

        gaps            5.0 and 5.5     - clearable at a run, not from a standing start
        standable       up to 3.0       - the step, the one-way platform and the ledge
        FEET reach      3.20
        HANDS reach     5.00            - the body is 1.8 tall, so the top of it gets this high
        grabbable only  3.2 .. 5.0      - too high to land on, low enough to catch

    That last window is the whole reason the hang-and-climb slice has anywhere to prove itself, so
    the high ledge here sits at 4.2 - squarely inside it, and unreachable by any amount of skill
    until hanging exists. tools/archer_reach.py re-derives all five numbers from Stage.h.
*/
void Stage::BuildMainLevel(){
    //--- The ground, in three runs with two gaps between them ----------------------------------
    blocks.push_back({  1.00f, -2.00f, 13.00f, 2.00f, BLOCK_SOLID,  true });    //x -12 .. 14
    blocks.push_back({ 26.50f, -2.00f,  7.50f, 2.00f, BLOCK_SOLID,  true });    //x  19 .. 34
    //Out past the old end wall at 176 since 2026-09-27, to the stepping stones' pit - the test
    //ground, below, carries the level on from the pit's far side.
    blocks.push_back({ 117.75f, -2.00f, 78.25f, 2.00f, BLOCK_SOLID, true });    //x 39.5 .. 196

    //--- Traversal ------------------------------------------------------------------------------
    blocks.push_back({  7.00f,  0.90f,  2.00f, 0.90f, BLOCK_SOLID,  true });    //a step, top at 1.8
    //One-way: jump up through it from below, drop back through it by holding Down. Thin, because
    //a one-way platform you can stand on the underside of is a bug waiting to be reported - and
    //0.3 thick against a 0.57-unit terminal-velocity tick is exactly why MoveAndCollide substeps.
    blocks.push_back({ 23.50f,  2.65f,  2.50f, 0.15f, BLOCK_PLATFORM, true });  //top at 2.8
    //A LEDGE differs from a SOLID only in advertising its top corners as grabbable. It collides
    //identically, so this one is an ordinary platform until the hang slice reads the kind.
    blocks.push_back({ 30.50f,  1.30f,  2.50f, 1.30f, BLOCK_LEDGE,  true });    //top at 2.6
    /*
        THE ONE THAT CANNOT BE JUMPED ONTO. Top at 4.2, against feet that reach 3.20 and hands
        that reach 5.00 - so it sits in the grabbable-only window and no amount of play gets the
        archer up there until hanging exists. Scenery today, the hang slice's acceptance test
        tomorrow, and deliberately standing on open ground so that nothing higher can be used to
        cheat the approach.
    */
    blocks.push_back({ 46.00f,  2.10f,  2.00f, 2.10f, BLOCK_LEDGE,  true });    //top at 4.2

    /*
        THE ROPE BRIDGE (docs/bridge_crumble_plan.md section 3), up over the first gap: the step, then
        two floating slabs, then the bridge across to a third above the one-way platform.

          step      1.8   x  5 .. 9, the start's
          one       4.4   x 10.5 .. 12.5 - a hop up and across from the step, 2.6 up
          two       7.0   x 14.5 .. 16.5 - a running jump across from one, 2.6 up
          bridge    7.0   x 16.5 .. 24, hung from two's corner to three's, sagging about 0.9
          three     7.0   x 24 .. 26
          bridge 2  7.0   x 26 .. 30.5, the one that SNAPS - see STRAIN in Stage.h
          four      7.0   x 30.5 .. 31.9

        EVERYTHING HERE IS CLEAR OF THE GROUND ROUTE, and that sets the heights: slab one's
        underside at 3.8 is headroom to run under it; two and three have theirs at 6.4, above the
        5.0 a jump across the gap lifts her head and the 3.6 of room left standing on the one-way
        platform. The bridge's lowest, under her weight, stays above that jump's head too.

        THE SECOND BRIDGE holds a gentle crossing and gives way to a few hard landings; snapped,
        its halves hang from three and four above the ledge, and the way on is the ground again.
        Slab four stops short of 32: the rope is caught by a running jump off the ledge's end at
        33, which lifts her head to 7.6 over x 32.3 on - a slab there took that jump's head off.

        SOLID, not LEDGE, so none of them is the first high ledge stage_test's hang tests take.
        stage_test proves the climb and the crossings as route checks.
    */
    blocks.push_back({ 11.50f,  4.10f,  1.00f, 0.30f, BLOCK_SOLID,  true });    //slab one,   top 4.4
    blocks.push_back({ 15.50f,  6.70f,  1.00f, 0.30f, BLOCK_SOLID,  true });    //slab two,   top 7.0
    blocks.push_back({ 25.00f,  6.70f,  1.00f, 0.30f, BLOCK_SOLID,  true });    //slab three, top 7.0
    //Slab four is at the END of this function - see there for why.
    AddBridge(v2(16.50f,7.00f),v2(24.00f,7.00f),12,1.04f);
    AddBridge(v2(26.00f,7.00f),v2(30.50f,7.00f),7,1.04f,true);

    //A cracked wall across the path, 2.5 tall. Solid to the archer and to arrows until the
    //kick-and-break slice knocks it out - so for now the target behind it has to be LOBBED over,
    //which is the most interesting thing a bow can be asked to do and wants no extra code.
    blocks.push_back({ 57.00f,  1.25f,  0.50f, 1.25f, BLOCK_BREAKABLE, true });

    /*
        THE TREE, blocked out (docs/plant_mechanics_plan.md, section 1): the way UP between two slabs
        that the ground cannot reach - 5.5 and 10.2 against feet that reach 3.20 and hands 5.00.

        Three arms, right-left-right, 2.5 apart: each a hop of 2.5 against a 3.2 apex, and the
        next arm 2.3-ish across. The second arm's tip is 1.55 from the low slab, half a unit
        below its top; the third's is 1.55 from the high slab and 2.7 below it. stage_test climbs
        it hop by hop, so moving any of this says at once whether it is still climbable.
    */
    blocks.push_back({ 73.00f,  5.00f,  3.00f, 0.50f, BLOCK_SOLID,  true });    //low slab, x 70..76, top 5.5
    blocks.push_back({ 90.50f,  9.70f,  6.50f, 0.50f, BLOCK_SOLID,  true });    //high slab, x 84..97, top 10.2
    {
        StageTree tree;
        tree.x = 80.00f;
        tree.base = 0.00f;
        tree.height = 11.00f;
        tree.radius = 0.45f;
        tree.arms.push_back({ 2.50f,  1.0f, 2.00f });
        tree.arms.push_back({ 5.00f, -1.0f, 2.00f });
        tree.arms.push_back({ 7.50f,  1.0f, 2.00f });
        trees.push_back(tree);
    }

    /*
        THE SPRING PLANTS, blocked out (docs/plant_mechanics_plan.md, section 2), past the tree's high
        slab: a way up that only TIMING opens, twice.

        The pad at x 105, its cap 1.2 up: a plain jump off it reaches 4.4 and the shelf's top is
        7.5, so only a jump timed to the cap's rebound (her jump plus its rise) gets there. Holding
        right through the fling carries her the 4 units across to the shelf's end.

        The leaf grows off the shelf's right end, rising 10 degrees and 4.5 long. Near the stem it
        holds her; walked out along it, it bends under her past SPRING_LEAF_SLIP_DEG and she slides
        off the tip to the ground. Bounced on, it throws her up to the canopy at 13.0 - out of reach
        of a jump from the shelf (hands 12.5) or from the leaf (feet about 9.7).

        stage_test (TestSpringPlants) plays each of those, so retuning a spring or moving a block
        says at once whether the timing still opens the way and a plain jump still does not.
    */
    {
        StageSpringPlant pad;
        pad.kind = SPRING_PAD;
        pad.root = v2(105.00f,1.20f);
        pad.base = 0.00f;
        pad.length = 2.40f;
        pad.give = 0.12f;           //she sinks it this far standing on it
        pad.hz = 5.00f;             //a quick wobble alone; about 2.6 Hz with her on it
        pad.damping = 0.20f;
        pad.travel = 1.10f;         //down to 0.1 above the ground, never through it
        spring_plants.push_back(pad);
    }
    blocks.push_back({ 114.00f, 7.00f,  4.00f, 0.50f, BLOCK_SOLID,  true });    //shelf, x 110..118, top 7.5
    {
        //Its stem half a unit in from the shelf's end, level with the top: the shelf holds her there
        //whatever the leaf does, and SPRING_STEP_UP walks her back onto it off a bent one.
        StageSpringPlant leaf;
        leaf.kind = SPRING_LEAF;
        leaf.root = v2(117.50f,7.50f);
        leaf.side = 1.0f;
        leaf.length = 4.50f;
        leaf.rest_deg = 10.0f;
        leaf.give = 35.0f;          //degrees her weight bends it with her at the tip
        leaf.hz = 2.00f;
        leaf.damping = 0.30f;
        leaf.travel = 60.0f;        //from 70 up to 50 down
        spring_plants.push_back(leaf);
    }
    blocks.push_back({ 129.00f, 12.50f, 4.00f, 0.50f, BLOCK_SOLID,  true });   //canopy, x 125..133, top 13.0

    /*
        THE BRANCHES, blocked out (docs/plant_mechanics_plan.md, section 3): a balance walk, twice.

        The high one runs from the canopy's right end down to a perch, 12 long and 0.6 down: the
        one to cross. The low one is for practice, 2.6 up between two stumps, so going over costs
        little - she can jump onto either stump or the branch itself from the ground (3.2 of rise).

        NO LOWER THAN 2.6, because going over HANGS her from it (BRANCH_HANG_DROP), and the hang
        needs her height plus the drop under it: 2.11. At 2.0 her feet went 0.11 into the ground.
        stage_test checks every branch has that room.

        stage_test (TestBranch) walks both with a player that reacts late, as a person does, and
        checks that doing nothing gets her off.
    */
    branches.push_back({ v2(133.00f,13.00f), v2(145.00f,12.40f) });
    blocks.push_back({ 148.50f, 12.00f,  3.50f, 0.40f, BLOCK_SOLID,  true });   //perch, x 145..152, top 12.4
    blocks.push_back({ 156.00f,  1.30f,  1.00f, 1.30f, BLOCK_SOLID,  true });   //stump, x 155..157, top 2.6
    blocks.push_back({ 169.00f,  1.30f,  1.00f, 1.30f, BLOCK_SOLID,  true });   //stump, x 168..170, top 2.6
    branches.push_back({ v2(157.00f,2.60f), v2(168.00f,2.60f) });

    /*
        THE TEST GROUND, x 176..264: flat and empty, past where the level used to end. It is where
        docs/bridge_crumble_plan.md's pieces are blocked out - the bridges, the stepping stones, the
        chase - each added here as it is built, so nothing else in the level has to move for them.
    */

    /*
        THE STEPPING STONES (docs/bridge_crumble_plan.md section 2): a pit 18 wide and 4 deep, x 196..214,
        with four crumble stones across it level with the ground. Each hop is short - 2.4 to 2.9 of
        gap against a running jump's 6.5 - so the stones ask for rhythm, not reach: a stone holds for
        CRUMBLE_SHAKE_TICKS after she lands, so she has to keep going.

        THE DETOUR is the pit itself. Its floor is 4 below the rim, too high to jump out of (3.2)
        but inside the grab window (hands reach 5.0), and the far rim is a LEDGE - so a fall is a
        walk to the far wall, a jump, a catch and a climb. The near rim is plain SOLID: the way out
        is onward. stage_test proves both routes, the stones and the pit with every stone gone.
    */
    blocks.push_back({ 205.00f, -6.00f,  9.00f, 2.00f, BLOCK_SOLID,   true });   //the pit's floor, x 196..214, top -4
    blocks.push_back({ 217.00f, -2.00f,  3.00f, 2.00f, BLOCK_LEDGE,   true });   //the far rim, x 214..220
    blocks.push_back({ 199.50f, -0.30f,  0.60f, 0.30f, BLOCK_CRUMBLE, true });   //stone one,   x 198.9..200.1, top 0
    blocks.push_back({ 203.50f, -0.30f,  0.60f, 0.30f, BLOCK_CRUMBLE, true });   //stone two
    blocks.push_back({ 207.50f, -0.30f,  0.60f, 0.30f, BLOCK_CRUMBLE, true });   //stone three
    blocks.push_back({ 211.00f, -0.30f,  0.60f, 0.30f, BLOCK_CRUMBLE, true });   //stone four,  x 210.4..211.6

    /*
        THE CHASE (docs/bridge_crumble_plan.md section 2): a floor of crumble slabs over a second pit,
        x 220..252, that falls away BEHIND her. Stepping onto its first slab enters a trigger that
        starts the group, and from there a slab goes every CHASE_TICKS_PER_UNIT ticks per unit of
        floor - a front at 7.5 a second behind her 9, so a clean run gains on it and a hesitation
        at the start is paid for at once. One slab is left out at 236..238, a hole to jump while
        running: the stumble the front's margin has to allow for.

        Solid ground at the end, 252 on, and a LEDGE: the pit under the floor is the stones' again,
        4 deep, so a fall is a walk to the far wall and a catch and a climb. The rim between the
        two pits is a ledge on both sides, so either pit can be climbed out onto it.

        The slabs start only from the group, never under her feet (StageBlock::crumble_group) -
        a floor that went where she stood would not be a chase.
    */
    blocks.push_back({ 236.00f, -6.00f, 16.00f, 2.00f, BLOCK_SOLID,   true });   //the chase pit's floor, x 220..252, top -4
    blocks.push_back({ 259.00f, -2.00f,  7.00f, 2.00f, BLOCK_LEDGE,   true });   //solid ground past it, x 252..266
    const size_t chase_first = blocks.size();
    for (float left = 220.0f; left < 251.9f; left += 2.0f){
        if (left > 235.9f && left < 236.1f){
            continue;               //the hole
        }
        blocks.push_back({ left + 1.0f, -0.30f, 1.00f, 0.30f, BLOCK_CRUMBLE, true });
    }
    {
        int group = AddCrumbleGroup("chase",chase_first,CHASE_TICKS_PER_UNIT);
        StageZoneEffect start;
        start.kind = ZONE_START_CRUMBLE_GROUP;
        start.target = group;
        //Over the first slab and tall, so a jump from the rim over it still sets it off; its
        //bottom above the pit, so climbing about down there does not.
        AddTrigger("chase start",220.0f,222.0f,-1.0f,9.0f,start);
    }

    //The right-hand wall, so a run to the end stops rather than falling off the world. Tall
    //enough that a jump off the canopy cannot clear it: 13.0 + 3.2 + her 1.8 is 18.0. At 175
    //until the test ground went in beyond it.
    blocks.push_back({ 265.00f, 10.00f,  1.00f, 10.00f, BLOCK_SOLID, true });

    /*
        --- Zones: the level's areas, by what is in them ----------------------------------------------

        Side by side, covering the level from the terrain bay to the end wall, so she is always in
        exactly one and the HUD always has a name to show. Tall enough for everything above the
        ground - the canopy's 13, the island in the bay, a jump off either. Each `arrive` is a spot
        on the ground at the zone's left end, clear of props and blocks, where a teleport lands her
        with the area in front of her; stage_test drops her on every one.
    */
    const float zb = -6.0f, zt = 30.0f;
#if ARCHER_TEST_BAY
    //From two units past the far wall, so a teleport or a stray step at the wall is still in it.
    AddZone("Cave",             ARCHER_CAVE_X_MIN - 2.0f, ARCHER_TEST_BAY_X_MIN, zb, zt, v2(-44.00f,0.30f));
#endif
    AddZone("Terrain bay",      ARCHER_TEST_BAY_X_MIN, ARCHER_TEST_BAY_X_MAX, zb, zt, v2(-24.00f,0.30f));
    AddZone("Start",            ARCHER_TEST_BAY_X_MAX,  14.0f, zb, zt, v2( -6.00f,0.30f));
    AddZone("Gaps and rope",     14.0f,  39.5f, zb, zt, v2( 21.00f,0.30f));
    AddZone("Ledges and walls",  39.5f,  66.0f, zb, zt, v2( 41.50f,0.30f));
    AddZone("Tree",              66.0f, 100.0f, zb, zt, v2( 77.50f,0.30f));
    AddZone("Spring plants",    100.0f, 134.0f, zb, zt, v2(101.00f,0.30f));
    AddZone("Branches",         134.0f, 176.0f, zb, zt, v2(153.00f,0.30f));
    //Up in the air over the first two, from slab one's top to above the far anchor: a narrow area
    //inside the wide ones, which CurrentZone names while she is up there. Arrives on slab two.
    AddZone("Bridge",            10.5f,  25.0f, 5.2f, 16.0f, v2( 15.50f,7.00f));
    //And the snapping one past it, arriving on slab three.
    AddZone("Snapping bridge",   25.0f,  32.5f, 5.2f, 16.0f, v2( 25.20f,7.00f));
    //The test ground, a zone per piece as each is built and the rest still "Test ground".
    AddZone("Stepping stones",  176.0f, 214.0f, zb, zt, v2(192.00f,0.30f));
    //From the rim, so a teleport lands her before the chase's trigger rather than setting it off.
    AddZone("Chase",            214.0f, 254.0f, zb, zt, v2(216.00f,0.30f));
    AddZone("Test ground",      254.0f, 264.0f, zb, zt, v2(256.00f,0.30f));

    //--- Props: everything reactphysics3d owns --------------------------------------------------
    /*
        Kickable crates by the start, so the very first thing in reach proves the archer's
        hand-swept body really does shove a solved rigid body around.

        THE LONE ONE NEEDS RUNWAY, which it did not have. It sat at 3.00 with its right edge at
        3.40 and the stack's left edge at 3.70: a kick connected, reported "1 props", and moved it
        0.3 units into the stack, which is in turn 0.5 from the step's face at x 5.0. The whole
        cluster was jammed against the step, so the one thing the demo exists to show - a crate
        being punted - could not happen. Moved left to 0.60, which opens 2.7 units of clear ground
        in front of it.

        The stack stays where it is on purpose: two crates reach 1.65 and the step's top is 1.80,
        so it is the way UP there, and that only works while it is beside the step.
    */
    props.push_back({ PROP_CRATE, 0.60f, 0.40f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, 4.10f, 0.40f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, 4.10f, 1.25f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, -1.10f, 1.25f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, -1.10f, 2.25f, 0.80f, 0.80f, 1, 1 });

    /*
        Four targets, each demanding a different shot. This is the slice-one exercise, and it is
        the reason the level is shaped the way it is:

          across the first gap, flat     - a fast, nearly straight shot at full draw
          under the one-way platform     - low and flat, with a ceiling in the way
          on top of the far ledge        - upward, and short enough that a full draw overshoots
          beyond the cracked wall        - a lob, the only way over 2.5 units of wall

        w/h here are the board's full size, and the app stands it up as a dynamic body so a hit
        knocks it over instead of just scoring. The two on open ground are archery stands - see
        TargetVariant - which score by ring and take a kick, not an arrow, to knock down.
    */
    props.push_back({ PROP_TARGET, 20.00f, 0.70f, 0.70f, 1.40f, 1, 1, false, TARGET_STAND });  //across the first gap
    props.push_back({ PROP_TARGET, 24.50f, 0.80f, 0.30f, 1.60f, 1, 1 });   //under the one-way, 2.5 of headroom
    props.push_back({ PROP_TARGET, 31.00f, 3.40f, 0.30f, 1.60f, 1, 1 });   //standing on the ledge
    props.push_back({ PROP_TARGET, 62.00f, 0.70f, 0.70f, 1.40f, 1, 1, false, TARGET_STAND });  //behind the cracked wall

    //For the kick-and-break slice. w/h are the WHOLE wall; cols/rows subdivide it into bricks.
    props.push_back({ PROP_BRICKWALL, 49.50f, 1.60f, 2.70f, 3.15f, 3, 7 });

    //For the rope slice. x/y is the fixed anchor point, h the length of rope hanging from it -
    //over the second gap, because a rope you can walk around is a rope nobody swings on.
    props.push_back({ PROP_ROPE_ANCHOR, 36.75f, 9.00f, 0.10f, 6.00f, 1, 1 });

#if ARCHER_TEST_BAY
    /*
        --- The terrain test bay, x -40 .. -12 -------------------------------------------------
        Left of the start and contiguous with the main ground run, which ends at x -12.
        CONTIGUOUS MATTERS: TickArcher restarts the game below y -40, so ground floating in space
        would drop the player out of the world on the way into it.

        Two bays - see ARCHER_TEST_BAY in Stage.h. This is the starting layout; the point of the
        bays now is to move these boxes in the editor and regenerate, so read it as a sketch.

        See the SOLID-only and append-at-the-end rules in the ARCHER_TEST_BAY note in Stage.h.
    */
    /*
        The cave's MOUTH: a lip hanging from the roof over the way in (docs/cave_plan.md). This was the
        bay's left-hand wall, the level's end on this side, before the cave went in beyond it;
        still the same block, so no index after it moves. Its underside at 6.0 is clear of her
        head at the top of a full jump (5.0), so she runs and jumps through without a bonk. Deep,
        like the roof it hangs from, and above the split, so it melts with the roof.
    */
    blocks.push_back({ ARCHER_TEST_BAY_X_MIN,  7.60f, 0.60f, 1.60f, BLOCK_SOLID, true, false, -2.25f, 3.75f }); //lip, x -40.6..-39.4, y 6.0..9.2

    //ONE floor under the whole ground bay. It used to be four abutting segments, one per bay, and
    //a smooth union over a seam between two equal tops lifts the surface there by up to k/4.
    blocks.push_back({ (ARCHER_TEST_BAY_X_MIN + ARCHER_TEST_BAY_X_MAX) * 0.5f, -2.00f,
                       (ARCHER_TEST_BAY_X_MAX - ARCHER_TEST_BAY_X_MIN) * 0.5f, 2.00f,
                       BLOCK_SOLID, true });                                        //floor, top 0

    /*
        The ground: one low mound on the left and one hill on the right, and nothing else. It used
        to be four copies of a step, a wall and a pillar - each the cheapest shape that exposed one
        meshing failure - which did their job and read as a row of teeth once the terrain looked
        like terrain. Both are under 3.0 tall, so she can cross either way on foot and nothing down
        here can pen her in.
    */
    /*
        THE LAST TWO NUMBERS ON EACH OF THESE ARE z AND HALF-DEPTH (StageBlock::z, depth), and
        they are looks only - set back a little or thinned, so the pieces stop reading as one
        slab cut out with a biscuit cutter. Every one still covers STAGE_BLOCK_MIN_COVER either
        side of the walk line, which stage_test checks.
    */
    blocks.push_back({ -35.00f,  0.40f, 2.50f, 0.40f, BLOCK_SOLID, true, false, -0.25f, 1.20f }); //mound, top 0.8
    blocks.push_back({ -14.50f,  1.20f, 1.50f, 1.20f, BLOCK_SOLID, true, false,  0.00f, 1.30f }); //hill,  x -16..-13, top 2.4

    /*
        The upper bay: an island floating clear of everything below - its underside at 9.75, far
        above the 3.2 a jump lifts the feet. A slab, a hill on it, and one spike hanging under it,
        so the island has a top, a bump and an underside to look at.
    */
    blocks.push_back({ -26.00f, 10.50f, 8.00f, 0.75f, BLOCK_SOLID, true });         //slab,  top 11.25
    blocks.push_back({ -29.00f, 11.75f, 1.50f, 0.50f, BLOCK_SOLID, true, false, -0.30f, 1.10f }); //hill,  top 12.25
    blocks.push_back({ -21.00f,  9.00f, 0.40f, 1.00f, BLOCK_SOLID, true, false,  0.10f, 0.80f }); //spike, bottom 8

    /*
        THE WAY UP: the hill, then three stones, zig-zagging up the island's right end - 2.4 a rise,
        well inside the 3.2 a jump lifts the feet. Each stone is placed so that NOTHING IS OVER HER
        HEAD on the jump that reaches it: the first layout had the third stone between the second
        and the island, and the jump from the first stone to the second bonked on its underside
        and fell short. So the third is on the far side, and the last move is a running jump LEFT
        onto the island, over open air.

          hill   2.4    x -16 .. -13
          one    4.8    x -20.5 .. -18.2, tucked under the island's end (headroom 4.95)
          two    7.2    x -15.3 .. -13.3, a 2.9 gap back to the right
          three  9.6    x -13.8 .. -12.0, stepped onto from the left end of two
          island 11.25  a 4.2 gap, 1.65 up

        stage_test's TestBayClimb plays this route and searches every jump for anything else in the
        bay she can stand on - move a stone and it says whether the island is still reachable.
        Stone one's centre is below ARCHER_TEST_BAY_SPLIT_Y, so it melts with the ground bay rather
        than the island's; it is floating either way.
    */
    blocks.push_back({ -19.35f,  4.40f, 1.15f, 0.40f, BLOCK_SOLID, true, false,  0.15f, 1.05f }); //stone one,   top 4.8
    blocks.push_back({ -14.30f,  6.80f, 1.00f, 0.40f, BLOCK_SOLID, true, false, -0.20f, 1.00f }); //stone two,   top 7.2
    blocks.push_back({ -12.90f,  9.20f, 0.90f, 0.40f, BLOCK_SOLID, true, false,  0.10f, 0.90f }); //stone three, top 9.6

    /*
        Two floaters to look at and never stand on. The first hangs under the middle of the island,
        where a jump from the ground falls 3 short of it and the island's own underside stops
        anyone dropping in from above: 10 from stone one's edge and 3 in from the island's left
        end, with a ceiling at 9.75 flattening every arc. The second is above the island,
        its underside past the 15.45 a jump from the island's hill reaches.
    */
    blocks.push_back({ -30.00f,  6.50f, 1.00f, 0.30f, BLOCK_SOLID, true, false, -0.35f, 1.00f }); //under the island, top 6.8
    blocks.push_back({ -23.00f, 16.10f, 1.20f, 0.30f, BLOCK_SOLID, true, false, -0.40f, 1.10f }); //over it, bottom 15.8

    /*
        The waterfall (docs/water_plan.md), behind the middle of the ground: over the back wall BEHIND
        the island, into a pool on a shelf 2.6 up, over the shelf's front and away LEFT along the
        gap behind the ground, to the bay's left end - where the cave will be. Between the floater
        under the island (x -31 .. -29) and stone one (-20.5), so neither stands in front of it.

        The lip at 12.5 is where the island hides it from the ground: the camera there sees the
        wall behind the island from about 11.4 to 13.4. At 8.5, under the island, the notch was a
        pale window onto the painted backdrop with the water coming out of the bottom of it.
    */
    StageWater fall;
    fall.x = -26.0f;
    fall.lip_y = 12.5f;
    //Into the cave, and on to just short of its far wall.
    fall.stream_x_end = ARCHER_CAVE_X_MIN + 4.0f;
    waters.push_back(fall);
#endif

    /*
        The two authored terrain tiles, floating to the right of stone three in the play plane, so
        they can be judged against the marching-cubes stones beside them - and stood on. The big
        one level with stone three across a 2.9 gap, the round one 2.05 further and 1.2 down.

        The colliders are the tiles' walkable tops as measured off the meshes at her scale (the
        app re-measures and warns if these drift): the big one 3.21 wide with a flat underside
        1.52 below, the round one 3.79 wide - taken in a little, its lip rolls over - and only 1.0
        deep, because its rock tapers away underneath and a full-width box down to the tip would
        bump her head on air.
    */
    AddScenery({ SCENERY_TILE_BIG,   -7.50f, 9.60f, 0.00f, 0.0f, 1.60f, 0.76f });
    AddScenery({ SCENERY_TILE_ROUND, -2.00f, 8.40f, 0.00f, 0.0f, 1.85f, 0.50f });

    /*
        SLAB FOUR, the snapping bridge's right anchor (see THE ROPE BRIDGE above), LAST so that no
        block before it moves in the list. The dressing is seeded by block INDEX - the bay's backdrop
        ridges, its trees, the boulders - so a block added up with the others reshuffled the whole
        dressed start behind her, and stage_test's backdrop check with it. New level blocks that
        are not part of the bay go here, after everything the dressing is built from.
    */
    blocks.push_back({ 31.20f,  6.70f,  0.70f, 0.30f, BLOCK_SOLID,  true });    //slab four,  x 30.5..31.9, top 7.0

#if ARCHER_TEST_BAY
    /*
        --- The cave, x -66 .. -40 (docs/cave_plan.md) --------------------------------------------------
        Last, after slab four, by the rule above: nothing before them moves.

        A FLOOR OF ITS OWN rather than the bay's carried on, and that is to keep the bay as it is:
        the bank behind a ground block is laid in columns across its length, so a longer floor
        would have redrawn the whole skyline behind the bay - and the vines laid on its crest by
        hand. The two floors meet under the mouth, where the smooth union's lift at a seam (k/4,
        under a tenth) is a threshold rather than a bump in the open.

        The ROOF and the FAR WALL are deep, back to z -6: past the bank's face (-4.5), so the bank
        closes behind them with no gap for the painted sky to show through, and Backdrop.cpp raises
        the bank over them wherever they reach back into it.
    */
    blocks.push_back({ (ARCHER_CAVE_X_MIN + ARCHER_TEST_BAY_X_MIN) * 0.5f, -2.00f,
                       (ARCHER_TEST_BAY_X_MIN - ARCHER_CAVE_X_MIN) * 0.5f, 2.00f,
                       BLOCK_SOLID, true });                                        //cave floor, top 0
    blocks.push_back({ (ARCHER_CAVE_X_MIN + ARCHER_TEST_BAY_X_MIN) * 0.5f, ARCHER_CAVE_ROOF_Y + 1.00f,
                       (ARCHER_TEST_BAY_X_MIN - ARCHER_CAVE_X_MIN) * 0.5f, 1.00f,
                       BLOCK_SOLID, true, false, -2.25f, 3.75f });                  //roof, 9 .. 11
    blocks.push_back({ ARCHER_CAVE_X_MIN + 1.00f, (ARCHER_CAVE_ROOF_Y + 2.00f) * 0.5f,
                       1.00f, (ARCHER_CAVE_ROOF_Y + 2.00f) * 0.5f,
                       BLOCK_SOLID, true, false, -2.25f, 3.75f });                  //far wall, x -66..-64, 0 .. 11

    /*
        And its BIOME - the inside, floor to roof, far wall to mouth. Its own plants and rocks
        (FoliageBiomeFor, BoulderBiomeFor) and STILL AIR: the wind goes over the cave, not
        through it, which is also what keeps the leaves and streaks out.
    */
    StageBiome cave;
    cave.kind = BIOME_CAVE;
    cave.name = "Cave";
    cave.x = (ARCHER_CAVE_X_MIN + ARCHER_TEST_BAY_X_MIN) * 0.5f;
    cave.hw = (ARCHER_TEST_BAY_X_MIN - ARCHER_CAVE_X_MIN) * 0.5f;
    cave.y = ARCHER_CAVE_ROOF_Y * 0.5f;
    cave.hh = ARCHER_CAVE_ROOF_Y * 0.5f;
    cave.f_still_air = true;
    //The mouth's daylight: the jungle's dressing thins over the first six units in.
    cave.fade_right = 6.0f;
    biomes.push_back(cave);

    /*
        ON THE ROOF: a tree and a mushroom, the first drawn with the art (docs/plant_mechanics_plan.md,
        sections 1 and 2) - the one at x 80 stays the blockout. Up from the island by a running
        jump left onto the roof, then a way up the timing opens and the arms carry on: the tree's
        first arm, 16.5, is past a plain jump off the roof (feet 14.2, hands 16.0) and off the cap
        (12.15) too, so only a bounce timed to the cap's rebound reaches it. The pad stands under
        that arm's tip, so the bounce goes straight up through it. Then the arms as at x 80, 2.5
        apart right-left-right, and a 2.0 hop from the third onto the cut top at 23.5 - the
        highest place in the bay, looking out over it.

        No blocks: the arms and the top are the tree's (BuildTrees appends them after every level
        block, behind the tree at x 80's), and a pad is a spring, so no index the dressing is
        seeded by moves. stage_test (TestRoofTree) plays the whole way up.
    */
    {
        const float roof_top = ARCHER_CAVE_ROOF_Y + 2.0f;
        StageTree tree;
        tree.x = -54.00f;
        tree.base = roof_top;
        tree.height = 12.50f;
        tree.radius = BIGTREE_RADIUS;
        tree.top_width = BIGTREE_TOP_WIDTH;
        tree.f_bigtree = true;
        tree.arms.push_back({ 16.50f,  1.0f, BIGTREE_ARM_RIGHT_LENGTH });
        tree.arms.push_back({ 19.00f, -1.0f, BIGTREE_ARM_LEFT_LENGTH });
        tree.arms.push_back({ 21.50f,  1.0f, BIGTREE_ARM_RIGHT_LENGTH });
        trees.push_back(tree);

        //The same feel as the pad at x 105, which stage_test tuned: only the cap is the mushroom's.
        StageSpringPlant pad;
        pad.kind = SPRING_PAD;
        pad.root = v2(tree.x + 1.90f,roof_top + MUSHROOM_BIG_CAP_TOP);
        pad.base = roof_top;
        pad.length = MUSHROOM_BIG_CAP_WIDTH;
        pad.give = 0.12f;
        pad.hz = 5.00f;
        pad.damping = 0.20f;
        pad.travel = 1.00f;         //down to 0.15 above the roof - a shorter stalk than x 105's
        pad.f_mushroom = true;
        spring_plants.push_back(pad);
    }
#endif
}

int BiomeAt(const std::vector<StageBiome>* biomes, float x, float y, float* weight){
    if (weight){
        *weight = 1.0f;
    }
    if (biomes){
        for (const StageBiome& b : *biomes){
            if (!b.Contains(x,y)){
                continue;
            }
            if (weight){
                float w = 1.0f;
                if (b.fade_left > 0.0f){
                    w = fminf(w,(x - b.Left()) / b.fade_left);
                }
                if (b.fade_right > 0.0f){
                    w = fminf(w,(b.Right() - x) / b.fade_right);
                }
                *weight = (w < 0.0f) ? 0.0f : w;
            }
            return b.kind;
        }
    }
    return BIOME_JUNGLE;
}

void Stage::AddScenery(const StageScenery& s){
    scenery.push_back(s);
    if (s.collider_hw > 0.0f && s.collider_hh > 0.0f){
        StageBlock b = { s.x, s.y - s.collider_hh, s.collider_hw, s.collider_hh, BLOCK_SOLID, true };
        b.f_invisible = true;
        blocks.push_back(b);
    }
}

void Stage::KeepBlockLayout(){
    kept_layout = blocks;
    //The editor has moved boxes: the one change the tick's count of live blocks cannot see.
    RebuildEdges();
}

void Stage::BuildLevel(){
    switch (level){
        case STAGE_LEVEL_RANGE: BuildRangeLevel(); break;
        case STAGE_LEVEL_ROPE:  BuildRopeLevel();  break;
        case STAGE_LEVEL_CHARACTER: BuildCharacterLevel(); break;
        case STAGE_LEVEL_WEB:   BuildWebLevel();   break;
        default:               BuildMainLevel();  break;
    }
}

/*
    Every arm, a one-way platform from the trunk's face out to its tip, its top where the tree
    says. Appended after the level's own blocks, so the blocks a level declares keep their indices
    (stage_test's HighLedge and an editor's kept layout both count on them).
*/
void Stage::BuildTrees(){
    for (size_t ti = 0; ti < trees.size(); ti++){
        const StageTree& t = trees[ti];
        for (const StageTreeArm& a : t.arms){
            StageBlock b;
            b.tree = (int)ti;
            b.hw = a.length * 0.5f;
            b.x = t.x + a.side * (t.radius + b.hw);
            b.hh = STAGE_TREE_ARM_HALF_H;
            b.y = a.top - b.hh;
            b.kind = BLOCK_PLATFORM;
            b.z = STAGE_TREE_ARM_Z;
            b.depth = STAGE_TREE_ARM_HALF_DEPTH;
            blocks.push_back(b);
        }
        //Its cut top, if she can stand there: one-way like the arms, so the hop onto it from the
        //arm below comes up through it rather than bumping her head on it.
        if (t.top_width > 0.0f){
            StageBlock b;
            b.tree = (int)ti;
            b.hw = t.top_width * 0.5f;
            b.x = t.x;
            b.hh = STAGE_TREE_ARM_HALF_H;
            b.y = t.base + t.height - b.hh;
            b.kind = BLOCK_PLATFORM;
            b.z = STAGE_TREE_ARM_Z;
            b.depth = STAGE_TREE_ARM_HALF_DEPTH;
            blocks.push_back(b);
        }
    }
}

//--- Spring plants ------------------------------------------------------------------------------

float StageSpringPlant::Rest() const{
    return (kind == SPRING_LEAF) ? rest_deg * STAGE_DEG2RAD : 0.0f;
}

float StageSpringPlant::Travel() const{
    return (kind == SPRING_LEAF) ? travel * STAGE_DEG2RAD : travel;
}

/*
    From `give`: her weight, g, held still by the spring at that much displacement. A leaf feels her
    weight through its lever - the tip's distance out at rest - so the angle `give` names is the one
    she bends it to standing at the tip.
*/
float StageSpringPlant::Stiffness() const{
    if (kind == SPRING_LEAF){
        float g = (give > 0.1f ? give : 0.1f) * STAGE_DEG2RAD;
        return ARCHER_GRAVITY * length * cosf(Rest()) / g;
    }
    return ARCHER_GRAVITY / (give > 0.01f ? give : 0.01f);
}

//From `hz`, alone: the inertia that swings at that rate against this stiffness.
float StageSpringPlant::Inertia() const{
    float w = 2.0f * 3.14159265358979f * (hz > 0.1f ? hz : 0.1f);
    return Stiffness() / (w * w);
}

//From `damping`, alone: that fraction of critical.
float StageSpringPlant::Damping() const{
    return 2.0f * damping * sqrtf(Stiffness() * Inertia());
}

float StageSpringPlant::Lever(float x) const{
    if (kind == SPRING_LEAF){
        float out = (x - root.x) * side;
        return (out > 0.0f) ? out : 0.0f;
    }
    return 1.0f;
}

bool StageSpringPlant::Covers(float x) const{
    if (kind == SPRING_LEAF){
        float out = (x - root.x) * side;
        return out >= 0.0f && out <= length * cosf(q);
    }
    return fabsf(x - root.x) < length * 0.5f + ARCHER_HALF_W;
}

/*
    The top at x. A leaf is straight, so its surface is the line out of the stem at its angle -
    extended past either end, which is what lets the one-way test ask where it WAS under a foot
    that has only just come over it.
*/
float StageSpringPlant::SurfaceY(float x, float at_q) const{
    if (kind == SPRING_LEAF){
        return root.y + (x - root.x) * side * tanf(at_q);
    }
    return root.y + at_q;
}

float StageSpringPlant::SlopeDeg() const{
    return (kind == SPRING_LEAF) ? q / STAGE_DEG2RAD : 0.0f;
}

v2 StageSpringPlant::Tip() const{
    if (kind == SPRING_LEAF){
        return v2(root.x + side * length * cosf(q),root.y + length * sinf(q));
    }
    return root;
}

/*
    One step of every spring. Semi-implicit Euler, like her own motion: the rate first, then the
    position from the new rate, which stays stable here as long as a swing is more than a few
    ticks long - an empty pad at 5 Hz is 12 of them.

    HER WEIGHT AND HER MASS go onto the one she is standing on, through her lever: a pad takes her
    whole weight, a leaf her weight times how far out she stands, and her share of the inertia is
    that lever squared - which is why a leaf with her at the tip swings so much slower. Only while
    she is standing: on a rope or a ledge she is not on it, whatever spring_on last said.

    At the end of its travel it STOPS dead, losing the speed - a mushroom bottoming out, a leaf
    that cannot bend further. Without it a hard enough landing would fold the leaf through itself.
*/
void Stage::TickSpringPlants(){
    for (size_t i = 0; i < spring_plants.size(); i++){
        StageSpringPlant& p = spring_plants[i];
        p.prev_q = p.q;
        float inertia = p.Inertia();
        float force = -p.Stiffness() * (p.q - p.Rest()) - p.Damping() * p.qd;
        bool f_loaded = (spring_on == (int)i) && f_on_ground &&
                        (mode == MODE_GROUND || mode == MODE_KNEEL);
        if (f_loaded){
            float lever = p.Lever(pos.x);
            force -= ARCHER_GRAVITY * lever;
            inertia += lever * lever;
        }
        p.qd += force / inertia * ARCHER_DT;
        p.q += p.qd * ARCHER_DT;
        float lo = p.Rest() - p.Travel();
        float hi = p.Rest() + p.Travel();
        if (p.q < lo){
            p.q = lo;
            if (p.qd < 0.0f){ p.qd = 0.0f; }
        }else if (p.q > hi){
            p.q = hi;
            if (p.qd > 0.0f){ p.qd = 0.0f; }
        }
    }
}

//--- Rope bridges ----------------------------------------------------------------------------------

/*
    Every plank asked, rather than walking the points left to right: a snapped half hangs from its
    anchor with its points doubling back under it, and the first plank to span x is then not the
    one she is on. A plank on end spans nothing worth standing on, so past BRIDGE_STAND_DEG it is
    skipped - which is how a half swinging down drops her.
*/
int StageBridge::Plank(float x, const std::vector<v2>& pts, float* out_t) const{
    static const float max_rise = tanf(BRIDGE_STAND_DEG * 3.14159265358979f / 180.0f);
    const int n = (int)pts.size();
    int best = -1;
    float best_y = 0.0f, best_t = 0.0f;
    for (int k = 0; k + 1 < n; k++){
        if (k < (int)broken.size() && broken[k]){
            continue;
        }
        float x0 = pts[k].x, x1 = pts[k + 1].x;
        if (x < fminf(x0,x1) || x > fmaxf(x0,x1)){
            continue;
        }
        float dx = x1 - x0, dy = pts[k + 1].y - pts[k].y;
        if (fabsf(dx) < 1e-6f || fabsf(dy) > fabsf(dx) * max_rise){
            continue;
        }
        float t = (x - x0) / dx;
        float y = pts[k].y + dy * t;
        if (best < 0 || y > best_y){
            best = k;
            best_y = y;
            best_t = t;
        }
    }
    if (out_t){
        *out_t = (best_t < 0.0f) ? 0.0f : ((best_t > 1.0f) ? 1.0f : best_t);
    }
    return best;
}

float StageBridge::MaxStrain() const{
    float top = 0.0f;
    for (float s : strain){
        top = fmaxf(top,s);
    }
    return top;
}

float StageBridge::SurfaceY(float x) const{
    float t = 0.0f;
    int k = Plank(x,p,&t);
    return (k < 0) ? 0.0f : p[k].y + (p[k + 1].y - p[k].y) * t;
}

/*
    Last tick's plank under where she started the move - or, where there was none (she started it
    past the bridge's end, below an anchor), the plank under her now, clamped to its end. Answering
    0 there, "no surface", read as a floor at y 0 that she had been above, and a running jump across
    the gap UNDER the bridge landed on it.
*/
float StageBridge::SurfaceYThen(float x_then, float x_now) const{
    const std::vector<v2>& pts = (prev_p.size() == p.size()) ? prev_p : p;
    float t = 0.0f;
    int k = Plank(x_then,pts,&t);
    if (k >= 0){
        return pts[k].y + (pts[k + 1].y - pts[k].y) * t;
    }
    k = Plank(x_now,p,NULL);
    if (k < 0){
        return 1e30f;           //nothing to have been above
    }
    float dx = pts[k + 1].x - pts[k].x;
    t = (fabsf(dx) > 1e-6f) ? (x_then - pts[k].x) / dx : 0.0f;
    t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
    return pts[k].y + (pts[k + 1].y - pts[k].y) * t;
}

float StageBridge::SurfaceVelY(float x) const{
    float t = 0.0f;
    int k = Plank(x,p,&t);
    return (k < 0) ? 0.0f : v[k].y + (v[k + 1].y - v[k].y) * t;
}

float StageBridge::Slope(float x) const{
    int k = Plank(x,p,NULL);
    if (k < 0){
        return 0.0f;
    }
    float dx = p[k + 1].x - p[k].x;
    return (dx > 1e-6f) ? (p[k + 1].y - p[k].y) / dx : 0.0f;
}

float StageBridge::Lowest() const{
    float low = 1e30f;
    for (const v2& q : p){
        low = fminf(low,q.y);
    }
    return low;
}

float StageBridge::Mass(int i) const{
    float m = BRIDGE_POINT_MASS;
    if (load_at >= 0){
        if (i == load_at){
            m += 1.0f - load_t;
        }else if (i == load_at + 1){
            m += load_t;
        }
    }
    return m;
}

/*
    Hung as a parabola of the right length - the sag a chain of that slack has, near enough - then
    stepped ten seconds with nobody on it, so the level starts with it still rather than settling
    under her while she looks at it. The same steps on every Reset, so every run starts alike.
*/
void Stage::AddBridge(v2 a, v2 b, int planks, float slack, bool f_breakable){
    StageBridge br;
    br.a = a;
    br.b = b;
    br.planks = (planks < 2) ? 2 : planks;
    br.slack = (slack < 1.0f) ? 1.0f : slack;
    br.f_breakable = f_breakable;
    br.strain.assign(br.planks,0.0f);
    br.broken.assign(br.planks,0);
    float span = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    br.link = span * br.slack / (float)br.planks;
    //A parabola's length is about span + 8 d^2 / (3 span): the sag d that gives this slack.
    float sag = sqrtf(3.0f * span * span * (br.slack - 1.0f) / 8.0f);
    for (int i = 0; i <= br.planks; i++){
        float u = (float)i / (float)br.planks;
        v2 q(a.x + (b.x - a.x) * u,a.y + (b.y - a.y) * u - 4.0f * sag * u * (1.0f - u));
        br.p.push_back(q);
        br.v.push_back(v2(0.0f,0.0f));
    }
    for (int t = 0; t < 10 * ARCHER_TPS; t++){
        StepBridge(br,ARCHER_DT);
    }
    br.prev_p = br.p;
    bridges.push_back(br);
}

/*
    One tick of one bridge, in BRIDGE_SUBSTEPS: gravity on every point by its mass (hers on the two
    she stands between), each plank pulling its two ends together when stretched - its stiffness on
    how far, its damping on how fast - and a little air on every point. The anchors never move.
    Semi-implicit Euler, the speed first, like hers; the substeps are what keep a plank this stiff
    on a point this light from blowing up.
*/
void Stage::StepBridge(StageBridge& br, float dt){
    const int n = (int)br.p.size();
    if (n < 2){
        return;
    }
    const float h = dt / (float)BRIDGE_SUBSTEPS;
    std::vector<v2> force(n);
    for (int s = 0; s < BRIDGE_SUBSTEPS; s++){
        for (int j = 0; j < n; j++){
            force[j] = v2(0.0f,-ARCHER_GRAVITY * br.Mass(j));
        }
        for (int j = 0; j + 1 < n; j++){
            if (j < (int)br.broken.size() && br.broken[j]){
                continue;           //snapped: the two halves hang apart
            }
            float dx = br.p[j + 1].x - br.p[j].x;
            float dy = br.p[j + 1].y - br.p[j].y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len <= br.link || len < 1e-6f){
                continue;           //slack: a rope does not push
            }
            float ux = dx / len, uy = dy / len;
            float rate = (br.v[j + 1].x - br.v[j].x) * ux + (br.v[j + 1].y - br.v[j].y) * uy;
            float pull = BRIDGE_STIFFNESS * (len - br.link) + BRIDGE_PLANK_DAMPING * rate;
            if (pull <= 0.0f){
                continue;
            }
            force[j].x += ux * pull;
            force[j].y += uy * pull;
            force[j + 1].x -= ux * pull;
            force[j + 1].y -= uy * pull;
        }
        const float air = 1.0f - BRIDGE_AIR_DAMPING * h;
        for (int j = 1; j + 1 < n; j++){
            float m = br.Mass(j);
            br.v[j].x = (br.v[j].x + force[j].x / m * h) * air;
            br.v[j].y = (br.v[j].y + force[j].y / m * h) * air;
            br.p[j].x += br.v[j].x * h;
            br.p[j].y += br.v[j].y * h;
        }
    }
    br.p[0] = br.a;
    br.p[n - 1] = br.b;
    br.v[0] = v2(0.0f,0.0f);
    br.v[n - 1] = v2(0.0f,0.0f);
}

/*
    Her weight goes on the bridge she stood on at the end of last tick - on its two points either
    side of her, by where she is between them - but only while she stands: hanging or in the air
    she is not on it, whatever bridge_on last said. Then the step. Last tick's points are kept, for
    the landing test's "where it was".

    HER MOMENTUM MOVES WITH HER. Walking, her mass passes from one pair of points to the next, and
    handing it over as mass alone - the new pair suddenly heavy at whatever it was doing, the old
    one light again at her speed - pumped energy in at every plank: a run across set it swinging
    at 14 units a second. So the pair she moves onto takes her vertical speed as well, the way a
    landing hands it her fall (momentum kept, as two things that stick together), and the pair she
    leaves keeps its own. Standing still it is the same pair at the same speed, and changes nothing.
*/
void Stage::TickBridges(){
    for (size_t i = 0; i < bridges.size(); i++){
        StageBridge& br = bridges[i];
        br.prev_p = br.p;
        bool f_loaded = (bridge_on == (int)i) && f_on_ground &&
                        (mode == MODE_GROUND || mode == MODE_KNEEL);
        br.load_at = -1;
        if (f_loaded){
            br.load_at = br.Plank(pos.x,br.p,&br.load_t);
            //She rode it last tick, so her speed is the surface's where she was.
            const int last = (int)br.p.size() - 1;
            for (int j = br.load_at; br.load_at >= 0 && j <= br.load_at + 1; j++){
                if (j <= 0 || j >= last){
                    continue;
                }
                float w = (j == br.load_at) ? (1.0f - br.load_t) : br.load_t;
                br.v[j].y = (BRIDGE_POINT_MASS * br.v[j].y + w * vel.y) / (BRIDGE_POINT_MASS + w);
            }
        }
        StepBridge(br,ARCHER_DT);
    }
}

//--- Webs (StageWeb, docs/web_plan.md) -------------------------------------------------------------

int StageWeb::CutCount() const{
    int n = 0;
    for (const StageWebThread& t : threads){
        n += t.f_cut ? 1 : 0;
    }
    return n;
}

float StageWeb::Mass(int i) const{
    float m = WEB_NODE_MASS;
    for (const StageWebCatch& c : caught){
        if (c.node == i){
            m += WEB_ARROW_MASS;
        }
    }
    return m;
}

/*
    The shape - see StageWeb. Node 0 is the hub; then the spiral's nodes, turn by turn, spoke by
    spoke; then the anchors, one per spoke. The spokes are laid half a step off the axes, so none is
    flat: an arrow loosed level crosses every spoke it passes rather than running along one.

    The spiral climbs a whole spacing per turn, so each of its nodes sits a little further out than
    the last: at (turn + 1 + spoke / spokes) / (rings + 1) of the way along its spoke, which keeps
    the outer turn clear of the frame and the inner one clear of the hub.
*/
void Stage::AddWeb(float x, float y, float w, float h, int spokes, int rings, float hub_x, float hub_y){
    StageWeb web;
    web.x = x;
    web.y = y;
    web.w = w;
    web.h = h;
    web.spokes = (spokes < 3) ? 3 : ((spokes > WEB_MAX_SPOKES) ? WEB_MAX_SPOKES : spokes);
    web.rings = (rings < 1) ? 1 : ((rings > WEB_MAX_RINGS) ? WEB_MAX_RINGS : rings);
    web.hub_x = hub_x;
    web.hub_y = hub_y;
    const int S = web.spokes;
    const int R = web.rings;
    const v2 hub(x + w * 0.5f + hub_x,y + h * 0.5f + hub_y);

    //Where each spoke meets the opening's edge: the nearest of the four sides along its ray.
    std::vector<v2> ends(S);
    for (int i = 0; i < S; i++){
        float a = 2.0f * 3.14159265f * ((float)i + 0.5f) / (float)S;
        float dx = cosf(a), dy = sinf(a);
        float t = 1e9f;
        if (dx >  1e-6f) t = fminf(t,(x + w - hub.x) / dx);
        if (dx < -1e-6f) t = fminf(t,(x - hub.x) / dx);
        if (dy >  1e-6f) t = fminf(t,(y + h - hub.y) / dy);
        if (dy < -1e-6f) t = fminf(t,(y - hub.y) / dy);
        ends[i] = v2(hub.x + dx * t,hub.y + dy * t);
    }
    auto spiral = [S](int turn, int spoke){ return 1 + turn * S + spoke; };
    const int first_anchor = 1 + R * S;

    web.p.push_back(hub);
    web.anchor.push_back(0);
    for (int k = 0; k < R; k++){
        for (int i = 0; i < S; i++){
            float f = ((float)k + 1.0f + (float)i / (float)S) / (float)(R + 1);
            web.p.push_back(v2(hub.x + (ends[i].x - hub.x) * f,hub.y + (ends[i].y - hub.y) * f));
            web.anchor.push_back(0);
        }
    }
    for (int i = 0; i < S; i++){
        web.p.push_back(ends[i]);
        web.anchor.push_back(1);
    }
    web.v.assign(web.p.size(),v2(0.0f,0.0f));

    auto thread = [&web](int a, int b, bool f_spoke){
        StageWebThread t;
        t.a = a;
        t.b = b;
        float dx = web.p[b].x - web.p[a].x;
        float dy = web.p[b].y - web.p[a].y;
        t.rest = sqrtf(dx * dx + dy * dy) * WEB_PRETENSION;
        t.f_spoke = f_spoke;
        web.threads.push_back(t);
    };
    //The spokes, hub to frame, through every turn of the spiral.
    for (int i = 0; i < S; i++){
        int from = 0;
        for (int k = 0; k < R; k++){
            thread(from,spiral(k,i),true);
            from = spiral(k,i);
        }
        thread(from,first_anchor + i,true);
    }
    //The spiral, one thread from the inside out: round each turn and on to the next.
    for (int k = 0; k < R; k++){
        for (int i = 0; i < S; i++){
            int next = (i + 1 < S) ? spiral(k,i + 1) : ((k + 1 < R) ? spiral(k + 1,0) : -1);
            if (next >= 0){
                thread(spiral(k,i),next,false);
            }
        }
    }

    for (int t = 0; t < WEB_SETTLE_TICKS; t++){
        StepWeb(web,ARCHER_DT);
    }

    /*
        The wall: the opening, stacked from the bottom in slices of about WEB_WALL_SLICE - SOLID to
        her, passed by arrows (SegmentHitsBlock skips a block with `web` set), drawn by nobody but
        the blockout view. Appended here, which is why AddWeb wants calling after the blocks that
        must keep their indices.
    */
    web.slices = (int)floorf(h / WEB_WALL_SLICE + 0.5f);
    if (web.slices < 1){
        web.slices = 1;
    }
    const float slice_h = h / (float)web.slices;
    web.first_slice = (int)blocks.size();
    for (int s = 0; s < web.slices; s++){
        StageBlock b = { x + w * 0.5f, y + ((float)s + 0.5f) * slice_h, w * 0.5f, slice_h * 0.5f, BLOCK_SOLID, true };
        b.f_invisible = true;
        b.web = (int)webs.size();
        blocks.push_back(b);
    }
    webs.push_back(web);
}

/*
    One tick of one web, in WEB_SUBSTEPS - the bridge's step over a net instead of a chain: gravity
    on every node by its mass (a caught arrow's on its node), each whole thread pulling its ends
    together when stretched - its stiffness WEB_STIFFNESS over its length, so a short thread is as
    stiff as a long one is per unit stretched, like silk of one gauge - and air on every node.
*/
void Stage::StepWeb(StageWeb& web, float dt){
    const int n = (int)web.p.size();
    if (n < 2){
        return;
    }
    const float h = dt / (float)WEB_SUBSTEPS;
    std::vector<v2> force(n);
    std::vector<float> mass(n);
    for (int j = 0; j < n; j++){
        mass[j] = web.Mass(j);
    }
    for (int s = 0; s < WEB_SUBSTEPS; s++){
        for (int j = 0; j < n; j++){
            force[j] = v2(0.0f,-ARCHER_GRAVITY * mass[j]);
        }
        for (const StageWebThread& t : web.threads){
            if (t.f_cut){
                continue;
            }
            float dx = web.p[t.b].x - web.p[t.a].x;
            float dy = web.p[t.b].y - web.p[t.a].y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len <= t.rest || len < 1e-6f){
                continue;           //slack: silk does not push
            }
            float ux = dx / len, uy = dy / len;
            float rate = (web.v[t.b].x - web.v[t.a].x) * ux + (web.v[t.b].y - web.v[t.a].y) * uy;
            float pull = (WEB_STIFFNESS / t.rest) * (len - t.rest) + WEB_THREAD_DAMPING * rate;
            if (pull <= 0.0f){
                continue;
            }
            force[t.a].x += ux * pull;
            force[t.a].y += uy * pull;
            force[t.b].x -= ux * pull;
            force[t.b].y -= uy * pull;
        }
        const float air = 1.0f - WEB_AIR_DAMPING * h;
        for (int j = 0; j < n; j++){
            if (web.anchor[j]){
                continue;
            }
            web.v[j].x = (web.v[j].x + force[j].x / mass[j] * h) * air;
            web.v[j].y = (web.v[j].y + force[j].y / mass[j] * h) * air;
            web.p[j].x += web.v[j].x * h;
            web.p[j].y += web.v[j].y * h;
            //On the ground under the frame - see WEB_FLOOR_FRICTION.
            if (web.p[j].y < web.y){
                web.p[j].y = web.y;
                if (web.v[j].y < 0.0f){
                    web.v[j].y = 0.0f;
                }
                web.v[j].x *= fmaxf(0.0f,1.0f - WEB_FLOOR_FRICTION * h);
            }
        }
    }
}

void Stage::TickWebs(){
    for (StageWeb& web : webs){
        StepWeb(web,ARCHER_DT);
        /*
            Its caught arrows ride their nodes. One that has gone - aged out, or its slot taken by a
            new arrow, which moves it from where this left it - lets go of the web: a stuck arrow
            never moves by itself, so "not where I put it" is exactly "not mine any more".
        */
        for (size_t c = 0; c < web.caught.size();){
            StageWebCatch& k = web.caught[c];
            bool f_mine = k.arrow >= 0 && k.arrow < ARROW_MAX_LIVE && arrows[k.arrow].f_live &&
                          arrows[k.arrow].f_stuck && arrows[k.arrow].pos.x == k.placed.x &&
                          arrows[k.arrow].pos.y == k.placed.y;
            if (!f_mine || k.node < 0 || k.node >= (int)web.p.size()){
                web.caught.erase(web.caught.begin() + c);
                continue;
            }
            Arrow& a = arrows[k.arrow];
            a.prev_pos = a.pos;
            a.pos = v3(web.p[k.node].x + k.offset.x,web.p[k.node].y + k.offset.y,a.pos.z);
            k.placed = v2(a.pos.x,a.pos.y);
            c++;
        }
    }
}

/*
    Where segment p0 -> p1 crosses q0 -> q1, as the fraction along the first, or -1 for no crossing.
    In the plane: a web's threads are at z 0 and so, on a locked plane, is every arrow.
*/
static float CrossSegments(const v2& p0, const v2& p1, const v2& q0, const v2& q1){
    const float rx = p1.x - p0.x, ry = p1.y - p0.y;
    const float sx = q1.x - q0.x, sy = q1.y - q0.y;
    const float den = rx * sy - ry * sx;
    if (den > -1e-9f && den < 1e-9f){
        return -1.0f;               //parallel: grazing along a thread is not crossing it
    }
    const float qpx = q0.x - p0.x, qpy = q0.y - p0.y;
    const float t = (qpx * sy - qpy * sx) / den;
    const float u = (qpx * ry - qpy * rx) / den;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f){
        return -1.0f;
    }
    return t;
}

/*
    See ARROWS in StageWeb. Every whole thread the segment crosses, nearest first: each snaps, kicks
    its two ends along the flight, and takes its share of the arrow's speed - and the one that leaves
    it too slow to go on holds it. Caught, the arrow is stuck where it met that thread, riding the
    nearer of its two nodes that moves (an anchor holds it still).
*/
bool Stage::ArrowThroughWebs(int index, const v3& from, const v3& to, StageEvents& events){
    if (index < 0 || index >= ARROW_MAX_LIVE){
        return false;
    }
    Arrow& a = arrows[index];
    const v2 f2(from.x,from.y), t2(to.x,to.y);
    for (size_t wi = 0; wi < webs.size(); wi++){
        StageWeb& web = webs[wi];
        //Nowhere near the opening, padded for the net's swing: not this web.
        const float pad = 0.5f;
        if (fmaxf(f2.x,t2.x) < web.x - pad || fminf(f2.x,t2.x) > web.Right() + pad ||
            fmaxf(f2.y,t2.y) < web.y - pad || fminf(f2.y,t2.y) > web.Top() + pad){
            continue;
        }
        struct Cross{ float t; int thread; };
        std::vector<Cross> crosses;
        for (size_t ti = 0; ti < web.threads.size(); ti++){
            const StageWebThread& th = web.threads[ti];
            if (th.f_cut){
                continue;
            }
            float t = CrossSegments(f2,t2,web.p[th.a],web.p[th.b]);
            if (t < 0.0f){
                continue;
            }
            //In order along the flight; equal t by thread index, so the order never depends on luck.
            Cross c = { t, (int)ti };
            size_t at = crosses.size();
            crosses.push_back(c);
            while (at > 0 && (crosses[at - 1].t > c.t || (crosses[at - 1].t == c.t && crosses[at - 1].thread > c.thread))){
                crosses[at] = crosses[at - 1];
                at--;
            }
            crosses[at] = c;
        }
        if (crosses.empty()){
            continue;
        }
        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y + a.vel.z * a.vel.z);
        const float flat = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y);
        const v2 dir = (flat > 1e-6f) ? v2(a.vel.x / flat,a.vel.y / flat) : v2(0.0f,0.0f);
        StageEvents::WebHit hit;
        hit.web = (int)wi;
        hit.arrow = index;
        hit.at = v2(f2.x + (t2.x - f2.x) * crosses[0].t,f2.y + (t2.y - f2.y) * crosses[0].t);
        for (const Cross& c : crosses){
            StageWebThread& th = web.threads[c.thread];
            th.f_cut = true;
            const v2 at(f2.x + (t2.x - f2.x) * c.t,f2.y + (t2.y - f2.y) * c.t);
            StageEvents::WebThreadCut cut;
            cut.web = (int)wi;
            cut.thread = c.thread;
            cut.arrow = index;
            cut.at = at;
            events.web_cuts.push_back(cut);
            hit.threads++;
            for (int end : { th.a, th.b }){
                if (!web.anchor[end]){
                    web.v[end].x += dir.x * WEB_SNAP_KICK;
                    web.v[end].y += dir.y * WEB_SNAP_KICK;
                }
            }
            speed *= 1.0f - WEB_SLOW_PER_THREAD;
            a.vel = a.vel * (1.0f - WEB_SLOW_PER_THREAD);
            if (speed < WEB_CATCH_SPEED){
                a.pos = v3(at.x,at.y,from.z + (to.z - from.z) * c.t);
                a.vel = v3(0.0f,0.0f,0.0f);
                a.f_stuck = true;
                a.age_ticks = 0;
                //The nearer end that moves, so it rides the net; on a thread between two anchors, either.
                auto d2 = [&](int n){
                    float dx = web.p[n].x - at.x, dy = web.p[n].y - at.y;
                    return dx * dx + dy * dy;
                };
                int node = (d2(th.a) <= d2(th.b)) ? th.a : th.b;
                int other = (node == th.a) ? th.b : th.a;
                if (web.anchor[node] && !web.anchor[other]){
                    node = other;
                }
                StageWebCatch k;
                k.arrow = index;
                k.node = node;
                k.offset = v2(at.x - web.p[node].x,at.y - web.p[node].y);
                k.placed = at;
                web.caught.push_back(k);
                hit.f_caught = true;
                events.web_hits.push_back(hit);
                return true;
            }
        }
        events.web_hits.push_back(hit);
    }
    return false;
}

/*
    What still holds across the frame. A cut thread does not, nor does one in a strand left HANGING:
    a piece of the net joined to the frame through a single thread, which a body pushes aside, or
    one joined to it not at all, which falls. In graph terms, with every anchor counted as one node
    (the frame): a thread holds when it is in the frame's 2-edge-connected part - it lies on a loop
    through the frame, so cutting any one thread does not let it hang. Found by Tarjan's bridges
    from the frame, then a walk out from it that crosses no bridge.
*/
struct WebGraph{
    const StageWeb* web = NULL;
    std::vector<std::vector<int>> adj;  //per vertex: thread indices
    std::vector<int> tin, low;
    std::vector<uint8_t> is_bridge;
    int timer = 0;
    int frame = 0;                      //the vertex every anchor is folded into
    int Vertex(int node) const { return web->anchor[node] ? frame : node; }
    int Other(int thread, int v) const {
        const StageWebThread& t = web->threads[thread];
        int a = Vertex(t.a), b = Vertex(t.b);
        return (a == v) ? b : a;
    }
    void Walk(int v, int parent_thread){
        tin[v] = low[v] = ++timer;
        for (int t : adj[v]){
            if (t == parent_thread){
                continue;
            }
            int u = Other(t,v);
            if (tin[u]){
                low[v] = (tin[u] < low[v]) ? tin[u] : low[v];
            }else{
                Walk(u,t);
                low[v] = (low[u] < low[v]) ? low[u] : low[v];
                if (low[u] > tin[v]){
                    is_bridge[t] = 1;
                }
            }
        }
    }
};

void Stage::WebHeldThreads(const StageWeb& web, std::vector<uint8_t>& held) const{
    const int n = (int)web.p.size();
    held.assign(web.threads.size(),0);
    WebGraph g;
    g.web = &web;
    g.frame = n;
    g.adj.assign(n + 1,std::vector<int>());
    for (size_t ti = 0; ti < web.threads.size(); ti++){
        const StageWebThread& t = web.threads[ti];
        if (t.f_cut){
            continue;
        }
        int a = g.Vertex(t.a), b = g.Vertex(t.b);
        if (a == b){
            continue;
        }
        g.adj[a].push_back((int)ti);
        g.adj[b].push_back((int)ti);
    }
    g.tin.assign(n + 1,0);
    g.low.assign(n + 1,0);
    g.is_bridge.assign(web.threads.size(),0);
    g.Walk(g.frame,-1);
    std::vector<uint8_t> reached(n + 1,0);
    std::vector<int> stack(1,g.frame);
    reached[g.frame] = 1;
    while (!stack.empty()){
        int v = stack.back();
        stack.pop_back();
        for (int t : g.adj[v]){
            if (g.is_bridge[t]){
                continue;
            }
            held[t] = 1;
            int u = g.Other(t,v);
            if (!reached[u]){
                reached[u] = 1;
                stack.push_back(u);
            }
        }
    }
}

//Liang-Barsky against the band across the whole opening: does any held thread cross it?
bool Stage::WebBandClear(const StageWeb& web, const std::vector<uint8_t>& held, float y0, float y1) const{
    for (size_t ti = 0; ti < web.threads.size(); ti++){
        if (ti >= held.size() || !held[ti]){
            continue;
        }
        const StageWebThread& t = web.threads[ti];
        const v2 a = web.p[t.a], b = web.p[t.b];
        const float dx = b.x - a.x, dy = b.y - a.y;
        float t0 = 0.0f, t1 = 1.0f;
        const float pp[4] = { -dx, dx, -dy, dy };
        const float qq[4] = { a.x - web.x, web.Right() - a.x, a.y - y0, y1 - a.y };
        bool f_out = false;
        for (int i = 0; i < 4 && !f_out; i++){
            if (pp[i] > -1e-9f && pp[i] < 1e-9f){
                f_out = qq[i] < 0.0f;
                continue;
            }
            float r = qq[i] / pp[i];
            if (pp[i] < 0.0f){
                t0 = (r > t0) ? r : t0;
            }else{
                t1 = (r < t1) ? r : t1;
            }
            f_out = t0 > t1;
        }
        if (!f_out){
            return false;
        }
    }
    return true;
}

/*
    See THE WALL in StageWeb. Every band of her height plus WEB_PASS_MARGIN, on the slices, from the
    floor up to a jump's reach: one with no held thread across it opens, slices and all. Checked every
    tick rather than only on a cut, because a cut strand still swinging clear can open a band a few
    ticks after the arrow that freed it.
*/
void Stage::TickWebWalls(StageEvents& events){
    const float reach = (ARCHER_JUMP_SPEED * ARCHER_JUMP_SPEED) / (2.0f * ARCHER_GRAVITY);
    std::vector<uint8_t> held;
    for (size_t wi = 0; wi < webs.size(); wi++){
        StageWeb& web = webs[wi];
        if (web.first_slice < 0 || web.slices < 1 || web.first_slice + web.slices > (int)blocks.size()){
            continue;
        }
        const float slice_h = web.h / (float)web.slices;
        const int need = (int)ceilf((2.0f * ARCHER_HALF_H + WEB_PASS_MARGIN) / slice_h - 1e-4f);
        if (need > web.slices){
            continue;               //an opening she does not fit through is never a way through
        }
        bool f_held_found = false;
        for (int s = 0; s + need <= web.slices; s++){
            const float y0 = web.y + (float)s * slice_h;
            if (y0 - web.y > reach){
                break;              //she cannot get her feet up there: a hole too high
            }
            bool f_open = true;
            for (int k = s; k < s + need && f_open; k++){
                f_open = !blocks[web.first_slice + k].f_alive;
            }
            if (f_open){
                continue;
            }
            if (!f_held_found){
                WebHeldThreads(web,held);
                f_held_found = true;
            }
            const float y1 = y0 + (float)need * slice_h;
            //Above what she steps over (WEB_STEP_OVER): the band her body actually has to get through.
            if (!WebBandClear(web,held,y0 + WEB_STEP_OVER,y1)){
                continue;
            }
            for (int k = s; k < s + need; k++){
                StageBlock& b = blocks[web.first_slice + k];
                if (b.f_alive){
                    b.f_alive = false;
                    events.web_slices_opened.push_back(web.first_slice + k);
                }
            }
            StageEvents::WebBreach br;
            br.web = (int)wi;
            br.y0 = y0;
            br.y1 = y1;
            br.f_first = !web.f_breached;
            web.f_breached = true;
            events.web_breaches.push_back(br);
        }
    }
}

/*
    See STRAIN in Stage.h. `speed` is the landing's, against the plank under her and with the
    stomp in. Every landing is reported, breakable or not - the knock and the creak are the same on
    either - and a breakable bridge's strain, warnings and snap follow from it.

    THE SNAP is the most strained plank reaching 1: it stops being a spring, so its two ends part,
    and stops being a floor, so she falls through the gap if that is where she stands. Everything
    else stays simulated - the halves swing down from their anchors under their own weight.
*/
void Stage::StrainBridge(int bi, int plank, float speed, StageEvents& events){
    StageBridge& br = bridges[bi];
    if (br.f_breakable && br.level < BRIDGE_LEVEL_SNAPPED && plank >= 0){
        float add = (speed - BRIDGE_COMFORT_SPEED) * BRIDGE_STRAIN_PER_SPEED;
        if (add > 0.0f){
            for (int k = 0; k < (int)br.strain.size(); k++){
                if (br.broken[k]){
                    continue;
                }
                int d = abs(k - plank);
                float share = (d == 0) ? 1.0f : ((d == 1) ? BRIDGE_STRAIN_NEIGHBOUR : BRIDGE_STRAIN_SPREAD);
                br.strain[k] = fminf(br.strain[k] + add * share,1.0f);
            }
        }
        //Which plank is worst, and whether that has passed a level not yet reported. Levels go up
        //one landing at a time or several at once - a hard stomp on a strained bridge can go
        //straight past cracking to snapped, and each is still reported, in order.
        int worst = 0;
        for (int k = 1; k < (int)br.strain.size(); k++){
            if (br.strain[k] > br.strain[worst]){
                worst = k;
            }
        }
        float s = br.strain.empty() ? 0.0f : br.strain[worst];
        int level = (s >= 1.0f) ? BRIDGE_LEVEL_SNAPPED :
                    (s >= BRIDGE_CRACKING) ? BRIDGE_LEVEL_CRACKING :
                    (s >= BRIDGE_STRAINED) ? BRIDGE_LEVEL_STRAINED : BRIDGE_SOUND;
        v2 at((br.p[worst].x + br.p[worst + 1].x) * 0.5f,(br.p[worst].y + br.p[worst + 1].y) * 0.5f);
        while (br.level < level){
            br.level++;
            StageEvents::BridgeWarning w;
            w.bridge = bi;
            w.plank = worst;
            w.level = br.level;
            w.at = at;
            events.bridge_warnings.push_back(w);
        }
        if (level == BRIDGE_LEVEL_SNAPPED){
            br.broken[worst] = 1;
        }
    }
    StageEvents::BridgeLanding l;
    l.bridge = bi;
    l.plank = plank;
    l.speed = speed;
    l.strain = br.MaxStrain();
    l.x = pos.x;
    events.bridge_landings.push_back(l);
}

/*
    Every surface under x that is not a block - see StageSurface. A plant's surface is where it was
    last tick under x_from, since it moves, and a bridge's likewise; the others' are simply where
    they are. Down drops her through a plant, a branch and a bridge, as through a one-way platform.
    Never through a ramp: that is ground.
*/
void Stage::GatherSurfaces(float x, float x_from, bool f_down_held, std::vector<StageSurface>& out) const{
    out.clear();
    if (!f_down_held){
        for (size_t i = 0; i < spring_plants.size(); i++){
            const StageSpringPlant& p = spring_plants[i];
            if (!p.Covers(x)){
                continue;
            }
            StageSurface c;
            c.kind = SURFACE_PLANT;
            c.index = (int)i;
            c.top = p.SurfaceY(x);
            c.top_then = p.SurfaceY(x_from,p.prev_q);
            c.vel_y = p.SurfaceVelY(x);
            c.slope = tanf(p.kind == SPRING_LEAF ? p.q : 0.0f);     //its steepness; the sign is not used
            out.push_back(c);
        }
        for (size_t i = 0; i < branches.size(); i++){
            const StageBranch& br = branches[i];
            if (!br.Covers(x)){
                continue;
            }
            StageSurface c;
            c.kind = SURFACE_BRANCH;
            c.index = (int)i;
            c.top = br.SurfaceY(x);
            c.top_then = br.SurfaceY(x_from);
            c.slope = br.Slope();
            out.push_back(c);
        }
        //Ground to walk onto off an anchor's block, but still dropped through with Down.
        for (size_t i = 0; i < bridges.size(); i++){
            const StageBridge& br = bridges[i];
            if (!br.Covers(x)){
                continue;
            }
            StageSurface c;
            c.kind = SURFACE_BRIDGE;
            c.index = (int)i;
            c.top = br.SurfaceY(x);
            c.top_then = br.SurfaceYThen(x_from,x);
            c.vel_y = br.SurfaceVelY(x);
            c.slope = br.Slope(x);
            c.f_ground = true;
            out.push_back(c);
        }
    }
    for (size_t i = 0; i < ramps.size(); i++){
        const StageRamp& r = ramps[i];
        if (!r.Covers(x)){
            continue;
        }
        StageSurface c;
        c.kind = SURFACE_RAMP;
        c.index = (int)i;
        c.top = r.SurfaceY(x);
        c.top_then = r.SurfaceY(x_from);
        c.slope = r.Slope();
        c.f_ground = true;
        out.push_back(c);
    }
}

/*
    The surfaces as floors. See the declaration; the three cases, in the order they are asked:

      BELOW THE SURFACE NOW, having been ABOVE where it was: she lands, or it has come up under
        her - either way she is put on top. It is the one-way platform's rule, with the surface's
        own movement taken out, so a rising cap cannot pass up through feet that were on it.
      ABOVE IT, ON IT, and not moving away from it: kept on it. A slope falls away under a walking
        foot faster than gravity pulls her down, so without this she would walk down a bent leaf -
        or a ramp - into the air a tick at a time. "On it" is RIDING it (on it last tick), or for
        GROUND (a ramp) having been on any floor: walking off a block onto a ramp that falls away
        from its edge is still walking. That case also reaches a body-width further, because the
        block holds her box up until her CENTRE, which is where a surface is sampled, is half a
        body past its edge - over a ramp that has fallen away by that much by then.
      Otherwise she is off it. Rising faster than it rises is how she leaves one - a jump, or the
        rebound outrunning her fall.

    THE HIGHEST SURFACE SHE IS ON WINS. Before the three were one list, the plants came first and a
    branch was only asked about when no plant held her; nothing in any level puts one over the
    other, so the two rules agree there.

    A FRESH landing on a plant hands it her fall: momentum about the stem, shared between her and
    it, as two things that stick together. Her speed then becomes its speed at her feet. A STOMP -
    the aim held down through the end of the fall - drives her into it harder than she fell.
*/
void Stage::CollideSurfaces(const v2& from, bool f_down_held, bool f_on_block, bool f_was_grounded,
                            StageEvents& events, bool& out_hit_floor){
    int was_on[SURFACE_KINDS] = { spring_on, branch_on, ramp_on, bridge_on };
    //Dropping through the branch she stood on: long enough not to catch it again on the way past,
    //her hands crossing it a quarter of a second later.
    if (f_down_held && was_on[SURFACE_BRANCH] >= 0){
        grab_cooldown = LEDGE_RELEASE_COOLDOWN * 2;
    }
    //Her fall as the blocks left it, before a surface takes it over - what a plant landing shares.
    const float fall_vel_y = vel.y;
    spring_on = -1;
    branch_on = -1;
    ramp_on = -1;
    bridge_on = -1;
    std::vector<StageSurface> candidates;
    GatherSurfaces(pos.x,from.x,f_down_held,candidates);
    if (candidates.empty()){
        return;
    }
    float feet = pos.y - ARCHER_HALF_H;
    float feet_from = from.y - ARCHER_HALF_H;
    int best = -1;
    for (size_t i = 0; i < candidates.size(); i++){
        const StageSurface& c = candidates[i];
        bool f_riding = (was_on[c.kind] == c.index);
        bool f_on = false;
        if (feet < c.top){
            f_on = f_riding || feet_from >= c.top_then - STAGE_EPS;
        }else if ((f_riding || (c.f_ground && f_was_grounded)) && !f_on_block && vel.y <= c.vel_y + STAGE_EPS){
            //How far the slope can fall away under this tick's step, and a little more.
            float reach = fabsf(pos.x - from.x) + ((!f_riding && c.f_ground) ? ARCHER_HALF_W : 0.0f);
            f_on = (feet - c.top) <= fabsf(c.slope) * reach + 0.05f;
        }
        if (f_on && (best < 0 || c.top > candidates[best].top)){
            best = (int)i;
        }
    }
    if (best < 0){
        return;
    }
    const StageSurface& c = candidates[best];
    pos.y = c.top + ARCHER_HALF_H + STAGE_EPS;
    vel.y = c.vel_y;
    out_hit_floor = true;
    if (c.kind == SURFACE_BRANCH){
        branch_on = c.index;
        return;
    }
    if (c.kind == SURFACE_RAMP){
        ramp_on = c.index;
        return;
    }
    if (c.kind == SURFACE_BRIDGE){
        /*
            A fresh landing: the two points under her take her fall, each by its share of her,
            momentum kept - her mass is on them from here on, so they move together at what the
            three of them had. A drop onto it drives it down; walking on off an anchor's block
            brings nothing (she was not falling). A STOMP drives her into it harder than she fell,
            as on the pad - and so strains it harder too.
        */
        StageBridge& br = bridges[c.index];
        if (c.index != was_on[SURFACE_BRIDGE] && fall_vel_y < 0.0f){
            float t = 0.0f;
            int k = br.Plank(pos.x,br.p,&t);
            if (k >= 0){
                float stomp = (float)(stomp_ticks < SPRING_STOMP_TICKS ? stomp_ticks : SPRING_STOMP_TICKS) /
                              (float)SPRING_STOMP_TICKS;
                float into = fall_vel_y * (1.0f + SPRING_STOMP_GAIN * stomp);
                //How hard, against the plank's own speed: onto one already dropping away is softer.
                float speed = c.vel_y - into;
                const int last = (int)br.p.size() - 1;
                for (int j = k; j <= k + 1; j++){
                    if (j <= 0 || j >= last){
                        continue;           //an anchor does not move
                    }
                    float w = (j == k) ? (1.0f - t) : t;
                    float m = BRIDGE_POINT_MASS;
                    br.v[j].y = (m * br.v[j].y + w * into) / (m + w);
                }
                vel.y = br.SurfaceVelY(pos.x);
                //A step down off an anchor's block is not a landing worth the name.
                if (speed > 2.0f){
                    events.stomp = stomp;
                    StrainBridge(c.index,k,speed,events);
                }
            }
        }
        bridge_on = c.index;
        return;
    }
    StageSpringPlant& p = spring_plants[c.index];
    if (c.index != was_on[SURFACE_PLANT]){
        float lever = p.Lever(pos.x);
        float inertia = p.Inertia();
        float stomp = (float)(stomp_ticks < SPRING_STOMP_TICKS ? stomp_ticks : SPRING_STOMP_TICKS) /
                      (float)SPRING_STOMP_TICKS;
        float into = (fall_vel_y < 0.0f) ? fall_vel_y * (1.0f + SPRING_STOMP_GAIN * stomp) : fall_vel_y;
        p.qd = (inertia * p.qd + lever * into) / (inertia + lever * lever);
        events.stomp = stomp;
        spring_boost_seen = 0.0f;       //a new bounce, and a new best for the cue to measure against
        vel.y = p.SurfaceVelY(pos.x);
    }
    spring_on = c.index;
}

/*
    Down whatever she stands on, when it is too steep to stand on: gravity along the slope, less a
    friction that exactly holds her at the surface's slip angle, turned into the sideways pull the
    run code works in. A leaf slips past SPRING_LEAF_SLIP_DEG; a ramp past its own slip_deg, which
    is the leaf's unless the level says otherwise - the ramps are where that rule is tuned.
*/
float Stage::SlideAccel() const{
    if (!f_on_ground){
        return 0.0f;
    }
    float steep = 0.0f;         //degrees, however it leans
    float slip = 90.0f;
    float downhill = 0.0f;      //which way along x is down it
    if (spring_on >= 0 && spring_on < (int)spring_plants.size()){
        const StageSpringPlant& p = spring_plants[spring_on];
        if (p.kind != SPRING_LEAF){
            return 0.0f;
        }
        float slope = p.SlopeDeg();
        steep = fabsf(slope);
        slip = SPRING_LEAF_SLIP_DEG;
        //Falling away from the stem slides her out toward the tip; rising, back toward the stem.
        downhill = (slope < 0.0f) ? p.side : -p.side;
    }else if (ramp_on >= 0 && ramp_on < (int)ramps.size()){
        const StageRamp& r = ramps[ramp_on];
        float m = r.Slope();
        steep = atanf(fabsf(m)) / STAGE_DEG2RAD;
        slip = r.slip_deg;
        downhill = (m > 0.0f) ? -1.0f : 1.0f;
    }else if (bridge_on >= 0 && bridge_on < (int)bridges.size()){
        float m = bridges[bridge_on].Slope(pos.x);
        steep = atanf(fabsf(m)) / STAGE_DEG2RAD;
        slip = BRIDGE_SLIP_DEG;
        downhill = (m > 0.0f) ? -1.0f : 1.0f;
    }else{
        return 0.0f;
    }
    //A hair of tolerance: a ramp built AT the slip angle measures a millionth over it through the
    //atan, which switched on a slide with no pull behind it.
    if (steep <= slip + 0.001f){
        return 0.0f;
    }
    float a = steep * STAGE_DEG2RAD;
    float pull = ARCHER_GRAVITY * (sinf(a) - tanf(slip * STAGE_DEG2RAD) * cosf(a)) * cosf(a);
    return downhill * pull;
}

float Stage::SlopeUnderFeetDeg() const{
    if (!f_on_ground){
        return 0.0f;
    }
    if (spring_on >= 0 && spring_on < (int)spring_plants.size()){
        const StageSpringPlant& p = spring_plants[spring_on];
        return (p.kind == SPRING_LEAF) ? atanf(p.side * tanf(p.q)) / STAGE_DEG2RAD : 0.0f;
    }
    if (branch_on >= 0 && branch_on < (int)branches.size()){
        return atanf(branches[branch_on].Slope()) / STAGE_DEG2RAD;
    }
    if (ramp_on >= 0 && ramp_on < (int)ramps.size()){
        return atanf(ramps[ramp_on].Slope()) / STAGE_DEG2RAD;
    }
    if (bridge_on >= 0 && bridge_on < (int)bridges.size()){
        return atanf(bridges[bridge_on].Slope(pos.x)) / STAGE_DEG2RAD;
    }
    return 0.0f;
}

/*
    A jump pressed now keeps whatever she is rising at (see the fling in TickArcher), so on a
    spring plant that IS the boost: her rise while she rides it, and in the coyote ticks after it
    throws her. Anywhere else a jump adds nothing to anything, and the cue is off.
*/
bool Stage::SpringBoostActive() const{
    if (mode == MODE_GROUND && f_on_ground && spring_on >= 0){
        return true;
    }
    return mode == MODE_AIR && !f_on_ground && spring_air_ticks >= 0 && coyote_ticks > 0;
}

float Stage::SpringBoostNow() const{
    return (SpringBoostActive() && vel.y > 0.0f) ? vel.y : 0.0f;
}

/*
    The best boost of this bounce: what it has been (spring_boost_seen) or will be, found by
    ticking a copy on with the input held and its edges cleared, as PredictLanding does, until the
    bounce is over. A jump in the copy would end it, which is why the press is cleared.
*/
float Stage::PredictSpringBoostPeak(const ArcherInput& in, int horizon) const{
    float peak = spring_boost_seen;
    float now = SpringBoostNow();
    if (now > peak){
        peak = now;
    }
    if (!SpringBoostActive()){
        return peak;
    }
    ArcherInput held = in;
    held.f_jump_pressed = false;
    held.f_draw_released = false;
    held.f_kick_pressed = false;
    held.f_action_pressed = false;
    held.f_kneel_pressed = false;
    Stage ahead = *this;
    for (int i = 0; i < horizon; i++){
        StageEvents e;
        ahead.Tick(held,e);
        if (!ahead.SpringBoostActive() || e.f_landed){
            break;
        }
        float b = ahead.SpringBoostNow();
        if (b > peak){
            peak = b;
        }
    }
    return peak;
}

//--- Branches and balance -----------------------------------------------------------------------

float Stage::BalanceDanger() const{
    float d = fabsf(lean) / (BALANCE_FALL_DEG * STAGE_DEG2RAD);
    return (d < 1.0f) ? d : 1.0f;
}


/*
    The lean, one tick - see BALANCE_TOPPLE. Off a branch there is none: it is zeroed the moment
    she leaves one, by a jump or by walking off the end, so the next branch starts her upright.

    Stepping ON starts a new drift (balance_entries), and a landing knocks her by its speed, to the
    side the drift is about to push anyway - a hard landing on a branch is a wobble to catch.

    Past BALANCE_FALL_DEG she goes over - and catches the branch as she does, hanging below it
    (see BRANCH_HANG_DROP). Which side she went is in the event, for the view.
*/
void Stage::TickBalance(const ArcherInput& in, float land_speed, StageEvents& events){
    if (!f_on_ground || branch_on < 0){
        lean = 0.0f;
        lean_rate = 0.0f;
        balance_ticks = 0;
        return;
    }
    const float two_pi = 6.2831853f;
    float phase = (float)balance_entries * 2.3999632f;      //the golden angle, so entries never repeat
    if (balance_ticks == 0){
        balance_entries++;
        phase = (float)balance_entries * 2.3999632f;
        lean_rate = BALANCE_LAND_WOBBLE * land_speed * ((sinf(phase) >= 0.0f) ? 1.0f : -1.0f);
    }
    balance_ticks++;

    float t = (float)balance_ticks * ARCHER_DT;
    float drift = (sinf(t * two_pi / 1.7f + phase) +
                   0.6f * sinf(t * two_pi / 2.9f + 2.1f * phase) +
                   0.4f * sinf(t * two_pi / 4.3f + 3.3f * phase)) / 2.0f;
    float walking = fabsf(vel.x) / BRANCH_WALK_SPEED;
    if (walking > 1.0f){
        walking = 1.0f;
    }
    float push = ClampF(in.aim_axis,-1.0f,1.0f) * BALANCE_CORRECT;
    float accel = BALANCE_TOPPLE * sinf(lean) + drift * (BALANCE_DRIFT + BALANCE_WALK_DRIFT * walking) +
                  push - BALANCE_DAMPING * lean_rate;
    lean_rate += accel * ARCHER_DT;
    lean += lean_rate * ARCHER_DT;

    if (fabsf(lean) >= BALANCE_FALL_DEG * STAGE_DEG2RAD){
        events.f_lost_balance = true;
        events.fall_side = (lean > 0.0f) ? 1.0f : -1.0f;
        EnterBranchHang(branch_on,events);
    }
}

/*
    The air catch - FindGrabbableLedge's rule for a branch: falling (or all but), her hands within
    the ledge's band of the line, and far enough inside its ends for both hands. From above she
    lands on it long before her hands come near, so in practice this is a jump that comes up
    short of standing on one.
*/
int Stage::FindCatchableBranch() const{
    if (f_on_ground || vel.y > LEDGE_GRAB_MAX_RISE || grab_cooldown > 0){
        return -1;
    }
    float hands = pos.y + ARCHER_HALF_H;
    for (size_t i = 0; i < branches.size(); i++){
        const StageBranch& br = branches[i];
        if (pos.x < br.a.x + BRANCH_HANG_INSET || pos.x > br.b.x - BRANCH_HANG_INSET){
            continue;
        }
        float lip = br.SurfaceY(pos.x) - hands;
        if (lip <= LEDGE_GRAB_BAND_UP && lip >= -LEDGE_GRAB_BAND_DOWN){
            return (int)i;
        }
    }
    return -1;
}

//Hands on the branch, body below it, still - the hang is a pose, as EnterHang's is.
void Stage::EnterBranchHang(int branch, StageEvents& events){
    if (branch < 0 || branch >= (int)branches.size()){
        return;
    }
    const StageBranch& br = branches[branch];
    mode = MODE_HANG;
    hang_block = -1;
    hang_branch = branch;
    pos.x = ClampF(pos.x,br.a.x + BRANCH_HANG_INSET,br.b.x - BRANCH_HANG_INSET);
    pos.y = br.SurfaceY(pos.x) - ARCHER_HALF_H - BRANCH_HANG_DROP;
    vel = v2(0.0f,0.0f);
    f_on_ground = false;
    branch_on = -1;
    coyote_ticks = 0;
    buffer_ticks = 0;
    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    lean = 0.0f;
    lean_rate = 0.0f;
    balance_ticks = 0;
    events.f_caught_branch = true;
}

/*
    Hanging from a branch. Jump pulls her up onto it, LEDGE_CLIMB_INSET along it the way she faces
    - the ledge's climb, path and all, since the rise from this hang to standing is the same - and
    she is balancing again the moment she stands. Down lets go.
*/
void Stage::TickBranchHang(const ArcherInput& in, StageEvents& events){
    if (hang_branch < 0 || hang_branch >= (int)branches.size()){
        ReleaseHang(events);
        return;
    }
    const StageBranch& br = branches[hang_branch];
    if (in.f_jump_pressed){
        mode = MODE_CLIMB;
        climb_ticks = LEDGE_CLIMB_TICKS;
        climb_from = pos;
        float x = ClampF(pos.x + facing * LEDGE_CLIMB_INSET,br.a.x + ARCHER_HALF_W,br.b.x - ARCHER_HALF_W);
        climb_to = v2(x,br.SurfaceY(x) + ARCHER_HALF_H + STAGE_EPS);
        return;
    }
    if (in.f_down_held){
        ReleaseHang(events);
        return;
    }
    pos.y = br.SurfaceY(pos.x) - ARCHER_HALF_H - BRANCH_HANG_DROP;
    vel = v2(0.0f,0.0f);
}

void Stage::SetLevel(int new_level){
    level = (new_level >= 0 && new_level < STAGE_LEVEL_COUNT) ? new_level : STAGE_LEVEL_MAIN;
    Reset();
}

float Stage::RunSpeed() const{
    return (level == STAGE_LEVEL_RANGE) ? ARCHER_RANGE_RUN_SPEED : ARCHER_RUN_SPEED;
}

v2 Stage::StartPosition() const{
    //Both a little above the floor, so the first tick is a landing - see the note in Reset.
    if (level == STAGE_LEVEL_RANGE){
        return v2(0.0f,2.0f);
    }
    if (level == STAGE_LEVEL_ROPE){
        return v2(-6.0f,2.0f);      //a run-up's distance from the rope
    }
    if (level == STAGE_LEVEL_CHARACTER){
        return v2(0.0f,ARCHER_HALF_H + 0.05f);     //on the tile; a drop of 0.05 is not a landing
    }
    if (level == STAGE_LEVEL_WEB){
        return v2(-2.0f,2.0f);      //nine units short of the web: the middle of the three places to shoot from
    }
    return v2(-6.0f,2.0f);
}

/*
    The test range: one floor, a wall at each end, and targets at two distances on either side.

    NARROWER THAN ONE SCREEN, on purpose. The camera shows about 31.8 units across at
    CAMERA_DISTANCE, so with the floor at x -17 .. 17 the whole range fits in one frame when she
    stands in the middle, and the walls sit just past its edges. The camera follows her (see
    ArcherCameraTuning in ApplicationArcher.h), so walking to one end brings that wall into view
    and loses the far one. The walls exist so that there
    is nowhere to fall: an arrow that misses everything sticks in one, and she cannot walk off the
    end of the world into a restart.

    THE WALLS ARE 48 TALL, far above the top of the frame, and that height is measured rather than
    generous. A full draw leaves at ARROW_SPEED_MAX against ARROW_GRAVITY, which straight up is an
    apex of about 44. The first version had 8-unit walls and stage_test's range check caught every
    shot from 30 degrees up sailing clean over them - a 30 degree lob crosses x 17 at about 9 high,
    a 75 degree one at about 39. Change either constant and that check says whether this still
    holds.

    The targets are the same boards as the main level's and stand ON the floor (y 0.8 is half their
    1.6 height), at 6 and 12 either side of the start - a short shot and a long one, both ways, so
    that facing left is tested as often as facing right. Aiming is mirrored with `facing`, and a
    range that only had targets on one side would never catch that mirroring going wrong.

    Nothing here is BLOCK_LEDGE, so stage_test's HighLedge() - which takes the first ledge in
    `blocks` - has nothing to find in this level and must not be pointed at it.
*/
/*
    The rope test: a floor, a wall at each end, and one rope - nothing to fall off, nothing else to
    catch, and nothing in the way of a swing.

    THE ROPE IS LONG and hangs LOW on purpose: 9 units from an anchor 11 up, so its end is 2 above
    the floor. Its lowest link is then within reach standing (FindRopePoint measures from chest
    height), which is where climbing starts, and there are 9 units of rope above to climb. Hung
    from there she swings with her feet 0.6 off the floor; the main level's rope is 6 long and
    caught with a jump.

    The floor is as wide as the main level's run-up needs: at ARCHER_RUN_SPEED from the start at
    -6 she reaches the rope at full speed, so catching it at a run is testable. The walls are the
    range's height, so an arrow always stays in.

    A PIT OFF EACH END, for falls and landings (2026-09-25). Their depths are picked off the fall:
    under ARCHER_GRAVITY * ARCHER_FALL_GRAVITY_MUL (56.7) she passes PUPPET_HARD_LAND_VEL after
    5.5 units and reaches ARCHER_MAX_FALL_SPEED after 10.2.
    - Left, 3 deep: lands at 18.4 u/s, a soft landing much like a full jump's 19. The floor's
      last unit is a LEDGE, so a jump from the pit catches its lip and climbs out - a block of its
      own rather than the whole floor, so only the lip is drawn as something to catch.
    - Right, 15 deep: through the hard landing and on at top speed for the last 4.8 units. Nothing
      gets out of it - Restart. 14 wide, so a sprint off the edge (landing about 6.5 out) still
      comes down on its floor.
    The blocks go down to -16 and below, so nothing reads as a slab floating over a void.
*/
void Stage::BuildRopeLevel(){
    blocks.push_back({   0.50f, -8.00f, 16.50f,  8.00f, BLOCK_SOLID, true });  //the floor, top at 0
    blocks.push_back({ -16.50f, -8.00f,  0.50f,  8.00f, BLOCK_LEDGE, true });  //its left lip
    blocks.push_back({ -21.00f, -9.50f,  4.00f,  6.50f, BLOCK_SOLID, true });  //the shallow pit's floor, top -3
    blocks.push_back({  24.00f,-17.00f,  7.00f,  2.00f, BLOCK_SOLID, true });  //the deep pit's floor, top -15
    blocks.push_back({  31.50f, 16.50f,  0.50f, 31.50f, BLOCK_SOLID, true });  //right wall

    props.push_back({ PROP_ROPE_ANCHOR, 0.00f, 11.00f, 0.10f, 9.00f, 1, 1 });

    //Between the start and the rope, behind her walking line. The lower board points right, at
    //the rope; the upper one points left, where the drop is to go.
    signs.push_back({ SIGN_POST, -3.00f, 0.00f, -1.00f, 0.0f, { "DROP", "ROPE" } });

    //The rope and both pits right of the floor's lip; the gallery adds its own, left of it.
    AddZone("Rope", -17.0f, 31.0f, -20.0f, 48.0f, v2(-6.00f,0.30f));
    BuildSlideGallery();
}

/*
    THE WEB SCENE - docs/web_plan.md section 5. A floor with a wall at each end, the range's height so
    an arrow that misses everything stays in, and the web standing across it with 8 units of open
    floor behind it to walk out onto.

    Twice her height square (2 x ARCHER_HALF_H x 2 = 3.6), the hub a little up and right of the
    middle, as a spider builds it. The BEAM over it is a block, wide enough to cover both posts: her
    apex is 3.2, so she cannot get onto it or over it, and the web is the only way on. The POSTS are
    drawn and not built - the web hangs between them in her plane, and two solid posts there would be
    a wall she could never pass, cut threads or not.

    Shot at from three distances - the far wall's end (-12, 19 units off), the start (-2, 9) and the
    floor right in front of it - and from a one-way PLATFORM at 2.4, which she walks under and jumps up
    onto, to shoot level through the middle or down through the bottom. A sign names it. The web is
    added last, after every block, as AddWeb asks.
*/
void Stage::BuildWebLevel(){
    blocks.push_back({   3.00f, -2.00f, 17.00f,  2.00f, BLOCK_SOLID, true });  //the floor, x -14 .. 20, top 0
    blocks.push_back({ -14.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //left wall, top at 48
    blocks.push_back({  20.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //right wall
    const float web_w = 4.0f * ARCHER_HALF_H, web_h = 4.0f * ARCHER_HALF_H;
    const float web_x = 7.0f;
    blocks.push_back({ web_x + web_w * 0.5f, web_h + 0.25f, web_w * 0.5f + 0.3f, 0.25f, BLOCK_SOLID, true });   //the beam
    blocks.push_back({ 3.00f, 2.25f, 1.00f, 0.15f, BLOCK_PLATFORM, true });    //the shooting step, top 2.4
    signs.push_back({ SIGN_POST, 5.20f, 0.00f, -1.00f, 0.0f, { "WEB" } });
    AddZone("Web", -14.0f, 20.0f, -4.0f, 48.0f, v2(-2.00f,0.30f));
    AddWeb(web_x,0.0f,web_w,web_h,12,6,0.15f,0.25f);
}

/*
    The character scene: one round tile, its walkable top at the origin, and nothing else.

    The collider is the main level's round tile's, which the app already measures against the
    mesh. There is nothing to fall off to - her feet are locked by the app - but a jump on the spot
    comes back down onto the tile, which is why it has a collider at all rather than a floor at 0.
*/
void Stage::BuildCharacterLevel(){
    AddScenery({ SCENERY_TILE_ROUND, 0.00f, 0.00f, 0.00f, 0.0f, 1.85f, 0.50f });
    //The only level where the plane is not locked, so the only one where a block's depth decides
    //where an arrow goes (see SegmentHitsBlock): as deep as the round tile is wide, not the default
    //slab, or a shot at her feet toward the camera would fall past the front of the grass.
    blocks.back().depth = 1.85f;
    AddZone("Character", -4.0f, 4.0f, -4.0f, 8.0f, v2(0.00f,ARCHER_HALF_H + 0.05f));
}

/*
    THE SLIDE GALLERY - docs/plant_mechanics_plan.md, "Sliding". Left of the shallow pit, on its floor
    (y -3), where the rope level's left wall used to stand. Everything here is fixed, so a slide
    that feels wrong is the slide's fault and not a spring's.

      THE LADDER: six hills, 8 14 18 25 35 50 degrees, each HILL_H high with a flat top to stand
        on and a sign naming its angle - walked into from the right, so each is an ascent, a top
        and a descent at the same angle. 14 is SPRING_LEAF_SLIP_DEG exactly. The 25 and the 35
        meet at the foot with no floor between: the V.
      THE LONG RUN: stairs up to a block 6 high, and 25 degrees down from its top all the way to
        the floor - 12.9 units of it, for a slide's top speed.
      THE DROP: past a run-out, a 25 degree ramp off the end of the floor over a shallow pit - the
        leaf's way off, a slide that ends in the air. The pit is 2.5 deep, which a jump gets out of.

    Every ramp is sealed with blocks - its low end on the floor, its high end against a face - so
    there is no way under one but from below, through a surface that is one-way anyway.
*/
void Stage::BuildSlideGallery(){
    const float gy = SLIDE_GALLERY_FLOOR_Y;
    const float h = SLIDE_GALLERY_HILL_H;
    const float top_w = 2.0f;
    static const char* HILL_LABELS[SLIDE_GALLERY_HILLS] = { "8", "14", "18", "25", "35", "50" };
    static const float HILL_GAP_AFTER[SLIDE_GALLERY_HILLS] = { 2.5f, 2.5f, 2.5f, 0.0f, 2.5f, 3.0f };

    float x = SLIDE_GALLERY_START_X;        //the right foot of the next hill
    for (int i = 0; i < SLIDE_GALLERY_HILLS; i++){
        float run = h / tanf(SLIDE_GALLERY_DEG[i] * STAGE_DEG2RAD);
        float top_r = x - run;
        float top_l = top_r - top_w;
        ramps.push_back({ v2(top_r,gy + h), v2(x,gy) });                  //up, from the right
        blocks.push_back({ (top_l + top_r) * 0.5f, gy + h * 0.5f, top_w * 0.5f, h * 0.5f, BLOCK_SOLID, true });
        ramps.push_back({ v2(top_l - run,gy), v2(top_l,gy + h) });        //and down, going left
        signs.push_back({ SIGN_POST, (top_l + top_r) * 0.5f, gy + h, -1.0f, 0.0f, { HILL_LABELS[i] } });
        x = top_l - run - HILL_GAP_AFTER[i];
    }

    //The long run: two steps of 2 (her jump is 3.2), a block 6 high, and 25 degrees down its far side.
    blocks.push_back({ x - 2.0f,   gy + 1.0f, 2.0f, 1.0f, BLOCK_SOLID, true });
    blocks.push_back({ x - 5.5f,   gy + 2.0f, 1.5f, 2.0f, BLOCK_SOLID, true });
    blocks.push_back({ x - 8.5f,   gy + 3.0f, 1.5f, 3.0f, BLOCK_SOLID, true });
    float long_top = x - 10.0f;
    float long_foot = long_top - 6.0f / tanf(SLIDE_GALLERY_LONG_DEG * STAGE_DEG2RAD);
    ramps.push_back({ v2(long_foot,gy), v2(long_top,gy + 6.0f) });
    signs.push_back({ SIGN_POST, x - 2.0f, gy + 2.0f, -1.0f, 0.0f, { "LONG" } });

    //The floor, from the pit to a run-out past the long run's foot, then the drop.
    float floor_end = long_foot - 5.0f;
    blocks.push_back({ (floor_end + SLIDE_GALLERY_START_X + 2.0f) * 0.5f, -9.5f,
                       (SLIDE_GALLERY_START_X + 2.0f - floor_end) * 0.5f, 6.5f, BLOCK_SOLID, true });
    float drop_len = 3.5f;
    ramps.push_back({ v2(floor_end - drop_len,gy - drop_len * tanf(SLIDE_GALLERY_LONG_DEG * STAGE_DEG2RAD)),
                      v2(floor_end,gy) });
    signs.push_back({ SIGN_POST, floor_end + 1.5f, gy, -1.0f, 0.0f, { "DROP" } });
    float pit_l = floor_end - 14.0f;
    blocks.push_back({ (pit_l + floor_end) * 0.5f, -10.75f, (floor_end - pit_l) * 0.5f, 5.25f, BLOCK_SOLID, true });
    blocks.push_back({ pit_l - 0.5f, 22.50f, 0.50f, 25.50f, BLOCK_SOLID, true });  //the level's left wall now

    //Its zone: from the left wall to the floor's lip, the shallow pit included - arriving on the
    //pit's floor faces her up the first, gentlest hill.
    AddZone("Slide gallery", pit_l, -17.0f, -20.0f, 20.0f, v2(-21.00f,-2.70f));
}

void Stage::BuildRangeLevel(){
    blocks.push_back({   0.00f, -2.00f, 17.00f, 2.00f, BLOCK_SOLID, true });   //the floor, top at 0
    blocks.push_back({ -17.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //left wall, top at 48
    blocks.push_back({  17.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //right wall

    //Boards at the ends, stands nearer in, so the two kinds can be shot side by side.
    props.push_back({ PROP_TARGET, -12.00f, 0.80f, 0.30f, 1.60f, 1, 1 });
    props.push_back({ PROP_TARGET,  -6.00f, 0.70f, 0.70f, 1.40f, 1, 1, false, TARGET_STAND });
    props.push_back({ PROP_TARGET,   6.00f, 0.70f, 0.70f, 1.40f, 1, 1, false, TARGET_STAND });
    props.push_back({ PROP_TARGET,  12.00f, 0.80f, 0.30f, 1.60f, 1, 1 });

    //A straw man a few steps right of the start - the one thing here that scores the kick rather
    //than the arrow. 1.95 tall at her scale; the box is what the boot is swept against.
    props.push_back({ PROP_STRAWMAN, 3.00f, 0.975f, 0.50f, 1.95f, 1, 1 });

    /*
        An arch of FLOATING targets over the start - five boards on a half circle of radius 5
        centred a unit above the floor, at 30, 60, 90, 120 and 150 degrees. Gravity off (see
        StageProp::f_floating), so this is where "what does an arrow do to a body nothing holds
        up" gets looked at. Upright rather than turned along the curve, because StageProp has no
        rotation and a board is read the same way standing up.

        The top one is at x 0, directly overhead, and she cannot hit it from where she starts:
        the aim stops at BOW_AIM_MAX_DEG (85), and an 85 degree shot has drifted half a unit
        sideways by the time it is up there - more than the board is wide. A step to one side
        is the answer, which is a fair thing to ask of a range.
    */
    const float arch_r = 5.0f;
    const float arch_cy = 1.0f;
    const float arch_deg[] = { 30.0f, 60.0f, 90.0f, 120.0f, 150.0f };
    for (size_t i = 0; i < sizeof(arch_deg)/sizeof(arch_deg[0]); i++){
        float rad = arch_deg[i] * 3.14159265358979f / 180.0f;
        StageProp t = { PROP_TARGET, arch_r * cosf(rad), arch_cy + arch_r * sinf(rad), 0.30f, 1.60f, 1, 1 };
        t.f_floating = true;
        props.push_back(t);
    }

    /*
        A crate pyramid near each wall: 3, 2, 1 - six crates a side. The main level's crates, the
        same 0.80 box and the same 0.05 gap between rows that its two-high stacks use, so a stack
        here settles the way a stack there does.

        Centred at 14, so the base spans 12.8 .. 15.2: inside the frame with her standing at the
        start (about 15.9 either side - the first layout, at 15, had half of each pyramid cut off
        by the screen edge, back when the range camera did not move), and just BEHIND the far targets at 12, so a board knocked off its
        feet falls into a pyramid rather than onto bare floor - which is the interaction the
        stacks are here to produce.
    */
    const float crate = 0.80f;
    const float step = 0.85f;           //crate plus a 0.05 gap, side to side and row to row
    const float sides[] = { -14.0f, 14.0f };
    for (size_t s = 0; s < 2; s++){
        for (int row = 0; row < 3; row++){
            int count = 3 - row;
            float y = crate * 0.5f + row * step;
            for (int c = 0; c < count; c++){
                float x = sides[s] + ((float)c - (float)(count - 1) * 0.5f) * step;
                props.push_back({ PROP_CRATE, x, y, crate, crate, 1, 1 });
            }
        }
    }

    //One zone, wall to wall: the range is one room.
    AddZone("Range", -17.0f, 17.0f, -4.0f, 48.0f, v2(0.00f,0.30f));
}

//--- The props, as the rules see them -----------------------------------------------------------

void Stage::ClearObstacles(){
    obstacles.clear();
}

void Stage::AddObstacle(float x, float y, float hw, float hh, int id, bool f_pushable, bool f_blocks){
    StageObstacle o;
    o.x = x;
    o.y = y;
    o.hw = hw;
    o.hh = hh;
    o.id = id;
    o.f_pushable = f_pushable;
    o.f_blocks = f_blocks;
    obstacles.push_back(o);
}

//--- The tick -----------------------------------------------------------------------------------

void Stage::Tick(const ArcherInput& in_raw, StageEvents& events){
    /*
        GETTING UP, NOTHING COUNTS: the whole tick runs on an empty input, which is what makes the
        lock total - every verb below reads `in`, so none of them can be forgotten, and an edge
        pressed during the get-up is dropped rather than held for the tick it ends on. See GETUP_TICKS.
    */
    static const ArcherInput no_input;
    const ArcherInput& in = (mode == MODE_GETUP) ? no_input : in_raw;

    //The floors' edges, if last tick broke a wall or crumbled a stone - the cheap check, see
    //RefreshEdges. Nothing here reads them yet; the vines and the fear will (docs/vine_plan.md 15).
    if (blocks.size() != edges_blocks || CountAliveBlocks() != edges_alive){
        RebuildEdges();
    }

    //First, so a pick and a release in one tick loose the kind just picked.
    SelectArrow(in_raw,events);

    /*
        Order matters and is not arbitrary:

        The bow goes first because drawing it halves the run speed, so the archer has to be moved
        with this tick's draw state rather than last tick's. The arrow is loosed AFTER the archer
        has moved, so it leaves from where the archer ended up - loose first and every shot starts
        one tick behind the bow it came out of, which is invisible standing still and obvious
        running. The arrows fly last, so an arrow loosed this tick spends its first tick where it
        was born rather than already a frame downrange.
    */
    TickBow(in,events);

    /*
        A release looses only with an arrow ON THE STRING - see BOW_NOCK_TICKS. Before that it
        cancels the draw: she was still reaching for the quiver, and an arrow leaving the bow from
        there is what made a tap look wrong. (This used to be the opposite, deliberately: a press
        and release inside one tick fired a minimum-power shot so a tap was never swallowed. The
        draw animation made the tap visible, and a shot out of an empty bow is worse than none.)
    */
    bool f_loose = IsNocked() && in.f_draw_released;
    if (bow_mode == BOW_DRAWING && in.f_draw_released && !f_loose){
        bow_mode = BOW_IDLE;
        draw_ticks = 0;
        draws_cancelled++;
    }

    //The springs before she moves, loaded with where she stood last tick: she then moves against
    //where they are now, from where they were, which is what CollideSpringPlants compares.
    TickSpringPlants();
    TickBridges();
    TickWebs();

    //Before the archer moves, so the boot sweeps from where they were standing when it went out.
    //At a full run those differ by 0.15 of a unit - the difference between connecting with the
    //near brick of a wall and connecting with nothing.
    TickKick(in,events);

    TickArcher(in,events);

    if (f_loose){
        Loose(events);
    }
    TickArrows(events);
    //After the arrows, so a hole they cut is open on the tick it was cut.
    TickWebWalls(events);

    prev_aim_axis = in.aim_axis;
    if (SpringBoostActive()){
        float now = SpringBoostNow();
        if (now > spring_boost_seen){
            spring_boost_seen = now;
        }
    }else{
        spring_boost_seen = 0.0f;
    }

    //Once she has moved: a stone starts on the tick she lands on it.
    TickCrumbles(events);
    //Last, off where she ended the tick - a zone is about where she IS.
    TickZones(events);
    //And her body, off the same, and off what this tick's events say she did.
    TickVitals(events);

    ticks++;
}

//--- Crumbling rocks -------------------------------------------------------------------------------

bool Stage::StandingOn(const StageBlock& b) const{
    if (!f_on_ground || !b.f_alive || spring_on >= 0){
        return false;
    }
    float feet = pos.y - ARCHER_HALF_H;
    return fabsf(feet - b.Top()) < 0.02f &&
           pos.x + ARCHER_HALF_W > b.Left() && pos.x - ARCHER_HALF_W < b.Right();
}

/*
    docs/bridge_crumble_plan.md section 2. Whole until she stands on it; then shaking - still holding
    her - for CRUMBLE_SHAKE_TICKS; then gone. ONCE STARTED IT GOES: stepping off does not stop it,
    which is what makes a row of them a run rather than a walk.

    Gone is f_alive cleared, which every sweep already skips, and an event, as for a kicked wall -
    the app has a collider to switch off and rubble to drop. An arrow stuck in it has nothing to
    hold it now, so it is let go and falls.
*/
void Stage::TickCrumbles(StageEvents& events){
    for (size_t i = 0; i < blocks.size(); i++){
        StageBlock& b = blocks[i];
        if (!b.f_alive || b.kind != BLOCK_CRUMBLE){
            continue;
        }
        if (b.crumble_ticks < 0){
            if (b.crumble_group < 0 && StandingOn(b)){
                b.crumble_ticks = 0;
                events.crumbles_started.push_back((int)i);
            }
            continue;
        }
        b.crumble_ticks++;
        if (b.crumble_ticks < CRUMBLE_SHAKE_TICKS){
            continue;
        }
        b.f_alive = false;
        events.crumbled_blocks.push_back((int)i);
        //A hair round it, because an arrow sticks at the surface it hit, not inside.
        const float m = 0.05f;
        for (int a = 0; a < ARROW_MAX_LIVE; a++){
            Arrow& arrow = arrows[a];
            if (arrow.f_live && arrow.f_stuck &&
                arrow.pos.x > b.Left() - m && arrow.pos.x < b.Right() + m &&
                arrow.pos.y > b.Bottom() - m && arrow.pos.y < b.Top() + m){
                arrow.f_stuck = false;
                arrow.vel = v3(0.0f,0.0f,0.0f);
                //Its first sweep from where it hangs, not from where it flew in from.
                arrow.prev_pos = arrow.pos;
            }
        }
    }

    /*
        The started groups' next blocks, after the loop above: a block started here counts from
        next tick, exactly as a stone does from the tick she lands, so it too is gone
        CRUMBLE_SHAKE_TICKS after its start.
    */
    for (size_t g = 0; g < crumble_groups.size(); g++){
        StageCrumbleGroup& group = crumble_groups[g];
        if (group.ticks < 0 || group.f_done){
            continue;
        }
        bool f_any_left = false;
        for (size_t k = 0; k < group.blocks.size(); k++){
            StageBlock& b = blocks[group.blocks[k]];
            if (!b.f_alive){
                continue;
            }
            f_any_left = true;
            if (b.crumble_ticks < 0 && group.ticks >= group.starts[k]){
                b.crumble_ticks = 0;
                events.crumbles_started.push_back(group.blocks[k]);
            }
        }
        group.ticks++;
        if (!f_any_left){
            group.f_done = true;
            events.crumble_groups_done.push_back((int)g);
        }
    }
}

int Stage::AddCrumbleGroup(const char* name, size_t first, float ticks_per_unit){
    StageCrumbleGroup group;
    group.name = name;
    const int id = (int)crumble_groups.size();
    for (size_t i = first; i < blocks.size(); i++){
        blocks[i].crumble_group = id;
        group.blocks.push_back((int)i);
        group.starts.push_back((int)lroundf((blocks[i].Left() - blocks[first].Left()) * ticks_per_unit));
    }
    crumble_groups.push_back(group);
    return id;
}

//--- Zones -----------------------------------------------------------------------------------------

void Stage::AddZone(const char* name, float left, float right, float bottom, float top, v2 arrive){
    StageZone z;
    z.x = (left + right) * 0.5f;
    z.y = (bottom + top) * 0.5f;
    z.hw = (right - left) * 0.5f;
    z.hh = (top - bottom) * 0.5f;
    z.name = name;
    z.id = (int)zones.size();
    z.arrive = arrive;
    zones.push_back(z);
}

int Stage::AddTrigger(const char* name, float left, float right, float bottom, float top, const StageZoneEffect& effect){
    AddZone(name,left,right,bottom,top,v2(0.0f,0.0f));
    StageZone& z = zones.back();
    z.f_area = false;
    z.effects.push_back(effect);
    return z.id;
}

void Stage::ApplyZoneEffect(const StageZoneEffect& effect, StageEvents& events){
    switch (effect.kind){
        case ZONE_START_CRUMBLE_GROUP:
            if (effect.target >= 0 && effect.target < (int)crumble_groups.size() &&
                crumble_groups[effect.target].ticks < 0){
                crumble_groups[effect.target].ticks = 0;
                events.crumble_groups_started.push_back(effect.target);
            }
            break;
    }
}

/*
    Her body box against every zone, and the change since last tick. The standing box whatever she
    is doing - kneeling or hanging moves her a little, and a zone's edge flickering in and out as
    she kneels on it would be two events for nothing.
*/
void Stage::TickZones(StageEvents& events){
    if (zone_inside.size() != zones.size()){
        zone_inside.assign(zones.size(),0);
    }
    const float l = pos.x - ARCHER_HALF_W, r = pos.x + ARCHER_HALF_W;
    const float b = pos.y - ARCHER_HALF_H, t = pos.y + ARCHER_HALF_H;
    for (size_t i = 0; i < zones.size(); i++){
        StageZone& zone = zones[i];
        uint8_t now = zone.Overlaps(l,r,b,t) ? 1 : 0;
        if (now && !zone_inside[i]){
            events.zones_entered.push_back((int)i);
            //Once per run: the first entry queues them, and nothing does again until a restart.
            if (!zone.f_fired && !zone.effects.empty()){
                zone.f_fired = true;
                for (const StageZoneEffect& effect : zone.effects){
                    PendingEffect p;
                    p.effect = effect;
                    p.at = ticks + (uint64_t)((effect.delay > 0) ? effect.delay : 0);
                    pending_effects.push_back(p);
                }
            }
        }else if (!now && zone_inside[i]){
            events.zones_left.push_back((int)i);
        }
        zone_inside[i] = now;
    }
    //Due ones in the order they were queued, which is the order the zones were entered.
    for (size_t i = 0; i < pending_effects.size(); ){
        if (pending_effects[i].at <= ticks){
            StageZoneEffect effect = pending_effects[i].effect;
            pending_effects.erase(pending_effects.begin() + i);
            ApplyZoneEffect(effect,events);
        }else{
            i++;
        }
    }
}

int Stage::CurrentZone() const{
    int best = -1;
    float best_area = 0.0f;
    for (size_t i = 0; i < zones.size() && i < zone_inside.size(); i++){
        if (!zone_inside[i] || !zones[i].f_area){
            continue;
        }
        float area = zones[i].hw * zones[i].hh;
        if (best < 0 || area < best_area){
            best = (int)i;
            best_area = area;
        }
    }
    return best;
}

int Stage::FindZone(const char* name) const{
    for (size_t i = 0; i < zones.size(); i++){
        if (zones[i].name == name){
            return (int)i;
        }
    }
    return -1;
}

//--- Vitals ------------------------------------------------------------------------------------

float Stage::DropBelow(float x, float y) const{
    bool  f_found = false;
    float top = 0.0f;
    for (const StageBlock& b : blocks){
        if (!b.f_alive || x < b.Left() || x > b.Right() || b.Top() > y + STAGE_EPS * 50.0f){
            continue;
        }
        if (!f_found || b.Top() > top){
            top = b.Top();
            f_found = true;
        }
    }
    return f_found ? (y - top) : VITALS_NO_FLOOR;
}

float Stage::DropFear(float drop){
    return ClampF((drop - VITALS_DROP_FROM) / (VITALS_DROP_TO - VITALS_DROP_FROM),0.0f,1.0f);
}

//Eases `value` toward `target` over one tick, with the time constant for whichever way it goes.
static float EaseToward(float value, float target, float rise_tau, float fall_tau){
    float tau = (target > value) ? rise_tau : fall_tau;
    return value + (target - value) * (1.0f - expf(-ARCHER_DT / tau));
}

void Stage::TickVitals(const StageEvents& events){
    StageVitals& v = vitals;
    float feet = pos.y - ARCHER_HALF_H;

    //--- Exertion: what she is doing ---
    float target = v.exertion;          //in the air, and anything not listed, it holds
    float rise_tau = VITALS_EXERTION_RISE_TAU;
    float fall_tau = VITALS_EXERTION_FALL_TAU;
    switch (mode){
    case MODE_GROUND:{
        float share = fabsf(vel.x) / RunSpeed();
        if (share <= VITALS_RUN_FROM){
            target = 0.0f;
            //Standing recovers at the full rate; a walk at its top only VITALS_WALK_RECOVERY of it.
            float recovery = 1.0f - (1.0f - VITALS_WALK_RECOVERY) * (share / VITALS_RUN_FROM);
            fall_tau /= recovery;
        }else{
            target = VITALS_RUN_TARGET * ClampF((share - VITALS_RUN_FROM) / (1.0f - VITALS_RUN_FROM),0.0f,1.0f);
            //Slowing from a sprint to a jog is not a rest.
            fall_tau /= VITALS_WALK_RECOVERY;
        }
        break;
    }
    case MODE_HANG:
        target = VITALS_HANG_TARGET;
        rise_tau = VITALS_HANG_RISE_TAU;
        fall_tau /= VITALS_WALK_RECOVERY;
        break;
    case MODE_CLIMB:
        target = VITALS_CLIMB_TARGET;
        break;
    case MODE_ROPE:
        target = (rope_climb != 0) ? VITALS_CLIMB_TARGET : VITALS_ROPE_TARGET;
        fall_tau /= VITALS_WALK_RECOVERY;
        break;
    case MODE_KNEEL:
    case MODE_GETUP:
        target = 0.0f;
        break;
    default:
        break;
    }
    v.exertion_target = target;
    v.exertion = EaseToward(v.exertion,target,rise_tau,fall_tau);
    if (events.f_jumped){
        v.exertion += VITALS_JUMP_EFFORT;
    }
    if (events.f_kick_started){
        v.exertion += VITALS_KICK_EFFORT;
    }
    v.exertion = ClampF(v.exertion,0.0f,1.0f);

    //--- Fear: how far she could fall ---
    float fear = 0.0f;
    if (mode == MODE_AIR){
        //The speed she lands at, from what she has now and the drop still under her - energy, so
        //a rise counts as much as a fall. Not a forecast: over a gap she is afraid of the gap.
        float drop = DropBelow(pos.x,feet);
        float impact = sqrtf(vel.y * vel.y + 2.0f * ARCHER_GRAVITY * ((drop > 0.0f) ? drop : 0.0f));
        fear = ClampF((impact - VITALS_IMPACT_FROM) / (VITALS_IMPACT_TO - VITALS_IMPACT_FROM),0.0f,1.0f);
    }else if (mode == MODE_HANG || mode == MODE_CLIMB || mode == MODE_ROPE){
        fear = DropFear(DropBelow(pos.x,feet));
    }else if (f_on_ground && spring_on < 0 && branch_on < 0 && ramp_on < 0 && bridge_on < 0){
        /*
            On a floor: the nearest place either side where it ends, within VITALS_EDGE_REACH, and
            the drop past it. A step down is nothing (DropFear starts past a jump's height); the
            middle of a floor is nothing however high it is. Pads, branches, ramps and bridges are skipped:
            a branch has its balance instead, and neither is a block the column scan could see.
        */
        const float step = 0.1f;
        for (float side = -1.0f; side <= 1.0f; side += 2.0f){
            for (float d = 0.0f; d <= VITALS_EDGE_REACH + STAGE_EPS; d += step){
                float drop = DropBelow(pos.x + side * d,feet);
                if (drop > VITALS_DROP_FROM){
                    float edge = VITALS_EDGE_SHARE * (1.0f - d / VITALS_EDGE_REACH) * DropFear(drop);
                    fear = fmaxf(fear,edge);
                    break;
                }
            }
        }
    }
    if (branch_on >= 0){
        fear = fmaxf(fear,VITALS_BALANCE_SHARE * BalanceDanger());
    }
    v.fear_target = fear;
    v.fear = EaseToward(v.fear,fear,VITALS_FEAR_RISE_TAU,VITALS_FEAR_FALL_TAU);
    if (events.f_lost_balance){
        v.fear += VITALS_LOST_BALANCE_FEAR;
    }
    if (events.f_landed && events.land_speed >= VITALS_HARD_LANDING){
        v.fear += VITALS_HARD_LANDING_FEAR;
    }
    v.fear = ClampF(v.fear,0.0f,1.0f);

    //--- The heart, trailing both ---
    float bpm = VITALS_REST_BPM + VITALS_EXERTION_BPM * v.exertion + VITALS_FEAR_BPM * v.fear;
    bpm = fminf(bpm,VITALS_MAX_BPM);
    v.heart_rate = EaseToward(v.heart_rate,bpm,VITALS_HEART_RISE_TAU,VITALS_HEART_FALL_TAU);
}

void Stage::TickBow(const ArcherInput& in, StageEvents& events){
    (void)events;

    /*
        The aim tilts only with the bow drawn - see BOW_AIM_RETURN_TICKS. So the rope's climb, a
        branch's balance and the kick's choice all have the keys to themselves, and on a branch
        with the bow drawn both happen at once: aiming from a branch costs balance, which is the
        point of shooting from one. Read off last tick's bow, so the tick a draw starts does not
        tilt yet; a tick is nothing to the eye.
    */
    if (bow_mode == BOW_DRAWING){
        aim_deg = ClampF(aim_deg + in.aim_axis * BOW_AIM_RATE_DEG * ARCHER_DT,
                         BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
        aim_roam_ticks = 0;
    }else{
        bool f_moving = fabsf(vel.x) > BOW_AIM_RETURN_SPEED || fabsf(vel.y) > BOW_AIM_RETURN_SPEED;
        if (f_moving && aim_roam_ticks < BOW_AIM_RETURN_TICKS){
            aim_roam_ticks++;
        }
        //Once started back it carries on to neutral, even if she stops on the way.
        if (aim_roam_ticks >= BOW_AIM_RETURN_TICKS){
            aim_deg = MoveToward(aim_deg,BOW_AIM_NEUTRAL_DEG,BOW_AIM_RETURN_RATE_DEG * ARCHER_DT);
        }
    }

    //Both hands are on the rock: no draw can START while hanging or climbing, and EnterHang cancels
    //one already under way.
    bool f_hands_full = (mode == MODE_HANG || mode == MODE_CLIMB || mode == MODE_ROPE);

    if (in.f_draw_down && !f_hands_full){
        if (bow_mode == BOW_IDLE){
            bow_mode = BOW_DRAWING;
            draw_ticks = 0;
            sway_ticks = 0;
            draws_started++;
        }else{
            if (draw_ticks < BOW_DRAW_TICKS){
                draw_ticks++;
            }
            //Counted from the tick AFTER the nock, so the sway is exactly zero when the arc first
            //appears.
            if (draw_ticks > BOW_NOCK_TICKS){
                sway_ticks++;
            }
        }
        return;
    }

    //Key is up. If we were drawing and this is NOT the release we were told about, the draw was
    //interrupted rather than loosed - the window lost focus, or a scripted hold expired. Cancel
    //it: an arrow that fires itself because the player alt-tabbed is a bug, not a feature.
    if (bow_mode == BOW_DRAWING && !in.f_draw_released){
        bow_mode = BOW_IDLE;
        draw_ticks = 0;
    }
}

void Stage::TickArcher(const ArcherInput& in, StageEvents& events){
    //Getting up owns the body outright, like the climb below - it is not a stance anything else
    //layers on top of.
    if (mode == MODE_GETUP){
        TickGetUp(events);
        return;
    }
    /*
        ON THE ROPE THE SOLVER IS DRIVING, and this function must not also be - two things
        integrating one position is the classic way to get a character that vibrates. TickRope
        decides only when to let go; the app writes pos and vel back from the swinging body before
        every tick, and takes them away again on release.
    */
    if (mode == MODE_ROPE){
        TickRope(in,events);
        return;
    }
    if (rope_cooldown > 0){
        rope_cooldown--;
    }
    //Hanging and climbing own the position outright: no gravity, no run, no jump arc. Branching
    //here rather than threading `if (mode == ...)` through the code below is the whole reason
    //ArcherMode is one enum instead of a pile of booleans.
    if (mode == MODE_CLIMB){
        TickClimb(in,events);
        return;
    }
    if (mode == MODE_HANG){
        TickHang(in,events);
        return;
    }
    if (grab_cooldown > 0){
        grab_cooldown--;
    }

    /*
        Kneeling owns the body the same way. Getting down is decided HERE, on the tick of the
        press, so that tick already brakes: from the ground only - f_on_ground is last tick's, like
        the kick's gate - and not mid-kick, whose plant and boot box belong to standing.
    */
    //Not on a spring plant: the kneel plants her on ground that stays put.
    if (in.f_kneel_pressed && mode == MODE_GROUND && f_on_ground && kick_ticks == 0 && spring_on < 0 &&
        branch_on < 0 && ramp_on < 0 && bridge_on < 0){
        mode = MODE_KNEEL;
        kneel_phase = KNEEL_LOWERING;
        kneel_ticks = 0;
        events.f_knelt = true;
    }
    if (mode == MODE_KNEEL){
        TickKneel(in,events);
        return;
    }

    /*
        RIDING A SPRING PLANT: she keeps the speed it had under her last tick (CollideSpringPlants
        gave it her), so her vel.y is not 0 on this ground. That is what lets her leave it on her
        own - when it springs back and then slows faster than gravity slows her, she carries on.
        What it throws her with is recorded so the jump cut leaves it alone.
    */
    bool f_riding = f_on_ground && spring_on >= 0;
    if (f_riding){
        launch_lift = (vel.y > 0.0f) ? vel.y : 0.0f;
    }else if (f_on_ground){
        launch_lift = 0.0f;
    }

    //--- Horizontal ---------------------------------------------------------------------------
    float move_scale = (bow_mode == BOW_DRAWING) ? ARCHER_DRAW_MOVE_SCALE : 1.0f;
    float target_vx = ClampF(in.move_axis,-1.0f,1.0f) * RunSpeed() * move_scale;
    //Along a branch, one foot in front of the other.
    if (f_on_ground && branch_on >= 0){
        target_vx = ClampF(in.move_axis,-1.0f,1.0f) * BRANCH_WALK_SPEED * move_scale;
    }
    //Down a leaf too steep to hold: no grip to run or stop with, only a little control.
    float slide = SlideAccel();
    bool f_grip = f_on_ground && slide == 0.0f;

    /*
        A KICK ON THE GROUND PLANTS THE FEET, and freezes the facing with them.

        Both halves matter. The plant is what makes a kick a commitment rather than something you
        mash while running; friction rather than a hard stop, so it reads as weight instead of as
        the game confiscating the controls. Freezing the facing is the less obvious one: the boot's
        box is built from `facing`, so a player who turns mid-kick would otherwise swing it through
        180 degrees and connect with whatever happened to be behind them.

        The `&& f_on_ground` is belt and braces now rather than a branch: TickKick will not start a
        kick off the ground and ends one that leaves it, so kick_ticks > 0 already implies it. It
        stays because this line is what the plant MEANS, and a reader should not have to go and
        find the gate to know that a kick in the air does not root her.
    */
    bool f_planted = (kick_ticks > 0) && f_on_ground;
    if (f_planted){
        vel.x = MoveToward(vel.x,0.0f,KICK_ROOT_FRICTION * ARCHER_DT);
    }else if (in.move_axis > 0.01f || in.move_axis < -0.01f){
        float accel = f_grip ? ARCHER_RUN_ACCEL : ARCHER_AIR_ACCEL;
        /*
            ON A SLIDE THE FEET HAVE NO GRIP. Pushing UP it, what she has is a share of the pull
            itself (SPRING_LEAF_SLIDE_CONTROL): it slows the slide and can never climb it. That was a
            share of the AIR accel, 19 u/s^2, which beat the pull of every slope up to 50 degrees -
            measured on the slide gallery's ramps, she walked up 35 degrees at nearly full speed.
            Pushing down it or across, the air share still: a pull only just past the slip angle is
            near zero, and a share of it left her unable to walk down a ramp at all.
        */
        if (slide != 0.0f){
            bool f_uphill = (target_vx * slide) < 0.0f;
            accel = SPRING_LEAF_SLIDE_CONTROL * (f_uphill ? fabsf(slide) : ARCHER_AIR_ACCEL);
        }
        vel.x = MoveToward(vel.x,target_vx,accel * ARCHER_DT);
        //Facing follows the input even mid-draw. The aim angle is relative to facing, so turning
        //while drawn mirrors the shot rather than losing it, which is what a player turning to
        //deal with something behind them means.
        facing = (in.move_axis > 0.0f) ? 1.0f : -1.0f;
    }else if (slide == 0.0f){
        float friction = f_grip ? ARCHER_RUN_FRICTION : ARCHER_AIR_FRICTION;
        vel.x = MoveToward(vel.x,0.0f,friction * ARCHER_DT);
    }
    /*
        And NO FRICTION ON TOP OF A SLIDE: the pull already nets out the friction that holds her
        at the slip angle. With the air friction taken off it as well, the two together held her
        still on everything short of about 45 degrees - on the gallery's ramps she crept 0.14 in
        three seconds at 18 degrees and 0.64 at 35, when the rule says just past the slip angle she
        creeps and steeper throws her off. What bounds a long slide instead is SLIDE_MAX_SPEED.
    */
    vel.x += slide * ARCHER_DT;
    if (slide != 0.0f){
        float cap = SLIDE_MAX_SPEED * cosf(SlopeUnderFeetDeg() * STAGE_DEG2RAD);
        if (vel.x * slide > 0.0f && fabsf(vel.x) > cap){
            vel.x = (vel.x > 0.0f) ? cap : -cap;
        }
    }

    //--- Jump -----------------------------------------------------------------------------------
    if (in.f_jump_pressed){
        buffer_ticks = ARCHER_JUMP_BUFFER_TICKS;
    }
    /*
        NO JUMP OFF A BRANCH. Her feet are one in front of the other on something as thick as a
        wrist; there is nothing to push off, and a jump would be a way out of the balance - hop
        across instead of walking it. Landing ON one is fine. So on a branch a press does nothing
        and is not kept for later either (a press made just before touching down would otherwise
        fire on touchdown), and the coyote grace is not given for walking off one's end (see the
        grace timers below). Off it again - onto a stump, a ledge - she jumps as ever.
    */
    bool f_balancing = f_on_ground && branch_on >= 0;
    if (f_balancing){
        buffer_ticks = 0;
    }
    bool f_may_jump = !f_balancing && (f_on_ground || coyote_ticks > 0);
    if (buffer_ticks > 0 && f_may_jump){
        /*
            THE FLING: whatever she is already rising at is kept, and the jump goes on top. Off a
            spring plant on its way up that is its rise; a few ticks after one threw her, it is
            what is left of the throw (the coyote window is the timing's grace too). Anywhere else
            it is nothing - standing, vel.y is 0, and walking off an edge it is falling.
        */
        float carry = (vel.y > 0.0f) ? vel.y : 0.0f;
        vel.y = ARCHER_JUMP_SPEED + carry;
        if (vel.y > SPRING_MAX_LAUNCH){
            vel.y = SPRING_MAX_LAUNCH;
        }
        launch_lift = vel.y - ARCHER_JUMP_SPEED;
        buffer_ticks = 0;
        coyote_ticks = 0;
        f_on_ground = false;
        events.f_jumped = true;
    }

    /*
        THE SWING: an Up press in the first ticks after a spring plant lets go of her - thrown or
        flung - adds a share of the throw, full at first and fading out over SPRING_SWING_WINDOW.
        A press, not a hold, so Up held since before the release does nothing: the swing is timed.
        Once a flight, and into launch_lift so the jump cut leaves it alone.
    */
    bool f_up_press = in.aim_axis >= SPRING_PUMP_AIM && prev_aim_axis < SPRING_PUMP_AIM;
    if (spring_air_ticks >= 0 && !f_on_ground){
        spring_air_ticks++;
        if (f_up_press && !f_swung && vel.y > 0.0f && spring_air_ticks <= SPRING_SWING_WINDOW){
            float fade = 1.0f;
            if (spring_air_ticks > SPRING_SWING_FULL){
                fade = 1.0f - (float)(spring_air_ticks - SPRING_SWING_FULL) /
                              (float)(SPRING_SWING_WINDOW - SPRING_SWING_FULL + 1);
            }
            float was = vel.y;
            vel.y += SPRING_SWING_GAIN * launch_lift * fade;
            if (vel.y > SPRING_MAX_LAUNCH){
                vel.y = SPRING_MAX_LAUNCH;
            }
            launch_lift += vel.y - was;
            f_swung = true;
            events.f_swung = true;
            events.swing_speed = vel.y - was;
        }
    }

    /*
        The variable-height cut, as a CLAMP rather than a multiply.

        The obvious spelling - vel.y *= ARCHER_JUMP_CUT while the key is up - runs every tick the
        key stays up, so the rise does not get cut, it gets annihilated inside three ticks. A clamp
        is idempotent: the first tick after release brings the climb down to its capped value and
        every tick after that finds it already there.

        It cuts the JUMP and never a spring plant's throw: launch_lift goes on top of the cap, and
        riding one on its way up is not a jump at all.
    */
    if (vel.y > 0.0f && !in.f_jump_down && !(f_on_ground && spring_on >= 0)){
        float capped = ARCHER_JUMP_SPEED * ARCHER_JUMP_CUT + launch_lift;
        if (vel.y > capped){
            vel.y = capped;
        }
    }

    //--- Gravity --------------------------------------------------------------------------------
    float gravity = ARCHER_GRAVITY * ((vel.y > 0.0f) ? 1.0f : ARCHER_FALL_GRAVITY_MUL);
    vel.y -= gravity * ARCHER_DT;
    if (vel.y < -ARCHER_MAX_FALL_SPEED){
        vel.y = -ARCHER_MAX_FALL_SPEED;
    }

    //The stomp's count: the aim held down, falling, up to the tick she touches down. Let go, or
    //start rising, and it starts again - only the end of the fall counts.
    if (!f_on_ground && vel.y < 0.0f && in.aim_axis <= -SPRING_PUMP_AIM){
        if (stomp_ticks < SPRING_STOMP_TICKS){
            stomp_ticks++;
        }
    }else{
        stomp_ticks = 0;
    }

    //--- Move -----------------------------------------------------------------------------------
    bool f_was_on_ground = f_on_ground;
    float impact_speed = vel.y;     //captured because MoveAndCollide zeroes it on contact

    bool f_hit_floor = false;
    bool f_hit_ceiling = false;
    bool f_hit_wall = false;
    int riding = spring_on;
    MoveAndCollide(vel * ARCHER_DT,in.f_down_held,events,f_hit_floor,f_hit_ceiling,f_hit_wall);

    f_on_ground = f_hit_floor;
    if (f_hit_floor && !f_was_on_ground){
        events.f_landed = true;
        events.land_speed = (impact_speed < 0.0f) ? -impact_speed : impact_speed;
    }
    //Off a spring plant this tick - a jump, a throw, or walking off it - starts the swing's clock.
    if (f_riding && !f_on_ground){
        spring_left = riding;
        spring_air_ticks = 0;
        f_swung = false;
    }else if (f_on_ground){
        spring_left = -1;
        spring_air_ticks = -1;
    }
    if (f_hit_ceiling){
        events.f_bumped_head = true;
    }

    //--- Grace timers ---------------------------------------------------------------------------
    //None from a branch: the grace is a jump from where she just was, and there is no jump there.
    if (f_on_ground){
        coyote_ticks = (branch_on >= 0) ? 0 : ARCHER_COYOTE_TICKS;
    }else if (coyote_ticks > 0){
        coyote_ticks--;
    }
    if (buffer_ticks > 0){
        buffer_ticks--;
    }

    //Her balance, on a branch. Going over hangs her from it, which ends the tick as a catch does.
    TickBalance(in,events.f_landed ? events.land_speed : 0.0f,events);
    if (mode == MODE_HANG){
        return;
    }

    /*
        The ledge probe.

        After the move, so it sees where the archer actually ended up - in particular it sees the
        archer pressed flat against the wall face, which MoveAndCollide has just done and which is
        exactly the position the reach test wants to measure from.
    */
    /*
        The rope, before the ledge. Both are "catch something you are flying past", and a player
        who presses action at a rope means the rope - but a rope hanging beside a wall would
        otherwise be beaten to it by the automatic ledge grab, which needs no key at all.
    */
    if (in.f_action_pressed && mode != MODE_HANG && mode != MODE_CLIMB){
        float grip = 0.0f;
        int rope = FindRopePoint(&grip);
        if (rope >= 0){
            mode = MODE_ROPE;
            rope_id = rope;
            rope_ticks = 0;
            rope_s = grip;
            rope_climb = 0;
            rope_climbed = 0.0f;
            vel = v2(vel.x,vel.y);      //kept: the swing starts with the speed you arrived at
            events.f_grabbed_rope = true;
            events.grabbed_rope_id = rope;
            //Both hands on the rope.
            bow_mode = BOW_IDLE;
            draw_ticks = 0;
            return;
        }
    }

    if (!f_on_ground){
        float side = 0.0f;
        int block = FindGrabbableLedge(side);
        //Holding AWAY from the lip is how the player says they meant to miss it. The test is on
        //the input rather than on `facing`, because facing only changes when a direction is held -
        //so a player who let go of everything mid-jump would otherwise still be "facing" the wall.
        bool f_holding_away = (side < 0.0f && in.move_axis < -0.5f) ||
                              (side > 0.0f && in.move_axis > 0.5f);
        if (block >= 0 && !f_holding_away){
            EnterHang(block,side,events);
            return;
        }
        //A branch, the same way - unless Down is held, which is how a drop through one says so.
        int branch = FindCatchableBranch();
        if (branch >= 0 && !in.f_down_held){
            EnterBranchHang(branch,events);
            return;
        }
    }

    mode = f_on_ground ? MODE_GROUND : MODE_AIR;

    //Fell off the world. Restarting outright rather than dying, because there is nothing to die
    //of yet and a prototype that makes you relaunch it is a prototype nobody plays with.
    if (pos.y < -40.0f){
        pos = StartPosition();
        vel = v2(0.0f,0.0f);
    }
}

/*
    Moves the body box and stops it against the level.

    Axis-separated - all of x, resolved, then all of y - which is the standard answer for a box
    platformer and is what makes running into a wall while falling behave instead of catching on
    the corner. Sub-stepped so that a fast fall cannot pass through a thin platform: at terminal
    velocity the archer covers 0.57 units in a tick, which is wider than the one-way platform in
    this level is thick.
*/
void Stage::MoveAndCollide(const v2& delta, bool f_down_held, StageEvents& events,
                           bool& out_hit_floor, bool& out_hit_ceiling, bool& out_hit_wall){
    out_hit_floor = false;
    out_hit_ceiling = false;
    out_hit_wall = false;

    float span = (delta.x < 0.0f ? -delta.x : delta.x);
    float span_y = (delta.y < 0.0f ? -delta.y : delta.y);
    if (span_y > span){
        span = span_y;
    }
    //Quarter of the body's narrow axis. Small enough that nothing in this level can be stepped
    //over, large enough that an ordinary tick is a single pass.
    int steps = 1 + (int)(span / (ARCHER_HALF_W * 0.5f));
    v2 step = delta * (1.0f / (float)steps);
    //The box's missing top while kneeling. Everything below places the box by its FEET or, against
    //a ceiling, by its top - which is ARCHER_HALF_H - head_drop above pos.
    float head_drop = HeadDrop();
    //For the spring plants, which are resolved once for the whole move, and for SPRING_STEP_UP.
    v2 from = pos;
    //Off a spring plant, or up a ramp into the block at its top - see SPRING_STEP_UP.
    bool f_may_step_up = f_on_ground && (spring_on >= 0 || ramp_on >= 0 || bridge_on >= 0);
    //Off a bridge, higher: her own weight pulls the last plank down under her at an anchor.
    const float step_up = (f_on_ground && bridge_on >= 0) ? BRIDGE_STEP_UP : SPRING_STEP_UP;
    bool f_was_grounded = f_on_ground;

    for (int s = 0; s < steps; s++){
        //--- X ----------------------------------------------------------------------------------
        if (step.x != 0.0f){
            pos.x += step.x;
            for (size_t i = 0; i < blocks.size(); i++){
                const StageBlock& b = blocks[i];
                //One-way platforms never stop horizontal motion - that is the whole of what
                //one-way means, and forgetting it produces a platform you can walk into the side
                //of in mid-air.
                if (!b.f_alive || b.kind == BLOCK_PLATFORM){
                    continue;
                }
                if (!BoxOverlapsBlock(pos.x,pos.y,head_drop,b)){
                    continue;
                }
                //Off a spring plant, a low enough face is a step up rather than a wall - if there
                //is room to stand on top of it.
                float rise = b.Top() - (pos.y - ARCHER_HALF_H);
                if (f_may_step_up && rise > 0.0f && rise <= step_up){
                    float up_y = b.Top() + ARCHER_HALF_H + STAGE_EPS;
                    bool f_room = true;
                    for (size_t j = 0; j < blocks.size() && f_room; j++){
                        const StageBlock& o = blocks[j];
                        if (j != i && o.f_alive && o.kind != BLOCK_PLATFORM &&
                            BoxOverlapsBlock(pos.x,up_y,head_drop,o)){
                            f_room = false;
                        }
                    }
                    if (f_room){
                        pos.y = up_y;
                        continue;
                    }
                }
                pos.x = (step.x > 0.0f) ? (b.Left() - ARCHER_HALF_W - STAGE_EPS)
                                        : (b.Right() + ARCHER_HALF_W + STAGE_EPS);
                vel.x = 0.0f;
                out_hit_wall = true;
            }

        }

        /*
            And the props, which stop the archer exactly as the level does.

            OUTSIDE the `step.x != 0` guard, unlike the level, and that is the whole reason this
            block is separate rather than sitting with the walls. A wall cannot come to you; a
            crate can. An archer standing still while a shoved crate rebounds into them does no
            horizontal movement at all, so a resolution that only ran when step.x was non-zero
            skipped this entirely - and then the Y pass, which always runs because gravity always
            runs, found the overlap and resolved it the only way it knows: by standing the archer
            on top. Measured as the archer riding up a stack of crates without ever jumping,
            0.90 -> 1.70 -> 2.50.

            The order still matters: the LEVEL is resolved first, so an archer shoving a crate into
            a wall ends up stopped by the crate rather than swapped through it.
        */
        {
            for (size_t i = 0; i < obstacles.size(); i++){
                const StageObstacle& o = obstacles[i];
                if (!ObstacleStopsBox(pos.x,pos.y,head_drop,o)){
                    continue;
                }
                /*
                    SHORTEST WAY OUT, always - the side the archer is already nearer to.

                    The obvious rule is "put them back on the side they came from", and it is
                    wrong in a way that only shows up once props can move. An archer standing
                    mostly PAST a crate, with a sliver of overlap behind them, is still moving
                    forward - so "the side they came from" is the far side, and resolving to it
                    teleports them backwards straight through the crate. Measured: x -5.93 became
                    -7.38 in one tick, and the kick that followed connected with something that
                    was, a moment earlier, behind them.

                    Least penetration cannot do that. In the ordinary case - walking into a crate,
                    a few millimetres of overlap - it gives the same answer the naive rule does,
                    because the shallow side IS the side you came from.
                */
                float place_left_x  = o.Left()  - ARCHER_HALF_W - STAGE_EPS;
                float place_right_x = o.Right() + ARCHER_HALF_W + STAGE_EPS;
                float to_left  = pos.x - place_left_x;
                float to_right = pos.x - place_right_x;
                if (to_left < 0.0f){  to_left = -to_left;  }
                if (to_right < 0.0f){ to_right = -to_right; }
                bool f_place_left = (to_left <= to_right);

                float dir = (step.x > 0.0f) ? 1.0f : -1.0f;
                pos.x = f_place_left ? place_left_x : place_right_x;
                out_hit_wall = true;

                //Being shoved by a crate is not pushing it. A push is only reported when the
                //archer was moving INTO the thing - which is to say, when the side they were put
                //back on is the side they were coming from. A crate that rebounds off a wall into
                //a standing archer would otherwise drive itself along.
                if (step.x == 0.0f){
                    continue;
                }
                bool f_moving_into = (step.x > 0.0f) ? f_place_left : !f_place_left;
                if (!f_moving_into){
                    continue;
                }

                /*
                    Being stopped by something pushable IS the push. The speed reported is the one
                    the archer was trying to walk at, capped - so leaning on a crate moves it at
                    walking pace, and the archer then follows it at exactly that pace next tick
                    because the crate is where they are allowed to stand up to.

                    vel.x is NOT zeroed for a pushable one. Zeroing it would make the archer
                    re-accelerate from a standstill every single tick of the push, which comes out
                    as a crate that judders along at a fraction of the intended speed.
                */
                if (o.f_pushable){
                    float want = (vel.x < 0.0f) ? -vel.x : vel.x;
                    if (want > ARCHER_PUSH_SPEED){
                        want = ARCHER_PUSH_SPEED;
                    }
                    if (vel.x > ARCHER_PUSH_SPEED){         vel.x = ARCHER_PUSH_SPEED;  }
                    else if (vel.x < -ARCHER_PUSH_SPEED){   vel.x = -ARCHER_PUSH_SPEED; }
                    StageEvents::StagePush push;
                    push.id = o.id;
                    push.dir = dir;
                    push.speed = want;
                    events.pushes.push_back(push);
                }else{
                    vel.x = 0.0f;
                }
            }
        }

        //--- Y ----------------------------------------------------------------------------------
        if (step.y != 0.0f){
            //Where the feet were before this sub-step, which is what decides whether a one-way
            //platform is underfoot or overhead. Taken from the position, not from the velocity:
            //a platform is passable because you came from below it, not because you are rising.
            float prev_bottom = pos.y - ARCHER_HALF_H;
            pos.y += step.y;

            for (size_t i = 0; i < blocks.size(); i++){
                const StageBlock& b = blocks[i];
                if (!b.f_alive){
                    continue;
                }
                if (b.kind == BLOCK_PLATFORM){
                    //Solid only to something descending onto its top surface from clear above it,
                    //and not at all while Down is held.
                    if (f_down_held || step.y > 0.0f || prev_bottom < b.Top() - STAGE_EPS){
                        continue;
                    }
                }
                if (!BoxOverlapsBlock(pos.x,pos.y,head_drop,b)){
                    continue;
                }
                if (step.y < 0.0f){
                    pos.y = b.Top() + ARCHER_HALF_H + STAGE_EPS;
                    out_hit_floor = true;
                }else{
                    pos.y = b.Bottom() - (ARCHER_HALF_H - head_drop) - STAGE_EPS;
                    out_hit_ceiling = true;
                }
                vel.y = 0.0f;
            }

            /*
                Props vertically too, which is what makes a crate something you can STAND ON. It
                falls out of blocking rather than being a feature that had to be written, and it is
                the reason the crates by the start are stacked two high.

                GUARDED BY WHERE THE FEET WERE, the same rule the one-way platforms use. A prop
                only becomes a floor to someone who was already above it. Without that, any
                overlap arriving from the side gets resolved as a landing - which is the same
                crate-riding bug the X pass above guards against, reached by the other road, and it
                survives every fix to that one because the two passes can disagree about which
                obstacle they are resolving.
            */
            float prev_top = prev_bottom + ARCHER_HALF_H * 2.0f - head_drop;
            for (size_t i = 0; i < obstacles.size(); i++){
                const StageObstacle& o = obstacles[i];
                if (!ObstacleStopsBox(pos.x,pos.y,head_drop,o)){
                    continue;
                }
                if (step.y < 0.0f){
                    if (prev_bottom < o.Top() - STAGE_EPS){
                        continue;       //came at it from the side, not down onto it
                    }
                    pos.y = o.Top() + ARCHER_HALF_H + STAGE_EPS;
                    out_hit_floor = true;
                }else{
                    if (prev_top > o.Bottom() + STAGE_EPS){
                        continue;
                    }
                    pos.y = o.Bottom() - (ARCHER_HALF_H - head_drop) - STAGE_EPS;
                    out_hit_ceiling = true;
                }
                vel.y = 0.0f;
            }
        }
    }

    /*
        The spring plants, once for the whole move rather than per sub-step: each is tested from
        where she started against where the surface WAS to where she ended against where it IS,
        which cannot be stepped through however fast either moves. A block she has just been
        stood on outranks a plant surface below her feet - the shelf, over a leaf bent under it.
    */
    int was_ramp = ramp_on;
    bool f_on_surface = false;
    CollideSurfaces(from,f_down_held,out_hit_floor,f_was_grounded,events,f_on_surface);
    if (f_on_surface){
        out_hit_floor = true;
    }

    /*
        OFF A RAMP'S FOOT ONTO THE FLOOR. The ramp's last sample under her centre leaves her feet a
        hair above the floor it meets, a tick of gravity does not reach it, and she flew one tick
        at the foot of every descent - measured on the gallery's ramps, a landing each at 8, 14, 18
        and 35 degrees. The surfaces' keep-on case, for the blocks a ramp runs out onto: the same
        reach, down to the highest block top under her box.
    */
    if (!out_hit_floor && was_ramp >= 0 && was_ramp < (int)ramps.size() && f_was_grounded &&
        vel.y <= STAGE_EPS){
        float feet = pos.y - ARCHER_HALF_H;
        float reach = fabsf(ramps[was_ramp].Slope()) * (fabsf(pos.x - from.x) + ARCHER_HALF_W) + 0.05f;
        float best = -1e30f;
        for (size_t i = 0; i < blocks.size(); i++){
            const StageBlock& b = blocks[i];
            if (!b.f_alive || b.Right() <= pos.x - ARCHER_HALF_W || b.Left() >= pos.x + ARCHER_HALF_W){
                continue;
            }
            //Down drops her through a one-way platform here as anywhere - and only through one.
            if (f_down_held && b.kind == BLOCK_PLATFORM){
                continue;
            }
            float top = b.Top();
            if (top <= feet + STAGE_EPS && feet - top <= reach && top > best){
                best = top;
            }
        }
        if (best > -1e29f){
            pos.y = best + ARCHER_HALF_H + STAGE_EPS;
            vel.y = 0.0f;
            out_hit_floor = true;
        }
    }
}

//--- The rope ---------------------------------------------------------------------------------

void Stage::ClearRopePoints(){
    rope_points.clear();
}

void Stage::AddRopePoint(float x, float y, int id, float s){
    StageRopePoint p;
    p.x = x;
    p.y = y;
    p.id = id;
    p.s = s;
    rope_points.push_back(p);
}

/*
    A link the archer could catch, measured from the HANDS rather than from the body's centre.

    Same reasoning as the ledge: the thing doing the grabbing is at the top of the body, and
    measuring from the middle makes a rope at head height read as out of reach while one at knee
    height reads as catchable.
*/
int Stage::FindRopePoint(float* out_s) const{
    if (rope_cooldown > 0){
        return -1;
    }
    float hand_y = pos.y + ARCHER_HALF_H * 0.6f;
    float best = ROPE_GRAB_REACH * ROPE_GRAB_REACH;
    int found = -1;
    for (size_t i = 0; i < rope_points.size(); i++){
        float dx = rope_points[i].x - pos.x;
        float dy = rope_points[i].y - hand_y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= best){
            best = d2;
            found = rope_points[i].id;
            if (out_s){
                *out_s = rope_points[i].s;
            }
        }
    }
    return found;
}

/*
    On the rope.

    THE SOLVER IS DRIVING. pos and vel are written back from the swinging body by the app before
    this runs, so everything in here is reading rather than integrating - and the only decision
    left is when to let go.

    Two ways off, and they are deliberately different. ACTION drops you, keeping whatever the swing
    had given you. JUMP does that and adds ROPE_JUMP_BOOST upward, which is what turns a rope from
    a way across a gap into a way to gain height. Both are gated behind ROPE_MIN_HOLD_TICKS,
    because the press that caught the rope is still being held when this first runs.
*/
void Stage::TickRope(const ArcherInput& in, StageEvents& events){
    rope_ticks++;
    rope_pump = ClampF(in.move_axis,-1.0f,1.0f);

    /*
        CLIMBING: the DECISION only - which way, how fast, how far. The app turns rope_s into where
        the joint holds her and the solver moves her there, so she climbs while the rope swings and
        the pendulum really does shorten under her.

        The limits are the span of the points the app offers, which is the span she could have
        caught - so she cannot climb into the top links the app keeps out of reach, and cannot climb
        off the bottom.

        AND A GRIP PAST THE LAST POINT IS A GRIP ON NOTHING. The app offers points only on the part
        of the rope still hanging from its anchor, so a rope cut above her hands, or one offering
        no points at all, means what she is holding has come away - and she lets go, whatever she
        is pressing and however long she has held on. Nothing else in the rules needs to know a
        rope can be cut.
    */
    rope_climb = 0;
    float lo = 0.0f;
    float hi = -1.0f;
    for (size_t i = 0; i < rope_points.size(); i++){
        if (i == 0 || rope_points[i].s < lo){ lo = rope_points[i].s; }
        if (i == 0 || rope_points[i].s > hi){ hi = rope_points[i].s; }
    }
    //rope_s is only ever a point's own s or clamped between them, so any real excess is a cut.
    bool f_holding = !rope_points.empty() && rope_s <= hi + 1e-3f;
    if (f_holding){
        int want = (in.aim_axis > ROPE_CLIMB_DEADZONE) ? 1 : ((in.aim_axis < -ROPE_CLIMB_DEADZONE) ? -1 : 0);
        float next = ClampF(rope_s - (float)want * ROPE_CLIMB_SPEED * ARCHER_DT,lo,hi);
        if (next != rope_s){
            rope_climb = want;
            rope_climbed += rope_s - next;
            rope_s = next;
        }
    }

    //Face the way the swing is going, so the bow points down the arc rather than at the anchor.
    if (vel.x > 1.0f){
        facing = 1.0f;
    }else if (vel.x < -1.0f){
        facing = -1.0f;
    }

    if (f_holding){
        if (rope_ticks < ROPE_MIN_HOLD_TICKS){
            return;
        }
        if (!in.f_jump_pressed && !in.f_action_pressed){
            return;
        }
    }

    mode = MODE_AIR;
    rope_id = -1;
    rope_pump = 0.0f;
    rope_cooldown = ROPE_GRAB_COOLDOWN;
    f_on_ground = false;
    //The swing has just handed the archer a lot of speed, and a jump buffered during it would
    //spend it on a jump off nothing the moment they land.
    buffer_ticks = 0;
    events.f_released_rope = true;
    //Falling with the piece she held is not a jump, whatever was pressed.
    events.f_rope_jump = f_holding && in.f_jump_pressed;
    events.f_rope_lost = !f_holding;
}

//--- The kick -------------------------------------------------------------------------------------

/*
    See THE THREE KICKS in Stage.h. The first row is the defines, so it cannot drift from them; the
    other two were MEASURED 2026-09-26 by MeasureKickClip and checked by eye, frame by frame:

      Kick_Front    a snap front kick. Boot out at tick 22, at y +0.20, 11.0/s at its fastest.

      Kick_Front_2  a stepping PUSH kick: she shuffles in for thirty ticks and drives a straight
                    leg out at hip height. The boot lands at tick 46 of 78, LOWER than Kick_Front's
                    (y 0.00) and FASTER (14.3/s, 1.3x). So its box sits lower by the same 0.20
                    and it shoves harder and flatter - speed 1.3x, lift about half: it drives a
                    crate along the floor rather than punting it.

      Kick_Front_3  a HIGH rising kick: she leans back, the leg swings up past her head (ticks
                    26..33) and drives out at chest height, furthest at tick 36 of 68. So its box is
                    taller, reaching head height, and live from 31 to cover the leg coming up
                    through as well as the drive; and it LIFTS - most of the lift of the three and
                    the least shove. It reaches the top crate of a stack, which the other two do
                    not.

    Every box keeps Kick_Front's reach: all three boots land within 0.12 of each other there
    (1.14, 1.03, 1.02 past her edge), which is less than the difference that would show.
*/
const KickSpec KICK_SPECS[KICK_KIND_COUNT] = {
    //                 ticks        from              to              reach       half_height       y_offset       speed       lift
    { "Kick_Front",    KICK_TICKS,  KICK_ACTIVE_FROM, KICK_ACTIVE_TO, KICK_REACH, KICK_HALF_HEIGHT, KICK_Y_OFFSET, KICK_SPEED, KICK_LIFT },
    { "Kick_Front_2",  78,          44,               48,             KICK_REACH, 0.50f,            -0.45f,        17.0f,      2.5f },
    { "Kick_Front_3",  68,          31,               37,             KICK_REACH, 0.70f,            0.10f,         9.0f,       8.0f },
};

/*
    The box the boot sweeps.

    In front of the archer and reaching the kick's `reach` past the body's leading edge, at the
    height its boot lands. A separate function because three things want it and they must not
    drift apart: the sweep below, the rules test, and the debug draw the app puts on screen while
    tuning.
*/
void Stage::KickBox(float& out_left, float& out_right, float& out_bottom, float& out_top) const{
    const KickSpec& k = Kick();
    float lead = (facing > 0.0f) ? (pos.x + ARCHER_HALF_W) : (pos.x - ARCHER_HALF_W);
    float far_edge = lead + facing * k.reach;
    out_left  = (lead < far_edge) ? lead : far_edge;
    out_right = (lead < far_edge) ? far_edge : lead;
    float centre_y = pos.y + k.y_offset;
    out_bottom = centre_y - k.half_height;
    out_top    = centre_y + k.half_height;
}

/*
    The kick, from press to recovered.

    Runs BEFORE the archer moves, so the box is swept from where the archer was standing when the
    boot went out rather than from wherever they drifted to afterwards. On a fast run those differ
    by a sixth of a unit, which is the difference between connecting with the near brick of a wall
    and connecting with nothing.
*/
void Stage::TickKick(const ArcherInput& in, StageEvents& events){
    if (kick_cooldown > 0){
        kick_cooldown--;
    }

    /*
        WHAT CANNOT KICK, and why each one is on the list.

        Hanging, climbing and the rope are the obvious three - both hands and both feet are already
        holding on to something.

        THE AIR is the fourth and it used to be allowed. It was wrong on both counts. A kick roots
        her for KICK_TICKS, which is 1.1 seconds - longer than an entire jump - so a flying kick
        was really a decision to hang motionless in mid-air until the ground arrived; and
        Kick_Front is a GROUNDED clip, a wind-up and a plant and a recovery, all of which need a
        floor to push against and none of which read as anything but a bug when they float. A kick
        is something you do with your weight on the ground.

        f_on_ground is last tick's, because TickKick runs before TickArcher. One tick of lag on a
        gate costs nothing and keeps the ordering note at the top of Stage::Tick true.

        And KNEELING: the kick is a standing move, and a knee on the floor is not a plant. Nor is a
        branch she is keeping her balance on: one foot in the air there is a fall.
    */
    bool f_busy = (mode == MODE_HANG || mode == MODE_CLIMB || mode == MODE_ROPE || mode == MODE_KNEEL ||
                   !f_on_ground || branch_on >= 0);

    if (kick_ticks == 0){
        if (in.f_kick_pressed && kick_cooldown == 0 && !f_busy){
            kick_ticks = 1;
            //Which kick is chosen here and nowhere else - see THE THREE KICKS in Stage.h.
            kick_kind = KICK_FRONT;
            if (in.aim_axis <= -KICK_SELECT_AIM){
                kick_kind = KICK_FRONT_2;
            }else if (in.aim_axis >= KICK_SELECT_AIM){
                kick_kind = KICK_FRONT_3;
            }
            events.f_kick_started = true;
        }
        return;
    }

    /*
        AND THE MOVE ENDS WHERE THE GROUND DOES.

        The plant is friction rather than a freeze - see f_planted in TickArcher - so a kick thrown
        at a full run still carries about a unit of slide, which is easily enough to go over a lip.
        Without this the gate above would only cover kicks that STARTED in the air and she would
        still finish one floating, which is the same picture for the same second and a half.

        The cooldown applies, so landing does not hand back a free kick as a reward for falling.
    */
    if (!f_on_ground){
        kick_ticks = 0;
        kick_cooldown = KICK_COOLDOWN;
        return;
    }

    const KickSpec& spec = Kick();
    kick_ticks++;
    if (kick_ticks > spec.ticks){
        kick_ticks = 0;
        kick_cooldown = KICK_COOLDOWN;
        return;
    }
    //Wind-up and recovery: the move is running but the boot is not live.
    if (kick_ticks < spec.active_from || kick_ticks > spec.active_to){
        return;
    }

    float left, right, bottom, top;
    KickBox(left,right,bottom,top);

    /*
        BREAKABLE LEVEL GEOMETRY first. Clearing f_alive is all the rules have to do - every sweep
        in this file already skips a dead block - but the app has a collider to take out of the
        world and a cloud of debris to make, so the index goes out as an event rather than the
        block simply vanishing.
    */
    for (size_t i = 0; i < blocks.size(); i++){
        StageBlock& b = blocks[i];
        if (!b.f_alive || b.kind != BLOCK_BREAKABLE){
            continue;
        }
        if (right <= b.Left() || left >= b.Right() || top <= b.Bottom() || bottom >= b.Top()){
            continue;
        }
        b.f_alive = false;
        events.broken_blocks.push_back((int)i);
        events.f_kick_connected = true;
    }

    //And the props. Reported with WHERE the boot landed, because a wall of bricks wants to burst
    //away from the impact rather than all in the same direction.
    for (size_t i = 0; i < obstacles.size(); i++){
        const StageObstacle& o = obstacles[i];
        if (right <= o.Left() || left >= o.Right() || top <= o.Bottom() || bottom >= o.Top()){
            continue;
        }
        StageEvents::StageKick kick;
        kick.id = o.id;
        kick.kind = kick_kind;
        kick.dir = facing;
        kick.x = (left + right) * 0.5f;
        kick.y = (bottom + top) * 0.5f;
        events.kicks.push_back(kick);
        events.f_kick_connected = true;
    }

    //One connect per kick. Without this the boot stays live for the whole window and hits the same
    //crate five times, which is five impulses and a crate that leaves the level.
    if (events.f_kick_connected){
        kick_ticks = spec.active_to + 1;
    }
}

//--- Kneeling -----------------------------------------------------------------------------------

float Stage::HeadDrop() const{
    //Binary rather than following the clip down: the box is shorter from the first tick down to
    //the first tick up, and standing up is only started once the full box is known to fit.
    if (mode != MODE_KNEEL || kneel_phase == KNEEL_RISING){
        return 0.0f;
    }
    return 2.0f * (ARCHER_HALF_H - KNEEL_HALF_H);
}

float Stage::BodyHeight() const{
    return 2.0f * ARCHER_HALF_H - HeadDrop();
}

bool Stage::CanStandUp() const{
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        //A one-way platform overhead is passable from below, so it never stops her standing.
        if (!b.f_alive || b.kind == BLOCK_PLATFORM){
            continue;
        }
        if (BoxOverlapsBlock(pos.x,pos.y,0.0f,b)){
            return false;
        }
    }
    for (size_t i = 0; i < obstacles.size(); i++){
        if (ObstacleStopsBox(pos.x,pos.y,0.0f,obstacles[i])){
            return false;
        }
    }
    return true;
}

float Stage::KneelAmount() const{
    if (mode != MODE_KNEEL){
        return 0.0f;
    }
    //Smoothstepped: both clips ease in and out of the move, so the anchor should too.
    float t = 1.0f;
    if (kneel_phase == KNEEL_LOWERING){
        t = ClampF((float)kneel_ticks / (float)KNEEL_DOWN_TICKS,0.0f,1.0f);
    }else if (kneel_phase == KNEEL_RISING){
        t = 1.0f - ClampF((float)kneel_ticks / (float)KNEEL_UP_TICKS,0.0f,1.0f);
    }
    return t * t * (3.0f - 2.0f * t);
}

/*
    MODE_KNEEL, from the first tick down to the last tick up.

    NO RUN, NO JUMP, NO TURN: the move axis only brakes her - ARCHER_RUN_FRICTION, as if the stick
    had been let go, so a kneel pressed at a run skids to a stop in about five ticks rather than
    freezing on the spot - and a jump press is dropped outright, buffer included, so it does not
    fire the moment she is back up. Facing is left alone: a kneeling archer turning on the spot
    would need a clip, and standing up is how she turns round.

    GRAVITY STILL RUNS, because the floor is not guaranteed - a crate she knelt on can be knocked
    out from under her. Losing it ends the kneel and hands her to the air, standing box and all.
*/
void Stage::TickKneel(const ArcherInput& in, StageEvents& events){
    if (in.f_kneel_pressed && kneel_phase == KNEEL_HELD){
        if (CanStandUp()){
            kneel_phase = KNEEL_RISING;
            kneel_ticks = 0;
        }else{
            events.f_stand_blocked = true;
        }
    }

    if (kneel_phase == KNEEL_LOWERING){
        kneel_ticks++;
        if (kneel_ticks >= KNEEL_DOWN_TICKS){
            kneel_phase = KNEEL_HELD;
            kneel_ticks = 0;
        }
    }else if (kneel_phase == KNEEL_RISING){
        kneel_ticks++;
        if (kneel_ticks >= KNEEL_UP_TICKS){
            mode = MODE_GROUND;
            kneel_phase = KNEEL_LOWERING;
            kneel_ticks = 0;
            events.f_stood = true;
        }
    }

    buffer_ticks = 0;
    vel.x = MoveToward(vel.x,0.0f,ARCHER_RUN_FRICTION * ARCHER_DT);
    vel.y -= ARCHER_GRAVITY * ARCHER_FALL_GRAVITY_MUL * ARCHER_DT;
    if (vel.y < -ARCHER_MAX_FALL_SPEED){
        vel.y = -ARCHER_MAX_FALL_SPEED;
    }
    bool f_hit_floor = false;
    bool f_hit_ceiling = false;
    bool f_hit_wall = false;
    MoveAndCollide(vel * ARCHER_DT,false,events,f_hit_floor,f_hit_ceiling,f_hit_wall);
    f_on_ground = f_hit_floor;
    if (f_on_ground){
        coyote_ticks = ARCHER_COYOTE_TICKS;
        return;
    }
    mode = MODE_AIR;
    kneel_phase = KNEEL_LOWERING;
    kneel_ticks = 0;
}

//--- Getting up ---------------------------------------------------------------------------------

/*
    Starts the level entry from wherever she is.

    DROPPED ONTO THE FLOOR FIRST, in one sweep, because the start position is deliberately a little
    above the ground (see Reset) and the clip's first frame is her lying ON it: left to gravity she
    would fall the last few centimetres lying flat, which reads as being dropped into the level.
    Ten units is well past any start this file has; if there is no floor within it she simply falls
    the rest, still getting up.

    Everything a restart might have left mid-flight is cleared with it - a draw, a kick, a kneel,
    the jump buffer - so nothing resumes the moment the controls come back.
*/
void Stage::StartGetUp(){
    mode = MODE_GETUP;
    getup_ticks = 0;
    vel = v2(0.0f,0.0f);
    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    kick_ticks = 0;
    kneel_phase = KNEEL_LOWERING;
    kneel_ticks = 0;
    buffer_ticks = 0;
    coyote_ticks = 0;

    StageEvents scratch;
    bool f_hit_floor = false;
    bool f_hit_ceiling = false;
    bool f_hit_wall = false;
    MoveAndCollide(v2(0.0f,-10.0f),false,scratch,f_hit_floor,f_hit_ceiling,f_hit_wall);
    vel = v2(0.0f,0.0f);
    f_on_ground = f_hit_floor;
}

/*
    MODE_GETUP. No input reaches here at all - Stage::Tick has already swapped it for an empty one.

    The clock runs to GETUP_TICKS and then she is simply standing: MODE_GROUND if there is floor
    under her, MODE_AIR if not. GRAVITY STILL RUNS, the kneel's reason - nothing guarantees the
    floor, and a crate knocked out from under a lying archer should not leave her lying on air.
    Unlike the kneel, losing the floor does NOT end it: the level entry plays to its end either way.
*/
void Stage::TickGetUp(StageEvents& events){
    getup_ticks++;

    vel.x = MoveToward(vel.x,0.0f,ARCHER_RUN_FRICTION * ARCHER_DT);
    vel.y -= ARCHER_GRAVITY * ARCHER_FALL_GRAVITY_MUL * ARCHER_DT;
    if (vel.y < -ARCHER_MAX_FALL_SPEED){
        vel.y = -ARCHER_MAX_FALL_SPEED;
    }
    bool f_hit_floor = false;
    bool f_hit_ceiling = false;
    bool f_hit_wall = false;
    MoveAndCollide(vel * ARCHER_DT,false,events,f_hit_floor,f_hit_ceiling,f_hit_wall);
    f_on_ground = f_hit_floor;
    if (f_on_ground){
        vel.y = 0.0f;
        coyote_ticks = ARCHER_COYOTE_TICKS;
    }

    if (getup_ticks >= GETUP_TICKS){
        mode = f_on_ground ? MODE_GROUND : MODE_AIR;
        getup_ticks = 0;
        events.f_got_up = true;
    }
}

//--- Hanging and climbing -----------------------------------------------------------------------

/*
    A lip the archer could catch right now, or -1.

    Only BLOCK_LEDGE is grabbable, and that is a level-design decision rather than a shortcut: it
    means "can I hang here" is a property the level states, not one the player has to discover by
    trying every wall in the game. A SOLID block with the same shape is deliberately not catchable.

    Const and free of side effects, so the app can call it to draw a hint without the act of asking
    changing anything.
*/
int Stage::FindGrabbableLedge(float& out_side) const{
    if (grab_cooldown > 0){
        return -1;
    }
    //Falling only - see the long note on the constants in Stage.h. This single condition is what
    //stops a jump you could have made being stolen by a grab on the way up.
    if (vel.y > LEDGE_GRAB_MAX_RISE){
        return -1;
    }

    float hand_y = pos.y + ARCHER_HALF_H;
    float left_edge = pos.x - ARCHER_HALF_W;
    float right_edge = pos.x + ARCHER_HALF_W;

    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (!b.f_alive || b.kind != BLOCK_LEDGE){
            continue;
        }
        //Is the lip at hand height?
        if (b.Top() > hand_y + LEDGE_GRAB_BAND_UP){
            continue;
        }
        if (b.Top() < hand_y - LEDGE_GRAB_BAND_DOWN){
            continue;
        }

        /*
            Catching the LEFT corner: the archer is to the left of the block, their leading edge is
            within reach of its left face, and they are facing it.

            The facing test is what makes a grab something the player aimed at. Without it an
            archer falling down a wall with their back to it catches every lip on the way, which
            looks like the character being yanked about by the level.
        */
        if (facing > 0.0f && pos.x < b.x){
            if (right_edge >= b.Left() - LEDGE_GRAB_REACH && right_edge <= b.Left() + ARCHER_HALF_W){
                out_side = -1.0f;
                return (int)i;
            }
        }
        if (facing < 0.0f && pos.x > b.x){
            if (left_edge <= b.Right() + LEDGE_GRAB_REACH && left_edge >= b.Right() - ARCHER_HALF_W){
                out_side = 1.0f;
                return (int)i;
            }
        }
    }
    return -1;
}

/*
    Catch it: snap to the lip and stop dead.

    The snap is the point. A hang that keeps whatever sub-tick position the fall happened to end on
    leaves the archer a few centimetres off the wall or a few below the lip, differently every time
    - and then the climb that follows starts from somewhere slightly different every time. Hanging
    is a POSE, so it gets one exact position, and everything downstream can rely on it.
*/
void Stage::EnterHang(int block, float side, StageEvents& events){
    if (block < 0 || block >= (int)blocks.size()){
        return;
    }
    const StageBlock& b = blocks[block];

    mode = MODE_HANG;
    hang_block = block;
    hang_side = side;
    //Body flat against the face, fingers on the lip - see LEDGE_HANG_DROP.
    pos.x = (side < 0.0f) ? (b.Left() - ARCHER_HALF_W - STAGE_EPS)
                          : (b.Right() + ARCHER_HALF_W + STAGE_EPS);
    pos.y = b.Top() - ARCHER_HALF_H - LEDGE_HANG_DROP;
    vel = v2(0.0f,0.0f);
    f_on_ground = false;
    coyote_ticks = 0;
    //A jump pressed just before the catch must not fire on the tick after it - the player was
    //asking to jump at the wall, not to let go of it the instant they arrived.
    buffer_ticks = 0;
    facing = (side < 0.0f) ? 1.0f : -1.0f;

    //Both hands are on the rock. Cancelling rather than letting the draw continue invisibly, so
    //the bow cannot be loosed by a release that arrives while hanging.
    bow_mode = BOW_IDLE;
    draw_ticks = 0;

    events.f_grabbed_ledge = true;
}

void Stage::ReleaseHang(StageEvents& events){
    mode = MODE_AIR;
    hang_block = -1;
    hang_branch = -1;
    vel = v2(0.0f,0.0f);
    //Without this, the drop re-grabs the same lip on the next tick and the archer is welded to it.
    grab_cooldown = LEDGE_RELEASE_COOLDOWN;
    events.f_released_ledge = true;
}

void Stage::TickHang(const ArcherInput& in, StageEvents& events){
    if (hang_branch >= 0){
        TickBranchHang(in,events);
        return;
    }
    //The ledge could have been removed underneath us - a BREAKABLE one will be, once the
    //kick-and-break slice can destroy the thing you are hanging from.
    if (hang_block < 0 || hang_block >= (int)blocks.size() || !blocks[hang_block].f_alive){
        ReleaseHang(events);
        return;
    }
    const StageBlock& b = blocks[hang_block];

    //Up, or jump, pulls up over the lip.
    if (in.f_jump_pressed){
        mode = MODE_CLIMB;
        climb_ticks = LEDGE_CLIMB_TICKS;
        climb_from = pos;
        //Onto the top surface, LEDGE_CLIMB_INSET past the edge she came up over - or as far as a
        //narrow block allows, and never less than just inside it.
        float inset = ClampF(LEDGE_CLIMB_INSET,ARCHER_HALF_W + STAGE_EPS,
                             b.hw * 2.0f - ARCHER_HALF_W - STAGE_EPS);
        if (inset < ARCHER_HALF_W + STAGE_EPS){
            inset = ARCHER_HALF_W + STAGE_EPS;
        }
        climb_to = v2((hang_side < 0.0f) ? (b.Left() + inset) : (b.Right() - inset),
                      b.Top() + ARCHER_HALF_H + STAGE_EPS);
        return;
    }

    //Down, or holding away from the wall, lets go.
    bool f_holding_away = (hang_side < 0.0f && in.move_axis < -0.5f) ||
                          (hang_side > 0.0f && in.move_axis > 0.5f);
    if (in.f_down_held || f_holding_away){
        ReleaseHang(events);
        return;
    }

    //Otherwise hang: no gravity, no drift, no input. Held exactly where EnterHang put us, which is
    //re-asserted rather than assumed so that nothing else can nudge the pose.
    pos.x = (hang_side < 0.0f) ? (b.Left() - ARCHER_HALF_W - STAGE_EPS)
                               : (b.Right() + ARCHER_HALF_W + STAGE_EPS);
    pos.y = b.Top() - ARCHER_HALF_H - LEDGE_HANG_DROP;
    vel = v2(0.0f,0.0f);
}

/*
    The path, measured off `Climb` from the grab (1.033s) to its end - see LEDGE_CLIMB_TICKS, and
    LEDGE_CLIMB_REGRIP for why the rise is not the clip's alone. ApplicationArcher::
    MeasureLedgeClimb prints these again at every start if they have drifted.
*/
const float LEDGE_CLIMB_UP[LEDGE_CLIMB_PATH_SAMPLES] = {
    0.000f, 0.144f, 0.281f, 0.398f, 0.513f, 0.604f, 0.653f, 0.672f, 0.721f,
    0.783f, 0.828f, 0.858f, 0.913f, 0.950f, 0.975f, 0.991f, 1.000f };
const float LEDGE_CLIMB_ACROSS[LEDGE_CLIMB_PATH_SAMPLES] = {
    0.000f, 0.170f, 0.304f, 0.385f, 0.403f, 0.422f, 0.464f, 0.486f, 0.472f,
    0.448f, 0.455f, 0.520f, 0.617f, 0.728f, 0.843f, 0.943f, 1.000f };

static float SampleClimbPath(const float* path, float t){
    float f = ClampF(t,0.0f,1.0f) * (float)(LEDGE_CLIMB_PATH_SAMPLES - 1);
    int i = (int)f;
    if (i >= LEDGE_CLIMB_PATH_SAMPLES - 1){
        return path[LEDGE_CLIMB_PATH_SAMPLES - 1];
    }
    return path[i] + (path[i + 1] - path[i]) * (f - (float)i);
}

/*
    Pulling up over the lip.

    UNINTERRUPTIBLE, and along the clip's own path, so the hands the pose has on the lip stay
    there. The body box goes INTO the wall on the way - the hips close on it by 0.4 while she is
    still below the lip - which nothing minds: nothing collides with her during the move, and the
    box is never drawn. The straight up-then-across lerp this replaced kept the box out of the
    corner, which mattered only while the box was what you saw.
*/
void Stage::TickClimb(const ArcherInput& in, StageEvents& events){
    (void)in;
    climb_ticks--;
    if (climb_ticks <= 0){
        pos = climb_to;
        vel = v2(0.0f,0.0f);
        mode = MODE_GROUND;
        f_on_ground = true;
        hang_block = -1;
        //Up onto a branch: standing on it, and balancing again from upright.
        if (hang_branch >= 0){
            branch_on = hang_branch;
            hang_branch = -1;
            lean = 0.0f;
            lean_rate = 0.0f;
            balance_ticks = 0;
        }
        //Landing on top of the thing you just climbed is not a fresh chance to grab it.
        grab_cooldown = LEDGE_RELEASE_COOLDOWN;
        events.f_climbed = true;
        return;
    }

    float t = 1.0f - ((float)climb_ticks / (float)LEDGE_CLIMB_TICKS);
    float up = SampleClimbPath(LEDGE_CLIMB_UP,t);
    float across = SampleClimbPath(LEDGE_CLIMB_ACROSS,t);
    pos.x = climb_from.x + (climb_to.x - climb_from.x) * across;
    pos.y = climb_from.y + (climb_to.y - climb_from.y) * up;
    vel = v2(0.0f,0.0f);
}

//--- The bow ------------------------------------------------------------------------------------

float Stage::DrawPower() const{
    //Over the PULL only - from the nock to full draw - which is what the string does on screen.
    float t = (float)(draw_ticks - BOW_NOCK_TICKS) / (float)(BOW_DRAW_TICKS - BOW_NOCK_TICKS);
    return BOW_MIN_POWER + (1.0f - BOW_MIN_POWER) * ClampF(t,0.0f,1.0f);
}

/*
    The sway: three sines, weighted 0.6 / 0.3 / 0.1 so the sum never passes the amplitude. Periods
    1.9s and 3.1s make a slow drift that never quite repeats; 0.7s adds a small tremor on top.

    Each draw enters the drift at its own offset - the golden ratio's fractional steps spread
    successive draws evenly over 30 seconds of it, so no two neighbouring draws start alike - and
    the smoothstepped ramp brings it in from exactly zero at the nock.
*/
float Stage::AimSwayDeg() const{
    if (!IsNocked()){
        return 0.0f;
    }
    const float TWO_PI = 6.28318530718f;
    float spread = (float)draws_started * 0.6180339887f;
    float t = (spread - floorf(spread)) * 30.0f + (float)sway_ticks * ARCHER_DT;
    float ramp = ClampF((float)sway_ticks / (float)AIM_SWAY_RAMP_TICKS,0.0f,1.0f);
    ramp = ramp * ramp * (3.0f - 2.0f * ramp);
    float k = KneelAmount();
    float amplitude = AIM_SWAY_STAND_DEG + (AIM_SWAY_KNEEL_DEG - AIM_SWAY_STAND_DEG) * k;
    return amplitude * ramp * (0.6f * sinf(TWO_PI * t / 1.9f) +
                               0.3f * sinf(TWO_PI * t / 3.1f) +
                               0.1f * sinf(TWO_PI * t / 0.7f));
}

/*
    The sideways half: the same three weights on periods of its own - 2.3s, 3.7s and 0.9s, none a
    simple ratio of the up-and-down half's, so the tip traces a figure that never closes rather than
    a line or an ellipse. The same entry point and the same ramp, so it too is exactly zero on the
    nock and the same in a replay.

    Then SQUEEZED into the circle: by sqrt(1 - (up/amplitude)^2), so up^2 + side^2 never passes
    amplitude^2 and the up-and-down half is left exactly as it was (see AIM_SWAY_STAND_DEG).
*/
float Stage::AimSwaySideDeg() const{
    if (!IsNocked()){
        return 0.0f;
    }
    const float TWO_PI = 6.28318530718f;
    float spread = (float)draws_started * 0.6180339887f;
    float t = (spread - floorf(spread)) * 30.0f + (float)sway_ticks * ARCHER_DT;
    float ramp = ClampF((float)sway_ticks / (float)AIM_SWAY_RAMP_TICKS,0.0f,1.0f);
    ramp = ramp * ramp * (3.0f - 2.0f * ramp);
    float k = KneelAmount();
    float amplitude = AIM_SWAY_STAND_DEG + (AIM_SWAY_KNEEL_DEG - AIM_SWAY_STAND_DEG) * k;
    if (amplitude <= 0.0f){
        return 0.0f;
    }
    float side = amplitude * ramp * (0.6f * sinf(TWO_PI * t / 2.3f) +
                                     0.3f * sinf(TWO_PI * t / 3.7f) +
                                     0.1f * sinf(TWO_PI * t / 0.9f));
    float up = AimSwayDeg() / amplitude;
    return side * sqrtf(fmaxf(0.0f,1.0f - up * up));
}

v3 Stage::Forward() const{
    if (IsPlaneLocked()){
        return v3(facing,0.0f,0.0f);
    }
    float h = heading_deg * STAGE_DEG2RAD;
    return v3(sinf(h),0.0f,cosf(h));
}

v3 Stage::AimDirection() const{
    float a = ShotAimDeg() * STAGE_DEG2RAD;
    //Mirrored through facing, so +30 degrees means "thirty up from straight ahead" whichever way
    //the archer is looking. A world-space angle would mean the same key tilted the wrong way
    //half the time.
    //Locked, the sideways sway is dropped - flattened into the plane - and this is the 2D aim
    //exactly as it was, down to the bit, which is what keeps every shot on a locked plane the
    //shot it was before the aim had a third dimension.
    if (IsPlaneLocked()){
        return v3(cosf(a) * facing,sinf(a),0.0f);
    }
    /*
        Off the plane: tilt `a` up from level ahead, then turn it sideways by the sway, about the
        aim's own up (the up that is square to it, in her vertical plane) - which is what keeps the
        cone a cone at every elevation. That turn has a closed form: the tilted aim times cos(s),
        plus her LEFT times sin(s), since up x (tilted aim) is left whatever the tilt. Left is
        world up x Forward.
    */
    v3 f = Forward();
    v3 left(f.z,0.0f,-f.x);
    float s = AimSwaySideDeg() * STAGE_DEG2RAD;
    v3 tilted = f * cosf(a) + v3(0.0f,sinf(a),0.0f);
    return tilted * cosf(s) + left * sinf(s);
}

v3 Stage::AnchorPosition() const{
    //Eased from the standing anchor to the kneeling one as she goes down, and back.
    float k = KneelAmount();
    float fwd = BOW_NOCK_FWD + (KNEEL_NOCK_FWD - BOW_NOCK_FWD) * k;
    float up = BOW_NOCK_UP + (KNEEL_NOCK_UP - BOW_NOCK_UP) * k;
    //Her body is at z 0 in every level; the anchor is ahead of her along Forward - which on a
    //locked plane is (facing,0,0), and so is pos + (fwd * facing, up) as it always was.
    v3 f = Forward();
    return v3(pos.x + f.x * fwd,pos.y + up,f.z * fwd);
}

v3 Stage::MuzzlePosition() const{
    return AnchorPosition() + AimDirection() * ARROW_LENGTH;
}

/*
    An arrow's attitude from its velocity - see Arrow. On the plane (vz exactly 0) it is the one
    angle it has always been, atan2(vy, vx), and yaw 0; off it, yaw is the turn about +Y out of the
    plane, kept within a quarter turn by measuring it off whichever way along X the arrow is
    going, and `angle` is the climb in the vertical plane that turn leaves it in.
*/
static void ArrowAttitude(const v3& vel, float& out_angle, float& out_yaw){
    if (vel.z == 0.0f){
        out_angle = atan2f(vel.y,vel.x);
        out_yaw = 0.0f;
        return;
    }
    float along = (vel.x < 0.0f) ? -1.0f : 1.0f;
    float level = sqrtf(vel.x * vel.x + vel.z * vel.z);
    out_yaw = atan2f(-vel.z * along,vel.x * along);
    out_angle = atan2f(vel.y,level * along);
}

const char* ArrowKindName(int kind){
    static const char* names[ARROW_KIND_COUNT] = { "arrow", "vine", "bamboo" };
    return (kind >= 0 && kind < ARROW_KIND_COUNT) ? names[kind] : "?";
}

/*
    The number keys, or a pad's step round the kinds. From the RAW input, not the get-up's empty
    one (see Tick): choosing an arrow is not an action she performs, and a press swallowed by the
    get-up would be a key that silently did nothing. A pick of a kind that does not exist yet (key
    3 while there are two) is ignored rather than clamped, so it never picks the wrong one.
*/
void Stage::SelectArrow(const ArcherInput& in, StageEvents& events){
    int kind = arrow_kind;
    if (in.arrow_select >= 0){
        if (in.arrow_select < ARROW_KIND_COUNT){
            kind = in.arrow_select;
        }
    }else if (in.arrow_step != 0){
        int step = (in.arrow_step > 0) ? 1 : -1;
        kind = (arrow_kind + step + ARROW_KIND_COUNT) % ARROW_KIND_COUNT;
    }
    if (kind != arrow_kind){
        arrow_kind = kind;
        events.f_arrow_kind_changed = true;
    }
}

void Stage::Loose(StageEvents& events){
    float power = DrawPower();
    float speed = ARROW_SPEED_MIN + (ARROW_SPEED_MAX - ARROW_SPEED_MIN) * power;
    v3 dir = AimDirection();
    float shot_aim_deg = ShotAimDeg();      //read before the draw is cleared below takes the sway
    float shot_side_deg = AimSwaySideDeg();

    //A free slot, or the oldest arrow if every slot is live. Recycling rather than refusing: an
    //input that silently does nothing is the one failure mode a main verb must not have.
    int slot = -1;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (!arrows[i].f_live){
            slot = i;
            break;
        }
    }
    if (slot < 0){
        slot = next_arrow;
    }
    next_arrow = (slot + 1) % ARROW_MAX_LIVE;

    Arrow& a = arrows[slot];
    a = Arrow();
    a.pos = MuzzlePosition();
    //The segment the first sweep covers starts at the ANCHOR, not at the tip - see ARROW_LENGTH.
    //TickArrows reads it from here on the arrow's first step, and so does the app's prop raycast.
    a.prev_pos = AnchorPosition();
    /*
        The archer's own velocity is NOT added in.

        Physically it should be, and it is left out anyway: the arc drawn on screen while the bow
        is drawn is PredictArc(), and a running archer would make the drawn arc a lie for as long
        as they kept running. Adding it to the preview too only moves the problem - the preview is
        drawn on the tick you look at it and the shot happens on the tick you release, and between
        those two the run speed has changed. A promise drawn on screen has to be keepable.
    */
    a.vel = dir * speed;
    ArrowAttitude(a.vel,a.angle,a.yaw);
    a.f_live = true;
    a.f_stuck = false;
    a.kind = arrow_kind;

    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    arrows_shot++;

    events.f_shot = true;
    events.shot_power = power;
    events.shot_aim_deg = shot_aim_deg;
    events.shot_side_deg = shot_side_deg;
    events.shot_kind = a.kind;
}

void Stage::TickArrows(StageEvents& events){
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        Arrow& a = arrows[i];
        if (!a.f_live){
            continue;
        }
        if (a.f_stuck){
            a.age_ticks++;
            if (a.age_ticks > ARROW_STUCK_TICKS){
                a.f_live = false;
            }
            continue;
        }

        /*
            Where this step's sweep starts: the tip, except on the arrow's FIRST step, where it is
            the anchor Loose left in prev_pos - so the nock-to-tip span is swept as part of the
            flight rather than skipped. See ARROW_LENGTH. Kept in prev_pos for the app either way:
            it is the segment rp3d is asked about for crates and targets. See the handshake note
            on Stage::arrows.
        */
        v3 from;
        v3 next;
        v3 point;
        v3 normal;
        int block = FlyArrow(a,from,next,point,normal);
        a.prev_pos = from;
        //The webs, up to whatever block the flight meets this tick: a thread before the wall is met
        //first. Caught, it is stuck in the web already. Not in the forecast (PredictArrowImpact),
        //which is for the swoosh and the block it ends in.
        if (!webs.empty() && ArrowThroughWebs(i,from,(block >= 0) ? point : next,events)){
            continue;
        }
        if (block >= 0){
            float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y + a.vel.z * a.vel.z);
            //Backed off along the face so the shaft is embedded rather than coplanar with the
            //surface, which z-fights.
            a.pos = point + normal * 0.02f;
            a.vel = v3(0.0f,0.0f,0.0f);
            a.f_stuck = true;
            a.age_ticks = 0;
            arrows_hit_blocks++;

            StageEvents::ArrowHit hit;
            hit.arrow = i;
            hit.point = point;
            hit.normal = normal;
            hit.speed = speed;
            hit.block = block;
            hit.kind = a.kind;
            events.arrow_hits.push_back(hit);
            continue;
        }

        a.pos = next;
        ArrowAttitude(a.vel,a.angle,a.yaw);
        a.age_ticks++;
        if (a.age_ticks > ARROW_MAX_AGE_TICKS || a.pos.y < -60.0f){
            a.f_live = false;
        }
    }
}

int Stage::FlyArrow(Arrow& a, v3& from, v3& next, v3& point, v3& normal) const{
    from = (a.age_ticks == 0) ? a.prev_pos : a.pos;
    a.vel.y -= ARROW_GRAVITY * ARCHER_DT;
    next = a.pos + a.vel * ARCHER_DT;
    return SegmentHitsBlock(from,next,point,normal);
}

StageArrowImpact Stage::PredictArrowImpact(int index, int horizon, std::vector<v3>* path) const{
    StageArrowImpact out;
    if (path){
        path->clear();
    }
    if (index < 0 || index >= ARROW_MAX_LIVE || !arrows[index].f_live || arrows[index].f_stuck){
        return out;
    }
    Arrow a = arrows[index];
    for (int i = 1; i <= horizon; i++){
        v3 from;
        v3 next;
        v3 point;
        v3 normal;
        int block = FlyArrow(a,from,next,point,normal);
        if (path){
            if (path->empty()){
                path->push_back(from);
            }
            path->push_back((block >= 0) ? point : next);
        }
        if (block >= 0){
            out.f_hits = true;
            out.ticks = i;
            out.point = point;
            out.block = block;
            return out;
        }
        //The rest of TickArrows' step for a flight that carries on, and its end.
        a.pos = next;
        a.age_ticks++;
        if (a.age_ticks > ARROW_MAX_AGE_TICKS || a.pos.y < -60.0f){
            return out;
        }
    }
    return out;
}

int Stage::NumLiveArrows() const{
    int n = 0;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (arrows[i].f_live){
            n++;
        }
    }
    return n;
}

void Stage::StickArrow(int index, const v3& point){
    if (index < 0 || index >= ARROW_MAX_LIVE){
        return;
    }
    Arrow& a = arrows[index];
    a.pos = point;
    a.vel = v3(0.0f,0.0f,0.0f);
    a.f_stuck = true;
    a.age_ticks = 0;
}

void Stage::KillArrow(int index){
    if (index < 0 || index >= ARROW_MAX_LIVE){
        return;
    }
    arrows[index].f_live = false;
}

/*
    Nearest block struck by the segment a -> b.

    The slab method, with the entry face's normal carried along. A segment rather than a point
    test because at a full draw an arrow covers 0.77 units in a tick, which is wider than the
    cracked wall in this level is thick - a per-tick overlap test would let a fast arrow pass
    clean through it, and would do so only sometimes, which is the worst kind of bug to be handed.

    Three pairs of faces, the third the block's depth through the slab. On a locked plane the
    segment has no z to speak of - it is at z 0 and parallel to that pair - and every block covers
    z 0 (STAGE_BLOCK_MIN_COVER), so the third pair can neither narrow the hit nor lose it and the
    answer is the 2D one exactly. Off the plane it is what lets an arrow pass in front of a block.
*/
int Stage::SegmentHitsBlock(const v3& a, const v3& b, v3& out_point, v3& out_normal) const{
    v3 d = b - a;
    float best_t = 2.0f;
    int best = -1;
    v3 best_normal;

    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& blk = blocks[i];
        if (!blk.f_alive){
            continue;
        }
        //Arrows fly through one-way platforms, in both directions. A platform that is solid from
        //above to a body but transparent to an arrow is a small inconsistency and the right one:
        //the alternative is arrows collecting on the underside of every platform in the level.
        if (blk.kind == BLOCK_PLATFORM){
            continue;
        }
        //Nor through a web's wall: it is her wall, and the arrow is for the threads in it.
        if (blk.web >= 0){
            continue;
        }

        float t_near = 0.0f;
        float t_far = 1.0f;
        v3 normal;
        bool f_miss = false;

        for (int axis = 0; axis < 3 && !f_miss; axis++){
            float da   = (axis == 0) ? d.x : (axis == 1) ? d.y : d.z;
            float orig = (axis == 0) ? a.x : (axis == 1) ? a.y : a.z;
            float lo   = (axis == 0) ? blk.Left()   : (axis == 1) ? blk.Bottom() : blk.Back();
            float hi   = (axis == 0) ? blk.Right()  : (axis == 1) ? blk.Top()    : blk.Front();

            if (da > -1e-8f && da < 1e-8f){
                //Parallel to this pair of faces: either it is already between them for the whole
                //segment, or it can never be.
                if (orig < lo || orig > hi){
                    f_miss = true;
                }
                continue;
            }

            float t1 = (lo - orig) / da;
            float t2 = (hi - orig) / da;
            //The entry face is the one reached first, and its outward normal points back along
            //the direction of travel on this axis.
            float n = (da > 0.0f) ? -1.0f : 1.0f;
            if (t1 > t2){
                float swap = t1;
                t1 = t2;
                t2 = swap;
            }
            if (t1 > t_near){
                t_near = t1;
                normal = (axis == 0) ? v3(n,0.0f,0.0f) : (axis == 1) ? v3(0.0f,n,0.0f) : v3(0.0f,0.0f,n);
            }
            if (t2 < t_far){
                t_far = t2;
            }
            if (t_near > t_far){
                f_miss = true;
            }
        }

        if (f_miss || t_near > t_far || t_near > 1.0f){
            continue;
        }
        if (t_near < best_t){
            best_t = t_near;
            best = (int)i;
            best_normal = normal;
        }
    }

    if (best >= 0){
        out_point = a + d * best_t;
        out_normal = best_normal;
    }
    return best;
}

/*
    The landing forecast - see the declaration. The whole Stage is copied rather than the few
    fields a fall reads, because a copy that leaves something out is a second model of the rules,
    and a second model drifts; this one cannot, since it IS the rules. A copy is a few kilobytes
    and a fall rarely needs a full horizon, so it costs less than it sounds.
*/
StageLanding Stage::PredictLanding(const ArcherInput& in, int horizon) const{
    StageLanding out;
    if (mode != MODE_AIR || horizon < 1){
        return out;
    }
    ArcherInput held = in;
    held.f_jump_pressed = false;
    held.f_draw_released = false;
    held.f_kick_pressed = false;
    held.f_action_pressed = false;
    held.f_kneel_pressed = false;

    Stage ahead = *this;
    for (int i = 1; i <= horizon; i++){
        StageEvents e;
        ahead.Tick(held,e);
        if (e.f_landed){
            out.f_lands = true;
            out.ticks = i;
            out.speed = e.land_speed;
            out.pos = ahead.pos;
            return out;
        }
        if (ahead.mode == MODE_HANG){
            out.f_caught = true;
            out.ticks = i;
            out.pos = ahead.pos;
            return out;
        }
        if (ahead.mode != MODE_AIR){
            return out;
        }
    }
    return out;
}

/*
    The aim preview.

    Runs the SAME integration TickArrows does, at the same rate, with the same gravity, and stops
    at the first thing it would hit - so the dots are not a sketch of the flight, they are the
    flight, sampled. Any divergence between this function and TickArrows is a bug in one of them.
*/
int Stage::PredictArc(v3* out_points, int max_points) const{
    if (!out_points || max_points < 1){
        return 0;
    }

    float power = DrawPower();
    float speed = ARROW_SPEED_MIN + (ARROW_SPEED_MAX - ARROW_SPEED_MIN) * power;
    v3 p = MuzzlePosition();
    v3 v = AimDirection() * speed;
    //The first step sweeps from the anchor, exactly as TickArrows does - see ARROW_LENGTH.
    v3 from = AnchorPosition();

    int written = 0;
    int limit = (max_points < AIM_ARC_POINTS) ? max_points : AIM_ARC_POINTS;
    for (int i = 0; i < limit; i++){
        for (int s = 0; s < AIM_ARC_TICK_STRIDE; s++){
            v.y -= ARROW_GRAVITY * ARCHER_DT;
            v3 next = p + v * ARCHER_DT;
            v3 point;
            v3 normal;
            int hit = SegmentHitsBlock(from,next,point,normal);
            from = next;
            if (hit >= 0){
                out_points[written++] = point;
                return written;
            }
            p = next;
        }
        out_points[written++] = p;
    }
    return written;
}

std::string Stage::DebugLine() const{
    static const char* mode_names[] = { "ground","air","hang","climb","rope","kneel","getup" };
    char buf[256];
    snprintf(buf,sizeof(buf),
             "t=%llu %s pos=(%.2f,%.2f) vel=(%.2f,%.2f) face=%+.0f aim=%.0f draw=%d/%d arrows=%d/%d",
             (unsigned long long)ticks,
             mode_names[(mode >= 0 && mode <= MODE_GETUP) ? mode : 0],
             pos.x,pos.y,vel.x,vel.y,facing,aim_deg,draw_ticks,BOW_DRAW_TICKS,
             NumLiveArrows(),arrows_shot);
    return std::string(buf);
}

/*
    See the declaration. Field by field, never a struct's bytes: padding is whatever was in memory.
    The order is the members' own, so a field added to Stage.h has an obvious place here.
*/
void Stage::HashState(StateHash& h) const{
    auto add2 = [&h](const v2& v){ h.Add(v.x); h.Add(v.y); };
    auto add3 = [&h](const v3& v){ h.Add(v.x); h.Add(v.y); h.Add(v.z); };

    h.Begin("her");
    add2(pos); add2(vel);
    h.Add(mode); h.Add(facing); h.Add(f_on_ground); h.Add(coyote_ticks); h.Add(buffer_ticks);
    h.Add(spring_on); h.Add(launch_lift); h.Add(ramp_on); h.Add(bridge_on);
    h.Add(stomp_ticks); h.Add(spring_left); h.Add(spring_air_ticks); h.Add(f_swung);
    h.Add(prev_aim_axis); h.Add(spring_boost_seen);
    h.Add(branch_on); h.Add(lean); h.Add(lean_rate); h.Add(balance_ticks); h.Add(balance_entries);
    h.Add(vitals.exertion); h.Add(vitals.fear); h.Add(vitals.heart_rate);
    h.Add(vitals.exertion_target); h.Add(vitals.fear_target);
    h.Add(hang_block); h.Add(hang_branch); h.Add(hang_side); h.Add(climb_ticks);
    h.Add(kick_ticks); h.Add(kick_cooldown); h.Add(kick_kind);
    h.Add(kneel_phase); h.Add(kneel_ticks); h.Add(getup_ticks);
    h.Add(rope_id); h.Add(rope_ticks); h.Add(rope_cooldown); h.Add(rope_s); h.Add(rope_climb);
    h.Add(rope_climbed); h.Add(rope_pump); add2(climb_from); add2(climb_to); h.Add(grab_cooldown);
    h.Add(bow_mode); h.Add(draw_ticks); h.Add(aim_deg); h.Add(aim_roam_ticks); h.Add(draws_cancelled);
    h.Add(sway_ticks);
    h.Add(draws_started); h.Add(arrow_kind); h.Add(heading_deg);

    h.Begin("world");
    h.Add(ticks); h.Add(arrows_shot); h.Add(arrows_hit_blocks); h.Add(next_arrow);
    for (const Arrow& a : arrows){
        add3(a.pos); add3(a.prev_pos); add3(a.vel);
        h.Add(a.angle); h.Add(a.yaw); h.Add(a.age_ticks); h.Add(a.f_live); h.Add(a.f_stuck);
        h.Add(a.kind);
    }
    for (const StageBlock& b : blocks){
        h.Add(b.f_alive); h.Add(b.crumble_ticks);
    }
    for (const StageSpringPlant& sp : spring_plants){
        h.Add(sp.q); h.Add(sp.qd); h.Add(sp.prev_q);
    }
    for (const StageBridge& br : bridges){
        for (const v2& q : br.p){ add2(q); }
        for (const v2& q : br.v){ add2(q); }
        for (const v2& q : br.prev_p){ add2(q); }
        h.Add(br.load_at); h.Add(br.load_t);
        for (float s : br.strain){ h.Add(s); }
        for (uint8_t b : br.broken){ h.Add(b); }
        h.Add(br.level);
    }
    //Only where a level has one, so a level without adds nothing and its hash is what it was.
    for (const StageWeb& web : webs){
        for (const v2& q : web.p){ add2(q); }
        for (const v2& q : web.v){ add2(q); }
        for (const StageWebThread& t : web.threads){ h.Add(t.f_cut); }
        for (const StageWebCatch& c : web.caught){ h.Add(c.arrow); h.Add(c.node); add2(c.offset); add2(c.placed); }
        h.Add(web.f_breached);
    }
    for (const StageCrumbleGroup& g : crumble_groups){
        h.Add(g.ticks); h.Add(g.f_done);
    }
    for (uint8_t inside : zone_inside){
        h.Add(inside);
    }
    for (const PendingEffect& e : pending_effects){
        h.Add(e.effect.kind); h.Add(e.effect.target); h.Add(e.effect.delay); h.Add(e.at);
    }
}
