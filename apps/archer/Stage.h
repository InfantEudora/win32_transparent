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
/*
    The top speed ON THE RANGE, and the only rule the range changes.

    The bow plan wanted the range walk-only - no jump, no kick - by masking input. That was
    dropped for this: every verb still works, she is just slower, so the range shows the same
    character with a different feel instead of a crippled one, and the jump there is the jump you
    get from a jog rather than from a sprint. That difference is worth having side by side.

    3.0 because it is Running_Slow's own pace - 2.92 units/s at her scale, measured by
    MeasureClips - so at full stick the feet do not slide, where the main level's 9.0 slides them
    1.73x past the fastest clip there is. Everything that scales with speed (the jump's reach, the
    blend-space rung, the stop) follows from target_vx, so nothing else needs to know.
*/
#define ARCHER_RANGE_RUN_SPEED      3.0f
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
/*
    THE ARROW IS ON THE STRING only this many ticks into a draw. Before it she is reaching back to
    the quiver and bringing an arrow over, and there is nothing to shoot: letting go then CANCELS
    the draw - no arrow, no shot - and power builds only over the pull that follows, from
    BOW_MIN_POWER here to full at BOW_DRAW_TICKS.

    Cancel rather than queue the shot until the nock: a queued shot fires 0.4s after a tap, which
    reads as input lag on the game's main verb. A cancelled tap reads as a tap.

    MEASURED from Standing_DrawArrow, the same arrangement as KICK_TICKS: the app watches her hand
    reach the string (Bow::TrackHand) and warns, with the number to type here, when that tick and
    this constant drift apart - which is what re-timing the clip, or BOW_DRAW_TICKS, does.
*/
#define BOW_NOCK_TICKS              23
#define BOW_MIN_POWER               0.25f
#define BOW_AIM_MIN_DEG             -85.0f
#define BOW_AIM_MAX_DEG             85.0f
#define BOW_AIM_RATE_DEG            110.0f      //degrees per second while a tilt key is held
/*
    THE ARROW IS A SEGMENT: an ANCHOR (the nock, on the string), a direction (the aim) and a
    length. The tip - the point that flies and strikes - starts ARROW_LENGTH along the aim from
    the anchor, and the arrow's first sweep runs from the anchor, not from the tip. So anything
    between the string and the arrowhead catches it: pressed against a wall she hits the near face
    instead of burying the arrow in it, and a post thinner than an arrow is long cannot be shot
    through from point blank.

    MEASURED from the model rather than guessed, and typed in here because this header names no
    engine type (bow_plan.md §5). The anchor is the nocked arrow's origin at full draw, relative
    to her centre, averaged over aim -80..+80 with the aim override on - it moves about 0.2 across
    that range, since she turns about her chest rather than about the nock. The length is the
    arrow mesh, nock to point, at the rig's scale: 0.519 x 2.020. ApplicationArcher checks the
    length against the loaded mesh at every start and says which number to change.
*/
#define BOW_NOCK_UP                 0.54f       //the anchor, above her centre
#define BOW_NOCK_FWD                0.11f       //and ahead of it, before the aim is applied
#define ARROW_LENGTH                1.05f       //nock to point

