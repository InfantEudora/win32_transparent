#ifndef _ARCHER_STAGE_H_
#define _ARCHER_STAGE_H_

#include <stdint.h>
#include <vector>
#include <string>

/*
    The rules of the archer game, and nothing else.

    No engine type appears in this header on purpose - not Object, not Renderer, not vec3 - which
    is the same split breakout/Field.h and bomber/Maze.h make and for the same reason: the whole
    simulation becomes a pure function of (previous state, this tick's input), reasonable about
    with no window on the screen and testable by `make rules` in about a second. The Application
    owns the other half - turning keys into the intent below, and turning these numbers into lit
    geometry.

    --- WHAT IS SIMULATED HERE AND WHAT IS reactphysics3d's --------------------------------------
    The seam is not arbitrary, and it is the single most important thing to understand about this
    app:

      IN HERE, by hand:  the archer's own motion, and the flight of an arrow.
      IN rp3d:           crates, brick debris, targets that get knocked over, the rope.

    The archer is swept by hand because a platformer's feel is a DESIGNED response, not a physical
    one. Coyote time, a jump whose height depends on how long the button is held, air control that
    is deliberately worse than ground control - none of these are things a rigid body does, and
    every one of them is what makes the difference between a character that feels good and one
    that feels like a crate. A solver cannot express them, and fighting it to fake them is how you
    get a controller nobody can tune. Same argument breakout/Field.h makes for its ball.

    The arrow is swept by hand for a sharper reason: the aim arc drawn on screen while the bow is
    drawn IS PredictArc() below, running the same integrator the arrow will run. Hand it to rp3d
    and the preview and the flight are two different functions that agree only by luck - and they
    would not, because Physics::AddBoxCollider quietly sets linear damping to 0.5 behind your back
    (see the damping note in core/physics/Physics.h). A promise drawn on screen has to be kept.

    Everything else genuinely wants a solver, so it gets one. See ApplicationArcher for how the
    archer's hand-swept position still shoves rp3d's crates around despite never being solved
    against them.

    --- THE PLAY PLANE ---------------------------------------------------------------------------
    Side view: x to the right, y up, and z is scenery. That is why the vector type in here is two
    components rather than three - the third one is not "currently unused", it does not exist, and
    nothing in the rules can drift out of plane because there is nowhere to drift to. The app pins
    every physics body to the same plane with SetLinearLockAxis for the same reason; see the note
    on it in core/physics/Physics.h, which was written after a breakout capsule drifted a unit out
    of plane and passed clean through the paddle.

    Every duration in here is a count of SIMULATION TICKS. The app runs at ARCHER_TPS of them a
    second and must call SetPhysicsTPS with the same number - the rules have no engine types, so
    they cannot look the rate up for themselves.
*/

//The tick rate, and the one number the rules cannot work out for themselves. Sixty rather than
//the engine's default fifty: this is a fast-paced platformer and 16.7 ms of input granularity is
//noticeably tighter than 20 for a jump that lasts about forty ticks.
#define ARCHER_TPS                  60.0f
#define ARCHER_DT                   (1.0f / ARCHER_TPS)

/*
    A point or a direction in the play plane.

    Deliberately not core's vec2: this header names no engine type, and the constraint is worth
    more than the reuse. It carries only what the rules arithmetic actually uses.
*/
struct v2{
    float x = 0.0f;
    float y = 0.0f;
    v2(){};
    v2(float _x, float _y):x(_x),y(_y){};
    v2 operator+(const v2& o) const { return v2(x + o.x, y + o.y); };
    v2 operator-(const v2& o) const { return v2(x - o.x, y - o.y); };
    v2 operator*(float s) const { return v2(x * s, y * s); };
};

//--- The archer's body, in world units ----------------------------------------------------------
//An axis-aligned box, not a capsule. A platformer wants the sharp, predictable ledge behaviour a
//box gives - you are either over the edge or you are not - where a capsule slides off it.
#define ARCHER_HALF_W               0.35f
#define ARCHER_HALF_H               0.90f

/*
    --- FEEL -------------------------------------------------------------------------------------
    All of these are tuning, all of them are meant to be argued with, and all of them are in
    units/second or units/second^2 rather than per-tick so that changing ARCHER_TPS does not
    silently change the game.

    Gravity is four times earth's. That is not a mistake and it is not "unrealistic" - a jump under
    9.81 at a readable scale hangs in the air for well over a second, which reads as floaty and is
    wrong for everything this prototype is about. The numbers below give a jump about 3.2 units
    high that lasts about 43 ticks, clearing roughly 6.5 units of gap at full run.
*/
#define ARCHER_GRAVITY              42.0f
//Falling is faster than rising, which sharpens the apex. The oldest trick in the genre and still
//the one with the highest ratio of feel to code.
#define ARCHER_FALL_GRAVITY_MUL     1.35f
#define ARCHER_MAX_FALL_SPEED       34.0f

