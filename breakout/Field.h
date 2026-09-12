#ifndef _BREAKOUT_FIELD_H_
#define _BREAKOUT_FIELD_H_

#include <stdint.h>
#include <vector>
#include <string>
#include "RRandom.h"

/*
    The rules of the paddle game, and nothing else.

    No engine type appears in this header on purpose - not Object, not Renderer, not vec3. The
    one exception is RRandom, which is the engine's reproducible generator and is exactly what a
    seeded rules layer wants; it earns its place by depending on nothing but the vector types.
    (It used to hold a Texture, and so drag in glad.h, which is why this file once carried its own
    copy of the same xorshift32 RRandom::Generate uses.) The whole game is a pure function of
    (previous state, this tick's input), which
    is what lets it be reasoned about with no window on the screen and what would let it be
    replayed. The Application owns the other half: turning keys, a stick and a mouse into the
    intent below, and turning these numbers into cubes, light and sound.

    --- WHY THE GEOMETRY IS IN HERE --------------------------------------------------------------
    Unlike a Tetris board, a paddle game's rules ARE geometry: the interesting part of the
    simulation is a circle sweeping against rectangles, and that has to happen somewhere. So the
    field's dimensions are defined here, in world units, and the app draws what these describe
    rather than the other way round. Board coordinates are world coordinates, which is the same
    trick tetris/Playfield plays with its integer grid and costs nothing.

    --- WHY THE BALL IS INTEGRATED HERE RATHER THAN BY reactphysics3d -----------------------------
    See docs/breakout_findings.md 8.1 for the measurement behind this, but the short version: the
    single most important parameter in this genre is the angle the ball leaves the paddle at, and
    in the classic that angle is a DESIGNED response to where on the paddle it struck, not a
    physical one. A solver cannot express it. So the ball is swept by hand here, and the physics
    engine is given the jobs it is genuinely better at - the debris a brick bursts into, and the
    power-up capsules that fall and have to be caught.

    Every duration is a count of SIMULATION TICKS. The app runs at 60 of them a second.
*/

//--- The field, in world units ----------------------------------------------------------------
//The play area is the XY plane at z = 0, x to the right and y up, so a brick's grid position and
//its position in the world are the same arithmetic.
#define BREAKOUT_FIELD_LEFT         0.0f
#define BREAKOUT_FIELD_RIGHT        22.0f
#define BREAKOUT_FIELD_TOP          27.0f
//Below this the ball is gone. The shield stands a little above it - a ball crossing the shield
//line with charge in the bank is thrown back, and only an uncharged shield lets it through.
#define BREAKOUT_DEATH_Y            0.0f
#define BREAKOUT_SHIELD_Y           1.10f

#define BREAKOUT_COLS               11
#define BREAKOUT_ROWS               8
#define BREAKOUT_BRICK_W            2.0f
#define BREAKOUT_BRICK_H            1.0f
//Row 0 is the BOTTOM row of the wall, so row r spans y = BASE + r .. BASE + r + 1. Counting up
//matches the world's +Y and means nothing has to be flipped anywhere except the ASCII dump,
//which prints top row first because that is how a person reads a picture of the board.
#define BREAKOUT_BRICK_BASE_Y       17.0f

#define BREAKOUT_PADDLE_Y           2.60f
#define BREAKOUT_PADDLE_H           0.50f
#define BREAKOUT_PADDLE_W           3.60f
#define BREAKOUT_PADDLE_W_WIDE      6.00f
#define BREAKOUT_BALL_RADIUS        0.40f

/*
    The tick rate, and the one number in here the rules cannot work out for themselves.

    A grid game denominates everything in ticks and never needs a timestep at all; a ball sweeping
    across a field does, because its position is continuous. The rules have no engine types, so
    they cannot ask Scene::GetPhysicsTimestep() what the rate is - it has to be told, and the app
    must call SetPhysicsTPS with the same number. A static_assert cannot check that either.
    Sixty rather than the engine's default fifty, because the paddle is the most latency-sensitive
    control in the repo and 16.7 ms of input granularity is noticeably better than 20.
*/
#define BREAKOUT_TPS                60.0f
#define BREAKOUT_DT                 (1.0f / BREAKOUT_TPS)