/*
    THE AIM SWAYS once an arrow is on the string: a smooth drift added to aim_deg, up to this many
    degrees either way, narrowing as she kneels (animation_plan.md, Step 2). DETERMINISTIC - a sum
    of incommensurate sines of the ticks since the nock, no RNG - so PredictArc includes it, the
    dots drift with it, and the arrow still goes exactly where they point: the skill is timing the
    release, not rolling dice. Replays and `make rules` stay exact for the same reason. It eases
    in from zero over AIM_SWAY_RAMP_TICKS after the nock, so the arc does not jump when it
    appears; the bow on screen sways with it because the live aim neutral turns her to whatever
    angle this says.

    EACH DRAW SWAYS DIFFERENTLY. A function of the time since the nock alone is the same drift
    every draw - measured, every quick shot went about 3 degrees high, and "release at one
    second" would have been a thing to learn. So each draw starts the drift at its own point,
    picked from the draw count (draws_started) - still no RNG, and still the same in a replay.
*/
#define AIM_SWAY_STAND_DEG          4.0f
#define AIM_SWAY_KNEEL_DEG          1.0f
#define AIM_SWAY_RAMP_TICKS         15      //0.25s from still to the full drift

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
/*
    How far BELOW the lip the body box's top hangs. The box is not the hands: Hanging_Braced holds
    them well above her head, and with the box's top at the lip her fingers lay 0.33 above the
    stone. This drops her until the finger joints rest on it; MeasureLedgeHang checks it against
    the clip at every start and prints the number to type.
*/
#define LEDGE_HANG_DROP             0.31f
/*
    Climbing up is a fixed, uninterruptible move: a duration, a start and an end, and the path
    between them - the shape `Climb`'s hips take from the grab to standing, so the hands stay on
    the lip while the rules carry the body. Stage cannot read the clip, so the path is a copy, and
    ApplicationArcher::MeasureLedgeClimb re-measures it at every start and prints the table to
    type if the export has moved away from it (the KICK_TICKS arrangement).

    LEDGE_CLIMB_TICKS IS THE FEEL: the clip is pinned to the rules' progress, so any length plays
    it through exactly once. The grab-to-standing part is 1.77s; 60 ticks plays it at 1.77x. It
    was 18 (0.30s) before there was a clip - a hop onto the ledge, not a mantle.
*/
#define LEDGE_CLIMB_TICKS           60
//How far past the lip the body stands when the climb ends: where the clip's hips finish, less
//the half width. Clamped to the block, so a narrow one still takes her.
#define LEDGE_CLIMB_INSET           0.65f
#define LEDGE_CLIMB_PATH_SAMPLES    17
/*
    Fractions of the whole rise and the whole step across, at evenly spaced points through the
    move. The step across stalls in the middle, because the hips close on the wall while she pulls
    up and only walk on once she is over the lip.

    THE RISE IS NOT THE CLIP'S ALONE. The rules rise 2.11, hang to standing (the body's height and
    LEDGE_HANG_DROP); the clip's hips rise 1.61 from the grab. The 0.50 between them is put where
    no hand is planted, since that is the one place a body moving more than its pose does not show:
    LEDGE_CLIMB_REGRIP of it over the first samples - the crossfade in, where her hands move from
    hooked over the lip to flat on top, and the hang's hips sit lower under her hands than the
    grab's - and the rest over the stand-up, from LEDGE_CLIMB_STANDUP_FROM, where the clip ends
    0.08 higher than Idle stands. Spread evenly instead, the planted hand slid up the move.

    The re-grip is a quarter of the move, and the app sets the hang-to-climb crossfade to match
    it: a crossfade shorter than the re-grip would plant the hands while the body is still
    hitching up under them.
*/
#define LEDGE_CLIMB_REGRIP          (0.12f + LEDGE_HANG_DROP)
#define LEDGE_CLIMB_REGRIP_SAMPLES  4
#define LEDGE_CLIMB_STANDUP_FROM    12
extern const float LEDGE_CLIMB_UP[LEDGE_CLIMB_PATH_SAMPLES];
extern const float LEDGE_CLIMB_ACROSS[LEDGE_CLIMB_PATH_SAMPLES];

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
    //Collision only: something else is its look - an authored model (StageScenery) - so the app
    //draws no box for it outside the F2 blockout view, and it neither melts nor grows plants.
    //The rules never read it; to Stage it is an ordinary block of its kind.
    bool  f_invisible = false;

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
/*
    Two bays over the same stretch, x -40 .. -12, abutting the main ground run: bay 0 is the ground
    and everything standing on it, bay 1 an island floating well above. A block belongs to the bay
    its CENTRE is in - inside the stretch, and below or above ARCHER_TEST_BAY_SPLIT_Y - so moving a
    box in the editor and regenerating moves it between bays. Selection by position rather than by
    a new field on StageBlock, so that the terrain work never has to reach into the rules.
*/
#define ARCHER_TEST_BAY_COUNT       2
#define ARCHER_TEST_BAY_X_MIN       (-40.0f)
#define ARCHER_TEST_BAY_X_MAX       (-12.0f)
#define ARCHER_TEST_BAY_SPLIT_Y     6.0f

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

    ROPE is the same idea for the rope: a floor, two walls and ONE long rope in the middle, and
    nothing else - the place to work on catching, swinging and climbing it.
