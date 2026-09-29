/*
    The rules test.

        mingw32-make.exe rules       -> build/stage_test.exe, then runs it

    Builds against Stage.cpp ALONE - no core, no window, no GPU - which is possible only because
    Stage.h names no engine type, and which is the whole reason for that split. It takes about a
    second and is the cheapest way to find out whether a change to the feel constants broke
    something that depends on them.

    THE TESTS ARE WRITTEN AGAINST THE CONSTANTS, NOT AGAINST NUMBERS TYPED IN HERE. A jump is
    checked against v^2/2g derived from ARCHER_JUMP_SPEED and ARCHER_GRAVITY, not against "3.2",
    so retuning the feel does not turn this file red - only breaking the relationship does. The
    level layout is the exception and is meant to be: it is checked against the reach the
    constants produce, because a level that has drifted out of reach of its own jump is exactly
    the failure this is here to catch.
*/
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>

#include "Stage.h"
#include "Puppet.h"
#include "Foliage.h"
#include "Backdrop.h"
#include "Boulders.h"
#include "Vine.h"
#include "RopeMesh.h"
#include "RouteCheck.h"
#include "StateHash.h"

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool f_ok, const char* what, const char* detail = NULL){
    g_checks++;
    if (f_ok){
        //STAGE_TEST_VERBOSE=1 prints the numbers behind a pass too - the margins, when tuning.
        static const bool f_verbose = getenv("STAGE_TEST_VERBOSE") != NULL;
        printf("  ok    %s%s%s\n",what,(f_verbose && detail) ? " - " : "",(f_verbose && detail) ? detail : "");
        return;
    }
    g_failures++;
    printf("  FAIL  %s%s%s\n",what,detail ? " - " : "",detail ? detail : "");
}

//`extra` is appended to the got/wanted line, for the cases where the numbers alone do not say
//what went wrong - which surface was meant, what the reach was, and so on.
static void CheckNear(float got, float want, float tol, const char* what, const char* extra = NULL){
    char detail[260];
    snprintf(detail,sizeof(detail),"got %.4f, wanted %.4f +/- %.4f%s%s",
             got,want,tol,extra ? "; " : "",extra ? extra : "");
    Check(fabsf(got - want) <= tol,what,detail);
}

//Ticks the stage n times with one unchanging intent, discarding the events.
static void Run(Stage& s, int n, const ArcherInput& in){
    for (int i = 0; i < n; i++){
        StageEvents ev;
        s.Tick(in,ev);
    }
}

//Drops the archer onto whatever is underneath and leaves them standing still.
static void Settle(Stage& s){
    ArcherInput idle;
    for (int i = 0; i < 240 && !s.f_on_ground; i++){
        StageEvents ev;
        s.Tick(idle,ev);
    }
    Run(s,4,idle);
}

//--- What the constants imply, derived once and used by the level checks ------------------------
static float ApexRise(){
    return (ARCHER_JUMP_SPEED * ARCHER_JUMP_SPEED) / (2.0f * ARCHER_GRAVITY);
}
static float AirTicks(){
    float up = ARCHER_JUMP_SPEED / ARCHER_GRAVITY;
    float down = sqrtf(2.0f * ApexRise() / (ARCHER_GRAVITY * ARCHER_FALL_GRAVITY_MUL));
    return (up + down) * ARCHER_TPS;
}
static float GapReach(){
    return ARCHER_RUN_SPEED * (AirTicks() / ARCHER_TPS);
}

//--- The level ----------------------------------------------------------------------------------

static void TestLevel(){
    printf("level\n");
    Stage s;

    Check(s.blocks.size() >= 8,"the blockout has its geometry");

    int targets = 0;
    int walls = 0;
    int anchors = 0;
    int crates = 0;
    for (size_t i = 0; i < s.props.size(); i++){
        switch (s.props[i].kind){
            case PROP_TARGET:      targets++; break;
            case PROP_BRICKWALL:   walls++;   break;
            case PROP_ROPE_ANCHOR: anchors++; break;
            case PROP_CRATE:       crates++;  break;
            default: break;
        }
    }
    Check(targets == 4,"four targets, one per kind of shot");
    Check(walls == 1,"a brick wall for the kick slice");
    Check(anchors == 1,"a rope anchor for the rope slice");
    Check(crates >= 2,"crates to kick");

    /*
        Every standable surface is inside the jump, and the high ledge is outside it.

        This is the check that earns the whole file. The feel constants and the level are tuned by
        different people at different times, and the failure mode - a platform that quietly drifted
        0.3 units out of reach - is invisible in a screenshot and costs an hour to find by playing.
    */
    float feet = ApexRise();
    float hands = feet + ARCHER_HALF_H * 2.0f;
    int standable = 0;
    int grab_only = 0;
    for (size_t i = 0; i < s.blocks.size(); i++){
        const StageBlock& b = s.blocks[i];
        if (b.kind == BLOCK_SOLID || b.kind == BLOCK_BREAKABLE){
            continue;   //ground runs and walls are not meant to be landed on from below
        }
        if (b.tree >= 0){
            continue;   //a tree's arm is reached from the arm below, not the ground - TestTree climbs it
        }
        float top = b.Top();
        if (top <= feet){
            standable++;
        }else if (top < hands){
            grab_only++;
        }else{
            char detail[160];
            snprintf(detail,sizeof(detail),"a block's top is at %.2f, past the %.2f hands reach",top,hands);
            Check(false,"no platform is out of reach of even a grab",detail);
        }
    }
    Check(standable >= 2,"the one-way platform and the ledge can be jumped onto");
    Check(grab_only == 1,"exactly one ledge is grabbable-only, for the hang slice to aim at");
}

//--- Movement -----------------------------------------------------------------------------------

static void TestGroundAndRun(){
    printf("running\n");
    Stage s;
    Settle(s);
    Check(s.f_on_ground,"the archer lands on the start ground");
    CheckNear(s.pos.y,ARCHER_HALF_H,0.01f,"and stands with their feet on y = 0");

    ArcherInput right;
    right.move_axis = 1.0f;
    Run(s,20,right);
    CheckNear(s.vel.x,ARCHER_RUN_SPEED,0.05f,"a held run reaches full speed");
    Check(s.facing > 0.0f,"and faces the way it is running");

    //Reaching full speed should take about ARCHER_RUN_SPEED/ARCHER_RUN_ACCEL seconds - 6 ticks at
    //the shipped numbers. Checked as a bound rather than an equality: the point is that it is
    //responsive, not that it is exactly six.
    Stage s2;
    Settle(s2);
    int ticks_to_speed = 0;
    for (int i = 0; i < 60; i++){
        StageEvents ev;
        s2.Tick(right,ev);
        ticks_to_speed++;
        if (s2.vel.x >= ARCHER_RUN_SPEED - 0.01f){
            break;
        }
    }
    float expect = (ARCHER_RUN_SPEED / ARCHER_RUN_ACCEL) * ARCHER_TPS;
    char detail[160];
    snprintf(detail,sizeof(detail),"took %i ticks, expected about %.1f",ticks_to_speed,expect);
    Check((float)ticks_to_speed <= expect + 2.0f,"and gets there in the time the accel implies",detail);

    ArcherInput idle;
    Run(s,20,idle);
    CheckNear(s.vel.x,0.0f,0.01f,"letting go stops it");
}

static void TestJump(){
    printf("jumping\n");

    //--- A full, held jump ----------------------------------------------------------------------
    Stage s;
    Settle(s);
    float floor_y = s.pos.y;

    ArcherInput jump;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents ev;
    s.Tick(jump,ev);
    Check(ev.f_jumped,"pressing jump reports a jump");

    ArcherInput hold;
    hold.f_jump_down = true;
    float peak = s.pos.y;
    for (int i = 0; i < 120; i++){
        StageEvents e;
        s.Tick(hold,e);
        if (s.pos.y > peak){
            peak = s.pos.y;
        }
        if (s.f_on_ground){
            break;
        }
    }
    /*
        Against v^2/2g MINUS the half-step that semi-implicit Euler leaves on the table.

        An integrator that applies gravity and then moves loses about v*dt/2 of apex against the
        continuous formula - 0.137 units here, which is 4% of the jump and thousands of times
        larger than float error. Asserting the continuous value with a tolerance fat enough to
        swallow that would also swallow a real regression; asserting the discrete value catches a
        change of a centimetre. Widen this tolerance and you are hiding something.
    */
    float euler_loss = ARCHER_JUMP_SPEED * ARCHER_DT * 0.5f;
    CheckNear(peak - floor_y,ApexRise() - euler_loss,0.03f,
              "a held jump peaks where v^2/2g minus the Euler half-step says");
    Check(s.f_on_ground,"and comes back down");

    //--- A tapped jump ----------------------------------------------------------------------------
    Stage s2;
    Settle(s2);
    float floor2 = s2.pos.y;
    StageEvents e2;
    s2.Tick(jump,e2);
    Run(s2,2,hold);

    ArcherInput released;    //key up: the cut applies
    float peak2 = s2.pos.y;
    for (int i = 0; i < 120; i++){
        StageEvents e;
        s2.Tick(released,e);
        if (s2.pos.y > peak2){
            peak2 = s2.pos.y;
        }
        if (s2.f_on_ground){
            break;
        }
    }
    float tapped = peak2 - floor2;
    char detail[160];
    snprintf(detail,sizeof(detail),"tapped %.2f against a full %.2f",tapped,ApexRise());
    Check(tapped < ApexRise() * 0.55f,"a tapped jump is markedly shorter",detail);
    Check(tapped > 0.25f,"but still leaves the ground",detail);

    //--- Coyote time ------------------------------------------------------------------------------
    //Walk off the right-hand end of the first ground run and jump three ticks into the fall.
    Stage s3;
    s3.pos = v2(12.5f,ARCHER_HALF_H);
    Settle(s3);
    ArcherInput right;
    right.move_axis = 1.0f;
    int guard = 0;
    while (s3.f_on_ground && guard++ < 400){
        StageEvents e;
        s3.Tick(right,e);
    }
    Check(guard < 400,"the archer does walk off the edge");
    Run(s3,3,right);

    ArcherInput late = right;
    late.f_jump_pressed = true;
    late.f_jump_down = true;
    StageEvents e3;
    s3.Tick(late,e3);
    Check(e3.f_jumped,"a jump 3 ticks after walking off the edge still fires (coyote time)");

    //And that the grace really does run out, so it is a window and not a double jump.
    Stage s4;
    s4.pos = v2(12.5f,ARCHER_HALF_H);
    Settle(s4);
    guard = 0;
    while (s4.f_on_ground && guard++ < 400){
        StageEvents e;
        s4.Tick(right,e);
    }
    Run(s4,ARCHER_COYOTE_TICKS + 3,right);
    StageEvents e4;
    s4.Tick(late,e4);
    Check(!e4.f_jumped,"but not once the coyote window has passed");
}

static void TestJumpBuffer(){
    printf("jump buffer\n");

    //Find the tick the archer lands on by running a COPY, then re-run pressing jump shortly
    //before that. Stage is plain data and copies cleanly, which makes this kind of two-pass test
    //cheap - and it is the only honest way to test a buffer without hard-coding a fall time.
    Stage probe;
    Settle(probe);
    ArcherInput jump;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    ArcherInput hold;
    hold.f_jump_down = true;

    Stage s = probe;
    StageEvents ev;
    s.Tick(jump,ev);

    Stage copy = s;
    int land_tick = -1;
    for (int i = 0; i < 200; i++){
        StageEvents e;
        copy.Tick(hold,e);
        if (e.f_landed){
            land_tick = i;
            break;
        }
    }
    Check(land_tick > 10,"the probe jump lands");

    //Press three ticks before touching down. The buffer is ARCHER_JUMP_BUFFER_TICKS long and is
    //spent one per tick, so three is comfortably inside it.
    int press_at = land_tick - 3;
    bool f_jumped_again = false;
    for (int i = 0; i < land_tick + 6; i++){
        ArcherInput in = hold;
        if (i == press_at){
            in.f_jump_pressed = true;
        }
        StageEvents e;
        s.Tick(in,e);
        if (e.f_jumped){
            f_jumped_again = true;
            break;
        }
    }
    Check(f_jumped_again,"a jump pressed 3 ticks before landing fires on touchdown (buffered)");
}

static void TestGapAndPlatform(){
    printf("gaps and platforms\n");

    //--- The first gap, x 14 .. 19 ----------------------------------------------------------------
    /*
        Started well back and run up to the LIP rather than for a fixed number of ticks. A fixed
        run-up is tuning dressed as a test: twenty ticks covers 2.62 units at the shipped accel,
        so the first version of this walked clean off the edge before it ever pressed jump and
        then reported that the jump had failed. Running until the edge is in reach tests the jump
        whatever the acceleration is retuned to.
    */
    Stage s;
    s.pos = v2(9.50f,ARCHER_HALF_H);        //clear of the step, which ends at x 9
    Settle(s);
    ArcherInput right;
    right.move_axis = 1.0f;
    int runup = 0;
    while (s.pos.x < 13.50f && runup++ < 400){
        StageEvents e;
        s.Tick(right,e);
    }
    Check(runup < 400,"the archer reaches the lip of the gap");
    Check(s.f_on_ground,"and is still on the ground there");
    CheckNear(s.vel.x,ARCHER_RUN_SPEED,0.05f,"at a full run");

    ArcherInput jump = right;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents ev;
    s.Tick(jump,ev);
    Check(ev.f_jumped,"jumps at the lip of the gap");

    ArcherInput fly = right;
    fly.f_jump_down = true;
    for (int i = 0; i < 200 && !s.f_on_ground; i++){
        StageEvents e;
        s.Tick(fly,e);
    }
    char detail[160];
    snprintf(detail,sizeof(detail),"landed at x %.2f; the far lip is 19.00 and the reach is %.2f",
             s.pos.x,GapReach());
    Check(s.f_on_ground && s.pos.x > 19.0f + ARCHER_HALF_W,"a running jump clears the 5-unit gap",detail);

    //--- The one-way platform at x 21..26, top 2.8 -------------------------------------------------
    Stage p;
    p.pos = v2(23.5f,ARCHER_HALF_H);
    Settle(p);
    float ground_y = p.pos.y;
    StageEvents e2;
    p.Tick(jump,e2);        //straight up; move_axis is +1 but the platform is 5 wide
    ArcherInput up;
    up.f_jump_down = true;
    for (int i = 0; i < 200; i++){
        StageEvents e;
        p.Tick(up,e);
        if (e.f_landed){
            break;
        }
    }
    snprintf(detail,sizeof(detail),"ended at y %.2f, started at %.2f",p.pos.y,ground_y);
    Check(p.pos.y > ground_y + 2.0f,"a jump passes up through the one-way platform and lands on it",detail);

    //Holding Down drops back through it.
    ArcherInput down;
    down.f_down_held = true;
    Run(p,30,down);
    snprintf(detail,sizeof(detail),"ended at y %.2f",p.pos.y);
    Check(p.pos.y < ground_y + 0.5f,"holding Down drops back through it",detail);
}

//--- The bow ------------------------------------------------------------------------------------

static void TestBow(){
    printf("the bow\n");

    Stage s;
    Settle(s);

    //--- Nothing leaves the bow before the arrow is on it ------------------------------------------
    /*
        BOW_NOCK_TICKS: she spends the start of a draw reaching back to the quiver, and letting go
        then CANCELS the draw. This used to be the opposite on purpose - a tap fired a minimum
        shot so the main verb never swallowed a press - until the draw animation made it visible
        that the arrow was leaving an empty bow.
    */
    ArcherInput tap;
    tap.f_draw_down = true;
    tap.f_draw_released = true;      //pressed and let go inside one tick
    StageEvents ev;
    s.Tick(tap,ev);
    Check(!ev.f_shot && s.NumLiveArrows() == 0,"a press and release inside one tick looses nothing");
    Check(s.bow_mode == BOW_IDLE && s.draws_cancelled == 1,"it cancels the draw instead");

    ArcherInput hold;
    hold.f_draw_down = true;
    ArcherInput letgo;
    letgo.f_draw_released = true;
    Stage early;
    Settle(early);
    Run(early,BOW_NOCK_TICKS,hold);         //draw_ticks ends one short of the nock
    Check(!early.IsNocked(),"one tick short of BOW_NOCK_TICKS there is no arrow on the string");
    StageEvents ee0;
    early.Tick(letgo,ee0);
    Check(!ee0.f_shot && early.NumLiveArrows() == 0,"and letting go then shoots nothing");

    Stage nock;
    Settle(nock);
    Run(nock,BOW_NOCK_TICKS + 1,hold);
    Check(nock.IsNocked(),"at BOW_NOCK_TICKS the arrow is on the string");
    StageEvents en;
    nock.Tick(letgo,en);
    Check(en.f_shot && nock.NumLiveArrows() == 1,"and letting go looses it");
    CheckNear(en.shot_power,BOW_MIN_POWER,0.001f,"at the minimum power - the pull has only begun");

    //Power is the PULL, from the nock to full draw - what the string does on screen.
    Stage half;
    Settle(half);
    half.bow_mode = BOW_DRAWING;
    half.draw_ticks = (BOW_NOCK_TICKS + BOW_DRAW_TICKS) / 2;
    float want = BOW_MIN_POWER + (1.0f - BOW_MIN_POWER) *
                 (float)(half.draw_ticks - BOW_NOCK_TICKS) / (float)(BOW_DRAW_TICKS - BOW_NOCK_TICKS);
    CheckNear(half.DrawPower(),want,0.0001f,"halfway through the pull, power is halfway from the minimum");

    //--- A full draw is stronger -------------------------------------------------------------------
    Stage f;
    Settle(f);
    ArcherInput draw;
    draw.f_draw_down = true;
    Run(f,BOW_DRAW_TICKS + 10,draw);
    Check(f.bow_mode == BOW_DRAWING,"holding draws the bow");
    Check(f.draw_ticks == BOW_DRAW_TICKS,"and the draw caps at a full pull");
    CheckNear(f.DrawPower(),1.0f,0.001f,"which is full power");

    //--- The aim arc IS the flight -----------------------------------------------------------------
    /*
        The single most important assertion in this file.

        PredictArc is what gets drawn on screen while the bow is drawn, and the entire argument for
        integrating arrows by hand rather than handing them to reactphysics3d is that the preview
        and the flight are then the same function. If this drifts, that argument is void and the
        game is lying to the player about where their arrow will go.
    */
    v3 arc[AIM_ARC_POINTS];
    int n = f.PredictArc(arc,AIM_ARC_POINTS);
    Check(n >= 4,"the preview produces an arc");

    ArcherInput loose;
    loose.f_draw_released = true;
    StageEvents e;
    f.Tick(loose,e);
    Check(e.f_shot,"and releasing looses it");

    //Find the arrow that was just made.
    int idx = -1;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (f.arrows[i].f_live){
            idx = i;
            break;
        }
    }
    Check(idx >= 0,"the loosed arrow is live");

    /*
        The arrow integrates once on the tick it is born (Loose runs before TickArrows), and
        PredictArc writes a point every AIM_ARC_TICK_STRIDE steps. So arc point k is the arrow's
        position after (k+1)*stride steps, which is (k+1)*stride - 1 further ticks from here.
    */
    ArcherInput idle;
    int compared = 0;
    float worst = 0.0f;
    for (int k = 0; k < n && k < 6; k++){
        int want_steps = (k + 1) * AIM_ARC_TICK_STRIDE;
        while (compared < want_steps - 1 && f.arrows[idx].f_live && !f.arrows[idx].f_stuck){
            StageEvents ee;
            f.Tick(idle,ee);
            compared++;
        }
        if (!f.arrows[idx].f_live || f.arrows[idx].f_stuck){
            break;      //the arc stopped at geometry and so did the arrow; that is agreement
        }
        v3 a = f.arrows[idx].pos;
        float dx = a.x - arc[k].x;
        float dy = a.y - arc[k].y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > worst){
            worst = d;
        }
    }
    char detail[160];
    snprintf(detail,sizeof(detail),"worst divergence %.6f units over %i sampled ticks",worst,compared);
    Check(worst < 0.0005f,"the drawn aim arc IS the arrow's flight, tick for tick",detail);

    //--- A fast arrow does not tunnel --------------------------------------------------------------
    /*
        The cracked wall is 1.0 thick and a full-draw arrow covers ARROW_SPEED_MAX/ARCHER_TPS in a
        tick. Those numbers are close enough that a per-tick overlap test would pass this
        SOMETIMES, which is why SegmentHitsBlock sweeps.
    */
    Stage t;
    t.pos = v2(54.0f,ARCHER_HALF_H);
    Settle(t);
    t.aim_deg = 0.0f;
    Run(t,BOW_DRAW_TICKS + 4,draw);
    StageEvents te;
    t.Tick(loose,te);
    for (int i = 0; i < 120; i++){
        StageEvents ee;
        t.Tick(idle,ee);
    }
    bool f_any_past = false;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (t.arrows[i].f_live && t.arrows[i].pos.x > 57.6f){
            f_any_past = true;
        }
    }
    snprintf(detail,sizeof(detail),"per-tick step is %.3f against a 1.00 thick wall",
             ARROW_SPEED_MAX / ARCHER_TPS);
    Check(!f_any_past,"a full-draw arrow does not tunnel through the cracked wall",detail);
    Check(t.arrows_hit_blocks > 0,"it registers as a hit on the wall instead");

    //--- An arrow is born as a SEGMENT, nock to tip ------------------------------------------------
    /*
        Standing pressed against something and shooting into it. The arrow's tip starts a full
        arrow length ahead of the nock, and the nock is in front of her chest - so the tip can be
        born INSIDE a block, or clean PAST a thin one. A point that only starts sweeping from where
        it was born would bury itself in the first case and fly through in the second.

        A pillar is the thin case - 0.5 wide, thinner than an arrow is long - with her pressed
        against its near face. A level full-draw shot must stick IN THAT FACE: not inside the
        pillar, not beyond it. The pillar is this test's own, stood on open ground: it used to be
        the terrain bay's, and went when the bay was re-laid as a level.
    */
    {
        const float pillar_x = -8.0f;
        float pillar_left = pillar_x - 0.25f;
        float pillar_right = pillar_x + 0.25f;
        Stage p;
        p.blocks.push_back({ pillar_x, 1.00f, 0.25f, 1.00f, BLOCK_SOLID, true });
        p.pos = v2(pillar_left - ARCHER_HALF_W - 0.01f,ARCHER_HALF_H);
        p.facing = 1.0f;
        Settle(p);
        p.aim_deg = 0.0f;
        Run(p,BOW_DRAW_TICKS + 4,draw);
        StageEvents pe;
        p.Tick(loose,pe);
        int shot = -1;
        for (int i = 0; i < ARROW_MAX_LIVE; i++){
            if (p.arrows[i].f_live){ shot = i; }
        }
        Check(shot >= 0 && p.arrows[shot].f_stuck,
              "pressed against a thin pillar, a level shot sticks on the tick it is loosed");
        char pd[160];
        snprintf(pd,sizeof(pd),"stuck at x %.3f; the pillar spans %.3f .. %.3f",
                 (shot >= 0) ? p.arrows[shot].pos.x : 0.0f,pillar_left,pillar_right);
        Check(shot >= 0 && p.arrows[shot].pos.x <= pillar_left + 0.001f,
              "in the pillar's NEAR face - not buried inside it, not through it",pd);
    }

    //--- Aim is relative to facing -----------------------------------------------------------------
    Stage m;
    Settle(m);
    m.aim_deg = 30.0f;
    m.facing = 1.0f;
    v3 r = m.AimDirection();
    m.facing = -1.0f;
    v3 l = m.AimDirection();
    CheckNear(r.y,l.y,0.0001f,"facing left mirrors the aim rather than inverting it");
    CheckNear(r.x,-l.x,0.0001f,"and flips only the horizontal");
    Check(r.y > 0.0f,"a positive aim angle points upward in both directions");
}

//--- Hanging and climbing -------------------------------------------------------------------------

//The one ledge in the level that is grabbable-only: too high to land on, low enough to catch.
static const StageBlock& HighLedge(const Stage& s){
    float feet = ApexRise();
    for (size_t i = 0; i < s.blocks.size(); i++){
        if (s.blocks[i].kind == BLOCK_LEDGE && s.blocks[i].Top() > feet){
            return s.blocks[i];
        }
    }
    return s.blocks[0];
}

static void TestLedge(){
    printf("ledges\n");

    Stage probe;
    const StageBlock& ledge = HighLedge(probe);
    char detail[200];

    ArcherInput right;
    right.move_axis = 1.0f;
    ArcherInput fly = right;
    fly.f_jump_down = true;
    ArcherInput jump = fly;
    jump.f_jump_pressed = true;
    StageEvents ev;

    /*
        Stand against the high ledge's face and jump, holding INTO it.

        No run-up, and that is not laziness - the ground this ledge stands on only starts 4.5 units
        to its left, so there is nowhere to run from. It does not need one: the hands reach 5.00
        against a 4.20 lip, so a standing jump carries them well past it, and the archer presses
        against the face and catches it coming back down. Which is also the honest thing to test,
        because "jump straight up and catch the thing above you" is what a player will try first.
    */
    Stage s;
    s.pos = v2(ledge.Left() - ARCHER_HALF_W - 0.5f,ARCHER_HALF_H);
    Settle(s);
    snprintf(detail,sizeof(detail),"standing at x %.2f, y %.2f",s.pos.x,s.pos.y);
    Check(s.f_on_ground,"the archer starts on the ground below the high ledge",detail);

    s.Tick(jump,ev);
    Check(ev.f_jumped,"and jumps at it");

    bool f_grabbed = false;
    for (int i = 0; i < 200; i++){
        StageEvents e;
        s.Tick(fly,e);
        if (e.f_grabbed_ledge){
            f_grabbed = true;
            break;
        }
        if (s.f_on_ground){
            break;
        }
    }
    snprintf(detail,sizeof(detail),"ended at (%.2f,%.2f) mode %i; the lip is at %.2f and feet reach %.2f",
             s.pos.x,s.pos.y,s.mode,ledge.Top(),ApexRise());
    Check(f_grabbed,"a jump at a lip too high to land on CATCHES it",detail);
    Check(s.mode == MODE_HANG,"and leaves the archer hanging",detail);

    //The pose is exact, which everything downstream relies on.
    CheckNear(s.pos.y + ARCHER_HALF_H + LEDGE_HANG_DROP,ledge.Top(),0.01f,
              "with the body hung LEDGE_HANG_DROP below the lip, which puts her fingers on it");
    CheckNear(s.pos.x + ARCHER_HALF_W,ledge.Left(),0.01f,"and the body flat against the face");
    Check(s.facing > 0.0f,"facing the wall it caught");

    //Hanging is stable: no gravity, no drift, indefinitely.
    v2 held = s.pos;
    ArcherInput idle;
    Run(s,120,idle);
    Check(s.mode == MODE_HANG,"hanging holds with no input");
    Check(s.pos.x == held.x && s.pos.y == held.y,"and does not drift by even a float");

    //A draw cannot be started with both hands on the rock.
    ArcherInput draw;
    draw.f_draw_down = true;
    Run(s,10,draw);
    Check(s.bow_mode == BOW_IDLE,"and the bow cannot be drawn from a hang");

    //--- Climbing ---------------------------------------------------------------------------------
    ArcherInput up;
    up.f_jump_pressed = true;
    StageEvents ce;
    s.Tick(up,ce);
    Check(s.mode == MODE_CLIMB,"jump from a hang starts the climb");

    //Along the clip's path: halfway through the move, halfway through the table.
    v2 from = s.climb_from;
    v2 to = s.climb_to;
    bool f_climbed = false;
    int climb_took = 0;
    for (int i = 0; i < LEDGE_CLIMB_TICKS + 30; i++){
        StageEvents e;
        s.Tick(idle,e);
        climb_took++;
        if (i + 1 == LEDGE_CLIMB_TICKS / 2){
            const int mid = (LEDGE_CLIMB_PATH_SAMPLES - 1) / 2;
            CheckNear(s.pos.y,from.y + (to.y - from.y) * LEDGE_CLIMB_UP[mid],1e-4f,
                      "halfway, she has risen as far as the clip's hips have");
            CheckNear(s.pos.x,from.x + (to.x - from.x) * LEDGE_CLIMB_ACROSS[mid],1e-4f,
                      "and stepped across as far");
        }
        if (e.f_climbed){
            f_climbed = true;
            break;
        }
    }
    Check(f_climbed,"which finishes");
    Check(climb_took == LEDGE_CLIMB_TICKS,"in exactly LEDGE_CLIMB_TICKS");
    CheckNear(fabsf(s.pos.x - from.x) - ARCHER_HALF_W,LEDGE_CLIMB_INSET,0.01f,
              "standing LEDGE_CLIMB_INSET past the lip, where the clip's hips finish");
    Check(s.mode == MODE_GROUND && s.f_on_ground,"standing on the ledge");
    CheckNear(s.pos.y - ARCHER_HALF_H,ledge.Top(),0.02f,"with the feet on its top surface");
    Check(s.pos.x > ledge.Left() && s.pos.x < ledge.Right(),"and the body over the block, not past it");

    //And it stays there - a climb that ends in a position overlapping the block would be ejected.
    Run(s,60,idle);
    Check(s.f_on_ground,"and it is still standing there a second later");
    CheckNear(s.pos.y - ARCHER_HALF_H,ledge.Top(),0.02f,"at the same height");

    //--- Letting go --------------------------------------------------------------------------------
    Stage d;
    d.pos = v2(ledge.Left() - ARCHER_HALF_W - 0.5f,ARCHER_HALF_H);
    Settle(d);
    d.Tick(jump,ev);
    for (int i = 0; i < 200 && d.mode != MODE_HANG; i++){
        StageEvents e;
        d.Tick(fly,e);
    }
    Check(d.mode == MODE_HANG,"a second archer catches the same lip");

    ArcherInput drop;
    drop.f_down_held = true;
    StageEvents de;
    d.Tick(drop,de);
    Check(de.f_released_ledge && d.mode == MODE_AIR,"holding Down lets go");
    //The cooldown is the whole reason letting go works at all.
    Run(d,6,idle);
    Check(d.mode == MODE_AIR,"and it does not instantly re-grab the lip it just left");
    Run(d,180,idle);
    Check(d.f_on_ground,"the archer falls back to the ground");

    //--- Choosing to miss it -----------------------------------------------------------------------
    Stage m;
    m.pos = v2(ledge.Left() - ARCHER_HALF_W - 0.5f,ARCHER_HALF_H);
    Settle(m);
    m.Tick(jump,ev);
    ArcherInput away;
    away.move_axis = -1.0f;         //holding back from the wall
    away.f_jump_down = true;
    bool f_caught = false;
    for (int i = 0; i < 200; i++){
        StageEvents e;
        m.Tick(away,e);
        if (e.f_grabbed_ledge){
            f_caught = true;
            break;
        }
        if (m.f_on_ground){
            break;
        }
    }
    Check(!f_caught,"holding away from the lip refuses the grab");

    //--- A jump that CAN be made must not be stolen -------------------------------------------------
    /*
        The rule that earns the falling-only condition. A standable platform is landed ON; the hands
        cross its lip on the way up, and if a rising archer could grab, every such jump would snag.
    */
    const StageBlock* low = NULL;
    for (size_t i = 0; i < probe.blocks.size(); i++){
        //One standing UP out of the floor, that she jumps onto: not a rim level with the ground,
        //like the stepping stones' far side, whose top is 0 and whose lip is caught from a pit.
        if (probe.blocks[i].kind == BLOCK_LEDGE && probe.blocks[i].Top() <= ApexRise() &&
            probe.blocks[i].Top() > 0.5f){
            low = &probe.blocks[i];
        }
    }
    Check(low != NULL,"the level has a standable ledge to test that against");
    if (low){
        Stage t;
        t.pos = v2(low->Left() - 7.0f,ARCHER_HALF_H);
        Settle(t);
        int guard = 0;
        while (t.pos.x < low->Left() - 3.0f && guard++ < 400){
            StageEvents e;
            t.Tick(right,e);
        }
        t.Tick(jump,ev);
        bool f_snagged = false;
        for (int i = 0; i < 200; i++){
            StageEvents e;
            t.Tick(fly,e);
            if (e.f_grabbed_ledge){
                f_snagged = true;
            }
            if (t.f_on_ground){
                break;
            }
        }
        snprintf(detail,sizeof(detail),"its top is %.2f against a %.2f reach",low->Top(),ApexRise());
        Check(!f_snagged,"a ledge low enough to land on is landed on, not grabbed",detail);
        Check(t.f_on_ground && t.pos.y - ARCHER_HALF_H > low->Top() - 0.05f,
              "and the archer ends up standing on top of it",detail);
    }
}

//--- Props that block -----------------------------------------------------------------------------

//Ticks with one obstacle re-declared every tick, which is how the app drives it: the boxes are
//rebuilt from the bodies' live positions before each Tick, never left standing from the last one.
static void RunWithObstacle(Stage& s, int n, const ArcherInput& in,
                            float x, float y, float hw, float hh, int id, bool f_pushable,
                            StageEvents* out_last = NULL){
    for (int i = 0; i < n; i++){
        s.ClearObstacles();
        s.AddObstacle(x,y,hw,hh,id,f_pushable);
        StageEvents ev;
        s.Tick(in,ev);
        if (out_last){
            *out_last = ev;
        }
    }
}

static void TestObstacles(){
    printf("props that block\n");
    char detail[200];

    ArcherInput right;
    right.move_axis = 1.0f;
    ArcherInput idle;

    //--- A crate stops you --------------------------------------------------------------------
    Stage s;
    Settle(s);
    float crate_x = s.pos.x + 3.0f;
    StageEvents last;
    RunWithObstacle(s,90,right,crate_x,0.40f,0.40f,0.40f,7,true,&last);

    //Stopped with the body flat against the crate's near face - wherever the crate has been
    //shoved to by then, which is the point of re-reading it every tick.
    snprintf(detail,sizeof(detail),"archer at %.2f, crate face at %.2f",s.pos.x,crate_x - 0.40f);
    Check(s.pos.x <= crate_x - 0.40f - ARCHER_HALF_W + 0.01f,"a crate stops the archer walking into it",detail);
    Check(s.f_on_ground,"who is still on the ground");

    //--- ...and is shoved ----------------------------------------------------------------------
    Check(last.pushes.size() > 0,"and walking into it reports a push");
    if (last.pushes.size() > 0){
        Check(last.pushes[0].id == 7,"naming the obstacle by the id the app gave it");
        Check(last.pushes[0].dir > 0.0f,"in the direction of travel");
        snprintf(detail,sizeof(detail),"%.2f, capped at %.2f",last.pushes[0].speed,ARCHER_PUSH_SPEED);
        Check(last.pushes[0].speed > 0.1f && last.pushes[0].speed <= ARCHER_PUSH_SPEED + 0.001f,
              "at no more than the push speed",detail);
    }
    //The archer's own speed is held down to the push speed too - they are walking behind a crate,
    //not running through one.
    snprintf(detail,sizeof(detail),"vel.x %.2f against a %.2f cap",s.vel.x,ARCHER_PUSH_SPEED);
    Check(s.vel.x <= ARCHER_PUSH_SPEED + 0.01f,"and the archer slows to the pace of what they are pushing",detail);

    //--- A static prop stops you dead -----------------------------------------------------------
    Stage w;
    Settle(w);
    float wall_x = w.pos.x + 3.0f;
    StageEvents wlast;
    RunWithObstacle(w,90,right,wall_x,0.75f,0.40f,0.75f,3,false,&wlast);
    Check(wlast.pushes.size() == 0,"an unpushable prop reports no push");
    CheckNear(w.vel.x,0.0f,0.01f,"and stops the archer dead");
    snprintf(detail,sizeof(detail),"archer at %.2f, face at %.2f",w.pos.x,wall_x - 0.40f);
    CheckNear(w.pos.x,wall_x - 0.40f - ARCHER_HALF_W - 0.001f,0.02f,"flat against its face");

    //--- You can stand on one -------------------------------------------------------------------
    /*
        Not a feature that was written - it falls out of resolving the obstacle on the Y axis too,
        which is exactly why the crates by the start are stacked two high.
    */
    Stage t;
    Settle(t);
    float box_x = t.pos.x;
    //Dropped straight onto it rather than jumped at it. A running jump clears 6.5 units, so
    //aiming one at a 0.8-wide crate is a test of the jump arc, not of the thing being tested -
    //the first version of this sailed clean over the crate and landed on the ground beyond.
    t.pos = v2(box_x,3.0f);
    t.vel = v2(0.0f,0.0f);
    //Unpushable, so the archer cannot shove it out from under themselves on the way down.
    RunWithObstacle(t,120,idle,box_x,0.40f,0.40f,0.40f,1,false);
    snprintf(detail,sizeof(detail),"ended at y %.2f; the crate's top is 0.80",t.pos.y);
    Check(t.f_on_ground,"an archer dropped onto a crate lands on it",detail);
    CheckNear(t.pos.y - ARCHER_HALF_H,0.80f,0.05f,"standing on top of it");
    //And stays - a floor that only holds for the tick of the landing is the classic failure here.
    RunWithObstacle(t,60,idle,box_x,0.40f,0.40f,0.40f,1,false);
    CheckNear(t.pos.y - ARCHER_HALF_H,0.80f,0.05f,"and is still standing on it a second later");

    //--- A crate that comes to YOU must not carry you up ------------------------------------------
    /*
        THE REGRESSION THIS FILE MISSED FIRST TIME ROUND, so it is worth stating what it was.

        A crate shoved into a wall rebounds back into the archer. The archer is standing still, so
        there is no horizontal movement to resolve - and when the X pass only ran for a non-zero
        step, it skipped. The Y pass then ran, as it always does because gravity always does, found
        the overlap and resolved it the only way it knows how: by standing the archer on top. In
        the running game that came out as the archer riding up a stack of crates without ever
        pressing jump, 0.90 -> 1.70 -> 2.50.

        Reproduced by walking the obstacle INTO a stationary archer, a little each tick. The motion
        is the mechanism, so a box declared at a fixed spot tests nothing - the first version of
        this did exactly that and passed happily against the broken code. Checked both ways round:
        revert the X-pass fix and the second assertion here fails, with the crate having walked
        clean through the archer. The Y-pass guard is belt and braces for the case the X pass
        cannot settle on its own - two obstacles, where resolving against one leaves the archer
        inside the other - and is not what this particular test is holding down.
    */
    Stage r;
    Settle(r);
    float ground_y = r.pos.y;
    float highest = r.pos.y;
    //The crate WALKS INTO the archer, a little each tick, which a fixed box cannot imitate - and
    //the motion is the whole mechanism, so a stationary obstacle here tests nothing. It starts
    //clear to the right and closes in; the archer presses no key at all.
    float closing_x = r.pos.x + 1.60f;
    for (int i = 0; i < 90; i++){
        closing_x -= 0.05f;
        r.ClearObstacles();
        r.AddObstacle(closing_x,0.40f,0.40f,0.40f,4,true);
        StageEvents ev;
        r.Tick(idle,ev);
        if (r.pos.y > highest){
            highest = r.pos.y;
        }
    }
    snprintf(detail,sizeof(detail),"rose to %.2f from %.2f; the crate's top is 0.80",highest,ground_y);
    CheckNear(highest,ground_y,0.02f,"a crate shoved INTO a standing archer never carries them up",detail);
    snprintf(detail,sizeof(detail),"archer at %.2f, crate face at %.2f",r.pos.x,closing_x - 0.40f);
    Check(r.pos.x <= closing_x - 0.40f - ARCHER_HALF_W + 0.01f,"it pushes them along the ground instead",detail);

    //--- Obstacles are per tick, not persistent ---------------------------------------------------
    //The one way to misuse this is to leave them standing, so the fact that they evaporate is
    //worth asserting rather than assuming.
    Stage c;
    Settle(c);
    c.ClearObstacles();
    c.AddObstacle(c.pos.x + 1.0f,0.40f,0.40f,0.40f,1,false);
    Check(c.obstacles.size() == 1,"an obstacle can be declared");
    c.ClearObstacles();
    Check(c.obstacles.size() == 0,"and clearing removes it");
    float before = c.pos.x;
    Run(c,40,right);
    Check(c.pos.x > before + 1.5f,"with none declared, the archer walks straight past where it was");
}

//--- The kick -------------------------------------------------------------------------------------

//The cracked wall, found by kind rather than by the x it happens to sit at today.
static const StageBlock* FindBreakable(const Stage& s){
    for (size_t i = 0; i < s.blocks.size(); i++){
        if (s.blocks[i].kind == BLOCK_BREAKABLE && s.blocks[i].f_alive){
            return &s.blocks[i];
        }
    }
    return NULL;
}

static void TestKick(){
    printf("the kick\n");
    char detail[220];

    ArcherInput idle;
    ArcherInput kick;
    kick.f_kick_pressed = true;

    //--- It connects on a delay, not instantly ----------------------------------------------------
    Stage s;
    Settle(s);
    StageEvents ev;
    s.Tick(kick,ev);
    Check(ev.f_kick_started,"pressing kick starts one");
    Check(!ev.f_kick_connected,"which does not connect on the same tick - there is a wind-up");

    //--- Breaking the cracked wall ----------------------------------------------------------------
    Stage b;
    const StageBlock* wall = FindBreakable(b);
    Check(wall != NULL,"the level has a breakable wall to kick");
    if (wall){
        float wall_left = wall->Left();
        //Stand against its near face, facing it.
        b.pos = v2(wall_left - ARCHER_HALF_W - 0.05f,ARCHER_HALF_H);
        Settle(b);
        ArcherInput face_it;
        face_it.move_axis = 1.0f;
        Run(b,2,face_it);
        Check(b.facing > 0.0f,"the archer faces the wall");

        bool f_broke = false;
        int broke_index = -1;
        StageEvents kev;
        b.Tick(kick,kev);
        for (int i = 0; i < KICK_TICKS + 4 && !f_broke; i++){
            StageEvents e;
            b.Tick(idle,e);
            if (e.broken_blocks.size() > 0){
                f_broke = true;
                broke_index = e.broken_blocks[0];
            }
        }
        snprintf(detail,sizeof(detail),"wall face at %.2f, archer at %.2f",wall_left,b.pos.x);
        Check(f_broke,"a kick breaks the cracked wall",detail);
        if (broke_index >= 0){
            Check(!b.blocks[broke_index].f_alive,"and the block is marked dead");
        }

        //Which is the point of breaking it: the way is now open.
        ArcherInput right;
        right.move_axis = 1.0f;
        Run(b,150,right);
        snprintf(detail,sizeof(detail),"reached %.2f; the wall stood at %.2f",b.pos.x,wall_left);
        Check(b.pos.x > wall_left + 2.0f,"and the archer can now walk through where it stood",detail);
        //Height check too - walking THROUGH, not over the rubble.
        CheckNear(b.pos.y,ARCHER_HALF_H,0.1f,"at ground level",detail);
    }

    //--- A wall still standing stops you ----------------------------------------------------------
    Stage n;
    const StageBlock* wall2 = FindBreakable(n);
    if (wall2){
        float wall_left = wall2->Left();
        n.pos = v2(wall_left - 4.0f,ARCHER_HALF_H);
        Settle(n);
        ArcherInput right;
        right.move_axis = 1.0f;
        Run(n,150,right);
        snprintf(detail,sizeof(detail),"stopped at %.2f against a face at %.2f",n.pos.x,wall_left);
        Check(n.pos.x < wall_left,"without kicking it, the same wall stops the archer",detail);
    }

    //--- Kicking a prop ---------------------------------------------------------------------------
    Stage p;
    Settle(p);
    ArcherInput face_right;
    face_right.move_axis = 1.0f;
    Run(p,2,face_right);
    //A crate just within reach of the boot.
    float crate_x = p.pos.x + ARCHER_HALF_W + 0.35f;
    bool f_kicked = false;
    StageEvents::StageKick got;
    for (int i = 0; i < KICK_TICKS + 4 && !f_kicked; i++){
        p.ClearObstacles();
        p.AddObstacle(crate_x,0.40f,0.40f,0.40f,9,true);
        StageEvents e;
        p.Tick(i == 0 ? kick : idle,e);
        if (e.kicks.size() > 0){
            f_kicked = true;
            got = e.kicks[0];
        }
    }
    Check(f_kicked,"a kick connects with a prop in front of the archer");
    if (f_kicked){
        Check(got.id == 9,"naming it by the id the app gave it");
        Check(got.dir > 0.0f,"in the direction the archer is facing");
    }

    //--- ...but not one behind them ---------------------------------------------------------------
    Stage back;
    Settle(back);
    Run(back,2,face_right);
    float behind_x = back.pos.x - ARCHER_HALF_W - 0.35f;
    bool f_hit_behind = false;
    for (int i = 0; i < KICK_TICKS + 4; i++){
        back.ClearObstacles();
        back.AddObstacle(behind_x,0.40f,0.40f,0.40f,9,true);
        StageEvents e;
        back.Tick(i == 0 ? kick : idle,e);
        if (e.kicks.size() > 0){
            f_hit_behind = true;
        }
    }
    Check(!f_hit_behind,"and never one behind them");

    //--- One connect per kick ---------------------------------------------------------------------
    /*
        The boot is live for KICK_ACTIVE_TO - KICK_ACTIVE_FROM + 1 ticks, which is what makes a kick
        aimed at something actually connect with it. Without closing the window on the first hit,
        that is five impulses into the same crate and a crate that leaves the level.
    */
    Stage once;
    Settle(once);
    Run(once,2,face_right);
    float near_x = once.pos.x + ARCHER_HALF_W + 0.35f;
    int connects = 0;
    for (int i = 0; i < KICK_TICKS + 6; i++){
        once.ClearObstacles();
        once.AddObstacle(near_x,0.40f,0.40f,0.40f,9,true);
        StageEvents e;
        once.Tick(i == 0 ? kick : idle,e);
        connects += (int)e.kicks.size();
    }
    snprintf(detail,sizeof(detail),"connected %i times in one kick",connects);
    Check(connects == 1,"one kick is one connect, however long the boot is out",detail);

    //--- The cooldown ------------------------------------------------------------------------------
    Stage cd;
    Settle(cd);
    StageEvents c1;
    cd.Tick(kick,c1);
    Check(c1.f_kick_started,"the first kick starts");
    StageEvents c2;
    cd.Tick(kick,c2);
    Check(!c2.f_kick_started,"a second on the very next tick does not");
    Run(cd,KICK_TICKS + KICK_COOLDOWN + 2,idle);
    StageEvents c3;
    cd.Tick(kick,c3);
    Check(c3.f_kick_started,"but one after the cooldown does");

    //--- A grounded kick plants the feet ------------------------------------------------------------
    Stage plant;
    Settle(plant);
    Run(plant,30,face_right);
    float running = plant.vel.x;
    StageEvents pev;
    plant.Tick(kick,pev);
    Run(plant,4,face_right);
    snprintf(detail,sizeof(detail),"was running at %.2f, now %.2f",running,plant.vel.x);
    Check(plant.vel.x < running * 0.7f,"kicking on the ground plants the feet",detail);
    //And the facing is frozen with them, or the boot swings through 180 degrees mid-kick.
    ArcherInput face_left;
    face_left.move_axis = -1.0f;
    Run(plant,3,face_left);
    Check(plant.facing > 0.0f,"and the facing is frozen while it plays out",detail);

    //--- The three kicks ---------------------------------------------------------------------------
    /*
        K, Down+K and Up+K, read off the aim axis on the press - see THE THREE KICKS in Stage.h.
        Kick_Front's row must BE the defines, or everything above tested a kick nobody plays.
    */
    Check(KICK_SPECS[KICK_FRONT].ticks == KICK_TICKS &&
          KICK_SPECS[KICK_FRONT].active_from == KICK_ACTIVE_FROM &&
          KICK_SPECS[KICK_FRONT].active_to == KICK_ACTIVE_TO &&
          KICK_SPECS[KICK_FRONT].speed == KICK_SPEED && KICK_SPECS[KICK_FRONT].lift == KICK_LIFT,
          "K's kick is the KICK_* defines");
    {
        const float axis[KICK_KIND_COUNT] = { 0.0f, -1.0f, 1.0f };
        const char* picks[KICK_KIND_COUNT] = { "K alone throws Kick_Front", "Down+K throws Kick_Front_2",
                                               "Up+K throws Kick_Front_3" };
        for (int kind = 0; kind < KICK_KIND_COUNT; kind++){
            const KickSpec& spec = KICK_SPECS[kind];
            Check(spec.active_from >= 1 && spec.active_from <= spec.active_to && spec.active_to <= spec.ticks,
                  "its window is inside its move",spec.name);

            //The direction is let go of on the very next tick: the kick must stay the one chosen.
            Stage k;
            Settle(k);
            ArcherInput press = kick;
            press.aim_axis = axis[kind];
            StageEvents e;
            k.Tick(press,e);
            Check(e.f_kick_started && k.kick_kind == kind,picks[kind]);
            int lasted = (k.kick_ticks > 0) ? 1 : 0;
            bool f_kept = true;
            for (int i = 0; i < 200 && k.kick_ticks > 0; i++){
                StageEvents e2;
                k.Tick(idle,e2);
                if (k.kick_ticks > 0){
                    lasted++;
                    f_kept = f_kept && (k.kick_kind == kind);
                }
            }
            snprintf(detail,sizeof(detail),"lasted %i ticks, its row says %i",lasted,spec.ticks);
            Check(lasted == spec.ticks,"and runs its own length",detail);
            Check(f_kept,"and stays that kick after the direction is let go",spec.name);

            //Connects inside its own window, and says which kick it was.
            Stage c;
            Settle(c);
            Run(c,2,face_right);
            float x = c.pos.x + ARCHER_HALF_W + 0.35f;
            int hit_at = -1;
            int hit_kind = -1;
            for (int i = 0; i < spec.ticks + 4 && hit_at < 0; i++){
                c.ClearObstacles();
                c.AddObstacle(x,0.40f,0.40f,0.40f,9,true);
                StageEvents ce;
                int before = c.kick_ticks;          //read first: a connect jumps it past the window
                c.Tick(i == 0 ? press : idle,ce);
                if (ce.kicks.size() > 0){
                    hit_at = before + 1;
                    hit_kind = ce.kicks[0].kind;
                }
            }
            snprintf(detail,sizeof(detail),"%s connected on tick %i, window %i..%i",
                     spec.name,hit_at,spec.active_from,spec.active_to);
            Check(hit_at == spec.active_from,"connects with a crate on the first tick of its window",detail);
            Check(hit_kind == kind,"and reports which kick it was",spec.name);
        }

        /*
            THE BOXES ARE WHERE THE BOOTS ARE. Something at head height - a crate on a stack, a
            board on a ledge - is only reached by the high kick; something at the ankle is stepped
            over by it and caught by the other two. Heights are from the body's centre, pos.y.
        */
        const float probe_y[2] = { 0.75f, -0.80f };     //head, ankle
        const bool  reached[2][KICK_KIND_COUNT] = { { false, false, true },     //head
                                                    { true,  true,  false } };  //ankle
        const char* where[2] = { "at head height", "at the ankle" };
        for (int p2 = 0; p2 < 2; p2++){
            for (int kind = 0; kind < KICK_KIND_COUNT; kind++){
                Stage h;
                Settle(h);
                Run(h,2,face_right);
                ArcherInput press = kick;
                press.aim_axis = axis[kind];
                float x = h.pos.x + ARCHER_HALF_W + 0.35f;
                bool f_hit = false;
                for (int i = 0; i < KICK_SPECS[kind].ticks + 4 && !f_hit; i++){
                    h.ClearObstacles();
                    h.AddObstacle(x,h.pos.y + probe_y[p2],0.15f,0.08f,9,true);
                    StageEvents he;
                    h.Tick(i == 0 ? press : idle,he);
                    f_hit = he.kicks.size() > 0;
                }
                snprintf(detail,sizeof(detail),"%s %s something %s",KICK_SPECS[kind].name,
                         f_hit ? "hit" : "missed",where[p2]);
                Check(f_hit == reached[p2][kind],reached[p2][kind] ? "reaches what its boot reaches"
                                                                   : "and misses what its boot does not",detail);
            }
        }

        //Half a push on the stick is not a direction: a stick resting off-centre must not pick one.
        Stage half;
        Settle(half);
        ArcherInput nudge = kick;
        nudge.aim_axis = -KICK_SELECT_AIM * 0.8f;
        StageEvents he;
        half.Tick(nudge,he);
        Check(half.kick_kind == KICK_FRONT,"a stick barely tilted still throws K's kick");
    }

    //--- Hanging cannot kick -------------------------------------------------------------------------
    Stage h;
    const StageBlock& ledge = HighLedge(h);
    h.pos = v2(ledge.Left() - ARCHER_HALF_W - 0.5f,ARCHER_HALF_H);
    Settle(h);
    ArcherInput jump;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    ArcherInput fly;
    fly.move_axis = 1.0f;
    fly.f_jump_down = true;
    StageEvents hev;
    h.Tick(jump,hev);
    for (int i = 0; i < 200 && h.mode != MODE_HANG; i++){
        StageEvents e;
        h.Tick(fly,e);
    }
    Check(h.mode == MODE_HANG,"an archer hanging from a ledge");
    StageEvents hk;
    h.Tick(kick,hk);
    Check(!hk.f_kick_started,"cannot kick - both feet are on the wall");

    /*
        --- AND NEITHER CAN SOMEONE IN THE AIR ---------------------------------------------------

        Two halves, because gating only the start would leave the same second and a half of
        floating on screen by another route: one kick that never begins, and one that begins on the
        ground and is ended by the ground going away.
    */
    Stage air;
    Settle(air);
    StageEvents jev;
    air.Tick(jump,jev);
    Check(!air.f_on_ground,"an archer who has just jumped is off the ground");
    StageEvents ak;
    air.Tick(kick,ak);
    Check(!ak.f_kick_started,"and cannot start a kick in mid-air");

    /*
        The second half. She kicks standing on the very edge of a platform, and the kick's own
        slide carries her off it; the move has to end with the floor rather than play out in the
        air. Dropped onto the lip rather than run at it, because the plant would arrest a run
        before it reached the edge - which is the point of the plant, and no use to this test.
    */
    Stage off;
    const StageBlock& lip = HighLedge(off);
    off.pos = v2(lip.Right() - 0.05f,lip.Top() + ARCHER_HALF_H);
    Settle(off);
    if (off.f_on_ground){
        StageEvents oev;
        off.vel.x = ARCHER_RUN_SPEED;       //still carrying a run when the boot goes out
        off.Tick(kick,oev);
        Check(oev.f_kick_started,"a kick thrown on the lip of a platform starts");
        /*
            ONE TICK OF OVERLAP IS EXPECTED and is the ordering, not a hole: TickKick runs before
            TickArcher, so the tick she actually goes over the edge on has already had its kick
            update. What must not happen is the kick CARRYING ON in the air, which at KICK_TICKS
            would be a second and a half of it - so this counts the ticks rather than forbidding
            them, and 1 is the ordering while 90 is the bug.
        */
        int air_kick_ticks = 0;
        bool f_left_ground = false;
        for (int i = 0; i < KICK_TICKS + 4; i++){
            StageEvents e;
            off.Tick(idle,e);
            if (!off.f_on_ground){
                f_left_ground = true;
                if (off.kick_ticks > 0){
                    air_kick_ticks++;
                }
            }
        }
        Check(f_left_ground,"and the kick's own slide carries her off it");
        snprintf(detail,sizeof(detail),"kicked in mid-air for %i ticks",air_kick_ticks);
        Check(air_kick_ticks <= 1,"the kick ends with the ground, rather than floating out its "
                                  "remaining ticks",detail);
    }
}

//--- The rope -------------------------------------------------------------------------------------

/*
    Only the DECISIONS are testable here, and that is the right amount.

    The swing itself is a jointed chain in reactphysics3d and is the app's - see the rope note in
    Stage.h. What the rules own is when a grab is allowed, when a release happens and which link was
    caught, and those are exactly the parts that would otherwise only be checkable by swinging on
    it and squinting.
*/
static void TickRopeAt(Stage& s, int n, const ArcherInput& in, float x, float y, int id,
                       StageEvents* out_last = NULL){
    for (int i = 0; i < n; i++){
        s.ClearRopePoints();
        s.AddRopePoint(x,y,id);
        StageEvents ev;
        s.Tick(in,ev);
        if (out_last){
            *out_last = ev;
        }
    }
}

static void TestRope(){
    printf("the rope\n");
    char detail[220];

    ArcherInput idle;
    ArcherInput action;
    action.f_action_pressed = true;

    //--- Catching it ------------------------------------------------------------------------------
    Stage s;
    Settle(s);
    float rope_x = s.pos.x;
    float rope_y = s.pos.y + ARCHER_HALF_H * 0.6f;
    StageEvents ev;
    TickRopeAt(s,1,action,rope_x,rope_y,5,&ev);
    Check(ev.f_grabbed_rope,"pressing action at a rope catches it");
    Check(ev.grabbed_rope_id == 5,"naming the link by the id the app gave it");
    Check(s.mode == MODE_ROPE,"and puts the archer on it");

    //--- ...and only when it is in reach -----------------------------------------------------------
    Stage far;
    Settle(far);
    StageEvents fev;
    TickRopeAt(far,1,action,far.pos.x + ROPE_GRAB_REACH * 3.0f,far.pos.y,5,&fev);
    Check(!fev.f_grabbed_rope,"a rope out of reach is not caught");
    Check(far.mode != MODE_ROPE,"and the archer carries on");

    //--- The rules stop driving --------------------------------------------------------------------
    /*
        The whole point of MODE_ROPE: the solver owns the archer. If the rules were still
        integrating, gravity would drag pos down every tick and fight whatever the app wrote back.
    */
    v2 held = s.pos;
    TickRopeAt(s,20,idle,rope_x,rope_y,5);
    Check(s.pos.x == held.x && s.pos.y == held.y,
          "while on the rope the rules do not move the archer at all - the solver does");

    //--- The bow needs both hands ------------------------------------------------------------------
    ArcherInput draw;
    draw.f_draw_down = true;
    TickRopeAt(s,10,draw,rope_x,rope_y,5);
    Check(s.bow_mode == BOW_IDLE,"and the bow cannot be drawn from it");

    //--- Letting go --------------------------------------------------------------------------------
    //The press that caught it is still down when TickRope first runs, so an ungated release would
    //fire on the very next tick and the rope could never be held at all.
    Stage q;
    Settle(q);
    TickRopeAt(q,1,action,q.pos.x,q.pos.y + ARCHER_HALF_H * 0.6f,5);
    Check(q.mode == MODE_ROPE,"a second archer catches a rope");
    StageEvents rev;
    TickRopeAt(q,1,action,q.pos.x,q.pos.y,5,&rev);
    Check(!rev.f_released_rope,"an action on the very next tick does not let go");
    Check(q.mode == MODE_ROPE,"they are still on it");

    TickRopeAt(q,ROPE_MIN_HOLD_TICKS,idle,q.pos.x,q.pos.y,5);
    StageEvents rev2;
    TickRopeAt(q,1,action,q.pos.x,q.pos.y,5,&rev2);
    Check(rev2.f_released_rope,"but one after the minimum hold does");
    Check(!rev2.f_rope_jump,"reported as a plain release");
    Check(q.mode == MODE_AIR,"and the archer is airborne");

    //--- Letting go WITH jump is a different event -------------------------------------------------
    Stage j;
    Settle(j);
    TickRopeAt(j,1,action,j.pos.x,j.pos.y + ARCHER_HALF_H * 0.6f,5);
    TickRopeAt(j,ROPE_MIN_HOLD_TICKS + 1,idle,j.pos.x,j.pos.y,5);
    ArcherInput jump;
    jump.f_jump_pressed = true;
    StageEvents jev;
    TickRopeAt(j,1,jump,j.pos.x,j.pos.y,5,&jev);
    Check(jev.f_released_rope && jev.f_rope_jump,
          "letting go with jump is reported as such, so the app can add the boost");

    //--- The cooldown -------------------------------------------------------------------------------
    //Without it the action that let go immediately catches the same rope again, and the archer is
    //welded to it exactly as they were to the first ledge.
    StageEvents cev;
    TickRopeAt(j,1,action,j.pos.x,j.pos.y,5,&cev);
    Check(!cev.f_grabbed_rope,"the rope just released cannot be caught again straight away");
    snprintf(detail,sizeof(detail),"cooldown is %i ticks",ROPE_GRAB_COOLDOWN);
    TickRopeAt(j,ROPE_GRAB_COOLDOWN + 2,idle,j.pos.x,j.pos.y,5);
    StageEvents cev2;
    TickRopeAt(j,1,action,j.pos.x,j.pos.y,5,&cev2);
    Check(cev2.f_grabbed_rope,"but it can once the cooldown has run out",detail);

    //--- Rope points are per tick --------------------------------------------------------------------
    Stage c;
    Settle(c);
    c.ClearRopePoints();
    c.AddRopePoint(c.pos.x,c.pos.y,1);
    Check(c.rope_points.size() == 1,"a rope point can be declared");
    c.ClearRopePoints();
    Check(c.rope_points.size() == 0,"and clearing removes it");
    StageEvents nev;
    c.Tick(action,nev);
    Check(!nev.f_grabbed_rope,"with none declared, action catches nothing");
}

//--- Determinism ---------------------------------------------------------------------------------

/*
    --- the puppet ---------------------------------------------------------------------------

    The ANIMATION's decisions, which are testable here for exactly the same reason the rules are:
    Puppet.h names no engine type either, so "at a dead run the walk cycle is played at 1.8x" is a
    fact that can be asserted rather than squinted at.

    NO CLIP SPEEDS ARE TYPED IN FROM THE ASSET. The real ones are measured off the .glb at load
    (ApplicationArcher::MeasureClips), and a test that repeated them here would go quietly wrong
    the first time the walk is re-exported with a longer stride. What is checked instead is the
    RELATIONSHIP - that the rate is the speed over the clip's own native speed, that it is clamped,
    and that the clamp is what reports the missing animation.
*/
static void TestPuppet(){
    printf("\n[the puppet]\n");

    Puppet p;
    //A deliberately round stand-in: a clip that covers one unit a second on a rig drawn at 2x
    //walks at 2 units a second, whatever the real export happens to say.
    p.model_scale = 2.0f;
    p.clip_speed[CLIP_WALK] = 1.0f;
    CheckNear(p.WorldClipSpeed(CLIP_WALK),2.0f,0.0001f,"a clip's world speed is its own speed times the model scale");

    /*
        THE ONE COMBINATION THE CLIP TABLE MAY NOT CONTAIN.

        A clip whose yaw is extracted turns the CHARACTER, and the character's rotation is applied
        above the root bone - so any translation left on that bone is rotated by it rather than
        translated. R(yaw) * T(p) = T(R(yaw)*p) * R(yaw). A clip that walks while turning therefore
        orbits a point instead of walking: measured on Twirl, the hips traced a circle growing to
        1.759 world units of radius, which is its authored 0.879-unit walk-back times the model
        scale, rotated.

        The escape is f_extract_move, which takes the offset off the bone so there is nothing left
        to sweep round. What is forbidden is extracting the yaw and LEAVING the translation, so
        that is what this checks - before anyone has to notice it on screen.
    */
    int f_travels_and_turns = -1;
    for (int i = 0; i < CLIP_COUNT; i++){
        if (ARCHER_CLIPS[i].f_turns && ARCHER_CLIPS[i].f_travels &&
            !ARCHER_CLIPS[i].f_extract_move){
            f_travels_and_turns = i;
        }
    }
    {
        char detail[200];
        snprintf(detail,sizeof(detail),"%s both travels and turns - it would orbit a point",
                 (f_travels_and_turns >= 0) ? ARCHER_CLIPS[f_travels_and_turns].name : "");
        Check(f_travels_and_turns < 0,"no clip has its yaw extracted while its travel stays on the bone",detail);
    }

    /*
        AND THE ONE THE CLIP TABLE MUST CONTAIN: every rung of the ladder gives up its translation.

        These are the clips the RULES drive her with, so Stage has already walked the distance the
        clip is about to walk again. Leaving it on the bone draws her a full stride ahead of
        herself and snaps her back at the wrap - 1.53 world units for Walking, 2.94 for
        Running_Fast, and the snap on dropping to Idle reads as the character jumping backwards.

        Stated over PUPPET_LOCOMOTION rather than over f_travels because those are different
        questions: Twirl travels and is deliberately NOT extracted, since nothing plays it but the
        preview and its step back is the performance.
    */
    int unextracted_rung = -1;
    for (int i = 0; i < PUPPET_LOCOMOTION_COUNT; i++){
        if (!ARCHER_CLIPS[PUPPET_LOCOMOTION[i]].f_extract_move){
            unextracted_rung = PUPPET_LOCOMOTION[i];
        }
    }
    {
        char detail[200];
        snprintf(detail,sizeof(detail),"%s is a rung of the ladder but keeps its stride on the bone",
                 (unextracted_rung >= 0) ? ARCHER_CLIPS[unextracted_rung].name : "");
        Check(unextracted_rung < 0,"every locomotion clip hands its travel to the character",detail);
    }

    ArcherAnimParams in;
    in.f_on_ground = true;

    //Standing still.
    in.speed = 0.0f;
    in.ground_speed = 0.0f;
    PuppetChoice c = p.Choose(in);
    Check(c.clip == CLIP_IDLE,"standing still plays the idle");
    Check(!c.f_placeholder,"and the idle is a real clip, not a stand-in");

    //Moving at exactly the clip's own speed: the feet are planted and nothing is stretched.
    in.speed = 2.0f;
    in.ground_speed = 2.0f;
    c = p.Choose(in);
    Check(c.clip == CLIP_WALK,"moving plays the walk");
    CheckNear(c.rate,1.0f,0.001f,"at the clip's own speed it plays at 1.0","the feet are planted");

    //Three quarters of that speed gives three quarters of the rate, which is the whole of the
    //rate-matching rule. Chosen to sit INSIDE the clamp - the clamped case is checked separately
    //below, and a test that straddles a clamp is testing the clamp while claiming to test the rule.
    in.speed = 1.5f;
    in.ground_speed = 1.5f;
    c = p.Choose(in);
    CheckNear(c.rate,0.75f,0.001f,"at three quarters of the clip's speed it plays at three quarters rate");

    /*
        And below the floor it stops slowing down.

        A walk played at a quarter speed does not read as a slow walk, it reads as a video
        buffering - so there is a bottom to the match as well as a top. It is the cruder of the
        two limits, because the RIGHT answer at low speed is to blend toward the idle rather than
        to slow the walk at all; that is what the blend space (step 1) is for, and this clamp is
        what stands in until it exists.
    */
    in.speed = 0.5f;
    in.ground_speed = 0.5f;
    c = p.Choose(in);
    CheckNear(c.rate,PUPPET_RATE_MIN,0.001f,"very slow movement is clamped at the rate floor");

    /*
        And a full run, which is where the missing clip shows up as a number.

        wanted_rate is what planting the feet would take; rate is what it is allowed. The gap
        between them is foot slide, and this check exists so that adding a real run cycle CHANGES
        this test rather than quietly not mattering.
    */
    in.speed = ARCHER_RUN_SPEED;
    in.ground_speed = ARCHER_RUN_SPEED;
    c = p.Choose(in);
    CheckNear(c.wanted_rate,ARCHER_RUN_SPEED / 2.0f,0.001f,
              "a full run asks for speed over the clip's native speed");
    CheckNear(c.rate,PUPPET_RATE_MAX,0.001f,"but is clamped, because past that a walk is a fast-forward");
    Check(c.wanted_rate > c.rate,"so the clamp is what reports how much clip is missing");

    //Backing up is the same clip, backwards - no turn clip, no walking on the spot.
    in.speed = -2.0f;
    in.ground_speed = 2.0f;
    c = p.Choose(in);
    Check(c.clip == CLIP_WALK,"backing up is still the walk");
    CheckNear(c.rate,-1.0f,0.001f,"played backwards","one minus sign instead of a second clip");

    /*
        --- the locomotion ladder ---------------------------------------------------------------

        With more than one way of covering ground, the clip is CHOSEN before it is stretched. Set
        up a ladder an octave apart so the boundaries are unambiguous, and check that each speed
        lands on the clip nearest it IN RATIO - which is the thing that would be wrong if the
        search ever went back to comparing differences.
    */
    p.clip_speed[CLIP_RUN_SLOW] = 2.0f;     //4 units/s at the 2x model scale
    p.clip_speed[CLIP_RUN_FAST] = 4.0f;     //8
    in.f_on_ground = true;

    struct { float speed; int clip; const char* what; } ladder[] = {
        { 1.0f,  CLIP_WALK,     "a stroll picks the walk" },
        { 2.0f,  CLIP_WALK,     "the walk's own speed picks the walk" },
        { 4.0f,  CLIP_RUN_SLOW, "the slow run's own speed picks the slow run" },
        { 8.0f,  CLIP_RUN_FAST, "the fast run's own speed picks the fast run" },
        { 20.0f, CLIP_RUN_FAST, "and past every clip it stays on the fastest one" },
    };
    for (size_t i = 0; i < sizeof(ladder)/sizeof(ladder[0]); i++){
        in.speed = ladder[i].speed;
        in.ground_speed = ladder[i].speed;
        c = p.Choose(in);
        char detail[160];
        snprintf(detail,sizeof(detail),"at %.1f it picked %s",ladder[i].speed,ARCHER_CLIPS[c.clip].name);
        Check(c.clip == ladder[i].clip,ladder[i].what,detail);
    }

    /*
        --- the blend space ---------------------------------------------------------------------

        BETWEEN two rungs the answer is BOTH of them and a weight, which is the whole of what makes
        walking-to-running stop being an event. Set cycle lengths too, because what a blend covers
        per second depends on them - see BlendedSpeed.
    */
    p.clip_duration[CLIP_WALK]     = 1.0f;
    p.clip_duration[CLIP_RUN_SLOW] = 0.5f;
    p.clip_duration[CLIP_RUN_FAST] = 0.25f;

    in.speed = 3.0f;
    in.ground_speed = 3.0f;
    c = p.Choose(in);
    Check(c.clip == CLIP_WALK && c.blend_clip == CLIP_RUN_SLOW,
          "a speed between two rungs blends those two rungs");
    CheckNear(c.blend,0.5f,0.001f,"weighted by where it falls between their speeds",
              "2.0 and 4.0, so 3.0 is halfway");

    in.ground_speed = 3.5f;
    in.speed = 3.5f;
    c = p.Choose(in);
    CheckNear(c.blend,0.75f,0.001f,"and the weight moves with the speed rather than in steps");

    //Exactly ON a rung is that rung alone: there is nothing to blend it with.
    in.ground_speed = 4.0f;
    in.speed = 4.0f;
    c = p.Choose(in);
    Check(c.clip == CLIP_RUN_SLOW && c.blend_clip < 0,"a speed exactly on a rung needs no blend");
    CheckNear(c.rate,1.0f,0.001f,"and plays it at 1.0");

    /*
        The phase offset, which is what stops the feet skating.

        Two cycles on one playhead mix whatever poses they happen to be at, so the follower is
        shifted by the difference between the two clips' measured foot-plant phases. Nothing is
        re-authored; the correction comes out of the asset.
    */
    p.clip_phase[CLIP_WALK] = 0.83f;
    p.clip_phase[CLIP_RUN_SLOW] = 0.69f;
    in.ground_speed = 3.0f;
    in.speed = 3.0f;
    c = p.Choose(in);
    CheckNear(c.blend_phase_offset,0.69f - 0.83f,0.0001f,
              "the follower is offset by the difference in foot-plant phase");

    /*
        And the rate is matched against what the BLEND covers, not against the interpolation of the
        two clips' speeds - those differ whenever the cycles are different lengths, which is always.

        Walk: 2.0 units/s over 1.0s = a 2.0-unit stride. Slow run: 4.0 over 0.5s = 2.0 units.
        Halfway the stride is 2.0 and the cycle is 0.75s, so the blend covers 2.667 units/s - not
        the 3.0 that averaging the speeds would claim. At a ground speed of 3.0 the rate therefore
        has to be 3.0 / 2.667 = 1.125, and a test written against the naive figure would have
        happily accepted 1.0 and shipped 12% of foot slide.
    */
    CheckNear(p.BlendedSpeed(CLIP_WALK,CLIP_RUN_SLOW,0.5f),2.0f / 0.75f,0.001f,
              "a blend covers its interpolated stride over its interpolated cycle");
    CheckNear(c.rate,3.0f / (2.0f / 0.75f),0.001f,"and the rate is matched against that");

    //A clip that was never measured - missing from the export - cannot be chosen, however fast.
    p.clip_speed[CLIP_RUN_FAST] = 0.0f;
    in.speed = 20.0f;
    in.ground_speed = 20.0f;
    c = p.Choose(in);
    Check(c.clip == CLIP_RUN_SLOW,"a clip missing from the export is never picked");
    p.clip_speed[CLIP_RUN_FAST] = 4.0f;

    //And the climb, which is PINNED to the rules' progress - however far the clip and the window
    //disagree, a pin plays it through once and keeps the pose where the body is.
    p.clip_duration[CLIP_CLIMB] = (float)LEDGE_CLIMB_TICKS * ARCHER_DT * 4.0f;
    in.mode = MODE_CLIMB;
    in.action = ACTION_CLIMB;
    in.action_phase = 0.5f;
    c = p.Choose(in);
    Check(c.clip == CLIP_CLIMB,"climbing plays the climb");
    Check(!c.f_placeholder,"which is a real clip now, not a stand-in");
    CheckNear(c.wanted_rate,4.0f,0.001f,"reporting the rate the pin amounts to");
    CheckNear(c.rate,0.0f,0.0001f,"but held there, not advanced");
    CheckNear(c.pinned_time,(0.5f + 1.0f / (float)LEDGE_CLIMB_TICKS) * p.clip_duration[CLIP_CLIMB],1e-4f,
              "a tick ahead of the rules, because the pose shows a tick late");
    in.action_phase = 1.0f;
    c = p.Choose(in);
    Check(c.pinned_time < p.clip_duration[CLIP_CLIMB] && c.pinned_time > p.clip_duration[CLIP_CLIMB] - 0.01f,
          "and it stops just short of the end, where a one-shot counts as over");
    in.action = ACTION_NONE;
    in.action_phase = 0.0f;
    in.mode = MODE_GROUND;
    in.speed = 2.0f;
    in.ground_speed = 2.0f;

    //With matching off, every clip plays at its authored rate and the feet skate instead.
    p.f_match_feet = false;
    in.speed = ARCHER_RUN_SPEED;
    in.ground_speed = ARCHER_RUN_SPEED;
    c = p.Choose(in);
    CheckNear(c.rate,1.0f,0.001f,"with matching off a clip plays at 1.0 whatever the speed");
    p.f_match_feet = true;

    /*
        THE AIR SET, split on vel_y alone and fitted to nothing.

        Jump_ToAir is a TRANSITION into the airborne pose rather than a depiction of the rise, so
        it plays at 1.0 and holds what it arrives at - which is the pose Falling_Idle loops. That
        is the whole reason the four-piece set beats the one baked clip, and asserting rate 1.0
        here is what stops someone "helpfully" fitting it to the climb later.
    */
    in.f_on_ground = false;
    in.speed = 0.0f;
    in.ground_speed = 0.0f;
    in.vel_y = ARCHER_JUMP_SPEED;
    c = p.Choose(in);
    Check(c.clip == CLIP_JUMP_RISE,"rising plays the jump");
    Check(!c.f_placeholder,"which is authored now, not a stand-in");
    CheckNear(c.rate,1.0f,0.001f,"at its own speed - a transition clip is not fitted to a window");
    Check(c.start_time < 0.0f,"and from its first frame; it is already trimmed to the launch");

    in.vel_y = 0.0f;
    Check(p.Choose(in).clip == CLIP_FALL,"the apex counts as falling - vel_y > 0 is the test");
    in.vel_y = -ARCHER_JUMP_SPEED;
    c = p.Choose(in);
    Check(c.clip == CLIP_FALL,"and so does actually falling");
    Check(!c.f_placeholder,"and it is authored too");
    /*
        A jump does not stop being a jump because she is moving. The air set ignores ground_speed
        entirely today - Jump_Forward is in the table but nothing selects it - and this says so out
        loud, so the day it is wired the test fails rather than the clip never being reached.
    */
    in.vel_y = ARCHER_JUMP_SPEED;
    in.speed = ARCHER_RUN_SPEED;
    in.ground_speed = ARCHER_RUN_SPEED;
    c = p.Choose(in);
    Check(c.clip == CLIP_JUMP_RISE,"speed alone does not pick the running jump - takeoff does");
    Check(c.blend_clip < 0,"and the ladder does not reach into the air");
    in.speed = 0.0f;
    in.ground_speed = 0.0f;
    in.vel_y = 0.0f;

    /*
        THE RUNNING JUMP, latched at takeoff.

        The latch is the whole behaviour worth testing, so these go through Tick. The point is that
        the clip is chosen from the speed she LEFT THE GROUND at and then does not change, however
        much air friction takes off her afterwards.
    */
    auto takeoff_at = [](float ground_speed) -> Puppet {
        Puppet jp;
        jp.run_jump_rise = 0.333f;
        ArcherAnimParams a;
        a.f_on_ground = true;
        a.speed = ground_speed;
        a.ground_speed = ground_speed;
        jp.Tick(a);                 //one grounded tick, so the next one is a takeoff edge
        a.f_on_ground = false;
        a.vel_y = ARCHER_JUMP_SPEED;
        jp.Tick(a);
        return jp;
    };

    Check(takeoff_at(ARCHER_RUN_SPEED).choice.clip == CLIP_RUN_JUMP,
          "leaving the ground at a run plays the running jump");
    Check(takeoff_at(0.0f).choice.clip == CLIP_JUMP_RISE,
          "and leaving it from a standstill plays the standing rise");
    Check(takeoff_at(PUPPET_RUN_JUMP_SPEED - 0.1f).choice.clip == CLIP_JUMP_RISE,
          "the line between them is PUPPET_RUN_JUMP_SPEED");
    CheckNear(takeoff_at(ARCHER_RUN_SPEED).choice.rate,
              0.333f / (ARCHER_JUMP_SPEED / ARCHER_GRAVITY),0.001f,
              "fitted to its climb, not to its whole length");
    {
        /*
            THE LATCH. Air friction can strip a full run's worth of speed inside one flight, so a
            choice re-made in the air would swap the clip mid-arc. This is that exact scenario.
        */
        Puppet jp = takeoff_at(ARCHER_RUN_SPEED);
        ArcherAnimParams a;
        a.f_on_ground = false;
        a.vel_y = -1.0f;            //past the apex
        a.speed = 0.0f;
        a.ground_speed = 0.0f;      //and now barely moving
        jp.Tick(a);
        Check(jp.choice.clip == CLIP_RUN_JUMP,
              "and it stays the running jump after air friction has taken the speed away");
        a.f_on_ground = true;
        a.vel_y = 0.0f;
        jp.Tick(a);
        //Off a ledge at a standstill: still a takeoff, still re-latched, but she is already
        //falling - so the standing set's own vel_y split picks the fall rather than the rise.
        a.f_on_ground = false;
        a.vel_y = -1.0f;
        a.ground_speed = 0.0f;
        jp.Tick(a);
        Check(jp.choice.clip == CLIP_FALL,
              "the latch is re-taken on the NEXT takeoff - walking off a ledge is a flight too");
        a.f_on_ground = true;
        a.vel_y = 0.0f;
        jp.Tick(a);
        a.f_on_ground = false;
        a.vel_y = ARCHER_JUMP_SPEED;
        jp.Tick(a);
        Check(jp.choice.clip == CLIP_JUMP_RISE,"and a standing jump after it gets the standing rise");
    }
    {
        /*
            ONLY A JUMP IS A RUNNING JUMP. Running off an edge leaves the ground already falling,
            and used to play Running_Jump's push off one foot anyway - then hold its last frame to
            the bottom of the drop. A jump in the coyote window after it is a real push, and gets it.
        */
        ArcherAnimParams a;
        a.f_on_ground = true;
        a.speed = ARCHER_RUN_SPEED;
        a.ground_speed = ARCHER_RUN_SPEED;
        Puppet wp;
        wp.run_jump_rise = 0.333f;
        wp.Tick(a);
        a.f_on_ground = false;
        a.vel_y = -0.95f;           //the first tick off the lip, as measured in the rope scene
        wp.Tick(a);
        Check(wp.choice.clip == CLIP_FALL,"running off an edge is a fall, not a running jump");
        Check(wp.air_clip < 0,"and does not latch the running set");
        Puppet cp = wp;
        a.vel_y = ARCHER_JUMP_SPEED;
        cp.Tick(a);
        Check(cp.choice.clip == CLIP_RUN_JUMP,"a jump in the coyote window after it is a running jump");
        Check(cp.choice.start_time == 0.0f,"from its takeoff frame");
        //Well past the window, a rise cannot be that press - nothing latches.
        a.vel_y = -2.0f;
        for (int i = 0; i < ARCHER_COYOTE_TICKS + 2; i++){ wp.Tick(a); }
        a.vel_y = ARCHER_JUMP_SPEED;
        wp.Tick(a);
        Check(wp.air_clip < 0,"a rise long after walking off is not a coyote jump");

        //Letting go of a rope while swinging up at speed: a flight, but no push off the feet.
        Puppet rp;
        rp.run_jump_rise = 0.333f;
        a.f_on_ground = false;
        a.mode = MODE_ROPE;
        a.vel_y = 3.0f;
        rp.Tick(a);
        a.mode = MODE_AIR;
        a.vel_y = 6.0f;
        rp.Tick(a);
        Check(rp.choice.clip == CLIP_JUMP_RISE,"swinging off a rope rises in the standing set");
    }
    {
        /*
            A SPENT ARC HANDS OVER. With the clip's length known, a running jump still in the air
            when Running_Jump has played out goes to the standing set: the fall, then the landing's
            lead-in - where it used to hold its last frame all the way down.
        */
        Puppet sp;
        sp.run_jump_rise = 0.333f;
        sp.clip_duration[CLIP_RUN_JUMP] = 0.933f;
        sp.clip_duration[CLIP_LAND_HARD] = 1.100f;
        sp.clip_entry[CLIP_LAND_HARD] = 0.300f;
        ArcherAnimParams a;
        a.f_on_ground = true;
        a.speed = ARCHER_RUN_SPEED;
        a.ground_speed = ARCHER_RUN_SPEED;
        sp.Tick(a);
        a.f_on_ground = false;
        a.mode = MODE_AIR;
        a.vel_y = ARCHER_JUMP_SPEED;
        sp.Tick(a);
        Check(sp.choice.clip == CLIP_RUN_JUMP,"a running jump into a pit starts as one");
        //Ticks for the clip to play out at its fitted rate, one to spare either side.
        int spent = (int)ceilf(0.933f / (sp.RunJumpRate() * ARCHER_DT));
        a.vel_y = -20.0f;
        for (int i = 0; i < spent - 2; i++){ sp.Tick(a); }
        Check(sp.choice.clip == CLIP_RUN_JUMP,"and stays one while the clip still has frames to play");
        for (int i = 0; i < 3; i++){ sp.Tick(a); }
        Check(sp.air_clip < 0 && sp.choice.clip == CLIP_FALL,"played out, the rest of the drop is the fall");
        a.land_speed = PUPPET_HARD_LAND_VEL + 5.0f;
        a.land_in_ticks = 10;
        sp.Tick(a);
        Check(sp.choice.clip == CLIP_LAND_HARD,"and the hard landing's lead-in meets the ground");
    }

    //Both grips are authored now, and they are two clips rather than one because a lip is braced
    //against and a rope is hung from.
    in.f_on_ground = true;
    in.mode = MODE_HANG;
    c = p.Choose(in);
    Check(c.clip == CLIP_HANG,"hanging off a ledge has its own clip");
    Check(!c.f_placeholder,"and is not a stand-in any more");
    in.mode = MODE_ROPE;
    c = p.Choose(in);
    Check(c.clip == CLIP_ROPE,"and the rope has a different one - a different grip, not the same pose");
    Check(!c.f_placeholder,"which is also no longer a stand-in");
    in.mode = MODE_GROUND;

    /*
        THE LANDING, which is the one thing here that is an EVENT rather than a function of the
        current state - so it goes through Tick, not Choose, and needs a fresh Puppet each time.

        The durations and the contact frame are fed in by hand because a rules test has no .glb;
        MeasureLandingClips supplies them in the app. These are this export's.
    */
    auto land_after = [](float impact, float ground_speed) -> PuppetChoice {
        Puppet lp;
        lp.clip_duration[CLIP_LAND_SOFT] = 0.400f;
        lp.clip_duration[CLIP_LAND_HARD] = 1.100f;
        lp.clip_entry[CLIP_LAND_HARD] = 0.300f;
        ArcherAnimParams a;
        a.f_on_ground = false;
        a.vel_y = -impact;
        lp.Tick(a);                 //one airborne tick, so the impact speed is remembered
        a.f_on_ground = true;
        a.vel_y = 0.0f;             //Stage has already zeroed it - this is the point of last_vel_y
        a.speed = ground_speed;
        a.ground_speed = ground_speed;
        lp.Tick(a);
        return lp.choice;
    };

    Check(land_after(ARCHER_JUMP_SPEED,0.0f).clip == CLIP_LAND_SOFT,
          "a routine jump lands soft - the dramatic one is for falling off something");
    Check(land_after(PUPPET_HARD_LAND_VEL + 1.0f,0.0f).clip == CLIP_LAND_HARD,
          "a long drop lands hard");
    CheckNear(land_after(PUPPET_HARD_LAND_VEL + 1.0f,0.0f).start_time,0.300f,0.001f,
              "entered where its feet touch, not where its first frame is still falling");
    Check(land_after(PUPPET_LAND_VEL - 1.0f,0.0f).clip == CLIP_IDLE,
          "stepping off a kerb is not a landing at all");
    Check(land_after(ARCHER_JUMP_SPEED,ARCHER_RUN_SPEED).clip != CLIP_LAND_SOFT,
          "and landing at a run skips it - the rules never stopped her, so neither does this");

    /*
        THE LEAD-IN: with a forecast, the landing clip starts in the air, pinned so its contact
        frame falls on the contact tick. The hip curve is a stand-in shaped like the hard landing's
        - 0.5 above its contact height at the start, descending to it at the contact frame.
    */
    {
        Puppet lp;
        lp.clip_duration[CLIP_LAND_SOFT] = 0.400f;
        lp.clip_duration[CLIP_LAND_HARD] = 1.100f;
        lp.clip_entry[CLIP_LAND_SOFT] = 0.267f;
        lp.clip_entry[CLIP_LAND_HARD] = 0.300f;
        ArcherAnimParams a;
        a.f_on_ground = false;
        a.vel_y = -30.0f;
        a.land_speed = PUPPET_HARD_LAND_VEL + 5.0f;
        a.land_in_ticks = 25;
        lp.Tick(a);
        Check(lp.choice.clip == CLIP_FALL,"a landing further off than the clip's lead-in keeps the fall");
        a.land_in_ticks = 18;
        lp.Tick(a);
        Check(lp.choice.clip == CLIP_LAND_HARD,"within it, the landing the forecast speed calls for starts in the air");
        CheckNear(lp.choice.pinned_time,0.300f - 17.0f * ARCHER_DT,1e-4f,
                  "pinned so its contact frame falls on the contact tick - one tick ahead, as the pose shows late");
        a.land_in_ticks = -1;
        lp.Tick(a);
        Check(lp.choice.clip == CLIP_FALL,"a forecast that goes (she steered off the edge) drops back to the fall");
        a.land_in_ticks = 1;
        lp.Tick(a);
        CheckNear(lp.choice.pinned_time,0.300f,1e-4f,"on the last tick in the air the pin is the contact frame");
        //Touchdown, with last tick's vel_y reading as a SOFT landing: the lead-in's clip stands.
        a.vel_y = -(PUPPET_HARD_LAND_VEL - 2.0f);
        lp.Tick(a);
        a.f_on_ground = true;
        a.vel_y = 0.0f;
        a.land_in_ticks = -1;
        lp.Tick(a);
        Check(lp.choice.clip == CLIP_LAND_HARD,"at contact the clip the lead-in chose IS the landing, not re-chosen");

        //A running jump keeps its own arc - taking off rising, since that is what makes it a jump.
        Puppet rp;
        rp.clip_entry[CLIP_LAND_SOFT] = 0.267f;
        ArcherAnimParams r;
        r.f_on_ground = true;
        r.speed = ARCHER_RUN_SPEED;
        r.ground_speed = ARCHER_RUN_SPEED;
        rp.Tick(r);
        r.f_on_ground = false;
        r.vel_y = ARCHER_JUMP_SPEED;
        rp.Tick(r);
        r.vel_y = -10.0f;
        r.land_speed = ARCHER_JUMP_SPEED;
        r.land_in_ticks = 5;
        rp.Tick(r);
        Check(rp.choice.clip == CLIP_RUN_JUMP,"a running jump keeps its own arc to the ground");

        /*
            THE FALL POSE: grows with the fall speed, eased; gone in a lead-in, on the ground,
            rising. OFF TODAY (PUPPET_FALL_POSE_MAX 0) - Falling_Idle has motion of its own now and
            the pose froze it - so the fall must stay the clip as authored. The weights below are
            checked as a share of the ceiling, and run again the day it is raised.
        */
        if (PUPPET_FALL_POSE_MAX <= 0.0f){
            Puppet op;
            op.clip_entry[CLIP_LAND_HARD] = 0.300f;
            ArcherAnimParams o;
            o.f_on_ground = true;
            op.Tick(o);
            o.f_on_ground = false;
            o.mode = MODE_AIR;
            o.vel_y = -PUPPET_FALL_POSE_VEL * 2.0f;
            for (int i = 0; i < 30; i++){ op.Tick(o); }
            Check(op.choice.clip == CLIP_FALL && op.fall_weight == 0.0f && op.choice.overlay_clip < 0,
                  "the fall pose is off: a long fall is Falling_Idle as authored, nothing laid over it");
        }else{
        Puppet fp;
        fp.clip_entry[CLIP_LAND_HARD] = 0.300f;
        fp.clip_entry[CLIP_LAND_SOFT] = 0.267f;
        ArcherAnimParams f;
        f.f_on_ground = true;
        fp.Tick(f);
        f.f_on_ground = false;
        f.mode = MODE_AIR;
        f.vel_y = 5.0f;
        fp.Tick(f);
        Check(fp.fall_weight == 0.0f && fp.choice.overlay_clip < 0,"rising, no fall pose");
        f.vel_y = -PUPPET_FALL_POSE_VEL * 0.5f;
        float was = fp.fall_weight;
        fp.Tick(f);
        float step = 1.0f / (float)PUPPET_FALL_BLEND_TICKS;
        CheckNear(fp.fall_weight - was,fminf(step,0.5f * PUPPET_FALL_POSE_MAX),1e-5f,
                  "falling, it eases in a crossfade's step a tick");
        for (int i = 0; i < 30; i++){ fp.Tick(f); }
        CheckNear(fp.fall_weight,0.5f * PUPPET_FALL_POSE_MAX,1e-4f,
                  "to smoothstep of the fall speed over PUPPET_FALL_POSE_VEL - half at half");
        Check(fp.choice.overlay_clip == CLIP_LAND_HARD && fp.choice.overlay_time == 0.0f && fp.choice.overlay_weight == fp.fall_weight,
              "laid over the fall as the hard landing's airborne opening");
        f.vel_y = -PUPPET_FALL_POSE_VEL * 2.0f;
        for (int i = 0; i < 60; i++){ fp.Tick(f); }
        CheckNear(fp.fall_weight,PUPPET_FALL_POSE_MAX,1e-5f,"and all of it past that speed");
        f.land_speed = PUPPET_HARD_LAND_VEL + 5.0f;
        f.land_in_ticks = 10;
        fp.Tick(f);
        CheckNear(fp.fall_weight,fmaxf(0.0f,PUPPET_FALL_POSE_MAX - step),1e-5f,
                  "a lead-in takes over, and the fall pose fades out as long as its crossfade takes");
        f.f_on_ground = true;
        f.mode = MODE_GROUND;
        f.vel_y = 0.0f;
        f.land_in_ticks = -1;
        for (int i = 0; i < PUPPET_FALL_BLEND_TICKS; i++){ fp.Tick(f); }
        Check(fp.fall_weight == 0.0f && fp.choice.overlay_clip < 0,"and it is gone on the ground");
        }
    }

    /*
        THE RUN-TO-STOP, and the thing that tells it apart from running into a wall.

        The discriminator is not a tuned threshold, it is arithmetic: ground friction can remove at
        most PUPPET_FRICTION_STEP of speed in one tick, so a drop of about that much is her letting
        go of the key and a bigger one is something being in the way. Measured in the running game,
        releasing steps 9 -> 7 -> 5 -> 3 -> 1 -> 0 while a wall goes 9 -> 0 in a single tick.
    */
    auto decelerate_by = [](float from, float drop) -> PuppetChoice {
        Puppet sp;
        sp.stop_plant = 0.267f;
        sp.clip_duration[CLIP_STOP] = 0.933f;
        ArcherAnimParams a;
        a.f_on_ground = true;
        a.speed = from;
        a.ground_speed = from;
        sp.Tick(a);                         //one tick at speed, so the drop is visible next
        a.speed = from - drop;
        a.ground_speed = from - drop;
        sp.Tick(a);
        return sp.choice;
    };

    Check(decelerate_by(ARCHER_RUN_SPEED,PUPPET_FRICTION_STEP).clip == CLIP_STOP,
          "letting go at a run plays the run-to-stop");
    Check(decelerate_by(ARCHER_RUN_SPEED,ARCHER_RUN_SPEED).clip != CLIP_STOP,
          "but stopping dead does not - friction cannot take that much off in one tick");
    Check(decelerate_by(ARCHER_RUN_SPEED,ARCHER_RUN_SPEED - ARCHER_PUSH_SPEED).clip != CLIP_STOP,
          "and nor does clamping to a crate's push speed");
    Check(decelerate_by(PUPPET_STOP_FROM_SPEED - 0.5f,PUPPET_FRICTION_STEP).clip != CLIP_STOP,
          "a walk does not get one either - it is a flourish, not a hitch");
    Check(decelerate_by(ARCHER_RUN_SPEED,0.0f).clip != CLIP_STOP,
          "and holding a steady run certainly does not");
    /*
        The fit, which does not. The rules stop her in PUPPET_STOP_TIME and the clip wants that long
        again and more just to reach its plant, so this asks for the gap to be REPORTED rather than
        for it to be small - the same arrangement as the kick and the climb.
    */
    {
        PuppetChoice sc = decelerate_by(ARCHER_RUN_SPEED,PUPPET_FRICTION_STEP);
        CheckNear(sc.wanted_rate,0.267f / (ARCHER_RUN_SPEED / ARCHER_RUN_FRICTION),0.01f,
                  "and reports what fitting the plant to the stop would have taken");
        CheckNear(sc.rate,PUPPET_ACTION_RATE_MAX,0.001f,"clamped, because it is over three times");
    }

    /*
        AND NOTHING FALLS THROUGH TO THE IDLE ANY MORE.

        Every mode the archer can be in now names a clip of its own. The remaining placeholder is
        the BOW, which is not a mode - drawing is something she does while running or falling - and
        so cannot be reached from here at all; it waits on step 2's mask layer. Stated as a sweep
        over the modes rather than as one more line about the rope, because the useful invariant is
        "no mode is a stand-in", and a sweep keeps saying that when a mode is added.
    */
    int placeholder_mode = -1;
    {
        const ArcherMode modes[] = { MODE_GROUND, MODE_HANG, MODE_CLIMB, MODE_ROPE };
        for (int i = 0; i < (int)(sizeof(modes) / sizeof(modes[0])); i++){
            ArcherAnimParams m;
            m.f_on_ground = true;
            m.mode = modes[i];
            if (p.Choose(m).f_placeholder){
                placeholder_mode = (int)modes[i];
            }
        }
    }
    {
        char detail[100];
        snprintf(detail,sizeof(detail),"mode %i is still standing in",placeholder_mode);
        Check(placeholder_mode < 0,"every mode the archer can be in has a clip of its own",detail);
    }
    in.mode = MODE_GROUND;

    //The kick is the one action with a real clip, and it is fitted to the window the RULES give
    //it rather than to any speed.
    p.clip_duration[CLIP_KICK] = (float)KICK_TICKS * ARCHER_DT * 2.0f;   //twice as long as it may take
    in.action = ACTION_KICK;
    c = p.Choose(in);
    Check(c.clip == CLIP_KICK,"a kick plays the kick clip");
    CheckNear(c.wanted_rate,2.0f,0.001f,"at the rate that fits it into KICK_TICKS");
    //And the other two play their own clip, fitted to their own length rather than to K's.
    for (int kind = KICK_FRONT_2; kind < KICK_KIND_COUNT; kind++){
        int clip = PUPPET_KICK_CLIP[kind];
        p.clip_duration[clip] = (float)KICK_SPECS[kind].ticks * ARCHER_DT;
        in.kick_kind = kind;
        c = p.Choose(in);
        Check(c.clip == clip,"Down+K and Up+K play their own clip",ARCHER_CLIPS[clip].name);
        CheckNear(c.wanted_rate,1.0f,0.001f,"at the rate that fits it into its own KICK_SPECS row");
    }
    in.kick_kind = KICK_FRONT;
    in.action = ACTION_NONE;

    /*
        The turnaround.

        PUPPET_TURN_TICKS ticks to cross 180 degrees, and - the part that is a choice rather than
        arithmetic - it passes through ZERO on the way. Zero is facing the camera; 180 is facing
        away. Turning toward the viewer reads as a person changing their mind, turning away reads
        as a person leaving.
    */
    Puppet t;
    ArcherAnimParams turn;
    turn.f_on_ground = true;
    turn.facing = 1.0f;
    t.Tick(turn);
    CheckNear(t.yaw_deg,PUPPET_YAW_RIGHT,0.001f,"facing right holds the model at its right yaw");

    turn.facing = -1.0f;
    bool f_went_the_long_way = false;
    bool f_monotonic = true;
    float previous = t.yaw_deg;
    int ticks_to_turn = 0;
    for (int i = 0; i < PUPPET_TURN_TICKS * 3; i++){
        t.Tick(turn);
        //Going the LONG way round - through 180, showing the camera her back - is the failure this
        //is looking for, and it shows up as a yaw that leaves the quarter-turn either side of zero.
        //Checking the bound rather than for an exact zero, because with an odd number of ticks the
        //slew steps over zero rather than landing on it.
        if (fabsf(t.yaw_deg) > 90.0f + 0.001f){
            f_went_the_long_way = true;
        }
        if (t.yaw_deg >= previous){
            f_monotonic = false;
        }
        previous = t.yaw_deg;
        if (t.yaw_deg <= PUPPET_YAW_LEFT + 0.001f){
            ticks_to_turn = i + 1;
            break;
        }
    }
    Check(ticks_to_turn == PUPPET_TURN_TICKS,"a turnaround takes exactly PUPPET_TURN_TICKS ticks");
    Check(!f_went_the_long_way,"and turns TOWARD the camera - it never passes behind a quarter turn");
    Check(f_monotonic,"turning one way the whole time, with no wobble at the ends");

    /*
        THE UPPER LAYER AND THE AIM (animation_plan.md, Step 2).

        The draw is an upper-body layer in every stance, its clip pinned to the rules' progress and
        handing on to the held loop at full draw. The aim takes hold only from the NOCK - the live
        neutral reads the posed bow, which means nothing before the bow is up - and, now that the
        layer carries the draw over any legs, a draw at a run bends her like a standing one.
    */
    {
        //The draw, then at full draw the held loop - on the base when standing, on the layer always.
        Puppet h;
        ArcherAnimParams drawing;
        drawing.action = ACTION_DRAW;
        drawing.action_phase = 0.5f;
        Check(h.Choose(drawing).clip == CLIP_DRAW,"a standing draw under way plays the draw on her legs");
        PuppetChoice up;
        Puppet::ChooseUpper(drawing,up);
        Check(up.upper_clip == CLIP_DRAW,"and on the upper layer");
        CheckNear(up.upper_phase,0.5f,0.0001f,"sampled where the RULES' draw has got to");
        drawing.action_phase = 1.0f;
        Check(h.Choose(drawing).clip == CLIP_AIM_IDLE,"at full draw it hands on to the held loop");
        Puppet::ChooseUpper(drawing,up);
        Check(up.upper_clip == CLIP_AIM_IDLE && up.upper_phase < 0.0f,
              "the layer too, and the loop runs on its own clock");
        Check(ARCHER_CLIPS[CLIP_AIM_IDLE].f_looping,"which loops for as long as she holds");
        Check(Puppet::IsDrawPose(CLIP_AIM_IDLE),"and counts as a draw pose");

        //The aim waits for the nock.
        Puppet a;
        ArcherAnimParams draw;
        draw.action = ACTION_DRAW;
        draw.action_phase = (float)(BOW_NOCK_TICKS - 1) / (float)BOW_DRAW_TICKS;
        for (int i = 0; i < PUPPET_AIM_BLEND_TICKS * 2; i++){
            a.Tick(draw);
        }
        CheckNear(a.aim_weight,0.0f,0.0001f,"before the nock the aim does not touch her");
        CheckNear(a.upper_weight,1.0f,0.0001f,"though the upper layer is fully on");
        draw.action_phase = (float)BOW_NOCK_TICKS / (float)BOW_DRAW_TICKS;
        for (int i = 0; i < PUPPET_AIM_BLEND_TICKS - 1; i++){
            a.Tick(draw);
        }
        Check(a.aim_weight > 0.0f && a.aim_weight < 1.0f,"from the nock the aim eases in rather than snapping");
        a.Tick(draw);
        CheckNear(a.aim_weight,1.0f,0.0001f,"and holds fully after PUPPET_AIM_BLEND_TICKS");

        ArcherAnimParams idle;
        a.Tick(idle);
        Check(a.upper_weight < 1.0f && a.choice.upper_clip == CLIP_DRAW,
              "letting go fades the layer out of the pose she let go in");
        for (int i = 0; i < PUPPET_AIM_BLEND_TICKS; i++){
            a.Tick(idle);
        }
        CheckNear(a.aim_weight,0.0f,0.0001f,"and the aim lets go over the same ticks");
        Check(a.upper_weight == 0.0f && a.upper_latched < 0,"until the layer is off and forgotten");

        //At a run: the legs run, the layer draws, and from the nock the aim bends her.
        Puppet r;
        ArcherAnimParams run_draw;
        run_draw.action = ACTION_DRAW;
        run_draw.action_phase = 1.0f;
        run_draw.speed = 5.0f;
        run_draw.ground_speed = 5.0f;
        r.clip_speed[CLIP_RUN_FAST] = 2.5f;
        r.model_scale = 2.0f;
        for (int i = 0; i < PUPPET_AIM_BLEND_TICKS * 2; i++){
            r.Tick(run_draw);
        }
        Check(!Puppet::IsDrawPose(r.choice.clip),"a draw at a run keeps her legs running");
        Check(r.choice.upper_clip == CLIP_AIM_IDLE && r.upper_weight == 1.0f,"with the draw on her upper body");
        CheckNear(r.aim_weight,1.0f,0.0001f,"and the aim bending her - the layer is what makes that right");
    }

    /*
        And the seam itself: the rules, read into the parameters.

        Signed along FACING rather than along +X, which is the property the blend space will run
        on - so it is worth pinning down before anything depends on it.
    */
    Stage s;
    ArcherInput right;
    right.move_axis = 1.0f;
    Run(s,30,right);
    ArcherAnimParams got;
    DescribeArcher(s,got);
    Check(got.facing > 0.0f,"running right faces right");
    Check(got.speed > 0.0f,"and reports a POSITIVE speed - moving the way she faces");
    CheckNear(got.ground_speed,fabsf(s.vel.x),0.0001f,"ground speed is the unsigned one");

    //Now turn her around and check the sign flips while the ground speed does not.
    ArcherInput left;
    left.move_axis = -1.0f;
    StageEvents ev;
    s.Tick(left,ev);        //one tick: still facing right, now decelerating leftward
    DescribeArcher(s,got);
    Check(got.speed < got.ground_speed + 0.0001f,"a speed relative to facing is never above the unsigned one");
}

static void TestDeterminism(){
    printf("determinism\n");

    //The same inputs from the same start must produce the same state. Cheap to check, and it is
    //the property the whole rules/view split is FOR - without it none of this is replayable and
    //none of the MCP-driven measurement means anything.
    Stage a;
    Stage b;
    ArcherInput in;
    in.move_axis = 1.0f;
    in.f_draw_down = true;

    for (int i = 0; i < 300; i++){
        ArcherInput step = in;
        step.f_jump_pressed = (i % 37 == 0);
        step.f_jump_down = (i % 37 < 12);
        step.f_draw_released = (i % 53 == 0);
        step.aim_axis = ((i / 20) % 2) ? 1.0f : -1.0f;
        StageEvents ea;
        StageEvents eb;
        a.Tick(step,ea);
        b.Tick(step,eb);
    }
    Check(a.pos.x == b.pos.x && a.pos.y == b.pos.y,"two stages fed identical input agree exactly");
    Check(a.arrows_shot == b.arrows_shot,"and have loosed the same arrows");
    Check(a.DebugLine() == b.DebugLine(),"and report the same state line");
}

/*
    The test range - Stage::BuildRangeLevel. What is asserted is what the range is FOR: a floor to
    stand on, targets on both sides of the start, walls that hold her and hold every arrow, and no
    traversal geometry at all. And that choosing it is sticky across a restart, because the app
    restarts a level with Reset and a restart that quietly went back to the main level would put
    the range's scene around the main level's rules.
*/
/*
    The straw man: in the way of the boot and of nothing else. Both halves against the range's
    real prop, offered the way RefreshObstacles offers it - a non-blocking obstacle - and each half
    against a control, so the flag is shown to be what makes the difference rather than the box
    simply being somewhere she never reaches.
*/
static const StageProp* FindStrawMan(const Stage& s){
    for (size_t i = 0; i < s.props.size(); i++){
        if (s.props[i].kind == PROP_STRAWMAN){
            return &s.props[i];
        }
    }
    return NULL;
}

static void TestStrawMan(){
    printf("the straw man\n");
    char detail[200];

    Stage probe;
    probe.SetLevel(STAGE_LEVEL_RANGE);
    const StageProp* found = FindStrawMan(probe);
    Check(found != NULL,"the range has a straw man");
    if (!found){
        return;
    }
    const StageProp man = *found;
    Check(man.x > probe.StartPosition().x,"to the right of where she starts");
    CheckNear(man.y - man.h * 0.5f,0.0f,0.001f,"standing on the floor");

    //--- She walks through it ---------------------------------------------------------------------
    ArcherInput right;
    right.move_axis = 1.0f;
    float past = man.x + man.w * 0.5f + ARCHER_HALF_W;
    for (int f_blocks = 0; f_blocks < 2; f_blocks++){
        Stage s;
        s.SetLevel(STAGE_LEVEL_RANGE);
        Settle(s);
        //Long enough to be well past it at the range's slower run speed - the first version ran 60
        //ticks, stopped at 2.97 unblocked, and so passed the control without testing it.
        for (int i = 0; i < 240; i++){
            s.ClearObstacles();
            s.AddObstacle(man.x,man.y,man.w * 0.5f,man.h * 0.5f,0,true,f_blocks != 0);
            StageEvents ev;
            s.Tick(right,ev);
        }
        snprintf(detail,sizeof(detail),"at (%.2f, %.2f); its far side is %.2f",s.pos.x,s.pos.y,past);
        if (f_blocks){
            CheckNear(s.pos.x,man.x - man.w * 0.5f - ARCHER_HALF_W,0.02f,
                      "control: the same box, blocking, stops her at its face",detail);
        }else{
            Check(s.pos.x > past,"non-blocking, she runs straight through it",detail);
            CheckNear(s.pos.y,ARCHER_HALF_H,0.01f,"along the floor, not stood on top of it",detail);
        }
    }

    //--- ...and the boot still finds it ------------------------------------------------------------
    ArcherInput idle;
    ArcherInput kick;
    kick.f_kick_pressed = true;
    ArcherInput face;
    for (int from_left = 0; from_left < 2; from_left++){
        Stage k;
        k.SetLevel(STAGE_LEVEL_RANGE);
        float side = from_left ? -1.0f : 1.0f;
        //Just off its near side, within the boot's reach.
        k.pos = v2(man.x + side * (man.w * 0.5f + ARCHER_HALF_W + 0.30f),ARCHER_HALF_H + 0.01f);
        Settle(k);
        face.move_axis = -side;
        Run(k,2,face);
        int connects = 0;
        StageEvents::StageKick got;
        for (int i = 0; i < KICK_TICKS + 4; i++){
            k.ClearObstacles();
            k.AddObstacle(man.x,man.y,man.w * 0.5f,man.h * 0.5f,5,true,false);
            StageEvents ev;
            k.Tick(i == 0 ? kick : idle,ev);
            if (ev.kicks.size() > 0){
                got = ev.kicks[0];
            }
            connects += (int)ev.kicks.size();
        }
        snprintf(detail,sizeof(detail),"%i connects from x %.2f",connects,k.pos.x);
        Check(connects == 1,from_left ? "kicked from the left, the boot finds it once"
                                      : "kicked from the right, the boot finds it once",detail);
        Check(got.id == 5 && got.dir == -side,"by its id, in the direction she faces");
    }
}

#if ARCHER_TEST_BAY
/*
    The bank behind the terrain (Backdrop.h), against the real bay. What matters: it follows the
    floor and nothing standing on it, it stays behind the slab by the gap, it reaches below, and it
    is the same bank every time.
*/
static void TestBackdrop(){
    printf("the backdrop\n");
    char detail[200];
    Stage s;
    BackdropParams params;
    std::vector<StageBlock> bank;
    std::vector<int> grounds;
    //The two bays as the app meshes them: below and above the split.
    BuildBackdropBlocks(s.blocks,ARCHER_TEST_BAY_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,
                        params,bank,NULL,&grounds);
    Check(grounds.size() == 1,"the ground bay's bank follows one block - its floor");
    Check(!bank.empty(),"and is some humps long");
    if (grounds.empty() || bank.empty()){
        return;
    }
    const StageBlock& floor = s.blocks[grounds[0]];
    snprintf(detail,sizeof(detail),"the floor block is %.2f .. %.2f at y %.2f",floor.Left(),floor.Right(),floor.Top());
    Check(floor.Bottom() < 0.0f && floor.hw > 5.0f,"which is the wide slab under the bay",detail);

    float left = 1e30f, right = -1e30f, lowest = 1e30f, front = -1e30f, highest = -1e30f;
    int ridges = 0;
    for (size_t i = 0; i < bank.size(); i++){
        left = fminf(left,bank[i].Left());
        right = fmaxf(right,bank[i].Right());
        lowest = fminf(lowest,bank[i].Bottom());
        front = fmaxf(front,bank[i].Front());
        highest = fmaxf(highest,bank[i].Top());
        if (bank[i].Front() > floor.Back() - params.wall_gap + 0.001f){
            ridges++;
        }
    }
    snprintf(detail,sizeof(detail),"wall %.2f .. %.2f",left,right);
    Check(left <= floor.Left() && right >= floor.Right(),"the wall runs the floor's whole length",detail);
    CheckNear(lowest,floor.Bottom() - params.drop_below,0.001f,"and reaches drop_below under it");
    snprintf(detail,sizeof(detail),"highest top %.2f over a floor at %.2f",highest,floor.Top());
    Check(highest > floor.Top() + params.wall_high * 0.6f,"and rises well above it - a cave's back wall, not a bank",detail);
    snprintf(detail,sizeof(detail),"%i ridges",ridges);
    Check(ridges >= 3,"ridges stand forward of the wall",detail);
    CheckNear(front,floor.Back() - params.front_gap,0.001f,"the nearest face - a ridge's - is front_gap behind the slab's back");

    //The pines: on a wall column's top, inside its footprint and its depth, well up.
    std::vector<StageBlock> again;
    std::vector<BackdropTree> trees, trees_again;
    BuildBackdropBlocks(s.blocks,ARCHER_TEST_BAY_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,
                        params,again,&trees);
    Check(trees.size() >= 3,"pines grow on the wall's high points");
    int stranded = 0;
    float yaw_lo = 1e30f, yaw_hi = -1e30f, scale_lo = 1e30f, scale_hi = -1e30f;
    for (size_t t = 0; t < trees.size(); t++){
        const BackdropTree& tr = trees[t];
        bool f_on = false;
        for (size_t i = 0; i < again.size() && !f_on; i++){
            const StageBlock& b = again[i];
            f_on = fabsf(b.Top() - tr.y) < 0.001f && tr.x > b.Left() && tr.x < b.Right() &&
                   tr.z > b.Back() && tr.z < b.Front();
        }
        stranded += (f_on && tr.y >= floor.Top() + params.tree_min_rise) ? 0 : 1;
        yaw_lo = fminf(yaw_lo,tr.yaw); yaw_hi = fmaxf(yaw_hi,tr.yaw);
        scale_lo = fminf(scale_lo,tr.scale); scale_hi = fmaxf(scale_hi,tr.scale);
    }
    snprintf(detail,sizeof(detail),"%i of %i",stranded,(int)trees.size());
    Check(stranded == 0,"every pine stands on a wall top, inside it, tree_min_rise up",detail);
    snprintf(detail,sizeof(detail),"%i pines, yaw %.2f .. %.2f, scale %.2f .. %.2f",
             (int)trees.size(),yaw_lo,yaw_hi,scale_lo,scale_hi);
    Check(trees.size() >= 3 && yaw_hi - yaw_lo > 1.0f && scale_hi - scale_lo > 0.2f,
          "and they differ in yaw and size",detail);

    std::vector<StageBlock> third;
    BuildBackdropBlocks(s.blocks,ARCHER_TEST_BAY_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,
                        params,third,&trees_again);
    bool f_same = third.size() == bank.size() && trees_again.size() == trees.size();
    for (size_t i = 0; f_same && i < bank.size(); i++){
        f_same = (third[i].y == bank[i].y) && (third[i].hh == bank[i].hh) && (third[i].z == bank[i].z);
    }
    for (size_t i = 0; f_same && i < trees.size(); i++){
        f_same = (trees_again[i].x == trees[i].x) && (trees_again[i].yaw == trees[i].yaw);
    }
    Check(f_same,"the same level grows the same wall and the same pines");

    std::vector<StageBlock> upper;
    BuildBackdropBlocks(s.blocks,ARCHER_TEST_BAY_X_MIN,ARCHER_TEST_BAY_X_MAX,ARCHER_TEST_BAY_SPLIT_Y,1e30f,
                        params,upper);
    Check(upper.empty(),"the upper bay is all floaters, so it gets none");
}
#endif

/*
    The rocks (Boulders.h), against a made-up floor and wall and against the real level. What
    matters: they are at the foot of walls and nowhere else, each cluster is one big rock with
    small ones round it on the open side, every one is behind her walking line and on the top it
    claims, and the same level gets the same rocks.
*/
static void TestBoulders(){
    printf("the boulders\n");
    char detail[200];
    BoulderParams params;

    //A floor with a wall standing on its middle: two corners, one each side of the wall.
    std::vector<StageBlock> blocks;
    blocks.push_back({ 0.0f, -1.0f, 10.0f, 1.0f, BLOCK_SOLID, true });
    blocks.push_back({ 2.0f,  2.0f,  0.5f, 2.0f, BLOCK_SOLID, true });
    std::vector<BoulderCorner> corners;
    FindBoulderCorners(blocks,params,corners);
    Check(corners.size() == 2,"a wall on a floor makes two corners");
    bool f_left = false, f_right = false;
    for (size_t i = 0; i < corners.size(); i++){
        f_left  |= (corners[i].side < 0.0f && fabsf(corners[i].x - 1.5f) < 0.001f);
        f_right |= (corners[i].side > 0.0f && fabsf(corners[i].x - 2.5f) < 0.001f);
    }
    Check(f_left && f_right,"at its two faces, each opening away from it");

    //A step too low to count, and a one-way platform down on the floor: neither is a wall.
    std::vector<StageBlock> low = blocks;
    low[1].y = 0.2f; low[1].hh = 0.2f;
    FindBoulderCorners(low,params,corners);
    Check(corners.empty(),"a kerb under min_wall is not a corner");
    std::vector<StageBlock> plat = blocks;
    plat[1].kind = BLOCK_PLATFORM;
    FindBoulderCorners(plat,params,corners);
    Check(corners.empty(),"nor is a one-way platform");

    //The real level: rocks at every kind of corner it has, all of them behaving.
    Stage s;
    std::vector<Boulder> rocks;
    ScatterBoulders(s.blocks,params,rocks);
    FindBoulderCorners(s.blocks,params,corners);
    int bigs = 0, smalls = 0, bad_depth = 0, bad_top = 0, far_from_wall = 0;
    int big_by_kind[BOULDER_KIND_COUNT] = {};
    for (size_t i = 0; i < rocks.size(); i++){
        const Boulder& b = rocks[i];
        float r = params.radius[b.kind] * b.scale;
        IsBigBoulder(b.kind) ? bigs++ : smalls++;
        if (IsBigBoulder(b.kind)){
            big_by_kind[b.kind]++;
        }
        if (b.z + r > params.z_front_max + 0.001f){
            bad_depth++;
        }
        //Standing on a real top: some SOLID/LEDGE whose top is b.ground and spans x.
        bool f_on = false;
        for (size_t k = 0; k < s.blocks.size() && !f_on; k++){
            const StageBlock& t = s.blocks[k];
            f_on = (t.kind == BLOCK_SOLID || t.kind == BLOCK_LEDGE) && fabsf(t.Top() - b.ground) < 0.001f &&
                   b.x > t.Left() && b.x < t.Right();
        }
        bad_top += f_on ? 0 : 1;
        //Near SOME corner at that height - rocks are where they fell, not out on open floor.
        float nearest = 1e30f;
        for (size_t k = 0; k < corners.size(); k++){
            if (fabsf(s.blocks[corners[k].top].Top() - b.ground) < 0.001f){
                nearest = fminf(nearest,fabsf(b.x - corners[k].x));
            }
        }
        float big_radius = fmaxf(params.radius[BOULDER_BIG_1],params.radius[BOULDER_BIG_2]);
        if (nearest > big_radius * params.big_scale_max * 2.0f + params.small_reach + 0.5f){
            far_from_wall++;
        }
    }
    snprintf(detail,sizeof(detail),"%i corners, %i big, %i small",(int)corners.size(),bigs,smalls);
    Check(bigs >= 3 && smalls >= bigs * params.small_min / 2,"the level gets a few big rocks, each with small ones",detail);
    snprintf(detail,sizeof(detail),"%i of rock_big_1, %i of rock_big_2",
             big_by_kind[BOULDER_BIG_1],big_by_kind[BOULDER_BIG_2]);
    Check(big_by_kind[BOULDER_BIG_1] > 0 && big_by_kind[BOULDER_BIG_2] > 0,"and both big shapes are used",detail);
    //A big kind whose mesh is missing (radius 0) is never picked, so no cluster loses its big rock.
    BoulderParams one = params;
    one.radius[BOULDER_BIG_2] = 0.0f;
    std::vector<Boulder> only_one;
    ScatterBoulders(s.blocks,one,only_one);
    int stray = 0, big_one = 0;
    for (size_t i = 0; i < only_one.size(); i++){
        stray += (only_one[i].kind == BOULDER_BIG_2) ? 1 : 0;
        big_one += (only_one[i].kind == BOULDER_BIG_1) ? 1 : 0;
    }
    snprintf(detail,sizeof(detail),"%i of rock_big_2, %i of rock_big_1",stray,big_one);
    Check(stray == 0 && big_one == bigs,"a big shape that did not load is never chosen",detail);
    snprintf(detail,sizeof(detail),"%i of %i",bad_depth,(int)rocks.size());
    Check(bad_depth == 0,"every rock stays behind z_front_max, off her walking line",detail);
    snprintf(detail,sizeof(detail),"%i of %i",bad_top,(int)rocks.size());
    Check(bad_top == 0,"every rock stands on a top that is there",detail);
    snprintf(detail,sizeof(detail),"%i of %i",far_from_wall,(int)rocks.size());
    Check(far_from_wall == 0,"and every one is within a cluster's reach of a wall's foot",detail);

    std::vector<Boulder> again;
    ScatterBoulders(s.blocks,params,again);
    bool f_same = again.size() == rocks.size();
    for (size_t i = 0; f_same && i < rocks.size(); i++){
        f_same = again[i].x == rocks[i].x && again[i].z == rocks[i].z && again[i].yaw == rocks[i].yaw;
    }
    Check(f_same,"the same level gets the same rocks");
}

/*
    The slide gallery (Stage::BuildSlideGallery) - the ramps and the slide rule on them. Each claim
    per hill, against SLIDE_GALLERY_DEG, so a change to the slip angle moves the line the checks
    draw rather than breaking them.
*/
static void PlaceOn(Stage& s, float x, float y){
    s.pos = v2(x,y + ARCHER_HALF_H + 0.02f);
    s.vel = v2(0.0f,0.0f);
    s.mode = MODE_AIR;
    s.f_on_ground = false;
    ArcherInput idle;
    for (int i = 0; i < 30 && !s.f_on_ground; i++){
        StageEvents e;
        s.Tick(idle,e);
    }
}

static void TestSlideGallery(){
    printf("the slide gallery\n");
    char detail[240];
    Stage base;
    base.SetLevel(STAGE_LEVEL_ROPE);
    const float gy = SLIDE_GALLERY_FLOOR_Y;
    Check((int)base.ramps.size() == SLIDE_GALLERY_HILLS * 2 + 2,"the rope level has the gallery's ramps");
    if ((int)base.ramps.size() < SLIDE_GALLERY_HILLS * 2 + 2){
        return;
    }

    //Sealed: every hill ramp has its low end on the floor and its high end on a block's top edge.
    int open = 0;
    for (int i = 0; i < SLIDE_GALLERY_HILLS * 2 + 1; i++){
        const StageRamp& r = base.ramps[i];
        v2 low = (r.a.y < r.b.y) ? r.a : r.b;
        v2 high = (r.a.y < r.b.y) ? r.b : r.a;
        bool f_edge = false;
        for (size_t k = 0; k < base.blocks.size() && !f_edge; k++){
            const StageBlock& b = base.blocks[k];
            f_edge = fabsf(b.Top() - high.y) < 0.001f &&
                     (fabsf(b.Left() - high.x) < 0.001f || fabsf(b.Right() - high.x) < 0.001f);
        }
        open += (fabsf(low.y - gy) < 0.001f && f_edge) ? 0 : 1;
    }
    snprintf(detail,sizeof(detail),"%i open",open);
    Check(open == 0,"every ramp is sealed: its foot on the floor, its top against a block's edge",detail);

    ArcherInput idle, left, right, down_left;
    left.move_axis = -1.0f;
    right.move_axis = 1.0f;
    down_left.move_axis = -1.0f;
    down_left.f_down_held = true;
    int still_wrong = 0, climb_wrong = 0, hops = 0;
    int prev_ticks = 100000;
    bool f_faster = true;
    for (int h = 0; h < SLIDE_GALLERY_HILLS; h++){
        const float deg = SLIDE_GALLERY_DEG[h];
        const bool f_slips = deg > SPRING_LEAF_SLIP_DEG + 0.01f;
        const StageRamp& up = base.ramps[h * 2];            //met from the right, rising leftward
        const StageRamp& down = base.ramps[h * 2 + 1];      //falling leftward

        //Stood still halfway down a descent: held below the slip angle, slid to the foot above it.
        Stage s = base;
        float xm = (down.a.x + down.b.x) * 0.5f;
        PlaceOn(s,xm,down.SurfaceY(xm));
        float x0 = s.pos.x;
        int to_foot = -1;
        for (int t = 0; t < 240; t++){
            StageEvents e;
            s.Tick(idle,e);
            if (to_foot < 0 && s.pos.x <= down.a.x){
                to_foot = t;
            }
        }
        if (f_slips ? (to_foot < 0) : (fabsf(s.pos.x - x0) > 0.01f)){
            still_wrong++;
            printf("        %.0f deg stood still: moved %.3f, foot at tick %i\n",deg,x0 - s.pos.x,to_foot);
        }
        if (f_slips){
            f_faster = f_faster && to_foot < prev_ticks;
            prev_ticks = to_foot;
        }

        /*
            From rest a little way up an ascent, pushing up it: climbs below the slip angle, slides
            back down above it - the feet have no grip to climb with. Judged over the first 20
            ticks: past that, one she slid off has reached the floor, which DOES grip, run up and
            coasted back up the ramp on the run-up, which is its own (fair) business.
        */
        Stage c = base;
        float xc = up.b.x - 0.25f * (up.b.x - up.a.x);
        PlaceOn(c,xc,up.SurfaceY(xc));
        float c0 = c.pos.x;
        bool f_top = false;
        float c20 = c0;
        for (int t = 0; t < 240; t++){
            StageEvents e;
            c.Tick(left,e);
            f_top = f_top || (c.pos.x < up.a.x - 0.5f && c.f_on_ground);
            if (t == 19){
                c20 = c.pos.x;
            }
        }
        //Uphill is leftward on these: sliding back is x growing.
        bool f_climb_ok = f_slips ? (c20 > c0) : f_top;
        if (!f_climb_ok){
            climb_wrong++;
            printf("        %.0f deg from rest uphill: from %.2f to %.2f after 20 ticks, top %s\n",deg,c0,c20,
                   f_top ? "reached" : "not");
        }

        //Walked down from the top, with Down held too - neither a hop nor a drop through.
        Stage d = base;
        PlaceOn(d,down.b.x + 0.8f,gy + SLIDE_GALLERY_HILL_H);
        for (int t = 0; t < 120 && d.pos.x > down.a.x - 1.5f; t++){
            StageEvents e;
            d.Tick((h % 2) ? down_left : left,e);
            hops += d.f_on_ground ? 0 : 1;
        }
    }
    snprintf(detail,sizeof(detail),"%i of %i",still_wrong,SLIDE_GALLERY_HILLS);
    Check(still_wrong == 0,"stood still, she holds at the slip angle and below, and slides to the foot above it",detail);
    Check(f_faster,"and the steeper the hill, the sooner she is at its foot");
    snprintf(detail,sizeof(detail),"%i of %i",climb_wrong,SLIDE_GALLERY_HILLS);
    Check(climb_wrong == 0,"from rest she walks up a hill she can stand on, and slides back down one she cannot",detail);
    snprintf(detail,sizeof(detail),"%i ticks off the ground",hops);
    Check(hops == 0,"walking down every hill she never leaves the ground - not at its top, not at its foot, "
                    "not with Down held",detail);

    //The long run: let go at the top, and the slide levels off at SLIDE_MAX_SPEED along the slope.
    const StageRamp& lr = base.ramps[SLIDE_GALLERY_HILLS * 2];
    Stage l = base;
    PlaceOn(l,lr.b.x + 0.6f,gy + 6.0f);
    for (int t = 0; t < 8; t++){
        StageEvents e;
        l.Tick(left,e);
    }
    float vmax = 0.0f;
    bool f_foot = false;
    for (int t = 0; t < 300; t++){
        StageEvents e;
        l.Tick(idle,e);
        vmax = fmaxf(vmax,fabsf(l.vel.x));
        f_foot = f_foot || l.pos.x <= lr.a.x;
    }
    Check(f_foot,"let go at the top of the long run, she slides all the way down");
    CheckNear(vmax,SLIDE_MAX_SPEED * cosf(SLIDE_GALLERY_LONG_DEG * 3.14159265f / 180.0f),0.05f,
              "at SLIDE_MAX_SPEED along it, and no faster");

    //The drop: off the floor's end, down its ramp and into the pit.
    const StageRamp& dr = base.ramps[SLIDE_GALLERY_HILLS * 2 + 1];
    Stage p = base;
    PlaceOn(p,dr.b.x + 1.5f,gy);
    bool f_air = false;
    for (int t = 0; t < 200; t++){
        StageEvents e;
        p.Tick(t < 20 ? left : idle,e);
        f_air = f_air || (!p.f_on_ground && p.pos.x < dr.a.x);
    }
    snprintf(detail,sizeof(detail),"ended at (%.2f, %.2f)",p.pos.x,p.pos.y - ARCHER_HALF_H);
    Check(f_air && p.f_on_ground && p.pos.x < dr.a.x && fabsf(p.pos.y - ARCHER_HALF_H - (-5.5f)) < 0.01f,
          "walked onto the drop, she slides off its end through the air into the pit",detail);
}

static void TestRange(){
    printf("the range\n");

    Stage fresh;
    Check(fresh.GetLevel() == STAGE_LEVEL_MAIN,"a Stage starts on the main level");

    Stage s;
    s.SetLevel(STAGE_LEVEL_RANGE);
    Check(s.GetLevel() == STAGE_LEVEL_RANGE,"SetLevel picks the range");
    Check(s.blocks.size() == 3,"the range is a floor and two walls");
    int ledges = 0;
    for (size_t i = 0; i < s.blocks.size(); i++){
        if (s.blocks[i].kind != BLOCK_SOLID){
            ledges++;
        }
    }
    Check(ledges == 0,"and nothing in it is a ledge, a platform or breakable");

    int left = 0;
    int right = 0;
    int floating = 0;
    int crates_left = 0;
    int crates_right = 0;
    int strawmen = 0;
    int other = 0;
    float lowest_float = 1000.0f;
    v2 start = s.StartPosition();
    for (size_t i = 0; i < s.props.size(); i++){
        const StageProp& p = s.props[i];
        if (p.kind == PROP_TARGET && p.f_floating){
            floating++;
            lowest_float = fminf(lowest_float,p.y - p.h * 0.5f);
        }else if (p.kind == PROP_TARGET){
            (p.x < start.x) ? left++ : right++;
        }else if (p.kind == PROP_CRATE){
            (p.x < start.x) ? crates_left++ : crates_right++;
        }else if (p.kind == PROP_STRAWMAN){
            strawmen++;
        }else{
            other++;
        }
    }
    Check(other == 0,"the only props are targets, crates and the straw man");
    Check(strawmen == 1,"one straw man");
    Check(left == 2 && right == 2,"two standing targets either side of the start");
    Check(floating == 5,"an arch of five floating targets");
    char detail[96];
    snprintf(detail,sizeof(detail),"lowest board bottom at y %.2f, head at %.2f",
             lowest_float,ARCHER_HALF_H * 2.0f);
    Check(lowest_float > ARCHER_HALF_H * 2.0f + 0.5f,"and she can walk under all of it",detail);
    Check(crates_left == 6 && crates_right == 6,"a stack of six crates either side");

    /*
        NO TWO PROPS START OVERLAPPING. Each is a rigid body the app builds exactly here, and two
        that begin inside each other are pushed apart by the solver on the first tick - a crate
        pyramid that starts interpenetrated opens the range by exploding. Checked on the declared
        boxes, which is what the bodies are built from.
    */
    int overlaps = 0;
    for (size_t a = 0; a < s.props.size(); a++){
        for (size_t b = a + 1; b < s.props.size(); b++){
            const StageProp& pa = s.props[a];
            const StageProp& pb = s.props[b];
            bool f_x = fabsf(pa.x - pb.x) < (pa.w + pb.w) * 0.5f;
            bool f_y = fabsf(pa.y - pb.y) < (pa.h + pb.h) * 0.5f;
            if (f_x && f_y){
                overlaps++;
            }
        }
    }
    snprintf(detail,sizeof(detail),"%i overlapping pairs",overlaps);
    Check(overlaps == 0,"no two props start inside each other",detail);

    Settle(s);
    Check(s.f_on_ground,"the archer lands on the range floor");
    CheckNear(s.pos.x,start.x,0.001f,"where the range starts her");
    CheckNear(s.pos.y,ARCHER_HALF_H,0.01f,"standing on y 0");

    //Walk into each wall for longer than it could take to reach it, and check she stops at it.
    ArcherInput run_right;
    run_right.move_axis = 1.0f;
    Run(s,600,run_right);
    snprintf(detail,sizeof(detail),"stopped at x %.3f",s.pos.x);
    Check(s.f_on_ground && s.pos.x + ARCHER_HALF_W <= 17.0f + 0.001f && s.pos.x > 15.0f,
          "running right, the right wall stops her",detail);
    ArcherInput run_left;
    run_left.move_axis = -1.0f;
    Run(s,1200,run_left);
    snprintf(detail,sizeof(detail),"stopped at x %.3f",s.pos.x);
    Check(s.f_on_ground && s.pos.x - ARCHER_HALF_W >= -17.0f - 0.001f && s.pos.x < -15.0f,
          "running left, the left wall stops her",detail);

    /*
        NO ARROW LEAVES THE RANGE, at any angle. Full power from the middle, from flat to nearly
        straight up: each one has to end stuck in the floor or a wall, inside x -17 .. 17. The lob
        is the one that matters - the walls are 8 tall, and an arrow that sails over one is gone.
        Stage has no targets of its own (those are rigid bodies the app resolves), so in here every
        arrow meets the level, which is exactly the property being asserted.
    */
    const float angles[] = { 0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 75.0f };
    int escaped = 0;
    float worst_x = 0.0f;
    float worst_angle = 0.0f;
    for (size_t a = 0; a < sizeof(angles)/sizeof(angles[0]); a++){
        Stage shot;
        shot.SetLevel(STAGE_LEVEL_RANGE);
        Settle(shot);
        shot.aim_deg = angles[a];
        ArcherInput draw;
        draw.f_draw_down = true;
        Run(shot,BOW_DRAW_TICKS + 2,draw);
        ArcherInput loose;
        loose.f_draw_released = true;
        Run(shot,1,loose);
        //Watched tick by tick and stopped at the first stick, because a stuck arrow is retired
        //after ARROW_STUCK_TICKS - look too late and a kept arrow looks exactly like a lost one.
        ArcherInput idle;
        bool f_kept = false;
        for (int t = 0; t < 600 && !f_kept; t++){
            Run(shot,1,idle);
            for (int i = 0; i < ARROW_MAX_LIVE; i++){
                const Arrow& arrow = shot.arrows[i];
                if (!arrow.f_live){
                    continue;
                }
                if (fabsf(arrow.pos.x) > fabsf(worst_x)){
                    worst_x = arrow.pos.x;
                    worst_angle = angles[a];
                }
                if (arrow.f_stuck && fabsf(arrow.pos.x) <= 17.05f){
                    f_kept = true;
                }
            }
        }
        if (!f_kept){
            escaped++;
        }
    }
    snprintf(detail,sizeof(detail),"%i of 6 escaped; furthest at x %.3f, aimed %.0f deg",
             escaped,worst_x,worst_angle);
    Check(escaped == 0,"every full-power arrow, flat to 75 degrees, ends stuck inside the range",detail);

    //THE RANGE'S ONE RULE: a lower top speed, and nothing else - the main level keeps its own.
    {
        ArcherInput run;
        run.move_axis = 1.0f;
        Stage slow;
        slow.SetLevel(STAGE_LEVEL_RANGE);
        Settle(slow);
        Run(slow,40,run);
        CheckNear(slow.vel.x,ARCHER_RANGE_RUN_SPEED,0.01f,"on the range she tops out at ARCHER_RANGE_RUN_SPEED");
        Stage fast;
        Settle(fast);
        Run(fast,40,run);
        CheckNear(fast.vel.x,ARCHER_RUN_SPEED,0.01f,"and on the main level still at ARCHER_RUN_SPEED");
    }

    s.Reset();
    Check(s.GetLevel() == STAGE_LEVEL_RANGE && s.blocks.size() == 3,"a restart stays on the range");
}

/*
    The garden - apps/archer/Foliage.h. Engine-free like the rules, so it is checked here with them.

    On a bare floor with one wall and one box standing on it, which is the smallest level that has
    all three things the scatter has to get right: open ground, an inside corner, and a stack.
*/
/*
    The level entry - GETTING UP in Stage.h. The promise under test is that NOTHING counts until
    she is up: every verb at once, held and pressed, for the whole get-up, and not one of them
    leaves a mark - not even a buffered jump firing on the tick the controls come back.
*/
static void TestGetUp(){
    printf("\ngetting up\n");
    Stage s;
    v2 start = s.pos;
    s.StartGetUp();
    char d[160];
    snprintf(d,sizeof(d),"centre at %.3f, started at %.3f",s.pos.y,start.y);
    Check(s.mode == MODE_GETUP,"StartGetUp starts the get-up");
    Check(fabsf(s.pos.y - ARCHER_HALF_H) < 0.01f && s.f_on_ground,
          "she is laid ON the floor, not left to fall onto it lying down",d);
    Check(s.pos.x == start.x,"where she started, across");

    ArcherInput all;
    all.move_axis = 1.0f;
    all.aim_axis = 1.0f;
    all.f_jump_down = true;
    all.f_jump_pressed = true;
    all.f_draw_down = true;
    all.f_kick_pressed = true;
    all.f_action_pressed = true;
    all.f_kneel_pressed = true;
    float aim_before = s.aim_deg;
    bool f_ever_moved = false, f_ever_drew = false, f_ever_kicked = false, f_ended_early = false;
    bool f_got_up_early = false;
    for (int t = 0; t < GETUP_TICKS - 1; t++){
        StageEvents e;
        s.Tick(all,e);
        if (fabsf(s.pos.x - start.x) > 0.0001f){ f_ever_moved = true; }
        if (s.bow_mode != BOW_IDLE){ f_ever_drew = true; }
        if (s.kick_ticks != 0){ f_ever_kicked = true; }
        if (s.mode != MODE_GETUP){ f_ended_early = true; }
        if (e.f_got_up){ f_got_up_early = true; }
    }
    Check(!f_ever_moved,"no run or jump moves her while she gets up");
    Check(!f_ever_drew && s.NumLiveArrows() == 0,"no draw starts, and nothing is loosed");
    Check(!f_ever_kicked,"no kick starts");
    Check(s.aim_deg == aim_before,"the aim does not tilt");
    Check(!f_ended_early && !f_got_up_early,"and it lasts the whole of GETUP_TICKS");

    StageEvents last;
    s.Tick(all,last);
    Check(last.f_got_up && s.mode == MODE_GROUND,"on the last tick she is up, on the ground");

    //The jump was pressed on every tick of the get-up. If any of it had been buffered, it would
    //fire here, on the first tick with nothing pressed.
    ArcherInput none;
    StageEvents after;
    s.Tick(none,after);
    Check(!after.f_jumped && s.vel.y <= 0.0f,"nothing pressed during it fires afterwards");

    ArcherInput run;
    run.move_axis = 1.0f;
    float x = s.pos.x;
    Run(s,10,run);
    Check(s.pos.x > x + 0.5f,"and then the controls are hers");

    Puppet p;
    p.clip_duration[CLIP_LAYING_UP] = (float)GETUP_TICKS * ARCHER_DT;
    ArcherAnimParams in;
    in.mode = MODE_GETUP;
    in.f_on_ground = true;
    PuppetChoice c = p.Choose(in);
    snprintf(d,sizeof(d),"rate %.3f",c.rate);
    Check(c.clip == CLIP_LAYING_UP && fabsf(c.rate - 1.0f) < 0.001f && c.start_time == 0.0f,
          "the Puppet plays Laying_StandingUp whole, from its first frame, at 1.00",d);
}

static void TestFoliage(){
    printf("\nfoliage\n");
    std::vector<StageBlock> blocks;
    blocks.push_back({   0.0f, -1.0f, 12.0f, 1.0f, BLOCK_SOLID,     true });   //floor, x -12..12, top 0
    blocks.push_back({   8.0f,  2.0f,  1.0f, 2.0f, BLOCK_SOLID,     true });   //wall,  x 7..9, top 4
    blocks.push_back({  -6.0f,  0.5f,  1.5f, 0.5f, BLOCK_SOLID,     true });   //a box on the floor, x -7.5..-4.5
    blocks.push_back({   2.0f,  3.0f,  1.0f, 0.1f, BLOCK_PLATFORM,  true });   //one-way, x 1..3
    blocks.push_back({ -10.0f,  0.5f,  0.5f, 0.5f, BLOCK_BREAKABLE, true });   //cracked wall
    std::vector<bool> grows(blocks.size(),true);
    FoliageParams params;

    float open = FoliageOcclusion(blocks,-1.0f,0.0f,params);
    float corner = FoliageOcclusion(blocks,6.95f,0.0f,params);
    char d[160];
    snprintf(d,sizeof(d),"open %.3f, foot of the wall %.3f",open,corner);
    Check(open < 0.01f,"open floor is unoccluded",d);
    Check(corner > 0.35f,"the foot of a wall is about half occluded",d);
    float lip = FoliageOcclusion(blocks,-4.55f,1.0f,params);
    snprintf(d,sizeof(d),"%.3f",lip);
    Check(lip < 0.01f,"a box's outer edge is a convex corner, and unoccluded",d);

    std::vector<FoliagePlant> plants;
    ScatterFoliage(blocks,grows,params,plants);
    snprintf(d,sizeof(d),"%zu plants",plants.size());
    Check(plants.size() > 20,"the scatter grows something",d);

    int under_box = 0, on_box = 0, on_platform = 0, on_breakable = 0, in_wall = 0;
    int near_wall = 0, in_open = 0;
    int grass_near_wall = 0, grass_in_open = 0;
    for (size_t i = 0; i < plants.size(); i++){
        const FoliagePlant& p = plants[i];
        bool f_ground = fabsf(p.y - 0.0f) < 0.001f;
        if (f_ground && p.x > -7.5f && p.x < -4.5f){ under_box++; }
        if (fabsf(p.y - 1.0f) < 0.001f){ on_box++; }
        if (fabsf(p.y - 3.1f) < 0.001f){ on_platform++; }
        if (f_ground && p.x > -10.5f && p.x < -9.5f){ on_breakable++; }
        if (f_ground && p.x > 7.0f && p.x < 9.0f){ in_wall++; }
        //The density checks are per curve: the lottery kinds thicken into the corner, grass thins.
        bool f_grass = (p.kind == FOLIAGE_GRASS) || (p.kind == FOLIAGE_GRASS_2);
        int& near = f_grass ? grass_near_wall : near_wall;
        int& open = f_grass ? grass_in_open : in_open;
        if (f_ground && p.x > 5.5f && p.x < 7.0f){ near++; }
        if (f_ground && p.x > -3.5f && p.x < 0.5f){ open++; }
    }
    Check(under_box == 0,"nothing grows on the floor underneath a box standing on it");
    Check(on_box > 0,"the box's own top grows instead");
    Check(in_wall == 0,"nothing grows inside the wall's footprint");
    Check(on_platform == 0,"nothing grows on a one-way platform");
    Check(on_breakable == 0,"nothing grows under the breakable wall");
    //Per unit of length: 1.5 units at the wall's foot against 4 of open floor.
    float per_corner = near_wall / 1.5f;
    float per_open = in_open / 4.0f;
    snprintf(d,sizeof(d),"%.1f per unit at the wall's foot, %.1f in the open",per_corner,per_open);
    Check(per_corner > 2.0f * per_open,"plants are at least twice as dense in the corner",d);
    float grass_corner = grass_near_wall / 1.5f;
    float grass_open = grass_in_open / 4.0f;
    snprintf(d,sizeof(d),"grass: %.1f per unit at the wall's foot, %.1f in the open",grass_corner,grass_open);
    Check(grass_open > 1.5f * grass_corner && grass_open > 2.0f,
          "grass the other way round: thickest in the open, thinning into the corner",d);
    //Every plant within its own block's depth, with the scatter's margins. The test blocks are all
    //the default depth, so this is the default slab.
    bool f_in_depth = true;
    for (size_t i = 0; i < plants.size(); i++){
        if (plants[i].z < params.z_back - 0.001f || plants[i].z > params.z_front + 0.001f){
            f_in_depth = false;
        }
    }
    Check(f_in_depth,"every plant stands within its block's depth");

    std::vector<FoliagePlant> again;
    ScatterFoliage(blocks,grows,params,again);
    bool f_same = (again.size() == plants.size());
    for (size_t i = 0; f_same && i < plants.size(); i++){
        f_same = (again[i].x == plants[i].x && again[i].z == plants[i].z &&
                  again[i].kind == plants[i].kind);
    }
    Check(f_same,"the same blocks grow the same garden");

    std::vector<bool> none(blocks.size(),false);
    ScatterFoliage(blocks,none,params,again);
    Check(again.empty(),"a block the mask excludes grows nothing");
}

/*
    The vines - apps/archer/Vine.h. Checked on the real level's declarations, with the placeholder
    pieces, because what can go wrong is specific to where they are: a path laid through a block,
    a leaf buried in the ground it lies on. The curve and the deform themselves are checked by
    tools/spline_test.cpp.
*/
static void TestVines(){
    printf("\nvines\n");
    Stage s;
    std::vector<VinePath> paths;
    DeclareVines(STAGE_LEVEL_MAIN,paths);
    Check(!paths.empty(),"the main level declares vines");
    std::vector<VinePath> none;
    DeclareVines(STAGE_LEVEL_RANGE,none);
    Check(none.empty(),"the range has none");

    std::vector<vertex> tile;
    MakeVinePlaceholderTile(tile);
    std::vector<vertex> leaf_mesh;
    MakeVinePlaceholderLeaf(leaf_mesh);
    VineParams params;
    params.tile_radius = VineTileRadius(tile);
    float leaf_len = 0.0f;
    for (size_t i = 0; i < leaf_mesh.size(); i++){
        if (leaf_mesh[i].pos.z > leaf_len){ leaf_len = leaf_mesh[i].pos.z; }
    }
    params.leaf_length = leaf_len;
    char d[160];
    snprintf(d,sizeof(d),"radius %.3f, leaf %.3f",params.tile_radius,params.leaf_length);
    Check(params.tile_radius > 0.08f && params.tile_radius < 0.13f,"the placeholder is measured, not assumed",d);

    //The same hemisphere test the deform's own check uses: every face of the placeholder tile
    //faces away from its axis, or the trunk renders inside out.
    int inward = 0;
    for (size_t i = 0; i + 2 < tile.size(); i += 3){
        vec3 n = (tile[i + 1].pos - tile[i].pos).cross(tile[i + 2].pos - tile[i].pos);
        vec3 c = (tile[i].pos + tile[i + 1].pos + tile[i + 2].pos) / 3.0f;
        if (n.x * c.x + n.y * c.y <= 0.0f){ inward++; }
    }
    snprintf(d,sizeof(d),"%i of %zu",inward,tile.size() / 3);
    Check(inward == 0,"the placeholder tile's faces point outward",d);

    int total_leaves = 0, buried = 0, far_from_trunk = 0, backwards = 0, in_margin = 0;
    float worst_off_axis = 0.0f;
    for (size_t v = 0; v < paths.size(); v++){
        Spline sp;
        bool f_built = BuildVineSpline(paths[v],sp);
        snprintf(d,sizeof(d),"vine %zu, length %.2f",v,sp.GetLength());
        Check(f_built && sp.GetLength() > 1.0f,"each vine builds a curve of some length",d);

        std::vector<vertex> trunk;
        int tiles = BuildVineTrunk(sp,paths[v],tile,params,trunk);
        Check(tiles > 0 && trunk.size() == tile.size() * (size_t)tiles,"...and a trunk of whole tiles",d);
        //The wrap overlay is laid copy for copy with the trunk even though it overhangs the
        //period - a stand-in here, the tile fattened and stretched past both ends the way
        //vine_curl's slanted strands are.
        std::vector<vertex> overlay = tile;
        for (size_t i = 0; i < overlay.size(); i++){
            overlay[i].pos = vec3(overlay[i].pos.x * 1.4f,overlay[i].pos.y * 1.4f,overlay[i].pos.z * 1.06f - 0.01f);
        }
        std::vector<vertex> wrap;
        int wraps = BuildVineOverlay(sp,paths[v],overlay,tile,params,wrap);
        snprintf(d,sizeof(d),"vine %zu: %i wrap copies, %i trunk tiles",v,wraps,tiles);
        Check(wraps == tiles,"...and the wrap lays exactly one copy per trunk tile",d);
        //No vertex further from the curve than the widest the cross-section gets.
        float bound = params.tile_radius * params.tile_scale * paths[v].thickness;
        for (size_t i = 0; i < trunk.size(); i += 7){
            float off = trunk[i].pos.distance(sp.PositionAt(sp.ClosestDistance(trunk[i].pos)));
            if (off - bound > worst_off_axis){ worst_off_axis = off - bound; }
        }

        std::vector<VineLeaf> leaves;
        ScatterVineLeaves(sp,paths[v],params,&s.blocks,leaves);
        total_leaves += (int)leaves.size();
        for (size_t i = 0; i < leaves.size(); i++){
            const VineLeaf& l = leaves[i];
            vec3 fwd = l.rotation * vec3(0.0f,0.0f,1.0f);
            vec3 tip = l.position + fwd * (params.leaf_length * l.scale);
            for (size_t k = 0; k < s.blocks.size(); k++){
                const StageBlock& b = s.blocks[k];
                if (b.f_alive && tip.x > b.Left() && tip.x < b.Right() && tip.y > b.Bottom() && tip.y < b.Top()){
                    buried++;
                    break;
                }
            }
            vec3 on_axis = sp.PositionAt(l.s);
            if (l.position.distance(on_axis) > bound * 1.01f){ far_from_trunk++; }
            //Toward the growing tip: the blade leans along the curve, never back down it.
            if (fwd.dot(sp.TangentAt(l.s)) <= 0.0f){ backwards++; }
            if (l.s < params.leaf_end_margin - 0.05f || l.s > sp.GetLength() - params.leaf_end_margin + 0.05f){
                in_margin++;
            }
        }
        //Deterministic: grown twice, the same leaves.
        std::vector<VineLeaf> again;
        ScatterVineLeaves(sp,paths[v],params,&s.blocks,again);
        bool f_same = again.size() == leaves.size();
        for (size_t i = 0; f_same && i < leaves.size(); i++){
            f_same = again[i].position.distance(leaves[i].position) == 0.0f && again[i].kind == leaves[i].kind;
        }
        Check(f_same,"the same vine grows the same leaves",d);
    }
    snprintf(d,sizeof(d),"worst %.4f beyond the cross-section",worst_off_axis);
    Check(worst_off_axis < 0.005f,"the trunk stays within its cross-section of the curve",d);
    snprintf(d,sizeof(d),"%i leaves",total_leaves);
    Check(total_leaves > 20,"the vines grow leaves",d);
    snprintf(d,sizeof(d),"%i buried",buried);
    Check(buried == 0,"no leaf tip is inside a block",d);
    snprintf(d,sizeof(d),"%i",far_from_trunk);
    Check(far_from_trunk == 0,"every stem is on the trunk",d);
    snprintf(d,sizeof(d),"%i",backwards);
    Check(backwards == 0,"every leaf leans toward the tip",d);
    snprintf(d,sizeof(d),"%i",in_margin);
    Check(in_margin == 0,"no leaf in the tapered ends",d);

    //And that the blocks were what kept them out: with none given, the vine along the ground
    //buries some - otherwise the check above has no teeth.
    Spline sp;
    BuildVineSpline(paths[0],sp);
    std::vector<VineLeaf> blind;
    ScatterVineLeaves(sp,paths[0],params,(const std::vector<StageBlock>*)NULL,blind);
    int blind_buried = 0;
    for (size_t i = 0; i < blind.size(); i++){
        vec3 tip = blind[i].position + (blind[i].rotation * vec3(0.0f,0.0f,1.0f)) * (params.leaf_length * blind[i].scale);
        for (size_t k = 0; k < s.blocks.size(); k++){
            const StageBlock& b = s.blocks[k];
            if (b.f_alive && tip.x > b.Left() && tip.x < b.Right() && tip.y > b.Bottom() && tip.y < b.Top()){
                blind_buried++;
                break;
            }
        }
    }
    snprintf(d,sizeof(d),"%i of %zu buried without them",blind_buried,blind.size());
    Check(blind_buried > 0,"...and it is the blocks that keep them out",d);

    //QuatFromBasis against quat's own rotation: the axes it was given must be where X, Y, Z go.
    vec3 bx = vec3(0.36f,0.48f,-0.8f);
    vec3 bz = vec3(0.8f,-0.6f,0.0f);
    vec3 by = bz.cross(bx);
    quat q = QuatFromBasis(bx,by,bz);
    float err = (q * vec3(1,0,0)).distance(bx) + (q * vec3(0,1,0)).distance(by) + (q * vec3(0,0,1)).distance(bz);
    snprintf(d,sizeof(d),"error %.2e",err);
    Check(err < 1e-5f,"QuatFromBasis maps X, Y, Z onto the basis",d);
    //And the branches Shepperd's method takes for a trace <= 0: a half turn about each axis.
    float worst_q = 0.0f;
    for (int a = 0; a < 3; a++){
        vec3 x = (a == 0) ? vec3(1,0,0) : vec3(-1,0,0);
        vec3 y = (a == 1) ? vec3(0,1,0) : vec3(0,-1,0);
        vec3 z = (a == 2) ? vec3(0,0,1) : vec3(0,0,-1);
        quat h = QuatFromBasis(x,y,z);
        float e = (h * vec3(1,0,0)).distance(x) + (h * vec3(0,1,0)).distance(y) + (h * vec3(0,0,1)).distance(z);
        if (e > worst_q){ worst_q = e; }
    }
    snprintf(d,sizeof(d),"error %.2e",worst_q);
    Check(worst_q < 1e-5f,"...including half turns",d);
}

//--- Kneeling -------------------------------------------------------------------------------------
/*
    C kneels and C stands (animation_plan.md, Step 2): a stance with timed transitions, a shorter
    body, no run, jump, kick or turn - and a bow that still draws, lower. Written against the
    KNEEL_* constants, so re-timing the clips does not turn this red.
*/
static void TestKneel(){
    printf("\nkneeling\n");
    char detail[200];
    ArcherInput idle;
    ArcherInput press;
    press.f_kneel_pressed = true;

    Stage s;
    s.SetLevel(STAGE_LEVEL_RANGE);
    Settle(s);
    float feet = s.pos.y - ARCHER_HALF_H;
    float facing = s.facing;
    StageEvents e;
    s.Tick(press,e);
    Check(e.f_knelt && s.mode == MODE_KNEEL && s.kneel_phase == KNEEL_LOWERING,
          "C on the ground starts her down onto one knee");
    CheckNear(s.BodyHeight(),2.0f * KNEEL_HALF_H,0.0001f,"and the body box is the kneeling one at once");
    CheckNear(s.pos.y - ARCHER_HALF_H,feet,0.0001f,"with its feet where they were");
    Run(s,KNEEL_DOWN_TICKS - 2,idle);
    Check(s.kneel_phase == KNEEL_LOWERING,"still going down one tick before KNEEL_DOWN_TICKS");
    Run(s,1,idle);
    Check(s.kneel_phase == KNEEL_HELD,"and down on the knee at KNEEL_DOWN_TICKS");
    CheckNear(s.KneelAmount(),1.0f,0.0001f,"fully kneeling");

    //Nothing moves her.
    ArcherInput go;
    go.move_axis = -1.0f;
    go.f_jump_pressed = true;
    go.f_jump_down = true;
    go.f_kick_pressed = true;
    float x = s.pos.x;
    Run(s,30,go);
    CheckNear(s.pos.x,x,0.0001f,"kneeling, the stick does not move her");
    Check(s.facing == facing,"or turn her round");
    Check(s.mode == MODE_KNEEL && s.f_on_ground,"a jump press does not jump");
    Check(s.kick_ticks == 0,"and a kick press does not kick");
    Run(s,10,idle);
    Check(s.mode == MODE_KNEEL && s.vel.y <= 0.0f,"nor does a buffered jump fire later");

    //The bow still works, from lower down.
    Stage stand;
    stand.SetLevel(STAGE_LEVEL_RANGE);
    Settle(stand);
    CheckNear(s.AnchorPosition().y - stand.AnchorPosition().y,KNEEL_NOCK_UP - BOW_NOCK_UP,0.0001f,
              "the kneeling anchor is lower by exactly the measured difference");
    ArcherInput draw;
    draw.f_draw_down = true;
    Run(s,BOW_DRAW_TICKS + 20,draw);
    Check(s.IsNocked(),"she draws while kneeling");
    v3 arc[AIM_ARC_POINTS];
    int n = s.PredictArc(arc,AIM_ARC_POINTS);
    Check(n > 0,"and the arc is drawn from the kneeling anchor");
    ArcherInput loose;
    loose.f_draw_released = true;
    StageEvents le;
    s.Tick(loose,le);
    Check(le.f_shot && s.mode == MODE_KNEEL,"and looses without standing up");

    //Standing up.
    StageEvents ue;
    s.Tick(press,ue);
    Check(s.kneel_phase == KNEEL_RISING,"C again starts her standing up");
    CheckNear(s.BodyHeight(),2.0f * ARCHER_HALF_H,0.0001f,"with the full box from the first tick up");
    bool f_stood = false;
    for (int i = 0; i < KNEEL_UP_TICKS && !f_stood; i++){
        StageEvents te;
        s.Tick(idle,te);
        f_stood = te.f_stood;
    }
    Check(f_stood && s.mode == MODE_GROUND,"and she is standing after KNEEL_UP_TICKS");
    ArcherInput run;
    run.move_axis = 1.0f;
    Run(s,10,run);
    Check(s.vel.x > 0.0f,"and runs again");

    //A press during a transition is ignored.
    Stage t;
    t.SetLevel(STAGE_LEVEL_RANGE);
    Settle(t);
    Run(t,1,press);
    Run(t,5,press);
    Check(t.mode == MODE_KNEEL && t.kneel_phase == KNEEL_LOWERING,
          "pressing C while going down does not turn her round halfway");

    //Not from the air.
    Stage a;
    a.SetLevel(STAGE_LEVEL_RANGE);
    a.pos.y += 3.0f;
    a.f_on_ground = false;
    a.mode = MODE_AIR;
    Run(a,1,press);
    Check(a.mode != MODE_KNEEL,"C in the air does nothing");

    //At a run she brakes to a stop.
    Stage r;
    r.SetLevel(STAGE_LEVEL_RANGE);
    Settle(r);
    Run(r,20,run);
    float vx = r.vel.x;
    Run(r,1,press);
    Run(r,8,idle);
    snprintf(detail,sizeof(detail),"running at %.2f, %.3f left after 8 ticks",vx,r.vel.x);
    Check(r.mode == MODE_KNEEL && r.vel.x == 0.0f,"kneeling at a run brakes her to a stop",detail);

    //No room to stand: something low overhead.
    Stage low;
    low.SetLevel(STAGE_LEVEL_RANGE);
    Settle(low);
    Run(low,1,press);
    Run(low,KNEEL_DOWN_TICKS + 2,idle);
    float kneel_top = low.pos.y - ARCHER_HALF_H + 2.0f * KNEEL_HALF_H;
    float stand_top = low.pos.y + ARCHER_HALF_H;
    float cy = (kneel_top + stand_top) * 0.5f + 0.25f;
    low.ClearObstacles();
    low.AddObstacle(low.pos.x,cy,1.0f,0.25f,7,false);
    Check(!low.CanStandUp(),"a box between the kneeling and standing heads leaves no room to stand");
    StageEvents be;
    low.Tick(press,be);
    Check(be.f_stand_blocked && low.kneel_phase == KNEEL_HELD,"so C is refused and she stays down");
    low.ClearObstacles();
    StageEvents ge;
    low.Tick(press,ge);
    Check(low.kneel_phase == KNEEL_RISING,"and stands once it is gone");
}

//--- The aim sway -------------------------------------------------------------------------------
static void TestSway(){
    printf("\naim sway\n");
    ArcherInput draw;
    draw.f_draw_down = true;

    Stage s;
    s.SetLevel(STAGE_LEVEL_RANGE);
    Settle(s);
    Run(s,BOW_NOCK_TICKS + 1,draw);
    Check(s.IsNocked(),"nocked");
    CheckNear(s.AimSwayDeg(),0.0f,0.0001f,"the sway is exactly zero on the nock, so the arc does not jump");
    CheckNear(s.AimSwaySideDeg(),0.0f,0.0001f,"and so is its sideways half");

    //Bounded by the amplitude, and actually moving - both halves, inside the cone's circle.
    float lo = 0.0f;
    float hi = 0.0f;
    float side_lo = 0.0f;
    float side_hi = 0.0f;
    float widest = 0.0f;
    float apart = 0.0f;
    bool  f_flat = true;
    bool  f_as_2d = true;
    const float D2R = 3.14159265358979f / 180.0f;
    for (int i = 0; i < 600; i++){
        Run(s,1,draw);
        float w = s.AimSwayDeg();
        float side = s.AimSwaySideDeg();
        if (w < lo){ lo = w; }
        if (w > hi){ hi = w; }
        if (side < side_lo){ side_lo = side; }
        if (side > side_hi){ side_hi = side; }
        widest = fmaxf(widest,sqrtf(w * w + side * side));
        apart = fmaxf(apart,fabsf(w - side));
        //On the range the plane is locked: the aim is the 2D one, to the bit, and never leaves it.
        v3 d = s.AimDirection();
        float a = s.ShotAimDeg() * D2R;
        f_flat = f_flat && d.z == 0.0f;
        f_as_2d = f_as_2d && d.x == cosf(a) * s.facing && d.y == sinf(a);
    }
    char detail[160];
    snprintf(detail,sizeof(detail),"%.2f .. %.2f over ten seconds",lo,hi);
    Check(hi <= AIM_SWAY_STAND_DEG && lo >= -AIM_SWAY_STAND_DEG,"standing, it stays inside AIM_SWAY_STAND_DEG",detail);
    Check(hi - lo > AIM_SWAY_STAND_DEG,"and really drifts",detail);
    CheckNear(s.ShotAimDeg(),s.aim_deg + s.AimSwayDeg(),0.0001f,"the shot's angle is the aim plus the sway");
    snprintf(detail,sizeof(detail),"sideways %.2f .. %.2f, widest %.3f of %.1f, halves up to %.2f apart",
             side_lo,side_hi,widest,AIM_SWAY_STAND_DEG,apart);
    Check(side_hi - side_lo > AIM_SWAY_STAND_DEG,"the sideways half drifts as far",detail);
    Check(widest <= AIM_SWAY_STAND_DEG + 0.0001f,"and the two together stay inside the cone's circle",detail);
    Check(apart > 1.0f,"on a curve of their own - the halves are not one drift twice",detail);
    Check(f_flat && f_as_2d,"on a locked plane the aim is the 2D aim exactly, with no z at all");

    //Kneeling narrows it.
    Stage k;
    k.SetLevel(STAGE_LEVEL_RANGE);
    Settle(k);
    ArcherInput press;
    press.f_kneel_pressed = true;
    ArcherInput idle;
    Run(k,1,press);
    Run(k,KNEEL_DOWN_TICKS + 2,idle);
    Run(k,BOW_NOCK_TICKS + 1,draw);
    float klo = 0.0f;
    float khi = 0.0f;
    float kwidest = 0.0f;
    for (int i = 0; i < 600; i++){
        Run(k,1,draw);
        float w = k.AimSwayDeg();
        float side = k.AimSwaySideDeg();
        if (w < klo){ klo = w; }
        if (w > khi){ khi = w; }
        kwidest = fmaxf(kwidest,sqrtf(w * w + side * side));
    }
    snprintf(detail,sizeof(detail),"%.2f .. %.2f kneeling, widest %.3f",klo,khi,kwidest);
    Check(khi <= AIM_SWAY_KNEEL_DEG && klo >= -AIM_SWAY_KNEEL_DEG,"kneeling, it stays inside AIM_SWAY_KNEEL_DEG",detail);
    Check(kwidest <= AIM_SWAY_KNEEL_DEG + 0.0001f,"and the whole cone narrows with it",detail);

    //Two draws do not sway alike, and the same draw in a second Stage does.
    Stage a;
    Stage b;
    a.SetLevel(STAGE_LEVEL_RANGE);
    b.SetLevel(STAGE_LEVEL_RANGE);
    Settle(a);
    Settle(b);
    Run(a,BOW_NOCK_TICKS + 40,draw);
    Run(b,BOW_NOCK_TICKS + 40,draw);
    CheckNear(a.AimSwayDeg(),b.AimSwayDeg(),0.0f,"the sway is the same in a replay");
    CheckNear(a.AimSwaySideDeg(),b.AimSwaySideDeg(),0.0f,"both halves of it");
    ArcherInput loose;
    loose.f_draw_released = true;
    Run(a,1,loose);
    Run(a,BOW_NOCK_TICKS + 40,draw);
    Check(fabsf(a.AimSwayDeg() - b.AimSwayDeg()) > 0.05f,"but the next draw sways differently");
    Check(fabsf(a.AimSwaySideDeg() - b.AimSwaySideDeg()) > 0.05f,"sideways too");

    //A locked shot never leaves the plane: z, vz and yaw are exactly 0 from the loose to the wall.
    Stage f;
    f.SetLevel(STAGE_LEVEL_RANGE);
    Settle(f);
    f.aim_deg = 10.0f;
    Run(f,BOW_DRAW_TICKS + 30,draw);
    StageEvents fe;
    f.Tick(loose,fe);
    bool f_in_plane = fe.f_shot;
    for (int t = 0; t < 120; t++){
        for (int i = 0; i < ARROW_MAX_LIVE; i++){
            const Arrow& ar = f.arrows[i];
            if (ar.f_live){
                f_in_plane = f_in_plane && ar.pos.z == 0.0f && ar.prev_pos.z == 0.0f &&
                             ar.vel.z == 0.0f && ar.yaw == 0.0f;
            }
        }
        Run(f,1,idle);
    }
    Check(f_in_plane && f.arrows_hit_blocks > 0,"a locked shot flies and sticks with no z, no vz and no yaw");
}

/*
    OFF THE PLANE: the character scene, where the turntable turns her to any heading and the arrow
    leaves along it (Stage::IsPlaneLocked). The same rules as the locked plane, with the third
    component switched on - so the checks are that it points where the heading says, that the arc
    still IS the flight, that the attitude handed to the app rebuilds the direction, and that a
    block's depth now decides a hit.
*/
static void TestAimOffPlane(){
    printf("\naim off the plane\n");
    char d[200];
    ArcherInput draw;
    draw.f_draw_down = true;
    ArcherInput loose;
    loose.f_draw_released = true;
    ArcherInput idle;

    Stage range;
    range.SetLevel(STAGE_LEVEL_RANGE);
    Stage c;
    c.SetLevel(STAGE_LEVEL_CHARACTER);
    Settle(c);
    Check(range.IsPlaneLocked() && !c.IsPlaneLocked(),"every level is locked to the plane but the character scene");

    //Ahead is the heading, in the turntable's convention: 0 toward the camera, 90 to +X.
    c.aim_deg = 0.0f;
    c.heading_deg = 0.0f;
    v3 ahead = c.AimDirection();
    snprintf(d,sizeof(d),"(%.4f,%.4f,%.4f)",ahead.x,ahead.y,ahead.z);
    Check(fabsf(ahead.x) < 1e-6f && fabsf(ahead.y) < 1e-6f && fabsf(ahead.z - 1.0f) < 1e-6f,
          "undrawn and level at heading 0, the aim is +Z",d);
    CheckNear(c.AnchorPosition().z,BOW_NOCK_FWD,1e-6f,"and the anchor is ahead of her along it");
    c.heading_deg = 90.0f;
    ahead = c.AimDirection();
    snprintf(d,sizeof(d),"(%.4f,%.4f,%.4f)",ahead.x,ahead.y,ahead.z);
    Check(fabsf(ahead.x - 1.0f) < 1e-6f && fabsf(ahead.z) < 1e-6f,"at heading 90 it is +X",d);

    //Drawn, both halves of the cone show in the direction, each on its own axis.
    c.heading_deg = 30.0f;
    c.aim_deg = 25.0f;
    Run(c,BOW_DRAW_TICKS + 47,draw);
    Check(c.IsNocked() && fabsf(c.AimSwaySideDeg()) > 0.2f,"drawn on the turntable, it sways sideways");
    const float D2R = 3.14159265358979f / 180.0f;
    v3 dir = c.AimDirection();
    v3 fwd = c.Forward();
    v3 left(fwd.z,0.0f,-fwd.x);
    float up = c.ShotAimDeg() * D2R;
    float side = c.AimSwaySideDeg() * D2R;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    float along_left = dir.x * left.x + dir.z * left.z;
    snprintf(d,sizeof(d),"|d| %.6f, left %.6f for sin(side) %.6f, up %.6f for %.6f",
             len,along_left,sinf(side),dir.y,sinf(up) * cosf(side));
    CheckNear(len,1.0f,1e-5f,"the direction is a unit vector",d);
    CheckNear(along_left,sinf(side),1e-5f,"its sideways part is the sideways sway, to her left",d);
    CheckNear(dir.y,sinf(up) * cosf(side),1e-5f,"and its climb is the aim",d);

    //The arc IS the flight, in 3D as on the plane - the same comparison as TestBow's.
    v3 arc[AIM_ARC_POINTS];
    int n = c.PredictArc(arc,AIM_ARC_POINTS);
    StageEvents le;
    c.Tick(loose,le);
    int idx = -1;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (c.arrows[i].f_live && !c.arrows[i].f_stuck){ idx = i; }
    }
    Check(le.f_shot && idx >= 0 && n >= 4,"it looses, with an arc drawn");
    if (idx < 0){
        return;
    }
    CheckNear(le.shot_side_deg,side / D2R,1e-4f,"and the shot reports the sideways sway it left with");
    int compared = 0;
    float worst = 0.0f;
    bool  f_attitude = true;
    for (int k = 0; k < n && k < 8; k++){
        int want_steps = (k + 1) * AIM_ARC_TICK_STRIDE;
        while (compared < want_steps - 1 && c.arrows[idx].f_live && !c.arrows[idx].f_stuck){
            Run(c,1,idle);
            compared++;
        }
        const Arrow& a = c.arrows[idx];
        if (!a.f_live || a.f_stuck){
            break;
        }
        v3 e = a.pos - arc[k];
        worst = fmaxf(worst,sqrtf(e.x * e.x + e.y * e.y + e.z * e.z));
        //The mesh is turned by yaw(Y) * angle(Z) from +X: that has to be the way it is going.
        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y + a.vel.z * a.vel.z);
        v3 rebuilt(cosf(a.angle) * cosf(a.yaw),sinf(a.angle),-cosf(a.angle) * sinf(a.yaw));
        v3 diff = rebuilt - a.vel * (1.0f / speed);
        f_attitude = f_attitude && sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) < 1e-5f;
    }
    snprintf(d,sizeof(d),"worst divergence %.6f over %i ticks",worst,compared);
    Check(compared > 10 && worst < 0.0005f,"off the plane the drawn arc is still the flight, tick for tick",d);
    Check(f_attitude,"and the attitude the app turns the mesh by is the way it flies");
    snprintf(d,sizeof(d),"at (%.2f,%.2f,%.2f)",c.arrows[idx].pos.x,c.arrows[idx].pos.y,c.arrows[idx].pos.z);
    Check(c.arrows[idx].pos.z > 1.0f && c.arrows[idx].pos.x > 0.5f,"it went off toward heading 30, out of the plane",d);

    //Headings behind her and to -X, where the attitude has to carry the turn in `angle`.
    const float headings[] = { 180.0f, -90.0f, -150.0f };
    for (float h : headings){
        Stage b;
        b.SetLevel(STAGE_LEVEL_CHARACTER);
        Settle(b);
        b.heading_deg = h;
        b.aim_deg = 15.0f;
        Run(b,BOW_DRAW_TICKS + 5,draw);
        b.Tick(loose,le);
        int bi = -1;
        for (int i = 0; i < ARROW_MAX_LIVE; i++){
            if (b.arrows[i].f_live){ bi = i; }
        }
        if (bi < 0){
            Check(false,"a shot at another heading looses");
            continue;
        }
        const Arrow& a = b.arrows[bi];
        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y + a.vel.z * a.vel.z);
        v3 rebuilt(cosf(a.angle) * cosf(a.yaw),sinf(a.angle),-cosf(a.angle) * sinf(a.yaw));
        v3 diff = rebuilt - a.vel * (1.0f / speed);
        float hr = h * D2R;
        float ahead_dot = (a.vel.x * sinf(hr) + a.vel.z * cosf(hr)) / speed;
        snprintf(d,sizeof(d),"heading %.0f: yaw %.1f deg, angle %.1f deg, %.4f along the heading",
                 h,a.yaw / D2R,a.angle / D2R,ahead_dot);
        Check(sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) < 1e-5f && ahead_dot > 0.9f &&
              fabsf(a.yaw) <= 1.5708f,"the attitude rebuilds the flight at any heading, yaw within a quarter turn",d);
    }

    //A block's depth decides a hit off the plane: the same block misses set back, hits at z 0.
    for (int set_back = 0; set_back < 2; set_back++){
        Stage b;
        b.SetLevel(STAGE_LEVEL_CHARACTER);
        Settle(b);
        StageBlock wall = { 7.0f, 1.5f, 0.5f, 2.5f, BLOCK_SOLID, true };
        //A unit either side: the sideways sway can carry the arrow half a unit off z 0 by here.
        wall.z = set_back ? -3.0f : 0.0f;
        wall.depth = 1.0f;
        size_t wi = b.blocks.size();
        b.blocks.push_back(wall);
        b.heading_deg = 90.0f;
        b.aim_deg = 0.0f;
        Run(b,BOW_DRAW_TICKS + 5,draw);
        b.Tick(loose,le);
        bool f_hit = false;
        StageEvents::ArrowHit hit;
        for (int t = 0; t < 60 && !f_hit; t++){
            StageEvents e;
            b.Tick(idle,e);
            for (const StageEvents::ArrowHit& h : e.arrow_hits){
                if (h.block == (int)wi){ f_hit = true; hit = h; }
            }
        }
        if (set_back){
            Check(!f_hit,"a wall set back behind the arrow's line lets it pass in front");
        }else{
            snprintf(d,sizeof(d),"struck at (%.3f,%.3f,%.3f), normal (%.0f,%.0f,%.0f)",hit.point.x,hit.point.y,
                     hit.point.z,hit.normal.x,hit.normal.y,hit.normal.z);
            Check(f_hit && fabsf(hit.point.x - 6.5f) < 0.001f && hit.normal.x == -1.0f,
                  "the same wall across its line stops it at the near face",d);
        }
    }

    /*
        And through a block's FRONT or BACK face: a wall across +Z, shot into at heading 20. Not
        heading 0 with the wall across her: her own body is still swept in 2D, so a wall spanning
        her x stands her on top of it. Off to her side, the arrow reaches its x range well before
        its z range, so the face it goes in by is the back one.
    */
    {
        Stage b;
        b.SetLevel(STAGE_LEVEL_CHARACTER);
        Settle(b);
        StageBlock wall = { 3.1f, 1.5f, 1.9f, 2.5f, BLOCK_SOLID, true };
        wall.z = 6.0f;
        wall.depth = 0.5f;
        size_t wi = b.blocks.size();
        b.blocks.push_back(wall);
        b.heading_deg = 20.0f;
        b.aim_deg = 0.0f;
        Run(b,BOW_DRAW_TICKS + 5,draw);
        b.Tick(loose,le);
        bool f_hit = false;
        StageEvents::ArrowHit hit;
        for (int t = 0; t < 60 && !f_hit; t++){
            StageEvents e;
            b.Tick(idle,e);
            for (const StageEvents::ArrowHit& h : e.arrow_hits){
                if (h.block == (int)wi){ f_hit = true; hit = h; }
            }
        }
        snprintf(d,sizeof(d),"struck at (%.3f,%.3f,%.3f), normal z %.0f",hit.point.x,hit.point.y,hit.point.z,
                 hit.normal.z);
        Check(f_hit && fabsf(hit.point.z - 5.5f) < 0.001f && hit.normal.z == -1.0f,
              "a wall across +Z stops it at its back face, with that face's normal",d);
    }
}

//--- The kneel, animated ------------------------------------------------------------------------
static void TestKneelPuppet(){
    printf("\nkneeling, animated\n");
    Puppet p;
    ArcherAnimParams in;
    in.mode = MODE_KNEEL;
    in.kneel_phase = KNEEL_LOWERING;
    PuppetChoice c = p.Choose(in);
    Check(c.clip == CLIP_KNEEL_DOWN && c.start_time == 0.0f,"going down plays Stand_ToKneel from its start");
    in.kneel_phase = KNEEL_HELD;
    Check(p.Choose(in).clip == CLIP_KNEEL_IDLE,"held, Kneel_Idle");
    in.kneel_phase = KNEEL_RISING;
    Check(p.Choose(in).clip == CLIP_KNEEL_UP,"getting up, Kneel_ToStand");

    //The layer is on for the whole kneel; at rest it holds the draw's first frame.
    in.kneel_phase = KNEEL_HELD;
    for (int i = 0; i < PUPPET_UPPER_BLEND_TICKS + 2; i++){
        p.Tick(in);
    }
    Check(p.choice.upper_clip == CLIP_DRAW && p.choice.upper_phase == 0.0f,
          "kneeling at rest, the upper body is the draw's first frame");
    CheckNear(p.upper_weight,1.0f,0.0001f,"at full weight");
    Check(p.choice.upper_from_clip < 0,"with no crossfade running");

    //Draw and hold, then let go: the layer stays on and CROSSFADES back to rest.
    in.action = ACTION_DRAW;
    in.action_phase = 0.0f;
    p.Tick(in);
    Check(p.choice.upper_from_clip < 0,"starting a draw from rest needs no crossfade - it is its own first frame");
    in.action_phase = 1.0f;
    for (int i = 0; i < 20; i++){
        p.Tick(in);
    }
    Check(p.choice.upper_clip == CLIP_AIM_IDLE && p.choice.upper_from_clip < 0,
          "the draw hands over to the hold without one either");
    in.action = ACTION_NONE;
    in.action_phase = 0.0f;
    p.Tick(in);
    Check(p.choice.upper_clip == CLIP_DRAW && p.choice.upper_from_clip == CLIP_AIM_IDLE,
          "letting go while kneeling crossfades from the hold back to rest");
    CheckNear(p.upper_weight,1.0f,0.0001f,"with the layer still fully on");
    Check(p.upper_mix < 1.0f,"part way");
    for (int i = 0; i < PUPPET_UPPER_BLEND_TICKS; i++){
        p.Tick(in);
    }
    Check(p.choice.upper_from_clip < 0 && p.upper_mix == 1.0f,"and it finishes in PUPPET_UPPER_BLEND_TICKS");

    //Standing, a release fades the layer out instead - no crossfade.
    Puppet q;
    ArcherAnimParams st;
    st.action = ACTION_DRAW;
    st.action_phase = 1.0f;
    for (int i = 0; i < 20; i++){
        q.Tick(st);
    }
    st.action = ACTION_NONE;
    st.action_phase = 0.0f;
    q.Tick(st);
    Check(q.choice.upper_from_clip < 0 && q.upper_weight < 1.0f,"standing, letting go fades the layer out rather than crossfading");
}

//--- The rope test level ------------------------------------------------------------------------
/*
    One rope and nothing else. What makes it a rope TEST is that its lowest link is in reach from
    standing - which is where climbing starts - so that is asserted against FindRopePoint's own
    reach numbers rather than eyeballed.
*/
static void TestRopeLevel(){
    printf("\nthe rope test level\n");
    Stage s;
    s.SetLevel(STAGE_LEVEL_ROPE);
    int ropes = 0;
    int others = 0;
    const StageProp* rope = NULL;
    for (size_t i = 0; i < s.props.size(); i++){
        if (s.props[i].kind == PROP_ROPE_ANCHOR){ ropes++; rope = &s.props[i]; }
        else{ others++; }
    }
    Check(ropes == 1 && others == 0,"the rope level has one rope and no other props");
    Settle(s);
    Check(s.f_on_ground && s.mode == MODE_GROUND,"she lands on its floor");
    if (!rope){
        return;
    }
    //The rope's end, and the reach from her chest when standing under it (FindRopePoint).
    float end_y = rope->y - rope->h;
    float hand_y = ARCHER_HALF_H + ARCHER_HALF_H * 0.6f;
    char d[160];
    snprintf(d,sizeof(d),"rope ends at %.2f, her hands are at %.2f, reach %.2f",end_y,hand_y,ROPE_GRAB_REACH);
    Check(end_y - hand_y < ROPE_GRAB_REACH,"its end is within reach from standing, so climbing can start there",d);
    Check(end_y > ARCHER_HALF_H * 2.0f * 0.5f,"and hangs clear of the floor",d);
}

/*
    One hop: jump, holding `dir` from `delay` ticks in, and report where she lands - the top she is
    standing on once she is down again. -1000 if she never landed.
*/
static float Hop(Stage& s, float dir, int delay, float* land_x = NULL){
    ArcherInput in;
    in.f_jump_down = true;
    in.f_jump_pressed = true;
    in.move_axis = (delay <= 0) ? dir : 0.0f;
    bool f_left = false;
    for (int i = 0; i < 240; i++){
        StageEvents e;
        s.Tick(in,e);
        in.f_jump_pressed = false;
        if (i + 1 >= delay){ in.move_axis = dir; }
        if (!s.f_on_ground){ f_left = true; }
        if (f_left && s.f_on_ground){
            Run(s,3,ArcherInput());     //let go and settle, so the next hop starts from rest
            if (land_x){ *land_x = s.pos.x; }
            return s.pos.y - ARCHER_HALF_H;
        }
    }
    return -1000.0f;
}

/*
    THE TREE (plant_mechanics_plan.md 1): its arms are one-way platforms, and it is CLIMBABLE -
    proved hop by hop with plain inputs, so moving an arm or a slab says at once if the climb broke.
*/
static void TestTree(){
    printf("\nthe tree\n");
    char d[200];
    Stage s;
    Check(s.trees.size() == 1,"the main level has a tree");
    if (s.trees.empty()){
        return;
    }
    const StageTree& t = s.trees[0];
    int arms = 0;
    bool f_shape = true;
    for (const StageBlock& b : s.blocks){
        if (b.tree != 0){ continue; }
        const StageTreeArm& a = t.arms[arms];
        f_shape = f_shape && b.kind == BLOCK_PLATFORM && fabsf(b.Top() - a.top) < 1e-4f &&
                  fabsf((a.side > 0.0f ? b.Left() : b.Right()) - (t.x + a.side * t.radius)) < 1e-4f &&
                  fabsf(b.hw * 2.0f - a.length) < 1e-4f &&
                  b.Front() >= STAGE_BLOCK_MIN_COVER && b.Back() <= -STAGE_BLOCK_MIN_COVER;
        arms++;
    }
    Check(arms == (int)t.arms.size() && f_shape,
          "every arm is a one-way platform from the trunk's face out to its tip, covering the play plane");

    //Neither slab can be reached from the ground: the tree is the way up.
    float hands = ApexRise() + ARCHER_HALF_H * 2.0f;
    Check(5.5f > hands && 10.2f > hands,"both slabs are out of reach from the ground - even of a grab");

    //The climb. From under the first arm, straight up through it.
    const float arm_mid[3] = { t.x + t.radius + 1.0f, t.x - t.radius - 1.0f, t.x + t.radius + 1.0f };
    s.pos = v2(arm_mid[0],ARCHER_HALF_H + 0.001f);
    Settle(s);
    float lx = 0.0f;
    float top = Hop(s,0.0f,0,&lx);
    snprintf(d,sizeof(d),"landed on %.2f at x %.2f",top,lx);
    CheckNear(top,t.arms[0].top,0.01f,"straight up from the ground, through the first arm and onto it",d);
    top = Hop(s,-1.0f,6,&lx);
    snprintf(d,sizeof(d),"landed on %.2f at x %.2f",top,lx);
    CheckNear(top,t.arms[1].top,0.01f,"a hop up and across to the second, on the other side",d);

    //Branch one: off the second arm onto the low slab.
    Stage low = s;
    top = Hop(low,-1.0f,0,&lx);
    snprintf(d,sizeof(d),"landed on %.2f at x %.2f",top,lx);
    CheckNear(top,5.5f,0.01f,"from the second arm, a hop onto the low slab",d);

    //Branch two: up to the third arm and onto the high slab.
    top = Hop(s,1.0f,6,&lx);
    snprintf(d,sizeof(d),"landed on %.2f at x %.2f",top,lx);
    CheckNear(top,t.arms[2].top,0.01f,"back across and up to the third",d);
    top = Hop(s,1.0f,0,&lx);
    snprintf(d,sizeof(d),"landed on %.2f at x %.2f",top,lx);
    CheckNear(top,10.2f,0.01f,"and from the third, up onto the high slab",d);

    //Down through an arm with Down held, as on any one-way platform.
    Stage drop;
    drop.pos = v2(arm_mid[0],ARCHER_HALF_H + 0.001f);
    Settle(drop);
    Hop(drop,0.0f,0);
    ArcherInput down;
    down.f_down_held = true;
    Run(drop,40,down);
    Settle(drop);
    snprintf(d,sizeof(d),"ended at y %.2f",drop.pos.y - ARCHER_HALF_H);
    Check(fabsf(drop.pos.y - ARCHER_HALF_H) < 0.01f,"and Down drops back through an arm to the ground",d);
}

//--- Spring plants ------------------------------------------------------------------------------

//`out` along a leaf from its stem, as an x offset.
static float side_out(const StageSpringPlant& p, float out){
    return p.side * out;
}

static int FindSpringPlant(const Stage& s, int kind){
    for (size_t i = 0; i < s.spring_plants.size(); i++){
        if (s.spring_plants[i].kind == kind){
            return (int)i;
        }
    }
    return -1;
}

//Puts her in the air with her feet at (x, feet), still, and lets her fall until she stands.
static void DropOnto(Stage& s, float x, float feet){
    s.pos = v2(x,feet + ARCHER_HALF_H);
    s.vel = v2(0.0f,0.0f);
    s.mode = MODE_AIR;
    s.f_on_ground = false;
    s.spring_on = -1;
    ArcherInput idle;
    for (int i = 0; i < 240 && !s.f_on_ground; i++){
        StageEvents e;
        s.Tick(idle,e);
    }
}

/*
    One flight from where she is: `in` held - its jump press only on the first tick - until she is
    standing again. The highest her feet got, and the top and x she came down on.
*/
struct Flight{
    float apex = -1000.0f;
    float land = -1000.0f;
    float land_x = 0.0f;
    int   ticks = 0;
};
static Flight Fly(Stage& s, ArcherInput in, int max_ticks = 300){
    Flight f;
    bool f_left = false;
    for (int i = 0; i < max_ticks; i++){
        StageEvents e;
        s.Tick(in,e);
        in.f_jump_pressed = false;
        float feet = s.pos.y - ARCHER_HALF_H;
        if (!s.f_on_ground){
            f_left = true;
            if (feet > f.apex){ f.apex = feet; }
        }
        if (f_left && s.f_on_ground){
            f.land = feet;
            f.land_x = s.pos.x;
            f.ticks = i + 1;
            return f;
        }
    }
    return f;
}

/*
    Every moment to jump in the `window` ticks after `landed`, holding `dir`: the flight each one
    gives. The best is the timed jump; tick 0 - the jump already waiting as she lands - the worst.
*/
static Flight BestJump(const Stage& landed, float dir, int window, int* out_delay, Flight* out_first){
    Flight best;
    for (int d = 0; d < window; d++){
        Stage c = landed;
        Run(c,d,ArcherInput());
        ArcherInput jump;
        jump.f_jump_pressed = true;
        jump.f_jump_down = true;
        jump.move_axis = dir;
        Flight f = Fly(c,jump);
        if (d == 0 && out_first){
            *out_first = f;
        }
        if (f.apex > best.apex){
            best = f;
            if (out_delay){ *out_delay = d; }
        }
    }
    return best;
}

/*
    THE BOUNCE PAD AND THE LEAF (plant_mechanics_plan.md 2). Each claim the level makes about them
    is played here with plain inputs: the pad sinks and throws a hop, a TIMED jump off it reaches the
    shelf and a mistimed one does not; the leaf holds her near its stem, bends and slides her off
    when she walks out, throws her to the canopy when bounced on in time, and lets her walk back.
*/
static void TestSpringPlants(){
    printf("\nthe spring plants\n");
    char d[220];
    Stage s;
    int pad = FindSpringPlant(s,SPRING_PAD);
    int leaf = FindSpringPlant(s,SPRING_LEAF);
    Check(pad >= 0 && leaf >= 0,"the main level has a bounce pad and a leaf");
    if (pad < 0 || leaf < 0){
        return;
    }
    const float shelf_top = 7.5f;
    const float canopy_top = 13.0f;
    const float plain_reach = ApexRise() + ARCHER_HALF_H * 2.0f;
    Check(shelf_top > s.spring_plants[pad].root.y + plain_reach,
          "the shelf is out of reach of a plain jump off the pad, even of a grab");

    //--- The pad ---
    StageSpringPlant p0 = s.spring_plants[pad];
    DropOnto(s,p0.root.x,p0.root.y + ApexRise());
    Check(s.spring_on == pad,"a fall onto the cap lands on the pad");
    Stage landed = s;
    float lowest = 0.0f;
    bool f_rode_down = true;
    Stage ride = s;
    for (int i = 0; i < 30; i++){
        Run(ride,1,ArcherInput());
        const StageSpringPlant& p = ride.spring_plants[pad];
        if (p.q < lowest){
            lowest = p.q;
            f_rode_down = f_rode_down && ride.spring_on == pad &&
                          fabsf(ride.pos.y - ARCHER_HALF_H - p.SurfaceY(ride.pos.x)) < 0.01f;
        }
    }
    snprintf(d,sizeof(d),"sank %.2f of %.2f travel",-lowest,p0.travel);
    Check(lowest < -0.4f && lowest > -p0.travel + 0.01f,"the landing sinks the cap well down, short of bottoming out",d);
    Check(f_rode_down,"and she rides it down, feet on the cap");

    Stage hop = landed;
    Flight rebound = Fly(hop,ArcherInput(),200);
    snprintf(d,sizeof(d),"hop to %.2f over the cap, a plain jump rises %.2f",rebound.apex - p0.root.y,ApexRise());
    Check(rebound.apex > p0.root.y + 0.2f && rebound.apex < p0.root.y + ApexRise(),
          "left alone she rebounds a hop, lower than a jump",d);
    Stage rest = landed;
    Run(rest,400,ArcherInput());
    snprintf(d,sizeof(d),"cap at %.3f, on it %d",rest.spring_plants[pad].q,rest.spring_on == pad ? 1 : 0);
    CheckNear(rest.spring_plants[pad].q,-p0.give,0.01f,"and settles standing on it, sunk by its give",d);

    int delay = 0;
    Flight first;
    Flight timed = BestJump(landed,1.0f,45,&delay,&first);
    snprintf(d,sizeof(d),"jump %d ticks after landing: apex %.2f, came down on %.2f at x %.2f",
             delay,timed.apex,timed.land,timed.land_x);
    CheckNear(timed.land,shelf_top,0.01f,"a jump timed to the rebound, holding right, reaches the shelf",d);
    Check(timed.apex - p0.root.y <= SPRING_MAX_LAUNCH * SPRING_MAX_LAUNCH / (2.0f * ARCHER_GRAVITY) + 0.1f,
          "and no higher than SPRING_MAX_LAUNCH allows",d);
    snprintf(d,sizeof(d),"apex %.2f",first.apex);
    Check(first.apex < p0.root.y + ApexRise() + 0.3f,"a jump already waiting as she lands is a plain jump",d);

    //The grace: the jump pressed a tick or two AFTER the rebound threw her still flings.
    {
        Stage late = landed;
        int left_at = -1;
        for (int i = 0; i < 60 && left_at < 0; i++){
            Run(late,1,ArcherInput());
            if (!late.f_on_ground){ left_at = i; }
        }
        Run(late,2,ArcherInput());
        ArcherInput jump;
        jump.f_jump_pressed = true;
        jump.f_jump_down = true;
        Flight f = Fly(late,jump);
        snprintf(d,sizeof(d),"apex %.2f, pressed 3 ticks after she left the cap",f.apex);
        Check(f.apex > p0.root.y + ApexRise() + 1.0f,"a jump just after the rebound throws her still flings",d);
    }
    //Letting go of jump shortens the jump part and never the throw.
    {
        Stage c = landed;
        Run(c,delay,ArcherInput());
        ArcherInput tap;
        tap.f_jump_pressed = true;
        tap.f_jump_down = true;
        StageEvents e;
        c.Tick(tap,e);
        Flight f = Fly(c,ArcherInput());
        snprintf(d,sizeof(d),"tapped: apex %.2f; held: %.2f; the rebound alone: %.2f",f.apex,timed.apex,rebound.apex);
        Check(f.apex < timed.apex - 1.0f && f.apex > rebound.apex,"a tapped fling is lower than a held one, higher than no jump",d);
    }
    //The landing forecast sees the pad.
    {
        Stage f;
        f.pos =v2(p0.root.x,p0.root.y + 3.0f + ARCHER_HALF_H);
        f.vel = v2(0.0f,0.0f);
        f.mode = MODE_AIR;
        f.f_on_ground = false;
        f.spring_on = -1;
        StageLanding fc = f.PredictLanding(ArcherInput(),60);
        int real = 0;
        for (int i = 1; i <= 60 && !real; i++){
            StageEvents e;
            f.Tick(ArcherInput(),e);
            if (e.f_landed){ real = i; }
        }
        snprintf(d,sizeof(d),"forecast tick %d, real %d",fc.ticks,real);
        Check(fc.f_lands && fc.ticks == real && f.spring_on == pad,"the landing forecast lands her on the pad on the right tick",d);
    }

    //--- The leaf ---
    StageSpringPlant l0 = s.spring_plants[leaf];
    Stage near;
    DropOnto(near,l0.root.x + side_out(l0,1.0f),shelf_top + 0.3f);
    Run(near,300,ArcherInput());
    snprintf(d,sizeof(d),"on it %d, bent to %.1f deg",near.spring_on == leaf ? 1 : 0,near.spring_plants[leaf].SlopeDeg());
    Check(near.spring_on == leaf && near.spring_plants[leaf].SlopeDeg() > -SPRING_LEAF_SLIP_DEG,
          "standing near the stem, the leaf holds her",d);

    Stage walk;
    DropOnto(walk,114.0f,shelf_top + 0.1f);
    ArcherInput out;
    out.move_axis = 0.4f;
    bool f_slid = false;
    float steepest = 0.0f;
    for (int i = 0; i < 400; i++){
        Run(walk,1,out);
        if (walk.spring_on == leaf){
            f_slid = f_slid || walk.SlideAccel() != 0.0f;
            float a = walk.spring_plants[leaf].SlopeDeg();
            if (a < steepest){ steepest = a; }
        }
        if (walk.f_on_ground && walk.pos.y - ARCHER_HALF_H < 0.01f){
            break;
        }
    }
    snprintf(d,sizeof(d),"steepest %.1f deg, ended at (%.2f,%.2f), slid %d",
             steepest,walk.pos.x,walk.pos.y - ARCHER_HALF_H,f_slid ? 1 : 0);
    Check(f_slid && fabsf(walk.pos.y - ARCHER_HALF_H) < 0.01f && walk.pos.x > l0.root.x,
          "walked out along it, it bends under her and she slides off the end to the ground",d);
    Run(walk,300,ArcherInput());
    snprintf(d,sizeof(d),"at %.2f deg",walk.spring_plants[leaf].SlopeDeg());
    CheckNear(walk.spring_plants[leaf].SlopeDeg(),l0.rest_deg,0.5f,"and springs back to rest once she is off it",d);

    //Bounced on: land out along it from a hop off the shelf, and jump as it comes back up.
    Stage bounce;
    DropOnto(bounce,l0.root.x + side_out(l0,2.6f),shelf_top + ApexRise());
    Check(bounce.spring_on == leaf,"a fall onto the leaf lands on it");
    Flight lfirst;
    int ldelay = 0;
    Flight fling = BestJump(bounce,1.0f,50,&ldelay,&lfirst);
    snprintf(d,sizeof(d),"jump %d ticks after landing: apex %.2f, came down on %.2f at x %.2f",
             ldelay,fling.apex,fling.land,fling.land_x);
    CheckNear(fling.land,canopy_top,0.01f,"a jump timed to the leaf's spring back reaches the canopy",d);
    snprintf(d,sizeof(d),"apex %.2f",lfirst.apex);
    Check(lfirst.apex < canopy_top,"a jump already waiting as she lands does not",d);
    Check(shelf_top + plain_reach < canopy_top,"nor does any jump or grab from the shelf");

    //And back off it onto the shelf, over the step a bent leaf leaves at its stem.
    Stage back;
    DropOnto(back,l0.root.x + side_out(l0,1.6f),shelf_top + 0.3f);
    ArcherInput left;
    left.move_axis = -1.0f;
    Run(back,40,left);
    snprintf(d,sizeof(d),"ended at (%.2f,%.2f)",back.pos.x,back.pos.y - ARCHER_HALF_H);
    Check(back.f_on_ground && back.pos.x < l0.root.x - 1.0f && fabsf(back.pos.y - ARCHER_HALF_H - shelf_top) < 0.01f,
          "walking back along it steps her up onto the shelf",d);
}

/*
    THE TIMING CUE AND PUMPING. The cue (now / PredictSpringBoostPeak) must read full on the very
    tick a press gives the best jump, and nothing while the cap is still going down. The stomp and
    the swing must each add height - and only when timed: Down through the end of the fall, Up in
    the first ticks after the release.
*/
static void TestSpringPump(){
    printf("\nspring plants: the timing cue and pumping\n");
    char d[240];
    Stage s;
    int pad = FindSpringPlant(s,SPRING_PAD);
    if (pad < 0){
        Check(false,"the main level has a bounce pad");
        return;
    }
    const StageSpringPlant p0 = s.spring_plants[pad];
    DropOnto(s,p0.root.x,p0.root.y + ApexRise());
    Stage landed = s;

    //--- The cue ---
    float peak_at_landing = landed.PredictSpringBoostPeak(ArcherInput());
    float seen_max = 0.0f;
    int cue_best = -1;
    float cue_best_frac = -1.0f;
    bool f_dark_going_down = true;
    Stage c = landed;
    for (int t = 0; t < 30; t++){
        if (c.SpringBoostActive()){
            float peak = c.PredictSpringBoostPeak(ArcherInput());
            float frac = (peak > 0.0f) ? c.SpringBoostNow() / peak : 0.0f;
            if (frac > cue_best_frac + 1e-4f){
                cue_best_frac = frac;
                cue_best = t;
            }
            //On the cap only: once it has thrown her she is rising in the air, a coyote press still
            //pays, and the cap is on its way back down by then.
            if (c.f_on_ground && c.spring_on == pad && c.spring_plants[pad].qd < 0.0f && frac > 0.0f){
                f_dark_going_down = false;
            }
            if (c.SpringBoostNow() > seen_max){
                seen_max = c.SpringBoostNow();
            }
        }
        Run(c,1,ArcherInput());
    }
    snprintf(d,sizeof(d),"forecast %.2f at the landing, the most seen %.2f",peak_at_landing,seen_max);
    CheckNear(peak_at_landing,seen_max,0.01f,"at the landing, the cue already knows this bounce's best boost",d);
    int best_delay = 0;
    Flight first;
    Flight best = BestJump(landed,0.0f,30,&best_delay,&first);
    snprintf(d,sizeof(d),"the cue peaks %d ticks after landing (%.2f), the best jump is pressed at %d (apex %.2f)",
             cue_best,cue_best_frac,best_delay,best.apex);
    Check(cue_best_frac > 0.999f && abs(cue_best - best_delay) <= 1,"the cue reads full on the tick the best jump is pressed",d);
    Check(f_dark_going_down,"and reads nothing while the cap is still going down");

    //--- The stomp ---
    Stage stomped;
    {
        ArcherInput down;
        down.aim_axis = -1.0f;
        stomped.pos = v2(p0.root.x,p0.root.y + ApexRise() + ARCHER_HALF_H);
        stomped.vel = v2(0.0f,0.0f);
        stomped.mode = MODE_AIR;
        stomped.f_on_ground = false;
        float stomp = 0.0f;
        for (int i = 0; i < 240 && !stomped.f_on_ground; i++){
            StageEvents e;
            stomped.Tick(down,e);
            if (e.f_landed){ stomp = e.stomp; }
        }
        snprintf(d,sizeof(d),"stomp %.2f",stomp);
        Check(stomp > 0.999f,"Down held through the fall lands a full stomp",d);
    }
    float sink_plain = 0.0f, sink_stomp = 0.0f;
    {
        Stage a = landed, b = stomped;
        for (int i = 0; i < 20; i++){
            Run(a,1,ArcherInput());
            Run(b,1,ArcherInput());
            if (a.spring_plants[pad].q < sink_plain){ sink_plain = a.spring_plants[pad].q; }
            if (b.spring_plants[pad].q < sink_stomp){ sink_stomp = b.spring_plants[pad].q; }
        }
    }
    int stomp_delay = 0;
    Flight stomp_best = BestJump(stomped,0.0f,30,&stomp_delay,NULL);
    snprintf(d,sizeof(d),"sank %.2f against %.2f; best jump apex %.2f (at %d) against %.2f",
             -sink_stomp,-sink_plain,stomp_best.apex,stomp_delay,best.apex);
    Check(sink_stomp < sink_plain - 0.05f && stomp_best.apex > best.apex + 0.3f,
          "a stomped landing sinks the cap deeper and the timed jump off it goes higher",d);
    {
        //Held only early in the fall, then let go: it does not count.
        Stage early;
        early.pos = v2(p0.root.x,p0.root.y + ApexRise() + ARCHER_HALF_H);
        early.mode = MODE_AIR;
        float stomp = -1.0f;
        for (int i = 0; i < 240 && !early.f_on_ground; i++){
            ArcherInput in;
            in.aim_axis = (i < 8) ? -1.0f : 0.0f;
            StageEvents e;
            early.Tick(in,e);
            if (e.f_landed){ stomp = e.stomp; }
        }
        snprintf(d,sizeof(d),"stomp %.2f",stomp);
        Check(stomp == 0.0f,"Down let go before the end of the fall is no stomp",d);
    }

    //--- The swing ---
    //The rebound alone, then the same with Up pressed at various ticks after she leaves the cap.
    auto rebound_with_up = [&](int press_at, bool f_hold_from_start, bool* out_swung){
        Stage r = landed;
        int air = -1;
        float apex = -100.0f;
        bool f_swung = false;
        for (int i = 0; i < 200; i++){
            ArcherInput in;
            if (f_hold_from_start || (air >= 0 && air + 1 >= press_at && press_at >= 0)){
                in.aim_axis = 1.0f;
            }
            StageEvents e;
            r.Tick(in,e);
            f_swung = f_swung || e.f_swung;
            if (!r.f_on_ground){
                air++;
                float feet = r.pos.y - ARCHER_HALF_H;
                if (feet > apex){ apex = feet; }
            }else if (air >= 0){
                break;
            }
        }
        if (out_swung){ *out_swung = f_swung; }
        return apex;
    };
    bool f_sw = false;
    float plain = rebound_with_up(-1,false,&f_sw);
    float early_up = rebound_with_up(1,false,&f_sw);
    bool f_early = f_sw;
    float late_up = rebound_with_up(SPRING_SWING_WINDOW + 3,false,&f_sw);
    bool f_late = f_sw;
    float held_up = rebound_with_up(-1,true,&f_sw);
    bool f_held = f_sw;
    snprintf(d,sizeof(d),"rebound alone %.2f; Up at 1 tick %.2f (%d); at %d %.2f (%d); held from before %.2f (%d)",
             plain,early_up,f_early,SPRING_SWING_WINDOW + 3,late_up,f_late,held_up,f_held);
    Check(f_early && early_up > plain + 0.2f,"Up pressed as the pad throws her swings her higher",d);
    Check(!f_late && fabsf(late_up - plain) < 0.01f,"pressed too late it does nothing",d);
    Check(!f_held && fabsf(held_up - plain) < 0.01f,"and Up held since before the throw does nothing - it is a press",d);

    //Both on the timed fling: stomp in, jump on the best tick, swing out.
    {
        Stage f = stomped;
        Run(f,stomp_delay,ArcherInput());
        ArcherInput jump;
        jump.f_jump_pressed = true;
        jump.f_jump_down = true;
        float apex = -100.0f;
        for (int i = 0; i < 200; i++){
            ArcherInput in = jump;
            in.f_jump_pressed = (i == 0);
            in.aim_axis = (i == 1) ? 1.0f : 0.0f;
            StageEvents e;
            f.Tick(in,e);
            float feet = f.pos.y - ARCHER_HALF_H;
            if (feet > apex){ apex = feet; }
            if (i > 2 && f.f_on_ground){ break; }
        }
        snprintf(d,sizeof(d),"apex %.2f; timed jump alone %.2f, with the stomp %.2f; the cap allows %.2f over the cap",
                 apex,best.apex,stomp_best.apex,
                 SPRING_MAX_LAUNCH * SPRING_MAX_LAUNCH / (2.0f * ARCHER_GRAVITY));
        Check(apex >= stomp_best.apex,"stomp, timed jump and swing together go highest of all",d);
    }
}

/*
    A stand-in for a person balancing with the keys: it sees her lean LATE - `delay` ticks behind,
    about a human reaction - and answers with a full press of Up or Down or nothing, as a keyboard
    does. Looking a little ahead along the lean rate is what a person watching it tip does.
*/
struct LatePlayer{
    int delay = 10;
    float dead_deg = 3.0f;
    std::vector<v2> seen;       //(lean, rate), oldest first
    float Decide(const Stage& s){
        seen.push_back(v2(s.lean,s.lean_rate));
        if ((int)seen.size() <= delay){
            return 0.0f;
        }
        v2 then = seen[seen.size() - 1 - delay];
        float ahead = (then.x + then.y * 0.30f) * 57.2957795f;
        if (ahead > dead_deg){ return -1.0f; }
        if (ahead < -dead_deg){ return 1.0f; }
        return 0.0f;
    }
};

//Walks her along with `move` held, balancing with a LatePlayer (or not), until she is off the
//branch. Returns the tick she lost her balance, or -1 if she walked off an end still upright.
static int WalkBranch(Stage& s, float move, bool f_balance, int max_ticks, float* out_lean_max = NULL){
    LatePlayer player;
    float lean_max = 0.0f;
    bool f_was_on = false;
    for (int i = 0; i < max_ticks; i++){
        ArcherInput in;
        in.move_axis = move;
        if (f_balance){
            in.aim_axis = player.Decide(s);
        }
        StageEvents e;
        s.Tick(in,e);
        if (fabsf(s.lean) > lean_max){ lean_max = fabsf(s.lean); }
        if (e.f_lost_balance){
            if (out_lean_max){ *out_lean_max = lean_max; }
            return i;
        }
        f_was_on = f_was_on || s.branch_on >= 0;
        if (f_was_on && s.f_on_ground && s.branch_on < 0){
            break;
        }
    }
    if (out_lean_max){ *out_lean_max = lean_max; }
    return -1;
}

/*
    THE BRANCH AND BALANCE (plant_mechanics_plan.md 3): she stands on it and leans; left alone she
    goes over, walking she goes over sooner, pushing too long tips her the other way - and a player
    reacting as late as a person does can still cross both branches.
*/
static void TestBranch(){
    printf("\nthe branches and balance\n");
    char d[220];
    Stage s;
    Check(s.branches.size() == 2,"the main level has two branches");
    if (s.branches.size() < 2){
        return;
    }
    const StageBranch high = s.branches[0];
    const StageBranch low = s.branches[1];

    Stage idle;
    DropOnto(idle,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    Check(idle.branch_on == 1 && idle.f_on_ground,"a drop onto the low branch stands her on it");
    int fell = -1;
    float side = 0.0f;
    bool f_caught = false;
    for (int i = 0; i < 900 && fell < 0; i++){
        StageEvents e;
        idle.Tick(ArcherInput(),e);
        if (e.f_lost_balance){ fell = i; side = e.fall_side; f_caught = e.f_caught_branch; }
    }
    snprintf(d,sizeof(d),"fell after %d ticks, to side %+.0f",fell,side);
    Check(fell >= 60 && fell < 360,"left alone she goes over - not at once, but within six seconds",d);
    snprintf(d,sizeof(d),"mode %d, branch %d, hands %.3f below the line (want %.3f), caught %d",idle.mode,
             idle.hang_branch,low.a.y - (idle.pos.y + ARCHER_HALF_H),BRANCH_HANG_DROP,f_caught ? 1 : 0);
    Check(f_caught && idle.mode == MODE_HANG && idle.hang_branch == 1 &&
          fabsf(low.a.y - (idle.pos.y + ARCHER_HALF_H) - BRANCH_HANG_DROP) < 0.001f,
          "and catches the branch as she goes, hanging below it",d);

    //Walking at full speed goes over sooner than standing, on average over eight drifts - any one
    //drift can happen to push against the walk's.
    float top_speed = 0.0f;
    float stand_sum = 0.0f, walk_sum = 0.0f;
    int walk_falls = 0;
    for (int k = 0; k < 8; k++){
        for (int w = 0; w < 2; w++){
            Stage t;
            t.balance_entries = 3 * k;
            DropOnto(t,low.a.x + 0.5f,low.a.y + 0.3f);
            ArcherInput in;
            in.move_axis = (float)w;
            for (int i = 0; i < 900; i++){
                StageEvents e;
                t.Tick(in,e);
                if (t.branch_on >= 0 && fabsf(t.vel.x) > top_speed){ top_speed = fabsf(t.vel.x); }
                if (e.f_lost_balance){
                    if (w){ walk_sum += i; walk_falls++; }else{ stand_sum += i; }
                    break;
                }
                if (t.f_on_ground && t.branch_on < 0){ break; }
            }
        }
    }
    snprintf(d,sizeof(d),"on average walking fell at %.0f ticks (%d of 8), standing at %.0f",
             walk_sum / (walk_falls > 0 ? walk_falls : 1),walk_falls,stand_sum / 8.0f);
    Check(walk_falls == 8 && walk_sum / 8.0f < stand_sum / 8.0f,"walking along it, she goes over sooner",d);
    snprintf(d,sizeof(d),"top speed %.2f",top_speed);
    CheckNear(top_speed,BRANCH_WALK_SPEED,0.01f,"and walks no faster than BRANCH_WALK_SPEED on it",d);

    //Up held the whole time pushes her over the far side, fast.
    Stage push;
    DropOnto(push,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    int push_fell = -1;
    for (int i = 0; i < 600 && push_fell < 0; i++){
        ArcherInput in;
        in.aim_axis = 1.0f;
        StageEvents e;
        push.Tick(in,e);
        if (e.f_lost_balance){ push_fell = i; side = e.fall_side; }
    }
    snprintf(d,sizeof(d),"fell at %d to side %+.0f",push_fell,side);
    Check(push_fell >= 0 && push_fell < fell && side > 0.0f,"Up held too long tips her over the far side, sooner still",d);

    //The late player crosses both.
    Stage cross;
    DropOnto(cross,low.a.x - 1.0f,low.a.y + 0.2f);     //on the left stump
    float lean_max = 0.0f;
    int lost = WalkBranch(cross,1.0f,true,1200,&lean_max);
    snprintf(d,sizeof(d),"lost it at %d; ended at x %.2f on %.2f; worst lean %.1f deg",lost,cross.pos.x,
             cross.pos.y - ARCHER_HALF_H,lean_max * 57.2957795f);
    Check(lost < 0 && cross.pos.x + ARCHER_HALF_W > low.b.x && fabsf(cross.pos.y - ARCHER_HALF_H - low.b.y) < 0.01f,
          "a player reacting a sixth of a second late crosses the low branch at full speed, stump to stump",d);
    Stage cross_high;
    DropOnto(cross_high,high.a.x - 1.0f,high.a.y + 0.2f);  //on the canopy
    lost = WalkBranch(cross_high,1.0f,true,1200,&lean_max);
    snprintf(d,sizeof(d),"lost it at %d; ended at x %.2f on %.2f; worst lean %.1f deg",lost,cross_high.pos.x,
             cross_high.pos.y - ARCHER_HALF_H,lean_max * 57.2957795f);
    Check(lost < 0 && cross_high.pos.x + ARCHER_HALF_W > high.b.x && fabsf(cross_high.pos.y - ARCHER_HALF_H - 12.4f) < 0.01f,
          "and the high one, down from the canopy to the perch",d);
    //Three more crossings in a row, each a different drift.
    int crossed = 0;
    for (int k = 0; k < 3; k++){
        Stage again = cross;
        again.pos = v2(low.a.x - 1.0f,low.a.y + ARCHER_HALF_H + 0.01f);
        again.balance_entries = cross.balance_entries + 7 * (k + 1);
        Settle(again);
        if (WalkBranch(again,1.0f,true,1200) < 0 && again.pos.x + ARCHER_HALF_W > low.b.x){ crossed++; }
    }
    snprintf(d,sizeof(d),"%d of 3",crossed);
    Check(crossed == 3,"on three other drifts too",d);

    //Balancing does not tilt the bow, and Down still drops through.
    Stage aim;
    DropOnto(aim,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    float aim_before = aim.aim_deg;
    ArcherInput up;
    up.aim_axis = 1.0f;
    Run(aim,10,up);
    snprintf(d,sizeof(d),"aim %.1f -> %.1f",aim_before,aim.aim_deg);
    Check(aim.aim_deg == aim_before,"balancing with Up and Down leaves the bow's aim where it was",d);
    Stage drop;
    DropOnto(drop,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    ArcherInput down;
    down.f_down_held = true;
    Run(drop,30,down);
    Settle(drop);
    Check(fabsf(drop.pos.y - ARCHER_HALF_H) < 0.01f,"Down drops through it, like any one-way platform");

    //A hard landing is a wobble; stepping on is not.
    Stage hard;
    DropOnto(hard,(low.a.x + low.b.x) * 0.5f,low.a.y + ApexRise());
    snprintf(d,sizeof(d),"landing lean rate %.2f against %.2f stepping on",fabsf(hard.lean_rate),fabsf(idle.lean_rate));
    Stage soft;
    DropOnto(soft,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.05f);
    snprintf(d,sizeof(d),"lean rate after the landing %.3f; after stepping on %.3f",fabsf(hard.lean_rate),fabsf(soft.lean_rate));
    Check(fabsf(hard.lean_rate) > fabsf(soft.lean_rate) + 0.3f,"a hard landing on it knocks her off balance more than stepping on",d);
}

/*
    THE CATCH (plant_mechanics_plan.md 3, step 2): going over hangs her from the branch. From the
    hang Jump climbs her back up onto it - balancing again, from upright - and Down lets go. A branch
    is caught in the air like a ledge, and a drop through one with Down is not caught on the way.
*/
static void TestBranchCatch(){
    printf("\nthe branches: the catch\n");
    char d[220];
    Stage s;
    if (s.branches.size() < 2){
        Check(false,"the main level has two branches");
        return;
    }
    const StageBranch low = s.branches[1];

    //Every branch in every level has room to hang under it, the whole way along: the hanging body
    //box, at every point she could catch it, clear of every block.
    for (int level = 0; level < STAGE_LEVEL_COUNT; level++){
        Stage lv;
        lv.SetLevel(level);
        for (size_t i = 0; i < lv.branches.size(); i++){
            const StageBranch& br = lv.branches[i];
            float worst = 1e9f;
            for (float x = br.a.x + BRANCH_HANG_INSET; x <= br.b.x - BRANCH_HANG_INSET + 1e-4f; x += 0.05f){
                float top = br.SurfaceY(x) - BRANCH_HANG_DROP;       //the hanging box's top
                float bottom = top - ARCHER_HALF_H * 2.0f;
                for (const StageBlock& b : lv.blocks){
                    if (!b.f_alive || b.kind == BLOCK_PLATFORM || x + ARCHER_HALF_W <= b.Left() || x - ARCHER_HALF_W >= b.Right()){
                        continue;
                    }
                    if (b.Bottom() >= top){
                        continue;       //above the hang, not under it
                    }
                    float clear = bottom - b.Top();
                    if (clear < worst){ worst = clear; }
                }
            }
            snprintf(d,sizeof(d),"level %d branch %d: the hanging feet clear the blocks below by %.2f",level,(int)i,worst);
            Check(worst >= 0.0f,"there is room to hang under the branch, the whole way along",d);
        }
    }

    //Over she goes, pushed: Up held until she catches.
    Stage h;
    DropOnto(h,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    ArcherInput up;
    up.aim_axis = 1.0f;
    for (int i = 0; i < 200 && h.mode != MODE_HANG; i++){
        Run(h,1,up);
    }
    Check(h.mode == MODE_HANG && h.hang_branch == 1,"pushed over, she hangs from the branch");
    float hang_x = h.pos.x;
    float hang_y = h.pos.y;
    Run(h,120,ArcherInput());
    snprintf(d,sizeof(d),"moved (%.3f, %.3f)",h.pos.x - hang_x,h.pos.y - hang_y);
    Check(h.mode == MODE_HANG && fabsf(h.pos.x - hang_x) < 1e-4f && fabsf(h.pos.y - hang_y) < 1e-4f,
          "and stays hanging, still, for as long as nothing is pressed",d);

    //Jump: back up, standing on it, balancing again.
    Stage c = h;
    ArcherInput jump;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents e;
    c.Tick(jump,e);
    Check(c.mode == MODE_CLIMB,"Jump starts the climb back up");
    bool f_climbed = false;
    for (int i = 0; i < LEDGE_CLIMB_TICKS + 5 && !f_climbed; i++){
        StageEvents ce;
        c.Tick(ArcherInput(),ce);
        f_climbed = ce.f_climbed;
    }
    snprintf(d,sizeof(d),"mode %d, on branch %d, feet %.3f on a %.3f line, x %.2f from %.2f, lean %.2f",c.mode,
             c.branch_on,c.pos.y - ARCHER_HALF_H,low.SurfaceY(c.pos.x),c.pos.x,hang_x,c.lean);
    Check(f_climbed && c.mode == MODE_GROUND && c.branch_on == 1 &&
          fabsf(c.pos.y - ARCHER_HALF_H - low.SurfaceY(c.pos.x)) < 0.01f && fabsf(c.lean) < 1e-4f,
          "and she stands on it again, upright",d);
    Run(c,2,ArcherInput());
    snprintf(d,sizeof(d),"on branch %d, balance ticks %d, lean rate %.3f",c.branch_on,c.balance_ticks,c.lean_rate);
    Check(c.branch_on == 1 && c.balance_ticks > 0,"balancing again - the lean runs from there",d);

    //Down: lets go, to the ground below, and is not caught again on the way.
    Stage let = h;
    ArcherInput down;
    down.f_down_held = true;
    Run(let,1,down);
    bool f_recaught = false;
    for (int i = 0; i < 120 && !let.f_on_ground; i++){
        StageEvents le;
        let.Tick(ArcherInput(),le);
        f_recaught = f_recaught || le.f_caught_branch;
    }
    snprintf(d,sizeof(d),"ended on %.2f, caught again %d",let.pos.y - ARCHER_HALF_H,f_recaught ? 1 : 0);
    Check(!f_recaught && let.f_on_ground && fabsf(let.pos.y - ARCHER_HALF_H) < 0.01f,"Down lets go, to the ground",d);

    //Caught in the air: a branch at 4.2 over open ground, above her feet's reach and under her hands'.
    Stage air;
    air.branches.push_back({ v2(59.0f,4.2f), v2(62.5f,4.2f) });
    int test_branch = (int)air.branches.size() - 1;
    DropOnto(air,60.75f,0.1f);
    bool f_air_caught = false;
    ArcherInput hop;
    hop.f_jump_pressed = true;
    hop.f_jump_down = true;
    for (int i = 0; i < 90 && !f_air_caught; i++){
        StageEvents ae;
        air.Tick(hop,ae);
        hop.f_jump_pressed = false;
        f_air_caught = ae.f_caught_branch;
    }
    snprintf(d,sizeof(d),"caught %d, branch %d, hands %.3f below the line",f_air_caught ? 1 : 0,air.hang_branch,
             4.2f - (air.pos.y + ARCHER_HALF_H));
    Check(f_air_caught && air.mode == MODE_HANG && air.hang_branch == test_branch,
          "a jump that comes up short of a branch catches it, as it would a ledge",d);
    Stage miss;
    miss.branches.push_back({ v2(59.0f,4.2f), v2(62.5f,4.2f) });
    DropOnto(miss,60.75f,0.1f);
    ArcherInput hop_down = jump;
    hop_down.f_down_held = true;
    bool f_miss_caught = false;
    for (int i = 0; i < 90; i++){
        StageEvents me;
        miss.Tick(hop_down,me);
        hop_down.f_jump_pressed = false;
        f_miss_caught = f_miss_caught || me.f_caught_branch;
    }
    Check(!f_miss_caught,"not with Down held");

    //No jumping off a branch: a press on it, a press buffered just before landing on it, and a
    //press in the grace ticks after walking off its end all do nothing.
    {
        Stage j;
        DropOnto(j,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
        StageEvents je;
        j.Tick(jump,je);
        bool f_jumped = je.f_jumped;
        Run(j,10,ArcherInput());
        snprintf(d,sizeof(d),"jumped %d, on branch %d, feet %.2f",f_jumped ? 1 : 0,j.branch_on,j.pos.y - ARCHER_HALF_H);
        Check(!f_jumped && j.branch_on == 1,"Jump does nothing while she balances on a branch",d);

        Stage b;
        b.pos = v2((low.a.x + low.b.x) * 0.5f,low.a.y + 1.5f + ARCHER_HALF_H);
        b.mode = MODE_AIR;
        bool f_buffered_jump = false;
        bool f_pressed = false;
        for (int i = 0; i < 60 && !(b.f_on_ground && i > 20); i++){
            ArcherInput in;
            //Pressed a few ticks before touching down, inside the jump buffer.
            if (!f_pressed && b.pos.y - ARCHER_HALF_H - low.a.y < 0.5f){
                in.f_jump_pressed = true;
                in.f_jump_down = true;
                f_pressed = true;
            }
            StageEvents be;
            b.Tick(in,be);
            f_buffered_jump = f_buffered_jump || be.f_jumped;
        }
        snprintf(d,sizeof(d),"pressed %d, jumped %d, on branch %d",f_pressed ? 1 : 0,f_buffered_jump ? 1 : 0,b.branch_on);
        Check(f_pressed && !f_buffered_jump && b.branch_on == 1,"nor does one pressed just before landing on it",d);

        //Off the end of a branch that ends in the air - the test one at 4.2 - then Jump at once.
        Stage end;
        end.branches.push_back({ v2(59.0f,4.2f), v2(62.5f,4.2f) });
        DropOnto(end,61.8f,4.5f);
        ArcherInput right;
        right.move_axis = 1.0f;
        bool f_end_jump = false;
        bool f_off = false;
        for (int i = 0; i < 60 && !f_off; i++){
            StageEvents ee;
            end.Tick(right,ee);
            f_off = !end.f_on_ground;
        }
        ArcherInput jr = jump;
        jr.move_axis = 1.0f;
        StageEvents ee;
        end.Tick(jr,ee);
        f_end_jump = ee.f_jumped;
        snprintf(d,sizeof(d),"walked off %d, jumped %d",f_off ? 1 : 0,f_end_jump ? 1 : 0);
        Check(f_off && !f_end_jump,"nor one in the grace ticks after walking off its end",d);

        //And off it, onto the stump, she jumps as ever.
        Stage stump;
        DropOnto(stump,low.b.x + 1.0f,low.b.y + 0.2f);
        StageEvents se;
        stump.Tick(jump,se);
        Check(se.f_jumped,"while from the stump at its end she jumps as ever");
    }

    //Standing on it, Down drops through - and is not caught on the way past.
    Stage drop;
    DropOnto(drop,(low.a.x + low.b.x) * 0.5f,low.a.y + 0.3f);
    Run(drop,1,down);
    bool f_drop_caught = false;
    for (int i = 0; i < 120 && !(drop.f_on_ground && drop.branch_on < 0); i++){
        StageEvents de;
        drop.Tick(ArcherInput(),de);
        f_drop_caught = f_drop_caught || de.f_caught_branch;
    }
    snprintf(d,sizeof(d),"caught %d, ended on %.2f",f_drop_caught ? 1 : 0,drop.pos.y - ARCHER_HALF_H);
    Check(!f_drop_caught && fabsf(drop.pos.y - ARCHER_HALF_H) < 0.01f,"a tap of Down drops through it without catching it again",d);
}

/*
    The rope level's two pits - a soft landing and a hard one, by construction. See BuildRopeLevel
    for why those depths.
*/
static void TestRopePits(){
    printf("\nthe rope level's pits\n");
    ArcherInput left;
    left.move_axis = -1.0f;
    ArcherInput right;
    right.move_axis = 1.0f;
    char d[160];

    //Walked off the left end: 3 down, a soft landing.
    Stage s;
    s.SetLevel(STAGE_LEVEL_ROPE);
    Settle(s);
    s.pos = v2(-15.5f,ARCHER_HALF_H + 0.001f);
    Settle(s);
    float speed = -1.0f;
    for (int i = 0; i < 240 && speed < 0.0f; i++){
        StageEvents e;
        s.Tick(left,e);
        if (e.f_landed && s.pos.x < -17.0f){ speed = e.land_speed; }
    }
    snprintf(d,sizeof(d),"landed at (%.2f,%.2f)",s.pos.x,s.pos.y - ARCHER_HALF_H);
    CheckNear(s.pos.y - ARCHER_HALF_H,-3.0f,0.01f,"walking off the left end lands on the shallow pit's floor",d);
    CheckNear(speed,sqrtf(2.0f * ARCHER_GRAVITY * ARCHER_FALL_GRAVITY_MUL * 3.0f),0.8f,
              "at the speed three units of fall give");
    Check(speed < PUPPET_HARD_LAND_VEL,"which is a soft landing");

    //And out again: a jump at the wall catches the floor's lip, and the climb stands her on it.
    ArcherInput up = right;
    up.f_jump_down = true;
    up.f_jump_pressed = true;
    StageEvents je;
    s.Tick(up,je);
    up.f_jump_pressed = false;
    bool f_hung = false;
    for (int i = 0; i < 120 && !f_hung; i++){
        StageEvents e;
        s.Tick(up,e);
        f_hung = (s.mode == MODE_HANG);
    }
    Check(f_hung,"a jump from the shallow pit catches the floor's lip");
    ArcherInput climb;
    climb.f_jump_pressed = true;
    StageEvents ce;
    s.Tick(climb,ce);
    Run(s,LEDGE_CLIMB_TICKS + 10,ArcherInput());
    Check(s.f_on_ground && fabsf(s.pos.y - ARCHER_HALF_H) < 0.01f && s.pos.x > -17.0f,
          "and the climb stands her back on the floor");

    //Walked off the right end: 15 down, through the hard landing to top speed.
    Stage r;
    r.SetLevel(STAGE_LEVEL_ROPE);
    Settle(r);
    r.pos = v2(15.5f,ARCHER_HALF_H + 0.001f);
    Settle(r);
    speed = -1.0f;
    for (int i = 0; i < 240 && speed < 0.0f; i++){
        StageEvents e;
        r.Tick(right,e);
        if (e.f_landed && r.pos.x > 17.0f){ speed = e.land_speed; }
    }
    CheckNear(r.pos.y - ARCHER_HALF_H,-15.0f,0.01f,"walking off the right end lands on the deep pit's floor");
    Check(speed >= PUPPET_HARD_LAND_VEL,"hard");
    CheckNear(speed,ARCHER_MAX_FALL_SPEED,0.01f,"at top fall speed");

    //A sprint off the edge still comes down inside it.
    Stage q;
    q.SetLevel(STAGE_LEVEL_ROPE);
    Settle(q);
    q.pos = v2(4.0f,ARCHER_HALF_H + 0.001f);
    Settle(q);
    bool f_landed = false;
    for (int i = 0; i < 400 && !f_landed; i++){
        StageEvents e;
        q.Tick(right,e);
        f_landed = e.f_landed && q.pos.x > 17.0f;
    }
    snprintf(d,sizeof(d),"landed at x %.2f, the far wall is at 31",q.pos.x);
    Check(f_landed && q.pos.x + ARCHER_HALF_W < 31.0f,"a sprint off the right edge lands on the deep pit's floor, short of its wall",d);
}

/*
    Stage::PredictLanding. The forecast IS the rules, so with the input unchanged it must be exact
    - the same tick, the same speed, the same place, to the bit - and not merely close. When the
    input changes, the old forecast is wrong and the next one is right; that is the whole contract.
*/
static void TestLandingForecast(){
    printf("\nthe landing forecast\n");
    char d[200];

    //Standing still, there is nothing to forecast.
    Stage g;
    Settle(g);
    Check(!g.PredictLanding(ArcherInput()).f_lands,"on the ground there is no landing to forecast");

    //A drop: forecast on the first tick in the air, then fall for real.
    Stage s;
    Settle(s);
    s.pos = v2(s.pos.x,s.pos.y + 4.0f);
    ArcherInput idle;
    Run(s,1,idle);
    StageLanding f = s.PredictLanding(idle);
    int ticks = 0;
    StageEvents land;
    for (int i = 0; i < 120; i++){
        StageEvents e;
        s.Tick(idle,e);
        ticks++;
        if (e.f_landed){ land = e; break; }
    }
    snprintf(d,sizeof(d),"forecast %i ticks at %.4f, landed after %i at %.4f",f.ticks,f.speed,ticks,land.land_speed);
    Check(f.f_lands && f.ticks == ticks,"a drop lands on exactly the tick forecast",d);
    Check(f.speed == land.land_speed,"at exactly the speed forecast",d);
    Check(f.pos.x == s.pos.x && f.pos.y == s.pos.y,"in exactly the place forecast");

    //A long fall into the rope level's deep pit, running: past the horizon nothing is reported,
    //and once the landing is in sight it stays on the same tick all the way down.
    Stage r;
    r.SetLevel(STAGE_LEVEL_ROPE);
    Settle(r);
    r.pos = v2(14.0f,ARCHER_HALF_H + 0.001f);
    Settle(r);
    ArcherInput run;
    run.move_axis = 1.0f;
    int now = 0;
    int landing_at = -1;
    bool f_consistent = true;
    bool f_beyond_quiet = true;
    bool f_saw_horizon = false;
    for (int i = 0; i < 300; i++){
        StageEvents e;
        r.Tick(run,e);
        now++;
        if (e.f_landed && r.pos.x > 17.0f){
            f_consistent = f_consistent && (landing_at == now);
            break;
        }
        StageLanding k = r.PredictLanding(run);
        if (r.mode != MODE_AIR){ continue; }
        if (!k.f_lands){
            f_beyond_quiet = f_beyond_quiet && (landing_at < 0);
            continue;
        }
        if (k.ticks == STAGE_PREDICT_TICKS){ f_saw_horizon = true; }
        if (landing_at < 0){ landing_at = now + k.ticks; }
        f_consistent = f_consistent && (now + k.ticks == landing_at);
    }
    Check(f_saw_horizon,"a long fall's landing first appears at the edge of the horizon");
    Check(f_beyond_quiet,"and nothing is reported before it comes into range");
    Check(landing_at > 0 && f_consistent,"and from then on every forecast names the same tick, which is the tick she lands");

    //Changing the input makes the old forecast wrong and the next one right. A jump off the main
    //floor's left end holding left lands in the shallow pit; turning back mid-air lands on the
    //floor instead, and sooner.
    Stage j;
    j.SetLevel(STAGE_LEVEL_ROPE);
    Settle(j);
    j.pos = v2(-16.0f,ARCHER_HALF_H + 0.001f);
    Settle(j);
    ArcherInput go_left;
    go_left.move_axis = -1.0f;
    go_left.f_jump_down = true;
    go_left.f_jump_pressed = true;
    StageEvents je;
    j.Tick(go_left,je);
    go_left.f_jump_pressed = false;
    Run(j,4,go_left);
    StageLanding before = j.PredictLanding(go_left,120);
    ArcherInput go_right;
    go_right.move_axis = 1.0f;
    go_right.f_jump_down = true;
    StageLanding after = j.PredictLanding(go_right,120);
    int flown = 0;
    for (int i = 0; i < 200; i++){
        StageEvents e;
        j.Tick(go_right,e);
        flown++;
        if (e.f_landed){ break; }
    }
    snprintf(d,sizeof(d),"held left: %i ticks at y %.2f; turned back: %i at y %.2f; flew %i, landed at y %.2f",
             before.ticks,before.pos.y - ARCHER_HALF_H,after.ticks,after.pos.y - ARCHER_HALF_H,flown,
             j.pos.y - ARCHER_HALF_H);
    Check(before.f_lands && before.pos.y - ARCHER_HALF_H < -2.9f,"held left, the forecast lands her in the pit",d);
    Check(after.f_lands && after.ticks == flown && fabsf(j.pos.y - ARCHER_HALF_H) < 0.01f,
          "turned back, the forecast from that tick is the landing on the floor",d);
    Check(after.ticks < before.ticks,"and it is sooner, so a clip started off the first would have been late");

    //A ledge catch ends a forecast as a catch, on the tick it happens.
    Stage c;
    Settle(c);
    c.pos = v2(43.1f,ARCHER_HALF_H + 0.001f);
    Settle(c);
    ArcherInput reach;
    reach.move_axis = 1.0f;
    reach.f_jump_down = true;
    reach.f_jump_pressed = true;
    StageEvents re;
    c.Tick(reach,re);
    reach.f_jump_pressed = false;
    StageLanding catchf = c.PredictLanding(reach,120);
    int caught = -1;
    for (int i = 1; i <= 200; i++){
        StageEvents e;
        c.Tick(reach,e);
        if (c.mode == MODE_HANG){ caught = i; break; }
    }
    snprintf(d,sizeof(d),"forecast a catch in %i, caught after %i",catchf.ticks,caught);
    Check(catchf.f_caught && !catchf.f_lands && catchf.ticks == caught,"a jump at the high ledge forecasts the catch, to the tick",d);
}

/*
    Stage::PredictArrowImpact: an arrow's flight takes no input, so its forecast is exact - the
    tick and the point it sticks, to the bit - and its path is the segments the real one sweeps.
*/
static void TestArrowForecast(){
    printf("\nthe arrow forecast\n");
    char d[200];
    Stage s;
    s.SetLevel(STAGE_LEVEL_RANGE);
    Settle(s);
    ArcherInput hold;
    hold.f_draw_down = true;
    Run(s,BOW_DRAW_TICKS + 2,hold);
    ArcherInput letgo;
    letgo.f_draw_released = true;
    StageEvents le;
    s.Tick(letgo,le);
    int index = -1;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (s.arrows[i].f_live && !s.arrows[i].f_stuck){ index = i; }
    }
    Check(le.f_shot && index >= 0,"a full draw looses an arrow along the range");
    if (index < 0){
        return;
    }
    std::vector<v3> path;
    StageArrowImpact f = s.PredictArrowImpact(index,600,&path);
    ArcherInput idle;
    int flown = 0;
    StageEvents::ArrowHit hit;
    bool f_hit = false;
    std::vector<v3> swept;
    for (int i = 0; i < 600 && !f_hit; i++){
        StageEvents e;
        s.Tick(idle,e);
        flown++;
        if (swept.empty()){ swept.push_back(s.arrows[index].prev_pos); }
        for (size_t h = 0; h < e.arrow_hits.size(); h++){
            if (e.arrow_hits[h].arrow == index){ hit = e.arrow_hits[h]; f_hit = true; }
        }
        swept.push_back(f_hit ? hit.point : s.arrows[index].pos);
    }
    snprintf(d,sizeof(d),"forecast %i ticks to (%.3f,%.3f), struck after %i at (%.3f,%.3f)",
             f.ticks,f.point.x,f.point.y,flown,hit.point.x,hit.point.y);
    Check(f.f_hits && f_hit && f.ticks == flown,"it strikes on exactly the tick forecast",d);
    Check(f.point.x == hit.point.x && f.point.y == hit.point.y && f.point.z == hit.point.z &&
          f.block == hit.block,"at exactly the point, in the block forecast",d);
    bool f_same = (path.size() == swept.size());
    for (size_t i = 0; f_same && i < path.size(); i++){
        f_same = (path[i].x == swept[i].x && path[i].y == swept[i].y && path[i].z == swept[i].z);
    }
    snprintf(d,sizeof(d),"forecast %zu points, flown %zu",path.size(),swept.size());
    Check(f_same,"and the path handed back is the path it swept, so a prop raycast along it finds what the flight will",d);
    Check(!s.PredictArrowImpact(index,600).f_hits,"a stuck arrow has nothing left to forecast");
}

/*
    The skinned rope - apps/archer/RopeMesh.h. What the skinning shader will do with it is done
    here by hand, sum of weight * (R (p - bind centre) + centre) over a pose of the chain, so the
    three things that matter are checked without a GPU: the weights are a partition of unity, a
    rope swung whole moves as one rigid thing, and a bent one stays in one piece.
*/
struct RopeTestPose{
    std::vector<quat> rot;
    std::vector<vec3> centre;
};

//The chain hanging from the anchor with link i at angle a[i] off straight down, joined end to end
//- the shape rp3d's ball-and-socket joints hold it in.
static RopeTestPose PoseRope(const RopeMeshInput& in, const std::vector<float>& a){
    RopeTestPose pose;
    float seg = in.length / (float)in.links;
    vec3 joint = in.anchor;
    for (int i = 0; i < in.links; i++){
        quat q(vec3(0.0f,0.0f,1.0f),a[i]);
        vec3 dir = q * vec3(0.0f,-1.0f,0.0f);
        pose.rot.push_back(q);
        pose.centre.push_back(joint + dir * (seg * 0.5f));
        joint = joint + dir * seg;
    }
    return pose;
}

static vec3 SkinVertex(const skinned_vertex& v, const RopeMeshInput& in, const RopeTestPose& pose){
    int b[3] = { v.bones.x, v.bones.y, v.bones.z };
    float w[3] = { v.weights.x, v.weights.y, v.weights.z };
    vec3 out;
    for (int k = 0; k < 3; k++){
        if (w[k] == 0.0f){
            continue;
        }
        vec3 local = v.pos - RopeLinkBindCentre(in,b[k]);
        out = out + (pose.rot[b[k]] * local + pose.centre[b[k]]) * w[k];
    }
    return out;
}

static void TestRopeMesh(){
    printf("\nrope mesh\n");
    char d[200];

    //--- Weights ---
    const int links = 8;
    const float seg = 1.125f;
    float worst_sum = 0.0f;
    int bad = 0;
    for (int i = 0; i <= 400; i++){
        float s = -1.0f + (links * seg + 2.0f) * (float)i / 400.0f;
        int3 b;
        vec3 w;
        RopeWeights(s,seg,links,b,w);
        float sum = w.x + w.y + w.z;
        if (fabsf(sum - 1.0f) > worst_sum){ worst_sum = fabsf(sum - 1.0f); }
        if (w.x < 0.0f || w.y < 0.0f || w.z < 0.0f || b.x < 0 || b.y < 0 || b.z < 0 ||
            b.x >= links || b.y >= links || b.z >= links){
            bad++;
        }
    }
    snprintf(d,sizeof(d),"worst |sum - 1| %.2e, %i out of range",worst_sum,bad);
    Check(worst_sum < 1e-5f && bad == 0,"weights: non-negative, in the chain, summing to 1",d);
    int3 b;
    vec3 w;
    RopeWeights(0.0f,seg,links,b,w);
    Check(b.x == 0 && fabsf(w.x - 1.0f) < 1e-6f,"the top of the rope is wholly the first link");
    RopeWeights(links * seg,seg,links,b,w);
    Check(b.x == links - 1 && fabsf(w.x - 1.0f) < 1e-6f,"its end is wholly the last link");
    RopeWeights(3.5f * seg,seg,links,b,w);
    snprintf(d,sizeof(d),"links %i/%i/%i at %.3f/%.3f/%.3f",b.x,b.y,b.z,w.x,w.y,w.z);
    Check(b.y == 3 && fabsf(w.y - 0.75f) < 1e-5f && fabsf(w.x - 0.125f) < 1e-5f,
          "at a link's centre: 0.75 to it, 0.125 to each neighbour",d);

    //--- The mesh ---
    std::vector<vertex> tile, piece;
    MakeVinePlaceholderTile(tile);
    MakeVinePlaceholderLeaf(piece);       //a stand-in for the ring and the tassel: any rigid shape
    std::vector<vertex> ring = piece;
    for (size_t i = 0; i < ring.size(); i++){
        ring[i].pos.z = -ring[i].pos.z;   //the ring's convention: above the join, along -Z
    }
    RopeMeshInput in;
    in.anchor = vec3(0.0f,11.0f,0.0f);
    in.length = links * seg;
    in.links = links;
    in.tile = &tile;
    in.ring = &ring;
    in.tassel = &piece;
    in.collar = &piece;
    in.collar_at = { 0.4f, in.length - 0.4f };
    std::vector<skinned_vertex> mesh;
    Check(BuildRopeMesh(in,mesh) && !mesh.empty(),"the rope builds");
    size_t tile_verts = 0;
    {
        Spline line;
        line.points = { in.anchor, in.anchor + vec3(0.0f,-in.length,0.0f) };
        line.Build(0.05f);
        std::vector<vertex> only;
        SplineDeformParams dp;
        DeformAlongSpline(line,tile,dp,only);
        tile_verts = only.size();
    }
    snprintf(d,sizeof(d),"%zu verts, %zu of them the middle",mesh.size(),tile_verts);
    Check(mesh.size() == tile_verts + piece.size() * 4,"the middle, a ring, two collars and a tassel",d);
    float ring_low = 1e9f, tassel_high = -1e9f;
    size_t ring_from = tile_verts, tassel_from = tile_verts + piece.size() * 3;
    for (size_t i = ring_from; i < ring_from + piece.size(); i++){
        if (mesh[i].pos.y < ring_low){ ring_low = mesh[i].pos.y; }
    }
    for (size_t i = tassel_from; i < mesh.size(); i++){
        if (mesh[i].pos.y > tassel_high){ tassel_high = mesh[i].pos.y; }
    }
    snprintf(d,sizeof(d),"ring down to %.3f, anchor %.3f; tassel up to %.3f, end %.3f",
             ring_low,in.anchor.y,tassel_high,in.anchor.y - in.length);
    Check(ring_low >= in.anchor.y - 1e-4f && tassel_high <= in.anchor.y - in.length + 1e-4f,
          "the ring sits above the anchor and the tassel below the end",d);

    //--- Swung whole: rigid ---
    std::vector<float> same(links,0.5f);
    RopeTestPose swung = PoseRope(in,same);
    quat q(vec3(0.0f,0.0f,1.0f),0.5f);
    float worst_rigid = 0.0f;
    for (size_t i = 0; i < mesh.size(); i++){
        vec3 want = q * (mesh[i].pos - in.anchor) + in.anchor;
        float e = SkinVertex(mesh[i],in,swung).distance(want);
        if (e > worst_rigid){ worst_rigid = e; }
    }
    snprintf(d,sizeof(d),"worst %.2e",worst_rigid);
    Check(worst_rigid < 1e-4f,"swung whole, the rope moves as one rigid thing",d);

    //--- Bent: still one piece ---
    std::vector<float> bent;
    for (int i = 0; i < links; i++){
        bent.push_back(0.45f * sinf(1.3f * (float)i) + 0.1f * (float)i);
    }
    RopeTestPose pose = PoseRope(in,bent);
    float worst_top = 0.0f;
    float worst_ratio_hi = 1.0f, worst_ratio_lo = 1.0f;
    for (size_t i = 0; i + 2 < tile_verts; i += 3){
        for (int k = 0; k < 3; k++){
            const skinned_vertex& a = mesh[i + k];
            const skinned_vertex& c = mesh[i + (k + 1) % 3];
            float bind = a.pos.distance(c.pos);
            if (bind < 1e-4f){
                continue;
            }
            float now = SkinVertex(a,in,pose).distance(SkinVertex(c,in,pose));
            float r = now / bind;
            if (r > worst_ratio_hi){ worst_ratio_hi = r; }
            if (r < worst_ratio_lo){ worst_ratio_lo = r; }
        }
        //The very top ring of the middle stays on the anchor, whatever the rope does below.
        if (fabsf(mesh[i].pos.y - in.anchor.y) < 1e-4f){
            vec3 axis_now = SkinVertex(mesh[i],in,pose);
            vec3 want = pose.rot[0] * (mesh[i].pos - in.anchor) + in.anchor;
            float e = axis_now.distance(want);
            if (e > worst_top){ worst_top = e; }
        }
    }
    snprintf(d,sizeof(d),"edges stretch %.3f..%.3f of their bind length",worst_ratio_lo,worst_ratio_hi);
    Check(worst_ratio_lo > 0.6f && worst_ratio_hi < 1.6f,"bent, the middle stays in one piece",d);
    snprintf(d,sizeof(d),"worst %.2e",worst_top);
    Check(worst_top < 1e-4f,"its top turns about the anchor with the first link",d);
    //The tassel rides the last link rigidly.
    float worst_tassel = 0.0f;
    for (size_t i = tassel_from; i < mesh.size(); i++){
        vec3 want = pose.rot[links - 1] * (mesh[i].pos - RopeLinkBindCentre(in,links - 1)) + pose.centre[links - 1];
        float e = SkinVertex(mesh[i],in,pose).distance(want);
        if (e > worst_tassel){ worst_tassel = e; }
    }
    snprintf(d,sizeof(d),"worst %.2e",worst_tassel);
    Check(worst_tassel < 1e-4f,"the tassel swings rigidly with the last link",d);

    //--- Cut: two pieces that share no bone ---
    const int cut = 3;
    int ranged_bad = 0;
    for (int i = 0; i <= 200; i++){
        float s = (links * seg) * (float)i / 200.0f;
        RopeWeights(s,seg,cut,links - 1,b,w);
        int bb[3] = { b.x, b.y, b.z };
        float ww[3] = { w.x, w.y, w.z };
        for (int k = 0; k < 3; k++){
            if (ww[k] > 0.0f && (bb[k] < cut || bb[k] > links - 1)){
                ranged_bad++;
            }
        }
        if (fabsf(w.x + w.y + w.z - 1.0f) > 1e-5f){
            ranged_bad++;
        }
    }
    snprintf(d,sizeof(d),"%i bad",ranged_bad);
    Check(ranged_bad == 0,"a section's weights stay inside its own links and still sum to 1",d);

    RopeMeshInput cut_in = in;
    cut_in.cuts = { cut };
    cut_in.cut_end = &piece;
    std::vector<skinned_vertex> cut_mesh;
    Check(BuildRopeMesh(cut_in,cut_mesh),"a cut rope builds");
    snprintf(d,sizeof(d),"%zu verts, uncut %zu",cut_mesh.size(),mesh.size());
    Check(cut_mesh.size() == mesh.size() + piece.size() * 2,"the same rope plus a cut end on each side",d);
    int mixed = 0;
    for (size_t i = 0; i + 2 < cut_mesh.size(); i += 3){
        int above = 0, below = 0;
        for (size_t c = i; c < i + 3; c++){
            const skinned_vertex& v = cut_mesh[c];
            int bb[3] = { v.bones.x, v.bones.y, v.bones.z };
            float ww[3] = { v.weights.x, v.weights.y, v.weights.z };
            for (int k = 0; k < 3; k++){
                if (ww[k] > 0.0f){
                    if (bb[k] < cut){ above++; }else{ below++; }
                }
            }
        }
        if (above > 0 && below > 0){
            mixed++;
        }
    }
    snprintf(d,sizeof(d),"%i triangles on both sides",mixed);
    Check(mixed == 0,"every triangle belongs wholly to one piece",d);

    //Pulled apart: the lower piece carried off sideways, as if it had fallen away.
    RopeTestPose apart = PoseRope(in,bent);
    for (int i = cut; i < links; i++){
        apart.centre[i] = apart.centre[i] + vec3(4.0f,-2.0f,0.0f);
    }
    float cut_hi = 1.0f, uncut_hi = 1.0f;
    for (size_t i = 0; i + 2 < tile_verts; i += 3){
        for (int k = 0; k < 3; k++){
            float bind = cut_mesh[i + k].pos.distance(cut_mesh[i + (k + 1) % 3].pos);
            if (bind < 1e-4f){
                continue;
            }
            float r = SkinVertex(cut_mesh[i + k],in,apart).distance(SkinVertex(cut_mesh[i + (k + 1) % 3],in,apart)) / bind;
            if (r > cut_hi){ cut_hi = r; }
            float ru = SkinVertex(mesh[i + k],in,apart).distance(SkinVertex(mesh[i + (k + 1) % 3],in,apart)) / bind;
            if (ru > uncut_hi){ uncut_hi = ru; }
        }
    }
    snprintf(d,sizeof(d),"worst edge %.2f of its bind length cut, %.2f uncut",cut_hi,uncut_hi);
    Check(cut_hi < 1.6f && uncut_hi > 3.0f,"pulled apart, nothing stretches across the gap - uncut, it would",d);

    //The two cut ends, in the order they are added: the upper piece's, then the lower's.
    size_t end_up = tile_verts + piece.size() * 4, end_down = end_up + piece.size();
    float cut_y = in.anchor.y - (float)cut * seg;
    float worst_up = 0.0f, worst_down = 0.0f, lowest_flipped = 1e9f;
    for (size_t i = end_up; i < end_down; i++){
        vec3 want = apart.rot[cut - 1] * (cut_mesh[i].pos - RopeLinkBindCentre(in,cut - 1)) + apart.centre[cut - 1];
        float e = SkinVertex(cut_mesh[i],in,apart).distance(want);
        if (e > worst_up){ worst_up = e; }
    }
    for (size_t i = end_down; i < cut_mesh.size(); i++){
        vec3 want = apart.rot[cut] * (cut_mesh[i].pos - RopeLinkBindCentre(in,cut)) + apart.centre[cut];
        float e = SkinVertex(cut_mesh[i],in,apart).distance(want);
        if (e > worst_down){ worst_down = e; }
        if (cut_mesh[i].pos.y < lowest_flipped){ lowest_flipped = cut_mesh[i].pos.y; }
    }
    snprintf(d,sizeof(d),"worst %.2e above, %.2e below; the lower one's bottom %.3f, the cut %.3f",
             worst_up,worst_down,lowest_flipped,cut_y);
    Check(worst_up < 1e-4f && worst_down < 1e-4f && lowest_flipped >= cut_y - 1e-4f,
          "each cut end rides the link it caps, the lower one turned up over its piece",d);

    RopeMeshInput top_in = in;
    top_in.cuts = { 0 };
    top_in.cut_end = &piece;
    std::vector<skinned_vertex> top_mesh;
    BuildRopeMesh(top_in,top_mesh);
    Check(top_mesh.size() == mesh.size(),"a cut at the anchor splits nothing - the rope is one piece");
}

/*
    Climbing the rope - the grip distance in Stage, and the pose chosen by distance in the Puppet.

    The rope here is the shape the app offers: a vertical line of points every 0.25 down from an
    anchor, each carrying its distance down, from the first grabbable one to the bottom.
*/
static void OfferRope(Stage& s, float x, float anchor_y, float s_from, float s_to){
    s.ClearRopePoints();
    int id = 0;
    for (float d = s_from; d <= s_to + 1e-4f; d += 0.25f){
        s.AddRopePoint(x,anchor_y - d,id++,d);
    }
}

static void TickOnRope(Stage& s, int n, const ArcherInput& in, float x, float anchor_y,
                       float s_from, float s_to){
    for (int i = 0; i < n; i++){
        OfferRope(s,x,anchor_y,s_from,s_to);
        StageEvents ev;
        s.Tick(in,ev);
    }
}

static void TestRopeClimb(){
    printf("\nclimbing the rope\n");
    char d[200];
    ArcherInput idle, action, up, down;
    action.f_action_pressed = true;
    up.aim_axis = 1.0f;
    down.aim_axis = -1.0f;

    Stage s;
    Settle(s);
    //Anchored so the point 6.0 down the rope is exactly at her hands.
    float x = s.pos.x;
    float anchor_y = s.pos.y + ARCHER_HALF_H * 0.6f + 6.0f;
    const float top = 1.5f, bottom = 8.75f;
    TickOnRope(s,1,action,x,anchor_y,top,bottom);
    snprintf(d,sizeof(d),"grip at %.3f",s.rope_s);
    Check(s.mode == MODE_ROPE && fabsf(s.rope_s - 6.0f) < 1e-4f,
          "the catch takes the grip distance of the point it caught",d);
    Check(s.rope_climbed == 0.0f && s.rope_climb == 0,"and starts with nothing climbed");

    float aim_before = s.aim_deg;
    TickOnRope(s,60,up,x,anchor_y,top,bottom);
    snprintf(d,sizeof(d),"grip %.3f, climbed %.3f",s.rope_s,s.rope_climbed);
    Check(fabsf(s.rope_s - (6.0f - ROPE_CLIMB_SPEED)) < 1e-3f && fabsf(s.rope_climbed - ROPE_CLIMB_SPEED) < 1e-3f,
          "holding up for a second climbs ROPE_CLIMB_SPEED up the rope",d);
    Check(s.rope_climb == 1,"and says so, this tick");
    Check(s.aim_deg == aim_before,"the up key climbs rather than tilting the aim");
    Check(s.pos.x == x,"and the rules still do not move her - the joint does");

    TickOnRope(s,1,idle,x,anchor_y,top,bottom);
    Check(s.rope_climb == 0,"let go of the key, and she holds where she is");

    TickOnRope(s,60 * 20,up,x,anchor_y,top,bottom);
    snprintf(d,sizeof(d),"grip %.3f",s.rope_s);
    Check(fabsf(s.rope_s - top) < 1e-4f,"no further up than the highest point she could have caught",d);
    Check(s.rope_climb == 0,"and at the top a held key is not a climb");

    TickOnRope(s,60 * 30,down,x,anchor_y,top,bottom);
    snprintf(d,sizeof(d),"grip %.3f, climbed %.3f",s.rope_s,s.rope_climbed);
    Check(fabsf(s.rope_s - bottom) < 1e-4f,"nor off the bottom going down",d);
    Check(fabsf(s.rope_climbed - (6.0f - bottom)) < 1e-3f,"climbed is signed: all of it undone and more",d);

    //Caught again: a fresh grip and a fresh count.
    TickOnRope(s,ROPE_MIN_HOLD_TICKS,idle,x,anchor_y,top,bottom);
    TickOnRope(s,1,action,x,anchor_y,top,bottom);
    Check(s.mode != MODE_ROPE,"let go");

    //--- Cut: the app stops offering points below the cut ---
    ArcherInput jump;
    jump.f_jump_pressed = true;
    Stage below;
    Settle(below);
    TickOnRope(below,1,action,x,anchor_y,top,bottom);
    TickOnRope(below,1,idle,x,anchor_y,top,7.0f);
    Check(below.mode == MODE_ROPE,"cut below her hands, she keeps hold of what still hangs");

    Stage cut;
    Settle(cut);
    TickOnRope(cut,1,action,x,anchor_y,top,bottom);
    OfferRope(cut,x,anchor_y,top,4.25f);
    StageEvents cev;
    cut.Tick(jump,cev);
    Check(cut.mode == MODE_AIR && cev.f_released_rope && cev.f_rope_lost,
          "cut above them, she lets go at once - inside the minimum hold, pressing nothing to");
    Check(!cev.f_rope_jump,"and falling with the piece is not a jump, whatever is pressed");

    Stage bare;
    Settle(bare);
    TickOnRope(bare,1,action,x,anchor_y,top,bottom);
    bare.ClearRopePoints();
    StageEvents nev;
    bare.Tick(idle,nev);
    Check(bare.mode != MODE_ROPE && nev.f_rope_lost,"a rope offering nothing at all is let go of too");

    Stage kept;
    Settle(kept);
    TickOnRope(kept,1,action,x,anchor_y,top,bottom);
    StageEvents kev;
    OfferRope(kept,x,anchor_y,top,bottom);
    kept.Tick(idle,kev);
    Check(kept.mode == MODE_ROPE && !kev.f_rope_lost,"an uncut rope is not");

    //--- The pose, by distance ---
    Puppet p;
    const float times[] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f };
    const float rise[]  = { 0.0f, 0.2f, 0.2f, 0.5f, 0.6f };   //a pause between 0.5 and 1.0
    p.climb_times.assign(times,times + 5);
    p.climb_rise.assign(rise,rise + 5);
    p.climb_cycle_rise = 0.6f;
    p.clip_duration[CLIP_ROPE_CLIMB] = 2.0f;
    float lift = 0.0f;
    float t1 = p.ClimbTimeAt(0.1f,lift);
    snprintf(d,sizeof(d),"t %.3f lift %.3f",t1,lift);
    Check(fabsf(t1 - 0.25f) < 1e-4f && fabsf(lift - 0.1f) < 1e-5f,"distance maps to the time the hips had risen that far",d);
    float t2 = p.ClimbTimeAt(0.2f,lift);
    snprintf(d,sizeof(d),"t %.3f",t2);
    Check(fabsf(t2 - 0.5f) < 1e-4f,"a pause maps to its first frame - the target passes it over; the playhead follows, below",d);
    float t3 = p.ClimbTimeAt(0.7f,lift);
    snprintf(d,sizeof(d),"t %.3f lift %.3f",t3,lift);
    Check(fabsf(t3 - 0.25f) < 1e-4f && fabsf(lift - 0.1f) < 1e-5f,"a second cycle wraps round",d);
    float t4 = p.ClimbTimeAt(-0.1f,lift);
    snprintf(d,sizeof(d),"t %.3f lift %.3f",t4,lift);
    Check(fabsf(t4 - 1.5f) < 1e-4f && fabsf(lift - 0.5f) < 1e-5f,"and so does climbing down past the catch",d);

    ArcherAnimParams in;
    in.mode = MODE_ROPE;
    p.Tick(in);
    Check(p.choice.clip == CLIP_ROPE,"caught and still: the hang");
    in.rope_climb = 1;
    in.rope_climbed = 0.1f;
    p.Tick(in);
    snprintf(d,sizeof(d),"clip %i, pinned %.3f, rate %.2f, lift %.3f",p.choice.clip,p.choice.pinned_time,
             p.choice.rate,p.choice.lift);
    Check(p.choice.clip == CLIP_ROPE_CLIMB && fabsf(p.choice.pinned_time - 0.25f) < 1e-4f &&
          p.choice.rate == 0.0f && fabsf(p.choice.lift - 0.1f) < 1e-5f,
          "climbing: the climb, its playhead set from the distance, not run",d);
    in.rope_climb = 0;
    p.Tick(in);
    Check(p.choice.clip == CLIP_ROPE_CLIMB && fabsf(p.choice.pinned_time - 0.25f) < 1e-4f,
          "stopping holds the climb's pose where she stopped");
    in.mode = MODE_AIR;
    p.Tick(in);
    in.mode = MODE_ROPE;
    in.rope_climbed = 0.0f;
    p.Tick(in);
    Check(p.choice.clip == CLIP_ROPE,"off the rope and on again: the hang, until she climbs");

    //--- A steady climb through the pause and round the loop ---
    Puppet sc;
    sc.climb_times = p.climb_times;
    sc.climb_rise = p.climb_rise;
    sc.climb_cycle_rise = p.climb_cycle_rise;
    sc.clip_duration[CLIP_ROPE_CLIMB] = 2.0f;
    ArcherAnimParams st;
    st.mode = MODE_ROPE;
    st.rope_climb = 1;
    float cap = fmaxf(PUPPET_CLIMB_RATE_MAX,1.5f * ROPE_CLIMB_SPEED * 2.0f / 0.6f) * ARCHER_DT;
    float worst_step = 0.0f, worst_lag = 0.0f, last_t = -1.0f, last_base = 0.0f;
    int base_jumps_off_wrap = 0, wraps = 0;
    for (int i = 0; i < 200; i++){
        st.rope_climbed = (float)i * ROPE_CLIMB_SPEED * ARCHER_DT;
        sc.Tick(st);
        float t = sc.choice.pinned_time;
        bool f_wrapped = (last_t >= 0.0f && t < last_t - 1.0f);
        if (last_t >= 0.0f && !f_wrapped && t - last_t > worst_step){ worst_step = t - last_t; }
        if (f_wrapped && t + 2.0f - last_t > worst_step){ worst_step = t + 2.0f - last_t; }
        if (f_wrapped){ wraps++; }
        if (i > 0 && sc.choice.lift_base != last_base && !f_wrapped){ base_jumps_off_wrap++; }
        if (sc.climb_target - sc.climb_playhead > worst_lag){ worst_lag = sc.climb_target - sc.climb_playhead; }
        last_t = t;
        last_base = sc.choice.lift_base;
    }
    snprintf(d,sizeof(d),"largest step %.4f s against a cap of %.4f; worst lag %.3f s",worst_step,cap,worst_lag);
    Check(worst_step <= cap + 1e-4f && worst_lag > 0.1f,
          "the pause is played out at the capped rate rather than passed in one tick",d);
    Check(fabsf(sc.climb_target - sc.climb_playhead) < 1e-4f,"and the playhead catches the distance up again after it",d);
    snprintf(d,sizeof(d),"%i wraps, %i base changes elsewhere",wraps,base_jumps_off_wrap);
    Check(wraps >= 2 && base_jumps_off_wrap == 0,
          "the cycle's base moves on exactly when the shown pose wraps, so the gripping hand has no seam",d);

    Puppet none;
    ArcherAnimParams climbing;
    climbing.mode = MODE_ROPE;
    climbing.rope_climb = 1;
    none.Tick(climbing);
    Check(none.choice.clip == CLIP_ROPE,"with no climb clip measured, climbing keeps the hang");

    //--- The loose legs: the Puppet's three decisions ---
    Puppet lg;
    lg.climb_times = p.climb_times;
    lg.climb_rise = p.climb_rise;
    lg.climb_cycle_rise = p.climb_cycle_rise;
    lg.clip_duration[CLIP_ROPE_CLIMB] = 2.0f;
    ArcherAnimParams ground;
    lg.Tick(ground);
    Check(lg.leg_weight == 0.0f && lg.leg_lead_deg == 0.0f,"on the ground the legs are the clip's");
    ArcherAnimParams hang;
    hang.mode = MODE_ROPE;
    lg.Tick(hang);
    Check(lg.leg_weight > 0.0f && lg.leg_weight < 1.0f,"caught, they come loose gradually");
    for (int i = 0; i < PUPPET_LEG_BLEND_TICKS; i++){ lg.Tick(hang); }
    Check(lg.leg_weight == 1.0f && lg.leg_gravity == PUPPET_LEG_GRAVITY_HANG,
          "hanging, fully loose, under all of gravity");
    hang.rope_pump = -1.0f;
    lg.Tick(hang);
    snprintf(d,sizeof(d),"lead %.1f",lg.leg_lead_deg);
    Check(fabsf(lg.leg_lead_deg + PUPPET_LEG_PUMP_DEG) < 1e-4f,"pumping left leads the legs left",d);
    hang.rope_pump = 0.0f;
    hang.rope_climb = 1;
    hang.rope_climbed = 0.1f;
    for (int i = 0; i < PUPPET_LEG_BLEND_TICKS; i++){ lg.Tick(hang); }
    Check(lg.leg_weight == 0.0f,"climbing, the clip has the legs back - its feet grip the rope");
    hang.rope_climb = 0;
    for (int i = 0; i < PUPPET_LEG_BLEND_TICKS; i++){ lg.Tick(hang); }
    Check(lg.leg_weight == 1.0f && lg.leg_gravity == PUPPET_LEG_GRAVITY_GRIP,
          "stopped mid-climb, loose again about the gripping pose, under a share of gravity");
    lg.Tick(ground);
    Check(lg.leg_lead_deg == 0.0f && lg.leg_weight < 1.0f && lg.leg_gravity == PUPPET_LEG_GRAVITY_HANG,
          "off the rope, easing back to the clip");
}

//--- The terrain bay: a way up, and two floaters with none ---------------------------------------
#if ARCHER_TEST_BAY
/*
    The bay is part of the level now rather than a test pattern (Stage.cpp, BuildMainLevel), and
    it makes two promises the eye cannot check: the island can be climbed onto, and the two
    floaters cannot.

    The first is PLAYED - the route the layout was built around, hop by hop. The second is
    SEARCHED, because "nobody can get there" is a claim about every jump rather than one: from
    each surface already reached, stand at points along it, run up for a while either way (or
    not), jump with a short, middling or full hold (or just walk off), steer either way or not at
    all, and see what she lands on. Coarse - a player can mix those - but coarse the same way for
    the island, which it must find, as for the floaters, which it must not.
*/

//Longer than Settle: a landing can hold the controls for a few ticks, and the next hop wants them.
static void SettleBay(Stage& s){
    ArcherInput idle;
    for (int i = 0; i < 300 && !s.f_on_ground; i++){
        StageEvents ev;
        s.Tick(idle,ev);
    }
    Run(s,30,idle);
}

static float Feet(const Stage& s){
    return s.pos.y - ARCHER_HALF_H;
}

//The block she is standing on, or -1.
static int StandingOn(const Stage& s){
    if (!s.f_on_ground){
        return -1;
    }
    for (size_t i = 0; i < s.blocks.size(); i++){
        const StageBlock& b = s.blocks[i];
        if (fabsf(b.Top() - Feet(s)) < 0.05f &&
            s.pos.x + ARCHER_HALF_W > b.Left() && s.pos.x - ARCHER_HALF_W < b.Right()){
            return (int)i;
        }
    }
    return -1;
}

//The block whose top is `top` and which spans `x`, or -1 - how the checks name a surface.
static int BlockAt(const Stage& s, float x, float top){
    for (size_t i = 0; i < s.blocks.size(); i++){
        const StageBlock& b = s.blocks[i];
        if (fabsf(b.Top() - top) < 0.01f && x > b.Left() && x < b.Right()){
            return (int)i;
        }
    }
    return -1;
}

//A running jump: run toward `dir` until past jump_x, jump on a full hold, steer `dir` to landing.
static void BayHop(Stage& s, float dir, float jump_x){
    ArcherInput run;
    run.move_axis = dir;
    for (int i = 0; i < 600; i++){
        if ((dir < 0.0f) ? (s.pos.x <= jump_x) : (s.pos.x >= jump_x)){
            break;
        }
        StageEvents ev;
        s.Tick(run,ev);
    }
    ArcherInput jump = run;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents ev;
    s.Tick(jump,ev);
    ArcherInput fly = run;
    fly.f_jump_down = true;
    Run(s,3,fly);
    for (int i = 0; i < 240 && !s.f_on_ground; i++){
        StageEvents e;
        s.Tick(fly,e);
    }
    SettleBay(s);
}

//A running jump that stops where it is aimed: steer `dir` until past land_x, then steer back to
//brake - air friction alone would carry a full-speed jump nearly three units further.
static void BayLeap(Stage& s, float dir, float jump_x, float land_x){
    ArcherInput run;
    run.move_axis = dir;
    for (int i = 0; i < 600; i++){
        if ((dir < 0.0f) ? (s.pos.x <= jump_x) : (s.pos.x >= jump_x)){
            break;
        }
        StageEvents ev;
        s.Tick(run,ev);
    }
    ArcherInput jump = run;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents ev;
    s.Tick(jump,ev);
    ArcherInput fly;
    fly.f_jump_down = true;
    bool f_past = false;
    for (int i = 0; i < 240; i++){
        f_past = f_past || ((dir < 0.0f) ? (s.pos.x <= land_x) : (s.pos.x >= land_x));
        fly.move_axis = !f_past ? dir : ((s.vel.x * dir > 0.3f) ? -dir : 0.0f);
        StageEvents e;
        s.Tick(fly,e);
        if (s.f_on_ground && i > 2){
            break;
        }
    }
    SettleBay(s);
}

//A short climb onto a narrow stone: walk to x_at, jump straight up, and step toward `dir` only
//once the feet are above `top` - holding the direction throughout overshoots a 1.8-wide stone.
static void BayRiseStep(Stage& s, float dir, float x_at, float top){
    ArcherInput walk;
    walk.move_axis = (x_at < s.pos.x) ? -1.0f : 1.0f;
    for (int i = 0; i < 600 && fabsf(s.pos.x - x_at) > 0.08f; i++){
        StageEvents ev;
        s.Tick(walk,ev);
    }
    SettleBay(s);
    ArcherInput jump;
    jump.f_jump_pressed = true;
    jump.f_jump_down = true;
    StageEvents ev;
    s.Tick(jump,ev);
    ArcherInput fly;
    fly.f_jump_down = true;
    for (int i = 0; i < 240; i++){
        fly.move_axis = (Feet(s) > top + 0.05f) ? dir : 0.0f;
        StageEvents e;
        s.Tick(fly,e);
        if (s.f_on_ground && i > 2){
            break;
        }
    }
    SettleBay(s);
}

static void TestBayClimb(){
    printf("\nthe terrain bay\n");
    char d[160];

    //--- Depth: every block in every level still covers the play plane ---------------------------
    {
        bool f_covers = true;
        for (int level = 0; level < STAGE_LEVEL_COUNT; level++){
            Stage l;
            l.SetLevel(level);
            for (size_t i = 0; i < l.blocks.size(); i++){
                const StageBlock& b = l.blocks[i];
                if (fabsf(b.z) + STAGE_BLOCK_MIN_COVER > b.HalfDepth() + 1e-4f){
                    snprintf(d,sizeof(d),"level %i block %i: z %.2f, half-depth %.2f",level,(int)i,b.z,b.HalfDepth());
                    f_covers = false;
                }
            }
        }
        Check(f_covers,"every block keeps STAGE_BLOCK_MIN_COVER of depth either side of z 0",f_covers ? NULL : d);
    }

    //--- The route ----------------------------------------------------------------------------
    Stage s;
    s.pos = v2(-10.0f,ARCHER_HALF_H);
    SettleBay(s);
    BayHop(s,-1.0f,-11.40f);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    CheckNear(Feet(s),2.40f,0.02f,"from the main ground onto the hill",d);
    BayHop(s,-1.0f,-15.85f);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    CheckNear(Feet(s),4.80f,0.02f,"from its left end onto stone one, under the island's end",d);
    BayHop(s,1.0f,-18.40f);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    CheckNear(Feet(s),7.20f,0.02f,"back right across 2.9 onto stone two, nothing overhead",d);
    BayRiseStep(s,1.0f,-14.40f,9.60f);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    CheckNear(Feet(s),9.60f,0.02f,"up onto stone three",d);
    BayHop(s,-1.0f,-13.75f);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    CheckNear(Feet(s),11.25f,0.02f,"and a running jump left, onto the island",d);

    //--- The authored tiles, right of stone three ----------------------------------------------
    //Colliders out of StageScenery: invisible SOLID blocks whose tops are the tiles' grass.
    {
        Stage r;
        int big = BlockAt(r,-7.5f,9.60f);
        int round = BlockAt(r,-2.0f,8.40f);
        Check(big >= 0 && round >= 0 && r.blocks[big].f_invisible && r.blocks[round].f_invisible,
              "both terrain tiles have an invisible collider under their grass");
        r.pos = v2(-12.9f,9.60f + ARCHER_HALF_H + 0.02f);
        SettleBay(r);
        BayLeap(r,1.0f,-12.30f,-8.00f);
        snprintf(d,sizeof(d),"at x %.2f",r.pos.x);
        CheckNear(Feet(r),9.60f,0.02f,"from stone three, a running jump right onto the big tile",d);
        BayLeap(r,1.0f,-6.40f,-2.50f);
        snprintf(d,sizeof(d),"at x %.2f",r.pos.x);
        CheckNear(Feet(r),8.40f,0.02f,"and on, down onto the round one",d);
    }

    //--- The search ---------------------------------------------------------------------------
    const Stage level;
    int island = BlockAt(level,-26.0f,11.25f);
    int under = BlockAt(level,-30.0f,6.80f);
    int over = BlockAt(level,-23.0f,16.40f);
    Check(island >= 0 && under >= 0 && over >= 0,"the island and both floaters are where this test looks");

    const float x_lo = ARCHER_TEST_BAY_X_MIN;
    const float x_hi = ARCHER_TEST_BAY_X_MAX + 1.0f;       //a little of the main ground, to enter by
    std::vector<bool> reached(level.blocks.size(),false);
    std::vector<int> queue;
    {
        Stage g = level;
        g.pos = v2(-11.0f,ARCHER_HALF_H);
        SettleBay(g);
        int b = StandingOn(g);
        if (b >= 0){
            reached[b] = true;
            queue.push_back(b);
        }
    }
    int sims = 0;
    const int runups[] = { 0, 6, 12, 24 };
    const int holds[] = { 0, 10, 30 };                      //0 is walking off, no jump
    while (!queue.empty()){
        int from = queue.back();
        queue.pop_back();
        const StageBlock blk = level.blocks[from];
        float lo = fmaxf(blk.Left() + 0.05f,x_lo);
        float hi = fminf(blk.Right() - 0.05f,x_hi);
        for (float x = lo; x <= hi; x += 0.5f){
            Stage base = level;
            base.pos = v2(x,blk.Top() + ARCHER_HALF_H + 0.02f);
            SettleBay(base);
            if (StandingOn(base) != from){
                continue;
            }
            for (int d1 = -1; d1 <= 1; d1++){
            for (int k : runups){
            for (int h : holds){
            for (int d2 = -1; d2 <= 1; d2++){
                if (d1 == 0 && k > 0){
                    continue;
                }
                Stage t = base;
                ArcherInput run;
                run.move_axis = (float)d1;
                Run(t,k,run);
                if (h > 0){
                    ArcherInput jump = run;
                    jump.f_jump_pressed = true;
                    jump.f_jump_down = true;
                    StageEvents ev;
                    t.Tick(jump,ev);
                }
                ArcherInput fly;
                fly.move_axis = (float)d2;
                for (int i = 0; i < 260; i++){
                    fly.f_jump_down = (i < h);
                    StageEvents ev;
                    t.Tick(fly,ev);
                    if (t.f_on_ground && i > 2){
                        break;
                    }
                }
                sims++;
                int to = StandingOn(t);
                if (to >= 0 && !reached[to]){
                    reached[to] = true;
                    queue.push_back(to);
                }
            }}}}
        }
    }
    snprintf(d,sizeof(d),"%i jumps tried",sims);
    Check(island >= 0 && reached[island],"the search finds the island - so it would notice losing it",d);
    Check(under >= 0 && !reached[under],"nothing reaches the floater under the island",d);
    Check(over >= 0 && !reached[over],"nor the one above it",d);
}
#endif

/*
    THE ZONES (bridge_crumble_plan.md section 1, cue_plan.md section 8). What matters: every level
    is covered with no gaps and no overlaps, so she is always in exactly one and the HUD always
    names it; every zone's arrival spot is ground she lands on inside that zone; and crossing from
    one to the next reports one left and one entered, once each.
*/
static void TestZones(){
    printf("\nthe zones\n");
    char d[220];
    const int levels[] = { STAGE_LEVEL_MAIN, STAGE_LEVEL_RANGE, STAGE_LEVEL_ROPE };
    const char* level_names[] = { "main", "range", "rope" };
    for (int li = 0; li < 3; li++){
        Stage s;
        s.SetLevel(levels[li]);
        snprintf(d,sizeof(d),"%s: %i",level_names[li],(int)s.zones.size());
        Check(!s.zones.empty(),"every level declares zones",d);
        //Side by side: sorted by their left edges, each starts where the last ended. The areas;
        //a trigger sits inside one, and has no arrival spot. So does a NESTED area - a shorter one
        //laid over taller ones, like the bridge's up in the air, which CurrentZone names over them.
        auto inside = [](const StageZone& in, const StageZone& out){
            return in.id != out.id && in.hh < out.hh &&
                   in.Overlaps(out.Left(),out.Right(),out.Bottom(),out.Top()) &&
                   in.Bottom() >= out.Bottom() && in.Top() <= out.Top();
        };
        std::vector<StageZone> z;
        for (const StageZone& zone : s.zones){
            bool f_nested = false;
            for (const StageZone& other : s.zones){
                f_nested = f_nested || (other.f_area && inside(zone,other));
            }
            if (zone.f_area && !f_nested){
                z.push_back(zone);
            }
        }
        std::sort(z.begin(),z.end(),[](const StageZone& a, const StageZone& b){ return a.Left() < b.Left(); });
        int gaps = 0, overlaps = 0;
        for (size_t i = 1; i < z.size(); i++){
            float step = z[i].Left() - z[i - 1].Right();
            gaps += (step > 0.001f) ? 1 : 0;
            overlaps += (step < -0.001f) ? 1 : 0;
        }
        snprintf(d,sizeof(d),"%s: %i gaps, %i overlaps",level_names[li],gaps,overlaps);
        Check(gaps == 0 && overlaps == 0,"the zones sit side by side",d);
        int bad = 0;
        std::string which;
        for (size_t i = 0; i < s.zones.size(); i++){
            Stage t = s;
            const StageZone& zone = s.zones[i];
            if (!zone.f_area){
                continue;
            }
            DropOnto(t,zone.arrive.x,zone.arrive.y);
            bool f_ok = t.f_on_ground && t.CurrentZone() == zone.id &&
                        fabsf(t.pos.y - ARCHER_HALF_H - zone.arrive.y) < 0.6f;
            if (!f_ok){
                bad++;
                which += " '" + zone.name + "'";
            }
        }
        snprintf(d,sizeof(d),"%s:%s",level_names[li],bad ? which.c_str() : " all");
        Check(bad == 0,"every zone's arrival spot is ground inside it",d);
    }

    //Across the old end of the level into the test ground's first piece: one left, one entered.
    Stage s;
    int branches = s.FindZone("Branches");
    int ground = s.FindZone("Stepping stones");
    Check(branches >= 0 && ground >= 0,"the main level has the branches and the stepping stones");
    DropOnto(s,172.0f,0.3f);
    Check(s.CurrentZone() == branches,"standing at 172 she is among the branches");
    ArcherInput right;
    right.move_axis = 1.0f;
    int entered = 0, left = 0, wrong = 0;
    for (int i = 0; i < 90; i++){
        StageEvents e;
        s.Tick(right,e);
        for (int id : e.zones_entered){ entered += (id == ground) ? 1 : 0; wrong += (id != ground) ? 1 : 0; }
        for (int id : e.zones_left){ left += (id == branches) ? 1 : 0; wrong += (id != branches) ? 1 : 0; }
    }
    snprintf(d,sizeof(d),"entered %i, left %i, others %i, at x %.1f",entered,left,wrong,s.pos.x);
    Check(entered == 1 && left == 1 && wrong == 0,"running across the edge leaves one zone and enters the next, once each",d);
    Check(s.CurrentZone() == ground && s.pos.x > 185.0f,"and the level no longer ends at 176",d);
    //From past the chase, on to the end wall.
    DropOnto(s,254.0f,0.3f);
    Run(s,600,right);
    snprintf(d,sizeof(d),"stopped at x %.2f",s.pos.x);
    Check(s.pos.x < 264.0f && s.pos.x > 260.0f,"the end wall stops her at the test ground's end",d);

    //A restart forgets what she was in, so its first ticks enter the start zone again.
    s.Reset();
    int start = s.FindZone("Start");
    bool f_reentered = false;
    for (int i = 0; i < 5 && !f_reentered; i++){
        StageEvents e;
        s.Tick(ArcherInput(),e);
        for (int id : e.zones_entered){ f_reentered |= (id == start); }
    }
    Check(f_reentered,"a restart enters the start zone again");
}

//The level's crumble stones, in block order - which is left to right in the pit. The ones that
//start under her feet: the chase's slabs are a group's, and TestChase's.
static std::vector<int> CrumbleStones(const Stage& s){
    std::vector<int> out;
    for (size_t i = 0; i < s.blocks.size(); i++){
        if (s.blocks[i].kind == BLOCK_CRUMBLE && s.blocks[i].crumble_group < 0){
            out.push_back((int)i);
        }
    }
    return out;
}

/*
    THE CRUMBLING STONES (bridge_crumble_plan.md section 2). A stone holds her, shakes for
    CRUMBLE_SHAKE_TICKS, and goes, with its two events once each; stepping off early does not save
    it; one nobody touches stays whole; a gone one stays gone until a restart, which brings it back;
    and an arrow stuck in one falls when it goes.
*/
static void TestCrumble(){
    printf("\nthe crumbling stones\n");
    char d[220];
    Stage s;
    std::vector<int> stones = CrumbleStones(s);
    snprintf(d,sizeof(d),"%i",(int)stones.size());
    Check(stones.size() == 4,"the stepping stones' pit has four crumble stones",d);
    if (stones.size() < 4){
        return;
    }

    //Onto stone one, counting events by hand - DropOnto throws them away.
    Stage a = s;
    const int s0 = stones[0];
    a.pos = v2(a.blocks[s0].x,a.blocks[s0].Top() + 0.3f + ARCHER_HALF_H);
    a.vel = v2(0.0f,0.0f);
    a.mode = MODE_AIR;
    a.f_on_ground = false;
    int started = 0, gone = 0, landed_at = -1, gone_at = -1, fell_at = -1;
    bool f_held = true;
    for (int t = 0; t < 200; t++){
        StageEvents e;
        a.Tick(ArcherInput(),e);
        for (int b : e.crumbles_started){ started += (b == s0) ? 1 : 0; }
        for (int b : e.crumbled_blocks){ gone += (b == s0) ? 1 : 0; }
        if (landed_at < 0 && a.f_on_ground){ landed_at = t; }
        if (gone_at < 0 && !a.blocks[s0].f_alive){ gone_at = t; }
        if (landed_at >= 0 && gone_at < 0 && !a.f_on_ground){ f_held = false; }
        if (gone_at >= 0 && fell_at < 0 && !a.f_on_ground){ fell_at = t; }
    }
    snprintf(d,sizeof(d),"landed tick %i, gone tick %i, fell tick %i, started %i, gone %i",
             landed_at,gone_at,fell_at,started,gone);
    Check(started == 1 && gone == 1,"a stone she lands on starts once and goes once",d);
    Check(gone_at - landed_at == CRUMBLE_SHAKE_TICKS,"it goes CRUMBLE_SHAKE_TICKS after she landed",d);
    Check(f_held,"and holds her the whole time it shakes",d);
    Check(fell_at >= gone_at && fell_at <= gone_at + 1,"then she drops the tick it goes",d);
    snprintf(d,sizeof(d),"feet %.2f",a.pos.y - ARCHER_HALF_H);
    Check(a.f_on_ground && fabsf(a.pos.y - ARCHER_HALF_H - (-4.0f)) < 0.02f,"into the pit, onto its floor",d);

    //Stepping off early does not save it.
    Stage b = s;
    const int s1 = stones[1];
    DropOnto(b,b.blocks[s1].x,b.blocks[s1].Top() + 0.3f);
    Run(b,4,ArcherInput());
    DropOnto(b,256.0f,0.3f);
    Run(b,CRUMBLE_SHAKE_TICKS,ArcherInput());
    snprintf(d,sizeof(d),"stone two alive %i, stone three alive %i, whole %i",b.blocks[s1].f_alive ? 1 : 0,
             b.blocks[stones[2]].f_alive ? 1 : 0,b.blocks[stones[2]].crumble_ticks < 0 ? 1 : 0);
    Check(!b.blocks[s1].f_alive,"leaving a shaking stone does not stop it going",d);
    Check(b.blocks[stones[2]].f_alive && b.blocks[stones[2]].crumble_ticks < 0,
          "and a stone nobody stood on is still whole",d);

    //Gone stays gone - a long while later, and through her standing about - until a restart.
    Run(b,2000,ArcherInput());
    Check(!b.blocks[s1].f_alive,"a gone stone stays gone");
    b.Reset();
    bool f_back = true;
    for (int i : CrumbleStones(b)){
        f_back = f_back && b.blocks[i].f_alive && b.blocks[i].crumble_ticks < 0;
    }
    Check(f_back,"and a restart brings every stone back whole");

    //An arrow stuck in a stone's face falls when the stone goes.
    Stage c = s;
    const int s3 = stones[3];
    c.arrows[0].f_live = true;
    c.StickArrow(0,v3(c.blocks[s3].Left(),c.blocks[s3].y,0.0f));
    float stuck_y = c.arrows[0].pos.y;
    DropOnto(c,c.blocks[s3].x,c.blocks[s3].Top() + 0.3f);
    Run(c,CRUMBLE_SHAKE_TICKS + 10,ArcherInput());
    snprintf(d,sizeof(d),"stuck %i, y %.2f from %.2f",c.arrows[0].f_stuck ? 1 : 0,c.arrows[0].pos.y,stuck_y);
    Check(!c.blocks[s3].f_alive && c.arrows[0].pos.y < stuck_y - 0.1f,"an arrow stuck in a stone falls when it goes",d);
}

/*
    Runs the chase from the rim: walks right at `axis` until the trigger starts the group, stands
    `hesitate` ticks, then runs, jumping the hole from a fixed spot. True if she ends standing on
    the solid ground past the floor; false once she is in the pit.
*/
static bool RunChase(Stage s, int hesitate, float axis){
    const float jump_from = 234.0f;     //her centre; the hole is 236..238
    int since_start = -1, jump_held = 0;
    bool f_jumped = false;
    for (int t = 0; t < 900; t++){
        ArcherInput in;
        if (since_start < 0){
            in.move_axis = axis;
        }else if (since_start >= hesitate){
            in.move_axis = axis;
        }
        if (!f_jumped && s.f_on_ground && s.pos.x >= jump_from){
            in.f_jump_pressed = true;
            f_jumped = true;
            jump_held = 12;
        }
        in.f_jump_down = jump_held > 0;
        jump_held = (jump_held > 0) ? jump_held - 1 : 0;
        StageEvents e;
        s.Tick(in,e);
        if (since_start >= 0){
            since_start++;
        }else if (!e.crumble_groups_started.empty()){
            since_start = 0;
        }
        if (s.pos.y - ARCHER_HALF_H < -1.0f){
            return false;
        }
        if (s.f_on_ground && s.pos.x - ARCHER_HALF_W > 252.0f){
            return true;
        }
    }
    return false;
}

/*
    THE CHASE (bridge_crumble_plan.md section 2): a floor of crumble slabs that a trigger starts
    going one after another. The slabs ignore her standing on them; the trigger starts the group
    once per run; each slab starts on its tick - by distance, so the hole does not change the pace
    - and goes CRUMBLE_SHAKE_TICKS later; the group reports its start and its end once each; a
    restart brings the floor back unfired. And the point of it: run at her speed it is outrun, with
    a margin that is MEASURED - how long she can stand at the start and still make it - and at a
    walk it catches her.
*/
static void TestChase(){
    printf("\nthe chase\n");
    char d[220];
    Stage s;
    int group = -1;
    for (size_t g = 0; g < s.crumble_groups.size(); g++){
        if (s.crumble_groups[g].name == "chase"){
            group = (int)g;
        }
    }
    int trigger = s.FindZone("chase start");
    Check(group >= 0 && trigger >= 0,"the main level has the chase's group and its trigger");
    if (group < 0 || trigger < 0){
        return;
    }
    const StageCrumbleGroup& g0 = s.crumble_groups[group];
    bool f_owned = true;
    for (int b : g0.blocks){
        f_owned = f_owned && s.blocks[b].kind == BLOCK_CRUMBLE && s.blocks[b].crumble_group == group;
    }
    snprintf(d,sizeof(d),"%i slabs",(int)g0.blocks.size());
    Check(g0.blocks.size() == 15 && f_owned,"fifteen slabs, each naming the group back",d);
    const StageZone& tz = s.zones[trigger];
    Check(!tz.f_area && tz.effects.size() == 1 && tz.effects[0].kind == ZONE_START_CRUMBLE_GROUP &&
          tz.effects[0].target == group,"its trigger is not an area, and starts the group");
    //By distance: 8 ticks a unit from the first slab's left edge, across the hole too.
    bool f_paced = true;
    for (size_t k = 0; k < g0.blocks.size(); k++){
        int want = (int)lroundf((s.blocks[g0.blocks[k]].Left() - 220.0f) * CHASE_TICKS_PER_UNIT);
        f_paced = f_paced && g0.starts[k] == want;
    }
    Check(f_paced,"each slab's start is its distance along the floor, hole or no hole");

    //Standing on a slab beyond the trigger starts nothing: they go by the group alone.
    Stage a = s;
    DropOnto(a,244.0f,0.3f);
    Run(a,120,ArcherInput());
    bool f_whole = true;
    for (int b : g0.blocks){
        f_whole = f_whole && a.blocks[b].f_alive && a.blocks[b].crumble_ticks < 0;
    }
    Check(f_whole && a.f_on_ground,"standing on a slab past the trigger starts nothing");

    //On the first slab, in the trigger, doing nothing: every slab goes on its tick, in order.
    Stage b = s;
    b.pos = v2(221.0f,0.3f + ARCHER_HALF_H);
    b.vel = v2(0.0f,0.0f);
    b.mode = MODE_AIR;
    b.f_on_ground = false;
    std::vector<int> started_at(b.blocks.size(),-1), gone_at(b.blocks.size(),-1);
    int group_started = -1, group_done = -1, starts = 0, dones = 0, landed_at = -1, fell_at = -1;
    for (int t = 0; t < 400; t++){
        StageEvents e;
        b.Tick(ArcherInput(),e);
        for (int i : e.crumble_groups_started){ starts++; group_started = (i == group) ? t : group_started; }
        for (int i : e.crumble_groups_done){ dones++; group_done = (i == group) ? t : group_done; }
        for (int i : e.crumbles_started){ started_at[i] = t; }
        for (int i : e.crumbled_blocks){ gone_at[i] = t; }
        if (landed_at < 0 && b.f_on_ground){ landed_at = t; }
        if (landed_at >= 0 && fell_at < 0 && !b.f_on_ground){ fell_at = t; }
    }
    int off_pace = 0, never = 0;
    for (size_t k = 0; k < g0.blocks.size(); k++){
        int i = g0.blocks[k];
        if (started_at[i] < 0 || gone_at[i] < 0){
            never++;
            continue;
        }
        //The group starts the tick she enters; its slabs count from the tick after.
        bool f_ok = started_at[i] == group_started + 1 + g0.starts[k] &&
                    gone_at[i] - started_at[i] == CRUMBLE_SHAKE_TICKS;
        off_pace += f_ok ? 0 : 1;
    }
    snprintf(d,sizeof(d),"group started tick %i, done tick %i (%i, %i times); %i off pace, %i never went",
             group_started,group_done,starts,dones,off_pace,never);
    Check(starts == 1 && dones == 1,"the group starts once and ends once",d);
    Check(off_pace == 0 && never == 0,"every slab starts on its tick and goes CRUMBLE_SHAKE_TICKS later",d);
    int last = g0.blocks.back();
    Check(group_done == gone_at[last],"and the group ends the tick its last slab goes",d);
    snprintf(d,sizeof(d),"fell at tick %i, the first slab gone at %i; feet %.2f",fell_at,gone_at[g0.blocks[0]],
             b.pos.y - ARCHER_HALF_H);
    Check(fell_at >= 0 && fell_at <= gone_at[g0.blocks[0]] + 1 && b.f_on_ground &&
          fabsf(b.pos.y - ARCHER_HALF_H - (-4.0f)) < 0.02f,"standing on it, she drops into the pit with the first slab",d);

    //Once per run: back out and in again does not restart it. A restart brings it all back.
    Stage c = s;
    DropOnto(c,221.0f,0.3f);
    int first_ticks = c.crumble_groups[group].ticks;
    uint64_t then = c.ticks;
    DropOnto(c,216.0f,0.3f);
    bool f_out = !c.zone_inside[trigger];
    //Over the first two slabs, still in the trigger: held up by the second if the first has gone.
    DropOnto(c,222.2f,0.3f);
    int want = first_ticks + (int)(c.ticks - then);
    snprintf(d,sizeof(d),"group ticks %i, then %i against %i; left it %i, back in %i",first_ticks,
             c.crumble_groups[group].ticks,want,f_out ? 1 : 0,c.zone_inside[trigger] ? 1 : 0);
    Check(first_ticks > 0 && f_out && c.zone_inside[trigger] && c.crumble_groups[group].ticks == want,
          "entering the trigger again does not restart the group",d);
    c.Reset();
    bool f_back = c.crumble_groups[group].ticks < 0 && !c.crumble_groups[group].f_done && !c.zones[trigger].f_fired;
    for (int i : c.crumble_groups[group].blocks){
        f_back = f_back && c.blocks[i].f_alive && c.blocks[i].crumble_ticks < 0;
    }
    Check(f_back,"a restart brings the floor back whole, and the trigger unfired");

    //Outrun at her run speed; the margin is how long she can stand still in the trigger first.
    Stage rim = s;
    DropOnto(rim,216.0f,0.3f);
    Run(rim,30,ArcherInput());
    Check(RunChase(rim,0,1.0f),"running straight through, she outruns the floor");
    int margin = -1;
    for (int h = 0; h <= 120; h++){
        if (!RunChase(rim,h,1.0f)){
            break;
        }
        margin = h;
    }
    snprintf(d,sizeof(d),"stands up to %i ticks (%.2f s) in the trigger and still makes it",margin,margin / (float)ARCHER_TPS);
    Check(margin >= 6 && margin <= 40,"the margin: a moment to react, never time to stand about",d);
    Check(!RunChase(rim,0,0.5f),"at a walk, the floor catches her");
}

//The fastest any point of a bridge is moving.
static float BridgeSpeed(const StageBridge& br){
    float top = 0.0f;
    for (const v2& v : br.v){
        top = fmaxf(top,sqrtf(v.x * v.x + v.y * v.y));
    }
    return top;
}

/*
    THE ROPE BRIDGE as a surface (bridge_crumble_plan.md section 3): it hangs still from its two
    anchors at rest, sagging within range; she stands on it, her feet on its planks, and it sags
    further under her, the dip where she is; a landing drives it down past that and it settles
    again; Down drops her through; she walks across it from slab to slab without leaving her feet;
    and it comes back to rest once she is off. Two builds of it are identical. And it never hangs
    into the ground route: loaded, it stays above the head of a jump across the gap below.
*/
static void TestBridge(){
    printf("\nthe rope bridge\n");
    char d[240];
    Stage s;
    Check(s.bridges.size() == 2 && !s.bridges[0].f_breakable,"the main level has two rope bridges, the first unbreakable");
    if (s.bridges.empty()){
        return;
    }
    const StageBridge& br = s.bridges[0];
    const float anchor_y = br.a.y;
    const float mid_x = (br.a.x + br.b.x) * 0.5f;
    float worst_stretch = 0.0f;
    for (size_t j = 0; j + 1 < br.p.size(); j++){
        float dx = br.p[j + 1].x - br.p[j].x, dy = br.p[j + 1].y - br.p[j].y;
        worst_stretch = fmaxf(worst_stretch,sqrtf(dx * dx + dy * dy) / br.link - 1.0f);
    }
    float rest_sag = anchor_y - br.Lowest();
    snprintf(d,sizeof(d),"sag %.2f, fastest point %.4f, planks stretched up to %.2f%%, ends at (%.2f,%.2f) (%.2f,%.2f)",
             rest_sag,BridgeSpeed(br),worst_stretch * 100.0f,br.p.front().x,br.p.front().y,br.p.back().x,br.p.back().y);
    Check(rest_sag > 0.6f && rest_sag < 1.2f,"at rest it sags between 0.6 and 1.2",d);
    Check(BridgeSpeed(br) < 0.01f,"and hangs still: the level starts with it settled",d);
    Check(worst_stretch < 0.02f,"its planks barely stretched by its own weight, under 2%",d);
    Stage twin;
    bool f_same = twin.bridges.size() == s.bridges.size() && twin.bridges[0].p.size() == br.p.size();
    for (size_t j = 0; f_same && j < br.p.size(); j++){
        f_same = twin.bridges[0].p[j].x == br.p[j].x && twin.bridges[0].p[j].y == br.p[j].y;
    }
    Check(f_same,"two builds of it are identical, point for point");

    //Standing in the middle: on it, feet on its planks, the dip under her and deeper than at rest.
    Stage a = s;
    DropOnto(a,mid_x,br.SurfaceY(mid_x) + 0.3f);
    Run(a,240,ArcherInput());
    const StageBridge& la = a.bridges[0];
    float loaded_sag = anchor_y - la.Lowest();
    float feet_gap = (a.pos.y - ARCHER_HALF_H) - la.SurfaceY(a.pos.x);
    snprintf(d,sizeof(d),"bridge_on %i, on ground %i, sag %.2f against %.2f at rest, feet %.3f off its surface, fastest %.3f",
             a.bridge_on,a.f_on_ground ? 1 : 0,loaded_sag,rest_sag,feet_gap,BridgeSpeed(la));
    Check(a.bridge_on == 0 && a.f_on_ground && fabsf(feet_gap) < 0.02f,"she stands on it, feet on its planks",d);
    Check(loaded_sag > rest_sag + 0.2f && loaded_sag < 2.0f,"it sags further under her, and not past 2",d);
    Check(BridgeSpeed(la) < 0.05f,"and has stopped bouncing four seconds later",d);
    //Well clear of the ground route: a running jump over the gap lifts her head to 5.0.
    snprintf(d,sizeof(d),"lowest %.2f",la.Lowest());
    Check(la.Lowest() > 5.3f,"loaded, it still hangs above a jump across the gap below",d);

    //Off-centre, the dip goes with her: the lowest point is by her, not in the middle.
    Stage off = s;
    DropOnto(off,br.a.x + 2.0f,br.SurfaceY(br.a.x + 2.0f) + 0.3f);
    Run(off,240,ArcherInput());
    float low_x = 0.0f, low_y = 1e30f;
    for (const v2& q : off.bridges[0].p){
        if (q.y < low_y){ low_y = q.y; low_x = q.x; }
    }
    snprintf(d,sizeof(d),"she is at %.2f, its lowest point at %.2f",off.pos.x,low_x);
    Check(off.bridge_on == 0 && fabsf(low_x - off.pos.x) < br.link * 1.5f,"the dip is where she stands",d);

    //A landing from 2 up drives it down past where standing leaves it, and it settles back.
    Stage l = s;
    DropOnto(l,mid_x,br.SurfaceY(mid_x) + 2.0f);
    float deepest = 1e30f;
    for (int t = 0; t < 120; t++){
        StageEvents e;
        l.Tick(ArcherInput(),e);
        deepest = fminf(deepest,l.bridges[0].Lowest());
    }
    Run(l,240,ArcherInput());
    snprintf(d,sizeof(d),"deepest %.2f, standing %.2f, after %.2f",deepest,la.Lowest(),l.bridges[0].Lowest());
    Check(deepest < la.Lowest() - 0.1f,"a landing drives it down past where standing leaves it",d);
    Check(fabsf(l.bridges[0].Lowest() - la.Lowest()) < 0.03f && l.bridge_on == 0,"then it settles back, her still on it",d);

    //Down drops her through it.
    Stage dn = a;
    ArcherInput down;
    down.f_down_held = true;
    Run(dn,30,down);
    snprintf(d,sizeof(d),"feet %.2f against its %.2f",dn.pos.y - ARCHER_HALF_H,dn.bridges[0].SurfaceY(dn.pos.x));
    Check(dn.bridge_on < 0 && dn.pos.y - ARCHER_HALF_H < dn.bridges[0].SurfaceY(dn.pos.x) - 1.0f,"Down drops her through it",d);

    //Walked across from slab two to slab three, never off her feet; then it comes back to rest.
    Stage w = s;
    DropOnto(w,15.5f,7.3f);
    Run(w,10,ArcherInput());
    ArcherInput right;
    right.move_axis = 1.0f;
    int airborne = 0, on_it = 0;
    for (int t = 0; t < 150 && w.pos.x < 25.5f; t++){
        StageEvents e;
        w.Tick(right,e);
        airborne += w.f_on_ground ? 0 : 1;
        on_it += (w.bridge_on == 0) ? 1 : 0;
    }
    snprintf(d,sizeof(d),"at x %.2f feet %.2f; %i ticks on it, %i off the ground",w.pos.x,w.pos.y - ARCHER_HALF_H,on_it,airborne);
    Check(w.pos.x > 25.0f && fabsf(w.pos.y - ARCHER_HALF_H - 7.0f) < 0.02f && on_it > 20 && airborne == 0,
          "she runs across it from slab two to slab three without leaving her feet",d);
    Run(w,300,ArcherInput());
    snprintf(d,sizeof(d),"sag %.2f against %.2f, fastest %.4f",anchor_y - w.bridges[0].Lowest(),rest_sag,BridgeSpeed(w.bridges[0]));
    Check(fabsf((anchor_y - w.bridges[0].Lowest()) - rest_sag) < 0.03f && BridgeSpeed(w.bridges[0]) < 0.02f,
          "with her off it, it comes back to rest",d);

    //The Bridge area names it while she is up there, and not from the ground under it.
    int zone = s.FindZone("Bridge");
    Stage under = s;
    DropOnto(under,20.0f,0.3f);
    snprintf(d,sizeof(d),"up there %i, below %i, the zone %i",a.CurrentZone(),under.CurrentZone(),zone);
    Check(zone >= 0 && a.CurrentZone() == zone && under.CurrentZone() != zone,"the Bridge area is up there, not under it",d);
}

/*
    One hop on a bridge from standing: Jump held 12 ticks, the aim held down through the fall if
    `f_stomp`, until she stands again and a moment after. The landing it made, or none.
*/
struct BridgeHop{
    bool  f_landed = false;
    float speed = 0.0f;
    std::vector<StageEvents::BridgeWarning> warnings;
};
static BridgeHop HopOnBridge(Stage& s, bool f_stomp){
    BridgeHop out;
    bool f_left = false;
    for (int t = 0; t < 120; t++){
        ArcherInput in;
        in.f_jump_pressed = (t == 0);
        in.f_jump_down = (t < 12);
        if (f_stomp && !s.f_on_ground && s.vel.y < 0.0f){
            in.aim_axis = -1.0f;
        }
        StageEvents e;
        s.Tick(in,e);
        for (const StageEvents::BridgeLanding& l : e.bridge_landings){
            out.f_landed = true;
            out.speed = l.speed;
        }
        for (const StageEvents::BridgeWarning& w : e.bridge_warnings){
            out.warnings.push_back(w);
        }
        f_left = f_left || !s.f_on_ground;
        if (f_left && s.f_on_ground && t > 20){
            break;
        }
    }
    Run(s,30,ArcherInput());
    return out;
}

/*
    THE SNAPPING BRIDGE (bridge_crumble_plan.md section 3, "Strain, warnings and the snap"): the
    second bridge, slab three to slab four. A gentle crossing leaves it sound. Hops in its middle
    strain it - each landing more than the last, the warnings in order and once each - until it
    snaps on the landing the tuning says; a stomp strains it more than a plain hop, so fewer snap it.
    The first bridge takes any number of hops and never strains. Snapped: she falls to the ground
    below, the halves hang from their anchors apart and settle, and a restart brings it back sound.
    Two identical runs of hops give identical bridges.
*/
static void TestSnapBridge(){
    printf("\nthe snapping bridge\n");
    char d[260];
    Stage s;
    if (s.bridges.size() < 2){
        Check(false,"the main level has the snapping bridge");
        return;
    }
    const int bi = 1;
    const StageBridge& br = s.bridges[bi];
    Check(br.f_breakable && br.level == BRIDGE_SOUND && br.MaxStrain() == 0.0f,"the second bridge is breakable, and starts sound");
    const float mid_x = (br.a.x + br.b.x) * 0.5f;

    //Run across it, slab three to slab four: nothing.
    Stage run = s;
    DropOnto(run,25.2f,7.3f);
    Run(run,10,ArcherInput());
    ArcherInput right;
    right.move_axis = 1.0f;
    int warnings = 0, on_it = 0;
    for (int t = 0; t < 120 && run.pos.x < 31.3f; t++){
        StageEvents e;
        run.Tick(right,e);
        warnings += (int)e.bridge_warnings.size();
        on_it += (run.bridge_on == bi) ? 1 : 0;
    }
    snprintf(d,sizeof(d),"at x %.2f feet %.2f, %i ticks on it, strain %.3f, %i warnings",run.pos.x,
             run.pos.y - ARCHER_HALF_H,on_it,run.bridges[bi].MaxStrain(),warnings);
    Check(run.pos.x > 30.8f && fabsf(run.pos.y - ARCHER_HALF_H - 7.0f) < 0.02f && on_it > 10 &&
          run.bridges[bi].MaxStrain() == 0.0f && warnings == 0,"a run across it leaves it sound",d);

    //Hop in the middle until it goes: count, the strain after each, the warnings in order.
    auto hops_to_snap = [&](bool f_stomp, std::string& log, std::vector<int>& levels, Stage& end){
        Stage h = s;
        DropOnto(h,mid_x,h.bridges[bi].SurfaceY(mid_x) + 0.3f);
        Run(h,60,ArcherInput());
        //The drop onto it was a landing too: from 0.3 it is under the comfort speed.
        for (int n = 1; n <= 12; n++){
            BridgeHop hop = HopOnBridge(h,f_stomp);
            char one[64];
            snprintf(one,sizeof(one),"%s%.1f->%.2f",log.empty() ? "" : ", ",hop.speed,h.bridges[bi].MaxStrain());
            log += one;
            for (const StageEvents::BridgeWarning& w : hop.warnings){
                levels.push_back(w.level);
            }
            if (h.bridges[bi].level == BRIDGE_LEVEL_SNAPPED){
                end = h;
                return n;
            }
        }
        end = h;
        return -1;
    };
    std::string plain_log, stomp_log;
    std::vector<int> plain_levels, stomp_levels;
    Stage snapped, stomped;
    int plain = hops_to_snap(false,plain_log,plain_levels,snapped);
    int stomp = hops_to_snap(true,stomp_log,stomp_levels,stomped);
    snprintf(d,sizeof(d),"plain hops snap it on %i (%s); stomps on %i (%s)",plain,plain_log.c_str(),stomp,stomp_log.c_str());
    Check(plain >= 3 && plain <= 5,"plain hops in the middle snap it on the 3rd to 5th",d);
    Check(stomp > 0 && stomp < plain,"stomping snaps it in fewer",d);
    bool f_order = plain_levels.size() == 3 && plain_levels[0] == BRIDGE_LEVEL_STRAINED &&
                   plain_levels[1] == BRIDGE_LEVEL_CRACKING && plain_levels[2] == BRIDGE_LEVEL_SNAPPED;
    snprintf(d,sizeof(d),"%i warnings",(int)plain_levels.size());
    Check(f_order,"strained, then cracking, then snapped - each once",d);

    //Snapped: she drops to the ground or the ledge under it, the halves hang apart and settle.
    Run(snapped,360,ArcherInput());
    const StageBridge& sb = snapped.bridges[bi];
    int cut = -1;
    for (int k = 0; k < (int)sb.broken.size(); k++){
        cut = sb.broken[k] ? k : cut;
    }
    float gap = (cut >= 0) ? sqrtf((sb.p[cut + 1].x - sb.p[cut].x) * (sb.p[cut + 1].x - sb.p[cut].x) +
                                   (sb.p[cut + 1].y - sb.p[cut].y) * (sb.p[cut + 1].y - sb.p[cut].y)) : 0.0f;
    snprintf(d,sizeof(d),"plank %i snapped, its ends %.2f apart; she is at (%.2f, feet %.2f), on ground %i; lowest %.2f, fastest %.3f; ends (%.2f,%.2f) (%.2f,%.2f)",
             cut,gap,snapped.pos.x,snapped.pos.y - ARCHER_HALF_H,snapped.f_on_ground ? 1 : 0,sb.Lowest(),BridgeSpeed(sb),
             sb.p.front().x,sb.p.front().y,sb.p.back().x,sb.p.back().y);
    Check(cut >= 0 && gap > sb.link * 2.0f,"one plank snapped, and its two ends have parted",d);
    Check(snapped.f_on_ground && snapped.pos.y - ARCHER_HALF_H < 2.7f && snapped.bridge_on < 0,"she fell with it, to the ledge or the ground under it",d);
    Check(sb.p.front().x == sb.a.x && sb.p.front().y == sb.a.y && sb.p.back().x == sb.b.x && sb.p.back().y == sb.b.y &&
          sb.Lowest() < sb.a.y - 1.5f && BridgeSpeed(sb) < 0.05f,"the halves hang from their anchors, and have settled",d);
    //Gone stays gone - hops on what is left do nothing - until a restart.
    snapped.Reset();
    const StageBridge& rb = snapped.bridges[bi];
    bool f_whole = rb.level == BRIDGE_SOUND && rb.MaxStrain() == 0.0f;
    for (uint8_t b : rb.broken){
        f_whole = f_whole && !b;
    }
    Check(f_whole,"a restart brings it back whole and sound");

    //The same hops twice give the same bridge, bit for bit.
    std::string again_log;
    std::vector<int> again_levels;
    Stage again;
    hops_to_snap(false,again_log,again_levels,again);
    Stage first;
    std::string first_log;
    std::vector<int> first_levels;
    hops_to_snap(false,first_log,first_levels,first);
    bool f_same = true;
    for (size_t j = 0; j < first.bridges[bi].p.size(); j++){
        f_same = f_same && first.bridges[bi].p[j].x == again.bridges[bi].p[j].x &&
                 first.bridges[bi].p[j].y == again.bridges[bi].p[j].y;
    }
    for (size_t k = 0; k < first.bridges[bi].strain.size(); k++){
        f_same = f_same && first.bridges[bi].strain[k] == again.bridges[bi].strain[k];
    }
    Check(f_same,"two runs of the same hops give the same bridge");

    //The first bridge takes any number of hops and never strains.
    Stage one = s;
    const StageBridge& b0 = one.bridges[0];
    float x0 = (b0.a.x + b0.b.x) * 0.5f;
    DropOnto(one,x0,b0.SurfaceY(x0) + 0.3f);
    Run(one,60,ArcherInput());
    int landed = 0;
    for (int n = 0; n < 8; n++){
        BridgeHop hop = HopOnBridge(one,true);
        landed += hop.f_landed ? 1 : 0;
    }
    snprintf(d,sizeof(d),"%i landings, strain %.3f, level %i",landed,one.bridges[0].MaxStrain(),one.bridges[0].level);
    Check(landed == 8 && one.bridges[0].MaxStrain() == 0.0f && one.bridges[0].level == BRIDGE_SOUND,
          "eight stomps on the first bridge and it is still sound",d);
}

/*
    ROUTE CHECKS (bridge_crumble_plan.md section 6): each designed way through the level, played
    against the rules by RouteCheck. Passable, with every timed leg leaving a player at least
    ROUTE_MIN_WINDOW ticks to be early or late in - and the solved keys, replayed from the start,
    arrive where the solve said. That last one is what makes a route writable as a recording.
*/
#define ROUTE_MIN_WINDOW    4

static void CheckRoute(const char* name, const Stage& start, const std::vector<RouteLeg>& legs,
                       const std::function<bool(const Stage&)>& arrived){
    char d[320];
    RouteResult r = SolveRoute(start,legs);
    if (!r.f_passable){
        snprintf(d,sizeof(d),"stuck at '%s'",r.failed_leg.c_str());
        Check(false,name,d);
        return;
    }
    std::string legs_text;
    for (const RouteLegResult& l : r.legs){
        char one[96];
        snprintf(one,sizeof(one),"%s%s %i%s",legs_text.empty() ? "" : ", ",l.name.c_str(),l.window,
                 l.f_timed ? "" : "*");
        legs_text += one;
    }
    snprintf(d,sizeof(d),"%.1f s; windows %s (* not timed)",r.timeline.size() / (float)ARCHER_TPS,legs_text.c_str());
    Check(r.narrowest_window >= ROUTE_MIN_WINDOW,name,d);
    Stage end = PlayRoute(start,r.timeline);
    Check(arrived(end),"   and its keys, played from the start, arrive");
}

static void TestRoutes(){
    printf("\nroute checks\n");
    Stage probe;
    const int pad = FindSpringPlant(probe,SPRING_PAD);
    const int leaf = FindSpringPlant(probe,SPRING_LEAF);
    if (pad < 0 || leaf < 0){
        Check(false,"the main level has the pad and the leaf");
        return;
    }
    auto on_top = [](float top, float x0, float x1){
        return [=](const Stage& s){
            return s.f_on_ground && fabsf(s.pos.y - ARCHER_HALF_H - top) < 0.02f && s.pos.x > x0 && s.pos.x < x1;
        };
    };
    auto on_plant = [](int plant){
        return [=](const Stage& s){ return s.f_on_ground && s.spring_on == plant; };
    };

    //--- Pad to canopy: onto the pad, one pumping bounce, the shelf, the leaf, the canopy ---
    //The start the 2026-09-27 recording (archer_pad_leaf_route.rec) uses: standing at x 100.
    Stage start;
    DropOnto(start,100.0f,0.3f);
    Run(start,30,ArcherInput());
    std::vector<RouteLeg> legs;
    {
        RouteLeg l;
        l.name = "onto the pad";
        l.goal = on_plant(pad);
        l.wait_max = 20;
        l.air_max = 40;
        l.air_step = 2;
        legs.push_back(l);
    }
    {
        //Straight up and down again, kept for how deep it sinks the cap - the next bounce's power.
        RouteLeg l;
        l.name = "pump";
        l.goal = on_plant(pad);
        l.score = [pad](const Stage& landed){
            Stage c = landed;
            float lowest = 0.0f;
            for (int i = 0; i < 30; i++){
                StageEvents e;
                c.Tick(ArcherInput(),e);
                lowest = fminf(lowest,c.spring_plants[pad].q);
            }
            return -lowest;
        };
        l.air_max = 0;
        l.f_try_stomp = true;
        legs.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "shelf";
        l.goal = on_top(7.5f,110.0f,118.0f);
        legs.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "leaf";
        l.goal = on_plant(leaf);
        l.walk = 1;
        l.wait_max = 90;
        l.air_max = 40;
        l.air_step = 2;
        legs.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "canopy";
        l.goal = on_top(13.0f,125.0f,133.0f);
        l.wait_max = 50;
        l.air_min = 10;
        l.air_step = 5;
        legs.push_back(l);
    }
    CheckRoute("pad to canopy is passable",start,legs,on_top(13.0f,125.0f,133.0f));

    //--- The stepping stones: hop by hop across the pit, never standing on one too long ---
    std::vector<int> stones = CrumbleStones(probe);
    if (stones.size() < 4){
        Check(false,"the stepping stones' pit has its stones");
        return;
    }
    Stage rim;
    DropOnto(rim,193.0f,0.3f);
    Run(rim,30,ArcherInput());
    const float far_rim = 214.0f;
    std::vector<RouteLeg> hops;
    for (size_t k = 0; k < stones.size(); k++){
        const StageBlock blk = probe.blocks[stones[k]];
        RouteLeg l;
        l.name = "stone " + std::to_string(k + 1);
        l.goal = on_top(blk.Top(),blk.Left() - ARCHER_HALF_W,blk.Right() + ARCHER_HALF_W);
        l.walk = 1;
        l.wait_max = 30;
        l.air_max = 40;
        l.air_step = 2;
        hops.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "far rim";
        l.goal = on_top(0.0f,far_rim,264.0f);
        l.walk = 1;
        l.wait_max = 30;
        l.air_max = 40;
        l.air_step = 2;
        hops.push_back(l);
    }
    CheckRoute("the stepping stones are crossable",rim,hops,on_top(0.0f,far_rim,264.0f));

    //--- The detour: every stone gone, down into the pit and up its far wall by the ledge ---
    Stage bare = rim;
    for (int i : stones){
        bare.blocks[i].f_alive = false;
    }
    std::vector<RouteLeg> detour;
    {
        RouteLeg l;
        l.name = "into the pit";
        l.goal = on_top(-4.0f,196.0f,far_rim);
        l.walk = 1;
        l.wait_max = 20;
        l.air_max = 40;
        l.air_step = 4;
        l.f_timed = false;          //a fall, not a timing
        detour.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "catch and climb";
        l.goal = on_top(0.0f,far_rim,264.0f);
        l.walk = 1;
        l.wait_max = 90;
        l.air_max = 60;
        l.air_step = 4;
        l.f_climb = true;
        detour.push_back(l);
    }
    CheckRoute("with every stone gone, the pit's far ledge is the way on",bare,detour,on_top(0.0f,far_rim,264.0f));

    //--- The chase: from the rim, over the hole in the floor, onto the ground past it ---
    //The trigger starts the floor falling as she steps on; the waits that make it are the ones
    //the front has not caught.
    const float chase_end = 252.0f;
    Stage chase_rim;
    DropOnto(chase_rim,216.0f,0.3f);
    Run(chase_rim,30,ArcherInput());
    std::vector<RouteLeg> chase;
    {
        RouteLeg l;
        l.name = "over the hole";
        l.goal = on_top(0.0f,238.0f,chase_end);
        l.walk = 1;
        l.wait_max = 150;
        l.air_max = 40;
        l.air_step = 2;
        chase.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "onto the ground";
        l.goal = on_top(0.0f,chase_end + ARCHER_HALF_W,264.0f);
        l.walk = 1;
        l.wait_max = 90;
        l.air_max = 40;
        l.air_step = 2;
        chase.push_back(l);
    }
    CheckRoute("the chase is outrun",chase_rim,chase,on_top(0.0f,chase_end + ARCHER_HALF_W,264.0f));

    //--- Its detour: the floor gone, down into the pit, along it and up the far wall ---
    Stage fallen = chase_rim;
    for (const StageCrumbleGroup& g : fallen.crumble_groups){
        for (int i : g.blocks){
            fallen.blocks[i].f_alive = false;
        }
    }
    std::vector<RouteLeg> under;
    {
        RouteLeg l;
        l.name = "into the pit";
        l.goal = on_top(-4.0f,220.0f,chase_end);
        l.walk = 1;
        l.wait_max = 20;
        l.air_max = 40;
        l.air_step = 4;
        l.f_timed = false;
        under.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "catch and climb";
        l.goal = on_top(0.0f,chase_end,264.0f);
        l.walk = 1;
        l.wait_max = 240;
        l.air_max = 60;
        l.air_step = 4;
        l.f_climb = true;
        under.push_back(l);
    }
    CheckRoute("with the floor gone, the chase pit's far ledge is the way on",fallen,under,on_top(0.0f,chase_end,264.0f));

    //--- Up to the bridge and over it: the step, slab one, slab two, then run it to slab three ---
    Stage step;
    DropOnto(step,6.0f,2.1f);
    Run(step,30,ArcherInput());
    std::vector<RouteLeg> up;
    {
        RouteLeg l;
        l.name = "slab one";
        l.goal = on_top(4.4f,10.5f - ARCHER_HALF_W,12.5f + ARCHER_HALF_W);
        l.walk = 1;
        l.wait_max = 40;
        l.air_max = 40;
        l.air_step = 2;
        up.push_back(l);
    }
    {
        RouteLeg l;
        l.name = "slab two";
        l.goal = on_top(7.0f,14.5f - ARCHER_HALF_W,16.5f + ARCHER_HALF_W);
        l.walk = 1;
        l.wait_max = 30;
        l.air_max = 40;
        l.air_step = 2;
        up.push_back(l);
    }
    {
        //Run along it and hop the last of it onto slab three.
        RouteLeg l;
        l.name = "across";
        l.goal = on_top(7.0f,24.0f + ARCHER_HALF_W,26.0f);
        l.walk = 1;
        l.wait_max = 90;
        l.air_max = 30;
        l.air_step = 2;
        up.push_back(l);
    }
    CheckRoute("up from the step and across the bridge",step,up,on_top(7.0f,24.0f + ARCHER_HALF_W,26.0f));
}

/*
    The vitals - vitals_plan.md. On a layout of their own rather than a level's, so every height is
    the test's: a floor with its top at 0 ending at a pit 15 deep, a step down of 2 at its far end,
    a low ledge standing on that step and a pillar of the same height standing in the pit.
*/
static void VitalsLayout(Stage& s){
    s.Reset();
    s.blocks.clear();
    s.props.clear();
    s.signs.clear();
    s.scenery.clear();
    s.trees.clear();
    s.spring_plants.clear();
    s.branches.clear();
    s.ramps.clear();
    s.zones.clear();
    s.zone_inside.clear();
    s.ClearObstacles();
    s.ClearRopePoints();
    auto add = [&](float left, float right, float bottom, float top, int kind){
        StageBlock b;
        b.x = (left + right) * 0.5f;
        b.y = (bottom + top) * 0.5f;
        b.hw = (right - left) * 0.5f;
        b.hh = (top - bottom) * 0.5f;
        b.kind = kind;
        s.blocks.push_back(b);
    };
    add(-120.0f,-60.0f,-3.0f,-2.0f,BLOCK_SOLID);    //the step, 2 below the floor
    add(-60.0f,0.0f,-1.0f,0.0f,BLOCK_SOLID);        //the floor, ending at the pit
    add(0.0f,40.0f,-16.0f,-15.0f,BLOCK_SOLID);      //the pit's floor
    add(-100.0f,-98.0f,-2.0f,2.2f,BLOCK_LEDGE);     //a low ledge on the step: its lip 4.2 up
    add(10.0f,12.0f,-15.0f,2.2f,BLOCK_LEDGE);       //the same lip, over 17 units of pit
}

static void VitalsStandAt(Stage& s, float x, float floor_top){
    s.pos = v2(x,floor_top + ARCHER_HALF_H + 0.01f);
    s.vel = v2(0.0f,0.0f);
    s.mode = MODE_AIR;
    s.f_on_ground = false;
    Settle(s);
    s.vitals = StageVitals();
}

//Drops her against a lip's left face, holding into it, until she catches it. Returns whether she did.
static bool VitalsHangFrom(Stage& s, float face_x, float lip){
    //A little above where she would hang, against the face: falling, her hands cross the lip.
    s.pos = v2(face_x - ARCHER_HALF_W - 0.05f,lip - LEDGE_HANG_DROP - ARCHER_HALF_H + 0.4f);
    s.vel = v2(0.0f,0.0f);
    s.mode = MODE_AIR;
    s.f_on_ground = false;
    s.facing = 1.0f;
    ArcherInput into;
    into.move_axis = 1.0f;
    for (int i = 0; i < 90 && s.mode != MODE_HANG; i++){
        StageEvents ev;
        s.Tick(into,ev);
    }
    return s.mode == MODE_HANG;
}

static void TestVitals(){
    printf("vitals\n");
    char detail[200];
    ArcherInput idle;
    Stage s;
    VitalsLayout(s);

    //--- Fear ---
    VitalsStandAt(s,-30.0f,0.0f);
    Run(s,120,idle);
    snprintf(detail,sizeof(detail),"fear %.3f, target %.3f",s.vitals.fear,s.vitals.fear_target);
    Check(s.f_on_ground && s.vitals.fear_target == 0.0f && s.vitals.fear == 0.0f,
          "the middle of a floor is no fear, however far the pit is",detail);

    VitalsStandAt(s,-59.8f,0.0f);
    Run(s,120,idle);
    snprintf(detail,sizeof(detail),"at x %.2f, fear target %.3f",s.pos.x,s.vitals.fear_target);
    Check(s.f_on_ground && s.vitals.fear_target == 0.0f,"nor is the lip of a step down she could jump back up",detail);

    VitalsStandAt(s,-0.2f,0.0f);
    Run(s,60,idle);
    snprintf(detail,sizeof(detail),"at x %.2f, fear %.3f, target %.3f",s.pos.x,s.vitals.fear,s.vitals.fear_target);
    Check(s.f_on_ground && s.vitals.fear_target > 0.25f && s.vitals.fear_target <= VITALS_EDGE_SHARE,
          "the lip of the pit is, up to VITALS_EDGE_SHARE of the drop's",detail);
    Check(s.vitals.fear > 0.9f * s.vitals.fear_target,"and she gets there within a second",detail);
    float lip_fear = s.vitals.fear_target;
    VitalsStandAt(s,-0.7f,0.0f);
    Run(s,10,idle);
    snprintf(detail,sizeof(detail),"target %.3f at x %.2f, %.3f at the lip",s.vitals.fear_target,s.pos.x,lip_fear);
    Check(s.vitals.fear_target < lip_fear,"a step back from it is less",detail);

    Stage low = s, high = s;
    bool f_low = VitalsHangFrom(low,-100.0f,2.2f);
    bool f_high = VitalsHangFrom(high,10.0f,2.2f);
    snprintf(detail,sizeof(detail),"low: mode %i at (%.2f,%.2f); high: mode %i at (%.2f,%.2f)",
             low.mode,low.pos.x,low.pos.y,high.mode,high.pos.x,high.pos.y);
    Check(f_low && f_high,"she catches both lips",detail);
    Run(low,120,idle);
    Run(high,120,idle);
    snprintf(detail,sizeof(detail),"over the pit %.3f, over the step %.3f",high.vitals.fear,low.vitals.fear);
    Check(high.vitals.fear > 0.8f && low.vitals.fear < 0.1f,
          "hanging over the pit is fear; hanging the same lip over a floor is not",detail);

    //The tail: out of danger, the fear lingers, and is gone in the end.
    float hung = high.vitals.fear;
    VitalsStandAt(high,-30.0f,0.0f);
    high.vitals.fear = hung;
    Run(high,60,idle);
    float after_1s = high.vitals.fear;
    Run(high,540,idle);
    snprintf(detail,sizeof(detail),"%.3f hanging, %.3f a second later, %.3f ten seconds later",hung,after_1s,high.vitals.fear);
    Check(after_1s > 0.6f * hung && high.vitals.fear < 0.2f * hung,
          "fear lingers a second after the danger, and fades",detail);

    //Falling: the landing it is heading for, before it lands.
    Stage fall = s;
    fall.pos = v2(20.0f,2.0f);
    fall.vel = v2(0.0f,0.0f);
    fall.mode = MODE_AIR;
    fall.f_on_ground = false;
    fall.vitals = StageVitals();
    float fear_before_landing = 0.0f;
    bool  f_hard = false;
    for (int i = 0; i < 120 && !fall.f_on_ground; i++){
        StageEvents ev;
        fall.Tick(idle,ev);
        if (!fall.f_on_ground){
            fear_before_landing = fall.vitals.fear;
        }
        f_hard = f_hard || (ev.f_landed && ev.land_speed >= VITALS_HARD_LANDING);
    }
    snprintf(detail,sizeof(detail),"fear %.3f in the air, %.3f landed",fear_before_landing,fall.vitals.fear);
    Check(fear_before_landing > 0.5f,"a long fall is frightening before it lands",detail);
    Check(f_hard && fall.vitals.fear > fear_before_landing,"and the hard landing adds to it",detail);

    Stage hop = s;
    VitalsStandAt(hop,-30.0f,0.0f);
    ArcherInput jump;
    jump.f_jump_down = jump.f_jump_pressed = true;
    ArcherInput held;
    held.f_jump_down = true;
    StageEvents jev;
    hop.Tick(jump,jev);
    float hop_fear = 0.0f;
    for (int i = 0; i < 120 && !hop.f_on_ground; i++){
        StageEvents ev;
        hop.Tick(held,ev);
        hop_fear = fmaxf(hop_fear,hop.vitals.fear_target);
    }
    snprintf(detail,sizeof(detail),"the most fear it aimed at: %.3f",hop_fear);
    Check(jev.f_jumped && hop_fear == 0.0f,"a jump on flat ground is none",detail);

    //--- Exertion ---
    Stage run = s;
    VitalsStandAt(run,-58.0f,0.0f);
    ArcherInput right;
    right.move_axis = 1.0f;
    Run(run,150,right);
    float ran = run.vitals.exertion;
    float bpm_ran = run.vitals.heart_rate;
    snprintf(detail,sizeof(detail),"exertion %.3f, %.0f bpm after 2.5 s of sprint",ran,bpm_ran);
    Check(ran > 0.3f,"a sprint winds her",detail);
    Check(bpm_ran > VITALS_REST_BPM + 10.0f,"and raises her heart rate",detail);

    Stage stand = run, walk = run;
    ArcherInput stroll;
    stroll.move_axis = 0.3f;
    Run(stand,180,idle);
    Run(walk,180,stroll);
    snprintf(detail,sizeof(detail),"from %.3f: %.3f standing, %.3f walking",ran,stand.vitals.exertion,walk.vitals.exertion);
    Check(stand.vitals.exertion < ran && walk.vitals.exertion > stand.vitals.exertion,
          "standing gets her breath back; walking barely does",detail);
    Check(stand.vitals.exertion > 0.5f * ran,"and not all at once",detail);
    Run(stand,1200,idle);
    snprintf(detail,sizeof(detail),"exertion %.3f, %.1f bpm",stand.vitals.exertion,stand.vitals.heart_rate);
    Check(stand.vitals.exertion < 0.05f && stand.vitals.heart_rate < VITALS_REST_BPM + 5.0f,
          "twenty seconds' rest and she is rested, heart and all",detail);

    Stage jumps = s;
    VitalsStandAt(jumps,-30.0f,0.0f);
    for (int n = 0; n < 3; n++){
        StageEvents ev;
        jumps.Tick(jump,ev);
        for (int i = 0; i < 120 && !jumps.f_on_ground; i++){
            StageEvents e;
            jumps.Tick(held,e);
        }
        Run(jumps,2,idle);
    }
    snprintf(detail,sizeof(detail),"exertion %.3f",jumps.vitals.exertion);
    Check(jumps.vitals.exertion > 2.0f * VITALS_JUMP_EFFORT,"three jumps in a row tell",detail);

    //Reset clears them, and the level they are for never sees any of this layout's numbers.
    s.vitals.exertion = s.vitals.fear = 0.7f;
    s.Reset();
    Check(s.vitals.exertion == 0.0f && s.vitals.fear == 0.0f && s.vitals.heart_rate == VITALS_REST_BPM,
          "a restart puts her body back at rest");
}

/*
    The arrow kinds - vine_plan.md section 8. The rules only CARRY a kind, so that is what is
    checked: the pick and the step set it, a kind that does not exist is ignored, Loose puts it on
    the arrow, the hit hands it back, a restart leaves it, and the state hash sees both copies of
    it - a kind a replay could lose without the trace noticing would defeat the point of making it
    rules state.
*/
static uint64_t HashOf(const Stage& s){
    StateHash h;
    s.HashState(h);
    return h.Total();
}

static void TestArrowKinds(){
    printf("\narrow kinds\n");
    Stage s;
    Settle(s);
    Check(s.arrow_kind == ARROW_NORMAL,"she starts with normal arrows");

    ArcherInput pick;
    pick.arrow_select = ARROW_VINE;
    StageEvents e1;
    s.Tick(pick,e1);
    Check(s.arrow_kind == ARROW_VINE && e1.f_arrow_kind_changed,"a pick selects that kind, and says so");
    StageEvents e2;
    s.Tick(pick,e2);
    Check(!e2.f_arrow_kind_changed,"picking the kind already chosen is no change");
    ArcherInput nothing;
    nothing.arrow_select = ARROW_KIND_COUNT + 2;
    Run(s,1,nothing);
    Check(s.arrow_kind == ARROW_VINE,"a key for a kind that does not exist yet is ignored, not clamped");

    ArcherInput next;
    next.arrow_step = 1;
    Run(s,1,next);
    Check(s.arrow_kind == (ARROW_VINE + 1) % ARROW_KIND_COUNT,"a step forward goes round to the next");
    ArcherInput back;
    back.arrow_step = -1;
    Stage w;
    Settle(w);
    Run(w,1,back);
    Check(w.arrow_kind == ARROW_KIND_COUNT - 1,"a step back from the first wraps to the last");
    ArcherInput both;
    both.arrow_select = ARROW_NORMAL;
    both.arrow_step = 1;
    Run(w,1,both);
    Check(w.arrow_kind == ARROW_NORMAL,"a pick wins over a step in the same tick");

    //Picked WHILE NOCKED: the arrow on the string goes as the new kind.
    Stage n;
    Settle(n);
    ArcherInput hold;
    hold.f_draw_down = true;
    Run(n,BOW_NOCK_TICKS + 4,hold);
    ArcherInput switch_kind = hold;
    switch_kind.arrow_select = ARROW_VINE;
    Run(n,1,switch_kind);
    ArcherInput letgo;
    letgo.f_draw_released = true;
    StageEvents shot;
    n.Tick(letgo,shot);
    int idx = -1;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (n.arrows[i].f_live){ idx = i; }
    }
    Check(shot.f_shot && shot.shot_kind == ARROW_VINE,"a kind picked while nocked is the kind loosed");
    Check(idx >= 0 && n.arrows[idx].kind == ARROW_VINE,"and the arrow carries it");

    //Into the ground, and the hit hands the kind back.
    Stage g;
    Settle(g);
    g.arrow_kind = ARROW_VINE;
    g.aim_deg = BOW_AIM_MIN_DEG;
    Run(g,BOW_DRAW_TICKS,hold);
    StageEvents loosed;
    g.Tick(letgo,loosed);
    //Aimed at her feet it can strike on the tick it leaves (Loose, then TickArrows).
    int hit_kind = loosed.arrow_hits.empty() ? -1 : loosed.arrow_hits[0].kind;
    ArcherInput idle;
    for (int t = 0; t < 120 && hit_kind < 0; t++){
        StageEvents e;
        g.Tick(idle,e);
        if (!e.arrow_hits.empty()){
            hit_kind = e.arrow_hits[0].kind;
        }
    }
    Check(loosed.f_shot && hit_kind == ARROW_VINE,"the hit reports the kind of arrow that struck");

    //The get-up takes every order but this one.
    Stage u;
    u.StartGetUp();
    Run(u,1,pick);
    Check(u.mode == MODE_GETUP && u.arrow_kind == ARROW_VINE,"a pick during the get-up still counts");

    //A restart leaves her choice.
    Stage r;
    r.arrow_kind = ARROW_VINE;
    r.Reset();
    Check(r.arrow_kind == ARROW_VINE,"a restart leaves the kind she chose");

    //Both copies are in the trace.
    Stage h1;
    Stage h2;
    Settle(h1);
    Settle(h2);
    Check(HashOf(h1) == HashOf(h2),"two identical stages hash alike");
    h2.arrow_kind = ARROW_VINE;
    Check(HashOf(h1) != HashOf(h2),"the selected kind is in the state hash");
    h2.arrow_kind = ARROW_NORMAL;
    h2.arrows[3].kind = ARROW_VINE;
    Check(HashOf(h1) != HashOf(h2),"and so is each arrow's");

    Check(strcmp(ArrowKindName(ARROW_NORMAL),"arrow") == 0 && strcmp(ArrowKindName(ARROW_VINE),"vine") == 0 &&
          strcmp(ArrowKindName(-1),"?") == 0,"every kind has a name, and out of range is '?'");
}

/*
    The aim moves only with the bow drawn, is kept standing still, and goes back to neutral after
    BOW_AIM_RETURN_TICKS of moving - see the note at the constant. Against the constants: the
    running is back and forth on the floor by the start, fast enough to count as moving.
*/
static void RunAbout(Stage& s, int ticks){
    for (int t = 0; t < ticks; t++){
        ArcherInput run;
        run.move_axis = ((t / 30) % 2) ? -1.0f : 1.0f;
        StageEvents e;
        s.Tick(run,e);
    }
}

static void TestAimHold(){
    printf("\nthe aim, undrawn\n");
    char d[160];
    Stage s;
    Settle(s);
    ArcherInput tilt;
    tilt.aim_axis = -1.0f;
    Run(s,60,tilt);
    snprintf(d,sizeof(d),"aim %.2f",s.aim_deg);
    CheckNear(s.aim_deg,BOW_AIM_NEUTRAL_DEG,0.0001f,"Down with the bow away leaves the aim alone",d);

    ArcherInput draw_tilt;
    draw_tilt.f_draw_down = true;
    draw_tilt.aim_axis = -1.0f;
    Run(s,30,draw_tilt);
    //The draw's first tick does not tilt: it reads last tick's bow.
    float want = BOW_AIM_NEUTRAL_DEG - BOW_AIM_RATE_DEG * ARCHER_DT * 29.0f;
    CheckNear(s.aim_deg,want,0.01f,"drawn, it tilts at BOW_AIM_RATE_DEG");
    float aimed = s.aim_deg;
    ArcherInput letgo;
    letgo.f_draw_released = true;
    Run(s,1,letgo);
    ArcherInput idle;
    Run(s,600,idle);
    CheckNear(s.aim_deg,aimed,0.0001f,"loosed and standing still ten seconds, the aim is kept for the next shot");

    int back_at = BOW_AIM_RETURN_TICKS / 2;
    RunAbout(s,back_at);
    snprintf(d,sizeof(d),"aim %.2f after %i ticks of running, roam %i",s.aim_deg,back_at,s.aim_roam_ticks);
    CheckNear(s.aim_deg,aimed,0.0001f,"a second of running about keeps it too",d);
    Check(s.aim_roam_ticks > back_at / 2,"...while counting the running",d);

    //Long enough to reach the count and then turn all the way back.
    int to_neutral = (int)ceilf(fabsf(BOW_AIM_NEUTRAL_DEG - aimed) / (BOW_AIM_RETURN_RATE_DEG * ARCHER_DT));
    RunAbout(s,BOW_AIM_RETURN_TICKS + to_neutral + 30);
    snprintf(d,sizeof(d),"aim %.2f, roam %i",s.aim_deg,s.aim_roam_ticks);
    CheckNear(s.aim_deg,BOW_AIM_NEUTRAL_DEG,0.0001f,"after a couple of seconds more, it is back at neutral",d);

    //A draw clears the count.
    Stage r;
    Settle(r);
    RunAbout(r,BOW_AIM_RETURN_TICKS - 10);
    int roamed = r.aim_roam_ticks;
    ArcherInput draw;
    draw.f_draw_down = true;
    Run(r,2,draw);
    snprintf(d,sizeof(d),"roam %i before, %i after",roamed,r.aim_roam_ticks);
    Check(roamed > 0 && r.aim_roam_ticks == 0,"a draw starts the count again",d);

    //Started back, it finishes even if she stops.
    Stage b;
    Settle(b);
    b.aim_deg = -60.0f;
    b.aim_roam_ticks = BOW_AIM_RETURN_TICKS;
    Run(b,(int)ceilf(80.0f / (BOW_AIM_RETURN_RATE_DEG * ARCHER_DT)) + 2,idle);
    CheckNear(b.aim_deg,BOW_AIM_NEUTRAL_DEG,0.0001f,"once on its way back, standing still does not stop it");

    //A restart puts both back, and the count is in the trace.
    b.aim_roam_ticks = 50;
    b.Reset();
    Check(b.aim_deg == BOW_AIM_NEUTRAL_DEG && b.aim_roam_ticks == 0,"a restart starts at neutral with no count");
    Stage h1;
    Stage h2;
    Settle(h1);
    Settle(h2);
    h2.aim_roam_ticks = 7;
    Check(HashOf(h1) != HashOf(h2),"the count is in the state hash");
}

/*
    The floors' edges - vine_plan.md section 15, StageEdges.cpp. Each rule against a layout built
    for it, then the main level's own landmarks, then the refresh: a block that changes changes the
    edges on the next tick, and a rebuild equals a fresh build of the same blocks.
*/
static int FindEdge(const Stage& s, float x, float y, int side){
    for (size_t i = 0; i < s.edges.size(); i++){
        const StageEdge& e = s.edges[i];
        if (fabsf(e.x - x) < 0.01f && fabsf(e.y - y) < 0.01f && e.side == side){ return (int)i; }
    }
    return -1;
}
static int FindCorner(const Stage& s, float x, float y, int side){
    for (size_t i = 0; i < s.corners.size(); i++){
        const StageCorner& c = s.corners[i];
        if (fabsf(c.x - x) < 0.01f && fabsf(c.y - y) < 0.01f && c.side == side){ return (int)i; }
    }
    return -1;
}
//Blocks as (left, right, bottom, top) - easier to read in a test than centres and halves.
static StageBlock Box(float l, float r, float b, float t, int kind = BLOCK_SOLID){
    StageBlock k;
    k.x = 0.5f * (l + r);
    k.y = 0.5f * (b + t);
    k.hw = 0.5f * (r - l);
    k.hh = 0.5f * (t - b);
    k.kind = kind;
    return k;
}
static bool SameEdges(const Stage& a, const Stage& b){
    if (a.spans.size() != b.spans.size() || a.edges.size() != b.edges.size() ||
        a.corners.size() != b.corners.size()){
        return false;
    }
    for (size_t i = 0; i < a.edges.size(); i++){
        const StageEdge& p = a.edges[i];
        const StageEdge& q = b.edges[i];
        if (p.x != q.x || p.y != q.y || p.side != q.side || p.drop != q.drop || p.wall != q.wall ||
            p.block != q.block || p.f_grabbable != q.f_grabbable){
            return false;
        }
    }
    for (size_t i = 0; i < a.corners.size(); i++){
        const StageCorner& p = a.corners[i];
        const StageCorner& q = b.corners[i];
        if (p.x != q.x || p.y != q.y || p.side != q.side || p.rise != q.rise || p.block != q.block){
            return false;
        }
    }
    return true;
}

static void TestEdges(){
    printf("\nthe floors' edges\n");
    char d[200];
    Stage s;

    //Two boxes side by side at one height are one floor: no edge where they meet.
    s.blocks = { Box(0,5,-2,0), Box(5,9,-2,0) };
    s.RebuildEdges();
    snprintf(d,sizeof(d),"%zu spans, %zu edges",s.spans.size(),s.edges.size());
    Check(s.spans.size() == 1 && s.edges.size() == 2 && FindEdge(s,0,0,-1) >= 0 && FindEdge(s,9,0,1) >= 0,
          "two boxes abutting at one height are one floor, with an edge at each end only",d);
    Check(s.edges[0].drop == VITALS_NO_FLOOR && fabsf(s.edges[0].wall - 2.0f) < 0.001f,
          "nothing below: VITALS_NO_FLOOR, and the whole face bare");

    //A box on a box: the lower top is split around it, and each piece ends at a wall's foot.
    s.blocks = { Box(0,10,-2,0), Box(4,6,0,1.5f) };
    s.RebuildEdges();
    int cl = FindCorner(s,4,0,1), cr = FindCorner(s,6,0,-1);
    snprintf(d,sizeof(d),"%zu spans, %zu corners",s.spans.size(),s.corners.size());
    Check(s.spans.size() == 3 && cl >= 0 && cr >= 0,"a box standing on a floor splits it, with a wall's foot either side",d);
    Check(cl >= 0 && fabsf(s.corners[cl].rise - 1.5f) < 0.001f && s.corners[cl].block == 1,
          "the foot knows the wall's block and how far it rises");
    int top_l = FindEdge(s,4,1.5f,-1);
    Check(top_l >= 0 && fabsf(s.edges[top_l].drop - 1.5f) < 0.001f && fabsf(s.edges[top_l].wall - 1.5f) < 0.001f,
          "and its own top's edges drop to the floor it stands on");

    //A step down: the drop is the step, and so is the bare face.
    s.blocks = { Box(0,5,-2,0), Box(5,9,-2,-0.6f) };
    s.RebuildEdges();
    int step = FindEdge(s,5,0,1);
    Check(step >= 0 && fabsf(s.edges[step].drop - 0.6f) < 0.001f && fabsf(s.edges[step].wall - 0.6f) < 0.001f,
          "a step down drops, and bares, the step's height");
    Check(FindCorner(s,5,-0.6f,-1) >= 0 && s.corners.size() == 1,"and the lower floor ends at its foot");

    //A one-way platform over the end of a floor is not a wall: she walks up through it.
    s.blocks = { Box(0,5,-2,0), Box(4,8,1.0f,1.2f,BLOCK_PLATFORM) };
    s.RebuildEdges();
    Check(s.corners.empty() && FindEdge(s,5,0,1) >= 0 && FindEdge(s,8,1.2f,1) >= 0,
          "a platform is a floor with edges, never a wall");
    int plat = FindEdge(s,4,1.2f,-1);
    Check(plat >= 0 && fabsf(s.edges[plat].drop - 1.2f) < 0.001f && fabsf(s.edges[plat].wall - 0.2f) < 0.001f,
          "and its face is its own thickness");

    //A ledge's corners are the only grabbable ones; a gap narrower than a join is no gap.
    s.blocks = { Box(0,5,-2,0), Box(5.01f,9,-2,0), Box(12,14,0,3,BLOCK_LEDGE) };
    s.RebuildEdges();
    Check(FindEdge(s,5,0,1) < 0 && FindEdge(s,5.01f,0,-1) < 0,"a gap narrower than STAGE_EDGE_JOIN is one floor");
    int ledge = FindEdge(s,14,3,1);
    Check(ledge >= 0 && s.edges[ledge].f_grabbable && FindEdge(s,0,0,-1) >= 0 && !s.edges[FindEdge(s,0,0,-1)].f_grabbable,
          "a ledge's corners are grabbable, a solid block's are not");

    //Queries.
    s.blocks = { Box(0,5,-2,0), Box(8,12,-2,0) };
    s.RebuildEdges();
    int near_r = s.NearestEdge(4.4f,0.0f,1.0f);
    Check(near_r >= 0 && s.edges[near_r].x == 5.0f,"NearestEdge finds the lip she is walking toward");
    Check(s.NearestEdge(2.5f,0.0f,1.0f) < 0,"and nothing in the middle of a floor");
    Check(s.NearestEdge(7.6f,0.0f,1.0f,1) < 0 && s.NearestEdge(7.6f,0.0f,1.0f,-1) >= 0,"by side, when asked");
    Check(s.SpanAt(3.0f,0.0f) == 0 && s.SpanAt(6.5f,0.0f) < 0 && s.SpanAt(3.0f,1.0f) < 0,
          "SpanAt: on a floor, over the gap, above it");

    //--- The main level's landmarks ---
    Stage m;
    snprintf(d,sizeof(d),"%zu spans, %zu edges, %zu corners",m.spans.size(),m.edges.size(),m.corners.size());
    Check(!m.spans.empty() && !m.edges.empty() && !m.corners.empty(),"the main level has floors, edges and wall feet",d);
    Check(FindCorner(m,5,0,1) >= 0 && FindCorner(m,9,0,-1) >= 0,"the step by the start stands on the ground: a foot each side");
    int step_r = FindEdge(m,9,1.8f,1);
    Check(step_r >= 0 && fabsf(m.edges[step_r].drop - 1.8f) < 0.001f,"and its top drops 1.8 to it");
    int hl = FindEdge(m,44,4.2f,-1), hr = FindEdge(m,48,4.2f,1);
    Check(hl >= 0 && hr >= 0 && m.edges[hl].f_grabbable && m.edges[hr].f_grabbable,"the high ledge's lips are grabbable");
    Check(hl >= 0 && fabsf(m.edges[hl].drop - 4.2f) < 0.001f && fabsf(m.edges[hl].wall - 4.2f) < 0.001f,
          "a 4.2 drop and a 4.2 face");
    Check(FindEdge(m,14,0,1) >= 0 && FindEdge(m,19,0,-1) >= 0,"the first gap, x 14 .. 19, has a lip each side");
#if ARCHER_TEST_BAY
    Check(FindEdge(m,-12,0,-1) < 0 && FindEdge(m,-12,0,1) < 0,"no edge where the bay's ground meets the start's");
#endif
    //Every edge is on its block's top, and every drop is real.
    int wrong = 0;
    for (const StageEdge& e : m.edges){
        if (e.block < 0 || fabsf(m.blocks[e.block].Top() - e.y) > 0.001f || e.drop <= 0.0f){ wrong++; }
    }
    snprintf(d,sizeof(d),"%i of %zu",wrong,m.edges.size());
    Check(wrong == 0,"every edge sits on its block's top, and drops",d);

    //--- Refreshing ---
    Stage fresh;
    Check(SameEdges(m,fresh) && !m.RefreshEdges(),"two builds of one level agree, and an unchanged level is not rebuilt");
    int wall_block = -1;
    for (size_t i = 0; i < m.blocks.size(); i++){
        if (m.blocks[i].kind == BLOCK_BREAKABLE){ wall_block = (int)i; break; }
    }
    int gen = m.edges_generation;
    size_t corners_before = m.corners.size();
    m.blocks[wall_block].f_alive = false;
    ArcherInput idle;
    Run(m,1,idle);
    Check(m.edges_generation == gen + 1 && m.corners.size() < corners_before,
          "a wall kicked in is gone from the edges on the next tick - its two feet with it");
    Stage same;
    same.blocks = m.blocks;
    same.RebuildEdges();
    Check(SameEdges(m,same),"and the refreshed edges equal a fresh build of the same blocks");
    m.blocks[wall_block].f_alive = true;
    m.blocks[wall_block].x += 0.5f;
    Check(m.RefreshEdges(),"moving a box is noticed by RefreshEdges, not only a block going");
    int gen_moved = m.edges_generation;
    m.blocks[wall_block].x -= 0.5f;
    m.KeepBlockLayout();
    Check(m.edges_generation == gen_moved + 1 && SameEdges(m,fresh),
          "and the editor's KeepBlockLayout rebuilds them, back to the level as built");
}

/*
    The growth walker - vine_plan.md section 10, Vine.cpp's GrowVine. What the growth depends on:
    it never enters a block, an underside vine hangs, a vine that reaches a floor lies on it and
    stops, the same shot grows the same vine, and every strand builds into a trunk. Against the
    main level's own undersides and a ceiling built to be landed under, then two hundred seeds.
*/
struct GrowCheck{
    float worst_clear = 1e9f;       //nearest a walked point came to a block, less the trunk's keep
    float worst_inside = 1e9f;      //nearest ANY walked point came, start included: never inside
    float worst_curve = 1e9f;       //the built curve, sampled every 0.05
    bool  f_built = true;
    bool  f_lower = true;
};
static void CheckGrowth(const VineGrowth& g, const VineSpecies& sp, const VineParams& params,
                        const std::vector<StageBlock>& blocks, const vec3& anchor, GrowCheck& c){
    std::vector<vertex> tile;
    MakeVinePlaceholderTile(tile);
    for (size_t k = 0; k < g.strands.size(); k++){
        const VineStrand& st = g.strands[k];
        float keep = params.tile_radius * params.tile_scale * st.path.thickness + sp.clearance;
        /*
            Point 0 is inside the rock (or the parent) by design, and the walk starts just off the
            face, within the keep while it leaves it - so the keep is asked of every point once the
            walk is two keeps from where it began, and of all of them only that none is inside.
        */
        const vec3& begin = st.path.points[(st.path.points.size() > 1) ? 1 : 0];
        for (size_t i = 1; i < st.path.points.size(); i++){
            const vec3& p = st.path.points[i];
            float dist = VineBlockDistance(blocks,p.x,p.y);
            c.worst_inside = fminf(c.worst_inside,dist);
            if ((p - begin).length() > 2.0f * keep){
                c.worst_clear = fminf(c.worst_clear,dist - keep);
            }
        }
        Spline sp_curve;
        if (!BuildVineSpline(st.path,sp_curve)){
            c.f_built = false;
            continue;
        }
        for (float s = 0.2f; s < sp_curve.GetLength(); s += 0.05f){
            vec3 p = sp_curve.PositionAt(s);
            c.worst_curve = fminf(c.worst_curve,VineBlockDistance(blocks,p.x,p.y));
        }
        std::vector<vertex> trunk;
        if (BuildVineTrunk(sp_curve,st.path,tile,params,trunk) <= 0){
            c.f_built = false;
        }
    }
    if (g.strands.empty() || g.strands[0].path.points.back().y > anchor.y - 1.0f){
        c.f_lower = false;
    }
}

/*
    The teeter (Puppet::teeter_ticks): LosingBalance once, standing still past a lip she faces over a
    real drop. Looks only, so this is the Puppet and DescribeArcher - the rules are not asked.
*/
static void TestTeeter(){
    printf("the teeter\n");
    const float dur = 6.0f;
    auto at_lip = [](){
        ArcherAnimParams a;
        a.f_on_ground = true;
        a.mode = MODE_GROUND;
        a.edge_over = 0.2f;
        a.edge_drop = 15.0f;
        return a;
    };
    {
        Puppet p;
        p.clip_duration[CLIP_TEETER] = dur;
        ArcherAnimParams a = at_lip();
        p.Tick(a);
        Check(p.choice.clip == CLIP_TEETER && p.choice.start_time == 0.0f,
              "stopped past a lip she faces, over a real drop: the teeter, from its first frame");
        int ticks = (int)(dur * ARCHER_TPS);
        for (int i = 0; i < ticks - 2; i++){ p.Tick(a); }
        Check(p.choice.clip == CLIP_TEETER,"and it plays through");
        for (int i = 0; i < 4; i++){ p.Tick(a); }
        Check(p.choice.clip == CLIP_IDLE,"once - then she idles there, not a six-second loop");
        for (int i = 0; i < 100; i++){ p.Tick(a); }
        Check(p.choice.clip == CLIP_IDLE,"and stays idle for as long as she stands there");
        a.ground_speed = 1.0f;
        a.speed = -1.0f;
        p.Tick(a);
        a.ground_speed = 0.0f;
        a.speed = 0.0f;
        p.Tick(a);
        Check(p.choice.clip == CLIP_TEETER,"a step and a stop at the lip again is a new teeter");
    }
    {
        Puppet p;
        p.clip_duration[CLIP_TEETER] = dur;
        ArcherAnimParams a = at_lip();
        a.edge_over = PUPPET_TEETER_FROM - 0.05f;
        p.Tick(a);
        Check(p.choice.clip == CLIP_IDLE,"short of the lip, no teeter");
        a = at_lip();
        a.edge_drop = PUPPET_TEETER_DROP - 0.5f;
        p.Tick(a);
        Check(p.choice.clip == CLIP_IDLE,"nor over a drop too small to fear");
        a = at_lip();
        a.ground_speed = PUPPET_IDLE_SPEED + 0.5f;
        a.speed = a.ground_speed;
        p.Tick(a);
        Check(p.choice.clip != CLIP_TEETER,"nor on the move - a run past the lip is a run, or a fall");
    }
    {
        //Drawing takes over, and a loose does not set her wobbling again.
        Puppet p;
        p.clip_duration[CLIP_TEETER] = dur;
        ArcherAnimParams a = at_lip();
        for (int i = 0; i < 10; i++){ p.Tick(a); }
        a.action = ACTION_DRAW;
        p.Tick(a);
        Check(p.choice.clip != CLIP_TEETER,"drawing at the lip is the draw, not the teeter");
        a.action = ACTION_NONE;
        p.Tick(a);
        Check(p.choice.clip == CLIP_IDLE,"and after the shot she stands, rather than teetering again");
    }
    {
        //Braked into it: the run-to-stop gives way to the teeter once she is still.
        Puppet p;
        p.clip_duration[CLIP_TEETER] = dur;
        p.clip_duration[CLIP_STOP] = 1.0f;
        p.stop_plant = 0.3f;
        ArcherAnimParams a = at_lip();
        a.edge_over = -1.0f;
        a.ground_speed = ARCHER_RUN_SPEED;
        a.speed = ARCHER_RUN_SPEED;
        p.Tick(a);
        float v = ARCHER_RUN_SPEED;
        while (v > 0.0f){
            v -= PUPPET_FRICTION_STEP;
            a.ground_speed = a.speed = (v > 0.0f) ? v : 0.0f;
            a.edge_over = (v > 0.0f) ? -0.1f : 0.2f;
            p.Tick(a);
        }
        Check(p.choice.clip == CLIP_TEETER,"a late brake that leaves her past the lip teeters rather than settling");
        Check(p.settle_ticks == 0,"and the stop is let go");
    }
    {
        //Landing at the lip: the landing first, then the teeter.
        Puppet p;
        p.clip_duration[CLIP_TEETER] = dur;
        p.clip_duration[CLIP_LAND_SOFT] = 0.4f;
        ArcherAnimParams a;
        a.f_on_ground = false;
        a.mode = MODE_AIR;
        a.vel_y = -ARCHER_JUMP_SPEED;
        p.Tick(a);
        a = at_lip();
        p.Tick(a);
        Check(p.choice.clip == CLIP_LAND_SOFT,"landing at the lip lands first");
        for (int i = 0; i < 30; i++){ p.Tick(a); }
        Check(p.choice.clip == CLIP_TEETER,"and teeters once it has");
    }
    {
        //DescribeArcher, on the rope level's floor lip: a 15-unit drop to the deep pit.
        Stage s;
        s.SetLevel(STAGE_LEVEL_ROPE);
        int lip = -1;
        for (size_t i = 0; i < s.edges.size(); i++){
            const StageEdge& e = s.edges[i];
            if (e.side == 1 && fabsf(e.y) < 0.01f && e.drop >= PUPPET_TEETER_DROP && e.x > 0.0f){
                lip = (int)i;
                break;
            }
        }
        Check(lip >= 0,"the rope level's floor ends in a real drop on its right");
        if (lip < 0){
            return;
        }
        float x = s.edges[lip].x;
        PlaceOn(s,x + 0.2f,0.0f);
        ArcherAnimParams got;
        DescribeArcher(s,got);
        Check(s.f_on_ground,"placed 0.2 past the lip, she is still standing - the box holds her");
        CheckNear(got.edge_over,0.2f,0.01f,"and DescribeArcher reports her 0.2 past it");
        CheckNear(got.edge_drop,s.edges[lip].drop,1e-4f,"with its drop");
        Check(Puppet::AtLip(got),"which is a lip to teeter at");
        s.facing = -1.0f;
        DescribeArcher(s,got);
        Check(got.edge_over < 0.0f && !Puppet::AtLip(got),"facing away from it, she does not teeter");
        s.facing = 1.0f;
        PlaceOn(s,x - 3.0f,0.0f);
        DescribeArcher(s,got);
        Check(got.edge_drop == 0.0f && !Puppet::AtLip(got),"and three units short of it there is no lip at all");
    }
}

static void TestVineGrowth(){
    printf("\ngrowing vines\n");
    char d[220];
    Stage m;
    const VineSpecies& sp = VineSpeciesFor(VINE_SPECIES_VINE);
    //The game's numbers: vine_trunk's measured radius at her scale.
    VineParams params;
    params.tile_radius = 0.083f;
    params.tile_scale = 2.02f;
    const vec3 under(0.0f,-1.0f,0.0f);

    //Under slab one (x 10.5 .. 12.5, underside 3.8), the ground 3.8 below.
    vec3 a1(11.5f,3.8f,0.0f);
    VineGrowth g1;
    bool f_grew = GrowVine(sp,params,a1,under,VineGrowthSeed(a1,0),m.blocks,g1);
    snprintf(d,sizeof(d),"%zu strands, main %zu points, %.2f walked",g1.strands.size(),
             g1.strands.empty() ? 0 : g1.strands[0].path.points.size(),g1.strands.empty() ? 0.0f : g1.strands[0].length);
    Check(f_grew && !g1.strands.empty() && g1.strands[0].path.points.size() >= 3,"a vine grows from an underside",d);
    const vec3& first = g1.strands[0].path.points[0];
    Check(first.y > a1.y && fabsf(first.x - a1.x) < 1e-4f,"its path starts inside the rock, so it comes out of it");
    GrowCheck c1;
    CheckGrowth(g1,sp,params,m.blocks,a1,c1);
    snprintf(d,sizeof(d),"walked points %.3f clear of the keep, none nearer than %.3f, curve %.3f clear",
             c1.worst_clear,c1.worst_inside,c1.worst_curve);
    Check(c1.worst_clear >= -0.002f && c1.worst_inside > 0.0f && c1.worst_curve > 0.0f,
          "it never enters a block, walked or curved",d);
    Check(c1.f_lower && c1.f_built,"it ends lower than it started, and every strand builds a trunk");

    //Under the high slab (x 84 .. 97, underside 9.2): more drop than any vine is long, so it hangs.
    vec3 a2(90.0f,9.2f,0.0f);
    VineGrowth g2;
    GrowVine(sp,params,a2,under,VineGrowthSeed(a2,3),m.blocks,g2);
    const std::vector<vec3>& pts = g2.strands[0].path.points;
    Spline hang;
    BuildVineSpline(g2.strands[0].path,hang);
    vec3 from = hang.PositionAt(fminf(1.0f,hang.GetLength()));
    vec3 fall = pts.back() - from;
    fall.normalize();
    float deg = acosf(fminf(1.0f,fmaxf(-1.0f,fall.dot(under)))) * 180.0f / 3.14159265f;
    snprintf(d,sizeof(d),"%.1f deg off straight down past its first metre, %.2f long",deg,g2.strands[0].length);
    Check(!g2.strands[0].f_rested && deg < 30.0f,"with nothing under it, it hangs",d);
    Check(g2.strands[0].length >= sp.length_min - sp.step && g2.strands[0].length <= sp.length_max + sp.step,
          "for a length the species allows",d);

    //Under a low ceiling (underside 1.5 over a floor at 0): it lands, lies along the floor, stops.
    std::vector<StageBlock> low(2);
    low[0].x = 0.0f; low[0].y = -1.0f; low[0].hw = 10.0f; low[0].hh = 1.0f;
    low[1].x = 0.0f; low[1].y = 2.0f;  low[1].hw = 2.0f;  low[1].hh = 0.5f;
    vec3 a3(0.3f,1.5f,0.0f);
    VineGrowth g3;
    GrowVine(sp,params,a3,under,VineGrowthSeed(a3,0),low,g3);
    const VineStrand& rest = g3.strands[0];
    float keep = params.tile_radius * params.tile_scale * rest.path.thickness + sp.clearance;
    const vec3& tip = rest.path.points.back();
    snprintf(d,sizeof(d),"tip at (%.2f, %.3f), keep %.3f, walked %.2f",tip.x,tip.y,keep,rest.length);
    Check(rest.f_rested,"under a low ceiling it reaches the floor and comes to rest",d);
    Check(fabsf(tip.y - keep) < 0.03f && fabsf(tip.x - a3.x) > 0.3f,"lying on it, having crept along it",d);
    Check(rest.length < sp.length_max,"and stops there rather than walking its whole length",d);

    //Off a wall - the step's right face: out from it, then down.
    vec3 a4(9.0f,1.2f,0.0f);
    VineGrowth g4;
    GrowVine(sp,params,a4,vec3(1.0f,0.0f,0.0f),VineGrowthSeed(a4,0),m.blocks,g4);
    GrowCheck c4;
    CheckGrowth(g4,sp,params,m.blocks,a4,c4);
    snprintf(d,sizeof(d),"%zu strands, walked %.3f clear of the keep, none nearer than %.3f, curve %.3f",
             g4.strands.size(),c4.worst_clear,c4.worst_inside,c4.worst_curve);
    Check(!g4.strands.empty() && c4.worst_clear >= -0.002f && c4.worst_inside > 0.0f && c4.worst_curve > 0.0f &&
          c4.f_built,"off a wall it grows clear of the face and the floor under it",d);

    //The same shot, the same vine; another arrow in the same spot, another vine.
    VineGrowth again;
    GrowVine(sp,params,a1,under,VineGrowthSeed(a1,0),m.blocks,again);
    bool f_same = again.strands.size() == g1.strands.size();
    for (size_t k = 0; f_same && k < again.strands.size(); k++){
        const std::vector<vec3>& p = again.strands[k].path.points;
        const std::vector<vec3>& q = g1.strands[k].path.points;
        f_same = (p.size() == q.size());
        for (size_t i = 0; f_same && i < p.size(); i++){
            f_same = (p[i].x == q[i].x && p[i].y == q[i].y && p[i].z == q[i].z);
        }
    }
    Check(f_same,"the same shot grows the same vine, bit for bit");
    Check(VineGrowthSeed(a1,0) != VineGrowthSeed(a1,1) && VineGrowthSeed(a1,0) == VineGrowthSeed(a1,0),
          "another arrow into the same spot has another seed");

    //Branches leave from their parent's own curve.
    float worst_root = 0.0f;
    int branches = 0;
    Spline main_curve;
    BuildVineSpline(g1.strands[0].path,main_curve);
    for (const VineStrand& st : g1.strands){
        if (st.parent < 0){ continue; }
        branches++;
        worst_root = fmaxf(worst_root,(st.path.points[0] - main_curve.PositionAt(st.s_on_parent)).length());
    }
    snprintf(d,sizeof(d),"%i branches, worst %.2e off",branches,worst_root);
    Check(worst_root < 1e-4f,"every branch starts on its parent's curve",d);

    //Two hundred seeds under slab one and under the low ceiling: every one holds.
    GrowCheck all;
    int failed_lower = 0, total_branches = 0, rested = 0;
    for (int seed = 0; seed < 200; seed++){
        VineGrowth g;
        GrowVine(sp,params,a1,under,seed * 7919,m.blocks,g);
        GrowCheck c;
        CheckGrowth(g,sp,params,m.blocks,a1,c);
        all.worst_clear = fminf(all.worst_clear,c.worst_clear);
        all.worst_inside = fminf(all.worst_inside,c.worst_inside);
        all.worst_curve = fminf(all.worst_curve,c.worst_curve);
        all.f_built = all.f_built && c.f_built;
        failed_lower += c.f_lower ? 0 : 1;
        total_branches += (int)g.strands.size() - 1;
        Spline parent;
        if (!g.strands.empty() && BuildVineSpline(g.strands[0].path,parent)){
            for (const VineStrand& st : g.strands){
                if (st.parent >= 0){
                    worst_root = fmaxf(worst_root,(st.path.points[0] - parent.PositionAt(st.s_on_parent)).length());
                    branches++;
                }
            }
        }
        VineGrowth gl;
        GrowVine(sp,params,a3,under,seed * 7919,low,gl);
        GrowCheck cl;
        CheckGrowth(gl,sp,params,low,vec3(a3.x,a3.y + 10.0f,0.0f),cl);  //not asked to end lower
        all.worst_clear = fminf(all.worst_clear,cl.worst_clear);
        all.worst_inside = fminf(all.worst_inside,cl.worst_inside);
        all.worst_curve = fminf(all.worst_curve,cl.worst_curve);
        all.f_built = all.f_built && cl.f_built;
        rested += gl.strands[0].f_rested ? 1 : 0;
    }
    snprintf(d,sizeof(d),"walked %.3f clear of the keep, none nearer than %.3f, curve %.3f, %i branches, "
             "%i of 200 rested under the ceiling",all.worst_clear,all.worst_inside,all.worst_curve,total_branches,rested);
    Check(all.worst_clear >= -0.002f && all.worst_inside > 0.0f && all.worst_curve > 0.0f,"200 seeds: none enters a block",d);
    Check(all.f_built && failed_lower == 0 && rested == 200,"all of them build, hang lower, and land under the ceiling",d);
    snprintf(d,sizeof(d),"%i branches, worst %.2e off",branches,worst_root);
    Check(branches > 50 && worst_root < 1e-4f,"and every one of their branches starts on its parent's curve",d);

    //The clock: from nothing at the strike to the whole length at grow_ticks, never back, and
    //faster at the start than the end - eased out, not a progress bar.
    float L = 5.0f;
    bool f_rising = true;
    float prev_front = 0.0f;
    for (int t = 0; t <= sp.grow_ticks + 10; t++){
        float f = VineGrowthFront(sp,L,t);
        if (f < prev_front){ f_rising = false; }
        prev_front = f;
    }
    float first_tenth = VineGrowthFront(sp,L,sp.grow_ticks / 10);
    float last_tenth = L - VineGrowthFront(sp,L,sp.grow_ticks - sp.grow_ticks / 10);
    snprintf(d,sizeof(d),"first tenth of the time %.2f, last tenth %.2f",first_tenth,last_tenth);
    Check(VineGrowthFront(sp,L,0) == 0.0f && VineGrowthFront(sp,L,sp.grow_ticks) == L && f_rising,
          "the front runs from 0 at the strike to the length at grow_ticks, never back");
    Check(first_tenth > 4.0f * last_tenth,"fast from the arrow, slowing to a stop",d);
    Check(VineLeafOpen(sp,1.0f,1.0f + sp.leaf_delay - 0.01f) == 0.0f &&
          VineLeafOpen(sp,1.0f,1.0f + sp.leaf_delay + sp.leaf_unfold + 0.01f) == 1.0f &&
          VineLeafOpen(sp,1.0f,1.0f + sp.leaf_delay + 0.5f * sp.leaf_unfold) > 0.5f,
          "a leaf waits leaf_delay behind the front, then opens over leaf_unfold");
    Check(sp.leaf_delay >= SplineDeformParams().grow_tip_length,
          "and waits until the growing tip's taper has passed, so it sits on the finished trunk");

    /*
        Against the DRAWN surface - vine_plan.md step 6. A stone floating over a floor: the terrain
        draws a belly under it, so the box's underside, where an arrow sticks, is inside the rock
        you see. With no surfaces the level field is exactly the boxes; with the stone's surface,
        the vine starts on the belly, not inside it, and nothing it walks or grows a leaf into is
        inside the drawn rock.
    */
    std::vector<StageBlock> stone(2);
    stone[0].x = 0.0f; stone[0].y = -1.0f; stone[0].hw = 10.0f; stone[0].hh = 1.0f;
    stone[1].x = 0.0f; stone[1].y = 5.5f;  stone[1].hw = 2.0f;  stone[1].hh = 0.5f;
    TerrainRegion above;
    above.x_min = -20.0f; above.x_max = 20.0f; above.y_min = 3.0f; above.y_max = 20.0f;
    TerrainSurface drawn;
    drawn.Build(stone,above,TerrainParams());
    std::vector<const TerrainSurface*> none;
    VineLevelField plain(stone,none);
    float worst_same = 0.0f;
    for (float x = -4.0f; x <= 4.0f; x += 0.37f){
        for (float y = -1.0f; y <= 8.0f; y += 0.41f){
            worst_same = fmaxf(worst_same,fabsf(plain.Distance(vec3(x,y,0.0f)) - VineBlockDistance(stone,x,y)));
        }
    }
    Check(worst_same == 0.0f,"with no terrain surfaces the level field is exactly the boxes");
    std::vector<const TerrainSurface*> surfaces(1,&drawn);
    VineLevelField level(stone,surfaces);
    float belly = level.Distance(vec3(0.0f,4.9f,0.0f));
    snprintf(d,sizeof(d),"drawn distance %.3f just under the box's underside",belly);
    Check(belly < 0.0f,"the stone's drawn belly hangs below its box",d);

    vec3 a5(0.0f,5.0f,0.0f);
    VineGrowth boxed, drawn_vine;
    GrowVine(sp,params,a5,under,VineGrowthSeed(a5,0),stone,boxed);
    GrowVine(sp,params,a5,under,VineGrowthSeed(a5,0),level,drawn_vine);
    const vec3& box_start = boxed.strands[0].path.points[1];
    const vec3& drawn_start = drawn_vine.strands[0].path.points[1];
    snprintf(d,sizeof(d),"against the boxes it starts at y %.2f (drawn distance %.2f); against the "
             "drawing at y %.2f (%.3f)",box_start.y,level.Distance(box_start),drawn_start.y,level.Distance(drawn_start));
    Check(level.Distance(box_start) < 0.0f && level.Distance(drawn_start) >= 0.0f && drawn_start.y < box_start.y,
          "against the drawing it starts on the belly, not up inside it",d);
    float worst_walk = 1e9f, worst_leaf = 1e9f;
    int leaves_checked = 0;
    float drawn_keep = params.tile_radius * params.tile_scale * drawn_vine.strands[0].path.thickness + sp.clearance;
    for (const VineStrand& st : drawn_vine.strands){
        const vec3& begin = st.path.points[1];
        for (size_t i = 1; i < st.path.points.size(); i++){
            const vec3& p = st.path.points[i];
            if ((p - begin).length() > 2.0f * drawn_keep){
                worst_walk = fminf(worst_walk,level.Distance(p) - drawn_keep);
            }
        }
        Spline curve;
        BuildVineSpline(st.path,curve);
        std::vector<VineLeaf> lv;
        ScatterVineLeaves(curve,st.path,params,&level,lv);
        for (const VineLeaf& l : lv){
            vec3 tip = l.position + (l.rotation * vec3(0.0f,0.0f,1.0f)) * (params.leaf_length * l.scale);
            worst_leaf = fminf(worst_leaf,level.Distance(tip));
            leaves_checked++;
        }
    }
    snprintf(d,sizeof(d),"walked points %.3f clear of the keep, leaf tips %.3f clear, %i leaves",
             worst_walk,worst_leaf,leaves_checked);
    Check(worst_walk >= -0.01f && leaves_checked > 0 && worst_leaf >= 0.0f,
          "and neither the walk nor a leaf goes into the drawn rock",d);
    //Rooted: full thickness where it comes out, tapered only at its free end.
    Spline rooted;
    BuildVineSpline(drawn_vine.strands[0].path,rooted);
    float full = params.tile_radius * params.tile_scale * drawn_vine.strands[0].path.thickness;
    float r0 = VineRadiusAt(rooted,drawn_vine.strands[0].path,params,0.0f);
    float r1 = VineRadiusAt(rooted,drawn_vine.strands[0].path,params,rooted.GetLength());
    snprintf(d,sizeof(d),"radius %.3f at the root, %.3f at the tip, %.3f full",r0,r1,full);
    Check(drawn_vine.strands[0].path.f_rooted && fabsf(r0 - full) < 1e-5f && r1 < 0.5f * full,
          "a grown vine is full thickness out of the rock and tapers only at its tip",d);
}

/*
    Roots and tufts - vine_plan.md step 7. The roots are a species of the same walker, so what is
    checked is what makes them roots: a few of them from one strike, short, thin, quick, forked,
    down and clear of the rock; and a tuft is a few small plants on the surface it was asked for.
*/
static void TestRootsAndTufts(){
    printf("\nroots and tufts\n");
    char d[200];
    Stage m;
    const VineSpecies& rs = VineSpeciesFor(VINE_SPECIES_ROOTS);
    //The placeholder octagon, as the app uses until root_tile exists.
    std::vector<vertex> tile;
    MakeVinePlaceholderTile(tile);
    VineParams params;
    params.tile_scale = 1.0f;
    params.tile_radius = VineTileRadius(tile);
    VineBlockField field(m.blocks);
    const vec3 under(0.0f,-1.0f,0.0f);

    int worst_count_lo = 99, worst_count_hi = 0, bad_parent = 0, inside = 0, not_lower = 0, forks = 0;
    float longest = 0.0f, shortest = 1e9f, thickest = 0.0f;
    for (int seed = 0; seed < 100; seed++){
        vec3 a(11.5f + 0.01f * (float)(seed % 20),3.8f,0.0f);
        VineGrowth g;
        bool f_grew = GrowRoots(rs,params,a,under,seed * 31 + 7,field,g);
        int roots = 0;
        for (size_t k = 0; k < g.strands.size(); k++){
            const VineStrand& st = g.strands[k];
            if (st.parent < 0){
                roots++;
                longest = fmaxf(longest,st.length);
                shortest = fminf(shortest,st.length);
                if (st.path.points.back().y >= a.y - 0.1f){ not_lower++; }
            }else{
                forks++;
                if (st.parent >= (int)k || g.strands[st.parent].parent >= 0){ bad_parent++; }
            }
            thickest = fmaxf(thickest,params.tile_radius * params.tile_scale * st.path.thickness);
            for (size_t i = 2; i < st.path.points.size(); i++){
                const vec3& p = st.path.points[i];
                if (VineBlockDistance(m.blocks,p.x,p.y) < 0.0f){ inside++; }
            }
        }
        if (!f_grew){ roots = 0; }
        worst_count_lo = std::min(worst_count_lo,roots);
        worst_count_hi = std::max(worst_count_hi,roots);
    }
    snprintf(d,sizeof(d),"%i .. %i roots a strike, %i forks over 100 strikes",worst_count_lo,worst_count_hi,forks);
    Check(worst_count_lo >= 2 && worst_count_hi <= 4 && forks > 100,"two to four roots from a strike, forking",d);
    Check(bad_parent == 0,"every fork hangs off its own root, pointing into the plant's list");
    snprintf(d,sizeof(d),"%.2f .. %.2f long, %.3f thick at most",shortest,longest,thickest);
    Check(shortest >= rs.length_min - rs.step && longest <= rs.length_max + rs.step && thickest < 0.12f,
          "short and thin: roots, not vines",d);
    snprintf(d,sizeof(d),"%i points inside a block, %i roots not lower than the strike",inside,not_lower);
    Check(inside == 0 && not_lower == 0,"down out of the underside, never into the rock",d);
    Check(rs.grow_ticks <= 30 && rs.grow_ticks < VineSpeciesFor(VINE_SPECIES_VINE).grow_ticks / 4,
          "quick - they are grown by every normal arrow into an underside");

    //A tuft on a top, and one out of a wall.
    std::vector<TuftPlant> top, wall, again;
    ScatterTuft(vec3(3.0f,0.0f,0.0f),vec3(0.0f,1.0f,0.0f),5,0.28f,top);
    ScatterTuft(vec3(9.0f,1.0f,0.0f),vec3(1.0f,0.0f,0.0f),5,0.28f,wall);
    ScatterTuft(vec3(3.0f,0.0f,0.0f),vec3(0.0f,1.0f,0.0f),5,0.28f,again);
    float off_plane = 0.0f, spread = 0.0f;
    int grass = 0;
    for (const TuftPlant& p : top){
        off_plane = fmaxf(off_plane,fabsf(p.position.y - 0.0f));
        spread = fmaxf(spread,(p.position - vec3(3.0f,0.0f,0.0f)).length());
        grass += (p.kind == FOLIAGE_GRASS || p.kind == FOLIAGE_GRASS_2) ? 1 : 0;
    }
    for (const TuftPlant& p : wall){
        off_plane = fmaxf(off_plane,fabsf(p.position.x - 9.0f));
    }
    snprintf(d,sizeof(d),"%zu plants on the top (%i grass), %.3f off its surface at most, %.2f across",
             top.size(),grass,off_plane,spread);
    Check(top.size() >= 3 && top.size() <= 5 && grass >= (int)top.size() - 2,"a tuft is three to five plants, mostly grass",d);
    Check(off_plane < 1e-5f && spread <= 0.28f + 1e-4f,"on the surface it was asked for, within its radius - a wall's too",d);
    bool f_same = (again.size() == top.size());
    for (size_t i = 0; f_same && i < top.size(); i++){
        f_same = (again[i].kind == top[i].kind && again[i].position.x == top[i].position.x &&
                  again[i].position.z == top[i].position.z && again[i].scale == top[i].scale);
    }
    Check(f_same && wall[0].up.x == 1.0f,"the same spot grows the same tuft, and a wall's stands out of the wall");
}

/*
    Creepers - vine_plan.md step 8, the walker's hug. A wall 3 tall standing on a floor: a creeper
    struck into its face climbs it, comes over the lip onto the top, keeps against the surface the
    whole way and never goes into it; one struck into a top creeps along it. A hundred seeds hold.
*/
struct CreepCheck{
    float top = -1e9f;          //highest point reached
    bool  f_over = false;       //a point lying on the wall's top
    int   near = 0, points = 0; //points within hug reach of a surface, of the points counted
    float worst_inside = 1e9f;  //nearest any point came, start included
    float worst_keep = 1e9f;    //nearest a point came less the keep, once clear of the start
};
static void MeasureCreeper(const VineGrowth& g, const VineSpecies& sp, const VineParams& params,
                           const std::vector<StageBlock>& blocks, float wall_l, float wall_r, float wall_top,
                           CreepCheck& c){
    for (const VineStrand& st : g.strands){
        float keep = params.tile_radius * params.tile_scale * st.path.thickness + sp.clearance;
        const vec3& begin = st.path.points[1];
        for (size_t i = 1; i < st.path.points.size(); i++){
            const vec3& p = st.path.points[i];
            float d = VineBlockDistance(blocks,p.x,p.y);
            c.worst_inside = fminf(c.worst_inside,d);
            if ((p - begin).length() > 2.0f * keep){
                c.worst_keep = fminf(c.worst_keep,d - keep);
            }
            if (i >= 2){
                c.points++;
                c.near += (d < keep + sp.hug_reach) ? 1 : 0;
            }
            c.top = fmaxf(c.top,p.y);
            if (p.x > wall_l + 0.1f && p.x < wall_r - 0.1f && p.y >= wall_top && p.y < wall_top + keep + 0.3f){
                c.f_over = true;
            }
        }
    }
}

static void TestCreepers(){
    printf("\ncreepers\n");
    char d[220];
    const VineSpecies& cs = VineSpeciesFor(VINE_SPECIES_CREEPER);
    VineParams params;
    params.tile_radius = 0.083f;
    params.tile_scale = 2.02f;
    std::vector<StageBlock> blocks = { Box(-10,10,-2,0), Box(2,4,0,3) };
    VineBlockField field(blocks);

    vec3 a(2.0f,1.0f,0.0f);
    VineGrowth g;
    bool f_grew = GrowVine(cs,params,a,vec3(-1.0f,0.0f,0.0f),VineGrowthSeed(a,0),field,g);
    CreepCheck c;
    if (f_grew){
        MeasureCreeper(g,cs,params,blocks,2.0f,4.0f,3.0f,c);
        if (getenv("STAGE_TEST_CREEPER")){
            for (const vec3& p : g.strands[0].path.points){
                printf("      creeper (%.2f, %.2f, %.2f) d %.3f\n",p.x,p.y,p.z,VineBlockDistance(blocks,p.x,p.y));
            }
        }
    }
    snprintf(d,sizeof(d),"%zu strands, %.2f walked, up to y %.2f, %i of %i points against a surface",
             g.strands.size(),f_grew ? g.strands[0].length : 0.0f,c.top,c.near,c.points);
    Check(f_grew && c.top >= 3.0f,"a creeper struck into a wall climbs it",d);
    Check(c.f_over,"comes over the lip onto the top",d);
    Check(c.points > 0 && c.near >= (int)(0.9f * (float)c.points),"and lies against the surface the whole way",d);
    snprintf(d,sizeof(d),"none nearer than %.3f, %.3f clear of the keep once off the start",c.worst_inside,c.worst_keep);
    Check(c.worst_inside > 0.0f && c.worst_keep >= -0.01f,"never into the rock",d);
    Check(!g.strands.empty() && fabsf(g.strands[0].path.up.x + 1.0f) < 1e-5f,
          "its leaves' frame stands out of the wall it grew from");

    //Struck into a top: along it, never resting, never lifting off.
    vec3 f0(-5.0f,0.0f,0.0f);
    VineGrowth gf;
    GrowVine(cs,params,f0,vec3(0.0f,1.0f,0.0f),VineGrowthSeed(f0,0),field,gf);
    float highest = -1e9f, travelled = 0.0f;
    if (!gf.strands.empty()){
        for (const vec3& p : gf.strands[0].path.points){
            highest = fmaxf(highest,p.y);
        }
        travelled = fabsf(gf.strands[0].path.points.back().x - f0.x);
    }
    snprintf(d,sizeof(d),"highest y %.2f, %.2f along the floor, rested %s",highest,travelled,
             (!gf.strands.empty() && gf.strands[0].f_rested) ? "yes" : "no");
    Check(!gf.strands.empty() && highest < 0.5f && travelled > 1.5f && !gf.strands[0].f_rested,
          "struck into a top it creeps along it, low, and does not stop to rest",d);

    //A hundred seeds up the same wall.
    int over = 0, climbed = 0, bad = 0;
    for (int seed = 0; seed < 100; seed++){
        VineGrowth s;
        if (!GrowVine(cs,params,a,vec3(-1.0f,0.0f,0.0f),seed * 977 + 3,field,s)){
            bad++;
            continue;
        }
        CreepCheck k;
        MeasureCreeper(s,cs,params,blocks,2.0f,4.0f,3.0f,k);
        over += k.f_over ? 1 : 0;
        climbed += (k.top >= 2.5f) ? 1 : 0;
        if (k.worst_inside <= 0.0f || k.worst_keep < -0.01f){ bad++; }
    }
    snprintf(d,sizeof(d),"%i climbed, %i came over the top, %i went into the rock or failed",climbed,over,bad);
    Check(bad == 0 && climbed >= 95 && over >= 80,"a hundred seeds: they climb, most come over, none goes in",d);
}

#if ARCHER_TEST_BAY
/*
    The cave (cave_plan.md): that it is there and closed, that she can get in and through the
    mouth without a bonk and cannot get out past the far wall, that its zone names it, and that the
    bank behind closes it - the check a screenshot only makes from one angle.
*/
static void TestCave(){
    printf("the cave\n");
    char d[200];
    const Stage level;
    int floor = BlockAt(level,-53.0f,0.0f);
    int bay_floor = BlockAt(level,-26.0f,0.0f);
    int roof = -1, wall = -1, lip = -1;
    for (size_t i = 0; i < level.blocks.size(); i++){
        const StageBlock& b = level.blocks[i];
        if (fabsf(b.Bottom() - ARCHER_CAVE_ROOF_Y) < 0.01f && b.x < ARCHER_TEST_BAY_X_MIN) roof = (int)i;
        if (b.Right() > ARCHER_CAVE_X_MIN && b.Left() < ARCHER_CAVE_X_MIN + 3.0f && b.Bottom() > -0.01f &&
            b.Top() >= ARCHER_CAVE_ROOF_Y) wall = (int)i;
        if (fabsf(b.x - ARCHER_TEST_BAY_X_MIN) < 0.01f && b.Bottom() > 3.0f) lip = (int)i;
    }
    Check(floor >= 0 && bay_floor >= 0 && roof >= 0 && wall >= 0 && lip >= 0,
          "the cave's floor, roof, far wall and mouth are where this test looks");
    if (floor < 0 || bay_floor < 0 || roof < 0 || wall < 0 || lip < 0){
        return;
    }
    const StageBlock& f = level.blocks[floor];
    const StageBlock& bf = level.blocks[bay_floor];
    const StageBlock& r = level.blocks[roof];
    const StageBlock& w = level.blocks[wall];
    const StageBlock& m = level.blocks[lip];
    Check(fabsf(f.Right() - bf.Left()) < 0.001f && f.Top() == bf.Top() && f.Back() == bf.Back(),
          "its floor meets the bay's under the mouth, level with it and as deep, so the stream runs on");
    Check(f.Left() <= ARCHER_CAVE_X_MIN + 0.001f && r.Left() <= w.Left() + 0.001f && r.Right() >= f.Right() - 0.001f,
          "the roof covers it from the far wall to the mouth");
    Check(w.Top() >= r.Bottom() && fabsf(w.Bottom() - f.Top()) < 0.001f,"the far wall stands on the floor, up into the roof");
    snprintf(d,sizeof(d),"lip underside %.2f, head at the top of a jump %.2f",m.Bottom(),ApexRise() + 2.0f * ARCHER_HALF_H);
    Check(m.Bottom() > ApexRise() + 2.0f * ARCHER_HALF_H + 0.3f && m.Top() >= r.Bottom(),
          "the mouth's lip hangs from the roof, clear of her head at the top of a jump",d);

    //In on foot - from past the bay's mound, which is a step up she has to jump - in on a jump off
    //the mound's top, the tightest one under the lip, and not out past the far wall.
    Stage s = level;
    DropOnto(s,-39.0f,0.0f);
    ArcherInput left;
    left.move_axis = -1.0f;
    Run(s,240,left);
    snprintf(d,sizeof(d),"at x %.2f",s.pos.x);
    Check(s.f_on_ground && s.pos.x < -45.0f,"she walks in through the mouth",d);
    Run(s,1200,left);
    snprintf(d,sizeof(d),"stopped at x %.3f",s.pos.x);
    Check(s.f_on_ground && s.pos.x - ARCHER_HALF_W >= w.Right() - 0.001f && s.pos.x < w.Right() + 1.0f,
          "and the far wall stops her",d);
    Stage j = level;
    DropOnto(j,-37.0f,0.0f);
    ArcherInput jump = left;
    jump.f_jump_down = true;
    jump.f_jump_pressed = true;
    float peak_vy_cut = 0.0f;
    for (int t = 0; t < 120; t++){
        StageEvents e;
        float vy_before = j.vel.y;
        j.Tick(jump,e);
        jump.f_jump_pressed = false;
        //A bonk is her rising speed cut to nothing in one tick, under a roof.
        if (vy_before > 2.0f && j.vel.y <= 0.0f && j.pos.x < -38.0f){
            peak_vy_cut = vy_before;
        }
    }
    snprintf(d,sizeof(d),"at x %.2f, rising %.2f cut",j.pos.x,peak_vy_cut);
    Check(j.f_on_ground && j.pos.x < -42.0f && peak_vy_cut == 0.0f,"and jumps in through it without a bonk",d);

    //Its zone names it.
    Stage z = level;
    DropOnto(z,-50.0f,0.0f);
    int cave_zone = -1;
    for (const StageZone& zone : level.zones){
        cave_zone = (zone.name == "Cave") ? zone.id : cave_zone;
    }
    Check(cave_zone >= 0 && z.CurrentZone() == cave_zone,"inside, the zone is the cave");

    //The bank closes it: under the roof every wall column stands roof_rise over it, and the roof
    //and far wall reach back into the bank, so there is no gap for the sky between.
    BackdropParams bp;
    std::vector<StageBlock> bank;
    BuildBackdropBlocks(level.blocks,ARCHER_CAVE_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,bp,bank);
    const float wall_front = f.Back() - bp.wall_gap;
    int columns = 0, low = 0;
    for (const StageBlock& b : bank){
        //Wall columns only, and only those behind the roof over their whole core.
        if (fabsf(b.Front() - wall_front) > 0.001f || b.x < r.Left() || b.x > r.Right()){
            continue;
        }
        columns++;
        low += (b.Top() < r.Top() + bp.roof_rise - 0.001f) ? 1 : 0;
    }
    snprintf(d,sizeof(d),"%i of %i columns behind the roof are low",low,columns);
    Check(columns >= 10 && low == 0,"the bank stands over the roof all along the cave",d);
    Check(r.Back() < wall_front && w.Back() < wall_front && m.Back() < wall_front,
          "and the roof, the far wall and the lip reach back into it");
}
#endif

int main(void){
    printf("--- archer stage rules ---\n");
    printf("derived from the constants: apex %.2f, airtime %.1f ticks, gap reach %.2f\n\n",
           ApexRise(),AirTicks(),GapReach());

    TestLevel();
    TestGroundAndRun();
    TestJump();
    TestJumpBuffer();
    TestGapAndPlatform();
    TestBow();
    TestLedge();
    TestObstacles();
    TestKick();
    TestRope();
    TestPuppet();
    TestDeterminism();
    TestRange();
    TestStrawMan();
    TestGetUp();
    TestFoliage();
    TestVines();
    TestKneel();
    TestSway();
    TestAimOffPlane();
    TestKneelPuppet();
    TestRopeLevel();
    TestRopePits();
    TestSlideGallery();
    TestTree();
    TestSpringPlants();
    TestSpringPump();
    TestBranch();
    TestBranchCatch();
    TestLandingForecast();
    TestArrowForecast();
    TestRopeMesh();
    TestRopeClimb();
#if ARCHER_TEST_BAY
    TestBayClimb();
    TestBackdrop();
    TestCave();
#endif
    TestBoulders();
    TestZones();
    TestCrumble();
    TestChase();
    TestBridge();
    TestSnapBridge();
    TestRoutes();
    TestVitals();
    TestArrowKinds();
    TestAimHold();
    TestEdges();
    TestTeeter();
    TestVineGrowth();
    TestRootsAndTufts();
    TestCreepers();

    printf("\n%i checks, %i failures\n",g_checks,g_failures);
    return g_failures ? 1 : 0;
}
