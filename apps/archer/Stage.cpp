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

//Does the archer's body box, centred at (cx,cy), overlap this block?
static bool BoxOverlapsBlock(float cx, float cy, const StageBlock& b){
    if (cx + ARCHER_HALF_W <= b.Left())   { return false; }
    if (cx - ARCHER_HALF_W >= b.Right())  { return false; }
    if (cy + ARCHER_HALF_H <= b.Bottom()) { return false; }
    if (cy - ARCHER_HALF_H >= b.Top())    { return false; }
    return true;
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
    pos = v2(-6.0f,2.0f);
    vel = v2(0.0f,0.0f);
    mode = MODE_AIR;
    facing = 1.0f;
    f_on_ground = false;
    coyote_ticks = 0;
    buffer_ticks = 0;

    bow_mode = BOW_IDLE;
    draw_ticks = 0;
    aim_deg = 20.0f;

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
void Stage::BuildLevel(){
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
    //Kickable crates by the start, so the very first thing in reach proves the archer's hand-swept
    //body really does shove a solved rigid body around.
    props.push_back({ PROP_CRATE, 3.00f, 0.40f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, 4.10f, 0.40f, 0.80f, 0.80f, 1, 1 });
    props.push_back({ PROP_CRATE, 4.10f, 1.25f, 0.80f, 0.80f, 1, 1 });

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

    if (in.f_draw_down){
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
    //The rope slice hands the body to rp3d, and while it holds it this function must not also be
    //driving it - two things integrating one position is the classic way to get a character that
    //vibrates. Named here so the slice that adds it has an obvious place to hook in.
    if (mode == MODE_ROPE){
        return;
    }

    //--- Horizontal ---------------------------------------------------------------------------
    float move_scale = (bow_mode == BOW_DRAWING) ? ARCHER_DRAW_MOVE_SCALE : 1.0f;
    float target_vx = ClampF(in.move_axis,-1.0f,1.0f) * ARCHER_RUN_SPEED * move_scale;

    if (in.move_axis > 0.01f || in.move_axis < -0.01f){
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
    MoveAndCollide(vel * ARCHER_DT,in.f_down_held,f_hit_floor,f_hit_ceiling,f_hit_wall);

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

    //MODE_HANG and MODE_CLIMB are not reachable yet; the hang slice adds the ledge probe that
    //enters them, and this line is where it goes.
    mode = f_on_ground ? MODE_GROUND : MODE_AIR;

    //Fell off the world. Restarting outright rather than dying, because there is nothing to die
    //of yet and a prototype that makes you relaunch it is a prototype nobody plays with.
    if (pos.y < -40.0f){
        pos = v2(-6.0f,2.0f);
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
void Stage::MoveAndCollide(const v2& delta, bool f_down_held, bool& out_hit_floor,
                           bool& out_hit_ceiling, bool& out_hit_wall){
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
        }
    }
}

bool Stage::BodyOverlapsSolid(const v2& centre, float prev_bottom, bool f_down_held) const{
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (!b.f_alive){
            continue;
        }
        if (b.kind == BLOCK_PLATFORM){
            if (f_down_held || prev_bottom < b.Top() - STAGE_EPS){
                continue;
            }
        }
        if (BoxOverlapsBlock(centre.x,centre.y,b)){
            return true;
        }
    }
    return false;
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