*/
enum StageLevel{
    STAGE_LEVEL_MAIN = 0,
    STAGE_LEVEL_RANGE,
    STAGE_LEVEL_ROPE,
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
    /*
        Hangs where it is put instead of falling: the app builds it with gravity OFF. Still a
        dynamic body, so an arrow still knocks it - and with nothing pulling it down and nothing
        slowing it, a knocked one drifts and spins until it meets something. That is the thing the
        range's arch of targets is there to find out about; setting it static instead would be the
        other answer. Last in the struct so every existing brace-initialised prop stays as it is.
    */
    bool  f_floating = false;
    /*
        Which LOOK a prop has, where one kind has several - see TargetVariant. The rules never read
        it: a target is a target whatever it looks like. The app picks the model, the collider and
        the weight by it. Last again, for the same reason as f_floating.
    */
    int   variant = 0;
};

/*
    The targets there are, as the app builds them. Only the app reads this; it is here so the level
    can say which it wants.

      TARGET_BOARD   the plain board on end: light, and it topples when struck - the original.
      TARGET_STAND   archer.glb's archery_target, a round board on a wooden tripod: heavy enough
                     that an arrow does not knock it over, and scored by the ring an arrow hits.
                     A kick still tips it.

    `w` and `h` are the box the level author places it by (y is its centre, as for every prop), so
    a stand wants h = 1.40 to stand on the ground - its height at the character's scale.
*/
enum TargetVariant{
    TARGET_BOARD = 0,
    TARGET_STAND
};

/*
    Scenery with writing on it. NOT a prop: nothing collides with it and no body is built for it,
    so it has no size here and the rules never read it. It lives in the Stage only so that a
    level's whole layout, words included, is in one place.

    The variant picks the model; the model says where its text goes (apps/archer/Sign.h), so the
    level gives only the strings, in slot order - text[0] on the model's text_0 and so on.
    STRING LITERALS ONLY: they are held as pointers, and levels are built from code.
*/
enum SignVariant{
    SIGN_POST = 0,          //archer.glb's signpost: two arrow boards, text_0 the upper one
    SIGN_VARIANT_COUNT
};

#define STAGE_SIGN_MAX_TEXTS 4

struct StageSign{
    int   variant = SIGN_POST;
    float x = 0.0f;
    float y = 0.0f;         //the ground it stands on, not a centre - the models are base-down
    float z = 0.0f;         //depth in the slab; behind her (negative) keeps it out of her way
    float yaw_deg = 0.0f;   //about +Y; 0 faces the camera
    const char* text[STAGE_SIGN_MAX_TEXTS] = {};
};

/*
    A piece of authored scenery - a model out of archer.glb stood in the level as it is. Like a
    sign, NOT a prop: no rigid body, and the rules never read the model.

    PLACED BY ITS WALKABLE SURFACE, not its origin. The terrain tiles are the first of these, and
    their origin sits inside the rock, off-centre; what a level cares about is where the grass
    is. So (x, y) is the centre and height of the model's flat top, and the app fits the model to
    it - measured off the up-facing triangles, so the grass blades sticking up above the surface
    do not count (on terrain_tile_big they would have lifted her a quarter of a unit off it).

    A COLLIDER IS OPTIONAL: collider_hw > 0 makes AddScenery put an invisible SOLID block under
    that surface, its top exactly at y. The level types its size rather than the rules measuring
    the mesh, because Stage builds headless and must not load a file; the app checks the two
    agree when it loads the model and says so if they do not. Axis-aligned, so a model with a
    collider should keep yaw_deg at 0.
*/
enum SceneryVariant{
    SCENERY_TILE_BIG = 0,   //terrain_tile_big: a flat-topped chunk of ground, about 3.3 wide at her scale
    SCENERY_TILE_ROUND,     //terrain_tile_round: a rounder one, about 3.9 wide
    SCENERY_VARIANT_COUNT
};

