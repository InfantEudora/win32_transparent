#include "Field.h"
#include "Debug.h"
#include "type_helpers.h"

#include <math.h>

static Debugger* debug = new Debugger("BreakoutField",DEBUG_ALL);

/*
    Only the .cpp reaches for the engine, and only for logging and the scalar helpers - the header
    stays free of it, which is what lets the rules be reasoned about (and one day replayed)
    without a window. Same split as tetris/Playfield.
*/

//How far the ball is pushed back off a surface after a bounce, in world units. Without it the
//next sweep of the same tick starts exactly ON the face it just left, where the slab test's
//entry time is zero and the ball can bounce off the same brick twice.
#define BREAKOUT_SKIN               0.0015f

//How many separate impacts one ball may resolve within a single tick. Three is the most a real
//game reaches (a brick, then the wall, then the brick beside it); six is slack for a corner.
#define BREAKOUT_MAX_RESOLUTIONS    6

//What the ball ran into, so one loop can find the earliest of everything and then decide what
//that MEANS - a brick and a wall are the same arithmetic and different consequences.
enum BreakoutHitKind{
    HIT_NONE = 0,
    HIT_WALL,
    HIT_CEILING,
    HIT_PADDLE,
    HIT_SHIELD,
    HIT_BRICK
};

static float SignOf(float v){
    return (v < 0.0f) ? -1.0f : 1.0f;
}

/*
    Swept circle against an axis-aligned box: the whole of this game's collision.

    The standard reduction - grow the box by the radius and cast a RAY at it - so the problem
    becomes the slab test and the answer is exact for the four faces. It is approximate at the
    four CORNERS, where the true swept shape is a quarter circle and this is a square: a ball
    clipping the very corner of a brick reports the face normal rather than the radial one, and so
    leaves a few degrees off where a perfectly round ball would. At a 0.4-unit radius against
    2x1 bricks that is invisible, and the exact version costs four quadratic solves per brick per
    substep. Written down because it IS an approximation, not because it has ever been noticed.

    Returns the entry time in 0..1 along (dx,dy) and the face normal, which by construction points
    back along the incoming motion - so a hit is never reported for a face the ball is leaving.
*/
static bool SweepCircleAABB(float px,float py,float dx,float dy,float radius,
                            float minx,float miny,float maxx,float maxy,
                            float& t_out,float& nx_out,float& ny_out){
    const float EPS = 1e-8f;

    minx -= radius;  miny -= radius;
    maxx += radius;  maxy += radius;

    float lo = 0.0f;
    float hi = 1.0f;
    float nx = 0.0f;
    float ny = 0.0f;

    if (fabsf(dx) < EPS){
        //Parallel to the x slab: either inside it for the whole sweep or never in it at all.
        if (px < minx || px > maxx){
            return false;
        }
    }else{
        float t1 = (minx - px) / dx;
        float t2 = (maxx - px) / dx;
        float face = -1.0f;         //entering through the LEFT face, so the normal points -x
        if (t1 > t2){
            float swap = t1; t1 = t2; t2 = swap;
            face = 1.0f;            //...or the right one
        }
        if (t1 > lo){ lo = t1; nx = face; ny = 0.0f; }
        if (t2 < hi){ hi = t2; }
        if (lo > hi){ return false; }
    }

    if (fabsf(dy) < EPS){
        if (py < miny || py > maxy){
            return false;
        }
    }else{
        float t1 = (miny - py) / dy;
        float t2 = (maxy - py) / dy;
        float face = -1.0f;
        if (t1 > t2){
            float swap = t1; t1 = t2; t2 = swap;
            face = 1.0f;
        }
        if (t1 > lo){ lo = t1; nx = 0.0f; ny = face; }
        if (t2 < hi){ hi = t2; }
        if (lo > hi){ return false; }
    }

    //lo == 0 with no face assigned means the ball STARTED inside the expanded box: there is no
    //entry to report and reflecting off an invented normal would fling it somewhere arbitrary.
    //The caller's skin offset is what keeps this from happening; the paddle, which can be driven
    //onto a ball by the mouse, has its own push-out instead.
    if ((nx == 0.0f) && (ny == 0.0f)){
        return false;
    }
    if (lo < 0.0f || lo > 1.0f){
        return false;
    }

    t_out = lo;
    nx_out = nx;
    ny_out = ny;
    return true;
}

//--- Level shapes ------------------------------------------------------------------------------

