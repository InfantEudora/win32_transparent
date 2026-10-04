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

    ONE EXCEPTION, AND ONLY IN ONE LEVEL: the arrow, and the aim it leaves along, are 3D (v3). In
    every level but the character scene the PLANE IS LOCKED (IsPlaneLocked) and the aim is
    flattened into it, so an arrow's z and vz are exactly 0 for its whole flight and it flies
    bit-for-bit the flight it flew when it was 2D. On the character scene's turntable she can face
    any way, and the arrow leaves the way she faces. Nothing else - not her body, not a block, not
    a prop - gains a z.

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

/*
    A point or a direction off the play plane - for the arrow and the aim only (see above). Built
    from a v2 only EXPLICITLY, with the z it is to have: a v2 quietly becoming a v3 at z 0 is how
    an arrow on the turntable would be teleported back into the plane without anyone noticing.
*/
struct v3{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    v3(){};
    v3(float _x, float _y, float _z):x(_x),y(_y),z(_z){};
    explicit v3(const v2& p, float _z):x(p.x),y(p.y),z(_z){};
    v3 operator+(const v3& o) const { return v3(x + o.x, y + o.y, z + o.z); };
    v3 operator-(const v3& o) const { return v3(x - o.x, y - o.y, z - o.z); };
    v3 operator*(float s) const { return v3(x * s, y * s, z * s); };
    v2 xy() const { return v2(x,y); };
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
#define BOW_AIM_NEUTRAL_DEG         20.0f       //where a level starts her aim, and where it goes back to
/*
    THE AIM ONLY MOVES WHILE THE BOW IS DRAWN. The rest of the time Up/Down mean other things -
    her balance on a branch, climbing the rope, which kick - and an aim that tilted along with them
    was a bow pointing at the ground on the next draw. (It used to tilt undrawn on purpose, so a
    shot could be lined up before the draw; in play that read as the aim having a mind of its own.)

    Standing still it is KEPT, however long, so shot after shot can go to the same place. But after
    BOW_AIM_RETURN_TICKS of moving without a draw she has plainly stopped shooting at that spot,
    and it goes back to neutral at BOW_AIM_RETURN_RATE_DEG. Moving is her speed past
    BOW_AIM_RETURN_SPEED either way, so running, jumping, falling, climbing and swinging all count;
    standing still pauses the count without clearing it, and a draw clears it.
*/
#define BOW_AIM_RETURN_TICKS        120         //2 s
#define BOW_AIM_RETURN_RATE_DEG     90.0f       //degrees per second on the way back
#define BOW_AIM_RETURN_SPEED        0.5f        //units per second
/*
    THE ARROW IS A SEGMENT: an ANCHOR (the nock, on the string), a direction (the aim) and a
    length. The tip - the point that flies and strikes - starts ARROW_LENGTH along the aim from
    the anchor, and the arrow's first sweep runs from the anchor, not from the tip. So anything
    between the string and the arrowhead catches it: pressed against a wall she hits the near face
    instead of burying the arrow in it, and a post thinner than an arrow is long cannot be shot
    through from point blank.

    MEASURED from the model rather than guessed, and typed in here because this header names no
    engine type (docs/bow_plan.md §5). The anchor is the nocked arrow's origin at full draw, relative
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
    degrees either way, narrowing as she kneels (docs/animation_plan.md, Step 2). DETERMINISTIC - a sum
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

    IT IS A CONE, not a line: a second drift, SIDEWAYS (AimSwaySideDeg), with periods of its own,
    measured off the aim rather than about world up so it stays a cone at any elevation - a turn
    about up would shrink to nothing aiming straight overhead. The up-and-down half is exactly the
    drift above, untouched, and the sideways half is squeezed as that one nears its peak, so the
    tip stays inside a circle of the amplitude rather than a square of it. On a locked plane the
    sideways half is LOOKS ONLY: the bow wanders in and out of the screen, and the arrow leaves
    flattened into the plane, where the camera can see where it goes and the props can be hit.
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

/*
    WHICH ARROW she takes from the quiver - apps/archer/docs/vine_plan.md section 8. RULES STATE, not the
    view's: what an arrow does when it lands is gameplay the moment any kind has a mechanic, and a
    replay has to reproduce which one was loosed. For now the rules only CARRY the kind - from the
    selection onto the arrow in Loose, and out on its hit - and nothing here treats the kinds
    differently: the vine a vine arrow grows is the view's, and visual only (section 9).
*/
enum ArrowKind{
    ARROW_NORMAL = 0,
    ARROW_VINE,
    ARROW_BAMBOO,
    ARROW_KIND_COUNT
};
//A kind's name, for the HUD, the log and MCP; "?" out of range.
const char* ArrowKindName(int kind);

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
    BLOCK_BREAKABLE,
    /*
        A rock that gives way under her - docs/bridge_crumble_plan.md section 2. Solid like any block
        until she stands on it; then it SHAKES for CRUMBLE_SHAKE_TICKS, still holding her, and is
        gone - whether or not she is still on it. Gone until the level restarts. See StageBlock::
        crumble_ticks and Stage::TickCrumbles.

        Nothing grows on it or heaps against it, the terrain does not melt it and the wind does not
        see it: it is a thing that is about to not be there.
    */
    BLOCK_CRUMBLE
};
/*
    How long a crumble block holds once she has stood on it: 24 ticks, 0.4 s - long enough to land
    and jump again without hurrying, too short to stop and think. The stepping stones' route check
    holds the crossing to it.
*/
#define CRUMBLE_SHAKE_TICKS         24

/*
    How deep a block is through the slab when it does not say: half of it, either side of the
    archer's walk line at z 0. The boxes, the terrain and the plants all read HalfDepth() rather
    than this, so a block that sets its own depth is that deep everywhere at once.
*/
#define STAGE_BLOCK_HALF_DEPTH      1.50f
/*
    Every block must still cover the play plane this far either side of z 0, whatever depth and
    offset it is given. The rules are 2D and never look, but the crates, targets and debris are
    rp3d bodies standing at z 0 and PROP_DEPTH deep, and a block slid back far enough would let
    them fall through a floor the archer is standing on. stage_test holds every level to it.
*/
#define STAGE_BLOCK_MIN_COVER       0.60f

/*
    A TREE she climbs by its arms - apps/archer/docs/plant_mechanics_plan.md, section 1.

    Declared HERE, in the rules, because the arms are gameplay: BuildTrees turns each into a
    one-way BLOCK_PLATFORM after the level is built, so the rules test can prove a tree climbable
    and a replay stands on the same arms. The trunk collides with nothing - it stands behind her
    walk line and she climbs past it - so it is the app's alone to draw, from these same numbers,
    as it will draw the tree's mesh when there is one.
*/
struct StageTreeArm{
    float top = 0.0f;           //the arm's walkable top
    float side = 1.0f;          //+1 out to the right of the trunk, -1 to the left
    float length = 2.0f;        //from the trunk's surface to the tip
};
struct StageTree{
    float x = 0.0f;             //the trunk's centre
    float base = 0.0f;          //where it stands
    float height = 10.0f;       //to the cut top
    float radius = 0.45f;
    /*
        The cut top as somewhere to stand, this wide - a one-way platform across the trunk at
        base + height, reached from the arm below like an arm is. 0 is a top she cannot stand on.
    */
    float top_width = 0.0f;
    //Looks only: drawn with archer.glb's bigtree_* pieces rather than as a blockout box.
    bool  f_bigtree = false;
    std::vector<StageTreeArm> arms;
};
/*
    A SNAKE BRANCH - docs/creature_plan.md section 4. A polyline in the world from a tree's trunk out,
    in front of her line, behind it, or across it: where the snakes live. Hers to look at only - it
    collides with nothing, she cannot stand on it - so it is no block and needs no tuning against
    her jump. Declared in the rules all the same, because a snake's path is gameplay and is built
    from these points; the app draws it from them too.
*/
struct StageSnakeBranch{
    std::vector<v3> points;     //from the trunk out; the first on the trunk's axis
    float radius = 0.09f;       //drawn, and how far above its centre line a snake's path runs
    int   tree = -1;            //the StageTree it grows from
};

/*
    A SNAKE PATH - docs/creature_plan.md section 4, built from a branch by Stage::BuildSnakePaths:
    from the branch's TIP in along its top to the trunk, then round and down the trunk to the
    ground, a helix. One open path per branch, so a snake owns a route rather than a graph to choose
    through: it patrols its branch, and a dropped one climbs back up from the foot of its own path.
    The trunk is shared by every branch's path, each helix starting where its branch leaves.

    The CONTACT line - on the surface, the way a snake's belly lies - with beside each point the
    way its back faces (`ups`), out of the branch's top or out from the trunk. A snake adds its own
    radius along the up. Straight between points, with points close enough round the trunk that the
    helix reads as a curve: rules code walks it, so it is plain arithmetic, not core's Spline.
*/
struct StageSnakePath{
    std::vector<v3>    points;
    std::vector<v3>    ups;         //unit, square to the path
    std::vector<float> dist;        //distance along the path to each point; dist[0] is 0
    int   branch = -1;
    int   trunk_from = -1;          //the first point on the trunk: before it, the branch
    float Length() const { return dist.empty() ? 0.0f : dist.back(); }
    //Where `s` along it is, clamped to the path: the point, the way on (unit), and the up.
    void  At(float s, v3& pos, v3& tangent, v3& up) const;
};
//The blockout trunk is a square box: a helix at the radius stays outside its faces but cuts its
//corners, so it runs out at the corners' distance. A round trunk (a mesh) would take the radius.
#define SNAKE_TRUNK_ROUND_OUT       1.42f
#define SNAKE_TRUNK_TURN_RISE       2.50f   //height the helix climbs per turn round the trunk
#define SNAKE_PATH_STEP             0.15f   //the helix's point spacing, along it

/*
    A SNAKE - docs/creature_plan.md section 4. GAMEPLAY, so it lives here: declared by the level,
    stepped every tick, in the state hash and in a recording's starting state, and the app only
    draws it from these numbers.

    Its head is `s` along its path (StageSnakePath), and its body lies on the path behind the head,
    `Length()` back the way it came, so every part of it passes where the head went. `dir` is the
    way the head is going: -1 out toward the tip (s falling), +1 in and down the trunk.

    PATROL, for now the only thing it does: runs and pauses, a glide rather than a spider's dart,
    between the branch's tip and `trunk_reach` down the trunk past the branch. At either end it
    stops, and TURNS by its head swapping to the other end of its body - a blockout's turn: the body
    stays where it lay and only which end leads changes. The model's turn can curl the head back
    over the body; the rules do not need to know. Now and then it turns back mid-patrol as well.

    Size follows the hit points, so a tougher one reads as one: see SNAKE_LENGTH / SNAKE_RADIUS.
    Every random draw is its own xorshift, seeded by the level, so a replay patrols the same way.
*/
enum SnakeState{
    SNAKE_PATROL = 0,
    SNAKE_STATE_COUNT
};
struct StageSnake{
    //--- The level's ---
    int   branch = -1;              //its home branch, and so its path (one per branch)
    int   hp_max = 1;               //1..3, and its size
    float start_s = 0.0f;           //where its head starts, along the path
    int   start_dir = -1;
    float trunk_reach = 2.0f;       //how far past the branch, down the trunk, its patrol goes
    uint32_t seed = 1;

    //--- State, stepped by Stage::TickSnakes ---
    int   path = -1;                //into Stage::snake_paths, resolved when the paths are built
    int   hp = 1;
    int   state = SNAKE_PATROL;
    bool  f_moving = false;         //a run, as opposed to a pause
    bool  f_turn = false;           //turn round when this pause ends
    int   ticks = 0;                //left in this run or pause
    float s = 0.0f;
    int   dir = -1;
    float v = 0.0f;                 //speed along the path now, never negative; `dir` is the way
    float run_speed = 0.0f;         //this run's
    float travelled = 0.0f;         //distance really covered - the view's slither runs off it
    uint32_t rng = 1;