#define ARCHER_RUN_SPEED            9.0f
#define ARCHER_RUN_ACCEL            90.0f       //full speed from rest in about 6 ticks
#define ARCHER_RUN_FRICTION         120.0f      //decel with no input on the ground; higher than
                                                //accel so stopping is crisper than starting
#define ARCHER_AIR_ACCEL            54.0f       //0.6 of ground: committed, but not helpless
#define ARCHER_AIR_FRICTION         14.0f

#define ARCHER_JUMP_SPEED           16.4f       //-> 3.2 units of apex under ARCHER_GRAVITY
//Releasing the jump key on the way up cuts the climb, so a tap is a hop and a hold is a full
//jump. The single control that most decides whether a platformer feels precise.
#define ARCHER_JUMP_CUT             0.45f
//Grace either side of the edge of a platform. Both are pure generosity to the player and neither
//is detectable as cheating at these lengths - 100 ms.
#define ARCHER_COYOTE_TICKS         6           //may still jump this long after walking off
#define ARCHER_JUMP_BUFFER_TICKS    6           //a jump pressed this long before landing still fires

//Drawing a bow plants your feet. Halving the run speed rather than pinning it makes aiming a
//decision with a cost instead of a mode you stop in.
#define ARCHER_DRAW_MOVE_SCALE      0.5f

/*
    --- THE BOW ----------------------------------------------------------------------------------
    Hold to draw, Up/Down to tilt, release to loose. Power ramps over BOW_DRAW_TICKS and is
    clamped up from BOW_MIN_POWER so that a panicked tap still produces an arrow that goes
    somewhere - an input that does nothing at all reads as a dropped key press.

    The aim angle is relative to the way the archer is FACING: 0 is horizontal ahead, positive is
    up, and facing left mirrors the whole thing. So the control means the same thing in both
    directions, which a world-space angle would not.
*/
#define BOW_DRAW_TICKS              36          //0.6 s to full draw
#define BOW_MIN_POWER               0.25f
#define BOW_AIM_MIN_DEG             -85.0f
#define BOW_AIM_MAX_DEG             85.0f
#define BOW_AIM_RATE_DEG            110.0f      //degrees per second while a tilt key is held
#define BOW_SHOULDER_UP             0.35f       //where the arrow leaves, relative to the archer's
#define BOW_SHOULDER_FWD            0.40f       //centre, before the aim angle is applied

#define ARROW_SPEED_MIN             16.0f       //at BOW_MIN_POWER
#define ARROW_SPEED_MAX             46.0f       //at a full draw
//Lighter than the archer's gravity so an arrow draws a long readable arc rather than a mortar
//lob. An arrow is not trying to be a projectile simulation, it is trying to be aimable.
#define ARROW_GRAVITY               24.0f
#define ARROW_MAX_LIVE              24          //a ring buffer; the oldest is recycled
#define ARROW_STUCK_TICKS           600         //how long an arrow stays stuck in a wall
#define ARROW_MAX_AGE_TICKS         900         //a miss that flew off the level is reaped
#define ARROW_HALF_LEN              0.40f       //visual only; the sweep is a point

//How far the aim preview is drawn, and at what resolution. Whole ticks, so the dots are literally
//where the arrow will be on those ticks.
#define AIM_ARC_POINTS              28
#define AIM_ARC_TICK_STRIDE         3