/*
    Five hand-drawn patterns that repeat, getting tougher each time round.

    Deliberately not random layouts: a paddle game's level is the thing the player reads and plans
    against, and noise reads as nothing. The RANDOM part is which bricks carry a prize, which is
    the part that should differ between two runs of the same level.
*/
void Field::BuildLevel(int level_number){
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        for (int c = 0; c < BREAKOUT_COLS; c++){
            bricks[r][c] = BRICK_EMPTY;
        }
    }

    int pattern  = (level_number - 1) % 5;
    //Every full cycle of the five promotes each brick one step up the toughness ladder.
    int hardness = (level_number - 1) / 5;

    switch (pattern){
        case 0:{    //A plain wall. Softest at the bottom, so the first thing the ball meets gives.
            for (int r = 2; r < BREAKOUT_ROWS; r++){
                for (int c = 0; c < BREAKOUT_COLS; c++){
                    bricks[r][c] = (int8_t)((r >= 6) ? BRICK_TOUGH : BRICK_NORMAL);
                }
            }
            break;
        }
        case 1:{    //Checkerboard: half the bricks, twice the angles, because the gaps let the
                    //ball through into the row above and it rattles around up there.
            for (int r = 1; r < BREAKOUT_ROWS; r++){
                for (int c = 0; c < BREAKOUT_COLS; c++){
                    if (((r + c) & 1) == 0){
                        bricks[r][c] = (int8_t)((r >= 5) ? BRICK_TOUGH : BRICK_NORMAL);
                    }
                }
            }
            break;
        }
        case 2:{    //A pyramid, widest at the top. The narrow bottom rows are the ones the ball
                    //reaches first, so the opening shot has to be aimed.
            for (int r = 0; r < BREAKOUT_ROWS; r++){
                int inset = (BREAKOUT_ROWS - 1 - r) / 2;
                for (int c = inset; c < BREAKOUT_COLS - inset; c++){
                    bricks[r][c] = (int8_t)((r >= 6) ? BRICK_ARMOURED : BRICK_NORMAL);
                }
            }
            break;
        }
        case 3:{    //Columns with indestructible caps. The caps are the point: they make the
                    //corridors between the columns the only way up, and a ball that gets into one
                    //clears the column from the inside.
            for (int c = 0; c < BREAKOUT_COLS; c++){
                if ((c & 1) != 0){
                    continue;
                }
                for (int r = 1; r < BREAKOUT_ROWS - 1; r++){
                    bricks[r][c] = (int8_t)((r >= 5) ? BRICK_TOUGH : BRICK_NORMAL);
                }
                bricks[BREAKOUT_ROWS - 1][c] = BRICK_SOLID;
            }
            break;
        }
        default:{   //A fortress: a solid shell with armour inside it and one gap in the floor.
            for (int r = 1; r < BREAKOUT_ROWS - 1; r++){
                for (int c = 1; c < BREAKOUT_COLS - 1; c++){
                    bool f_edge = (r == 1) || (r == BREAKOUT_ROWS - 2) ||
                                  (c == 1) || (c == BREAKOUT_COLS - 2);
                    if (f_edge){
                        bricks[r][c] = BRICK_SOLID;
                    }else{
                        bricks[r][c] = BRICK_ARMOURED;
                    }
                }
            }
            //The way in. Without it the fortress is unclearable, which is a different game.
            bricks[1][BREAKOUT_COLS / 2] = BRICK_NORMAL;
            break;
        }
    }

    //Promote everything destructible by one step per completed cycle, so level 6 is level 1 with
    //tough bricks and level 11 is level 1 with armour. SOLID stays solid: it is level geometry.
    for (int pass = 0; pass < hardness; pass++){
        for (int r = 0; r < BREAKOUT_ROWS; r++){
            for (int c = 0; c < BREAKOUT_COLS; c++){
                if (bricks[r][c] == BRICK_NORMAL){       bricks[r][c] = BRICK_TOUGH; }
                else if (bricks[r][c] == BRICK_TOUGH){   bricks[r][c] = BRICK_ARMOURED; }
            }
        }
    }

    //Prizes last, so they overwrite whatever a cell was going to be rather than being promoted
    //away by the loop above. Roughly one brick in eleven, and at least two per level however the
    //dice fall - a level with no way to earn a power-up is a worse level than a lucky one.
    int prizes = 0;
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        for (int c = 0; c < BREAKOUT_COLS; c++){
            if (bricks[r][c] == BRICK_EMPTY || bricks[r][c] == BRICK_SOLID){
                continue;
            }
            //0.09, not 9: RRandom::Roll takes a chance in 0..1. The hand-rolled generator this
            //replaced took a percentage, and passing 9 to this one clamps to 1.0 - which does not
            //fail, it just quietly turns every brick on the board into a prize.
            if (rng.Roll(0.09f)){
                bricks[r][c] = BRICK_PRIZE;
                prizes++;
            }
        }
    }
    for (int attempt = 0; (prizes < 2) && (attempt < 200); attempt++){
        int r = rng.GetInt(0,BREAKOUT_ROWS - 1);
        int c = rng.GetInt(0,BREAKOUT_COLS - 1);
        if (bricks[r][c] == BRICK_EMPTY || bricks[r][c] == BRICK_SOLID || bricks[r][c] == BRICK_PRIZE){
            continue;
        }
        bricks[r][c] = BRICK_PRIZE;
        prizes++;
    }

    debug->Info("Level %i built: pattern %i, hardness %i, %i bricks, %i prizes\n",
                level_number,pattern,hardness,BricksRemaining(),prizes);
}