    float Length() const;
    float Radius() const;
    float Tail() const { return s - (float)dir * Length(); }
};
#define SNAKE_SPEED_MIN             0.40f   //a run's speed, units a second
#define SNAKE_SPEED_MAX             0.90f
#define SNAKE_ACCEL                 1.50f   //to a run's speed and back to rest, units a second per second
#define SNAKE_RUN_TICKS_MIN         60
#define SNAKE_RUN_TICKS_MAX         240
#define SNAKE_PAUSE_TICKS_MIN       40
#define SNAKE_PAUSE_TICKS_MAX       240
#define SNAKE_TURN_CHANCE           0.25f   //a pause mid-patrol that ends with a turn

//An arm is as thin as the level's other one-way platform - see the note on it in BuildMainLevel.
#define STAGE_TREE_ARM_HALF_H       0.15f
/*
    The trunk's centre and half-depth through the slab: behind her, clear of her body at z 0. The
    arms reach from its face forward across the play plane - they are blocks, and every block has
    to cover STAGE_BLOCK_MIN_COVER of it or a crate knocked onto one falls through.
*/
#define STAGE_TREE_Z                -1.30f
#define STAGE_TREE_HALF_DEPTH       0.45f
#define STAGE_TREE_ARM_Z            -0.55f
#define STAGE_TREE_ARM_HALF_DEPTH   1.20f
/*
    THE BIGTREE's numbers, measured off archer.glb's pieces at her scale (model_scale 1.82) so the
    arms she stands on are as long as the drawn ones: the trunk's radius off bigtree_segment, each
    arm's walkable top from the trunk's face to its tip off bigtree_arm_1 (it grows right) and
    bigtree_arm_2 (left), the cut top's width off bigtree_top. The app measures the same pieces at
    load and warns if a re-export has moved them - these are what the rules and the test play.
*/
#define BIGTREE_RADIUS              0.51f
#define BIGTREE_ARM_RIGHT_LENGTH    1.54f
#define BIGTREE_ARM_LEFT_LENGTH     1.60f
#define BIGTREE_TOP_WIDTH           0.97f
//And mushroom_big's, for a bounce pad drawn with it: the cap's walkable top over its base, and its width.
#define MUSHROOM_BIG_CAP_TOP        1.15f
#define MUSHROOM_BIG_CAP_WIDTH      2.19f

/*
    A SPRING PLANT she stands on and is thrown by - docs/plant_mechanics_plan.md, section 2: the bounce
    pad (a mushroom cap) and the leaf.

    ONE SPRING, TWO SHAPES. Each has a single coordinate `q` and a spring pulling it back to rest:
    the pad's is how far its cap has sunk, the leaf's the angle it is bent to about its stem. What
    she stands on is the same for both - a one-way surface that MOVES, at the speed SurfaceVelY
    says - so the landing, the ride and the fling are written once. The leaf adds a slope, and past
    SPRING_LEAF_SLIP_DEG she slides down it.

    TUNED BY FEEL rather than by constants, the idea taken from core/physics/SpringHinge: how far
    her WEIGHT moves it at rest (`give`), how fast it swings with nobody on it (`hz`) and how fast
    that swing dies (`damping`, a ratio - 0 rings forever, 1 settles without overshoot). Stiffness,
    inertia and damping are worked out from those. Her mass is 1; everything else is relative.

    SHE IS PART OF THE SPRING while she stands on it. Her weight bends it and her mass slows it - a
    leaf with her at the tip swings far slower than an empty one - and landing hands it her fall as
    momentum about the stem, so a landing at the tip bends a leaf harder than one by the stem. She
    rides it at its own speed, so when it springs back faster than she can fall she leaves it with
    that speed: the rebound hop. A jump while it rises adds the rise to the jump: the fling.

    Not a block, and no rp3d body: crates and arrows pass through both. The app draws them from
    these same numbers.
*/
enum SpringPlantKind{
    SPRING_PAD = 0,         //a mushroom cap: flat, sinks straight down
    SPRING_LEAF             //a leaf on a stem: bends about the stem, and slopes
};
struct StageSpringPlant{
    int   kind = SPRING_PAD;
    v2    root;                 //PAD: the middle of the cap's top at rest. LEAF: the stem it bends about
    float length = 2.0f;        //PAD: the cap's width. LEAF: stem to tip
    float side = 1.0f;          //LEAF: +1 grows out to the right, -1 to the left
    float rest_deg = 0.0f;      //LEAF: its angle above the horizontal with nobody on it
    float base = 0.0f;          //PAD: where its stalk stands - looks only
    float give = 0.2f;          //her weight, at rest: PAD units sunk; LEAF degrees bent with her at the tip
    float hz = 3.0f;            //the swing with nobody on it
    float damping = 0.2f;       //and how fast it dies away, as a ratio
    float travel = 1.0f;        //how far it can go either side of rest: PAD units, LEAF degrees
    bool  f_mushroom = false;   //PAD, looks only: drawn as archer.glb's mushroom_big, not a box

    //--- State, stepped by Stage::TickSpringPlants ---
    float q = 0.0f;             //PAD: the cap's height above rest, sunk is negative. LEAF: its angle, radians
    float qd = 0.0f;            //and its rate
    float prev_q = 0.0f;        //last tick's q - the surface she was above, for the one-way test

    float Rest() const;
    float Stiffness() const;
    float Inertia() const;      //of the plant alone, without her
    float Damping() const;
    float Travel() const;       //`travel` in q's units
    /*
        How much of q's rate shows as vertical speed at x: 1 anywhere on a pad, the distance out
        from the stem on a leaf. Also her LEVER on it - what her weight turns it by, and (squared)
        her share of its inertia while she stands there.
    */
    float Lever(float x) const;
    bool  Covers(float x) const;        //a pad, if any of her is over the cap; a leaf, if her centre is
    float SurfaceY(float x, float at_q) const;
    float SurfaceY(float x) const { return SurfaceY(x,q); }
    float SurfaceVelY(float x) const { return Lever(x) * qd; }
    float SlopeDeg() const;             //+ rising away from the stem; 0 on a pad
    v2    Tip() const;                  //LEAF: where it ends
};
/*
    Steeper than this and she slides down a leaf. The pull is gravity along the slope less a
    friction that holds her at exactly this angle, so a leaf just past it creeps and a bent one
    throws her off. Low, because a leaf is slippery and "it bends and you slide off" is the point.
*/
#define SPRING_LEAF_SLIP_DEG        14.0f
//What input can do on a slide, as a share of its pull: holding uphill slows one, it does not climb it.
#define SPRING_LEAF_SLIDE_CONTROL   0.35f
//A slide's top speed along the surface - faster than her run (9), so a long one is worth riding.
#define SLIDE_MAX_SPEED             14.0f
/*
    THE SKID (docs/slide_plan.md): off the foot of a slide onto the flat she rides it out, rather
    than stopping dead at the run's friction (120 u/s^2, which stopped her off the long run's 12.7
    in a tenth of a second). The speed bleeds off at SKID_DECEL - about a second and seven units from
    the long run - or at SKID_BRAKE pushing against it. Pushing along it hands her back to the run
    once she is down to running pace; she cannot speed a skid up. Only off a slide at SKID_MIN_SPEED
    or more, and over at SKID_END_SPEED. The animation plays the surf for it.
*/
#define SKID_DECEL                  12.0f
#define SKID_BRAKE                  40.0f
#define SKID_MIN_SPEED              2.0f
#define SKID_END_SPEED              0.8f
/*
    The most a fling can throw her at. A timed bounce is her jump plus the rise, and without a cap
    every bounce off a pad lands harder and so throws her higher than the last. 28 is about 9.3 of
    rise, three jumps' worth.
*/
#define SPRING_MAX_LAUNCH           28.0f
//How far she steps UP off a spring plant onto a block beside it - a leaf bent below the ledge it grows
//from would otherwise leave her walking into the ledge's face. Only while standing on one.
#define SPRING_STEP_UP              0.40f
/*
    PUMPING, the swing's trick: legs driven down into the plant as she comes onto it, and swung up
    as it throws her. Both are TIMED, like the jump: the stomp counts only the aim held down in the
    last ticks of the fall, the swing only an Up press in the first ticks after she leaves it.

    On the aim axis - the Up/Down keys and the right stick - because that is the stick a player
    already has a thumb on, and the move keys are steering the flight. It tilts the bow as well;
    the aim survives a landing, so that costs a re-aim at worst.
*/
#define SPRING_PUMP_AIM             0.5f    //how far the aim axis has to go, either way
#define SPRING_STOMP_TICKS          10      //down held this long before touching down is a full stomp
#define SPRING_STOMP_GAIN           0.30f   //which lands her this much harder into it
#define SPRING_SWING_WINDOW         10      //ticks after it lets go of her that an Up press still swings
#define SPRING_SWING_FULL           3       //at full strength within these, fading to none at the window
#define SPRING_SWING_GAIN           0.25f   //of what it threw her with (launch_lift), added to her rise

/*
    A THIN BRANCH she walks along and has to keep her balance on - docs/plant_mechanics_plan.md,
    section 3. A straight one-way line from one end to the other: landed on from above, dropped
    through with Down, like a platform with no thickness. Rigid for now; sagging under her is later.

    Declared in the rules because she stands on it and because the balance is a rule. The app draws
    it from the same two points.
*/
struct StageBranch{
    v2    a;                    //the left end
    v2    b;                    //the right end
    bool  Covers(float x) const { return x >= a.x && x <= b.x; }
    float SurfaceY(float x) const { return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x); }
    float Slope() const { return (b.y - a.y) / (b.x - a.x); }
};

/*
    A RAMP: a sloped floor that does not move - docs/plant_mechanics_plan.md, "Sliding". A leaf held
    still, which is exactly what it is for: the slide is tuned here, at fixed angles, rather than on
    a leaf whose angle changes under her while she slides.

    A straight one-way line like a branch, but GROUND rather than a beam: Down does not drop
    through it, walking off a block onto it keeps her feet on it, and walking up it into the block
    at its top steps her up. Past `slip_deg` she slides, by the same rule as a leaf. The level
    seals the space under it with blocks - the low end on the floor, the high end against a face.
*/
struct StageRamp{
    v2    a;                    //the left end
    v2    b;                    //the right end
    float slip_deg = SPRING_LEAF_SLIP_DEG;  //steeper than this and she slides - the leaf's, by default
    bool  Covers(float x) const { return x >= a.x && x <= b.x; }
    float SurfaceY(float x) const { return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x); }
    float Slope() const { return (b.y - a.y) / (b.x - a.x); }
};
/*
    Is (x, y) inside the space under any of `ramps` - over its run, below its line - grown by
    `margin` both ways? The terrain draws that space filled (docs/terrain_plan.md section 12), and a
    floor's top runs on under it, so this is what the dressing asks before standing anything there.
*/
bool InsideRampWedge(const std::vector<StageRamp>& ramps, float x, float y, float margin);

/*
    A ROPE BRIDGE - docs/bridge_crumble_plan.md section 3. Planks hung between two pinned anchors, a
    chain of POINTS stepped in the rules every tick: each point a small mass under gravity, each
    plank a spring that pulls when stretched past its length and never pushes - a rope, not a rod.
    Semi-implicit Euler in BRIDGE_SUBSTEPS fixed substeps, so it is deterministic and stable at the
    stiffness a bridge wants.

    IT SAGS under its own weight, and more under hers: while she stands on it her mass is on the
    two points either side of her, shared by where she stands between them, so the dip travels
    with her and the planks under her feet stay nearly level. A LANDING hands those two points
    her fall as momentum, like a plant's, so a hard landing drives it down and it bounces back.

    She stands on it as a surface (SURFACE_BRIDGE): one-way, landed on from above, dropped through
    with Down like a branch; but held to like a ramp when she walks onto it off the block at an
    anchor, since the first plank already hangs a little below that block's top.

    `planks` and `slack` are the level's; the rest is state. Collides with nothing in rp3d: crates
    and arrows pass through it. The app draws a plank between each pair of points.
*/
/*
    Substeps and stiffness go together: the fastest thing the chain can do is two neighbours
    zig-zagging, at sqrt(4 k / m), and semi-implicit Euler holds that only while the substep is
    well inside its period - with the plank damping on top, which shortens the limit. At 8 substeps
    and a damping of 12 it was past it: the bridge never settled, holding a wobble of 0.9 a second.
*/
#define BRIDGE_SUBSTEPS             12
//Each point, in her masses: thirteen of them weigh about twice her, a plank bridge's heft, and
//what keeps her run from whipping it - a lighter chain sent waves along it at 14 units a second.
#define BRIDGE_POINT_MASS           0.15f
#define BRIDGE_STIFFNESS            7500.0f //per plank: its own weight stretches the most-pulled 1.5%
#define BRIDGE_PLANK_DAMPING        4.0f    //along each plank, on how fast it is stretching
/*
    On every point, per second - what settles a swing, and what keeps a run across from whipping
    it. Heavy on purpose: a light chain carries waves at about twice her run speed, and at 1.5 a
    run left it thrashing at 14 units a second. Ropes, knots and planks rubbing are a lot of friction.
*/
#define BRIDGE_AIR_DAMPING          4.0f
//How far she steps UP off a bridge onto the block at an anchor: her weight near one pulls the last
//plank down below the block's top by more than SPRING_STEP_UP, and she walked into its face.
#define BRIDGE_STEP_UP              0.9f
//Steeper than this under her feet and she slides - far past a leaf's, since a plank has grip
//and her own weight keeps the planks under her level. What slides her is a broken half, later.
#define BRIDGE_SLIP_DEG             30.0f
//Steeper than this and a plank is no floor at all: she falls off it. A snapped half swinging down
//goes from walkable, through sliding (past BRIDGE_SLIP_DEG), to this.
#define BRIDGE_STAND_DEG            50.0f