//--- Feel -------------------------------------------------------------------------------------
//How fast the paddle can be driven by a digital key or a fully deflected stick, in units/second,
//and how quickly it reaches that (0..1, a per-tick lerp). The mouse bypasses both: it moves the
//paddle directly, and paddle_vx is then measured from where the paddle actually went, so every
//input device imparts the same "English" to the ball by the same rule.
#define BREAKOUT_PADDLE_SPEED       26.0f
#define BREAKOUT_PADDLE_RESPONSE    0.35f

#define BREAKOUT_BALL_SPEED_BASE    15.0f
#define BREAKOUT_BALL_SPEED_MAX     30.0f
//Each brick broken nudges the ball along a little, so a level that starts gentle does not end
//that way. Reset on a new ball, not on a new level - losing a ball is the relief.
#define BREAKOUT_BALL_SPEED_PER_BRICK 0.10f
//A ball travelling too flat never comes back down to the paddle; a ball travelling dead vertical
//bounces between two points forever and stops being a game. Both are clamped as floors on the
//two components of the unit direction, which is cheaper than an angle and easier to reason about.
#define BREAKOUT_MIN_UY             0.24f
#define BREAKOUT_MIN_UX             0.12f
//How far off vertical the paddle's edge throws the ball. This is the designed response the whole
//"integrate it by hand" decision exists to make possible.
#define BREAKOUT_PADDLE_MAX_DEFLECT 1.12f
//How much of the paddle's own movement is added to the ball's horizontal velocity on a hit.
#define BREAKOUT_PADDLE_ENGLISH     0.22f

//--- The shield -------------------------------------------------------------------------------
//A gameplay resource, and the thing the custom shader draws. It starts full, one save costs a
//chunk of it, and breaking bricks charges it back up - so the player who is doing well gets to
//keep being saved, which is the direction a mercy mechanic should lean.
#define BREAKOUT_SHIELD_SAVE_COST   0.34f
#define BREAKOUT_SHIELD_PER_BRICK   0.022f
//Ticks the shield flares for after a save, for the shader and the sound.
#define BREAKOUT_SHIELD_FLARE_TICKS 45

//--- Timings, all in ticks --------------------------------------------------------------------
#define BREAKOUT_SERVE_DELAY_TICKS  40  //after a ball is lost, before the next one is on the paddle
#define BREAKOUT_CLEARED_TICKS      110 //the pause after the last brick, before the next level
#define BREAKOUT_COMBO_TICKS        90  //a second brick within this many ticks continues the combo

//--- Brick types ------------------------------------------------------------------------------
#define BRICK_EMPTY                 -1
#define BRICK_NORMAL                0   //one hit
#define BRICK_TOUGH                 1   //two hits: degrades to NORMAL
#define BRICK_ARMOURED              2   //three hits: degrades to TOUGH
#define BRICK_PRIZE                 3   //one hit, and drops a power-up capsule
#define BRICK_SOLID                 4   //never breaks; part of the level's shape
#define BRICK_TYPE_COUNT            5

//--- Power-ups --------------------------------------------------------------------------------
//The capsule itself is a rigid body the app owns and the solver drops; these are only what
//catching one MEANS. See ApplicationBreakout::UpdatePowerups.
#define POWERUP_WIDE_PADDLE         0
#define POWERUP_SLOW_BALL           1
#define POWERUP_SHIELD              2
#define POWERUP_MULTIBALL           3
#define POWERUP_COUNT               4
//How long the two timed ones last, in ticks.
#define BREAKOUT_POWERUP_TICKS      600

#define BREAKOUT_MAX_BALLS          3
#define BREAKOUT_START_LIVES        3

//What the game is doing. Everything that takes time is a phase with a tick counter rather than a
//flag plus a timestamp, so "what is the game doing" has exactly one answer.
enum BreakoutPhase{
    BREAKOUT_PHASE_READY = 0,       //a ball is stuck to the paddle, waiting to be launched
    BREAKOUT_PHASE_PLAYING,
    BREAKOUT_PHASE_LOST_BALL,       //the beat after the last ball died
    BREAKOUT_PHASE_LEVEL_CLEARED,   //the beat after the last brick
    BREAKOUT_PHASE_GAMEOVER
};

