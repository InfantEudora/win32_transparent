#include <math.h>
#include <stdio.h>

#include "Stage.h"

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

//Does the archer's body box, centred at (cx,cy), overlap this rectangle?
static bool BoxOverlapsRect(float cx, float cy, float left, float right, float bottom, float top){
    if (cx + ARCHER_HALF_W <= left)   { return false; }
    if (cx - ARCHER_HALF_W >= right)  { return false; }
    if (cy + ARCHER_HALF_H <= bottom) { return false; }
    if (cy - ARCHER_HALF_H >= top)    { return false; }
    return true;
}

static bool BoxOverlapsBlock(float cx, float cy, const StageBlock& b){
    return BoxOverlapsRect(cx,cy,b.Left(),b.Right(),b.Bottom(),b.Top());
}

static bool BoxOverlapsObstacle(float cx, float cy, const StageObstacle& o){
    return BoxOverlapsRect(cx,cy,o.Left(),o.Right(),o.Bottom(),o.Top());
}

//--- Construction -------------------------------------------------------------------------------

Stage::Stage(){
    Reset();
}

void Stage::Reset(){
    blocks.clear();
    props.clear();
    BuildLevel();

    //Above the start ground, so the first thing the archer does is land - which exercises the
    //landing path on tick one rather than leaving it untested until the first jump.
    pos = StartPosition();
    vel = v2(0.0f,0.0f);
    mode = MODE_AIR;
    facing = 1.0f;
    f_on_ground = false;
    coyote_ticks = 0;
    buffer_ticks = 0;

    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    aim_deg = 20.0f;

    hang_block = -1;
    hang_side = -1.0f;
    climb_ticks = 0;
    climb_from = v2();
    climb_to = v2();
    grab_cooldown = 0;
    kick_ticks = 0;
    kick_cooldown = 0;
    rope_id = -1;
    rope_ticks = 0;
    rope_cooldown = 0;
    rope_points.clear();

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        arrows[i] = Arrow();
    }
    next_arrow = 0;

    ticks = 0;
    arrows_shot = 0;
    arrows_hit_blocks = 0;
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
    blocks.push_back({ 55.75f, -2.00f, 16.25f, 2.00f, BLOCK_SOLID,  true });    //x 39.5 .. 72

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

    //A cracked wall across the path, 2.5 tall. Solid to the archer and to arrows until the
    //kick-and-break slice knocks it out - so for now the target behind it has to be LOBBED over,
    //which is the most interesting thing a bow can be asked to do and wants no extra code.
    blocks.push_back({ 57.00f,  1.25f,  0.50f, 1.25f, BLOCK_BREAKABLE, true });

    //The right-hand wall, so a run to the end stops rather than falling off the world.
    blocks.push_back({ 71.00f,  4.00f,  1.00f, 4.00f, BLOCK_SOLID,  true });

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
        knocks it over instead of just scoring.
    */
    props.push_back({ PROP_TARGET, 20.00f, 0.80f, 0.30f, 1.60f, 1, 1 });   //across the first gap
    props.push_back({ PROP_TARGET, 24.50f, 0.80f, 0.30f, 1.60f, 1, 1 });   //under the one-way, 2.5 of headroom
    props.push_back({ PROP_TARGET, 31.00f, 3.40f, 0.30f, 1.60f, 1, 1 });   //standing on the ledge
    props.push_back({ PROP_TARGET, 62.00f, 0.80f, 0.30f, 1.60f, 1, 1 });   //behind the cracked wall

    //For the kick-and-break slice. w/h are the WHOLE wall; cols/rows subdivide it into bricks.
    props.push_back({ PROP_BRICKWALL, 49.50f, 1.60f, 2.70f, 3.15f, 3, 7 });

    //For the rope slice. x/y is the fixed anchor point, h the length of rope hanging from it -
    //over the second gap, because a rope you can walk around is a rope nobody swings on.
    props.push_back({ PROP_ROPE_ANCHOR, 36.75f, 9.00f, 0.10f, 6.00f, 1, 1 });