/*
    --- STRAIN (docs/bridge_crumble_plan.md section 3, "Strain, warnings and the snap") ---------------
    A breakable bridge keeps a STRAIN per plank, 0 sound .. 1 snapped, and it never heals within a
    run. Only LANDINGS add to it - walking, running and standing add nothing, however it bounces:
    what hurts a bridge is her coming down on it. By how hard, against the bridge's own speed under
    her, past BRIDGE_COMFORT_SPEED; a stomp (the aim held down through the fall, as on the pad)
    lands harder still, and drives it down harder too.

    The plank she lands on takes it all, its neighbours half, and the rest of the bridge a share -
    so landing on one spot breaks it there sooner, but spreading the landings about does not make
    it last for ever. Warnings at a third and two thirds, each reported once with the plank and the
    level: `strained`, `cracking`. At 1 that plank SNAPS: it is no longer a spring or a floor, and the
    two halves swing down from their anchors, still simulated.
*/
#define BRIDGE_COMFORT_SPEED        8.0f    //a landing slower than this adds nothing
#define BRIDGE_STRAIN_PER_SPEED     0.026f  //per unit/s past it, on the plank she lands on
#define BRIDGE_STRAIN_NEIGHBOUR     0.5f    //of that, on the planks either side
#define BRIDGE_STRAIN_SPREAD        0.2f    //and on every other plank
#define BRIDGE_STRAINED             0.33f
#define BRIDGE_CRACKING             0.66f
enum BridgeLevel{
    BRIDGE_SOUND = 0,
    BRIDGE_LEVEL_STRAINED,
    BRIDGE_LEVEL_CRACKING,
    BRIDGE_LEVEL_SNAPPED
};

struct StageBridge{
    v2    a;                    //the left anchor, at the top corner of the block it is tied to
    v2    b;                    //the right one
    int   planks = 12;
    float slack = 1.04f;        //its length over the span: how much it can hang
    bool  f_breakable = false;  //strains under landings and snaps - see STRAIN
    float link = 0.0f;          //one plank's length, from the two above (Stage::AddBridge)

    //--- State, stepped by Stage::TickBridges ---
    std::vector<v2> p;          //planks + 1 points; the first and last are the anchors
    std::vector<v2> v;
    std::vector<v2> prev_p;     //where the points were last tick - the surface she was above
    //Her mass this tick, on points `load_at` and `load_at + 1`: the share on the second. -1 none.
    int   load_at = -1;
    float load_t = 0.0f;
    std::vector<float> strain;  //per plank, 0..1
    std::vector<uint8_t> broken;//per plank: snapped, neither spring nor floor
    int   level = BRIDGE_SOUND; //the worst warning reported so far, a BridgeLevel

    bool  Covers(float x) const { return Plank(x,p,NULL) >= 0; }
    /*
        The plank under x that is a floor: whole, spanning x, no steeper than BRIDGE_STAND_DEG -
        the highest there if more than one is (a snapped half folds back under itself). And how far
        along it x is. -1 for none. Whole, the points run left to right and it is the one plank.
    */
    int   Plank(float x, const std::vector<v2>& pts, float* out_t) const;
    float MaxStrain() const;
    float SurfaceY(float x) const;
    //The plank under `x_now`, as it was last tick, under `x_then` - clamped to its ends, so a move
    //that starts off the bridge's end is measured against its end plank rather than against nothing.
    float SurfaceYThen(float x_then, float x_now) const;
    float SurfaceVelY(float x) const;
    float Slope(float x) const;             //dy/dx of the plank under x
    float Lowest() const;                   //the lowest point's y
    float Mass(int i) const;                //point i's, with her share on it
};

/*
    A SPIDER WEB in a wooden frame - docs/web_plan.md. The rope bridge's chain made a net: NODES
    (points with a little mass, under gravity) and THREADS between them (springs that pull when
    stretched past their length and never push), stepped in WEB_SUBSTEPS fixed substeps.

    AT AN ANGLE TO HER PLANE (docs/README.md, "Gameplay is 2D; the world is 3D"). The net lives in a
    plane of its own: u along the web, v up (world y), n along its normal - how far a node bulges
    out of the plane. That plane is turned `angle_deg` about Y and crosses her plane (z = 0) along
    one vertical line, u = 0, at world x = `cx`: the whole of the web as her rules meet it. Its frame
    stands either side of her plane, one post in front and one behind; which way round is the
    level's (`angle_deg`), since it depends on which way she walks into it.

    THE SHAPE, from the declaration as BuildTrees builds arms from a tree: `spokes` from the hub to
    the frame's opening, ANCHORS where they meet it (they never move), and one thread SPIRALLING
    out between the spokes for `rings` turns - its nodes where it crosses a spoke, the spoke's own
    segments between them. Each thread's length is a little short of where it was hung
    (WEB_PRETENSION), so the web is taut and only sags a little under its own weight; it is
    settled at build time, like a bridge, so every run starts alike.

    ARROWS (Stage::ArrowThroughWebs): an arrow flies in her plane, so it meets the web's at ONE
    point, on the crossing line. Every whole thread within WEB_HOLE_RADIUS of that point snaps - a
    round hole, always on the line she has to pass through - each taking WEB_SLOW_PER_THREAD of
    its speed; an arrow left slower than WEB_CATCH_SPEED is CAUGHT, stuck where it met the web and
    riding the nearest node. A snap kicks its two ends along the arrow, so the hole shudders.

    HER (Stage::TickWebs): her body is a vertical capsule, ARCHER_HALF_W round, feet to head. Every
    whole thread's point nearest it is pushed out of it - a stiff, damped spring, shared by the
    thread's two ends - and the reaction slows her, along x only, before she moves: the bridge's
    way of handing her momentum. Her run drive is untouched, so she leans into the web with her
    run and it leans back harder the further it is stretched: an intact web holds her pressed in,
    and lets her go again softly (WEB_PUSHBACK_MAX). With holes along the line fewer threads take
    her, and once she presses against WEB_BURST_THREADS or fewer of them they SNAP - she bursts
    through the last few herself. A thread stretched past WEB_BREAK_STRAIN snaps too, as a
    backstop. No wall, no hard constraint: nothing to stick in.

    `cx .. hub_v` and the counts are the level's; the rest is built (Stage::AddWeb) and stepped.
    Collides with nothing in rp3d. The app draws the threads as lines and the frame as boxes, turned
    to the same angle (StageWeb::World).
*/
#define WEB_SUBSTEPS            12
#define WEB_NODE_MASS           0.02f   //in her masses: a web is light, and barely sags
#define WEB_ARROW_MASS          0.05f   //what a caught arrow hangs on its node - enough to see it droop
#define WEB_STIFFNESS           300.0f  //a thread's stiffness is this over its length, like silk of one gauge
#define WEB_THREAD_DAMPING      0.6f    //along each thread, on how fast it is stretching
#define WEB_AIR_DAMPING         3.0f    //on every node, per second: what lets a shiver die away
#define WEB_PRETENSION          0.95f   //a thread's length, of where it was hung - taut, not slack
#define WEB_SETTLE_TICKS        300     //stepped at build, so it starts still
#define WEB_HOLE_RADIUS         0.30f   //what an arrow tears out round where it passes through the web
#define WEB_SLOW_PER_THREAD     0.12f   //of an arrow's speed, per thread it snaps
#define WEB_CATCH_SPEED         12.0f   //an arrow slower than this after a snap is caught: a tap (25) after 6 threads, a full draw (46) after 11
#define WEB_SNAP_KICK           2.5f    //the speed a snap gives its two ends, along the arrow
#define WEB_CONTACT_STIFFNESS   2000.0f //her body against a thread: per unit inside her
#define WEB_CONTACT_DAMPING     8.0f    //...and on how fast it is closing - what keeps her from bouncing
/*
    The fastest the web may send her back, in units a second. The net stores the whole of her run in
    its threads and gives it back: uncapped, a run into a half-cut web came back at 8, a trampoline
    rather than the bridge's give. Soft and settling first, as the user asked; raise it for spring.
*/
#define WEB_PUSHBACK_MAX        3.0f
/*
    THE BURST: pressing against this many whole threads or fewer, she tears them. An explicit rule
    rather than only the strain edge, which sat between 0.6 (an intact web burst at once) and 0.7
    (it held) - too sharp to keep tuned through any change to her run.
*/
#define WEB_BURST_THREADS       4
#define WEB_BURST_PRESS         20.0f   //...when the web is pushing back on her at least this hard
#define WEB_BREAK_STRAIN        1.0f    //a thread stretched to twice its length snaps whatever else
/*
    The opening's bottom is the ground: a strand cut free falls to it and lies there, sliding to a stop
    at this much of its speed per second, rather than falling for ever. A web hung over a drop would
    want the floor under it instead - none is, yet.
*/
#define WEB_FLOOR_FRICTION      8.0f
#define WEB_MAX_SPOKES          32
#define WEB_MAX_RINGS           16

struct StageWebThread{
    int   a = -1;               //nodes
    int   b = -1;
    float rest = 0.0f;          //the length it pulls back to
    bool  f_cut = false;        //snapped
    bool  f_spoke = false;      //a spoke's segment rather than the spiral's - for the app's colour
};

//A caught arrow riding the web: Stage::arrows[arrow] is kept at the node's world point + offset.
struct StageWebCatch{
    int   arrow = -1;
    int   node = -1;
    v3    offset;
    v3    placed;               //where it was put last: an arrow anywhere else is no longer this one
};

struct StageWeb{
    //--- The level's ---
    float cx = 0.0f;            //where the web crosses her plane, world x - the middle of its opening
    float y = 0.0f;             //the opening's bottom, which is the ground under it
    float w = 3.1f, h = 3.5f;   //the opening: across, along the web, and up
    //Its u axis from +x toward +z, in degrees: positive puts the end she reaches first walking
    //right - the low-x one - BEHIND her plane. 0 would lay it flat in her plane, which it must not be.
    float angle_deg = 45.0f;
    int   spokes = 12;
    int   rings = 6;
    float hub_u = 0.0f;         //the hub, from the opening's middle - often a little off it
    float hub_v = 0.0f;

    //--- Built, and state, stepped by Stage::TickWebs ---
    std::vector<v3> p;          //nodes in the web's own frame (u, v, n); anchors among them
    std::vector<v3> v;
    std::vector<v3> p_built;    //where each node settled when built: what "across her path" is measured on
    std::vector<uint8_t> anchor;    //per node: fixed to the frame
    std::vector<StageWebThread> threads;
    std::vector<StageWebCatch> caught;
    int   side = 0;             //the side of the crossing she was last clear of it on: -1 low x, +1 high, 0 not yet
    bool  f_breached = false;   //WebBreached has been reported
    int   contacts = 0;         //threads touching her last tick
    float push = 0.0f;          //and how hard, along x, the web pushed her