/*
    One tick's worth of intent.

    Two ways of driving the paddle, deliberately, because the genre has always had both and they
    are not the same shape. `paddle_axis` is a THROTTLE: a key or a stick asking for a velocity,
    which the rules feed through an acceleration model so a key press has weight. `paddle_delta`
    is a DISPLACEMENT: the mouse, which is already a position and must not be smoothed or the
    pointer and the paddle come apart. They add, so a player may use both at once.
*/
struct BreakoutInput{
    float paddle_axis = 0.0f;       //-1..+1
    float paddle_delta = 0.0f;      //world units this tick, straight from the mouse
    bool  f_launch = false;
};

//One brick that was struck this tick. The app hangs sound, debris and score popups off these.
struct BreakoutBrickHit{
    int   col = 0;
    int   row = 0;
    int   type_before = BRICK_EMPTY;    //what it was when it was struck
    bool  f_destroyed = false;          //...and whether that was the end of it
    float x = 0.0f;                     //world centre, so the app does not repeat the arithmetic
    float y = 0.0f;
    float nx = 0.0f;                    //the face the ball came in on, for the debris burst
    float ny = 0.0f;
};

//What happened during a tick, for the app to hang sound and effects off. A consequence of the
//simulation, never a command into it - see core/SimCommand.h on that distinction.
struct BreakoutEvents{
    bool f_bounced_wall = false;
    bool f_bounced_ceiling = false;
    bool f_bounced_paddle = false;
    bool f_launched = false;
    bool f_shield_saved = false;    //the shield threw a ball back
    bool f_shield_empty = false;    //...or could not, and that is why a ball died
    bool f_ball_lost = false;       //a ball died; not necessarily a life (see f_life_lost)
    bool f_life_lost = false;       //the LAST ball died
    bool f_level_cleared = false;
    bool f_game_over = false;
    bool f_prize_dropped = false;
    float prize_x = 0.0f;
    float prize_y = 0.0f;
    float shield_impact_x = 0.0f;   //where the shield was struck, for the ripple
    float paddle_hit_offset = 0.0f; //-1..+1 across the paddle, for the sound's pitch and the HUD
    /*
        The direction the ball left the paddle at, in degrees from straight up, captured AT THE
        BOUNCE rather than at the end of the tick.

        That distinction is not pedantry: a ball that leaves the paddle at +48 degrees near the
        right wall can legitimately reach that wall and reflect to -48 within the same tick, so
        anything reading the velocity afterwards sees the wall's answer and reports the paddle's
        response as having the wrong sign. Measured exactly that way before this field existed.
        Only meaningful when f_bounced_paddle is set.
    */
    float paddle_hit_angle = 0.0f;
    int   combo = 0;                //the combo this tick's bricks scored at
    std::vector<BreakoutBrickHit> brick_hits;
};

//One ball. Several, because a power-up splits it - and because "all the balls are dead" is a
//cleaner losing condition than a special case bolted onto one ball.
struct BreakoutBall{
    bool  f_alive = false;
    bool  f_stuck = false;      //riding the paddle, waiting for a launch
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;            //direction * speed; the pair is re-normalised every tick so the
    float vy = 0.0f;            //  ball can never lose energy to a glancing bounce
    float speed = BREAKOUT_BALL_SPEED_BASE;
    float stuck_offset = 0.0f;  //where along the paddle it sits while stuck
};

/*
    Bytes of noise the level generator and the ball serve draw from. A level spends one Roll per
    brick (88) plus a handful of retries, and a serve one draw - so a few hundred bytes a level
    against 16 KB here, which is thousands of levels before the stream wraps and repeats
    (see core/RRandom.h).
*/
#define BREAKOUT_RANDOM_BYTES 16384

class Field{
public:
    Field();

    //--- Driving it ---------------------------------------------------------------------------
    void NewGame(uint32_t seed);
    //Exactly one simulation tick. `events` is cleared first, so a caller never has to.
    void Tick(const BreakoutInput& in, BreakoutEvents& events);
    //Catching a capsule. Called from inside the tick by the app, once the solver has reported the
    //capsule touching the paddle - so it is still simulation, just simulation the app resolved.
    void ApplyPowerup(int kind);