//--- Setup -------------------------------------------------------------------------------------

Field::Field(){
    //Size the stream once, here, rather than per game: NewGame re-seeds it and RRandom::SetSeed
    //refills a private buffer in place at whatever size it already has. Without this first
    //Generate there would be no buffer for that to refill and every draw would return 0.
    rng.Generate(BREAKOUT_RANDOM_BYTES);
}

void Field::NewGame(uint32_t new_seed){
    seed = new_seed ? new_seed : 1;
    rng.SetSeed(seed);

    score = 0;
    lives = BREAKOUT_START_LIVES;
    level = 1;
    bricks_broken = 0;
    speed_bricks = 0;
    combo = 0;
    best_combo = 0;
    shield_charge = 1.0f;
    shield_flare_ticks = 0;
    shield_saves = 0;
    powerup_wide_ticks = 0;
    powerup_slow_ticks = 0;
    powerups_caught = 0;
    ticks_elapsed = 0;
    speed_override = 0.0f;
    resolution_overruns = 0;
    paddle_x = (BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f;
    paddle_vx = 0.0f;
    paddle_drive_v = 0.0f;

    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        balls[i].f_alive = false;
        balls[i].f_stuck = false;
    }

    BuildLevel(level);
    ServeBall();
    phase = BREAKOUT_PHASE_READY;
    phase_ticks = 0;
}

//Puts exactly one ball back on the paddle. Every other ball is cleared first: a serve is a fresh
//start, and a multiball still in flight when the last one died would be a ball the player never
//earned back.
void Field::ServeBall(){
    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        balls[i].f_alive = false;
        balls[i].f_stuck = false;
    }
    BreakoutBall& ball = balls[0];
    ball.f_alive = true;
    ball.f_stuck = true;
    //A hair off centre so the opening shot is not a perfectly symmetric problem every single time.
    ball.stuck_offset = (rng.GetInt(0,1) == 0) ? -0.55f : 0.55f;
    ball.x = paddle_x + ball.stuck_offset;
    ball.y = BREAKOUT_PADDLE_Y + BREAKOUT_PADDLE_H * 0.5f + BREAKOUT_BALL_RADIUS;
    ball.speed = CurrentBallSpeed();
    ball.vx = 0.0f;
    ball.vy = ball.speed;
}

//--- Queries -----------------------------------------------------------------------------------

int Field::GetBrick(int col, int row) const{
    if (col < 0 || col >= BREAKOUT_COLS || row < 0 || row >= BREAKOUT_ROWS){
        return BRICK_EMPTY;
    }
    return bricks[row][col];
}

int Field::BricksRemaining() const{
    int count = 0;
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        for (int c = 0; c < BREAKOUT_COLS; c++){
            //SOLID is scenery. Counting it would make every level with a cap unclearable.
            if (bricks[r][c] != BRICK_EMPTY && bricks[r][c] != BRICK_SOLID){
                count++;
            }
        }
    }
    return count;
}

int Field::LiveBalls() const{
    int count = 0;
    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        if (balls[i].f_alive){
            count++;
        }
    }
    return count;
}

float Field::BrickCenterX(int col) const{
    return BREAKOUT_FIELD_LEFT + (col + 0.5f) * BREAKOUT_BRICK_W;
}

float Field::BrickCenterY(int row) const{
    return BREAKOUT_BRICK_BASE_Y + (row + 0.5f) * BREAKOUT_BRICK_H;
}

float Field::PaddleHalfWidth() const{
    return ((powerup_wide_ticks > 0) ? BREAKOUT_PADDLE_W_WIDE : BREAKOUT_PADDLE_W) * 0.5f;
}

float Field::CurrentBallSpeed() const{
    //The probe pins the speed so it can measure the collision test rather than the ramp.
    if (speed_override > 0.0f){
        return speed_override;
    }
    float speed = BREAKOUT_BALL_SPEED_BASE
                + (level - 1) * 0.8f
                + speed_bricks * BREAKOUT_BALL_SPEED_PER_BRICK;
    if (powerup_slow_ticks > 0){
        speed *= 0.68f;
    }
    return clamp(speed,BREAKOUT_BALL_SPEED_BASE * 0.6f,BREAKOUT_BALL_SPEED_MAX);
}

