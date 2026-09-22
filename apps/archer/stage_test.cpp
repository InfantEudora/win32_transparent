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

    //--- A tap still produces an arrow -------------------------------------------------------------
    ArcherInput tap;
    tap.f_draw_down = true;
    tap.f_draw_released = true;      //pressed and let go inside one tick
    StageEvents ev;
    s.Tick(tap,ev);
    Check(ev.f_shot,"a press and release inside one tick still looses an arrow");
    CheckNear(ev.shot_power,BOW_MIN_POWER,0.001f,"at the minimum power");
    Check(s.NumLiveArrows() == 1,"and the arrow exists");

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

    //Everything with no clip authored falls through to the idle AND says so.
    in.f_on_ground = false;
    in.speed = 0.0f;
    in.ground_speed = 0.0f;
    c = p.Choose(in);
    Check(c.f_placeholder,"airborne is a placeholder - nothing is authored for it");
    in.f_on_ground = true;
    in.mode = MODE_HANG;
    Check(p.Choose(in).f_placeholder,"and so is hanging");
    in.mode = MODE_ROPE;
    Check(p.Choose(in).f_placeholder,"and swinging on the rope");
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

    printf("\n%i checks, %i failures\n",g_checks,g_failures);
    return g_failures ? 1 : 0;
}