/*
    --- HANGING FROM A LEDGE ---------------------------------------------------------------------
    The grab is AUTOMATIC. No key: you jump at a ledge and you catch it, the way Prince of Persia
    and everything descended from it works. A grab key would need explaining, would be pressed late
    under pressure, and buys nothing - holding AWAY from the ledge is what expresses "I meant to
    miss that", and it is the same stick you were already holding.

    ONLY WHILE FALLING (vel.y <= LEDGE_GRAB_MAX_RISE, which is barely positive so the apex counts).
    This is the rule that resolves the one real conflict in the mechanic: rising past a lip you
    could have landed on top of must LAND, not snag, and the hands cross the lip before the feet
    clear it. Falling-only means a jump you can make is never stolen by a grab, and a jump you
    cannot make is caught on the way down. Nothing else needed tuning once this was right.

    The bands are generous on purpose. Falling past the level's high ledge the hands cross the lip
    at about 9.5 units a second, which is 0.16 of a unit per tick - so a 1.2-unit window is about
    seven ticks to catch it in, and a tight one would read as the grab being unreliable rather than
    as the player being late.
*/
#define LEDGE_GRAB_BAND_UP          0.45f   //the lip may be this far ABOVE the hands (a reach up)
#define LEDGE_GRAB_BAND_DOWN        0.75f   //...or this far below them (a catch on the way past)
#define LEDGE_GRAB_REACH            0.45f   //how far the hands reach past the body's leading edge
#define LEDGE_GRAB_MAX_RISE         0.50f   //see above: falling, or within a whisker of the apex
//After letting go, the same ledge cannot be caught again for this long - without it, dropping off
//a ledge re-grabs it on the very next tick and the archer is welded there.
#define LEDGE_RELEASE_COOLDOWN      14
//Climbing up is a fixed, uninterruptible move. It is a lerp today because there is no animation to
//drive it; a root-motion clip replaces it exactly, which is the reason it is a duration in ticks
//and a start/end pair rather than a velocity.
#define LEDGE_CLIMB_TICKS           18

//--- The level ----------------------------------------------------------------------------------
/*
    What a block IS, which decides what the rules do with it and what the app builds for it.

    SOLID and LEDGE are the same collision; they differ only in that a LEDGE advertises its top
    corners as grabbable, which the hang-and-climb slice reads. PLATFORM is one-way: solid from
    above, passable from below and when holding Down. BREAKABLE is solid to the archer and to
    arrows until the brick-wall slice knocks it out, at which point it is simply removed.
*/
enum StageBlockKind{
    BLOCK_SOLID = 0,
    BLOCK_LEDGE,
    BLOCK_PLATFORM,
    BLOCK_BREAKABLE
};

//An axis-aligned box in the play plane. Centre and half extents, because every test in here wants
//them that way and converting once at build time is cheaper than converting in the sweep.
struct StageBlock{
    float x = 0.0f;
    float y = 0.0f;
    float hw = 0.5f;
    float hh = 0.5f;
    int   kind = BLOCK_SOLID;
    bool  f_alive = true;       //BREAKABLE blocks clear this; nothing else ever does

    float Left()   const { return x - hw; };
    float Right()  const { return x + hw; };
    float Bottom() const { return y - hh; };
    float Top()    const { return y + hh; };
};

/*
    The terrain test bay - a stretch of level LEFT of the start that exists only to compare
    marching-cubes terrain settings against each other and against the plain blockout. See
    apps/archer/terrain_plan.md; this switch is how the whole thing comes back out in one edit.

    ONLY BLOCK_SOLID GOES IN THE BAY, and that is not a style preference. stage_test.cpp's reach
    assertion skips SOLID and BREAKABLE, so solid test geometry is invisible to it - but its
    HighLedge() takes the FIRST BLOCK_LEDGE above feet reach, so one test ledge in here would
    silently move the hang-slice tests onto a piece of scenery. The bay is appended at the END of
    BuildLevel for the matching reason: block_objects is indexed in step with blocks, and
    stage_test's blocks[0] fallback expects the main ground run to still be first.
*/
#define ARCHER_TEST_BAY             1
//Four bays of seven units, x -40 .. -12, abutting the main ground run. The app reads these to
//work out which blocks belong to which bay - selection is BY X RANGE rather than by a new field
//on StageBlock, so that the terrain work never has to reach into the rules.
#define ARCHER_TEST_BAY_COUNT       4
#define ARCHER_TEST_BAY_X_MIN       (-40.0f)
#define ARCHER_TEST_BAY_WIDTH       7.0f
#define ARCHER_TEST_BAY_X_MAX       (ARCHER_TEST_BAY_X_MIN + ARCHER_TEST_BAY_COUNT * ARCHER_TEST_BAY_WIDTH)
//The centre of bay i, which is also where its three test shapes are laid out around.
#define ARCHER_TEST_BAY_CENTRE(i)   (ARCHER_TEST_BAY_X_MIN + ((i) + 0.5f) * ARCHER_TEST_BAY_WIDTH)