    float Left() const  { return -0.5f * w; }       //the opening's edges, in u
    float Right() const { return 0.5f * w; }
    float Top() const   { return y + h; }
    float Cos() const;          //of angle_deg
    float Sin() const;
    //A point of the web's frame (u, v, n) in the world.
    v3    World(const v3& q) const;
    int   CutCount() const;
    float Mass(int i) const;    //node i's, a caught arrow's weight included
};

//Her body as a web sees it, in the web's frame: her axis at (u, n), from v0 (feet) to v1 (head),
//moving at (vel_u, vel_n). See StageWeb, HER.
struct StageWebBody{
    float u = 0.0f, n = 0.0f;
    float v0 = 0.0f, v1 = 0.0f;
    float vel_u = 0.0f, vel_n = 0.0f;
};

/*
    --- APPLES -----------------------------------------------------------------------------------
    docs/apple_plan.md. An apple hangs on its stem until an arrow frees it: through the STEM it is
    cut, and drops intact; through the APPLE it is hit, and goes off with the arrow in it. Either
    way it is LOOSE from then on, and rp3d's - falling, bouncing and rolling are what a solver is
    for. One she can reach, hanging or lying, she can PICK with the action button.

    THE SHOT IS 3D GEOMETRY. An apple is a sphere at (x, y, z) and its stem a vertical capsule above
    it, and an arrow's sweep is tested against both as they are - not against their outline in her
    plane. So an arrow at z 0 hits an apple only where it really passes through it: what the camera
    shows is what counted. Which apples are a SHOT follows from where the level hangs them, not
    from a flag: one within APPLE_STEM_HIT_R of her plane can be cut or hit, one within its radius
    only hit, and one deeper than that never - it is for picking (within APPLE_PICK_REACH_Z) or for
    looks. The level hangs its shootable ones within APPLE_ON_LINE_Z, so stem and body both
    straddle the walk line and an arrow through either is plainly through it on screen.

    PICKING is the kick's arrangement: the rules own the window and the tick the hand closes, the
    clip is played to fit it (Puppet), and the clip is a stand-in until a pick is exported - see
    PUPPET_PICK_CLIP. High or low by where the apple is: one at her feet is a low pick.
*/
#define APPLE_RADIUS                0.19f   //archer.glb's apple, 0.104 across its body x model_scale 1.82
#define APPLE_STEM_LEN              0.30f   //apple top to the twig; the model's own stem is the first 0.10
#define APPLE_STEM_HIT_R            0.06f   //the stem's capsule: generous, so it can be hit at bow range
//The share of the arrow's velocity a freed apple leaves with: a cut nudges it so it lands rolling
//rather than dropping dead, a hit carries it off.
#define APPLE_CUT_PUSH              0.04f
#define APPLE_HIT_TRANSFER          0.20f
#define APPLE_ON_LINE_Z             0.04f   //how far off her plane a shootable apple may hang
//Her reach, from her centre line at her feet: across, in depth, and up to the apple's centre.
#define APPLE_PICK_REACH_X          0.75f
#define APPLE_PICK_REACH_Z          0.65f
#define APPLE_PICK_REACH_UP         2.25f
#define APPLE_PICK_LOW_Y            0.80f   //an apple's centre below this above her feet is a low pick
/*
    The pick windows and the tick the hand closes, by height. FITTED TO THE STAND-IN CLIPS (see
    PUPPET_PICK_CLIP), measured 2026-10-01: Standing_DrawArrow is 1.07s and her right hand is at its
    highest, 1.57 above her feet, 25 ticks in - 39% of the way, so tick 19 of a 48-tick window (the
    clip at 1.33x). Stand_ToKneel to its settle is 40 ticks at 1x, and its hands are lowest at the
    end (0.69 up - it holds a rifle, so it never reaches the ground). A real pick clip sets these the
    KICK_TICKS way.
*/
#define APPLE_PICK_TICKS_HIGH       48
#define APPLE_PICK_CLOSE_HIGH       19
#define APPLE_PICK_TICKS_LOW        40
#define APPLE_PICK_CLOSE_LOW        36
//How far the apple may have rolled from where the press found it and still be taken at the close.
#define APPLE_PICK_SLACK            0.30f

enum AppleState{
    APPLE_HANGING = 0,
    APPLE_LOOSE,            //handed to the app: rp3d moves it, and writes back where (StageApple::at)
    APPLE_PICKED
};

struct StageApple{
    v3    hang;                     //its centre while it hangs
    float stem = APPLE_STEM_LEN;
    float radius = APPLE_RADIUS;
    int   tree = -1;                //the StageAppleTree it is drawn growing from, or -1; looks only
    int   state = APPLE_HANGING;
    //Where it is: `hang` while hanging; while loose, where the app last said the body is, before
    //each tick (Stage::SetLooseApple) - what a pick off the ground is measured against.
    v3    at;
    float Twig() const { return hang.y + radius + stem; }
};

/*
    An apple tree - archer.glb's tree_1, stood at (x, y, z) on the ground at y. LOOKS ONLY, like a
    sign: nothing collides with it and the rules never read it. Here so a level's apples and the
    trees they grow on are laid out in one place.
*/
struct StageAppleTree{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw_deg = 0.0f;
    float scale = 1.0f;             //on top of model_scale
};

/*
    THE SLIDE GALLERY, in the rope level - Stage::BuildSlideGallery. Named here so the rules test
    and the level agree on where each hill is without either typing a coordinate.
*/
#define SLIDE_GALLERY_FLOOR_Y       (-3.0f)     //the shallow pit's floor, which the gallery runs on from
#define SLIDE_GALLERY_START_X       (-27.0f)    //the first hill's right foot
#define SLIDE_GALLERY_HILL_H        1.5f
#define SLIDE_GALLERY_HILLS         6
#define SLIDE_GALLERY_LONG_DEG      25.0f       //the long run, and the drop off the floor's end
static const float SLIDE_GALLERY_DEG[SLIDE_GALLERY_HILLS] = { 8.0f, 14.0f, 18.0f, 25.0f, 35.0f, 50.0f };

/*
    ONE SURFACE UNDER HER FEET - whatever it is. The spring plants, the branches and the ramps are
    all a line she lands on from above, stays on while walking along it, and leaves; the landing
    test is written once, in Stage::CollideSurfaces, over one of these per candidate. What differs
    between the three is in the fields, not in the test: a plant's surface moves (`top_then`,
    `vel_y`), a branch has her balance on it, and a ramp holds her feet when she walks onto it from
    a block.
*/
enum SurfaceKind{
    SURFACE_NONE = -1,
    SURFACE_PLANT = 0,
    SURFACE_BRANCH,
    SURFACE_RAMP,
    SURFACE_BRIDGE,
    SURFACE_KINDS
};
struct StageSurface{
    int   kind = SURFACE_NONE;
    int   index = -1;               //into spring_plants, branches, ramps or bridges
    float top = 0.0f;               //under her now
    float top_then = 0.0f;          //under where she started the move, as it was then
    float vel_y = 0.0f;             //its own vertical speed under her
    float slope = 0.0f;             //dy/dx now
    bool  f_ground = false;         //holds feet that walk onto it from other ground (a ramp)
};
/*
    --- BALANCE ----------------------------------------------------------------------------------
    On a branch she has a LEAN, sideways - toward the camera or away from it, the axis a beam is
    really fallen off along. An inverted pendulum: the lean grows on its own (BALANCE_TOPPLE, pulled
    further the further over she is), a deterministic drift keeps pushing it about, and walking
    makes the drift worse. The aim axis pushes back: Up leans her AWAY from the camera, Down toward
    it. Push too long and she goes over the other way - there is no damping to hide an overcorrection
    in, only a little.

    DETERMINISTIC, like the aim sway: a sum of incommensurate sines, started at a different point
    each time she steps onto a branch (balance_entries), so no two crossings drift alike and a
    replay drifts exactly as the original did.

    Units: the lean in radians (+ away from the camera), the accelerations in radians/s^2.
*/
#define BRANCH_WALK_SPEED           1.8f    //her top speed along a branch, twice Balance_Walking's pace
#define BALANCE_FALL_DEG            35.0f   //leaning this far, she is off
#define BALANCE_TOPPLE              5.0f    //how hard the lean pulls itself over, per radian of it
#define BALANCE_DRIFT               1.2f    //the drift's strength standing still...
#define BALANCE_WALK_DRIFT          1.6f    //...and how much a full-speed walk adds to it
/*
    What a full push of the aim axis can do about it. Enough to beat the worst drift (standing
    still plus a full-speed walk) and the lean's own pull together, right up to BALANCE_FALL_DEG:
    a lean is always recoverable by a press in time, so what loses her is reacting late or pushing
    too long, never the numbers. At 4.0 a walk past 14 degrees was already lost whatever you did.
*/
#define BALANCE_CORRECT             6.0f
#define BALANCE_DAMPING             0.6f    //per second: a little, so an overcorrection still swings
//A landing on a branch knocks her sideways by this much lean rate per unit of landing speed.
#define BALANCE_LAND_WOBBLE         0.05f
/*
    THE CATCH: going over is not a fall. Past BALANCE_FALL_DEG she grabs the branch and hangs from
    it - MODE_HANG, the ledge's hang, with the branch remembered instead of a block - and from there
    Jump climbs back up onto it (the ledge's climb, along the branch) and Down lets go. A branch is
    caught in the air too, the way a ledge is: falling past it with her hands at it, Down not held.

    She hangs as far below the line as she hangs below a lip, and is held that far inside either
    end so both hands are on it.
*/
#define BRANCH_HANG_DROP            LEDGE_HANG_DROP
#define BRANCH_HANG_INSET           0.40f

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
    /*
        Through the slab: the centre, and the half-depth (0 is STAGE_BLOCK_HALF_DEPTH). LOOKS ONLY
        on a locked plane - the rules sweep the archer against x and y alone, and a locked arrow is
        at z 0, which every block covers - but it is the level's shape, so it lives with the rest
        of it: a stone set back, a thin slab, a deep one. Off the plane (the character scene) an
        arrow is swept through it too. See STAGE_BLOCK_MIN_COVER for how far either may go. Last,
        so every brace-initialised block keeps its meaning.
    */
    float z = 0.0f;
    float depth = 0.0f;
    //The tree this block is an ARM of (an index into Stage::trees), or -1 for a block the level
    //declared. The rules treat an arm as the one-way platform it is; the app and the tests ask.
    int   tree = -1;
    //BLOCK_CRUMBLE: -1 whole, else the ticks it has been shaking since she stood on it; at
    //CRUMBLE_SHAKE_TICKS it goes (f_alive clears). A restart rebuilds the block, whole.
    int   crumble_ticks = -1;
    //BLOCK_CRUMBLE: the StageCrumbleGroup that starts it (an index into Stage::crumble_groups), or
    //-1 for a stone that starts under her feet. A group's blocks ignore her standing on them.
    int   crumble_group = -1;

    float Left()   const { return x - hw; };
    float Right()  const { return x + hw; };
    float Bottom() const { return y - hh; };
    float Top()    const { return y + hh; };
    float HalfDepth() const { return (depth > 0.0f) ? depth : STAGE_BLOCK_HALF_DEPTH; };
    float Front()  const { return z + HalfDepth(); };
    float Back()   const { return z - HalfDepth(); };
};

/*
    THE FLOORS' EDGES - docs/vine_plan.md section 15. What the fear of heights, a teeter and a catch at
    a lip, a creeper turning over one and a tuft on top of a platform all ask about: where a floor
    ends, and what is past the end.

    DERIVED FROM THE BLOCKS, never authored and never hashed - the blocks are, and these follow
    from them. Rebuilt by Reset, by the tick after play changes a block (a wall kicked in, a stone
    crumbled), and by KeepBlockLayout after the editor moves one; RefreshEdges is there for any
    other code that edits blocks by hand. See StageEdges.cpp for how.

      SPAN     a floor: the top of one or more blocks at one height, less whatever stands on it,
               with neighbours at the same height joined into one. A one-way platform is a floor.
      EDGE     an end of a span where the floor drops away.
      CORNER   an end of a span where a wall rises instead - the foot of a face. A platform is
               never a wall: she walks through one from below.
*/
#define STAGE_EDGE_JOIN             0.02f   //gaps narrower than this, and heights closer, are one floor