struct StageScenery{
    int   variant = SCENERY_TILE_BIG;
    float x = 0.0f;         //centre of the walkable top
    float y = 0.0f;         //its height
    float z = 0.0f;
    float yaw_deg = 0.0f;
    float collider_hw = 0.0f;   //0: no collider
    float collider_hh = 0.0f;   //the block hangs this far below y, times two
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
    float s = 0.0f;     //how far down the rope from its anchor - what a grab and a climb are in
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
/*
    CLIMBING, with the aim keys - both hands are on the rope, so there is no aim to tilt.

    A SPEED THE RULES CHOOSE, not the clip's. Rope_Climbing's own rise is 0.305 world units a second
    at her scale, which makes the rope scene's nine units a thirty-second climb; this is about twice
    that. The animation does not stretch to it by playback rate - its playhead is set from the
    distance climbed (Puppet::ClimbTimeAt) - so any number here keeps the gripping hand on the rope,
    and the only thing it changes is how brisk the hand-over-hand looks.
*/
#define ROPE_CLIMB_SPEED            0.60f
#define ROPE_CLIMB_DEADZONE         0.30f   //of the aim axis, so a resting stick does not creep

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

    MEASURED, not guessed. Kick_Front as played is 66 ticks long and its boot reaches furthest from
    the hips at tick 22 - found by posing the model and watching both feet, see
    ApplicationArcher::MeasureKickClip. The app logs that measurement next to this window every
    start and says so loudly when the two stop lining up, which is what a re-export with a
    different impact frame would look like.

    IT HAS EARNED ITS KEEP TWICE. The clip was 98 ticks when these numbers were first fitted and
    86 after twelve frames came off its END in Blender; trimming the end does not move the strike,
    so only the total changed. Then on 2026-09-25 ten frames came off its FRONT, at load, through
    Kick_Front's trim_start in ARCHER_CLIPS - her dropping her guard, which the crossfade in does
    better - and that moves everything by 20 ticks: 86 -> 66, the strike 42 -> 22, the window
    40..44 -> 20..24.

    The active window stays FIVE TICKS wide for the reason below; it has simply moved to where the
    boot actually is. Everything else about the shape of the move is unchanged.

    THE COST, stated plainly because it is a real one: `f_planted` roots her for the whole of
    kick_ticks, so a kick is a 1.1-second commitment, of which 0.73s is recovery after the boot
    has already landed. That is a heavy, committal move. If it wants to be lighter, the fix is
    to unroot at KICK_ACTIVE_TO and let the recovery be cancelled by moving - which needs the
    Puppet to drop the clip at the same moment, or the animation would be overruling the rules.

    AND IT ONLY HAPPENS ON THE GROUND. A kick off the ground was allowed once, as a flying kick;
    it was a second and a half of hanging motionless in the air playing a clip that has a plant in
    it, and the plant is what a kick IS. See the gate at the top of Stage::TickKick.
*/
#define KICK_TICKS                  66      //the whole move; Kick_Front trimmed is 1.100s
#define KICK_ACTIVE_FROM            20      //wind-up before this
#define KICK_ACTIVE_TO              24      //recovery after; the boot connects at tick 22 of 66
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

/*
    --- KNEELING ---------------------------------------------------------------------------------
    C kneels, C again stands - a stance she gets into and out of, not a hold (animation_plan.md,
    Step 2). Kneeling she cannot run, jump, kick or take a rope, and she keeps her facing; she CAN
    draw, aim and loose. Only from the ground, and never mid-kick.

    THE TRANSITIONS ARE TIMED BY THE CLIPS, the KICK_TICKS arrangement: Stand_ToKneel and
    Kneel_ToStand set these, and the app warns with the number to type when a re-export moves
    them. Timed to where each clip's HIP SETTLES, not to its end - both hold still for a long
    tail (Stand_ToKneel is down at 0.67s of 1.53), which is never played; see Puppet::clip_settle.
    A press during either transition is ignored; standing up starts only from the held kneel.

    SHE IS SHORTER while down: the body box keeps its feet and loses its top, to 2 * KNEEL_HALF_H
    tall (`pos` stays the centre of the STANDING box, so the feet are pos.y - ARCHER_HALF_H in
    every stance and nothing that places the model has to know). It shrinks as she starts down and
    grows back as she starts up - which is why standing up is REFUSED with something low overhead.
    Moot while a kneel cannot move; it is the box a crouch-walk will need. MEASURED from Kneel_Idle
    against Idle, head over toes; the app reports the ratio at every start.

    And the arrow leaves LOWER: the kneeling anchor, measured like BOW_NOCK_UP/FWD - the nocked
    arrow's origin at full draw, averaged over the aim range. The anchor eases between the two
    over a transition, so a draw held through one keeps its arc on the bow.
*/
#define KNEEL_DOWN_TICKS            40      //Stand_ToKneel's hip settles at 0.667s
#define KNEEL_UP_TICKS              42      //Kneel_ToStand's at 0.700s
#define KNEEL_HALF_H                0.54f   //60% of her standing height, head over toes
#define KNEEL_NOCK_UP               0.02f   //the kneeling anchor, above the STANDING centre, averaged
#define KNEEL_NOCK_FWD              0.07f   //over aim -60..+80 (measured 2026-09-24)

/*
    --- GETTING UP -------------------------------------------------------------------------------
    The level entry: she starts lying on the ground and gets up, and nothing the player does counts
    until she is on her feet. Stage::Tick hands the whole tick an EMPTY ArcherInput while it runs,
    so "no input" is true of every verb by construction - no move, jump, draw, aim, kick, rope or
    kneel, and nothing buffered to fire the moment it ends - rather than being a gate each verb has
    to remember.

    NOT started by Reset, which every rules test calls and none of them wants to wait 3.5 seconds
    through. The app calls StartGetUp when a level starts; a test that wants it calls it too.

    THE WHOLE CLIP, not timed to a settle the way the kneel is: this one is meant to play to its
    end. So GETUP_TICKS is Laying_StandingUp's full length, and the app warns with the number to
    type when a re-export changes it - the KICK_TICKS arrangement.
*/
#define GETUP_TICKS                 210     //Laying_StandingUp is 3.500s

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
    MODE_ROPE,          //on the rope, where rp3d owns the body instead - the rope slice
    MODE_KNEEL,         //down on one knee, or getting down or up - see kneel_phase
    MODE_GETUP          //the level entry: lying, getting up, no input - see GETUP_TICKS
};