/*
    Something the APP builds a rigid body for, described here so that the whole level layout lives
    in one file even though the rules never touch these.

    A prop is rp3d's business - it exists to be knocked over, kicked, shattered or swung from, all
    of which are exactly what a solver is better at than this file would be. The rules know only
    that it is there and what kind it is, which is enough for the app to build it and enough for a
    headless rules test to assert the level contains one.
*/
enum StagePropKind{
    PROP_CRATE = 0,         //small, kickable, a single dynamic box
    PROP_TARGET,            //a standing board an arrow knocks down
    PROP_BRICKWALL,         //cols x rows of bricks that break apart when kicked through
    PROP_ROPE_ANCHOR        //the fixed top of a rope; the chain hangs from here
};

/*
    The levels a Stage can build.

    MAIN is the traversal level the whole prototype grew up in. RANGE is the test range from
    bow_plan.md section 7: one flat floor between two walls and a few targets either side of the
    start, and nothing else - no ledges, no rope, no gaps. It exists so the bow can be worked on
    with a still camera and nothing to fall off, and so a screenshot of it means the same thing
    from one run to the next.
*/
enum StageLevel{
    STAGE_LEVEL_MAIN = 0,
    STAGE_LEVEL_RANGE,
    STAGE_LEVEL_COUNT
};

struct StageProp{
    int   kind = PROP_CRATE;
    float x = 0.0f;
    float y = 0.0f;
    float w = 1.0f;         //full width, not half - these are read by level-building code, not
    float h = 1.0f;         //by a sweep, and full extents are what that wants
    int   cols = 1;         //PROP_BRICKWALL only
    int   rows = 1;         //PROP_BRICKWALL only
};

/*
    A PROP, AS THE RULES SEE IT: a box in the way, refreshed every tick.

    This is how "a crate blocks you" is expressed without the rules learning what a rigid body is.
    The app reads each prop's CURRENT position and size off its body and hands them over as six
    plain numbers before each Tick; the archer's sweep then stops against them exactly as it stops
    against the level, and the deep overlap that made the old arrangement unworkable never happens.

    `id` is the app's own handle - an index into its prop list - and is completely opaque in here.
    The rules never interpret it, they only hand it back on the push events below, which is what
    lets the app know WHICH body to shove without the rules knowing that bodies exist.

    Rebuilt from scratch every tick rather than kept in sync, because these things move: a crate
    that was shoved last tick is somewhere else now, and a stale box is a wall the player cannot
    see. Clearing and refilling a vector of a dozen PODs costs nothing next to being wrong.
*/
/*
    A point on a rope the archer could catch, refreshed every tick like the obstacles.

    The rope is a chain of rigid bodies the rules know nothing about; this is the app saying "there
    is something grabbable here". `id` is the app's handle on the segment and is never interpreted
    in here - it comes straight back on the grab event so the app knows which link to join to.
*/
struct StageRopePoint{
    float x = 0.0f;
    float y = 0.0f;
    int   id = -1;
};

struct StageObstacle{
    float x = 0.0f;
    float y = 0.0f;
    float hw = 0.5f;
    float hh = 0.5f;
    int   id = -1;          //the app's handle; never interpreted here
    bool  f_pushable = false;   //false for the static ones (a brick in a standing wall)

    float Left()   const { return x - hw; };
    float Right()  const { return x + hw; };
    float Bottom() const { return y - hh; };
    float Top()    const { return y + hh; };
};

/*
    --- THE ROPE ---------------------------------------------------------------------------------
    A swing, and the one mechanic in this game where the SOLVER owns the archer rather than these
    rules. A pendulum is exactly what a constraint solver is good at and exactly what a hand-written
    integrator is bad at: the whole appeal of a rope is that its motion is emergent, that a badly
    timed release drops you and a well timed one throws you, and none of that survives being
    scripted. So MODE_ROPE hands the body over and Stage::TickArcher steps aside - see the note at
    the top of ApplicationArcher.h, where this handoff is the reason the archer has a rigid body at
    all.

    What stays here is the DECISION - when a grab is allowed, when a release happens, and which
    rope was caught - because those are rules. The app feeds in where the rope is (like the props,
    as plain numbers) and carries the physics out.
*/
#define ROPE_GRAB_REACH             1.20f   //how near a rope point the archer's hands must be
//A grab press must not also read as the release press on the same or the next tick. Eight ticks is
//long enough that no human double-fires it and short enough that a panic release still works.
#define ROPE_MIN_HOLD_TICKS         8
#define ROPE_GRAB_COOLDOWN          20      //after letting go, before the same rope can be caught
//Pumping. Applied by the app as a force on the swinging body, because that body is the solver's -
//but the NUMBER lives here with the rest of the feel.
#define ROPE_PUMP_FORCE             520.0f
//Letting go with jump rather than action adds this much upward, so a rope can be used to gain
//height rather than only to cross a gap. The horizontal throw comes from the swing itself.
#define ROPE_JUMP_BOOST             7.0f