std::vector<std::string> Field::ToAsciiRows() const{
    //Top row first, because this exists to be LOOKED at - in a log line or an MCP reply - and a
    //picture of the board that is upside down is worse than no picture.
    static const char glyphs[BRICK_TYPE_COUNT] = { 'o','O','@','*','#' };
    std::vector<std::string> rows;
    for (int r = BREAKOUT_ROWS - 1; r >= 0; r--){
        std::string line;
        for (int c = 0; c < BREAKOUT_COLS; c++){
            int8_t type = bricks[r][c];
            if (type < 0 || type >= BRICK_TYPE_COUNT){
                line += '.';
            }else{
                line += glyphs[type];
            }
        }
        rows.push_back(line);
    }
    return rows;
}

//--- The paddle --------------------------------------------------------------------------------

void Field::UpdatePaddle(const BreakoutInput& in){
    float previous_x = paddle_x;

    //The throttle half: a key or a stick asks for a velocity and the paddle takes a few ticks to
    //get there, which is what stops a digital key feeling like a teleport.
    float target_v = clamp(in.paddle_axis,-1.0f,1.0f) * BREAKOUT_PADDLE_SPEED;
    paddle_drive_v = flerp(paddle_drive_v,target_v,BREAKOUT_PADDLE_RESPONSE);

    //The displacement half: the mouse is already a position and must not be smoothed, or the
    //pointer and the paddle drift apart and never reconverge.
    paddle_x += paddle_drive_v * BREAKOUT_DT + in.paddle_delta;

    float half = PaddleHalfWidth();
    float lo = BREAKOUT_FIELD_LEFT + half;
    float hi = BREAKOUT_FIELD_RIGHT - half;
    if (paddle_x < lo){
        paddle_x = lo;
        if (paddle_drive_v < 0.0f){ paddle_drive_v = 0.0f; }
    }
    if (paddle_x > hi){
        paddle_x = hi;
        if (paddle_drive_v > 0.0f){ paddle_drive_v = 0.0f; }
    }

    //MEASURED, not requested. The ball's "English" then comes out the same whichever device moved
    //the paddle, and a paddle pinned against a wall imparts nothing, which is correct.
    paddle_vx = (paddle_x - previous_x) / BREAKOUT_DT;
}

/*
    The designed bounce, and the reason this game does not hand its ball to the solver.

    Where the ball struck the paddle decides the angle it leaves at, full stop - the incoming
    direction is discarded. That is not physical and it is not meant to be: it is what makes a
    paddle a STEERING WHEEL rather than a wall, and every game in this genre since 1976 has done
    it. A rigid-body solver reflects about the contact normal and cannot express it.
*/
void Field::BounceOffPaddle(BreakoutBall& ball, float offset, BreakoutEvents* events){
    offset = clamp(offset,-1.0f,1.0f);
    float angle = offset * BREAKOUT_PADDLE_MAX_DEFLECT;   //0 = straight up, +/- = out to the side

    ball.speed = CurrentBallSpeed();
    ball.vx = sinf(angle) * ball.speed;
    ball.vy = cosf(angle) * ball.speed;

    //A moving paddle drags the ball with it a little. Small, because at full strength it lets a
    //player sweep the ball sideways at will and the offset above stops meaning anything.
    ball.vx += paddle_vx * BREAKOUT_PADDLE_ENGLISH;

    ConditionVelocity(ball);

    //Recorded here, where it is still the paddle's answer and nothing else's - see the comment on
    //BreakoutEvents::paddle_hit_angle.
    if (events){
        events->paddle_hit_angle = atan2f(ball.vx,ball.vy) * 180.0f / 3.14159265359f;
    }
}

void Field::ConditionVelocity(BreakoutBall& ball){
    float length = sqrtf(ball.vx * ball.vx + ball.vy * ball.vy);
    if (length < 1e-5f){
        //Nothing sensible to preserve a direction from; send it up.
        ball.vx = 0.0f;
        ball.vy = 1.0f;
        length = 1.0f;
    }
    float ux = ball.vx / length;
    float uy = ball.vy / length;

    if (fabsf(uy) < BREAKOUT_MIN_UY){
        uy = SignOf(uy) * BREAKOUT_MIN_UY;
        ux = SignOf(ux) * sqrtf(max(0.0f,1.0f - uy * uy));
    }
    if (fabsf(ux) < BREAKOUT_MIN_UX){
        ux = SignOf(ux) * BREAKOUT_MIN_UX;
        uy = SignOf(uy) * sqrtf(max(0.0f,1.0f - ux * ux));
    }

    //Speed is re-asserted rather than preserved: rebounds here are perfectly elastic by
    //construction, and the ramp (level, bricks broken, the slow power-up) then takes effect the
    //tick it changes instead of waiting for the next paddle hit.
    ball.speed = CurrentBallSpeed();
    ball.vx = ux * ball.speed;
    ball.vy = uy * ball.speed;
}