//Where in a kneel she is, while MODE_KNEEL.
enum KneelPhase{
    KNEEL_LOWERING = 0,     //Stand_ToKneel, KNEEL_DOWN_TICKS
    KNEEL_HELD,             //Kneel_Idle, for as long as she stays down
    KNEEL_RISING            //Kneel_ToStand, KNEEL_UP_TICKS
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
    bool  f_kneel_pressed = false;  //edge: kneel, or stand back up
};

/*
    A forecast of her next landing - see Stage::PredictLanding. `ticks` counts from now: 1 is the
    next tick. f_caught instead of f_lands when a ledge catch comes first.
*/
#define STAGE_PREDICT_TICKS         30      //half a second: the longest landing lead-in is 18 ticks
struct StageLanding{
    bool  f_lands = false;
    bool  f_caught = false;
    int   ticks = 0;
    float speed = 0.0f;             //the land_speed that tick will report
    v2    pos;                      //where the body box's centre will be
};

/*
    Where a live arrow will strike a BLOCK, if it does - see Stage::PredictArrowImpact. `ticks`
    counts from now, 1 being the next tick. Props (crates, targets) are the app's to find: it
    raycasts the path the forecast hands back, as it does the real flight.
*/
struct StageArrowImpact{
    bool  f_hits = false;
    int   ticks = 0;
    v2    point;
    int   block = -1;
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
    float shot_aim_deg = 0.0f;      //and the angle it left at, sway included
    bool  f_bumped_head = false;
    bool  f_grabbed_ledge = false;  //caught a lip this tick
    bool  f_released_ledge = false; //let go of one, by choice or by dropping
    bool  f_climbed = false;        //finished pulling up over one
    bool  f_kick_started = false;   //the boot went out; the connect comes a few ticks later
    bool  f_kick_connected = false; //...and hit at least one thing
    bool  f_knelt = false;          //started down onto one knee
    bool  f_stood = false;          //finished standing back up
    bool  f_got_up = false;         //the level entry finished; the controls are hers
    bool  f_stand_blocked = false;  //asked to stand with no room overhead; stays down

    //--- The rope -------------------------------------------------------------------------------
    //The app acts on these by creating and destroying the joint that makes the swing real.
    bool  f_grabbed_rope = false;
    int   grabbed_rope_id = -1;     //which link, by the id it was added with
    bool  f_released_rope = false;
    bool  f_rope_jump = false;      //let go WITH jump, so the app adds ROPE_JUMP_BOOST
    bool  f_rope_lost = false;      //let go because what she held came away - see Stage::TickRope

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
    //Lays her down where she stands - dropped onto whatever is under her - and starts the level
    //entry: GETUP_TICKS with no input. See GETTING UP.
    void StartGetUp();

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
    //Her top speed on this level: ARCHER_RUN_SPEED, or ARCHER_RANGE_RUN_SPEED on the range.
    float RunSpeed() const;

