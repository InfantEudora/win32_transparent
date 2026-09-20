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

//How fast the archer can shove a prop along, in units per second. Well under ARCHER_RUN_SPEED on
//purpose: the archer is blocked by what they are pushing, so this is also the speed they walk at
//while pushing it, and a crate that slid along at a full run would weigh nothing.
#define ARCHER_PUSH_SPEED           4.0f

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
    bool  f_action_pressed = false; //edge: grab, release, knife - the later slices
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

    //A one-line dump of the archer's state, for the log, the ImGui panel and the rules test.
    std::string DebugLine() const;

private:
    void BuildLevel();
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