struct StageSpan{
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y = 0.0f;
    int   block_left = -1;          //whose top each end is
    int   block_right = -1;
};

struct StageEdge{
    float x = 0.0f;                 //the collider's corner, where she can go over
    float y = 0.0f;
    int   side = 1;                 //+1: the floor to the left and the drop to the right; -1 the mirror
    int   block = -1;
    int   span = -1;
    float drop = 0.0f;              //down to the next floor just past it; VITALS_NO_FLOOR for none
    float wall = 0.0f;              //the bare face under the lip, down to where it meets anything
    float z_front = 0.0f;           //the block's depth, for anything placed along the lip
    float z_back = 0.0f;
    bool  f_grabbable = false;      //a BLOCK_LEDGE's corner: the level lets her hang here
};

struct StageCorner{
    float x = 0.0f;
    float y = 0.0f;
    int   side = 1;                 //+1: the wall rises to the right of the floor; -1 to the left
    int   block = -1;               //the block that rises
    int   span = -1;
    float rise = 0.0f;              //how far the face goes up before it is open again
};

/*
    A ZONE: a named stretch of the level that knows when she is in it - docs/cue_plan.md section 8, and
    docs/bridge_crumble_plan.md section 1, which is where it was first built.

    THE CUE PLAN'S SHAPE, deliberately, so there is one zone type rather than two: a rectangle like
    a StageBlock (centre and half extents), a name and an id, reporting `entered` and `left` off her
    body box. What that section adds later - `stayed`, how-often, flags and conditions, the narrator
    - grows on this rather than beside it. Collides with nothing; the rules only test overlap.

    `arrive` is where a teleport to the zone puts her FEET: a spot in it that is standable, which
    the rules test proves by dropping her there. It is what the panel's zone buttons and archer_zone
    use, so every mechanism area is one click away.

    Declared by the level, in BuildMainLevel and friends, beside what it covers.

    TWO USES of the one type. An AREA names a stretch of the level: the HUD shows it, the panel
    and archer_zone go to it, and a level's areas sit side by side. A TRIGGER (f_area false) is a
    small box placed where something should happen - the start of the chase - and exists for its
    EFFECTS: no label, no button, no arrival spot. Either can carry effects; see StageZoneEffect.
*/
enum StageZoneEffectKind{
    ZONE_START_CRUMBLE_GROUP = 0,   //target: an index into Stage::crumble_groups
};
/*
    What entering a zone DOES - docs/bridge_crumble_plan.md section 1: a Stage effect, `delay` ticks
    after the tick she entered. Once per run: the zone's effects fire on its first entry and never
    again until a restart rebuilds it. A short list on the zone, not a scripting language.
*/
struct StageZoneEffect{
    int   kind = ZONE_START_CRUMBLE_GROUP;
    int   target = -1;
    int   delay = 0;
};
struct StageZone{
    float x = 0.0f;
    float y = 0.0f;
    float hw = 1.0f;
    float hh = 1.0f;
    std::string name;
    int   id = -1;              //its index in Stage::zones - stable for the level's lifetime
    v2    arrive;               //feet, for a teleport; see above. An area's only
    bool  f_area = true;        //an area, or a trigger; see above
    std::vector<StageZoneEffect> effects;
    bool  f_fired = false;      //its effects are on their way; a restart rebuilds it clear

    float Left()   const { return x - hw; };
    float Right()  const { return x + hw; };
    float Bottom() const { return y - hh; };
    float Top()    const { return y + hh; };
    bool  Overlaps(float l, float r, float b, float t) const {
        return (r > Left()) && (l < Right()) && (t > Bottom()) && (b < Top());
    }
};

/*
    A CRUMBLE GROUP: crumble blocks that go ONE AFTER ANOTHER once something starts them, rather
    than each under her feet - the chase of docs/bridge_crumble_plan.md section 2, a floor that falls
    away behind her. Started by a zone's effect; from then block k begins its CRUMBLE_SHAKE_TICKS
    shake `starts[k]` ticks in. The starts go by DISTANCE along the floor, not by count, so the
    front runs at one speed across a gap left in it.

    `blocks` are in the order they go, and each names its group back (StageBlock::crumble_group)
    so standing on it starts nothing. Once started a group runs to its end; a restart rebuilds it.
*/
struct StageCrumbleGroup{
    std::string name;
    std::vector<int> blocks;
    std::vector<int> starts;    //per block, ticks after the group's start
    int   ticks = -1;           //-1 not started, else ticks since it was
    bool  f_done = false;       //every block gone
};
/*
    THE CHASE's pace, in ticks per unit of floor. 8 is a front moving at 7.5 units a second
    against her 9: she gains 1.5 a second on a clean run, and gives it back in a stumble.
    stage_test measures the margin - how long she can stand at the start and still make it.
*/
#define CHASE_TICKS_PER_UNIT        8.0f

/*
    The terrain test bay - a stretch of level LEFT of the start that exists only to compare
    marching-cubes terrain settings against each other and against the plain blockout. See
    apps/archer/docs/terrain_plan.md; this switch is how the whole thing comes back out in one edit.

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
    THE CAVE (apps/archer/docs/cave_plan.md), left of the bay through a mouth where the bay's left-hand
    wall used to be: a floor of its own, a roof, a far wall, and the bank behind closing its back
    (Backdrop.h - a block reaching back into the bank raises it to over its top). Enclosed on every
    side but the camera's, for lighting without the sun.

    The terrain bays' regions start at ARCHER_CAVE_X_MIN rather than ARCHER_TEST_BAY_X_MIN, so the
    cave's blocks melt with the bay's: its floor and far wall with the ground, its roof and the
    mouth's lip with the island above the split. Everything else that means "the bay" still reads
    ARCHER_TEST_BAY_X_MIN.
*/
#define ARCHER_CAVE_X_MIN           (-66.0f)    //the cave floor's left end; the far wall stands on it
#define ARCHER_CAVE_ROOF_Y          9.0f        //the roof's underside
/*
    THE SLOPES (docs/terrain_plan.md section 12), past the cave's far wall: the test bed for ramps
    melted into the terrain, entered off the cave roof's left end. The terrain bays' regions reach
    left to here, so its blocks and ramps melt with the rest.
*/
#define ARCHER_SLOPES_X_MIN         (-107.0f)   //the left wall's outer face - the level's end now
#define SLOPES_PITCH_DEG            40.0f       //off the roof onto the shelf: past the hip-slide line
#define SLOPES_RUN_DEG              22.0f       //the shelf to the floor: a surf, long enough for speed
#define SLOPES_SHELF_Y              8.2f        //2.8 under the roof - a running jump back up over the pitch

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
    PROP_ROPE_ANCHOR,       //the fixed top of a rope; the chain hangs from here
    /*
        A kicking dummy on a spring - apps/archer/docs/strawman_plan.md. She walks THROUGH it and the
        boot still finds it, so the app offers it as a non-blocking obstacle (StageObstacle::
        f_blocks). Not a TargetVariant: a target that tips past TARGET_KNOCKED_DEG is taken out of
        play, and this one tips that far on every good kick and comes back. y is its centre, as for
        every prop, and it is built standing on y - h / 2.
    */
    PROP_STRAWMAN
};

/*
    The levels a Stage can build.

    MAIN is the traversal level the whole prototype grew up in. RANGE is the test range from
    docs/bow_plan.md section 7: one flat floor between two walls and a few targets either side of the
    start, and nothing else - no ledges, no rope, no gaps. It exists so the bow can be worked on
    with a still camera and nothing to fall off, and so a screenshot of it means the same thing
    from one run to the next.

    ROPE is the same idea for the rope: a floor, two walls and ONE long rope in the middle, and
    nothing else - the place to work on catching, swinging and climbing it.

    CHARACTER is not a level to play at all: one round terrain tile with her standing on it, a
    fixed camera and a turntable - the place to look at her animations, skinning and textures up
    close. The app locks her feet there (GatherInput), so the only thing the rules do is hold her
    on the tile and run whatever she does on the spot: a draw, a kick, a kneel, a jump.

    WEB is the spider web's blockout (docs/web_plan.md section 5): a floor, two walls, the web in its
    frame and somewhere to shoot it from - an area of its own, as asked, to judge how it looks and
    breaks before one goes anywhere in the level. Not in the rope level, whose whole floor is a
    run-up its pit tests sprint along.
*/
enum StageLevel{
    STAGE_LEVEL_MAIN = 0,
    STAGE_LEVEL_RANGE,
    STAGE_LEVEL_ROPE,
    STAGE_LEVEL_CHARACTER,
    STAGE_LEVEL_WEB,
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
    A waterfall, and where its water goes - apps/archer/docs/water_plan.md. LOOKS ONLY: nothing here
    collides, and the rules never read it. It lives in the Stage for the reason the props do, so
    the whole layout of a level is in one file.

    It pours from a notch in the back wall of a terrain bay (Backdrop.h cuts it) into a pool on a
    rock shelf, spills over the shelf's front into the gap between the ground and the bank, and
    runs along that gap to stream_x_end. Water.h turns this into the shelf's rocks, the surfaces
    and the foam; every z comes from the ground block and BackdropParams, so none is given here.
*/
struct StageWater{
    float x = 0.0f;             //the fall's centre line
    float half_width = 0.9f;    //the fall's half width
    float lip_y = 8.5f;         //where it pours off the wall
    float basin_y = 2.6f;       //the pool's surface
    float basin_hw = 1.8f;      //the shelf holding the pool, either side of x
    float spill_dx = -0.6f;     //where the pool spills over the shelf's front, from x
    float stream_y = -0.12f;    //the stream's surface, just under the ground's top
    //Where the stream runs to - toward the cave, so left of x. It may run on past its own ground
    //onto the next, but not past the last, or its front edge is out in the open (water_test).
    float stream_x_end = -39.8f;
};

/*
    A BIOME: a box of the level with its own rules for how it is dressed and how the air moves in
    it - the cave's (docs/cave_plan.md) are the first. Declared the way zones are, but LOOKS ONLY: the
    rules never read one, which is why this is not a field on StageZone - a zone is the rules'
    business (the HUD's name for where she is, what entering it starts), and a biome is what the
    plants, the rocks and the wind make of a place.

    Everywhere outside every box is BIOME_JUNGLE, the level as it always was. Where boxes overlap,
    the first in the list wins. What each kind means is up to its readers - FoliageBiomeFor,
    BoulderBiomeFor, WindBlocks - so a new kind is a row in each of those, not a flag here.
*/
enum StageBiomeKind{
    BIOME_JUNGLE = 0,
    BIOME_CAVE,
    BIOME_COUNT
};
struct StageBiome{
    int   kind = BIOME_JUNGLE;
    std::string name;
    float x = 0.0f, y = 0.0f;
    float hw = 1.0f, hh = 1.0f;
    //STILL AIR: the wind treats the box as solid, so it flows round the place rather than
    //through it, and nothing it carries - leaves, streaks, fireflies - comes in (Wind.h).
    bool  f_still_air = false;
    /*
        How far inside its left and right edges it blends into what is outside it, for its
        dressing - the cave's grass thins over the first few units in from the mouth rather than
        stopping at a line. The air does not blend: still is still.
    */
    float fade_left = 0.0f;
    float fade_right = 0.0f;

    float Left()   const { return x - hw; };
    float Right()  const { return x + hw; };
    float Bottom() const { return y - hh; };
    float Top()    const { return y + hh; };
    bool  Contains(float px, float py) const {
        return (px >= Left()) && (px < Right()) && (py >= Bottom()) && (py < Top());
    }
};
//The biome at a point: the first box holding it, or BIOME_JUNGLE. `biomes` may be NULL. With
//`weight`, how much of that biome it is, 0..1 - under 1 only in a box's fade - and 1 for the jungle.
int BiomeAt(const std::vector<StageBiome>* biomes, float x, float y, float* weight = NULL);

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
    //False for something she walks through that the boot still finds - the straw man. Last, so
    //every existing brace-initialised obstacle keeps blocking.
    bool  f_blocks = true;

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
    --- THE THREE KICKS ---------------------------------------------------------------------------
    K alone is Kick_Front, the kick everything above was measured on. Down+K and Up+K are the other
    two front kicks in the export, and each is its own move: its own length, its own active window
    and box, and what it IMPARTS - because a kick that lands low and heavy and one that snaps out
    high are not the same kick with a different picture on it.