    //--- Reading it ---------------------------------------------------------------------------
    int  GetBrick(int col, int row) const;
    int  BricksRemaining() const;               //destructible bricks only: SOLID never counts
    int  LiveBalls() const;
    //The board as ROWS strings of COLS characters, TOP row first, for the MCP tools and the log.
    //'.' empty, 'o' normal, 'O' tough, '@' armoured, '*' prize, '#' solid.
    std::vector<std::string> ToAsciiRows() const;

    //Where a brick is in the world. The one place this arithmetic lives.
    float BrickCenterX(int col) const;
    float BrickCenterY(int row) const;
    float PaddleHalfWidth() const;

    //--- The state ----------------------------------------------------------------------------
    //Public like tetris/Playfield's, and for the same reason: the app is the view and reads all
    //of it every tick. It is written only from Tick() and the two calls above, all of which run
    //on the physics thread.
    int8_t bricks[BREAKOUT_ROWS][BREAKOUT_COLS] = {};
    BreakoutBall balls[BREAKOUT_MAX_BALLS];

    float paddle_x = 11.0f;
    float paddle_vx = 0.0f;         //measured from where the paddle actually went, not requested
    float paddle_drive_v = 0.0f;    //the axis-driven part of it, smoothed

    int   phase = BREAKOUT_PHASE_READY;
    int   phase_ticks = 0;
    uint64_t ticks_elapsed = 0;

    int   score = 0;
    int   lives = BREAKOUT_START_LIVES;
    int   level = 1;
    int   bricks_broken = 0;        //this game, for the stats
    //The speed ramp's own counter, reset when a life is lost rather than when a level ends.
    //Losing a ball is meant to be the relief; finishing a level is not.
    int   speed_bricks = 0;
    int   combo = 0;
    int   best_combo = 0;

    float shield_charge = 1.0f;
    int   shield_flare_ticks = 0;   //counts down after a save
    int   shield_saves = 0;

    int   powerup_wide_ticks = 0;
    int   powerup_slow_ticks = 0;
    int   powerups_caught = 0;

    uint32_t seed = 1;
    //This field's own stream. Sized once in the constructor, re-seeded per game by NewGame, and
    //drawn from only inside the tick - which is what keeps a seed worth quoting.
    RRandom rng;

    /*
        A test hook, and the only thing in this header that is not the game.

        CurrentBallSpeed() honours this instead of its own ramp when it is positive, which is what
        lets the collision probe hold the ball at a chosen speed while it measures the sweep.
        Without it the probe would be measuring the ramp rather than the collision test: the rules
        re-assert the ball's speed every tick, so anything set from outside is gone by the next
        one. Zero means "use the ramp", which is every case but a measurement.
    */
    float speed_override = 0.0f;

    /*
        How many times a ball has run out of collision resolutions inside a single tick.

        The sweep resolves up to BREAKOUT_MAX_RESOLUTIONS impacts per tick and then gives up,
        leaving the rest of that tick's travel unspent. That is not a tunnel - the ball stays
        inside the arena and nothing is passed through - but it IS the ball quietly moving less
        far than its velocity says, so it is worth counting rather than hoping. At playable speeds
        this stays at zero; it is the collision probe that drives it up, and the number it reaches
        is the honest upper bound on how fast this sweep can be pushed.
    */
    int resolution_overruns = 0;

private:
    void BuildLevel(int level_number);
    void ServeBall();                           //puts one ball back on the paddle
    void StepBall(BreakoutBall& ball, BreakoutEvents& events);
    void UpdatePaddle(const BreakoutInput& in);
    //Applies the designed bounce and re-clamps the angle. `offset` is -1..+1 across the paddle.
    //`events` is optional and only receives the departure angle - see paddle_hit_angle.
    void BounceOffPaddle(BreakoutBall& ball, float offset, BreakoutEvents* events);
    //Keeps a direction from becoming too flat or too steep, and keeps its length at `speed`.
    void ConditionVelocity(BreakoutBall& ball);
    void HitBrick(int col, int row, float nx, float ny, BreakoutEvents& events);
    float CurrentBallSpeed() const;
};

#endif
