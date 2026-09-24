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

#include "Stage.h"
#include "Puppet.h"
#include "Foliage.h"
#include "Vine.h"
#include "RopeMesh.h"

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool f_ok, const char* what, const char* detail = NULL){
    g_checks++;
    if (f_ok){
        printf("  ok    %s\n",what);
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
    v2 arc[AIM_ARC_POINTS];
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
        v2 a = f.arrows[idx].pos;
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

        The test bay's pillar is the thin case - 0.5 wide, thinner than an arrow is long - with her
        pressed against its near face. A level full-draw shot must stick IN THAT FACE: not inside
        the pillar, not beyond it.
    */
#if ARCHER_TEST_BAY
    {
        float cx = ARCHER_TEST_BAY_SHAPE_CENTRE(0);
        float pillar_left = cx + 2.2f - 0.25f;
        float pillar_right = cx + 2.2f + 0.25f;
        Stage p;
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
#endif

    //--- Aim is relative to facing -----------------------------------------------------------------
    Stage m;
    Settle(m);
    m.aim_deg = 30.0f;
    m.facing = 1.0f;
    v2 r = m.AimDirection();
    m.facing = -1.0f;
    v2 l = m.AimDirection();
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
    CheckNear(s.pos.y + ARCHER_HALF_H,ledge.Top(),0.01f,"with the hands exactly on the lip");
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

    bool f_climbed = false;
    for (int i = 0; i < LEDGE_CLIMB_TICKS + 30; i++){
        StageEvents e;
        s.Tick(idle,e);
        if (e.f_climbed){
            f_climbed = true;
            break;
        }
    }
    Check(f_climbed,"which finishes");
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
        if (probe.blocks[i].kind == BLOCK_LEDGE && probe.blocks[i].Top() <= ApexRise()){
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

    //And the climb, which has a clip and a window that disagree by a lot.
    p.clip_duration[CLIP_CLIMB] = (float)LEDGE_CLIMB_TICKS * ARCHER_DT * 4.0f;
    in.mode = MODE_CLIMB;
    c = p.Choose(in);
    Check(c.clip == CLIP_CLIMB,"climbing plays the climb");
    Check(!c.f_placeholder,"which is a real clip now, not a stand-in");
    CheckNear(c.wanted_rate,4.0f,0.001f,"asking for the rate that fits LEDGE_CLIMB_TICKS");
    CheckNear(c.rate,PUPPET_ACTION_RATE_MAX,0.001f,"clamped, so the gap is visible rather than a blur");
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
        }else{
            other++;
        }
    }
    Check(other == 0,"the only props are targets and crates");
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
    for (size_t i = 0; i < plants.size(); i++){
        const FoliagePlant& p = plants[i];
        bool f_ground = fabsf(p.y - 0.0f) < 0.001f;
        if (f_ground && p.x > -7.5f && p.x < -4.5f){ under_box++; }
        if (fabsf(p.y - 1.0f) < 0.001f){ on_box++; }
        if (fabsf(p.y - 3.1f) < 0.001f){ on_platform++; }
        if (f_ground && p.x > -10.5f && p.x < -9.5f){ on_breakable++; }
        if (f_ground && p.x > 7.0f && p.x < 9.0f){ in_wall++; }
        if (f_ground && p.x > 5.5f && p.x < 7.0f){ near_wall++; }
        if (f_ground && p.x > -3.5f && p.x < 0.5f){ in_open++; }
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
    ScatterVineLeaves(sp,paths[0],params,NULL,blind);
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
    v2 arc[AIM_ARC_POINTS];
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

    //Bounded by the amplitude, and actually moving.
    float lo = 0.0f;
    float hi = 0.0f;
    for (int i = 0; i < 600; i++){
        Run(s,1,draw);
        float w = s.AimSwayDeg();
        if (w < lo){ lo = w; }
        if (w > hi){ hi = w; }
    }
    char detail[160];
    snprintf(detail,sizeof(detail),"%.2f .. %.2f over ten seconds",lo,hi);
    Check(hi <= AIM_SWAY_STAND_DEG && lo >= -AIM_SWAY_STAND_DEG,"standing, it stays inside AIM_SWAY_STAND_DEG",detail);
    Check(hi - lo > AIM_SWAY_STAND_DEG,"and really drifts",detail);
    CheckNear(s.ShotAimDeg(),s.aim_deg + s.AimSwayDeg(),0.0001f,"the shot's angle is the aim plus the sway");

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
    for (int i = 0; i < 600; i++){
        Run(k,1,draw);
        float w = k.AimSwayDeg();
        if (w < klo){ klo = w; }
        if (w > khi){ khi = w; }
    }
    snprintf(detail,sizeof(detail),"%.2f .. %.2f kneeling",klo,khi);
    Check(khi <= AIM_SWAY_KNEEL_DEG && klo >= -AIM_SWAY_KNEEL_DEG,"kneeling, it stays inside AIM_SWAY_KNEEL_DEG",detail);

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
    ArcherInput loose;
    loose.f_draw_released = true;
    Run(a,1,loose);
    Run(a,BOW_NOCK_TICKS + 40,draw);
    Check(fabsf(a.AimSwayDeg() - b.AimSwayDeg()) > 0.05f,"but the next draw sways differently");
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
}

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
    TestGetUp();
    TestFoliage();
    TestVines();
    TestKneel();
    TestSway();
    TestKneelPuppet();
    TestRopeLevel();
    TestRopeMesh();

    printf("\n%i checks, %i failures\n",g_checks,g_failures);
    return g_failures ? 1 : 0;
}