    THE DIRECTION IS READ OFF THE AIM AXIS, not off the arrow keys by name. Up and Down already
    are the aim, on the keys and on the right stick alike, so reading the axis gives the pad the
    same three kicks (B with the stick tilted) and a scripted hold of "up" or "aim_down" asks for
    them too, with nothing added to the input map. Decided ONCE, on the press: a kick that has
    started is that kick to the end, whatever the stick does meanwhile.

    EVERY ROW IS SET BY ITS CLIP, the KICK_TICKS arrangement: the length is the clip's, the window
    covers the strike ApplicationArcher::MeasureKickClip finds, and the app warns with the numbers
    to type when a re-export moves either. The box and the impact are fitted to where and how fast
    that boot lands - the measurements are beside the table in Stage.cpp. KICK_FRONT's row IS the
    defines above, so the rules test and the retiming history in docs/animation_plan.md keep meaning
    what they say.
*/
enum KickKind{
    KICK_FRONT = 0,         //K        Kick_Front
    KICK_FRONT_2,           //Down+K   Kick_Front_2
    KICK_FRONT_3,           //Up+K     Kick_Front_3
    KICK_KIND_COUNT
};
struct KickSpec{
    const char* name;       //for the log and the panel
    int   ticks;            //the whole move, KICK_TICKS for this kick
    int   active_from;      //the boot is live from this tick...
    int   active_to;        //...through this one
    float reach;            //past the body's leading edge, KICK_REACH
    float half_height;      //KICK_HALF_HEIGHT
    float y_offset;         //the box's centre from pos.y, KICK_Y_OFFSET
    float speed;            //what it imparts along the facing, KICK_SPEED
    float lift;             //and upward, KICK_LIFT
};
extern const KickSpec KICK_SPECS[KICK_KIND_COUNT];
//How far the aim axis has to be pushed for Down+K or Up+K. Half, so a key (which is all of it)
//always counts and a stick resting a little off-centre never does.
#define KICK_SELECT_AIM             0.5f

/*
    --- KNEELING ---------------------------------------------------------------------------------
    C kneels, C again stands - a stance she gets into and out of, not a hold (docs/animation_plan.md,
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
    //Edges: this kind of arrow (an ArrowKind; -1 none, and out of range is ignored), or a step
    //round the kinds, +1 or -1, for a pad. The pick wins if both come in one tick.
    int   arrow_select = -1;
    int   arrow_step = 0;
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
    v3    point;
    int   block = -1;
};

/*
    An arrow in flight, or stuck in something. 3D, with z exactly 0 on a locked plane - see the
    play plane note at the top.

    prev_pos is kept because the app needs THIS TICK'S SEGMENT to ask rp3d whether the arrow
    passed through a crate or a target on its way - see the handshake note on Arrows() below. It
    is not used by the rules themselves.

    ITS ATTITUDE IS TWO ANGLES, for the app to orient the mesh by (it runs along +X): `yaw` about
    +Y, then `angle` about the turned Z - so the mesh's rotation is yaw(Y) * angle(Z). `yaw` is
    within a quarter turn either way, which leaves `angle` carrying which way along X it points;
    in the plane `yaw` is 0 and `angle` is atan2(vy, vx) exactly as it always was, so an arrow
    stuck in a prop, which turns by `angle` alone, needs no idea of 3D.
*/
struct Arrow{
    v3    pos;
    v3    prev_pos;
    v3    vel;
    float angle = 0.0f;         //radians, the direction of travel in its own vertical plane
    float yaw = 0.0f;           //radians about +Y, out of the play plane; 0 on a locked one
    int   age_ticks = 0;
    bool  f_live = false;
    bool  f_stuck = false;
    int   kind = ARROW_NORMAL;      //an ArrowKind, the selection's when it was loosed
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
    float shot_side_deg = 0.0f;     //the sideways sway it left with; flattened away on a locked plane
    int   shot_kind = ARROW_NORMAL; //and which kind it was
    bool  f_arrow_kind_changed = false; //the selection moved this tick, to Stage::arrow_kind
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
    //Pumping a spring plant: how much of a stomp the landing carried (0 none .. 1 full), and the
    //speed a swing added.
    float stomp = 0.0f;
    bool  f_swung = false;
    float swing_speed = 0.0f;
    //Off a branch because the lean went past BALANCE_FALL_DEG, and to which side (+1 away from the
    //camera, -1 toward it).
    bool  f_lost_balance = false;
    float fall_side = 0.0f;
    bool  f_caught_branch = false;  //grabbed one to hang from - going over, or in the air

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
        int   kind = KICK_FRONT;    //which kick, so the app imparts that kick's speed and lift
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
        v3    point;
        v3    normal;               //the face it went in through; z only off a locked plane
        float speed = 0.0f;
        int   block = -1;           //index into blocks; BREAKABLE is the interesting case
        int   kind = ARROW_NORMAL;  //the arrow's ArrowKind - what, if anything, grows from here
    };
    std::vector<ArrowHit> arrow_hits;

    //Zones her body box began or stopped overlapping this tick, by id - see StageZone. A restart
    //clears what she was in, so the first tick after one enters the zone she stands in.
    std::vector<int> zones_entered;
    std::vector<int> zones_left;

    //BLOCK_CRUMBLE blocks that began to shake this tick (she stood on them), and that went. A gone
    //one has f_alive clear already, as a kicked wall does; the app takes its collider away and
    //drops its rubble, as for broken_blocks.
    std::vector<int> crumbles_started;
    std::vector<int> crumbled_blocks;
    //Crumble groups that started this tick, and whose last block went - the chase's rumble is held
    //between the two. Indices into Stage::crumble_groups.
    std::vector<int> crumble_groups_started;
    std::vector<int> crumble_groups_done;

    //A landing on a bridge - any bridge - and how hard, against its own speed under her, stomp
    //included: the plank knock and the heavier creak. `strain` is the worst plank's after it.
    struct BridgeLanding{
        int   bridge = -1;
        int   plank = -1;
        float speed = 0.0f;
        float strain = 0.0f;
        float x = 0.0f;
    };
    std::vector<BridgeLanding> bridge_landings;
    //A breakable bridge passing a warning level - BRIDGE_LEVEL_STRAINED, _CRACKING, _SNAPPED -
    //once each per run, with the plank that took it there and where that plank is.
    struct BridgeWarning{
        int   bridge = -1;
        int   plank = -1;
        int   level = BRIDGE_SOUND;
        v2    at;
    };
    std::vector<BridgeWarning> bridge_warnings;

    //--- Webs (StageWeb) ------------------------------------------------------------------------
    //Each thread that snapped this tick - WebThreadCut in docs/web_plan.md: torn out by an arrow
    //(`arrow` its index), burst by her (`f_burst`), or stretched past WEB_BREAK_STRAIN (neither).
    //`at` is where, in the world: the arrow's crossing, or the thread's middle.
    struct WebThreadCut{
        int   web = -1;
        int   thread = -1;
        int   arrow = -1;
        bool  f_burst = false;
        v3    at;
    };
    std::vector<WebThreadCut> web_cuts;
    //Each arrow that went through a web this tick: how many threads it tore out, and whether it stayed.
    struct WebHit{
        int   web = -1;
        int   arrow = -1;
        int   threads = 0;
        bool  f_caught = false;
        v3    at;                   //where it met the web, on the crossing line
        float speed = 0.0f;         //the arrow's, as it met the web - before the threads slowed it
    };
    std::vector<WebHit> web_hits;
    //Her passing through a web's crossing line - the first time is WebBreached.
    struct WebBreach{
        int   web = -1;
        bool  f_first = false;
    };
    std::vector<WebBreach> web_breaches;
    //A web she has just started pushing against, and one she has just burst (a few threads at once).
    std::vector<int> web_pressed;
    std::vector<int> web_burst;

    //--- Apples (StageApple) --------------------------------------------------------------------
    //An apple freed this tick: `arrow` the one that did it, `at` where it met the stem or the
    //apple, and `vel` what the apple leaves with. A cut arrow flies on; a hit one is stuck at `at`.
    struct AppleShot{
        int   apple = -1;
        int   arrow = -1;
        v3    at;
        v3    vel;
    };
    std::vector<AppleShot> apples_cut;
    std::vector<AppleShot> apples_hit;
    int   pick_started = -1;        //the apple a pick has just reached for, or -1
    std::vector<int> apples_picked; //and taken, at the hand's close
};

/*
    --- VITALS -----------------------------------------------------------------------------------
    Her body as two slowly moving LEVELS - apps/archer/docs/vitals_plan.md. EXERTION, 0 rested .. 1
    spent, from what she is doing; FEAR, 0 calm .. 1 terrified, from how far she could fall; and
    the HEART RATE, which trails both.

    Rules state although today only the cues read it (her breathing and her heartbeat, which the
    app clocks off these): both will change how she behaves, and anything that does is Stage's.
    Levels, not resources - nothing spends them. Health and power, when they come, are the other
    kind and live beside these.

    Each eases toward a TARGET the tick works out, rising at one rate and falling at a slower one;
    a TAU is the seconds to get about two thirds of the way. The feel is almost all in the taus:
    she should still be breathing hard well after a climb, and her heart pounding for a second or
    two after a near miss, when the danger is already over.
*/
#define VITALS_EXERTION_RISE_TAU    3.0f
#define VITALS_EXERTION_FALL_TAU    8.0f    //standing still; see VITALS_WALK_RECOVERY
//Walking neither winds her nor lets her get her breath back properly: it recovers at this share
//of the standing rate, scaled down from there by how fast she walks.
#define VITALS_WALK_RECOVERY        0.35f
//On the ground, her speed as a share of RunSpeed() -> the target: nothing up to a walk, then
//rising to VITALS_RUN_TARGET at full speed.
#define VITALS_RUN_FROM             0.40f
#define VITALS_RUN_TARGET           0.85f
#define VITALS_HANG_TARGET          0.55f   //holding on is work...
#define VITALS_HANG_RISE_TAU        6.0f    //...that tells slowly
#define VITALS_CLIMB_TARGET         0.90f   //a mantle, or climbing the rope
#define VITALS_ROPE_TARGET          0.45f   //holding on to it without climbing
//Bursts on top of the target, per effort: three quick jumps wind her where one does not.
#define VITALS_JUMP_EFFORT          0.05f
#define VITALS_KICK_EFFORT          0.07f

#define VITALS_FEAR_RISE_TAU        0.35f
#define VITALS_FEAR_FALL_TAU        5.0f
/*
    How far down she could fall -> fear: nothing up to the first, all of it at the second. In
    units below her feet. A jump rises 3.2, so a drop she could jump back up is nothing to her.
*/
#define VITALS_DROP_FROM            3.5f
#define VITALS_DROP_TO              12.0f
//Standing within this of where the floor ends counts that drop, fading to nothing at the reach,
//and at most this share of the drop's fear right at the lip - she is on the floor, not over it.
#define VITALS_EDGE_REACH           0.9f
#define VITALS_EDGE_SHARE           0.6f
//In the air: the speed she will land at, from the drop below and how fast she is already
//falling (energy, so rising counts too) -> fear. From a little under the landing shake to well
//past the hard landing. A jump off flat ground lands at ARCHER_JUMP_SPEED, below the first.
#define VITALS_IMPACT_FROM          20.0f
#define VITALS_IMPACT_TO            34.0f
#define VITALS_BALANCE_SHARE        0.8f    //of BalanceDanger, on a branch
//Bursts: going over the side of a branch, and a landing hard enough to hurt.
#define VITALS_LOST_BALANCE_FEAR    0.35f
#define VITALS_HARD_LANDING         25.0f
#define VITALS_HARD_LANDING_FEAR    0.25f
//What a column with no floor under it at all reads as: past VITALS_DROP_TO, so it is all fear.
#define VITALS_NO_FLOOR             100.0f