#if ARCHER_TEST_BAY
    /*
        --- The terrain test bay, x -40 .. -12 -------------------------------------------------
        Four bays of seven units, left of the start and contiguous with the main ground run, which
        ends at x -12. CONTIGUOUS MATTERS: TickArcher restarts the game below y -40, so a bay
        floating in space would drop the player out of the world on the way into it.

        Seven units each because the camera shows about 31.8 of them at CAMERA_DISTANCE, so all
        four variants land in ONE screenshot - which is the entire reason for laying them out this
        way rather than rebuilding one bay over and over.

        See the SOLID-only and append-at-the-end rules in the ARCHER_TEST_BAY note in Stage.h.
    */
    //The left-hand wall, mirroring the one at x 71 for the same reason. Kept INSIDE bay 0's span
    //so that it melts with the rest of it - a lone blockout box at the end of a row of terrain
    //reads as something that failed to build rather than as a deliberate boundary.
    blocks.push_back({ ARCHER_TEST_BAY_X_MIN + 0.25f, 4.00f, 0.25f, 4.00f, BLOCK_SOLID, true });

    /*
        THE SAME FOUR SHAPES IN EVERY BAY, so that the four variants differ only in their meshing
        parameters and a difference between them can only be the parameters. Each shape is the
        cheapest thing that exposes one specific failure:

          the floor     a wide flat top - the surface every measurement of top-pinning is taken on
          the step      a convex lip - does the top stay pinned exactly where the collider is?
          the wall      the concave inside corner - does smooth union bulge into walkable space?
          the pillar    0.5 wide, thinner than twice a typical smoothing radius - does it survive?

        EACH BAY GETS ITS OWN FLOOR SEGMENT rather than one slab running under all four. The app
        selects a bay's blocks by x range, so a block spanning every bay would belong to all of
        them and be meshed four times into four overlapping surfaces - which is z-fighting, not a
        comparison. The segments abut exactly, so the collision underneath is still one flat run.
    */
    for (int i = 0; i < ARCHER_TEST_BAY_COUNT; i++){
        float cx = ARCHER_TEST_BAY_CENTRE(i);
        float half = ARCHER_TEST_BAY_WIDTH * 0.5f;
        blocks.push_back({ cx,        -2.00f, half,  2.00f, BLOCK_SOLID, true });   //floor,  top 0
        blocks.push_back({ cx - 2.0f,  0.60f, 1.00f, 0.60f, BLOCK_SOLID, true });   //step,   top 1.2
        blocks.push_back({ cx - 0.2f,  1.40f, 0.80f, 1.40f, BLOCK_SOLID, true });   //wall,   top 2.8
        blocks.push_back({ cx + 2.2f,  1.00f, 0.25f, 1.00f, BLOCK_SOLID, true });   //pillar, top 2.0
    }
#endif
}

void Stage::BuildLevel(){
    switch (level){
        case STAGE_LEVEL_RANGE: BuildRangeLevel(); break;
        default:                BuildMainLevel();  break;
    }
}

void Stage::SetLevel(int new_level){
    level = (new_level >= 0 && new_level < STAGE_LEVEL_COUNT) ? new_level : STAGE_LEVEL_MAIN;
    Reset();
}

v2 Stage::StartPosition() const{
    //Both a little above the floor, so the first tick is a landing - see the note in Reset.
    if (level == STAGE_LEVEL_RANGE){
        return v2(0.0f,2.0f);
    }
    return v2(-6.0f,2.0f);
}