    //One tick. The whole simulation, and the only way state changes.
    void Tick(const ArcherInput& in, StageEvents& events);

    //--- The world ------------------------------------------------------------------------------
    std::vector<StageBlock> blocks;
    std::vector<StageProp>  props;
    std::vector<StageSign>  signs;
    std::vector<StageScenery> scenery;

    /*
        Makes the blocks' CURRENT geometry the level's, so that Reset puts it back instead of
        BuildLevel's. The app calls this when an editor has moved boxes and the terrain has been
        regenerated to match - without it, the first restart would snap every box back while the
        terrain stayed where they had been moved to.

        Geometry only: x, y, hw and hh. Kind and f_alive still come from BuildLevel, so a restart
        still brings a broken wall back. Ignored if the block count no longer matches, which is
        BuildLevel having been edited since - the code is newer than the edit.
    */
    void KeepBlockLayout();

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
    void AddRopePoint(float x, float y, int id, float s = 0.0f);

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

    //--- Kneeling -------------------------------------------------------------------------------
    int   kneel_phase = KNEEL_LOWERING; //meaningful only while MODE_KNEEL
    int   kneel_ticks = 0;          //counts up through a transition; 0 while held
    int   getup_ticks = 0;          //counts up through MODE_GETUP, to GETUP_TICKS
    //How far down she is, 0 standing .. 1 kneeling, eased through the transitions. The anchor
    //moves by it; the Puppet reads the phase instead.
    float KneelAmount() const;
    //The body box's height: 2 * ARCHER_HALF_H standing, 2 * KNEEL_HALF_H from the moment she
    //starts down until she starts up. Its bottom is always pos.y - ARCHER_HALF_H.
    float BodyHeight() const;
    //Is there room to stand up - would the standing box fit where she kneels?
    bool  CanStandUp() const;

    //--- The rope -------------------------------------------------------------------------------
    int   rope_id = -1;             //which rope point was caught, while MODE_ROPE; the app's handle
    int   rope_ticks = 0;           //how long it has been held - see ROPE_MIN_HOLD_TICKS
    int   rope_cooldown = 0;
    /*
        THE GRIP, as a distance down the rope from its anchor - where the app anchors the joint.
        Set by the grab to the caught point's `s`, moved by climbing, and held inside the span of the
        points the app offers, so she can climb only as far as she could have caught it.
    */
    float rope_s = 0.0f;
    int   rope_climb = 0;           //this tick: +1 up, -1 down, 0 holding still
    float rope_climbed = 0.0f;      //since the catch, signed, up is + - the climb clip's playhead
    //This tick's pump, -1..1 along world X: the lean the app pushes the swing with, recorded so the
    //animation can show it (her legs kick with it). 0 off the rope.
    float rope_pump = 0.0f;
    v2    climb_from;              //where the climb started and ends, captured on entry so the
    v2    climb_to;                 //lerp cannot drift if anything else touches pos
    int   grab_cooldown = 0;        //see LEDGE_RELEASE_COOLDOWN

    //--- The bow --------------------------------------------------------------------------------
    int   bow_mode = BOW_IDLE;
    int   draw_ticks = 0;           //0..BOW_DRAW_TICKS
    float aim_deg = 20.0f;          //relative to facing; + is up. Survives a release, so the next
                                    //shot starts where the last one was aimed.
    //0..1: BOW_MIN_POWER at the nock, rising over the pull to 1 at BOW_DRAW_TICKS. Before the nock
    //there is no shot to have a power; it reads BOW_MIN_POWER so the arc has something to draw.
    float DrawPower() const;
    //Is an arrow on the string - drawing, and at least BOW_NOCK_TICKS in? Only then can she loose.
    bool  IsNocked() const { return bow_mode == BOW_DRAWING && draw_ticks >= BOW_NOCK_TICKS; }
    int   draws_cancelled = 0;      //let go before the nock; see BOW_NOCK_TICKS
    int   sway_ticks = 0;           //ticks since the nock, uncapped - draw_ticks stops at full
    int   draws_started = 0;        //every draw begun; picks where its sway starts
    //The sway on top of aim_deg right now, degrees - 0 unless nocked. See AIM_SWAY_STAND_DEG.
    float AimSwayDeg() const;
    //What the arrow is actually aimed at: aim_deg plus the sway. AimDirection follows it.
    float ShotAimDeg() const { return aim_deg + AimSwayDeg(); }

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