#define VITALS_REST_BPM             65.0f
#define VITALS_EXERTION_BPM         60.0f   //added at exertion 1
#define VITALS_FEAR_BPM             60.0f   //added at fear 1: 125, near the heartbeat file's 130
#define VITALS_MAX_BPM              165.0f
#define VITALS_HEART_RISE_TAU       1.5f
#define VITALS_HEART_FALL_TAU       6.0f

struct StageVitals{
    float exertion = 0.0f;
    float fear = 0.0f;
    float heart_rate = VITALS_REST_BPM;
    //This tick's targets, what the two levels are easing toward - for the debug view and the tests.
    float exertion_target = 0.0f;
    float fear_target = 0.0f;
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
    //Each tree's arms as one-way platforms, appended to `blocks` after the level is built.
    void BuildTrees();
    void BuildSnakePaths();
    void TickSnakes();
    //How far along its path a snake's patrol reaches: `trunk_reach` past its branch, on the path.
    float SnakePatrolEnd(const StageSnake& sn) const;

    //Where the archer starts and where a fall off the world puts her back. Per level.
    v2   StartPosition() const;
    //Her top speed on this level: ARCHER_RUN_SPEED, or ARCHER_RANGE_RUN_SPEED on the range.
    float RunSpeed() const;

    //One tick. The whole simulation, and the only way state changes.
    void Tick(const ArcherInput& in, StageEvents& events);

    //--- The world ------------------------------------------------------------------------------
    std::vector<StageBlock> blocks;

    //The floors, their edges and the feet of their walls, from the blocks - see StageEdge.
    std::vector<StageSpan>   spans;
    std::vector<StageEdge>   edges;
    std::vector<StageCorner> corners;
    int   edges_generation = 0;     //+1 on every rebuild, so a copy knows when it is stale
    //Rebuilds them from the blocks as they are now. Reset and KeepBlockLayout (the editor's
    //moves) both do.
    void  RebuildEdges();
    /*
        Rebuilds them if anything about the blocks has changed since the last build - every box,
        kind and f_alive, fingerprinted - true if it did. For code that edits blocks by hand.

        Tick does something cheaper: it counts the live blocks and rebuilds if the count moved,
        which is exactly what play can do to them (a wall kicked in, a stone crumbled - f_alive
        only ever clears until a restart). The full fingerprint over every block, on every tick of
        every landing forecast (which ticks a copy of the Stage up to 30 times), would cost more
        than the forecast itself.
    */
    bool  RefreshEdges();
    //The nearest edge of the floor at floor_y (within STAGE_EDGE_JOIN of it) within max_d of x
    //across, of `side` (0 either), or -1.
    int   NearestEdge(float x, float floor_y, float max_d, int side = 0) const;
    //The span under x at height y (within `tol`), or -1 - "is this top open here".
    int   SpanAt(float x, float y, float tol = 0.05f) const;

    std::vector<StageProp>  props;
    std::vector<StageSign>  signs;
    std::vector<StageScenery> scenery;
    std::vector<StageWater> waters;
    std::vector<StageBiome> biomes;
    std::vector<StageTree>  trees;
    std::vector<StageSnakeBranch> snake_branches;
    std::vector<StageSnakePath> snake_paths;    //built from snake_branches, one each - BuildSnakePaths
    //Declared by the level and stepped every tick, like the spring plants: a Reset rebuilds them.
    std::vector<StageSnake> snakes;
    //Declared by the level and stepped every tick: their state is theirs, so a Reset rebuilds them.
    std::vector<StageSpringPlant> spring_plants;
    std::vector<StageBranch> branches;
    std::vector<StageRamp>  ramps;
    std::vector<StageBridge> bridges;
    std::vector<StageWeb> webs;
    std::vector<StageApple> apples;
    std::vector<StageAppleTree> apple_trees;
    std::vector<StageZone>  zones;
    std::vector<StageCrumbleGroup> crumble_groups;

    //--- Zones ------------------------------------------------------------------------------------
    //Whether she is in zone i as of the last tick. Sized to `zones` by Reset, and all clear then.
    std::vector<uint8_t> zone_inside;
    /*
        The AREA she is in, for the HUD and archer_state: the SMALLEST of those her body overlaps,
        so a narrow area inside a wide one names the narrow one. -1 when she is in none. Triggers
        are never it.
    */
    int  CurrentZone() const;
    //By name, or -1.
    int  FindZone(const char* name) const;

    /*
        Makes the blocks' CURRENT geometry the level's, so that Reset puts it back instead of
        BuildLevel's. The app calls this when an editor has moved boxes and the terrain has been
        regenerated to match - without it, the first restart would snap every box back while the
        terrain stayed where they had been moved to.

        Geometry only: x, y, hw, hh, z and depth. Kind and f_alive still come from BuildLevel, so a restart
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
    void AddObstacle(float x, float y, float hw, float hh, int id, bool f_pushable, bool f_blocks = true);

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
    //The spring plant she is standing on, or -1 - kept in step with f_on_ground by the collision.
    int   spring_on = -1;
    //What a spring plant threw her up with: the part of her rise the jump cut may not take away,
    //so letting go of jump shortens the jump and never the throw.
    float launch_lift = 0.0f;
    /*
        Her sideways pull down whatever she stands on, when it is too steep to stand on - a leaf past
        SPRING_LEAF_SLIP_DEG, a ramp past its slip_deg - units/s^2, or 0. A pad is level and a
        branch has its balance instead.
    */
    float SlideAccel() const;
    int   ramp_on = -1;             //the ramp she is standing on, or -1 - kept with f_on_ground
    bool  f_skidding = false;       //riding a slide out on the flat - see SKID_DECEL
    int   bridge_on = -1;           //the bridge she is standing on, or -1 - kept with f_on_ground
    //The slope under her feet in degrees, + rising to the right; 0 on flat ground or in the air.
    float SlopeUnderFeetDeg() const;
    //--- Pumping - see SPRING_STOMP_TICKS ---
    int   stomp_ticks = 0;          //aim held down while falling, ticks running, capped
    int   spring_left = -1;         //the spring plant that last let go of her, while spring_air_ticks runs
    int   spring_air_ticks = -1;    //ticks since it did; -1 once she is down again
    bool  f_swung = false;          //this flight's one swing has been spent
    float prev_aim_axis = 0.0f;     //last tick's, for the Up press
    /*
        THE TIMING CUE. What a jump pressed now would add from a spring plant - her rise while she
        rides one, or in the coyote ticks after it throws her - and whether that is on offer at
        all. PredictSpringBoostPeak is the most it will add this bounce, past and future (a copy
        ticked forward, the PredictLanding way), so now / peak is how well timed a press would be.
    */
    bool  SpringBoostActive() const;
    float SpringBoostNow() const;
    float PredictSpringBoostPeak(const ArcherInput& in, int horizon = 40) const;
    float spring_boost_seen = 0.0f; //the most SpringBoostNow has been this bounce

    //--- Balance - see BALANCE_TOPPLE ---
    int   branch_on = -1;           //the branch she is standing on, or -1 - kept with f_on_ground
    float lean = 0.0f;              //radians, + away from the camera
    float lean_rate = 0.0f;
    int   balance_ticks = 0;        //on this branch, since she stepped onto it
    int   balance_entries = 0;      //every time she has; picks where the drift starts
    //0 upright .. 1 about to go over.
    float BalanceDanger() const;

    //--- Vitals - see VITALS ---
    StageVitals vitals;
    //How far below `y` the highest floor under x is - any live block's top, one-way ones included -
    //or VITALS_NO_FLOOR when there is none. The surfaces (pads, branches, ramps) are not floors here.
    float DropBelow(float x, float y) const;
    //The fear a drop of `drop` units is worth on its own, 0..1 - see VITALS_DROP_FROM.
    static float DropFear(float drop);

    //--- Hanging and climbing -------------------------------------------------------------------
    int   hang_block = -1;          //index into blocks, while MODE_HANG or MODE_CLIMB
    //Or into branches, when it is a branch she hangs from or climbs onto - hang_block is then -1.
    int   hang_branch = -1;
    //Which SIDE of that block the archer is on: -1 hanging off its left corner, +1 its right.
    //Not derived from `facing`, because the climb needs it after facing may have changed.
    float hang_side = -1.0f;
    int   climb_ticks = 0;          //counts down through MODE_CLIMB
    //The kick, counted UP from 1 so that 0 means "not kicking" - see KICK_ACTIVE_FROM/TO.
    int   kick_ticks = 0;
    int   kick_cooldown = 0;
    int   kick_kind = KICK_FRONT;   //KickKind of the kick running, or of the last one
    const KickSpec& Kick() const { return KICK_SPECS[kick_kind]; }

    //--- Picking an apple - see APPLES ----------------------------------------------------------
    //Counted up from 1 like the kick, 0 not picking. The window is PickTicks(), the close PickClose().
    int   pick_ticks = 0;
    int   pick_apple = -1;          //the apple reached for, while picking
    bool  f_pick_low = false;       //a low pick - at her feet - rather than a reach up
    int   apples_picked = 0;        //this run's; a restart empties her hands
    int   PickTicks() const { return f_pick_low ? APPLE_PICK_TICKS_LOW : APPLE_PICK_TICKS_HIGH; }
    int   PickClose() const { return f_pick_low ? APPLE_PICK_CLOSE_LOW : APPLE_PICK_CLOSE_HIGH; }
    /*
        The apple she would reach for if action were pressed now - the nearest in reach, hanging or
        lying - or -1. Not whether she CAN start a pick (the rope comes first, and she has to be
        standing on the ground): CanStartPick says that. Const, for the app's highlight and prompt.
    */
    int   FindPickableApple(bool* out_low = NULL) const;
    bool  CanStartPick() const;
    //A loose apple's body is now at `at` - the app, before each tick, like the obstacles.
    void  SetLooseApple(int apple, const v3& at);

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
    float aim_deg = BOW_AIM_NEUTRAL_DEG;    //relative to facing; + is up. Survives a release, so
                                            //the next shot starts where the last one was aimed.
    //Ticks spent moving since the last draw, up to BOW_AIM_RETURN_TICKS, where the aim starts
    //back to neutral.
    int   aim_roam_ticks = 0;
    //0..1: BOW_MIN_POWER at the nock, rising over the pull to 1 at BOW_DRAW_TICKS. Before the nock
    //there is no shot to have a power; it reads BOW_MIN_POWER so the arc has something to draw.
    float DrawPower() const;
    //Is an arrow on the string - drawing, and at least BOW_NOCK_TICKS in? Only then can she loose.
    bool  IsNocked() const { return bow_mode == BOW_DRAWING && draw_ticks >= BOW_NOCK_TICKS; }
    int   draws_cancelled = 0;      //let go before the nock; see BOW_NOCK_TICKS
    int   sway_ticks = 0;           //ticks since the nock, uncapped - draw_ticks stops at full
    int   draws_started = 0;        //every draw begun; picks where its sway starts
    /*
        The ArrowKind the next arrow is loosed as - chosen with the number keys, and taken by Loose,
        so a change while nocked applies to the arrow already on the string: what the HUD shows is
        what flies. Reset LEAVES IT, because it is her choice rather than the level's, and a
        restart should not quietly swap her quiver; a recording carries it in its state line
        instead, so a replay starts with the kind the original did.
    */
    int   arrow_kind = ARROW_NORMAL;
    //The sway on top of aim_deg right now, degrees - 0 unless nocked. See AIM_SWAY_STAND_DEG.
    float AimSwayDeg() const;
    //What the arrow is actually aimed at: aim_deg plus the sway. AimDirection follows it.
    float ShotAimDeg() const { return aim_deg + AimSwayDeg(); }
    //The cone's other half: the sway SIDEWAYS off the aim, degrees, + to her left (anticlockwise
    //from above). 0 unless nocked. On a locked plane only the bow shows it; see AIM_SWAY_STAND_DEG.
    float AimSwaySideDeg() const;