//Well under ARCHER_RUN_SPEED on
//purpose: the archer is blocked by what they are pushing, so this is also the speed they walk at
//while pushing it, and a crate that slid along at a full run would weigh nothing.
#define ARCHER_PUSH_SPEED           4.0f

/*
    --- THE KICK ---------------------------------------------------------------------------------
    A separate verb from the shove, and it has to be. Walking into a crate already moves it at
    ARCHER_PUSH_SPEED, which is what leaning on something looks like; a kick is a decision, and if
    the two were the same thing then either the shove would launch crates across the level or the
    kick would be indistinguishable from walking.

    So: a key, a wind-up, a brief window in which it actually connects, and a cooldown. The window
    is what makes it a commitment rather than a button to mash - there is a real cost to kicking at
    the wrong moment, which is what makes kicking at the right one worth anything.

    THE ACTIVE WINDOW IS A RANGE OF TICKS, not an instant. An instant lands on whatever happened to
    be overlapping on one exact tick, which for a moving crate is a coin toss; five ticks is 83 ms,
    long enough that a kick aimed at something connects with it and short enough that it cannot
    sweep up half the level on the way past.
*/
/*
    THESE THREE ARE SET BY THE ANIMATION, and that is the opposite of the usual direction here.

    Everywhere else in this file the rules decide and the clip is stretched to fit. The kick is the
    one move where that could not work: it was 14 ticks against a 1.6s clip, which needed 7.1x to
    fit, and a kick at seven times speed is not a fast kick, it is a glitch. A kick needs a wind-up
    to read as a kick at all, so the clip sets the pace and the rules follow it.

    MEASURED, not guessed. Kick_Front is 86 ticks long and its boot reaches furthest from the hips
    at tick 42 - found by posing the model and watching both feet, see
    ApplicationArcher::MeasureKickClip. The app logs that measurement next to this window every
    start and says so loudly when the two stop lining up, which is what a re-export with a
    different impact frame would look like.

    IT HAS ALREADY EARNED ITS KEEP ONCE. The clip was 98 ticks when these numbers were first fitted
    and is 86 now, because twelve frames came off the end of it; the app said so on the next start,
    in the form of the number to type. Trimming the END does not move the strike, so the window
    below was untouched by that - tick 42 of 86 rather than tick 42 of 98 - and only the total had
    to change. Trimming the FRONT would move it, and the window check is what catches that.

    The active window stays FIVE TICKS wide for the reason below; it has simply moved to where the
    boot actually is. Everything else about the shape of the move is unchanged.

    THE COST, stated plainly because it is a real one: `f_planted` roots her for the whole of
    kick_ticks, so a kick is a 1.43-second commitment, of which 0.73s is recovery after the boot
    has already landed. That is a heavy, committal move. If it wants to be lighter, the fix is
    to unroot at KICK_ACTIVE_TO and let the recovery be cancelled by moving - which needs the
    Puppet to drop the clip at the same moment, or the animation would be overruling the rules.

    AND IT ONLY HAPPENS ON THE GROUND. A kick off the ground was allowed once, as a flying kick;
    it was a second and a half of hanging motionless in the air playing a clip that has a plant in
    it, and the plant is what a kick IS. See the gate at the top of Stage::TickKick.
*/
#define KICK_TICKS                  86      //the whole move; Kick_Front is 1.433s
#define KICK_ACTIVE_FROM            40      //wind-up before this
#define KICK_ACTIVE_TO              44      //recovery after; the boot connects at tick 42 of 86
#define KICK_COOLDOWN               10      //ticks before another may be started
#define KICK_REACH                  0.75f   //how far past the body's leading edge it reaches
#define KICK_HALF_HEIGHT            0.55f   //half the height of the box it sweeps
#define KICK_Y_OFFSET               -0.25f  //centred low - it is a boot, not a shoulder barge
//What a kick imparts. Far above ARCHER_PUSH_SPEED, which is the point.
#define KICK_SPEED                  13.0f
#define KICK_LIFT                   4.5f
//A grounded kick plants the feet. Not a full stop - the archer keeps sliding a little, which reads
//as weight rather than as the game taking the controls away.
#define KICK_ROOT_FRICTION          40.0f