    /*
        The same, for an arrow already in flight: where it will strike a block, and when, within
        `horizon` ticks. It runs FlyArrow - the step TickArrows takes - on a copy of the arrow, so
        it is the flight and not a sketch of it; an arrow's flight takes no input, so nothing can
        make it wrong except the app's props, which the rules never see. `path`, if given, gets
        the positions the arrow sweeps through, one per tick after the first - the segments the
        app raycasts for props, as ResolveArrowsAgainstProps does the real ones.
    */
    StageArrowImpact PredictArrowImpact(int index, int horizon, std::vector<v2>* path = NULL) const;

    //The nocked arrow's ANCHOR - where the string holds it - for the current facing and aim. An
    //arrow's first sweep starts here; see ARROW_LENGTH.
    v2    AnchorPosition() const;
    //Where the arrow's TIP is at the loose: the anchor plus ARROW_LENGTH along the aim. Loose()
    //spawns the flying point here and PredictArc draws from here.
    v2    MuzzlePosition() const;
    v2    AimDirection() const;

    /*
        Where and when she comes down, if she keeps doing what she is doing - PredictArc's idea
        for her own body: a COPY of the rules ticked forward, so the forecast is the flight, not a
        model of it. Exact while the input holds; the moment it changes, the next tick's forecast
        is the one to believe. That is the prediction half of rollback netcode (a remote player's
        input is assumed unchanged until the real one arrives), pointed at the future.

        `in` is this tick's input. HELD things stay held and EDGES do not repeat - a jump pressed
        this tick is not pressed again next tick - which is also why a rope is never caught in a
        forecast: taking one is a press. A ledge IS caught, because the rules catch one on their
        own, and a catch ends the forecast as surely as a landing.

        Only from the air; anything else reports nothing. What the rules cannot see they cannot
        forecast: the obstacles are the app's copy of the props as of this tick.
    */
    StageLanding PredictLanding(const ArcherInput& in, int horizon = STAGE_PREDICT_TICKS) const;

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
    //What KeepBlockLayout recorded, laid over BuildLevel's blocks by Reset. Empty until then.
    std::vector<StageBlock> kept_layout;
    void BuildLevel();
    void BuildMainLevel();
    void BuildRangeLevel();
    void BuildRopeLevel();
    //Adds `s` to `scenery`, and its invisible collider to `blocks` if it has one. See StageScenery.
    void AddScenery(const StageScenery& s);
    void TickBow(const ArcherInput& in, StageEvents& events);
    void TickArcher(const ArcherInput& in, StageEvents& events);
    void TickArrows(StageEvents& events);
    /*
        One tick of an arrow's flight: gravity, then the sweep from where this step starts (the
        anchor on its first step - see ARROW_LENGTH) to `next`, against the blocks. Returns the
        block struck, or -1; moves nothing but the velocity. TickArrows and PredictArrowImpact
        both fly by it, which is what keeps the forecast honest.
    */
    int  FlyArrow(Arrow& a, v2& from, v2& next, v2& point, v2& normal) const;
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

    //--- Kneeling -------------------------------------------------------------------------------
    //The whole of MODE_KNEEL: the phase clock, standing up, and a planted body that still falls
    //if the floor goes.
    void  TickKneel(const ArcherInput& in, StageEvents& events);
    //The whole of MODE_GETUP: the clock, and a body that still falls if the floor goes.
    void  TickGetUp(StageEvents& events);
    //How much of the standing box's top is missing right now - 0 unless kneeling.
    float HeadDrop() const;

    //--- The rope -------------------------------------------------------------------------------
    //While MODE_ROPE the solver owns the archer's position, so this decides only one thing: when
    //to let go. The app writes pos/vel back from the body before each tick.
    void  TickRope(const ArcherInput& in, StageEvents& events);
    //A link within reach right now, or -1.
    int   FindRopePoint(float* out_s = NULL) const;

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