    /*
        --- THE PLANE LOCK --------------------------------------------------------------------------
        Locked, "ahead" is along facing - +X or -X - and the aim is flattened into the plane. Every
        level but the character scene, whose turntable turns her to face anywhere; there "ahead" is
        heading_deg, which the APP writes before each tick from the angle she is drawn at (the
        turntable and a clip's own turn) - the same arrangement as SyncArcherFromRope, and for the
        same reason: it is the view's to know, and the rules only follow it. Degrees about +Y, 0 is
        +Z (toward the camera), + anticlockwise from above: the turntable's own convention.
        Ignored while locked. A LEVEL property rather than a flag, so nothing can unlock a level
        the props and the camera are planar in.
    */
    bool  IsPlaneLocked() const { return level != STAGE_LEVEL_CHARACTER; }
    float heading_deg = 0.0f;
    //Straight ahead, level, as a unit vector: (facing, 0, 0) locked, off heading_deg unlocked.
    v3    Forward() const;

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

    void  StickArrow(int index, const v3& point);   //stop it dead and leave it embedded
    void  KillArrow(int index);                     //remove it entirely

    /*
        Where an arrow loosed right now would go, as up to AIM_ARC_POINTS positions sampled every
        AIM_ARC_TICK_STRIDE ticks.

        THIS IS THE SAME INTEGRATOR THE ARROW RUNS, which is the whole point of it existing here
        rather than in the app: the dots on screen are not an approximation of the flight, they
        are the flight. Stops at the first block the arc enters, so the preview also shows what it
        will hit. Returns how many points were written.
    */
    int   PredictArc(v3* out_points, int max_points) const;

    /*
        The same, for an arrow already in flight: where it will strike a block, and when, within
        `horizon` ticks. It runs FlyArrow - the step TickArrows takes - on a copy of the arrow, so
        it is the flight and not a sketch of it; an arrow's flight takes no input, so nothing can
        make it wrong except the app's props, which the rules never see. `path`, if given, gets
        the positions the arrow sweeps through, one per tick after the first - the segments the
        app raycasts for props, as ResolveArrowsAgainstProps does the real ones.
    */
    StageArrowImpact PredictArrowImpact(int index, int horizon, std::vector<v3>* path = NULL) const;

    //The nocked arrow's ANCHOR - where the string holds it - for the current facing and aim. An
    //arrow's first sweep starts here; see ARROW_LENGTH. At her z (0), plus Forward's along it.
    v3    AnchorPosition() const;
    //Where the arrow's TIP is at the loose: the anchor plus ARROW_LENGTH along the aim. Loose()
    //spawns the flying point here and PredictArc draws from here.
    v3    MuzzlePosition() const;
    //The way the arrow leaves, a unit vector: the aim and both halves of the sway about Forward,
    //or on a locked plane the aim and the up-and-down half only, with z exactly 0.
    v3    AimDirection() const;

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

    /*
        Everything the rules carry from one tick to the next, into a replay's state hash - see
        core/StateHash.h and docs/replay_determinism_plan.md. Two parts: `her` (the character,
        her bow, her body) and `world` (arrows, blocks, plants, bridges, crumbles, pending zone
        effects). A NEW FIELD THAT OUTLIVES A TICK BELONGS HERE TOO, or a replay can part over it
        without the trace noticing. The layout (what BuildLevel lays down and never changes) is
        left out; what the play changes of it - a block broken, a plant bent - is in.
    */
    void HashState(class StateHash& hash) const;

private:
    int  level = STAGE_LEVEL_MAIN;
    //What KeepBlockLayout recorded, laid over BuildLevel's blocks by Reset. Empty until then.
    std::vector<StageBlock> kept_layout;
    void BuildLevel();
    void BuildMainLevel();
    void BuildRangeLevel();
    void BuildRopeLevel();
    void BuildCharacterLevel();
    void BuildWebLevel();
    //Left of the rope level's shallow pit: ramps at fixed angles - see SLIDE_GALLERY_DEG.
    void BuildSlideGallery();
    //The main level's apple trees, in the terrain bay - docs/apple_plan.md, "As built".
    void BuildOrchard();
    //Adds `s` to `scenery`, and its invisible collider to `blocks` if it has one. See StageScenery.
    void AddScenery(const StageScenery& s);
    //Declares a zone by its edges rather than its centre, which is how a level is read, and
    //with the spot a teleport to it lands her feet on. See StageZone.
    void AddZone(const char* name, float left, float right, float bottom, float top, v2 arrive);
    //A bridge from anchor `a` to anchor `b`, hung and settled at rest so the level starts with
    //it still. See StageBridge.
    void AddBridge(v2 a, v2 b, int planks, float slack, bool f_breakable = false);
    //A landing's strain on a breakable bridge, its warnings and its snap. See STRAIN.
    void StrainBridge(int bridge, int plank, float speed, StageEvents& events);
    //Every bridge one tick: her weight on the one she stood on last tick, then the substeps.
    //Before she moves, like the spring plants, so she lands on where it is now.
    void TickBridges();
    void StepBridge(StageBridge& br, float dt);
public:
    /*
        A web crossing her plane at x = cx, its opening w across (along the web) and h up from the
        ground at y, turned angle_deg about Y (see StageWeb): its nodes and threads, settled. Adds no
        block - the frame is looks, and the net stops her itself. Public so the rules test can hang
        one where it wants.
    */
    void AddWeb(float cx, float y, float w, float h, float angle_deg, int spokes, int rings, float hub_u, float hub_v);
private:
    /*
        One tick of one web. With `body`, her capsule pushes the threads and they push back: the
        force on her along x comes back in `out_fx`, and each thread that touched her in `touched`.
    */
    void StepWeb(StageWeb& web, float dt, const StageWebBody* body = NULL, float* out_fx = NULL,
                 std::vector<uint8_t>* touched = NULL, std::vector<int>* strained = NULL);
    //Steps every web against her, slows her by its push, bursts the last threads, notes her through,
    //and carries its caught arrows. Before she moves, like the bridges.
    void TickWebs(StageEvents& events);
    //An arrow's segment this tick, from -> to, against every web: tears, slows and catches. True
    //if it was caught, in which case it is already stuck where it met the web.
    bool ArrowThroughWebs(int arrow, const v3& from, const v3& to, StageEvents& events);
    //A web pushed her this tick (TickWebs runs before TickArcher): her idle friction stands aside.
    bool WebPushing() const{
        for (const StageWeb& w : webs){
            if (w.contacts > 0 && w.push != 0.0f){
                return true;
            }
        }
        return false;
    }
public:
    //Threads still holding across the frame: not cut, and not part of a strand left hanging by
    //one end - see the definition. Per thread. Public for the rules test and the app's colours.
    void WebHeldThreads(const StageWeb& web, std::vector<uint8_t>& held) const;
    //The held threads across her path through it, feet v0 to head v1 - what the burst counts.
    void WebPathThreads(const StageWeb& web, float v0, float v1, std::vector<int>& out) const;
private:
    //A trigger - a zone that is not an area - by its edges, with one effect. Returns its index.
    int  AddTrigger(const char* name, float left, float right, float bottom, float top, const StageZoneEffect& effect);
    //A crumble group of the blocks from `first` to the end of `blocks`, in that order, starting
    //`ticks_per_unit` apart along x from the first's left edge. Returns its index, for an effect.
    int  AddCrumbleGroup(const char* name, size_t first, float ticks_per_unit);
    //Which zones her body overlaps now, against last tick: the entered and left events, and a
    //first entry's effects queued. Last in Tick, once she has moved.
    void TickZones(StageEvents& events);
    void ApplyZoneEffect(const StageZoneEffect& effect, StageEvents& events);
    //Effects a zone fired, waiting out their delay: applied on the tick `ticks` reaches `at`.
    struct PendingEffect{
        StageZoneEffect effect;
        uint64_t at = 0;
    };
    std::vector<PendingEffect> pending_effects;
    //The crumble blocks: start the one she stands on, and a started group's next ones; count the
    //shaking ones, drop the done. After she moves, so the tick she lands is the tick it starts.
    void TickCrumbles(StageEvents& events);
    //Whether she is standing on block `b` - on the ground, feet at its top, over it.
    bool StandingOn(const StageBlock& b) const;
    void TickBow(const ArcherInput& in, StageEvents& events);
    void TickArcher(const ArcherInput& in, StageEvents& events);
    void TickArrows(StageEvents& events);
    //Every spring plant's spring, one step, with her weight on the one she is standing on.
    void TickSpringPlants();
    //One tick of her lean while she stands on a branch, and the fall when it goes too far.
    void TickBalance(const ArcherInput& in, float land_speed, StageEvents& events);
    //The vitals, one tick, off where she ended it and what this tick's events say she did.
    void TickVitals(const StageEvents& events);
    /*
        Every surface that is not a block, as floors, once per move, after the blocks - see
        StageSurface. Three cases: a landing (from above the surface where it WAS, to below where it
        is), staying on one while walking along its slope, and leaving it. `from` is where she
        started the move; `f_on_block`, that the blocks already stood her on a floor;
        `f_was_grounded`, that she was on some floor before the move. The highest surface she lands
        on wins. Sets spring_on, branch_on and ramp_on, and hands a fresh landing's fall to a plant.
    */
    void CollideSurfaces(const v2& from, bool f_down_held, bool f_on_block, bool f_was_grounded,
                         StageEvents& events, bool& out_hit_floor);
    //The candidate surfaces under x now (and x_from then), for CollideSurfaces.
    void GatherSurfaces(float x, float x_from, bool f_down_held, std::vector<StageSurface>& out) const;
    /*
        One tick of an arrow's flight: gravity, then the sweep from where this step starts (the
        anchor on its first step - see ARROW_LENGTH) to `next`, against the blocks. Returns the
        block struck, or -1; moves nothing but the velocity. TickArrows and PredictArrowImpact
        both fly by it, which is what keeps the forecast honest.
    */
    int  FlyArrow(Arrow& a, v3& from, v3& next, v3& point, v3& normal) const;
    void Loose(StageEvents& events);
    void SelectArrow(const ArcherInput& in, StageEvents& events);

    //--- Hanging and climbing -------------------------------------------------------------------
    //A grabbable lip within reach right now, or -1. Fills the side of the block the archer is on.
    //Const and side-effect free, which is what lets the app draw a hint on it later.
    int   FindGrabbableLedge(float& out_side) const;
    void  EnterHang(int block, float side, StageEvents& events);
    void  ReleaseHang(StageEvents& events);
    //A branch she could catch right now in the air, or -1 - see BRANCH_HANG_DROP.
    int   FindCatchableBranch() const;
    void  EnterBranchHang(int branch, StageEvents& events);
    //MODE_HANG's branch half: hold, climb back up, or let go.
    void  TickBranchHang(const ArcherInput& in, StageEvents& events);
    void  TickHang(const ArcherInput& in, StageEvents& events);
    void  TickClimb(const ArcherInput& in, StageEvents& events);

    //--- The kick -------------------------------------------------------------------------------
    //Advances the kick timer and, on the ticks it is live, sweeps its box against the breakable
    //blocks and the obstacles. Everything it finds goes into `events`.
    void  TickKick(const ArcherInput& in, StageEvents& events);

    //--- Apples -----------------------------------------------------------------------------------
    //The pick under way: the clock, the close, and the end. Before she moves, like the kick.
    void  TickPick(StageEvents& events);
    //An arrow's segment this tick against the hanging apples: the first stem or apple along it.
    //True if it went INTO an apple, and is stuck there; a cut stem lets it fly on.
    bool  ArrowThroughApples(int arrow, const v3& from, const v3& to, StageEvents& events);
    //The apple pick's hash, only once an apple has been touched - see HashState.
    bool  ApplesTouched() const;

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

    /*
        Nearest block struck by the segment a->b, or -1. Fills the hit point and the face normal.
        In 3D, through each block's depth (StageBlock::Back/Front) as well as its x and y - which on
        a locked plane never decides anything, since z is 0 and every block covers z 0.
    */
    int   SegmentHitsBlock(const v3& a, const v3& b, v3& out_point, v3& out_normal) const;

    int   next_arrow = 0;           //the ring buffer's write cursor

    //What the blocks were when the edges were last built - see RefreshEdges - and the cheap
    //version Tick checks: how many there were, and how many alive.
    uint64_t edges_signature = 0;
    size_t   edges_blocks = 0;
    int      edges_alive = -1;
    uint64_t BlocksSignature() const;
    int      CountAliveBlocks() const;
};

#endif