//--- The archer's state machine -----------------------------------------------------------------
/*
    What the archer is doing with their whole body. Deliberately one small enum rather than a pile
    of booleans: these are mutually exclusive by construction, and the bugs in this kind of code
    are almost always two flags that disagree.

    The bow is a SEPARATE piece of state below, because drawing is something you do WHILE running
    or falling, not instead of it.
*/
enum ArcherMode{
    MODE_GROUND = 0,
    MODE_AIR,
    MODE_HANG,          //hanging from a ledge - the ledge slice
    MODE_CLIMB,         //pulling up over one - the ledge slice
    MODE_ROPE           //on the rope, where rp3d owns the body instead - the rope slice
};

enum BowMode{
    BOW_IDLE = 0,
    BOW_DRAWING
};

//One tick's worth of intent, already reduced from whatever device produced it.
struct ArcherInput{
    float move_axis = 0.0f;         //-1 left .. +1 right
    float aim_axis = 0.0f;          //-1 tilt down .. +1 tilt up
    bool  f_jump_down = false;      //held, for the variable-height cut
    bool  f_jump_pressed = false;   //edge
    bool  f_draw_down = false;      //held: the bow is being drawn
    bool  f_draw_released = false;  //edge: loose the arrow
    bool  f_down_held = false;      //drop through a one-way platform
    bool  f_kick_pressed = false;   //edge: kick
    bool  f_action_pressed = false; //edge: take the rope - the later slice
};

/*
    An arrow in flight, or stuck in something.

    prev_pos is kept because the app needs THIS TICK'S SEGMENT to ask rp3d whether the arrow
    passed through a crate or a target on its way - see the handshake note on Arrows() below. It
    is not used by the rules themselves.
*/
struct Arrow{
    v2    pos;
    v2    prev_pos;
    v2    vel;
    float angle = 0.0f;         //radians, the direction of travel; the app orients the mesh by it
    int   age_ticks = 0;
    bool  f_live = false;
    bool  f_stuck = false;
};

/*
    What happened this tick, for the app to turn into sound, particles and camera shake.

    Events rather than the app polling state, because most of these are instants: a landing is a
    tick, not a condition, and a poll at the wrong moment either misses it or reports it twice.
*/
struct StageEvents{
    bool  f_jumped = false;
    bool  f_landed = false;
    float land_speed = 0.0f;        //how hard; the app scales dust and shake by it
    bool  f_shot = false;
    float shot_power = 0.0f;        //0..1, the draw at the moment of release
    bool  f_bumped_head = false;
    bool  f_grabbed_ledge = false;  //caught a lip this tick
    bool  f_released_ledge = false; //let go of one, by choice or by dropping
    bool  f_climbed = false;        //finished pulling up over one
    bool  f_kick_started = false;   //the boot went out; the connect comes a few ticks later
    bool  f_kick_connected = false; //...and hit at least one thing

    //--- The rope -------------------------------------------------------------------------------
    //The app acts on these by creating and destroying the joint that makes the swing real.
    bool  f_grabbed_rope = false;
    int   grabbed_rope_id = -1;     //which link, by the id it was added with
    bool  f_released_rope = false;
    bool  f_rope_jump = false;      //let go WITH jump, so the app adds ROPE_JUMP_BOOST

    /*
        Props the kick connected with. Separate from `pushes` above on purpose - a shove and a
        kick are different events with very different numbers behind them, and collapsing them
        into one list with a magnitude field is how the app ends up unable to tell whether to
        play a footstep or break a wall.
    */
    struct StageKick{
        int   id = -1;
        float dir = 1.0f;
        float x = 0.0f;         //where the boot landed, for debris and for deciding which
        float y = 0.0f;         //bricks of a wall are nearest it
    };
    std::vector<StageKick> kicks;

    //BLOCK_BREAKABLE blocks destroyed this tick, by index. Stage has already cleared their
    //f_alive; the app still has to take their collider out of the world and burst them.
    std::vector<int> broken_blocks;

    /*
        Props the archer walked into this tick, and how hard.

        The rules decide WHETHER something is being shoved and in which direction, because that
        falls out of the sweep they already do; the app decides what that means for a rigid body.
        `id` is the handle the obstacle was added with.
    */
    struct StagePush{
        int   id = -1;
        float dir = 1.0f;       //+1 shoved to the right, -1 to the left
        float speed = 0.0f;     //units per second, already capped at ARCHER_PUSH_SPEED
    };
    std::vector<StagePush> pushes;