//--- Bricks ------------------------------------------------------------------------------------

void Field::HitBrick(int col, int row, float nx, float ny, BreakoutEvents& events){
    int8_t before = bricks[row][col];
    if (before == BRICK_EMPTY || before == BRICK_SOLID){
        //SOLID still reports the hit so the app can spark it, but nothing else happens.
        if (before == BRICK_SOLID){
            BreakoutBrickHit hit;
            hit.col = col;
            hit.row = row;
            hit.type_before = BRICK_SOLID;
            hit.f_destroyed = false;
            hit.x = BrickCenterX(col);
            hit.y = BrickCenterY(row);
            hit.nx = nx;
            hit.ny = ny;
            events.brick_hits.push_back(hit);
        }
        return;
    }

    //Toughness is expressed as a type that degrades rather than as a hit counter, so the board
    //array stays the single source of truth and the view needs nothing but the type to colour a
    //brick correctly.
    int8_t after = BRICK_EMPTY;
    if (before == BRICK_ARMOURED){      after = BRICK_TOUGH; }
    else if (before == BRICK_TOUGH){    after = BRICK_NORMAL; }

    bricks[row][col] = after;
    bool f_destroyed = (after == BRICK_EMPTY);

    BreakoutBrickHit hit;
    hit.col = col;
    hit.row = row;
    hit.type_before = before;
    hit.f_destroyed = f_destroyed;
    hit.x = BrickCenterX(col);
    hit.y = BrickCenterY(row);
    hit.nx = nx;
    hit.ny = ny;
    events.brick_hits.push_back(hit);

    if (!f_destroyed){
        //A hit that only cracks a brick is still worth something, but not worth a combo step -
        //otherwise the armoured levels hand out multipliers for free.
        score += 10;
        return;
    }

    bricks_broken++;
    speed_bricks++;
    combo = min(combo + 1,9);
    best_combo = max(best_combo,combo);
    events.combo = combo;

    static const int brick_value[BRICK_TYPE_COUNT] = { 50, 80, 130, 70, 0 };
    score += brick_value[before] * combo;

    //Breaking things charges the shield. The player who is doing well keeps being saved, which is
    //the direction a mercy mechanic should lean - it rewards the run that is already going, and a
    //player who is struggling was going to lose the ball anyway.
    shield_charge = clamp(shield_charge + BREAKOUT_SHIELD_PER_BRICK,0.0f,1.0f);

    if (before == BRICK_PRIZE){
        events.f_prize_dropped = true;
        events.prize_x = hit.x;
        events.prize_y = hit.y;
    }
}

//--- One ball, one tick -------------------------------------------------------------------------