/*
    The test range: one floor, a wall at each end, and targets at two distances on either side.

    NARROWER THAN ONE SCREEN, on purpose. The camera shows about 31.8 units across at
    CAMERA_DISTANCE, so with the floor at x -17 .. 17 the whole range fits in one frame when she
    stands in the middle, and the walls sit just past its edges. The range camera follows her
    slowly (RANGE_CAMERA_SMOOTH in ApplicationArcher.h), so walking to one end brings that wall
    into view and loses the far one. The walls exist so that there
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
void Stage::BuildRangeLevel(){
    blocks.push_back({   0.00f, -2.00f, 17.00f, 2.00f, BLOCK_SOLID, true });   //the floor, top at 0
    blocks.push_back({ -17.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //left wall, top at 48
    blocks.push_back({  17.50f, 24.00f,  0.50f, 24.00f, BLOCK_SOLID, true });  //right wall

    props.push_back({ PROP_TARGET, -12.00f, 0.80f, 0.30f, 1.60f, 1, 1 });
    props.push_back({ PROP_TARGET,  -6.00f, 0.80f, 0.30f, 1.60f, 1, 1 });
    props.push_back({ PROP_TARGET,   6.00f, 0.80f, 0.30f, 1.60f, 1, 1 });
    props.push_back({ PROP_TARGET,  12.00f, 0.80f, 0.30f, 1.60f, 1, 1 });

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
}

//--- The props, as the rules see them -----------------------------------------------------------

void Stage::ClearObstacles(){
    obstacles.clear();
}

void Stage::AddObstacle(float x, float y, float hw, float hh, int id, bool f_pushable){
    StageObstacle o;
    o.x = x;
    o.y = y;
    o.hw = hw;
    o.hh = hh;
    o.id = id;
    o.f_pushable = f_pushable;
    obstacles.push_back(o);
}

//--- The tick -----------------------------------------------------------------------------------

void Stage::Tick(const ArcherInput& in, StageEvents& events){
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

    //Computed after TickBow, which is what makes a press-and-release inside a single tick fire a
    //minimum-power shot rather than being swallowed. A dropped key press is the worst possible
    //outcome for the game's main verb.
    bool f_loose = (bow_mode == BOW_DRAWING) && in.f_draw_released;

    //Before the archer moves, so the boot sweeps from where they were standing when it went out.
    //At a full run those differ by 0.15 of a unit - the difference between connecting with the
    //near brick of a wall and connecting with nothing.
    TickKick(in,events);

    TickArcher(in,events);

    if (f_loose){
        Loose(events);
    }
    TickArrows(events);

    ticks++;
}

void Stage::TickBow(const ArcherInput& in, StageEvents& events){
    (void)events;

    //Aim tilts whether or not the bow is drawn, so the next shot starts where the last one was
    //pointed and a player can line up before committing to a draw.
    aim_deg = ClampF(aim_deg + in.aim_axis * BOW_AIM_RATE_DEG * ARCHER_DT,
                     BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);

    //Both hands are on the rock. Aiming still tilts - it costs nothing and lets a player line up
    //the shot they are about to take on landing - but no draw can START while hanging or climbing,
    //and EnterHang cancels one already under way.
    bool f_hands_full = (mode == MODE_HANG || mode == MODE_CLIMB || mode == MODE_ROPE);

    if (in.f_draw_down && !f_hands_full){
        if (bow_mode == BOW_IDLE){
            bow_mode = BOW_DRAWING;
            draw_ticks = 0;
        }else if (draw_ticks < BOW_DRAW_TICKS){
            draw_ticks++;
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

    //--- Horizontal ---------------------------------------------------------------------------
    float move_scale = (bow_mode == BOW_DRAWING) ? ARCHER_DRAW_MOVE_SCALE : 1.0f;
    float target_vx = ClampF(in.move_axis,-1.0f,1.0f) * ARCHER_RUN_SPEED * move_scale;

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
        float accel = f_on_ground ? ARCHER_RUN_ACCEL : ARCHER_AIR_ACCEL;
        vel.x = MoveToward(vel.x,target_vx,accel * ARCHER_DT);
        //Facing follows the input even mid-draw. The aim angle is relative to facing, so turning
        //while drawn mirrors the shot rather than losing it, which is what a player turning to
        //deal with something behind them means.
        facing = (in.move_axis > 0.0f) ? 1.0f : -1.0f;
    }else{
        float friction = f_on_ground ? ARCHER_RUN_FRICTION : ARCHER_AIR_FRICTION;
        vel.x = MoveToward(vel.x,0.0f,friction * ARCHER_DT);
    }

    //--- Jump -----------------------------------------------------------------------------------
    if (in.f_jump_pressed){
        buffer_ticks = ARCHER_JUMP_BUFFER_TICKS;
    }
    bool f_may_jump = f_on_ground || (coyote_ticks > 0);
    if (buffer_ticks > 0 && f_may_jump){
        vel.y = ARCHER_JUMP_SPEED;
        buffer_ticks = 0;
        coyote_ticks = 0;
        f_on_ground = false;
        events.f_jumped = true;
    }

    /*
        The variable-height cut, as a CLAMP rather than a multiply.

        The obvious spelling - vel.y *= ARCHER_JUMP_CUT while the key is up - runs every tick the
        key stays up, so the rise does not get cut, it gets annihilated inside three ticks. A clamp
        is idempotent: the first tick after release brings the climb down to its capped value and
        every tick after that finds it already there.
    */
    if (vel.y > 0.0f && !in.f_jump_down){
        float capped = ARCHER_JUMP_SPEED * ARCHER_JUMP_CUT;
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

    //--- Move -----------------------------------------------------------------------------------
    bool f_was_on_ground = f_on_ground;
    float impact_speed = vel.y;     //captured because MoveAndCollide zeroes it on contact

    bool f_hit_floor = false;
    bool f_hit_ceiling = false;
    bool f_hit_wall = false;
    MoveAndCollide(vel * ARCHER_DT,in.f_down_held,events,f_hit_floor,f_hit_ceiling,f_hit_wall);

    f_on_ground = f_hit_floor;
    if (f_hit_floor && !f_was_on_ground){
        events.f_landed = true;
        events.land_speed = (impact_speed < 0.0f) ? -impact_speed : impact_speed;
    }
    if (f_hit_ceiling){
        events.f_bumped_head = true;
    }

    //--- Grace timers ---------------------------------------------------------------------------
    if (f_on_ground){
        coyote_ticks = ARCHER_COYOTE_TICKS;
    }else if (coyote_ticks > 0){
        coyote_ticks--;
    }
    if (buffer_ticks > 0){
        buffer_ticks--;
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
        int rope = FindRopePoint();
        if (rope >= 0){
            mode = MODE_ROPE;
            rope_id = rope;
            rope_ticks = 0;
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
                if (!BoxOverlapsBlock(pos.x,pos.y,b)){
                    continue;
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
                if (!BoxOverlapsObstacle(pos.x,pos.y,o)){
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
                if (!BoxOverlapsBlock(pos.x,pos.y,b)){
                    continue;
                }
                if (step.y < 0.0f){
                    pos.y = b.Top() + ARCHER_HALF_H + STAGE_EPS;
                    out_hit_floor = true;
                }else{
                    pos.y = b.Bottom() - ARCHER_HALF_H - STAGE_EPS;
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
            float prev_top = prev_bottom + ARCHER_HALF_H * 2.0f;
            for (size_t i = 0; i < obstacles.size(); i++){
                const StageObstacle& o = obstacles[i];
                if (!BoxOverlapsObstacle(pos.x,pos.y,o)){
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
                    pos.y = o.Bottom() - ARCHER_HALF_H - STAGE_EPS;
                    out_hit_ceiling = true;
                }
                vel.y = 0.0f;
            }
        }
    }
}

//--- The rope ---------------------------------------------------------------------------------

void Stage::ClearRopePoints(){
    rope_points.clear();
}

void Stage::AddRopePoint(float x, float y, int id){
    StageRopePoint p;
    p.x = x;
    p.y = y;
    p.id = id;
    rope_points.push_back(p);
}

/*
    A link the archer could catch, measured from the HANDS rather than from the body's centre.

    Same reasoning as the ledge: the thing doing the grabbing is at the top of the body, and
    measuring from the middle makes a rope at head height read as out of reach while one at knee
    height reads as catchable.
*/
int Stage::FindRopePoint() const{
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

    //Face the way the swing is going, so the bow points down the arc rather than at the anchor.
    if (vel.x > 1.0f){
        facing = 1.0f;
    }else if (vel.x < -1.0f){
        facing = -1.0f;
    }

    if (rope_ticks < ROPE_MIN_HOLD_TICKS){
        return;
    }
    if (!in.f_jump_pressed && !in.f_action_pressed){
        return;
    }

    mode = MODE_AIR;
    rope_id = -1;
    rope_cooldown = ROPE_GRAB_COOLDOWN;
    f_on_ground = false;
    //The swing has just handed the archer a lot of speed, and a jump buffered during it would
    //spend it on a jump off nothing the moment they land.
    buffer_ticks = 0;
    events.f_released_rope = true;
    events.f_rope_jump = in.f_jump_pressed;
}

//--- The kick -------------------------------------------------------------------------------------

/*
    The box the boot sweeps.

    In front of the archer, low, and reaching KICK_REACH past the body's leading edge. A separate
    function because three things want it and they must not drift apart: the sweep below, the rules
    test, and the debug draw the app puts on screen while tuning.
*/
void Stage::KickBox(float& out_left, float& out_right, float& out_bottom, float& out_top) const{
    float lead = (facing > 0.0f) ? (pos.x + ARCHER_HALF_W) : (pos.x - ARCHER_HALF_W);
    float far_edge = lead + facing * KICK_REACH;
    out_left  = (lead < far_edge) ? lead : far_edge;
    out_right = (lead < far_edge) ? far_edge : lead;
    float centre_y = pos.y + KICK_Y_OFFSET;
    out_bottom = centre_y - KICK_HALF_HEIGHT;
    out_top    = centre_y + KICK_HALF_HEIGHT;
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
        her for KICK_TICKS, which is 1.43 seconds - longer than an entire jump - so a flying kick
        was really a decision to hang motionless in mid-air until the ground arrived; and
        Kick_Front is a GROUNDED clip, a wind-up and a plant and a recovery, all of which need a
        floor to push against and none of which read as anything but a bug when they float. A kick
        is something you do with your weight on the ground.

        f_on_ground is last tick's, because TickKick runs before TickArcher. One tick of lag on a
        gate costs nothing and keeps the ordering note at the top of Stage::Tick true.
    */
    bool f_busy = (mode == MODE_HANG || mode == MODE_CLIMB || mode == MODE_ROPE || !f_on_ground);

    if (kick_ticks == 0){
        if (in.f_kick_pressed && kick_cooldown == 0 && !f_busy){
            kick_ticks = 1;
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

    kick_ticks++;
    if (kick_ticks > KICK_TICKS){
        kick_ticks = 0;
        kick_cooldown = KICK_COOLDOWN;
        return;
    }
    //Wind-up and recovery: the move is running but the boot is not live.
    if (kick_ticks < KICK_ACTIVE_FROM || kick_ticks > KICK_ACTIVE_TO){
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
        kick.dir = facing;
        kick.x = (left + right) * 0.5f;
        kick.y = (bottom + top) * 0.5f;
        events.kicks.push_back(kick);
        events.f_kick_connected = true;
    }

    //One connect per kick. Without this the boot stays live for the whole window and hits the same
    //crate five times, which is five impulses and a crate that leaves the level.
    if (events.f_kick_connected){
        kick_ticks = KICK_ACTIVE_TO + 1;
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
    //Body flat against the face, hands exactly on the lip.
    pos.x = (side < 0.0f) ? (b.Left() - ARCHER_HALF_W - STAGE_EPS)
                          : (b.Right() + ARCHER_HALF_W + STAGE_EPS);
    pos.y = b.Top() - ARCHER_HALF_H;
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
    vel = v2(0.0f,0.0f);
    //Without this, the drop re-grabs the same lip on the next tick and the archer is welded to it.
    grab_cooldown = LEDGE_RELEASE_COOLDOWN;
    events.f_released_ledge = true;
}

void Stage::TickHang(const ArcherInput& in, StageEvents& events){
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
        //Onto the top surface, just inside the edge the archer came up over.
        climb_to = v2((hang_side < 0.0f) ? (b.Left() + ARCHER_HALF_W + STAGE_EPS)
                                         : (b.Right() - ARCHER_HALF_W - STAGE_EPS),
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
    pos.y = b.Top() - ARCHER_HALF_H;
    vel = v2(0.0f,0.0f);
}

/*
    Pulling up over the lip.

    UNINTERRUPTIBLE, and a straight lerp along both axes. That is not a placeholder standing in for
    something cleverer - it is the shape a root-motion climb clip has too, which is why this is a
    tick count and a start/end pair rather than a velocity: when the animation arrives, the clip's
    own displacement replaces the lerp and nothing else here changes.
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
        //Landing on top of the thing you just climbed is not a fresh chance to grab it.
        grab_cooldown = LEDGE_RELEASE_COOLDOWN;
        events.f_climbed = true;
        return;
    }

    float t = 1.0f - ((float)climb_ticks / (float)LEDGE_CLIMB_TICKS);
    //Up first, then across. Interpolating both together walks the body diagonally THROUGH the
    //corner it is climbing over, which with a box for a character is very visible.
    float up = ClampF(t * 1.6f,0.0f,1.0f);
    float across = ClampF((t - 0.35f) / 0.65f,0.0f,1.0f);
    pos.x = climb_from.x + (climb_to.x - climb_from.x) * across;
    pos.y = climb_from.y + (climb_to.y - climb_from.y) * up;
    vel = v2(0.0f,0.0f);
}

//--- The bow ------------------------------------------------------------------------------------

float Stage::DrawPower() const{
    float t = (float)draw_ticks / (float)BOW_DRAW_TICKS;
    return BOW_MIN_POWER + (1.0f - BOW_MIN_POWER) * ClampF(t,0.0f,1.0f);
}

v2 Stage::AimDirection() const{
    float a = aim_deg * STAGE_DEG2RAD;
    //Mirrored through facing, so +30 degrees means "thirty up from straight ahead" whichever way
    //the archer is looking. A world-space angle would mean the same key tilted the wrong way
    //half the time.
    return v2(cosf(a) * facing,sinf(a));
}

v2 Stage::MuzzlePosition() const{
    v2 shoulder = pos + v2(BOW_SHOULDER_FWD * facing,BOW_SHOULDER_UP);
    //Pushed a little further along the aim so the arrow is not born inside the archer's own body
    //box - which would be invisible here, where the rules do not collide arrows against the
    //archer, but shows up the moment something else does.
    return shoulder + AimDirection() * 0.25f;
}

void Stage::Loose(StageEvents& events){
    float power = DrawPower();
    float speed = ARROW_SPEED_MIN + (ARROW_SPEED_MAX - ARROW_SPEED_MIN) * power;
    v2 dir = AimDirection();

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
    a.prev_pos = a.pos;
    /*
        The archer's own velocity is NOT added in.

        Physically it should be, and it is left out anyway: the arc drawn on screen while the bow
        is drawn is PredictArc(), and a running archer would make the drawn arc a lie for as long
        as they kept running. Adding it to the preview too only moves the problem - the preview is
        drawn on the tick you look at it and the shot happens on the tick you release, and between
        those two the run speed has changed. A promise drawn on screen has to be keepable.
    */
    a.vel = dir * speed;
    a.angle = atan2f(a.vel.y,a.vel.x);
    a.f_live = true;
    a.f_stuck = false;

    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    arrows_shot++;

    events.f_shot = true;
    events.shot_power = power;
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

        //Kept for the app: the segment it needs to ask rp3d whether this arrow went through a
        //crate or a target on the way. See the handshake note on Stage::arrows.
        a.prev_pos = a.pos;

        a.vel.y -= ARROW_GRAVITY * ARCHER_DT;
        v2 next = a.pos + a.vel * ARCHER_DT;

        v2 point;
        v2 normal;
        int block = SegmentHitsBlock(a.pos,next,point,normal);
        if (block >= 0){
            float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y);
            //Backed off along the face so the shaft is embedded rather than coplanar with the
            //surface, which z-fights.
            a.pos = point + normal * 0.02f;
            a.vel = v2(0.0f,0.0f);
            a.f_stuck = true;
            a.age_ticks = 0;
            arrows_hit_blocks++;

            StageEvents::ArrowHit hit;
            hit.arrow = i;
            hit.point = point;
            hit.normal = normal;
            hit.speed = speed;
            hit.block = block;
            events.arrow_hits.push_back(hit);
            continue;
        }

        a.pos = next;
        a.angle = atan2f(a.vel.y,a.vel.x);
        a.age_ticks++;
        if (a.age_ticks > ARROW_MAX_AGE_TICKS || a.pos.y < -60.0f){
            a.f_live = false;
        }
    }
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

void Stage::StickArrow(int index, const v2& point){
    if (index < 0 || index >= ARROW_MAX_LIVE){
        return;
    }
    Arrow& a = arrows[index];
    a.pos = point;
    a.vel = v2(0.0f,0.0f);
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
*/
int Stage::SegmentHitsBlock(const v2& a, const v2& b, v2& out_point, v2& out_normal) const{
    v2 d = b - a;
    float best_t = 2.0f;
    int best = -1;
    v2 best_normal;

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

        float t_near = 0.0f;
        float t_far = 1.0f;
        v2 normal;
        bool f_miss = false;

        for (int axis = 0; axis < 2 && !f_miss; axis++){
            float da   = (axis == 0) ? d.x : d.y;
            float orig = (axis == 0) ? a.x : a.y;
            float lo   = (axis == 0) ? blk.Left()   : blk.Bottom();
            float hi   = (axis == 0) ? blk.Right()  : blk.Top();

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
                normal = (axis == 0) ? v2(n,0.0f) : v2(0.0f,n);
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
    The aim preview.

    Runs the SAME integration TickArrows does, at the same rate, with the same gravity, and stops
    at the first thing it would hit - so the dots are not a sketch of the flight, they are the
    flight, sampled. Any divergence between this function and TickArrows is a bug in one of them.
*/
int Stage::PredictArc(v2* out_points, int max_points) const{
    if (!out_points || max_points < 1){
        return 0;
    }

    float power = DrawPower();
    float speed = ARROW_SPEED_MIN + (ARROW_SPEED_MAX - ARROW_SPEED_MIN) * power;
    v2 p = MuzzlePosition();
    v2 v = AimDirection() * speed;

    int written = 0;
    int limit = (max_points < AIM_ARC_POINTS) ? max_points : AIM_ARC_POINTS;
    for (int i = 0; i < limit; i++){
        for (int s = 0; s < AIM_ARC_TICK_STRIDE; s++){
            v.y -= ARROW_GRAVITY * ARCHER_DT;
            v2 next = p + v * ARCHER_DT;
            v2 point;
            v2 normal;
            if (SegmentHitsBlock(p,next,point,normal) >= 0){
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
    static const char* mode_names[] = { "ground","air","hang","climb","rope" };
    char buf[256];
    snprintf(buf,sizeof(buf),
             "t=%llu %s pos=(%.2f,%.2f) vel=(%.2f,%.2f) face=%+.0f aim=%.0f draw=%d/%d arrows=%d/%d",
             (unsigned long long)ticks,
             mode_names[(mode >= 0 && mode <= MODE_ROPE) ? mode : 0],
             pos.x,pos.y,vel.x,vel.y,facing,aim_deg,draw_ticks,BOW_DRAW_TICKS,
             NumLiveArrows(),arrows_shot);
    return std::string(buf);
}