    //Arrows that struck level geometry this tick. Arrows that struck a PROP are found by the app
    //instead - see Arrows().
    struct ArrowHit{
        int   arrow = -1;
        v2    point;
        v2    normal;
        float speed = 0.0f;
        int   block = -1;           //index into blocks; BREAKABLE is the interesting case
    };
    std::vector<ArrowHit> arrow_hits;
};

/*
    The game.

    Owned and touched ONLY by the physics thread - RunSimulationTick and the command handlers,
    which also run there - plus DrawImGuiUI, which holds physics_mutex. Anything else reads the
    app's published snapshot instead.
*/
class Stage{
public:
    Stage();

    //Builds the blockout and puts the archer at the start. Call it again to restart.
    void Reset();

    /*
        WHICH LEVEL this Stage builds - see StageLevel. A different level is a different Stage
        INSTANCE, not a different mode of the one Stage: the app keeps one per scene, and each
        carries its own archer, arrows and props, so leaving the range and coming back finds it
        exactly as it was left. Setting it resets, because a level half-swapped is no level.
    */
    void SetLevel(int new_level);
    int  GetLevel() const { return level; };
    //Where the archer starts and where a fall off the world puts her back. Per level.
    v2   StartPosition() const;

    //One tick. The whole simulation, and the only way state changes.
    void Tick(const ArcherInput& in, StageEvents& events);

    //--- The world ------------------------------------------------------------------------------
    std::vector<StageBlock> blocks;
    std::vector<StageProp>  props;

    /*
        The props as boxes in the way, refreshed by the app BEFORE every Tick.

        Not filled by Stage and not persistent: `ClearObstacles` then one `AddObstacle` per live
        prop, every tick, then Tick. Leaving them stale is the one way to misuse this, and it
        misuses as an invisible wall standing where a crate used to be.
    */
    std::vector<StageObstacle> obstacles;
    void ClearObstacles();
    void AddObstacle(float x, float y, float hw, float hh, int id, bool f_pushable);

    //And the rope, the same way. Also refreshed before every Tick - the links are swinging.
    std::vector<StageRopePoint> rope_points;
    void ClearRopePoints();
    void AddRopePoint(float x, float y, int id);

    //--- The archer -----------------------------------------------------------------------------
    v2    pos;                      //centre of the body box
    v2    vel;
    int   mode = MODE_AIR;
    float facing = 1.0f;            //+1 right, -1 left; never 0
    bool  f_on_ground = false;
    int   coyote_ticks = 0;         //counts down after walking off an edge
    int   buffer_ticks = 0;         //counts down after a jump was pressed in the air

    //--- Hanging and climbing -------------------------------------------------------------------
    int   hang_block = -1;          //index into blocks, while MODE_HANG or MODE_CLIMB
    //Which SIDE of that block the archer is on: -1 hanging off its left corner, +1 its right.
    //Not derived from `facing`, because the climb needs it after facing may have changed.
    float hang_side = -1.0f;
    int   climb_ticks = 0;          //counts down through MODE_CLIMB
    //The kick, counted UP from 1 so that 0 means "not kicking" - see KICK_ACTIVE_FROM/TO.
    int   kick_ticks = 0;
    int   kick_cooldown = 0;

    //--- The rope -------------------------------------------------------------------------------
    int   rope_id = -1;             //which link is held, while MODE_ROPE; the app's handle
    int   rope_ticks = 0;           //how long it has been held - see ROPE_MIN_HOLD_TICKS
    int   rope_cooldown = 0;
    v2    climb_from;               //where the climb started and ends, captured on entry so the
    v2    climb_to;                 //lerp cannot drift if anything else touches pos
    int   grab_cooldown = 0;        //see LEDGE_RELEASE_COOLDOWN

    //--- The bow --------------------------------------------------------------------------------
    int   bow_mode = BOW_IDLE;
    int   draw_ticks = 0;           //0..BOW_DRAW_TICKS
    float aim_deg = 20.0f;          //relative to facing; + is up. Survives a release, so the next
                                    //shot starts where the last one was aimed.
    float DrawPower() const;        //0..1, what draw_ticks is worth after the minimum