void Field::StepBall(BreakoutBall& ball, BreakoutEvents& events){
    if (!ball.f_alive){
        return;
    }
    if (ball.f_stuck){
        //Riding the paddle. Re-derived from the paddle rather than moved with it, so a paddle
        //clamped against a wall does not drag the ball off the end of it.
        float half = PaddleHalfWidth();
        ball.stuck_offset = clamp(ball.stuck_offset,-half * 0.7f,half * 0.7f);
        ball.x = paddle_x + ball.stuck_offset;
        ball.y = BREAKOUT_PADDLE_Y + BREAKOUT_PADDLE_H * 0.5f + BREAKOUT_BALL_RADIUS;
        return;
    }

    ConditionVelocity(ball);

    const float radius = BREAKOUT_BALL_RADIUS;
    const float paddle_half = PaddleHalfWidth();
    const float paddle_min_y = BREAKOUT_PADDLE_Y - BREAKOUT_PADDLE_H * 0.5f;
    const float paddle_max_y = BREAKOUT_PADDLE_Y + BREAKOUT_PADDLE_H * 0.5f;
    //The shield only exists as a surface while there is enough charge to pay for a save. An
    //exhausted shield is a pane of glass the ball goes straight through, which is exactly what
    //the shader draws.
    const bool f_shield_live = (shield_charge >= BREAKOUT_SHIELD_SAVE_COST);

    float remaining = BREAKOUT_DT;

    for (int iteration = 0; (iteration < BREAKOUT_MAX_RESOLUTIONS) && (remaining > 1e-7f); iteration++){
        float dx = ball.vx * remaining;
        float dy = ball.vy * remaining;

        int   kind = HIT_NONE;
        float best_t = 1.0f;
        float best_nx = 0.0f;
        float best_ny = 0.0f;
        int   best_col = -1;
        int   best_row = -1;

        //--- the three walls. Planes rather than boxes: exact, and three lines each. ----------
        if (dx < 0.0f){
            float t = (BREAKOUT_FIELD_LEFT + radius - ball.x) / dx;
            if (t >= 0.0f && t < best_t){
                best_t = t; best_nx = 1.0f; best_ny = 0.0f; kind = HIT_WALL;
            }
        }else if (dx > 0.0f){
            float t = (BREAKOUT_FIELD_RIGHT - radius - ball.x) / dx;
            if (t >= 0.0f && t < best_t){
                best_t = t; best_nx = -1.0f; best_ny = 0.0f; kind = HIT_WALL;
            }
        }
        if (dy > 0.0f){
            float t = (BREAKOUT_FIELD_TOP - radius - ball.y) / dy;
            if (t >= 0.0f && t < best_t){
                best_t = t; best_nx = 0.0f; best_ny = -1.0f; kind = HIT_CEILING;
            }
        }

        //--- the shield -----------------------------------------------------------------------
        if (f_shield_live && (dy < 0.0f)){
            float t = (BREAKOUT_SHIELD_Y + radius - ball.y) / dy;
            if (t >= 0.0f && t < best_t){
                best_t = t; best_nx = 0.0f; best_ny = 1.0f; kind = HIT_SHIELD;
            }
        }

        //--- the paddle -----------------------------------------------------------------------
        //Only from ABOVE, deliberately. A ball can legitimately be underneath the paddle - the
        //shield sits below it and throws balls back up - and a two-sided paddle would then trap
        //one in the gap, bouncing between the paddle's underside and the shield forever. A paddle
        //the ball passes up through and lands on top of is the arcade behaviour anyway.
        if (ball.y >= BREAKOUT_PADDLE_Y){
            float t = 0.0f;
            float nx = 0.0f;
            float ny = 0.0f;
            if (SweepCircleAABB(ball.x,ball.y,dx,dy,radius,
                                paddle_x - paddle_half,paddle_min_y,
                                paddle_x + paddle_half,paddle_max_y,t,nx,ny)){
                if (t < best_t){
                    best_t = t; best_nx = nx; best_ny = ny; kind = HIT_PADDLE;
                }
            }
        }

        //--- the bricks -----------------------------------------------------------------------
        //Only the cells the swept ball's bounding box actually touches. A full 88-cell scan would
        //also be fast enough; the range is here because it makes the cost independent of how big
        //the wall is, which matters the moment somebody widens the board.
        {
            float sweep_min_x = min(ball.x,ball.x + dx) - radius;
            float sweep_max_x = max(ball.x,ball.x + dx) + radius;
            float sweep_min_y = min(ball.y,ball.y + dy) - radius;
            float sweep_max_y = max(ball.y,ball.y + dy) + radius;

            int col_lo = (int)floorf((sweep_min_x - BREAKOUT_FIELD_LEFT) / BREAKOUT_BRICK_W);
            int col_hi = (int)floorf((sweep_max_x - BREAKOUT_FIELD_LEFT) / BREAKOUT_BRICK_W);
            int row_lo = (int)floorf((sweep_min_y - BREAKOUT_BRICK_BASE_Y) / BREAKOUT_BRICK_H);
            int row_hi = (int)floorf((sweep_max_y - BREAKOUT_BRICK_BASE_Y) / BREAKOUT_BRICK_H);

            col_lo = max(col_lo,0);
            row_lo = max(row_lo,0);
            col_hi = min(col_hi,BREAKOUT_COLS - 1);
            row_hi = min(row_hi,BREAKOUT_ROWS - 1);

            for (int r = row_lo; r <= row_hi; r++){
                for (int c = col_lo; c <= col_hi; c++){
                    if (bricks[r][c] == BRICK_EMPTY){
                        continue;
                    }
                    float bx = BREAKOUT_FIELD_LEFT + c * BREAKOUT_BRICK_W;
                    float by = BREAKOUT_BRICK_BASE_Y + r * BREAKOUT_BRICK_H;
                    float t = 0.0f;
                    float nx = 0.0f;
                    float ny = 0.0f;
                    if (!SweepCircleAABB(ball.x,ball.y,dx,dy,radius,
                                         bx,by,bx + BREAKOUT_BRICK_W,by + BREAKOUT_BRICK_H,t,nx,ny)){
                        continue;
                    }
                    if (t < best_t){
                        best_t = t; best_nx = nx; best_ny = ny;
                        best_col = c; best_row = r;
                        kind = HIT_BRICK;
                    }
                }
            }
        }

        if (kind == HIT_NONE){
            ball.x += dx;
            ball.y += dy;
            break;
        }

        //Advance to the impact, then step off the surface so the next sweep of this same tick
        //does not immediately find the face it just left at t = 0.
        ball.x += dx * best_t + best_nx * BREAKOUT_SKIN;
        ball.y += dy * best_t + best_ny * BREAKOUT_SKIN;
        remaining *= (1.0f - best_t);

        switch (kind){
            case HIT_WALL:
            case HIT_CEILING:{
                //Mirror about the face normal. For an axis-aligned face this is one sign flip,
                //but the general form keeps the four cases from drifting apart.
                float dot = ball.vx * best_nx + ball.vy * best_ny;
                ball.vx -= 2.0f * dot * best_nx;
                ball.vy -= 2.0f * dot * best_ny;
                if (kind == HIT_CEILING){ events.f_bounced_ceiling = true; }
                else                    { events.f_bounced_wall = true; }
                break;
            }
            case HIT_BRICK:{
                float dot = ball.vx * best_nx + ball.vy * best_ny;
                ball.vx -= 2.0f * dot * best_nx;
                ball.vy -= 2.0f * dot * best_ny;
                HitBrick(best_col,best_row,best_nx,best_ny,events);
                break;
            }
            case HIT_PADDLE:{
                if (best_ny > 0.5f){
                    //Landed on the top face: the designed bounce.
                    float offset = (ball.x - paddle_x) / max(paddle_half,0.001f);
                    events.f_bounced_paddle = true;
                    events.paddle_hit_offset = clamp(offset,-1.0f,1.0f);
                    BounceOffPaddle(ball,offset,&events);
                }else{
                    //Clipped an END of the paddle. Reflecting sideways here rather than applying
                    //the steering rule is deliberate: a ball that caught the corner should be
                    //punished with an awkward angle, not rewarded with a perfect one.
                    float dot = ball.vx * best_nx + ball.vy * best_ny;
                    ball.vx -= 2.0f * dot * best_nx;
                    ball.vy -= 2.0f * dot * best_ny;
                    ball.vx += paddle_vx * BREAKOUT_PADDLE_ENGLISH;
                    events.f_bounced_paddle = true;
                }
                //Touching the paddle ends the combo. That is what makes a combo a thing you go
                //looking for - a rally that stays up among the bricks - rather than a counter that
                //only ever goes up.
                combo = 0;
                break;
            }
            case HIT_SHIELD:{
                ball.vy = fabsf(ball.vy);
                shield_charge = clamp(shield_charge - BREAKOUT_SHIELD_SAVE_COST,0.0f,1.0f);
                shield_flare_ticks = BREAKOUT_SHIELD_FLARE_TICKS;
                shield_saves++;
                events.f_shield_saved = true;
                events.shield_impact_x = ball.x;
                break;
            }
            default:
                break;
        }

        ConditionVelocity(ball);

        //The last permitted resolution still left travel on the clock: the ball has hit six
        //things in one tick and the rest of its step is being dropped. Counted, never hidden.
        if ((iteration == BREAKOUT_MAX_RESOLUTIONS - 1) && (remaining > 1e-5f)){
            resolution_overruns++;
        }
    }

    //Past the death line with no shield left to catch it.
    if (ball.y + radius < BREAKOUT_DEATH_Y){
        ball.f_alive = false;
        events.f_ball_lost = true;
        if (!f_shield_live){
            events.f_shield_empty = true;
        }
    }
}