    /*
        The arrows, live and stuck.

        PUBLIC AND WRITABLE ON PURPOSE, which is the one place this class breaks its own rule, so
        it is worth being explicit about why. An arrow can hit two quite different things: level
        geometry, which is in here, and a crate or a target, which is a rigid body the rules know
        nothing about. Resolving the second in here would mean teaching the rules about rp3d, which
        is precisely what this file exists not to do.

        So the app does it: after Tick, it walks the live arrows, asks rp3d to raycast the segment
        prev_pos -> pos, and if a body was struck NEARER than whatever the rules already resolved,
        it applies the impulse and calls StickArrow or KillArrow. The rules stay engine-free and
        the props stay the solver's. The cost is that "did this arrow hit anything" is answered in
        two places, which is why both are named here.
    */
    Arrow arrows[ARROW_MAX_LIVE];
    int   NumLiveArrows() const;

    void  StickArrow(int index, const v2& point);   //stop it dead and leave it embedded
    void  KillArrow(int index);                     //remove it entirely

    /*
        Where an arrow loosed right now would go, as up to AIM_ARC_POINTS positions sampled every
        AIM_ARC_TICK_STRIDE ticks.

        THIS IS THE SAME INTEGRATOR THE ARROW RUNS, which is the whole point of it existing here
        rather than in the app: the dots on screen are not an approximation of the flight, they
        are the flight. Stops at the first block the arc enters, so the preview also shows what it
        will hit. Returns how many points were written.
    */
    int   PredictArc(v2* out_points, int max_points) const;

    //Where an arrow would leave the bow, given the current facing and aim. The app draws the bow
    //there, and Loose() spawns from it.
    v2    MuzzlePosition() const;
    v2    AimDirection() const;

    //--- Bookkeeping ----------------------------------------------------------------------------
    uint64_t ticks = 0;
    int   arrows_shot = 0;
    int   arrows_hit_blocks = 0;

    //The box the boot sweeps, in world units. PUBLIC because three things need it and they must
    //not drift apart: the sweep itself, the app's debug draw while the numbers are being tuned,
    //and the rules test. Meaningful only while kick_ticks is non-zero, but cheap and side-effect
    //free to ask at any time.
    void  KickBox(float& out_left, float& out_right, float& out_bottom, float& out_top) const;

    //A one-line dump of the archer's state, for the log, the ImGui panel and the rules test.
    std::string DebugLine() const;

private:
    int  level = STAGE_LEVEL_MAIN;
    void BuildLevel();
    void BuildMainLevel();
    void BuildRangeLevel();
    void TickBow(const ArcherInput& in, StageEvents& events);
    void TickArcher(const ArcherInput& in, StageEvents& events);
    void TickArrows(StageEvents& events);
    void Loose(StageEvents& events);

    //--- Hanging and climbing -------------------------------------------------------------------
    //A grabbable lip within reach right now, or -1. Fills the side of the block the archer is on.
    //Const and side-effect free, which is what lets the app draw a hint on it later.
    int   FindGrabbableLedge(float& out_side) const;
    void  EnterHang(int block, float side, StageEvents& events);
    void  ReleaseHang(StageEvents& events);
    void  TickHang(const ArcherInput& in, StageEvents& events);
    void  TickClimb(const ArcherInput& in, StageEvents& events);

    //--- The kick -------------------------------------------------------------------------------
    //Advances the kick timer and, on the ticks it is live, sweeps its box against the breakable
    //blocks and the obstacles. Everything it finds goes into `events`.
    void  TickKick(const ArcherInput& in, StageEvents& events);

    //--- The rope -------------------------------------------------------------------------------
    //While MODE_ROPE the solver owns the archer's position, so this decides only one thing: when
    //to let go. The app writes pos/vel back from the body before each tick.
    void  TickRope(const ArcherInput& in, StageEvents& events);
    //A link within reach right now, or -1.
    int   FindRopePoint() const;

    //Moves the body box by `delta`, stopping against solid geometry AND against the obstacles,
    //and reports what was hit. Axis-separated: x first and resolved, then y - which is what makes
    //running into a wall while falling behave, and is the standard answer for a box platformer.
    //Pushes against pushable obstacles are appended to `events`.
    void MoveAndCollide(const v2& delta, bool f_down_held, StageEvents& events,
                        bool& out_hit_floor, bool& out_hit_ceiling, bool& out_hit_wall);

    //Nearest block struck by the segment a->b, or -1. Fills the hit point and the face normal.
    int   SegmentHitsBlock(const v2& a, const v2& b, v2& out_point, v2& out_normal) const;

    int   next_arrow = 0;           //the ring buffer's write cursor
};

#endif