//--- Power-ups ----------------------------------------------------------------------------------

void Field::ApplyPowerup(int kind){
    powerups_caught++;
    switch (kind){
        case POWERUP_WIDE_PADDLE:
            powerup_wide_ticks = BREAKOUT_POWERUP_TICKS;
            break;
        case POWERUP_SLOW_BALL:
            powerup_slow_ticks = BREAKOUT_POWERUP_TICKS;
            break;
        case POWERUP_SHIELD:
            shield_charge = clamp(shield_charge + 0.5f,0.0f,1.0f);
            break;
        case POWERUP_MULTIBALL:{
            //Split whichever ball is furthest along into as many as there is room for. Each copy
            //leaves at a fixed angle from the original rather than a random one, so a replay of
            //the same inputs produces the same three balls.
            BreakoutBall* source = NULL;
            for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
                if (balls[i].f_alive && !balls[i].f_stuck){
                    source = &balls[i];
                    break;
                }
            }
            if (!source){
                //Nothing in flight to split - the ball is still on the paddle. Bank it as shield
                //charge instead of silently doing nothing.
                shield_charge = clamp(shield_charge + 0.3f,0.0f,1.0f);
                break;
            }
            float base_angle = atan2f(source->vx,source->vy);   //from straight up, +x to the right
            int spawned = 0;
            for (int i = 0; (i < BREAKOUT_MAX_BALLS) && (spawned < 2); i++){
                if (balls[i].f_alive){
                    continue;
                }
                float angle = base_angle + ((spawned == 0) ? 0.45f : -0.45f);
                balls[i].f_alive = true;
                balls[i].f_stuck = false;
                balls[i].x = source->x;
                balls[i].y = source->y;
                balls[i].speed = source->speed;
                balls[i].vx = sinf(angle) * source->speed;
                balls[i].vy = cosf(angle) * source->speed;
                ConditionVelocity(balls[i]);
                spawned++;
            }
            break;
        }
        default:
            break;
    }
    debug->Info("Power-up %i applied\n",kind);
}

//--- The tick -----------------------------------------------------------------------------------

void Field::Tick(const BreakoutInput& in, BreakoutEvents& events){
    events = BreakoutEvents();
    ticks_elapsed++;
    phase_ticks++;

    if (shield_flare_ticks > 0){        shield_flare_ticks--; }
    if (powerup_slow_ticks > 0){        powerup_slow_ticks--; }
    if (powerup_wide_ticks > 0){        powerup_wide_ticks--; }

    //The paddle keeps moving in every phase but game over, so the player can line up a serve
    //while the "ball lost" beat plays out.
    if (phase != BREAKOUT_PHASE_GAMEOVER){
        UpdatePaddle(in);
    }

    switch (phase){
        case BREAKOUT_PHASE_READY:{
            for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
                StepBall(balls[i],events);
            }
            if (in.f_launch){
                for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
                    if (!balls[i].f_alive || !balls[i].f_stuck){
                        continue;
                    }
                    balls[i].f_stuck = false;
                    //Launched at the angle the ball's resting offset implies, so where the player
                    //parked the paddle before pressing the key already aims the shot.
                    float half = PaddleHalfWidth();
                    BounceOffPaddle(balls[i],balls[i].stuck_offset / max(half,0.001f),NULL);
                }
                phase = BREAKOUT_PHASE_PLAYING;
                phase_ticks = 0;
                events.f_launched = true;
            }
            break;
        }
        case BREAKOUT_PHASE_PLAYING:{
            for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
                StepBall(balls[i],events);
            }

            if (BricksRemaining() == 0){
                phase = BREAKOUT_PHASE_LEVEL_CLEARED;
                phase_ticks = 0;
                events.f_level_cleared = true;
                //A clear is worth something on its own, and worth more the fewer lives it cost.
                score += 1000 + lives * 250;
                break;
            }

            if (LiveBalls() == 0){
                lives--;
                combo = 0;
                speed_bricks = 0;
                //Timed power-ups die with the ball. Carrying a wide paddle through a life makes
                //the penalty for losing one almost nothing.
                powerup_wide_ticks = 0;
                powerup_slow_ticks = 0;
                events.f_life_lost = true;
                if (lives <= 0){
                    phase = BREAKOUT_PHASE_GAMEOVER;
                    events.f_game_over = true;
                }else{
                    phase = BREAKOUT_PHASE_LOST_BALL;
                }
                phase_ticks = 0;
            }
            break;
        }
        case BREAKOUT_PHASE_LOST_BALL:{
            if (phase_ticks >= BREAKOUT_SERVE_DELAY_TICKS){
                //A fresh ball comes with a fresh half-shield, so a run that emptied it is not
                //immediately lost again on the next mistake.
                shield_charge = max(shield_charge,0.5f);
                ServeBall();
                phase = BREAKOUT_PHASE_READY;
                phase_ticks = 0;
            }
            break;
        }
        case BREAKOUT_PHASE_LEVEL_CLEARED:{
            //Balls keep flying through the celebration - there is nothing left to hit, and a
            //frozen ball on a cleared board looks like a hang.
            for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
                StepBall(balls[i],events);
            }
            if (phase_ticks >= BREAKOUT_CLEARED_TICKS){
                level++;
                BuildLevel(level);
                shield_charge = clamp(shield_charge + 0.35f,0.0f,1.0f);
                ServeBall();
                phase = BREAKOUT_PHASE_READY;
                phase_ticks = 0;
            }
            break;
        }
        case BREAKOUT_PHASE_GAMEOVER:
        default:
            break;
    }
}
