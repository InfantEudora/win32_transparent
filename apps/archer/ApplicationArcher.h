#ifndef _APPLICATION_ARCHER_H_
#define _APPLICATION_ARCHER_H_

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <string>

#include "Application.h"
#include "Stage.h"
#include "Puppet.h"
#include "Terrain.h"
#include "Foliage.h"
#include "Backdrop.h"
#include "Boulders.h"
#include "Vine.h"
#include "WindView.h"
#include "Leaves.h"
#include "Streaks.h"
#include "Fireflies.h"
#include "RopeMesh.h"
#include "DynamicChain.h"
#include "Bow.h"
#include "TextMesh.h"
#include "Sign.h"
#include "SoundSystem.h"
#include "CueLog.h"
#include "SpringHinge.h"

/*
    A side-view platformer about an archer, in 3D assets.

    archer/Stage holds the RULES - the level, the archer's own motion, the bow and the flight of an
    arrow, with no engine type anywhere in its header. This class is the VIEW plus the wiring: it
    turns the keyboard into one intent, turns Stage's numbers into lit geometry, hands
    reactphysics3d the jobs a solver is genuinely better at, and exposes the whole thing over MCP
    so the game can be played and measured without a human. The same split breakout and bomber
    make; see the long note at the top of Stage.h for where the seam falls and why.

    --- THE HYBRID BODY, WHICH IS THE ONE THING TO UNDERSTAND HERE ---------------------------------
    The archer is simulated by hand (Stage) AND has a real rigid body in the world. Those are not
    in competition, and the way they are joined is the core trick of this app:

      The body is KINEMATIC. Every tick, after Stage has decided where the archer now is, the body
      is given the VELOCITY that carries it from where it is to where Stage says it should be:

            body_velocity = (stage.pos - body_position) / timestep

      rp3d then integrates it and lands exactly on Stage's answer, because nothing stops a
      kinematic body. So Stage keeps sole ownership of the archer's position - there is no second
      integrator to fight with, and no drift.

    WHAT THE BODY IS AND IS NOT FOR. It collides with NOTHING (ARCHER_MASK_ARCHER is 0), because
    Stage resolves the archer against everything itself - the level, and since the props-block
    change, the crates and targets too. The app hands Stage each prop as a box before every tick
    (RefreshObstacles), Stage stops the archer against it exactly as against a wall and reports
    which one was leaned on, and the app turns that into a shove (ApplyPushes). One resolver, in
    one place, and the solver is never asked to referee a character it cannot stop.

    That arrangement replaced an earlier one worth knowing about, because the earlier one is the
    obvious thing to try. A kinematic body moved by velocity normally shoves dynamic bodies for
    free - but that trick quietly assumes the character is STOPPED by what it pushes, so the
    overlap stays shallow. While Stage did not know props existed the archer walked straight
    through a crate, and the solver spent every tick of that resolving a deep overlap against
    infinite mass. Now that a crate really does block, that assumption holds again and the solver
    could take the pushing back; it is still done here because "how hard can you shove a crate" is
    a gameplay number, and gameplay numbers belong next to the rest of them.

    The body earns its place anyway, for two things:

      - the arrow raycast needs a body to EXCLUDE, or every shot hits the archer who fired it;
      - the rope slice needs something for the solver to take over. MODE_ROPE hands this body to
        it as a DYNAMIC one and Stage::TickArcher returns early, so exactly one thing is
        integrating the archer at any moment - and because the body has been driven by velocity
        all along rather than teleported, it arrives in the solver's hands already moving at the
        speed the archer was running. That handoff is the seam the hybrid was chosen for, and it
        is marked in both files.

    --- THREADING ---------------------------------------------------------------------------------
      - Init()              render thread, once. All GL, all mesh building, all registration.
      - RunSimulationTick() physics thread, once per tick that RUNS, physics_mutex held. The game
                            lives here - it pauses and single-steps with the physics.
      - UpdateView()        physics thread, every pass including paused ones. Chrome only.
      - DrawImGuiUI()       render thread, physics_mutex held. Reads the live Stage; never waits.
      - MCP handlers        their own thread, NO lock. They read `snapshot` and submit input
                            events or SimCommands. They never touch the scene.
*/

/*
    Our own input actions, numbered from INPUT_LAST like every other app's.

    Laid out so that no key means two things. The pad column is the gamepad (see SetupInput):

        A / D, Left / Right     left stick   run
        S                                    drop through a one-way platform; let go of a ledge
        Space                   A            jump (hold for height, tap for a hop); climb up
        J                       L1           hold to draw the bow, release to loose
        Up / Down               right stick  tilt the aim, whether or not the bow is drawn; on a
                                             branch, keep her balance (Up leans away from the camera)
        K                       B            kick - shoves props hard, breaks walls
        Down+K / Up+K           B + stick    the low push kick / the high rising kick (KICK_SPECS)
        C                       X            kneel / stand up - a toggle; kneeling she can draw
        E                       Y            action - take the rope             (later slice)
        L                       R1           knife                             (later slice)
        Home                    Start        restart
        F1                                   the engine's ImGui panels

    Aim is on Up/Down and drop-through is on S rather than Down, which is the one arrangement that
    keeps every key unambiguous while leaving both the arrows and WASD usable for running.
*/
#define INPUT_ARCHER_LEFT           INPUT_LAST+1
#define INPUT_ARCHER_RIGHT          INPUT_LAST+2
#define INPUT_ARCHER_DOWN           INPUT_LAST+3
#define INPUT_ARCHER_JUMP           INPUT_LAST+4
#define INPUT_ARCHER_DRAW           INPUT_LAST+5
#define INPUT_ARCHER_AIM_UP         INPUT_LAST+6
#define INPUT_ARCHER_AIM_DOWN       INPUT_LAST+7
#define INPUT_ARCHER_ACTION         INPUT_LAST+8
#define INPUT_ARCHER_KICK           INPUT_LAST+9
#define INPUT_ARCHER_KNIFE          INPUT_LAST+12
#define INPUT_ARCHER_RESTART        INPUT_LAST+10
#define INPUT_ARCHER_TOGGLE_UI      INPUT_LAST+11
//F2: put the blockout boxes back on top of the terrain. See SetBlockoutVisible.
#define INPUT_ARCHER_TOGGLE_BLOCKOUT INPUT_LAST+13
/*
    The left stick's X axis, as a SCALAR rather than a pair of buttons.

    Left/right on the keyboard can only ask for a full run, so every speed between standing and 9.0
    existed only for the two or three ticks acceleration took to cross it. That is the reason the
    blend space has had so little to do: she was always at 0 or at the top of the ladder. A stick
    asks for any speed in between and holds it, which is what the ladder was built for.

    Nothing in the rules had to change to allow it - `target_vx` was already
    `move_axis * ARCHER_RUN_SPEED`, so the magnitude has always been honoured and only the input
    was quantised.
*/
#define INPUT_ARCHER_MOVE           INPUT_LAST+14
//The right stick's Y, the aim's scalar twin of Up/Down in the same way MOVE is of Left/Right.
//It is a RATE, not an angle: Stage tilts by aim_axis * BOW_AIM_RATE_DEG, so a small deflection
//creeps the aim and a full one tilts as fast as the keys. GatherInput squares it on the way in.
#define INPUT_ARCHER_AIM            INPUT_LAST+15
//C, or X on a pad: kneel, and stand back up. A toggle, so it is read as a press edge.
#define INPUT_ARCHER_KNEEL          INPUT_LAST+16
/*
    The title screen's "click to continue": a click anywhere on the window, Space, or A or Start on
    a pad. Its own action rather than a reuse of JUMP, because the title is the one place a mouse
    click means anything at all, and the click arrives through a full-window touch rect that only
    this action is bound to - see LayoutTouchButtons.

    NOT ENTER, which it used to be: Alt+Enter is the fullscreen toggle, and the Enter of it,
    released on the title, dismissed the title as well as resizing the window.
*/
#define INPUT_ARCHER_CONTINUE       INPUT_LAST+17
/*
    Escape, or Back on a pad: from a level, back to the title - a pause, since nothing of the
    level runs under it - and from the title, out of the game. Back was the recording toggle; that
    is F9 alone now, because a dev feature should not sit on a button the game needs.
*/
#define INPUT_ARCHER_MENU           INPUT_LAST+18

//Our own simulation commands, numbered from SIM_CMD_LAST. Both are intent arriving from OUTSIDE
//the simulation - a key, an MCP call, later a replay - which is what the command queue is for:
//the caller is on the wrong thread, and the handler runs at one known place in the tick.
#define ARCHER_CMD_RESTART          SIM_CMD_LAST+0
#define ARCHER_CMD_AIM              SIM_CMD_LAST+1      //value[0] = degrees, relative to facing
#define ARCHER_CMD_PLACE            SIM_CMD_LAST+2      //value[0] = x, value[1] = y
/*
    Drive the animation from outside the game - subtype is an ArcherAnimSource.

        ANIM_FROM_CLIP    value[0] = clip index (ArcherClip), value[1] = playback rate
        ANIM_FROM_PANEL   value[2] = ground speed, SIGNED along facing; value[3] = 1 on the ground
        ANIM_FROM_GAME    nothing else is read - the rules take the controls back

    On the queue rather than a setter for the usual reason: an MCP handler and the ImGui panel are
    both on the wrong thread, and this way the change lands at one known point in one known tick.
*/
#define ARCHER_CMD_ANIM             SIM_CMD_LAST+3
//Which camera - subtype is ARCHER_CAM_SIDE or ARCHER_CAM_ORBIT. The panel's radio buttons, for the
//tools: the orbit could only be reached by clicking, and so could not be tested by a script.
#define ARCHER_CMD_CAMERA           SIM_CMD_LAST+4
//The master volume - value[0] = 0..1. The panel's slider, for the tools: an agent replaying a
//recording at someone's desk wants it silent, and the cue log does not change with it.
#define ARCHER_CMD_SOUND_VOLUME     SIM_CMD_LAST+5

/*
    Collision filtering.

    The archer is kept out of the LEVEL category entirely, which is the important one and is not an
    optimisation: Stage already resolves the archer against the level by hand, so letting the
    solver also resolve a kinematic body against the same static geometry would be a second opinion
    on a question that already has an answer. A kinematic body wins every such argument by
    definition, so the visible symptom would not be the archer stopping - it would be the archer
    grinding through walls while the solver spent its budget complaining.
*/
#define ARCHER_CAT_LEVEL            0x01    //static blocks; props and debris rest on these
#define ARCHER_CAT_ARCHER           0x02    //the kinematic body, whose only job is to shove props
#define ARCHER_CAT_PROP             0x04    //crates, targets, bricks
#define ARCHER_CAT_DEBRIS           0x08
#define ARCHER_CAT_ROPE             0x10    //the links of the swinging rope

//And what each one is allowed to touch.
//
//THE ARCHER TOUCHES NOTHING, which is 0 and means exactly that - "in no category, collides with
//nothing" is a real filter, not an unset one (see the note on the bits in core/Object.h). Stage
//resolves the archer against the level AND the props by hand, so there is nothing left for the
//solver to have an opinion about, and a kinematic body of infinite mass arguing with geometry that
//has already been resolved wins every time in the least useful way.
//
//Worth knowing while reading the rest: the props ALSO used to have gravity switched off, because a
//body from AddPhysics starts with it off and SetStatic(false) does not turn it on. No gravity means
//no weight on the floor, no normal force and so NO FRICTION - anything touched once slid or drifted
//forever, which reads exactly like the solver exploding and had three wrong theories chased at it
//before anyone read `gravity: false` off object_get. See MakePlanarBody.
//The level accepts the ARCHER too, and that matters only on the rope: a pair collides when EACH
//side's mask takes the other's category, and off the rope her own mask is 0. Without it the level
//never took her, so ARCHER_MASK_ON_ROPE below was half a filter and a swing - or a cut rope - went
//straight through the floor (found 2026-09-24 on the rope bench, her at y -6 under the floor).
//The level and the props accept the ROPE as well, for the same reason: only a cut-off piece ever
//asks (ARCHER_MASK_ROPE_LOOSE) - a rope still hanging asks for nothing, so it still passes through.
#define ARCHER_MASK_LEVEL           (ARCHER_CAT_PROP | ARCHER_CAT_DEBRIS | ARCHER_CAT_ARCHER | ARCHER_CAT_ROPE)
#define ARCHER_MASK_ARCHER          0
#define ARCHER_MASK_PROP            (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP | ARCHER_CAT_ARCHER | ARCHER_CAT_DEBRIS | \
                                     ARCHER_CAT_ROPE)
#define ARCHER_MASK_DEBRIS          ARCHER_MASK_PROP
/*
    A ROPE LINK COLLIDES WITH NOTHING AT ALL, including the rest of its own rope.

    A chain of bodies whose links can touch each other is a chain that jitters: neighbouring links
    overlap by construction - that is what a joint holding them together MEANS - so every tick the
    solver is asked to both hold them together and push them apart. The joints alone make the rope,
    and a rope that hangs through the scenery is a far smaller problem in a side view than one that
    buzzes.
*/
#define ARCHER_MASK_ROPE            0
/*
    EXCEPT ONCE IT IS CUT OFF. A piece below a cut is loose - it falls, and with the mask above it
    fell through the floor for good (found at y -1356). It lands on the level and the props instead;
    its links still ignore each other, for the reason above. Set by CutRopeJoint, undone by the
    restart that rebuilds the chain.
*/
#define ARCHER_MASK_ROPE_LOOSE      (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP)
//What the archer collides with WHILE SWINGING, which is the one time the solver owns them. Off the
//rope it is ARCHER_MASK_ARCHER (nothing), because Stage resolves everything itself - but on the
//rope Stage is not resolving anything, so without this the swing passes through the floor.
#define ARCHER_MASK_ON_ROPE         (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP)

/*
    The rope, as bodies.

    Links of about ROPE_LINK_LENGTH - eight over the main level's six units - short enough that the
    rope bends visibly rather than swinging as a plank, long enough that the solver is not holding
    thirty constraints together for a piece of set dressing. The count follows the rope's declared
    length, so the rope test's nine-unit rope gets twelve. The links are light against the
    archer's 70kg on purpose: a rope that weighs as much as the person on it swings like a wrecking
    ball rather than like a rope.
*/
#define ROPE_LINK_LENGTH            0.75f
#define ROPE_SEGMENT_MASS           1.2f
#define ROPE_SEGMENT_THICK          0.12f
//The links near the anchor are not offered as handholds - catching a rope at the very top gives a
//swing with no arc in it, and looks like the archer stuck to the ceiling.
#define ROPE_FIRST_GRABBABLE        2
/*
    HOW HARD HER OWN SPIN IS DAMPED while she is hanging, as a decay rate in 1/s - rp3d applies it
    as w *= 1/(1 + d*dt), so this is the e-folding rate rather than a fraction.

    It exists because the archer hanging from a rope is TWO pendulums, not one: the rope swings,
    and she swings about her own grip inside it. The second one had nothing damping it at all.
    Measured before this line existed, her body reached 88 degrees of tilt while the rope it hung
    from only reached 53 - she was windmilling, slowly, and building.

    Her natural period about her hands is 2.2s, so critical damping is about 5.6 and this is a
    little over half of that. Deliberately not critical: a body that snaps rigidly into line with
    the rope reads as a plank, and the lag between her and the rope is the part that looks alive.
*/
#define ROPE_HANG_DAMPING           3.0f
//The rope is offered to Stage as a point every this far down it, not one per link, so she catches it
//at the point nearest her hands rather than at the centre of whichever link is nearest.
#define ROPE_GRAB_STEP              0.25f
/*
    WHERE ON HER THE ROPE HOLDS - the body's end of the joint, above her centre in her own frame.

    THE MOUNT, and a decision still open (vine_plan.md step 5): the top of her box today. The drawn
    hands of Hanging_Rope sit about 0.19 above it; the climb's gripping hand is what this is being
    judged against. One number, so moving the mount is changing it.
*/
#define ROPE_GRIP_BODY_Y            ARCHER_HALF_H
//Over how much climbing the anchor carried across a link crossing fades back onto the link's own
//geometry. A crossing is a joint's stretch (0.13 - 0.20 loaded) to absorb; this spreads it over
//about half a second of climb. See ReanchorRopeJoint.
#define ROPE_GRIP_BLEND             0.30f
//The rope bench's peak gap is the worst over this long, so a spike is still on the readout by the
//time anyone looks.
#define ROPE_PEAK_TICKS             180
/*
    The drawn rope's thickness, as a multiplier on the file's pieces (her scale already applied).
    Only the cross-section and the pieces: the length is always the chain's. 1 draws the rope as
    modelled - rope_segment is 0.17 across in the file, 0.34 in the world, which is thick beside
    her hands; that is the number to turn if it reads as a hawser.
*/
#define ROPE_MESH_SCALE             0.7f
//The collars: two under the anchor and one over the tassel, as in the reference model, stacked by
//their own MEASURED height with this much rope showing between them - so a re-exported collar of
//another size is spaced by itself rather than overlapping a typed-in position.
#define ROPE_COLLAR_GAP             0.06f

/*
    How much of an arrow's speed the thing it hits takes, 0..1.

    A RATIO rather than a force, because it is scaled by the struck body's own mass when it is
    applied - so a target board and a crate pick up the same velocity from the same arrow, and this
    one number stays meaningful instead of needing a sibling per prop. At 0.04 a full-draw arrow
    hands a target 1.8 units a second, which topples a standing board and rocks a crate without
    launching either. Tunable live in the Archer panel.
*/
#define ARROW_SPEED_TRANSFER        0.040f

/*
    The hit counter: every arrow into a target counts, and the new count floats up out of the hit.

    TICKS, not seconds, so a popup freezes with the simulation when it is paused and can be stepped
    through frame by frame like everything else - see the house rule in CLAUDE.md.

    The numbers are meshes (core/TextMesh.h), baked ONCE at Init for 1 .. HIT_NUMBERS_MAX, because
    BuildTextMesh is render-thread only and a hit lands on the physics thread. A hit then only
    points a pooled Object at the right, already-built mesh. Past the last one it keeps showing
    the last one; ninety-nine arrows into one board is a stress test, not a score.
*/
#define HIT_POPUP_SLOTS             8       //popups up at once; the oldest is reused past that
#define HIT_POPUP_TICKS             60      //one second at ARCHER_TPS
#define HIT_POPUP_RISE              1.6f    //world units it climbs over its life
#define HIT_POPUP_SCALE             0.9f    //glyph height, in world units, at full size
#define HIT_NUMBERS_MAX             99

/*
    On the RANGE, a floating target gets its gravity back on its third hit - it hangs until it
    has been hit this many times and then drops. The main level has no floating targets and no
    such rule; there a hit only counts.
*/
#define RANGE_GRAVITY_HITS          3

/*
    --- THE ARCHERY STAND (TARGET_STAND) -----------------------------------------------------------
    archer.glb's archery_target: a round board on a tripod, drawn at the character's scale. Its body
    is the board as a flat CYLINDER and each leg as a BOX - Physics::AddCylinderCollider - so it
    rolls, tips and lands like the thing it looks like, not like its bounding box.

    HEAVY, NOT PINNED. It can tip, and a kick tips it; an arrow must not. "Heavier" cannot simply be
    a bigger mass here, because every shove in this app is a VELOCITY - the arrow's is scaled by the
    struck body's own mass so that every prop takes the same speed from the same shot (see
    ARROW_SPEED_TRANSFER), and the walk and the kick set speeds outright. So the weight is a HEFT:
    what an arrow gives a stand is divided by it, and a kick uses its own, gentler speed plus a spin
    that tips the stand away from the boot rather than sending it flying. The walk still shoves it
    at her pace, like a crate - moving it about is meant to be possible.

    The legs are the dense part, so the centre of mass sits low and a shove along the floor slides
    it rather than tipping it over its own feet; and it is less grippy than a crate for the same
    reason - friction at the feet times the height of the mass is what tips a sliding thing.
*/
#define TARGET_BOARD_MASS           3.0f    //the plain board, and the heft's yardstick
#define STAND_MASS                  18.0f
#define STAND_HEFT                  (STAND_MASS / TARGET_BOARD_MASS)
#define STAND_LEG_DENSITY           4.0f    //against the board's 1
#define STAND_FRICTION              0.35f
//Turned this far from facing the camera toward where she comes in: mostly at her, so her arrows go
//into the FACE, and the last 20 degrees left toward the camera so the rings still read. Set by eye
//in the Inspector (-70 about Y for a stand to her right).
#define STAND_YAW_DEG               70.0f
//A kick: this fraction of KICK_SPEED and KICK_LIFT, and this much spin (rad/s) away from the boot.
//MEASURED DOWN from 0.45 and 3.5, which cartwheeled it: up 1.3 units, past upside down (163 deg)
//and 4.5 units along before it settled. A heavy stand is knocked over, not thrown.
#define STAND_KICK_SCALE            0.25f
#define STAND_KICK_SPIN             2.2f
/*
    The rings, as fractions of the board's radius from its centre, and what each is worth: the red
    centre, white, red, white, then the wooden rim. Read off the model and meant to be checked
    against it. An arrow outside the board - a leg - scores nothing and pops nothing.
*/
#define STAND_RING_COUNT            5
static const float STAND_RINGS[STAND_RING_COUNT]  = { 0.20f, 0.40f, 0.60f, 0.80f, 1.00f };
static const int   STAND_POINTS[STAND_RING_COUNT] = { 10,    8,     6,     4,     2 };

/*
    --- THE STRAW MAN -------------------------------------------------------------------------------
    A kicking dummy on a spring hinge at its foot (core/physics/SpringHinge; apps/archer/
    strawman_plan.md). Scores the KICK, one point each; an arrow only sticks in it and sets it
    swinging. Passable: the rules offer it as a non-blocking obstacle, and its body leaves the
    archer's kinematic body out of its mask, so she walks through it in both.

    TUNED AS A SWING, not as a spring constant: STRAW_HZ and STRAW_DAMPING_RATIO are what is seen,
    and SpringHinge works k and c out of the body - gravity's pull over the pivot included, which on
    an upright dummy is working against the spring. Both have sliders in the panel.
*/
#define STRAW_MASS                  6.0f
#define STRAW_HZ                    1.3f
#define STRAW_DAMPING_RATIO         0.12f    //amplitude about halves each swing (e^(-2 pi zeta))
#define STRAW_LIMIT_DEG             65.0f    //each way; keeps a hard kick off the floor
/*
    The kick: an impulse of mass x KickSpec speed x this, at the height the boot lands, applied as
    one tick of force the way an arrow's is. The hinge does the rest - the same boot higher up is
    more lever, and the swing is whatever the inertia makes of that.
*/
#define STRAW_KICK_PUNCH            1.0f

/*
    --- THE BANK BEHIND THE TERRAIN -----------------------------------------------------------------
    Backdrop.h places it, Terrain.cpp meshes it on a params set of its own (BackdropTerrainParams in
    the .cpp: rounder, noisier and coarser than the slab's, since it is further off). HAZE is how far
    its three colours are pulled toward the painted backdrop's teal-grey rock - the aerial perspective there is
    no fog to give it.
*/
#define BACKDROP_HAZE               0.75f   //0.45 read olive; the user asked for more teal
/*
    And its FILL: this share of its own colour it gives off, unshadowed. Most of the bank stands in
    the slab's shadow - the sun is in front and above - and the renderer has no per-object "receive
    no shadows", so without it everything under the slab came out black. A far layer lit by the sky
    is what this stands in for.
*/
#define BACKDROP_FILL               0.35f

/*
    And until then a floating target is DAMPED, each one differently.

    Undamped - a body starts at 0, see the damping note in core/physics/Physics.h - a hit sent a
    board drifting clean across the range into the wall: measured, one went from x 4.3 to 16 over
    five shots and could only be hit three times by re-aiming at it every shot. Damped between
    these two, the same three hits took four shots and moved it under a unit (x 4.3 to 5.3), so
    three hits is a thing a player can do. RANDOM WITHIN THE RANGE so the five do not all behave
    as one; seeded from the prop's index, so each board is the same board every run and a
    measurement of one can be repeated. Put back to the undamped value when gravity comes on, so
    a board that drops falls like a board rather than sinking like a leaf.
*/
#define FLOAT_DAMPING_MIN           1.5f
#define FLOAT_DAMPING_MAX           4.0f

//How hard the archer shoves a prop is ARCHER_PUSH_SPEED, over in Stage.h with the rest of the feel
//numbers - the rules decide it, because the rules are what stop the archer against the thing being
//pushed. This file only carries it out; see ApplyPushes.

/*
    What a broken wall bursts into.

    A cap, because a kick can bring down a whole wall of bricks and every chunk is a rigid body in
    the same world everything else has to be solved against. Reaped on a timer as well, so a level
    somebody has spent five minutes demolishing does not end up carrying its entire history.
*/
#define ARCHER_DEBRIS_PER_BLOCK     7
#define ARCHER_DEBRIS_TICKS         480
#define ARCHER_MAX_DEBRIS           120

/*
    The character model, and the three names inside it that this app depends on.

    ALL THREE ARE STRINGS THE EXPORT DECIDES, and each fails differently when it is wrong: a bad
    skin name gives no skeleton at all (loud), a bad node name gives a skeleton with nothing to
    draw (loud), and a bad ROOT BONE name gives a fully working skeleton whose root motion can
    never be measured or extracted (silent). BuildArcherModel reports each one separately for that
    reason. The clip names live in Puppet.h with the rest of the animation's data.
*/
#define ARCHER_MODEL_ASSET          "meshes/archer.glb"
#define ARCHER_MODEL_SKIN           "archer_armature"
#define ARCHER_MODEL_NODE           "archer"
#define ARCHER_MODEL_ROOT_BONE      "mixamorig:Hips"
//The bone whose height says when a foot is DOWN - see MeasureClipPhases. The toe rather than the
//ankle because it is the last thing to leave the ground and the first to touch it, so its minimum
//is a sharper marker than the ankle's.
#define ARCHER_MODEL_TOE_BONE       "mixamorig:LeftToeBase"

/*
    How tall the model is drawn, in world units.

    It is ARCHER_HALF_H * 2 and it has to be - the level's whole blockout is derived from the body
    box (what the feet reach, what the hands reach, how wide a gap a running jump clears), so a
    model that disagrees with the box is a model standing in a level built for someone else. The
    rig is authored about 0.89 units tall, so the scale this works out to is roughly 2x; it is
    MEASURED from the bind pose at load rather than typed here, because a re-export at a different
    size must not silently shrink the character. See BuildArcherModel.
*/
#define ARCHER_MODEL_HEIGHT         (ARCHER_HALF_H * 2.0f)

/*
    Where the animation's parameters come from.

    The point of the split is that these are indistinguishable downstream: Puppet::Tick cannot
    tell which one filled the struct, so what the panel shows IS what the game does. See the note
    at the top of Puppet.h.
*/
enum ArcherAnimSource{
    ANIM_FROM_GAME = 0,     //the rules fill ArcherAnimParams - normal play
    ANIM_FROM_PANEL,        //the sliders fill it - tune the decisions with no level in the way
    ANIM_FROM_CLIP          //one clip on loop, decisions bypassed entirely - the export check
};

/*
    The archer's skeleton, which exists as its own class for ONE overridden method.

    A clip can turn the character - `Running_TurnAround` is a 180, `Twirl` is a spin - and that
    turn lives in the root bone's YAW. Animation::SampleRootMotion takes it off the bone
    unconditionally (unlike the position, which is gated on the extract flags) and hands it to
    Object::ApplyRootMotion, whose base implementation does NOTHING. So on a plain Skeleton the
    rotation in a clip is stripped from the pose and then dropped on the floor, and the character
    stands there not turning - which looks like the clip not having any rotation in it.

    PlayerCharacter overrides this to turn AND move; the archer only wants the turn, because Stage
    owns where she is and a clip must never be allowed to walk her off a ledge.
*/
/*
    --- THE AIM OVERRIDE (animation_plan.md, Step 3) ---------------------------------------------
    After the clips have posed her, the spine and shoulders are turned about the WORLD's Z - the
    play plane's normal, the axis pointing at the camera - so the bow points along aim_deg.

    Rotations about one shared world axis ADD, whichever bone they are applied to. So however the
    angle is split over the chain below, every bone downstream of all of it turns by exactly the
    whole angle - and the bow hangs off LeftHand, downstream of Spine..Spine2 and LeftShoulder. The
    shares on the bow's path therefore have to sum to 1, and that is what makes the drawn bow and
    Stage's arc agree to rounding rather than roughly. The neck's share is on top: the head is not
    on the bow's path, so it only decides how far she looks along the arrow.

    The shoulders rather than the arms, for the nock: LeftShoulder and RightShoulder pivot close
    together at the top of the chest, so turning both keeps the drawing hand near the string. The
    arms pivot a shoulder-width apart, and turning them would pull the hand off the nock by about
    that width times the angle.
*/
#define ARCHER_AIM_SPINE_SHARE      0.15f   //each of Spine, Spine1, Spine2
#define ARCHER_AIM_SHOULDER_SHARE   0.55f   //each of LeftShoulder, RightShoulder; 3*0.15 + 0.55 = 1
#define ARCHER_AIM_NECK_SHARE       0.35f   //on top, so her head follows most of the way

#define ARCHER_AIM_BONES            6

/*
    --- THE UPPER-BODY LAYER (animation_plan.md, Step 2) -----------------------------------------
    A second clip over the base, on every bone under mixamorig:Spine, graded so the torso keeps
    some of the base's lean while the arms, shoulders and head are the layer's: Spine and Spine1
    take part of it, everything from Spine2 up takes all of it. A PROTOTYPE living here until it
    earns a place in Object, the way the crossfade did.
*/
#define ARCHER_UPPER_ROOT           "mixamorig:Spine"
#define ARCHER_UPPER_SPINE_SHARE    0.3f    //mixamorig:Spine
#define ARCHER_UPPER_SPINE1_SHARE   0.6f    //mixamorig:Spine1; everything above is 1.0

/*
    --- THE LOOSE LEGS (animation_plan.md, "Loose legs") -----------------------------------------
    Each leg a core/DynamicChain - hip, knee, ankle, toe - stepped every tick in world space and
    swung in the play plane, so her legs trail, float and overshoot with the swing instead of
    being welded to her pose. Its weight and the pump's lead are the Puppet's (leg_weight,
    leg_lead_deg); how loose is the model's, on the panel.

    The defaults: soft enough to trail a catch or a turn at the end of an arc by a good few
    degrees, stiff enough that she is still holding the pose rather than hanging dead. The limits
    keep a leg a leg - the hip within ARCHER_LEG_HIP_LIMIT of the pose, the knee folding one way
    only and within its own, the ankle nearly set.
*/
#define ARCHER_LEG_BONES            4       //UpLeg, Leg, Foot, ToeBase: the chain's four points
#define ARCHER_LEG_STIFFNESS        0.07f
#define ARCHER_LEG_DAMPING          0.06f
//All of the clip's own leg motion passed straight through (DynamicChainParams::follow). Without
//it the chain lagged Hanging_Rope's authored leg swing too, and a knee authored at 3 degrees
//wobbled between 25 and 60 while she hung still.
#define ARCHER_LEG_FOLLOW           1.0f
#define ARCHER_LEG_HIP_LIMIT        1.0f    //radians off the pose, either way
#define ARCHER_LEG_KNEE_LIMIT       0.7f
#define ARCHER_LEG_ANKLE_LIMIT      0.35f

class ArcherModel : public Skeleton{
public:
    //Radians of yaw this clip has turned her through since it started. ADDED to the facing the
    //Puppet asks for, rather than replacing it - a turn authored in a clip is a turn relative to
    //wherever the character was already pointing. Reset when the clip changes.
    float clip_yaw = 0.0f;
    void ApplyRootMotion(const RootMotionDelta& delta) override;

    /*
        The base clips' pose, then the upper layer over it, then the loose legs, then the aim over
        all of it. See the notes above.

        IT UNDOES ITS OWN WORK FIRST. The base only writes a bone on a pass that actually poses - a
        paused sim, a stepped debug override or a finished one-shot may leave last tick's pose in
        place - and a layer or a turn applied on top of a pose it has already changed would
        accumulate: the spine creeping toward the layer's pose, the aim turning her a little further
        every frame. So every bone either touches has its base (clip-posed) transform kept, put back
        before the base runs, and the layer and aim re-applied to whatever the base left.
    */
    void ApplyAnimation(float time_delta) override;

    //Finds the chain. Call once the skeleton is loaded; with any bone missing the override is off.
    bool BuildAimChain();
    //Finds the upper layer's bones and their shares. Call once the skeleton is loaded.
    bool BuildUpperMask();
    //Finds every bone for the whole-body overlay. Call once the skeleton is loaded.
    bool BuildOverlay();

    //--- Set by the app each tick ---
    Animation* upper_clip = NULL;   //NULL: no upper layer
    float upper_time = 0.0f;        //seconds into upper_clip to sample
    float upper_weight = 0.0f;      //0..1, from Puppet::upper_weight
    //The layer's own crossfade (Puppet::upper_mix): the clip it is leaving and the time that clip
    //was last shown at, frozen. NULL when not crossfading.
    Animation* upper_from_clip = NULL;
    float upper_from_time = 0.0f;
    float upper_mix = 1.0f;
    //The whole-body overlay (PuppetChoice::overlay_clip): one clip at one time over the base.
    Animation* overlay_clip = NULL;
    float overlay_time = 0.0f;
    float overlay_weight = 0.0f;
    float aim_target_deg = 0.0f;    //Stage::aim_deg
    float aim_weight = 0.0f;        //0..1, from Puppet::aim_weight
    float aim_facing = 1.0f;        //+1 right, -1 left

    /*
        THE LIVE NEUTRAL, and the check on it: the angle the bow's front makes in the play plane,
        relative to facing, + up - Stage::aim_deg's convention. `aim_pose_deg` is read after the
        layers and BEFORE the aim turn, and the turn is aim_target_deg minus it, so a pose aiming
        anywhere ends on the rules' angle. `aim_drawn_deg` is read after the turn - the check.
    */
    Object* aim_probe = NULL;       //the bow
    float   aim_pose_deg = 0.0f;
    float   aim_drawn_deg = 0.0f;

    //--- The loose legs - see ARCHER_LEG_BONES ---
    //Finds both legs' bones. Call once the skeleton is loaded; a leg with any bone missing stays
    //as the clip has it.
    bool  BuildLegChains();
    float leg_weight = 0.0f;        //0..1, from Puppet::leg_weight
    float leg_lead_deg = 0.0f;      //from Puppet::leg_lead_deg
    //Gravity is the app's to set, from the physics world the swing is in; stiffness and damping
    //are the panel's.
    DynamicChainParams leg_params;
    //Her yaw (radians) and the rope's tilt as last drawn - set with the model's rotation. A change
    //of yaw is a TURN, her decision, and the chains are carried through it rigidly; only the
    //swing is left to their inertia. See ApplyLegChains.
    float leg_drawn_yaw = 0.0f;
    quat  leg_drawn_tilt = quat(0.0f,0.0f,0.0f,1.0f);
    //Measured each tick, left then right: the thigh's swing off the pose as drawn (+ forward), and
    //the knee's bend (+ the way a knee folds), degrees. For archer_state.
    float leg_swing_deg[2] = {};
    float leg_knee_deg[2] = {};

    /*
        Turns a bone by `angle` about a WORLD axis, keeping where it is. A world turn R on a bone
        whose world rotation is P * L gives R * P * L = P * (P^-1 R P) * L, and P^-1 R P is the
        same angle about the axis carried into the parent's space - so the local rotation gains a
        turn about that. The aim and the legs both use it.
    */
    static void TurnInWorld(Bone* bone, const vec3& axis_world, float angle);

private:
    Bone* leg_bones[2][ARCHER_LEG_BONES] = {};
    DynamicChain leg_chain[2];
    bool  f_leg_yaw_seen = false;
    float leg_last_yaw = 0.0f;      //the yaw the chains were last stepped under
    void  ApplyLegChains(float time_delta);

    Bone* aim_bones[ARCHER_AIM_BONES] = {};
    float aim_shares[ARCHER_AIM_BONES] = {};

    std::unordered_map<Object*,float> upper_share;  //bone -> its share of the layer
    std::vector<Bone*> upper_order;                 //the same bones, parents before children
    std::vector<Bone*> overlay_bones;               //every bone of the body, for the overlay
    void  ApplyOverlay();

    //Every bone the layer or the aim may change, with its base transform from the last pass that
    //changed any - see ApplyAnimation.
    std::vector<Bone*> layered_bones;
    std::vector<quat>  layered_rot;
    std::vector<vec3>  layered_pos;
    bool  f_layered = false;        //the saved transforms hold a pose the layer or aim has changed

    void SaveBasePose();
    void RestoreBasePose();
    void ApplyUpperLayer();
    void LayerClipModel(Animation* clip, float time, std::unordered_map<Object*,quat>& out_model,
                        std::unordered_map<Object*,vec3>& out_pos);
    float BowAimDeg();              //the bow's front, in the play plane, relative to facing
};

/*
    The backdrop.

    One quad, unlit, a long way behind the play plane, sized so that it still covers the view when
    the camera is zoomed all the way out. It is PARALLAX rather than scenery: `background_follow`
    is the fraction of the camera's motion it copies, so 1.0 pins it to the camera and it reads as
    infinitely far away, and 0.0 nails it to the world and it slides past at the same rate as the
    ground. Anything between is a distance.

    The image is 4096x2336, landscape and a hair narrower than 16:9, and it is fitted to COVER
    rather than to contain: scaled until it fills the width, with the overflow running off the top
    and bottom. Letterboxing a backdrop is worse than cropping one. (It used to be a 1152x1536
    portrait PNG, which is why the panel still has an offset slider for choosing a band of it; on
    this one the overflow is small and the slider mostly has nothing to do.)
*/
#define BACKGROUND_ASSET            "images/background1.jpeg"
#define BACKGROUND_IMAGE_ASPECT     (4096.0f / 2336.0f)
#define BACKGROUND_DEPTH            40.0f   //behind the play plane, in world units
#define BACKGROUND_FOLLOW           0.85f   //0 = nailed to the world, 1 = pinned to the camera
#define BACKGROUND_COVER            1.15f   //margin over the view it has to fill

/*
    The title screen: the painted title art, with the loading progress over it while Init builds
    the level, and "click to continue" once it has. See CreateTitleScene and LoadingStep.

    Fitted to COVER like the backdrop - a window narrower than the art loses a little of each
    side, one wider loses a little top and bottom, and neither gets bars. The art keeps the archer
    well in from the left edge for exactly that reason.
*/
#define TITLE_ASSET                 "images/title_background.jpeg"
#define TITLE_IMAGE_ASPECT          (4096.0f / 2336.0f)
//One full breath of "click to continue", in TICKS - the title scene ticks like any other.
#define TITLE_PULSE_TICKS           90

//The camera trails the archer rather than being welded to them - see UpdateCamera.
#define CAMERA_DISTANCE             26.0f
//And the wheel moves it in and out. Proportional rather than a fixed step, so a notch feels the
//same close up and far away - 10% of wherever it currently is.
#define CAMERA_ZOOM_PER_NOTCH       0.10f
#define CAMERA_DISTANCE_MIN         5.0f    //close enough to read a hand
#define CAMERA_DISTANCE_MAX         60.0f   //the whole of a screen's worth of level
/*
    How far above the target the camera sits AT CAMERA_DISTANCE. It scales with the distance
    rather than staying put, so the view keeps one pitch as the wheel moves it: a fixed 3.2 is a
    7-degree look down at 26 units and 33 degrees at 5, and zooming in to read a hand ended up
    looking down on her head. At the default distance the two are the same thing.
*/
#define CAMERA_HEIGHT               3.2f
/*
    Half the width of the sun's shadow ortho AT CAMERA_DISTANCE, in world units. PlaceCamera scales
    it with the zoom, so shadow texels stay the same size ON SCREEN rather than in the world: a
    fixed 22 is half a pixel a texel at the default view, several pixels a texel at
    CAMERA_DISTANCE_MIN (the stair-stepped edges), and narrower than the view itself at
    CAMERA_DISTANCE_MAX, where shadows simply stopped short of the screen edges. 22 over the
    ~15.9 of half-view at the default is the margin for casters just off screen.
*/
#define SUN_SHADOW_EXTENT           22.0f
#define SUN_OFFSET                  vec3(-9.0f,20.0f,10.0f)   //from the view's target, see SetupLights
/*
    Which camera is driving. SIDE is the game's own - trailing on the world, fixed on the range,
    always square-on to the play plane. ORBIT is the middle-mouse orbit from apps/isoanimation
    (by way of apps/bomber, which fixed its input handling): a debugging view for looking at the
    character and the terrain from angles the game never shows. The panel switches.
*/
#define ARCHER_CAM_SIDE             0
#define ARCHER_CAM_ORBIT            1
#define CAMERA_FOV                  38.0f   //vertical, degrees - see SetupCamera
/*
    How the side camera follows her: ONE set for every scene, on the panel's sliders so it can be
    tuned by feel. The range and the rope scene used to follow at a quarter of the main level's
    rate (to keep the range's targets still under the aim), and the rope scene's pits lost her.

    `follow_x` / `follow_y` are the per-tick ease toward where it wants to be: 0.10 closes a tenth
    of the gap a tick, about a sixth of a second to settle. `lead` is how far ahead of a full
    sprint it looks, as a fraction of the view's half-width, so it scales with the zoom - 0.21 is
    the old 3.0 units at the default distance. `keep_in` is the hard limit: her body box stays
    inside that fraction of the view's half-extents whatever the rates, the zoom or the speed.
*/
struct ArcherCameraTuning{
    float follow_x = 0.10f;
    float follow_y = 0.10f;
    float lead = 0.21f;
    float keep_in = 0.70f;
};

/*
    What the MCP tools are allowed to see.

    A tool handler runs on the server's own thread and holds NO lock, so reading the Stage from one
    is a straight data race against the physics thread. RunSimulationTick fills this at the end of
    every tick under `snapshot_mutex` and every tool serves from it. The cost is one copy per tick;
    the benefit is that telemetry never disturbs the game it is measuring, which matters when the
    whole point is to play the game through the tools. Copied wholesale from breakout, which
    explains the alternative (Scene::AtTickBoundary) and why this is the better trade for a game
    that is read far more often than it is written.
*/
struct ArcherSnapshot{
    uint64_t tick = 0;
    uint64_t stage_ticks = 0;
    //Which Stage these numbers came from - STAGE_LEVEL_MAIN or STAGE_LEVEL_RANGE. Reported because
    //every other field here means something different on each, and a script that switched scenes
    //should be able to tell it landed without a screenshot.
    int   level = STAGE_LEVEL_MAIN;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float facing = 1.0f;
    int   mode = MODE_AIR;
    bool  f_on_ground = false;
    int   coyote_ticks = 0;
    int   kick_ticks = 0;           //Stage::kick_ticks: 0 not kicking, else 1..that kick's length
    int   kick_kind = 0;            //KickKind of the kick running, or of the last one

    int   bow_mode = BOW_IDLE;
    int   draw_ticks = 0;
    float draw_power = 0.0f;
    float aim_deg = 0.0f;
    /*
        THE AIM CHECK: where the drawn bow actually points (ArcherModel::aim_drawn_deg, same
        convention as aim_deg), how much of the aim her body has taken, and the neutral the
        override works from. With aim_weight at 1 the drawn angle should equal aim_deg to within a
        degree or two - that is the promise the override makes, and this is how it is kept.
    */
    float aim_drawn_deg = 0.0f;
    float aim_weight = 0.0f;
    float aim_sway_deg = 0.0f;      //Stage::AimSwayDeg; aim_deg above already includes it
    float aim_neutral_deg = 0.0f;   //the LIVE neutral - see ArcherModel::aim_pose_deg
    int   upper_clip = -1;          //the upper-body layer's clip, and its weight
    float upper_weight = 0.0f;
    int   kneel_phase = -1;         //KneelPhase while kneeling, else -1
    float body_height = 0.0f;       //Stage::BodyHeight - shorter while kneeling
    /*
        THE ANCHOR CHECK: the nocked arrow's origin on the model, relative to her centre with x
        along facing, beside the anchor the rules shoot from (Stage::AnchorPosition). At full draw
        the two should agree to a few hundredths - BOW_NOCK_* and KNEEL_NOCK_* are this, measured.
    */
    float nock_fwd = 0.0f;
    float nock_up = 0.0f;
    float anchor_fwd = 0.0f;
    float anchor_up = 0.0f;
    //The string: how far it is drawn on screen, whether an arrow is on it, and how far the drawing
    //hand is from its pull line (rig units) - see Bow::TrackHand.
    float string_draw = 0.0f;
    bool  f_arrow_on_string = false;
    float hand_off_string = 0.0f;
    bool  f_arrow_in_hand = false;  //taken from the quiver, not yet on the string
    float hand_off_quiver = 0.0f;
    int   live_arrows = 0;
    int   arrows_shot = 0;
    int   arrows_hit_blocks = 0;
    bool  f_paused = false;

    //Every target, so a script can check its own shooting without a screenshot. `knocked` is the
    //thing worth measuring: a target board that has been tipped past halfway.
    struct TargetView{
        float x = 0.0f;
        float y = 0.0f;
        float tilt_deg = 0.0f;
        bool  f_knocked = false;
        int   hits = 0;             //arrows that have gone into it - the hit counter
        bool  f_floating = false;   //built with gravity off
        bool  f_gravity = true;     //whether gravity acts on it now
        float linear_damping = 0.0f;
        float angular_damping = 0.0f;
        bool  f_stand = false;      //TARGET_STAND rather than a plain board
        int   score = 0;            //a stand's points so far
    };
    std::vector<TargetView> targets;
    int   archery_score = 0;        //points on every stand this level
    int   archery_last_points = -1; //the last arrow into a stand; -1 before there has been one

    //Straw men: the swing, and the kicks landed on each.
    struct StrawView{
        float x = 0.0f;
        float angle_deg = 0.0f;         //SpringHinge::GetAngle
        float joint_angle_deg = 0.0f;   //rp3d's own reading - should match angle_deg
        float rate = 0.0f;              //rad/s
        int   kicks = 0;
        int   arrows = 0;
        float hz = 0.0f;
        float inertia = 0.0f;
        float stiffness = 0.0f;
        float gravity_stiffness = 0.0f;
        float damping = 0.0f;
    };
    std::vector<StrawView> strawmen;
    int   kick_score = 0;

    //Live arrows, so a miss can be diagnosed rather than guessed at.
    struct ArrowView{
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        bool  f_stuck = false;
    };
    std::vector<ArrowView> arrows;

    //Where the aim arc currently says an arrow would land, which is the single most useful number
    //for a program trying to hit something: it can solve for the angle by bisection instead of
    //shooting and looking.
    float predicted_x = 0.0f;
    float predicted_y = 0.0f;
    bool  f_predicted = false;

    //What the model is doing, which is a different question from what the archer is doing - see
    //Puppet.h. `wanted_rate` against `rate` is the foot-slide readout, and it is here rather than
    //only in the panel so it can be measured over a run instead of watched.
    int   clip = -1;
    //The second clip of the blend space and its weight, or -1 for a single clip.
    int   blend_clip = -1;
    float blend = 0.0f;
    float blend_phase_offset = 0.0f;
    float clip_rate = 1.0f;
    float clip_wanted_rate = 1.0f;
    bool  f_clip_placeholder = false;
    float model_yaw = 0.0f;
    float model_roll = 0.0f;    //only the rope ever gives her one
    //On the rope: how far the joint has opened, and how far her drawn hands are from the rope's end
    //of it. -1 off the rope. See BuildRopeAttachMarkers.
    float rope_joint_gap = -1.0f;
    float rope_hands_off = -1.0f;
    float leg_weight = 0.0f;            //the loose legs - see ArcherModel::ApplyLegChains
    float leg_lead_deg = 0.0f;
    float leg_swing_deg[2] = {};
    float leg_knee_deg[2] = {};
    float climb_pinned_time = -1.0f;    //the rope climb's playhead as pinned this tick, -1 unpinned
    float climb_lift_posed = 0.0f;      //and the lift the model was lowered by (last tick's pin)
    StageLanding landing_forecast;      //her next landing as forecast this tick
    //Stage::spring_on and launch_lift, and each spring plant's q and rate - pad units, leaf degrees.
    int   spring_on = -1;
    float launch_lift = 0.0f;
    float slide_accel = 0.0f;
    int   ramp_on = -1;
    float slope_deg = 0.0f;         //Stage::SlopeUnderFeetDeg
    float spring_cue = -1.0f;           //the timing cue, 0..1, or -1 while there is none
    float spring_boost = 0.0f;          //what a jump now would add, and this bounce's best
    float spring_boost_peak = 0.0f;
    int   stomp_ticks = 0;
    int   spring_air_ticks = -1;
    int   branch_on = -1;               //Stage::branch_on while standing, else -1
    float lean_deg = 0.0f;              //Stage::lean and its rate, degrees; + away from the camera
    float lean_rate_deg = 0.0f;
    float balance_danger = 0.0f;
    int   hang_branch = -1;             //the branch she hangs from or is climbing onto, else -1
    struct SpringPlantView{
        int   kind = 0;
        float q = 0.0f;
        float qd = 0.0f;
    };
    std::vector<SpringPlantView> spring_plants;
    float fall_weight = 0.0f;           //Puppet::fall_weight, the fall pose's share
    float air_hip_weight = 0.0f;        //how much the hips are held at standing height in the air
    float landing_forecast_us = 0.0f;   //and what the forecast cost
    //Each hand (left, right) on the drawn rope: how far down it, and how far off it. -1 off the rope.
    //A gripping hand's distance down stays put while she climbs; if it creeps, the hand is sliding.
    float rope_hand_s[2] = { -1.0f, -1.0f };
    float rope_hand_off[2] = { -1.0f, -1.0f };
    float rope_grip_s = -1.0f;      //where the joint holds the rope, down from the anchor
    //How many arrows are currently riding a prop rather than sitting in the world. Reported
    //because it is the one piece of this that is invisible until something moves: an arrow pinned
    //to the wrong prop, or to a prop it stopped being in, looks exactly like a correct one right
    //up until that prop is kicked.
    int   arrows_on_props = 0;
    int   sounds_playing = -1;          //audible voices; -1 with no sound system
    std::vector<SoundVoiceInfo> voices; //and what each one is - SoundSystem::ListVoices
    float sound_volume = 0.0f;
    int   anim_source = ANIM_FROM_GAME;
};

//A chunk of a broken wall, and when to reap it.
struct DebrisView{
    Object*  object = NULL;
    uint64_t reap_tick = 0;
};

//One prop, and the Object plus body that shows it. Kept so an arrow's raycast hit - which comes
//back as an rp3d body - can be turned into "that was target 2".
struct PropView{
    Object* object = NULL;
    int   kind = PROP_CRATE;
    int   index = -1;           //index into Stage::props, or -1 for a brick in a wall
    bool  f_knocked = false;
    //Half extents as built, handed to Stage every tick as the box that blocks the archer. An
    //approximation once a board has toppled, which is why a knocked prop is not offered as an
    //obstacle at all - you step over a fallen board rather than walking into it.
    vec3  half_extents = vec3(0.5f,0.5f,0.5f);
    //Fell out of the level and has been retired - see ReapFallenProps.
    bool  f_lost = false;
    /*
        Broken off a wall by a kick, and therefore RUBBLE rather than an obstacle.

        It still falls, still piles up, still collides with the level and with the other bricks -
        it simply stops blocking the ARCHER. That distinction is the whole difference between
        kicking a hole in a wall and building a second wall out of the first one: with broken
        bricks left as obstacles, the pile shoved the archer steadily backwards away from the hole
        they had just made, 51.19 -> 54.90 over four kicks, and the way through was never open.
    */
    bool  f_broken = false;
    //Targets only: how many arrows have gone into it, and whether it was built floating
    //(StageProp::f_floating) - which is what makes the range's gravity-after-3-hits rule apply.
    int   hits = 0;
    bool  f_floating = false;
    bool  f_gravity_restored = false;
    //The damping the body had before a floating target's was raised (FLOAT_DAMPING_MIN), kept so
    //it can be put back when gravity takes over.
    float base_linear_damping = 0.0f;
    float base_angular_damping = 0.0f;

    //--- Targets by variant - see TargetVariant ---
    int   variant = TARGET_BOARD;
    //What an arrow and a kick are divided by: 1 for everything but a stand. See STAND_HEFT.
    float heft = 1.0f;
    //How far above the object's ORIGIN the centre of half_extents is, along its own up. 0 for the
    //unit-cube props, whose origin is their centre; a stand's model stands on its origin.
    float obstacle_lift = 0.0f;
    //A stand's board, in the object's own frame at world scale: its centre, the axis running up
    //the face, and its radius - what a hit is scored against.
    vec3  board_centre = vec3(0.0f,0.0f,0.0f);
    vec3  board_up = vec3(0.0f,1.0f,0.0f);
    float board_radius = 0.0f;
    int   score = 0;                //points scored on this stand; kicks landed, on a straw man
    //A straw man's spring: owned here so it moves with prop_views when levels swap, and deleted
    //before the body it holds - see NewGame.
    SpringHinge* spring = NULL;
};

class ApplicationArcher : public Application{
public:
    ApplicationArcher();
    ~ApplicationArcher();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
    //Swaps the live level for the parked one. Physics thread, physics_mutex held - see core.
    void OnActiveSceneChanged(Scene* from, Scene* to) override;
    //Where an input recording starts, and putting her back there for its replay. Physics thread.
    json CaptureRecordingState() override;
    void RestoreRecordingState(const json& state) override;
    //Render thread, before the scene is drawn. Services f_regenerate_terrain.
    void PreRender(void) override;
    //Render thread. The title screen's text - the only 2D HUD this app has so far.
    void DrawOverlay(void) override;
    //Render thread, on a size change. Places the title's full-window click rect.
    void LayoutTouchButtons(int w, int h) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
    //Add Object > Objects From Assets: at her feet on the play plane, and an archer.glb prop at
    //model_scale - the scale everything from that file is drawn at. See Application::PlaceMenuSpawn.
    void PlaceMenuSpawn(SimCommand& cmd) override;
    //archer.glb's props, registered as assets by name so the menu can place them. See Init.
    void RegisterPlaceableProps();
#endif
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    //The rules. Touched ONLY from the physics thread (RunSimulationTick and the command handlers,
    //which also run there) and from DrawImGuiUI, which holds physics_mutex. Everything else goes
    //through `snapshot`.
    Stage stage;
    //Her next landing as of this tick, and what forecasting it cost - see Stage::PredictLanding.
    StageLanding landing_forecast;
    float landing_forecast_us = 0.0f;

    vec3 camera_target = vec3(0.0f,3.0f,0.0f);
    ArcherCameraTuning camera_tuning;

private:
    //--- Setup, all on the render thread from Init() ---------------------------------------------
    void BuildMaterials();
    void BuildBlocks();
    /*
        The marching-cubes terrain for the test bay, one Object per bay - see
        apps/archer/terrain_plan.md and apps/archer/Terrain.h.

        Runs AFTER BuildBlocks, because it hides the block objects it has replaced rather than
        stopping them from being built. Hiding rather than skipping keeps block_objects indexed in
        step with Stage::blocks, which BreakBlocks and NewGame both rely on, keeps every collider
        exactly where it was, and means Show()ing them again is a complete debug view of the
        blockout underneath the terrain - which is the only way to see whether the surface is
        sitting where the collider says it is.
    */
    void BuildTerrain();
    //Remeshes one bay from `stage` as it stands, reusing its Object and Mesh. Render thread.
    void RemeshTerrainBay(int bay);
    //The bank behind that bay (Backdrop.h), the same way. Render thread, from RemeshTerrainBay.
    void RemeshBackdrop(int bay);
    //Reads the blocks back off their (editor-moved) objects into `stage`, keeps that layout for
    //restarts, and remeshes every bay. Render thread; takes physics_mutex. See the definition.
    void RegenerateTerrain();
    //Recomputes which blocks the terrain covers and applies f_show_blockout to them. NO GL, so
    //unlike BuildTerrain this is safe from NewGame on the physics thread. See the definition.
    void ApplyBlockoutVisibility();
    //Show or hide the blockout boxes the terrain replaced. See the definition.
    void SetBlockoutVisible(bool f_visible);
    //Loads the crate out of archer.glb into crate_mesh, falling back to the box. Render thread;
    //before BuildProps, which NewGame also calls from the physics thread - so the upload is here.
    void BuildCrateMesh();
    //Loads archery_target for TARGET_STAND. Render thread, beside BuildCrateMesh and for its reason.
    void BuildStandMesh();
    //One TARGET_STAND: the model, its cylinder-and-legs body, and the PropView that tracks it.
    //Falls back to a plain board when the model is missing. No GL - NewGame calls it too.
    PropView MakeTargetStand(const StageProp& p, int index);
    //Loads straw_man for PROP_STRAWMAN. Render thread, beside BuildCrateMesh and for its reason.
    void BuildStrawManMesh();
    //One PROP_STRAWMAN: the model, its body, its SpringHinge. Nothing built if the model is missing.
    PropView MakeStrawMan(const StageProp& p, int index);
    //Every straw man's spring torque, once per tick before the step.
    void TickSprings();
    //Deletes every straw man's spring. Before the bodies go - NewGame.
    void DestroySprings();
    void BuildProps();
    void BuildArcher();
    //Loads meshes/archer.glb: the skin, the skinned mesh and every clip in Puppet.h's table.
    //Survivable if it fails - the app falls back to the coloured box it has always drawn.
    void BuildArcherModel();
    /*
        Puts the bow in her left hand and the nocked arrow in her right - see apps/archer/Bow.h and
        apps/archer/bow_plan.md.

        AFTER BuildArcherModel, because it needs the skeleton posed at a clip to work out the grip,
        and both the bones and the clips come from there. Survivable if it fails: the game plays,
        she just has empty hands.
    */
    void BuildBow();
    /*
        The ferns and flowers on the blockout - see apps/archer/Foliage.h for where they go.

        BuildFoliage loads the three meshes out of archer.glb (render thread, GL) and scatters
        once. ScatterFoliageObjects is the part that runs again: it re-places a pool of Objects
        from `stage` as it stands, with no GL and nothing removed from the scene, so it is safe
        from RegenerateTerrain and PreRender under the lock. Main level only - the range grows
        nothing.

        AFTER BuildArcherModel, because the plants take the character's scale: the file is
        authored with everything to scale with her, so one factor keeps them in proportion.
    */
    void BuildFoliage();
    /*
        The wind (wind_plan.md). RENDER THREAD, every frame, from PreRender: copies the blocks and
        the tick at a tick boundary, rebuilds the field if the blocks changed (a hash check - free
        when they have not, ~145 ms on the main level when they have), bakes the part the camera
        sees into the grid the renderer bends plants with, and redraws the debug view if it is on.
    */
    void UpdateWind();
    void ScatterFoliageObjects();
    /*
        The decorative vines - see apps/archer/Vine.h and vine_plan.md. Built ONCE, render thread
        (each trunk is a generated Mesh, and SetMeshData uploads), and never touched again: they
        are static, and nothing in a running game moves them. Takes `vine_trunk`, `vine_leaf_1`
        and `vine_leaf_2` out of archer.glb where they exist and falls back per piece to the
        placeholders in Vine.cpp, so it works before the asset does. `vine_curl`, the wrap, has no
        placeholder and is only laid over the file's own trunk. After BuildFoliage, for the
        same reason it comes after BuildArcherModel: the character's scale.
    */
    void BuildVines();
    /*
        The level's signs (StageSign), into main_scene - see apps/archer/Sign.h for how a model's
        text finds its boards. Built once per level, like the vines: they are static, and
        NewGame's rebuild leaves them alone. The models load on the first call. RENDER THREAD,
        and after BuildHitPopups, whose glyph set the text is baked from.
    */
    void BuildSigns();
    SignModel sign_models[SIGN_VARIANT_COUNT];
    bool f_sign_models_loaded = false;
    int material_sign_text = 0;
    //The level's authored scenery (StageScenery), the same way: once per level, models loaded on
    //the first call, each fitted to its walkable surface at the character's scale. RENDER THREAD.
    void BuildScenery();
    Mesh* scenery_meshes[SCENERY_VARIANT_COUNT] = {};
    std::vector<Material> scenery_materials[SCENERY_VARIANT_COUNT];
    //Each model's flat top at scale 1, measured by MeasureWalkableTop: its centre, height, width.
    float scenery_top_x[SCENERY_VARIANT_COUNT] = {};
    float scenery_top_y[SCENERY_VARIANT_COUNT] = {};
    float scenery_top_w[SCENERY_VARIANT_COUNT] = {};
    bool f_scenery_loaded = false;
    //Reads each clip's own root track for how far it travels and how long it lasts, and hands the
    //answers to the Puppet. See the note on the definition - this is the number that decides
    //whether the feet slide, and it is measured rather than declared.
    void MeasureClips();
    //Where in each locomotion clip's cycle the left foot is planted, found by posing the model
    //through the clip and watching the toe. What the blend space needs to line two cycles up.
    void MeasureClipPhases();
    //Where the jump clip's anticipation bottoms out and where it peaks, read off the hip's height.
    //The rise plays the span between them and skips the crouch in front of it.
    /*
        The two numbers the air set needs that are not in the clip table.

        Where each landing clip's feet reach the floor, read off the toe - a landing is entered
        there rather than at its first frame, which is still falling (see Puppet::clip_entry) - and
        how long the running jump spends climbing, read off the hip, which is what it gets fitted
        to (see Puppet::run_jump_rise).
    */
    void MeasureAirClips();
    //When the boot connects in Kick_Front, found by watching which foot reaches furthest from the
    //hips - for each of the three kicks. Checked against its KICK_SPECS row rather than setting it,
    //and logs where the boot is at the strike beside the row's box - see Puppet::kick_strike.
    void MeasureKickClip();
    //The kneel set against the rules: both transitions' lengths against KNEEL_DOWN/UP_TICKS, and
    //her kneeling height against KNEEL_HALF_H. Warns with the number to type, like the kick.
    void MeasureKneelClips();
    /*
        Rope_Climbing's hip rise at every keyframe, in world units from its first, made monotonic
        and handed to the Puppet - which pins the climb's playhead to the distance climbed through
        it. See Puppet::climb_rise for why distance rather than time.
    */
    void MeasureRopeClimb();
    //Climb's hip path from the grab against LEDGE_CLIMB_UP/ACROSS and LEDGE_CLIMB_INSET, which
    //Stage carries her along. Prints the table to type if the export has moved away from it.
    void MeasureLedgeClimb();
    //Where Hanging_Braced's fingers rest against the lip, as LEDGE_HANG_DROP puts her. Prints the
    //drop to type if they float above the stone or sink into it.
    void MeasureLedgeHang();
    void BuildArrowViews();
    /*
        The arrow in her hand, re-baked for flight: along +X with the TIP AT THE ORIGIN, at the
        size it is drawn in her hand. NULL if the bow did not load, and the arrows stay boxes.
        Render thread - it uploads a mesh. See the definition for how the tip is found.
    */
    Mesh* BuildFlightArrowMesh();
    void BuildAimArc();
    //The backdrop quad. Survivable if the image is missing - see the note on the definition.
    void BuildBackground();
    void SetupLights();
    void SetupCamera();
    void SetupInput();
    void RegisterCommandHandlers();
#ifdef USE_MCP
    void RegisterMCPTools();
#endif

    //Builds one dynamic box body, pinned to the play plane. Every prop goes through here, which is
    //what guarantees none of them can drift out of z = 0 - see the axis-lock note in
    //core/physics/Physics.h for what happens when one does. `parent`, if given, must be an
    //identity-transform grouping object (blockout_group); NULL makes it a scene root.
    Object* MakePlanarBody(Mesh* mesh, const char* name, const vec3& position, const vec3& size,
                           int material, uint32_t category, uint32_t collide_mask,
                           float mass, bool f_static, Object* parent = NULL);

    //--- Per tick, physics thread -----------------------------------------------------------------
    void GatherInput(ArcherInput& out);
    void HandleEvents(const StageEvents& events);
    /*
        The bow's sounds, once per tick after the rules have run - see the definition. The loose
        and the level hits come off `events`; the creak off the nock's EDGE, which is state rather
        than an event, and so is the one sound this has to remember something to play.
    */
    void UpdateSound(const StageEvents& events);
    /*
        EVERY sound the game plays or stops goes through these two, which write the decision to
        cue_log first. That log is the baseline the cue layer is proved against (cue_plan.md, step
        1): the same recording replayed before and after the move must print the same lines.
        `cue` names the reaction, `sound` the file's name; `gain` is the cue's own, and
        sound_volume is applied here, after the log, because the master volume is not the cue's
        decision. Logged whether or not a sound system exists to play it.
    */
    soundhandle_t PlayCue(const char* cue, const char* sound, float gain, float from = 0.0f);
    void StopCue(const char* cue, const char* sound, soundhandle_t handle);
    CueLog cue_log;
    //One arrow strike, at `point` and `speed`. Level hits come through UpdateSound, prop hits
    //from ResolveArrowsAgainstProps, which is the only place those are found.
    void PlayArrowHit(float x, float speed);
    //How loud an arrow sound is, by its speed and by how far from her it happens - see PlayArrowHit.
    //The cue's gain: sound_volume is not in it, PlayCue applies that.
    float ArrowSoundGain(float x, float speed);
    /*
        THE INCOMING SWOOSH, timed so its loudest moment lands on the impact: for every arrow in
        flight, Stage::PredictArrowImpact says when it strikes a block, a raycast along the path it
        hands back says whether a prop comes first, and once that is within the swoosh's lead the
        sound starts - partway in, if the impact is nearer than the whole lead, so the peak still
        lands on it. Once per flight. The hit itself stays on the real strike (PlayArrowHit), so a
        forecast a moving prop proves wrong costs a near miss, never an early thud.
    */
    void StartArrowSwooshes();
    float arrow_swoosh_peak = -1.0f;            //seconds into arrow_swoosh.wav it is loudest; -1 unloaded
    bool  arrow_swooshed[ARROW_MAX_LIVE] = {};  //this flight's swoosh has been started
    std::vector<v2> arrow_path;                 //scratch for the forecast's path
    //Loads the three wavs. Survivable: a missing file leaves that one sound silent.
    void SetupSound();
    //The other half of the arrow hit test - the half that knows about rigid bodies. See the
    //handshake note on Stage::arrows.
    void ResolveArrowsAgainstProps();
    void DriveArcherBody();
    //Colour the archer by what they are doing. Stands in for the animation that will say it later.
    void SyncArcherView();
    //Fill the animation parameters, ask the Puppet what to play, and carry the answer out on the
    //model. The whole of step 0 runs through here.
    void SyncArcherAnimation();
    //The bow's BEND only - its position comes from the hand bone it hangs off. See the definition.
    void SyncBow();
    //Warns when the hand reaches the string on a different tick from BOW_NOCK_TICKS.
    void CheckNockTicks();
    //Hand Stage every live prop as a box, BEFORE the tick. See the note on the definition.
    void RefreshObstacles();
    //Shove whatever Stage says was leaned on, AFTER it.
    void ApplyPushes(const StageEvents& events);
    //And boot whatever Stage says was kicked - far harder, and it frees a brick wall to collapse.
    void ApplyKicks(const StageEvents& events);
    //Take a broken block's collider out of the world and burst it into chunks.
    void BreakBlocks(const StageEvents& events);
    void SpawnDebris(const vec3& centre, const vec3& half_extents, const vec3& impulse_dir, int material);
    void UpdateDebris();

    //--- The rope ---------------------------------------------------------------------------------
    void BuildRope(const StageProp& anchor);
    void DestroyRope();
    /*
        What is DRAWN over the chain: one skinned mesh (RopeMesh.h) with a Bone per link. BUILT
        ONCE PER LEVEL on the render thread, after that level's BuildProps - never from BuildRope,
        which NewGame runs on the physics thread, where there is no GL. The chain under it is
        rebuilt on every restart and the skin simply carries on, because BuildRope always lays it
        in the same straight bind pose. The link boxes are hidden while a skin exists.
    */
    void BuildRopeSkin();
    //The skin's input for this level's rope, without the cuts. False on a level with no rope.
    bool MakeRopeMeshInput(RopeMeshInput& in);
    /*
        A CUT CHANGES THE SKIN'S WEIGHTS, so each piece moves with its own links and nothing
        stretches across the gap (RopeMeshInput::cuts). The tick compares the chain's cut joints
        with the ones the skin was built for and raises f_rope_skin_stale; PreRender rebuilds and
        re-uploads the mesh - GL, so render thread, inside AtTickBoundary since it reads the chain.
        A restart puts the joints back, and the same comparison rebuilds it whole.
    */
    void CheckRopeSkinCuts();
    void RebuildRopeSkinWeights();
    //The joints of the chain that are cut, in order.
    std::vector<int> RopeCutJoints() const;
    //Loads rope_segment/ring/collar/tassel out of archer.glb, once. Render thread.
    void LoadRopeParts();
    //Copies each link's transform onto its bone. Every tick, physics thread.
    void UpdateRopeSkin();
    void ApplyRopeLinkVisibility();
    /*
        A debug view of where the rope holds her, because rp3d's debug renderer draws colliders
        and contacts but not joint anchors. Three beads while MODE_ROPE: RED the rope's end of the
        joint (the link's centre), BLUE her end (the top of her box), YELLOW where the MODEL's hands
        actually are. Red and blue apart is the joint stretching; yellow away from red is the
        animation not holding the rope where the physics does. Both distances are also measured
        every tick and published in archer_state, so they can be read rather than squinted at.
    */
    void BuildRopeAttachMarkers();
    void UpdateRopeAttachMarkers();
    /*
        THE ROPE TEST BENCH - a debug tool, not a rule. Every tick: how far each joint of the chain
        has opened, the rope's loaded length against its rest length, and the worst gap now and over
        the last ROPE_PEAK_TICKS. Knobs: the scene's solver iterations, the links' mass (applied by
        rebuilding the level), and cutting a joint on command. Over MCP as `rope_test`, and in the
        panel's Rope section. Built to find the stretch in animation_plan.md "Climbing the rope".
    */
    void MeasureRopeStretch();
    bool CutRopeJoint(int joint);
    void SetRopeSolverIterations(int velocity, int position);
    //Hand Stage the links it may catch, BEFORE the tick, the same way the props are handed over.
    void RefreshRopePoints();
    //The handoff, both ways. See the note on AttachArcherToRope.
    void AttachArcherToRope(int segment);
    /*
        A distance down the rope as a link and a point on it in the link's own frame. False with no
        rope. Past either end it clamps onto the end link.
    */
    bool RopeDistanceToLink(float s, int& link, vec3& local) const;
    //How many links from the top still hang from the anchor - all of them unless it was cut.
    int  RopeAnchoredLinks() const;
    //Makes the joint at stage.rope_s: destroys the old one and creates it again with EXPLICIT LOCAL
    //anchors, because rp3d cannot move an anchor after creation. Called when climbing moves the grip.
    void ReanchorRopeJoint();
    void DetachArcherFromRope(bool f_jump);
    //Read the swinging body back into Stage, so the rules, the camera and the telemetry all know
    //where the archer is while the solver is the one moving them.
    void SyncArcherFromRope();
    void PumpRope(float move_axis);
    void SyncArrowViews();
    //Each spring plant's moving box, to where its spring is this tick.
    void SyncSpringPlants();
    //The balance gauge beside her head, while she is on a branch.
    void SyncBalanceGauge();
    void SyncAimArc();
    //Cuts the aim arc short at the first PROP it would hit - the half of "what will this arrow
    //hit" that Stage cannot answer. See the note on the definition.
    int  TruncateArcAgainstProps(v2* points, int count);
    void UpdateCamera();
    //Puts the camera where camera_target and camera_distance say, and drags the backdrop
    //and the sun along with it. Called from the tick AND from UpdateView, which is why it is
    //its own function - a zoom has to show while the simulation is paused. In ARCHER_CAM_ORBIT it
    //leaves the camera alone and moves only the backdrop and the sun.
    void PlaceCamera();
    //Middle-drag orbits round camera_target, shift+middle pans, the wheel dollies. UpdateView,
    //ARCHER_CAM_ORBIT only.
    void UpdateOrbitCamera(int dx, int dy, int wheel);
    void UpdateTargets();
    //Retires props that have been knocked out of the level, so they stop falling forever.
    void ReapFallenProps();
    void PublishSnapshot();
    void NewGame();
    //Teleport, clearing the movement state with it - ARCHER_CMD_PLACE and a replay's restore.
    void PlaceArcher(v2 pos);
#ifdef USE_MCP
    json BuildStateJson();
    //Blocks until `ticks` more simulation ticks have run, or the timeout expires. Every tool that
    //acts rather than observes ends in one of these, so a caller never has to sleep and guess.
    void WaitTicks(int ticks);
#endif

    //--- The hit counter -------------------------------------------------------------------------
    //Loads the glyphs, bakes the numbers and makes the popup pool. Render thread, from Init.
    void BuildHitPopups();
    //A target has just taken an arrow at `point`: count it, show the count, and apply the range's
    //gravity rule. Physics thread, from ResolveArrowsAgainstProps.
    void RegisterTargetHit(PropView& view, const vec3& point);
    void SpawnHitPopup(const vec3& at, int count);
    //Rise, grow in, shrink out, retire. Physics thread, every tick.
    void UpdateHitPopups();
    //Takes every popup down - on a scene switch, whose tick clock is not the one they were timed by.
    void ClearHitPopups();
    GlyphSet hit_glyphs;            //the app's one glyph set - the signs are baked from it too
    //Index is the count; [0] is unused. Held by the AssetManager as ar_hit_<n>.
    Mesh* hit_number_meshes[HIT_NUMBERS_MAX + 1] = {};
    struct HitPopup{
        Object*  object = NULL;
        uint64_t spawn_tick = 0;
        vec3     origin;
        bool     f_active = false;
    };
    HitPopup hit_popups[HIT_POPUP_SLOTS];
    int next_hit_popup = 0;
    int material_hit_text = 0;

    //--- Meshes and materials ---------------------------------------------------------------------
    Mesh* unit_mesh = NULL;         //a 1x1x1 box, scaled per block
    //What a crate is drawn with: archer.glb's wooden_crate re-baked into unit_mesh's shape, or
    //unit_mesh itself if the export lacks it. See BuildCrateMesh.
    Mesh* crate_mesh = NULL;
    std::vector<Material> crate_materials;
    bool f_crate_from_asset = false;
    //archer.glb's archery_target, as it comes from the file - drawn at model_scale. NULL if the
    //export lacks it, and a stand is then built as a plain board.
    Mesh* stand_mesh = NULL;
    std::vector<Material> stand_materials;
    //Points scored on every stand this level, and on the last hit - for the panel and archer_state.
    int archery_score = 0;
    int archery_last_points = -1;
    //archer.glb's straw_man, drawn at model_scale; NULL if the export lacks it.
    Mesh* straw_mesh = NULL;
    std::vector<Material> straw_materials;
    //Kicks landed on every straw man this level - the panel and archer_state.
    int kick_score = 0;
    //The straw men's swing, live - the panel's sliders. See STRAW_HZ.
    float straw_hz = STRAW_HZ;
    float straw_damping_ratio = STRAW_DAMPING_RATIO;
    float straw_kick_punch = STRAW_KICK_PUNCH;
    Mesh* arrow_mesh = NULL;
    Mesh* dot_mesh = NULL;          //the aim arc's beads

    int material_ground = 0;
    int material_ledge = 0;
    int material_platform = 0;
    int material_breakable = 0;
    int material_trunk = 0;
    int material_spring_pad = 0;
    int material_leaf = 0;
    int material_ramp = 0;          //the slide gallery's ramps - see Stage::BuildSlideGallery
    //The timing cue's ramp, green (a jump now adds nothing) through yellow to red (this bounce's
    //best). See spring_cue.
    static const int SPRING_CUE_STEPS = 9;
    int material_spring_cue[SPRING_CUE_STEPS] = {};
    int material_archer = 0;
    //The character model's own colour - see the note where it is assigned.
    int material_archer_skin = 0;
    //A second archer colour for MODE_HANG / MODE_CLIMB. With no animation yet, the colour IS the
    //state readout - it is what makes "is he hanging or is he stuck in the wall" answerable from a
    //screenshot, which is how this app gets checked over MCP.
    int material_archer_hang = 0;
    int material_archer_slide = 0;  //her tint while she slides - there is no slide clip yet
    int archer_model_material = -1; //the model's own slot 0, put back when the slide tint comes off
    int material_crate = 0;
    int material_target = 0;
    int material_target_hit = 0;
    int material_arrow = 0;
    int material_debris = 0;
    int material_dot = 0;
    int material_dot_hot = 0;
    //The terrain's three, in the order Terrain.cpp writes matid: 0 grass, 1 soil, 2 rock. An
    //Object has four slots (NUM_MATERIAL_SLOTS) and this uses three of them, which is the whole
    //reason a per-vertex classification is enough and no texture is needed.
    int material_grass = 0;
    int material_soil = 0;
    int material_rock = 0;
    //The same three for the bank behind the terrain, hazed toward the backdrop - see BACKDROP_HAZE.
    int material_grass_back = 0;
    int material_soil_back = 0;
    int material_rock_back = 0;
    //The placeholder vine's two, used only for a piece archer.glb does not have yet.
    int material_vine = 0;
    int material_vine_leaf = 0;

    //--- The scene --------------------------------------------------------------------------------
    /*
        THE ARCHER IS TWO OBJECTS, and keeping them apart is deliberate.

        `archer_object` is the BODY: the box collider, the kinematic rigid body, the thing the rope
        joint attaches to and the thing an arrow's raycast has to exclude. It is exactly the box
        Stage sweeps, which is why it stays useful as a debug draw even once there is a character
        to look at (f_show_collider) - a model that has drifted out of its own collider is a bug
        you can only see by drawing both.

        `archer_model` is the LOOK: the skinned mesh and its 65 bones, placed every tick from
        Stage's position and posed by the Puppet. It has no physics, no collider and no opinion -
        attaching it as a child of the body was the obvious alternative and was not done, because
        on the rope the body becomes DYNAMIC and the model would inherit a solver's idea of an
        orientation for a character who should stay side-on.
    */
    Object* archer_object = NULL;
    ArcherModel* archer_model = NULL;
    //Every clip in Puppet.h's table, in that order. NULL for one the export did not contain,
    //which BuildArcherModel reports and everything downstream checks for.
    Animation* archer_clips[CLIP_COUNT] = {};
    //The sun, kept because UpdateCamera drags it along with the view every tick - the level is 84
    //units wide and one shadow ortho cannot cover that, so the light follows the camera.
    DirectionalLight* sun_light = NULL;
    DirectionalLight* fill_light = NULL;        //held only so the range scene can share it
    std::vector<Object*> block_objects;         //parallel to Stage::blocks
    //The plants' boxes - trunks, stalks, caps, leaves - which are not blocks, so a restart destroys
    //them from here. The moving part of each spring plant is also in spring_plant_objects, parallel
    //to Stage::spring_plants, for SyncSpringPlants to place.
    std::vector<Object*> plant_objects;
    std::vector<Object*> spring_plant_objects;
    /*
        THE TIMING CUE, worked out each tick: which spring plant to tint (-1 none) and how well
        timed a jump pressed now would be, 0..1 - Stage::SpringBoostNow over this bounce's best,
        PredictSpringBoostPeak. The colour a frame shows is what a press on the NEXT tick gets.
    */
    //The balance gauge's two boxes, made with the branches - see SyncBalanceGauge. In plant_objects.
    Object* balance_bar = NULL;
    Object* balance_marker = NULL;
    int   spring_cue_plant = -1;
    float spring_cue = 0.0f;
    float spring_cue_peak = 0.0f;
    //The scene-tree parent of every block object, per level. Identity transform, no mesh, and it
    //must stay that way - see BuildBlocks.
    Object* blockout_group = NULL;
    std::vector<PropView> prop_views;
    std::vector<DebrisView> debris;

    //--- The terrain ------------------------------------------------------------------------------
    //One Object per test bay, so each can be hidden on its own and object_list names them
    //separately over MCP. Empty when ARCHER_TEST_BAY is off.
    std::vector<Object*> terrain_objects;
    //And the bank behind each bay (Backdrop.h), one Object per bay in the same order - hidden for a
    //bay with no ground, which is the upper one.
    std::vector<Object*> terrain_back_objects;
    //The finished bank's worst reach forward, over the ground's grass: how far in front of the
    //slab's back face its surface came, measured on the mesh. At or under 0 is the promise
    //BackdropParams::front_gap makes. Per bay, for the log and archer_state.
    std::vector<float> backdrop_reach;
    /*
        Asks the render thread to run RegenerateTerrain on its next frame.

        A flag rather than a call because the remesh is GL, and neither caller may do GL: the
        panel button fires in the middle of ImGui's frame with physics_mutex held (RegenerateTerrain
        takes it itself), and the MCP tool runs on the server thread. PreRender is the one place
        that is both render thread and outside the lock.
    */
    std::atomic<bool> f_regenerate_terrain{false};
    //Bumped at the end of every RegenerateTerrain, which is how the MCP tool knows it has run.
    //The two counts are written before the bump and so are safe to read once it is seen.
    std::atomic<int> terrain_generation{0};
    int last_regen_moved = 0;
    int last_regen_hidden = 0;

    //--- The foliage ------------------------------------------------------------------------------
    //One mesh per FoliageKind, shared by every plant of that kind - the renderer batches Objects
    //by mesh, so a few hundred plants are three draws. NULL for a node the export lacks.
    Mesh* foliage_meshes[FOLIAGE_KIND_COUNT] = {};
    std::vector<Material> foliage_materials[FOLIAGE_KIND_COUNT];
    //Radius in the ground plane and height of each mesh at scale 1, measured off its vertices.
    float foliage_mesh_radius[FOLIAGE_KIND_COUNT] = {};
    float foliage_mesh_height[FOLIAGE_KIND_COUNT] = {};
    //The scene-tree parent, identity like blockout_group.
    Object* foliage_group = NULL;
    //The pool. Grown, never shrunk: a rescatter re-places these and hides whatever is spare, so
    //nothing is ever deleted out from under the render thread. Children of foliage_group.
    std::vector<Object*> foliage_objects;
    FoliageParams foliage_params;
    //How big a plant is drawn: the character's own scale unless the panel says otherwise.
    float foliage_scale = 1.0f;
    int   foliage_counts[FOLIAGE_KIND_COUNT] = {};
    //Asks PreRender to rescatter - the panel's sliders raise it on release, not every frame.
    std::atomic<bool> f_rescatter_foliage{false};

    //--- The rocks (Boulders.h) ------------------------------------------------------------------
    //Laid out exactly like the foliage: a mesh per kind, measured at load, and a grown-never-shrunk
    //pool under one group, re-placed wherever the foliage is. Main level only, like the foliage.
    Mesh* boulder_meshes[BOULDER_KIND_COUNT] = {};
    std::vector<Material> boulder_materials[BOULDER_KIND_COUNT];
    float boulder_mesh_radius[BOULDER_KIND_COUNT] = {};
    float boulder_mesh_height[BOULDER_KIND_COUNT] = {};
    Object* boulder_group = NULL;
    std::vector<Object*> boulder_objects;
    BoulderParams boulder_params;
    int   boulder_counts[BOULDER_KIND_COUNT] = {};
    //Loads rock_big / rock_small and makes the group. Render thread, Init.
    void BuildBoulders();
    //Re-places the pool from the blocks as they are. No GL - see ScatterFoliageObjects.
    void ScatterBoulderObjects();

    //--- The pines on the back wall --------------------------------------------------------------
    //Placed by BuildBackdropBlocks, drawn as CHILDREN of their bay's terrain_back object - so they go
    //with the level on a scene swap without a list of their own - reused in order on a remesh.
    Mesh* pine_mesh = NULL;
    std::vector<Material> pine_materials;
    void PlaceBackdropPines(Object* wall, const std::vector<BackdropTree>& trees);
    //Every bay's pines again, without remeshing - for once model_scale is known. See Init.
    void PlaceAllBackdropPines();

    //--- The vines --------------------------------------------------------------------------------
    //One Object per trunk, its own generated mesh in world coordinates, and the leaves as Objects
    //sharing one mesh per kind like the ferns. All children of vine_group; built once, never moved.
    Object* vine_group = NULL;
    std::vector<Object*> vine_trunks;
    std::vector<Object*> vine_wraps;            //vine_curl laid over each trunk, if the file has it
    std::vector<Object*> vine_leaves;
    VineParams vine_params;
    int vine_leaf_counts[VINE_LEAF_KIND_COUNT] = {};
    //Which pieces came from archer.glb rather than Vine.cpp's placeholders, for the panel.
    bool f_vine_trunk_from_asset = false;
    bool f_vine_wrap_from_asset = false;
    bool f_vine_leaf_from_asset[VINE_LEAF_KIND_COUNT] = {};
    //Which block objects BuildTerrain hid, so the debug view can put them back without having to
    //work out again which ones melted. Indices into block_objects.
    std::vector<int> melted_blocks;
    //The debug view: the blockout boxes underneath the terrain. Off by default; F2 toggles it, and
    //so does the terrain_blockout MCP tool.
    bool f_show_blockout = false;
    /*
        Whether a level starts with her getting up (GETTING UP in Stage.h). OFF by default: it
        locks the controls for 3.5 s after every start and restart, which is the right entrance
        and the wrong thing to sit through fifty times while testing. The panel's checkbox turns it
        on; it takes effect at the next restart. Written by the panel with physics_mutex held and
        read by NewGame on the physics thread under it, so a plain bool is enough.
    */
    bool f_level_entry_getup = false;

    //--- Sound ------------------------------------------------------------------------------------
    //NULL in a USE_SOUND=0 build, and every caller copes - see SetupSound.
    SoundSystem* soundsystem = NULL;
    //The creak that is playing, so a loose or a cancel can cut it. 0 when there is none.
    soundhandle_t snd_bow_tension = SOUND_INVALID_HANDLE;
    //Last tick's Stage::IsNocked, for the edge the creak starts on.
    bool f_was_nocked = false;
    //Master gain for the lot, 0..1, on the panel.
    float sound_volume = 0.8f;
    /*
        Which tick of the kick (Stage::kick_ticks, 1..KICK_TICKS) the swing's whoosh plays on. On
        the panel, because the clip has no events and this is found by ear. 11 is where the swing
        sat inside the old combined kick.wav as it was tuned by ear, so the timing carried over.

        TUNED ON KICK_FRONT, and so are the shout's ticks below: the other two kicks play both
        shifted by how much later their boot lands (UpdateSound), so each sound keeps its place
        against the strike rather than against the key press.
    */
    int   kick_swing_tick = 11;
    /*
        Her shout on a kick - some kicks, not all, and not always on the same tick, because the
        same yell on the same frame every time is the thing that makes a sound effect read as one.
        Chosen on the kick's first tick; see UpdateSound. On the panel, found by ear like the swing.
        The defaults put the loud part of kick_hyaa.wav, about 7 ticks in, around the strike.

        ONE OF THREE SHOUTS, drawn with the chance. from/to were found by ear on kick_hyaa, so the
        other two are moved by how much earlier or later their own loud part comes
        (SoundSystem::LoudestAt, measured at load) and every shout peaks on the same tick.
    */
    float kick_shout_chance = 0.4f;
    int   kick_shout_from = 10;     //earliest kick tick it can start on
    int   kick_shout_to = 16;       //latest
    int   kick_shout_tick = 0;      //this kick's, or 0 when this one is quiet
    int   kick_shout = 0;           //which of KICK_SHOUTS this kick's is
    static constexpr int KICK_SHOUT_COUNT = 3;
    float kick_shout_peak[KICK_SHOUT_COUNT] = {};   //seconds into each file its loud part is

    //--- The rope ---------------------------------------------------------------------------------
    std::vector<Object*> rope_segments;         //top link first
    //The joint holding the archer to a link while MODE_ROPE, and NULL the rest of the time. Held
    //because it has to be destroyed again - a swing that cannot be let go of is not a swing.
    rp3d::BallAndSocketJoint* rope_joint = NULL;
    std::vector<rp3d::BallAndSocketJoint*> rope_joints;   //the links to each other, and to the anchor
    Object* rope_anchor_object = NULL;
    //The drawn rope, this level's: the Skeleton owns the skinned mesh, bone i follows
    //rope_segments[i + 1] (index 0 there is the fixed anchor body). NULL on a level with no rope.
    Skeleton* rope_skin = NULL;
    std::vector<Bone*> rope_bones;
    std::vector<int> rope_skin_cuts;            //the cut joints the skin's weights were built for
    std::atomic<bool> f_rope_skin_stale{false}; //raised by the tick, cleared by PreRender
    //The pieces, shared by every level's rope, in world units (the file's scale, the node's own
    //scale and ROPE_MESH_SCALE folded in). An empty tile means the placeholder was used.
    enum{ ROPE_PART_SEGMENT = 0, ROPE_PART_RING, ROPE_PART_COLLAR, ROPE_PART_TASSEL, ROPE_PART_COUNT };
    bool f_rope_parts_loaded = false;
    std::vector<vertex> rope_parts[ROPE_PART_COUNT];
    float rope_part_scale[ROPE_PART_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool  f_rope_part_from_asset[ROPE_PART_COUNT] = {};
    std::vector<Material> rope_materials;       //one list for the whole skin, matids remapped to it
    //Debug: draw the rp3d links as well (they are hidden under a skin). Panel checkbox.
    bool  f_show_rope_links = false;
    //The attachment beads - see BuildRopeAttachMarkers. Shared by every scene, like the aim arc.
    enum{ ROPE_MARK_LINK = 0, ROPE_MARK_BODY, ROPE_MARK_HAND_L, ROPE_MARK_HAND_R, ROPE_MARK_COUNT };
    Object* rope_marks[ROPE_MARK_COUNT] = {};
    Bone*   hand_bones[2] = {};             //left, right; found on the model once
    bool    f_show_rope_attach = false;

    //The wind field, its tuning and its debug view - see UpdateWind. Built and retuned on the
    //render thread; wind_mutex covers it against the archer_wind MCP handler, which samples it
    //from its own thread.
    WindField  wind;
    WindParams wind_params;
    WindView   wind_view;
    std::mutex wind_mutex;
    std::atomic<bool> f_show_wind{false};
    float      wind_view_ms = 0.0f;         //render-thread cost of the last debug view redraw
    //The grid handed to Renderer::SetWindField every frame - the field over what the camera
    //sees, WIND_GRID_STEP apart - and what baking it cost.
    std::vector<float> wind_grid;
    int        wind_grid_w = 0, wind_grid_h = 0;
    float      wind_bake_ms = 0.0f;
    /*
        How far the wind bends each plant, per FoliageKind - material_t::wind_flex, in world units
        per (unit/s * unit^2 of height). Tall plants need far less: the bend grows with the square
        of the height, so a fern three times the grass's height at the grass's flex would lie flat.
        Each kind draws with its OWN copy of its material (renamed "<name>@<node>") so the atlas
        it shares with the terrain tiles and props does not sway along with it.
    */
    float      foliage_wind_flex[FOLIAGE_KIND_COUNT] = { 0.035f, 0.20f, 0.06f, 0.30f, 0.30f };
    //Writes foliage_wind_flex into the renderer's copies of the plant materials. Render thread.
    void ApplyFoliageWindFlex();
    //Every renderer material whose name ends in `suffix` - the "@<node>" copies - gets `flex`.
    void SetMaterialWindFlex(const std::string& suffix, float flex);
    //The vine leaves' flex, in LEAF mode (material_t::wind_mode 1) - by distance from the stem.
    float      vine_leaf_wind_flex = 0.1f;

    /*
        LEAVES ON THE WIND (wind_plan.md step 4). The swarm is simulated on the PHYSICS thread,
        once a tick from RunSimulationTick - so a paused game holds every leaf where it is - under
        wind_mutex, since the render thread rebuilds the field. The pool is built once at Init
        (WIND_LEAF_POOL of them, all one mesh, so the renderer draws them as one instanced call);
        the swarm's count decides how many show. The swarm lives in the view padded 150% on
        every side (user, 2026-09-26: zooming out showed the leaves in a box, and aiming and
        obstacles will zoom), at a density - about 1600 leaves at the default zoom, of which a
        sixth are on screen.
    */
    enum{ WIND_LEAF_POOL = 2000, WIND_LEAF_TINTS = 4 };
    LeafSwarm leaf_swarm;
    std::atomic<bool> f_wind_leaves{true};
    Object*   leaf_group = NULL;
    std::vector<Object*> leaf_objects;
    Mesh*     leaf_mesh = NULL;
    std::vector<Material> leaf_materials[WIND_LEAF_TINTS];
    float     leaf_scale = 1.0f;            //panel; on top of model_scale and each leaf's own size
    void BuildWindLeaves();
    void StepWindLeaves();
    //For archer_wind: on or off, and how many are flying, lying and fading. Hold wind_mutex.
    json LeafSummary();

    /*
        WIND STREAKS (wind_plan.md step 5). Unlike the leaves these are simulated on the RENDER
        thread, in UpdateWind, catching up on however many ticks passed since the last frame - they
        are pure decoration, they rebuild a mesh every frame anyway (GL, render thread), and they
        need no physics-thread state. Still tick-driven, so a paused game freezes them. Drawn
        through the custom-shader pass (shaders/wind_streak.*): unlit, alpha-blended, no depth
        write, depth-tested so terrain in front hides them.
    */
    StreakSwarm streak_swarm;
    std::atomic<bool> f_wind_streaks{true};
    Object*   streak_object = NULL;
    Mesh*     streak_mesh = NULL;
    Shader*   streak_shader = NULL;
    int       streak_shader_index = -1;
    int64_t   streak_last_tick = -1;
    vec3      streak_color = vec3(0.90f,0.95f,1.0f);
    std::vector<StreakVertex> streak_ribbons;
    std::vector<vertex> streak_vertices;
    void BuildWindStreaks();
    //Under wind_mutex, from UpdateWind.
    void UpdateWindStreaks(int64_t tick);
    void SetStreakUniforms();

    /*
        FIREFLIES (wind_plan.md step 6). Homes come from the foliage scatter (ScatterFoliageObjects
        hands them over under wind_mutex), so they live on the main level where the plants are and
        most where the plants grew in shade. Simulated and drawn like the streaks - render thread,
        in UpdateWind, catching up the ticks - through shaders/firefly.* (a hot core and a drawn
        halo, since the engine has no bloom). FIREFLY_LIGHTS real point lights are ONE group: each
        stands for the flies in its third of the view, at their glow-weighted centre, brightening
        with their flashes (Fireflies::LightGroup), smoothed so a flash swells rather than blinks.
    */
    enum{ FIREFLY_LIGHTS = 3 };
    FireflySwarm firefly_swarm;
    std::atomic<bool> f_fireflies{true};
    Object*   firefly_object = NULL;
    Mesh*     firefly_mesh = NULL;
    Shader*   firefly_shader = NULL;
    int       firefly_shader_index = -1;
    int64_t   firefly_last_tick = -1;
    PointLight* firefly_lights[FIREFLY_LIGHTS] = {};
    float     firefly_light_level[FIREFLY_LIGHTS] = {};     //smoothed intensity per light
    float     firefly_light_gain = 1.2f;                    //panel
    float     firefly_glow_size = 0.9f;                     //panel; the quad across at full flash
    float     firefly_halo = 0.75f;                         //panel; the halo's strength
    std::vector<FireflyVertex> firefly_quads;
    std::vector<vertex> firefly_vertices;
    std::vector<FireflyLight> firefly_group;
    void BuildFireflies();
    //Under wind_mutex, from UpdateWind.
    void UpdateFireflies(int64_t tick);
    void SetFireflyUniforms();
    //For archer_wind. Hold wind_mutex.
    json FireflySummary();
    //Measured every tick on the rope, -1 off it. World units.
    float   rope_joint_gap = -1.0f;         //red to blue
    float   rope_hands_off = -1.0f;         //red to the point between the yellows
    float   rope_hand_s[2] = { -1.0f, -1.0f };
    float   rope_hand_off[2] = { -1.0f, -1.0f };
    /*
        THE GRIP on the rope as the joint holds it: the distance down it that the joint was last
        made at, the link that distance falls on, and the point on that link in its own frame.
        Re-made whenever Stage's rope_s moves - see ReanchorRopeJoint.
    */
    float   rope_grip_s = -1.0f;
    int     rope_grip_link = -1;
    vec3    rope_grip_local;
    //How far the anchor sits from the link's geometry, carried over at the last crossing - the
    //stretch of the joint she climbed across. See ReanchorRopeJoint.
    vec3    rope_grip_offset;
    float   rope_grip_cross_s = 0.0f;       //where the last crossing was; the offset fades from there
    float   rope_grip_drift = 0.0f;         //how far the anchor sits off the link's geometry now

    //--- The rope test bench (debug) - see MeasureRopeStretch ---
    //The links' mass, used by BuildRope. ROPE_SEGMENT_MASS unless the bench has changed it; a change
    //takes effect when the level is rebuilt.
    float   rope_link_mass = ROPE_SEGMENT_MASS;
    //This scene's solver iterations as last set - rp3d has setters but no getters. Core's defaults.
    int     solver_velocity_iterations = 12;
    int     solver_position_iterations = 10;
    struct RopeStretch{
        int   joints = 0;
        float rest_length = 0.0f;           //links * link length
        float loaded_length = 0.0f;         //the same plus every intact joint's gap
        float worst_gap = 0.0f;             //now
        int   worst_joint = -1;
        float peak_gap = 0.0f;              //the worst over the last ROPE_PEAK_TICKS
        int   peak_joint = -1;
        int   peak_age = 0;
        std::vector<float> gaps;            //per joint, top first; -1 where cut
        int   cuts = 0;
    };
    RopeStretch rope_stretch;
    int     rope_cut_request = 1;           //the panel's joint number
    //Each link's length, recorded by BuildRope - what turns a distance down the rope into a link.
    float   rope_seg_len = 0.0f;
    Object* arrow_objects[ARROW_MAX_LIVE] = {};
    /*
        AN ARROW THAT STRUCK A PROP, and rides it from then on.

        WHY THIS IS NOT AttachChild, which is the obvious way to do it and is wrong here: every prop
        is ONE unit mesh stretched by SetScale, so a child inherits that scale - and S*R is a shear
        whenever the two axes in the rotation plane differ. A crate is 0.80 x 0.80 and would have
        been fine; a target board is 0.30 x 1.60 and a brick is 0.90 x 0.45, so an arrow stuck in
        either at any angle off the axes would be drawn as a bent splinter. The two things in this
        level worth shooting are exactly the two that break.

        So the arrow stays a root object and FOLLOWS instead, which is the same idea one level down
        and costs a handful of lines because props are planar: a position and one angle.
    */
    struct StuckArrow{
        Object* prop = NULL;        //NULL means this arrow is loose in the world
        v2      local;              //where it went in, in the prop's own frame
        float   local_angle = 0.0f; //and at what angle, relative to the prop's
    };
    StuckArrow arrow_stuck[ARROW_MAX_LIVE];

    /*
        EVERYTHING THAT BELONGS TO ONE LEVEL RATHER THAN TO THE APP - the parked half of it.

        The app has three scenes - the main level, the test range and the rope test - and each has
        its own Stage, physics world, level objects and archer BODY. The live level's copy of all
        that sits in the ordinary members above, where the whole of this file has always found it;
        every other level's sits in one of these, in parked_levels. OnActiveSceneChanged swaps the
        live members with the slot holding the scene being switched TO, and that slot then holds
        the level just left - so the tick, the panel and the tools never have to know which level
        they are looking at, and a fourth scene is one more BuildExtraLevel call.

        THIS IS DELIBERATELY THE EASY WAY: a real per-scene store would mean indexing every one of
        these by scene instead of swapping them.
        What it gets right already is the split. What is NOT in here is shared by both scenes -
        the skinned model and the bow in her hands, the arrow and aim-arc views, the backdrop, the
        two lights, the Puppet and the animation state - because she is one character walking
        between two places, and a second copy of a 65-bone model with thirty clips would be one
        more thing to keep in step. Those Objects are added to BOTH scenes' lists; only the active
        scene is drawn, ticked or animated, so each is touched once per pass.

        Leaving a level PARKS it rather than resetting it. Crates stay kicked, arrows stay stuck
        and an arrow still in flight is still in flight when you come back, because it lives in
        that level's Stage. The archer's body stays in the parked world too, which is safe: a world
        that is not stepped does not move it.
    */
    struct ArcherLevel{
        Scene*  scene = NULL;
        Stage   stage;
        Object* archer_object = NULL;
        std::vector<Object*> block_objects;
        std::vector<Object*> plant_objects;
        std::vector<Object*> spring_plant_objects;
        Object* balance_bar = NULL;
        Object* balance_marker = NULL;
        Object* blockout_group = NULL;
        std::vector<PropView> prop_views;
        std::vector<DebrisView> debris;
        std::vector<Object*> terrain_objects;
        std::vector<Object*> terrain_back_objects;
        std::vector<float> backdrop_reach;
        std::vector<int> melted_blocks;
        std::vector<Object*> rope_segments;
        rp3d::BallAndSocketJoint* rope_joint = NULL;
        std::vector<rp3d::BallAndSocketJoint*> rope_joints;
        Object* rope_anchor_object = NULL;
        Skeleton* rope_skin = NULL;
        std::vector<Bone*> rope_bones;
        std::vector<int> rope_skin_cuts;
        float rope_seg_len = 0.0f;
        int   solver_velocity_iterations = 12;
        int   solver_position_iterations = 10;
        StuckArrow arrow_stuck[ARROW_MAX_LIVE];
        vec3 camera_target = vec3(0.0f,3.0f,0.0f);
        vec3 camera_ideal = vec3(0.0f,3.0f,0.0f);
        vec3 orbit_follow_offset = vec3(0.0f,0.0f,0.0f);
    };
    std::vector<ArcherLevel> parked_levels;     //one per scene that is not live
    //The scenes by what they are, so the tools and the swap can tell them apart without comparing
    //names. world_scene is the one Init built first; main_scene is whichever is live.
    Scene* world_scene = NULL;
    Scene* range_scene = NULL;
    Scene* rope_scene = NULL;

    /*
        --- THE TITLE SCREEN ------------------------------------------------------------------------

        A SCENE OF ITS OWN, as in apps/bomber: an ortho camera and one unlit quad. Being main_scene
        is what keeps the level quiet behind it - nothing of the world ticks or draws while it is
        up, and RunSimulationTick and UpdateView only have to step aside rather than be taught to
        idle - which is also what makes Escape to the title a pause. It is never parked and never
        swapped: the live members stay those of the level it was entered from (title_return_scene)
        the whole time it is up, and OnActiveSceneChanged treats leaving it as leaving that level.

        IT IS ALSO THE LOADING SCREEN, and the loading happens INSIDE Init. Init runs on the render
        thread with the GL context current and every build step needs that context, so the work
        cannot move to another thread; instead Init draws a frame of this scene between steps
        (LoadingStep). The window stays responsive throughout, because its messages are pumped on
        the main thread, not this one. When Init returns everything is built, so the screen simply
        changes its text from the progress to "click to continue" and waits.
    */
    Scene*  title_scene = NULL;
    Object* title_quad = NULL;
    int     title_tap_button = -1;          //the full-window click, or -1 if the bind failed
    //f_show_ui as it was before the title hid the panels, put back on leaving it.
    bool    f_title_saved_show_ui = true;
    /*
        The level the title was entered from: the one whose members are live while it is up, and
        the one continue goes back to. world_scene at startup. PHYSICS THREAD - written and read
        only in OnActiveSceneChanged and UpdateTitle.
    */
    Scene*  title_return_scene = NULL;
    void CreateTitleScene();
    //Scales the quad to cover a w x h window. Render thread.
    void FitTitleQuad(int w, int h);
    //Physics thread, from UpdateView while the title is live: continue, or exit.
    void UpdateTitle(InputController* input);
    //The title coming up and going down: its click rect and the panels. Physics thread.
    void EnterTitle();
    void LeaveTitle();

    //Loading progress, written and read on the render thread only - Init draws its own frames.
    bool        f_loading = false;
    int         loading_step = 0;
    int         loading_steps = 1;
    const char* loading_label = "";
    std::chrono::steady_clock::time_point loading_step_start;
    std::chrono::steady_clock::time_point loading_start;
    //Starts step `index` of `count`: logs how long the previous one took, then draws a frame.
    void LoadingStep(int index, int count, const char* label);
    //One frame of the title scene and its overlay, drawn from inside Init. See the definition.
    void DrawLoadingFrame();
    //Exchanges the live level's members with this parked slot's. See ArcherLevel.
    void SwapLevel(ArcherLevel& parked);
    //Builds `level` as a scene of its own called `name`, sharing the character. See the definition.
    Scene* BuildExtraLevel(int level, const char* name);
    //Adds the Objects both scenes share - see ArcherLevel - to `scene`'s list.
    void ShareCharacterWith(Scene* scene);
    //Puts everything view-side where the newly live level says, without advancing anything.
    void RefreshViewAfterSwitch();

    //Pins an arrow to the prop it just hit, and lets one go again. Releasing matters more than it
    //looks: the arrow pool is a 24-slot ring, and a recycled slot still holding a prop would fire
    //the NEXT arrow welded to a crate.
    void  StickArrowToProp(int index, Object* prop, const v2& point);
    void  ReleaseStuckArrows(Object* prop);     //NULL releases every one of them
    Object* arc_objects[AIM_ARC_POINTS] = {};

    //Where the camera would like to be, before smoothing. Kept between ticks so the lerp has
    //something to lerp from.
    vec3 camera_ideal = vec3(0.0f,3.0f,0.0f);
    //In ARCHER_CAM_ORBIT, where the pivot sits relative to her: the orbit follows her by keeping
    //this constant, and a shift+middle pan moves it. Taken from the view when the orbit is
    //switched on, so switching moves nothing. Per level - swapped with camera_target.
    vec3 orbit_follow_offset = vec3(0.0f,0.0f,0.0f);

    //--- The animation ----------------------------------------------------------------------------
    //The decisions. Lives on the physics thread with the Stage, and is read by DrawImGuiUI under
    //physics_mutex like everything else here.
    Puppet puppet;
    /*
        The bow and the nocked arrow, hung off the hand bones.

        NO PER-FRAME POSITION CODE GOES WITH THIS. The skeleton moves them, because they are
        children of bones and Object composes a child's world transform from its parent's - see the
        long note in Bow.h. What the app drives is the BEND, from Stage::draw_ticks, and that is
        one call.
    */
    Bow bow_rig;
    /*
        Whether an arrow is on the string: exactly while she draws (bow_mode == BOW_DRAWING), set
        every tick by SyncBow. So between shots the string is empty and the arrow in flight is the
        only one on screen - one left on the bow read as a second arrow - and at idle there is no
        arrow riding the socket pointing at the floor.
    */
    bool f_arrow_nocked = false;
    //The bend SyncBow last put on the string, 0..1 - from her hand or from draw_ticks, see SyncBow.
    float bow_draw_shown = 0.0f;
    int   nock_warned_at = -1;      //the draw tick CheckNockTicks last warned about
    //The upper-body layer as handed to the model: which clip, its loop clock, and the time last
    //sampled (held when a non-looping clip fades out). See SyncArcherAnimation.
    int   upper_clip_shown = -1;
    float upper_loop_time = 0.0f;
    float upper_time_shown = 0.0f;
    int   upper_xfade_seen = 0;         //Puppet::upper_xfade_serial as of the last freeze
    float upper_from_time = 0.0f;       //the leaving clip's time, frozen when the crossfade began
    //Worked out from the bind pose at load: what the rig has to be scaled by to stand
    //ARCHER_MODEL_HEIGHT tall, and where its feet sit once it has been.
    float model_scale = 1.0f;
    float model_foot_offset = 0.0f;

    int anim_source = ANIM_FROM_GAME;
    //What ANIM_FROM_PANEL feeds the Puppet. The same struct the rules fill, by hand.
    ArcherAnimParams panel_params;
    int   preview_clip = CLIP_IDLE;     //what ANIM_FROM_CLIP plays
    float preview_rate = 1.0f;
    //Draw the collider box as well as the model. Off by default once there is a model, because
    //the box is inside the character and reads as her standing in a crate.
    bool  f_show_collider = false;
    //What is on screen right now, so the panel and MCP can report it without asking the model.
    int   playing_clip = -1;
    //The yaw the model was actually drawn at, in degrees - the Puppet's answer plus a turning
    //clip's own contribution. Reported rather than recomputed, because the sum is the thing.
    float model_yaw_drawn = 0.0f;
    //And the tilt she was drawn at, which only the rope ever gives her. Degrees, + is anticlockwise
    //on screen. Reported for the same reason: it comes from the solver and is worth being able to
    //read when it looks wrong.
    float model_roll_drawn = 0.0f;
    //The rope climb's playhead pinned LAST tick - the pose the engine is showing, since the rig is
    //posed before the tick runs - by its cycle's base (PuppetChoice::lift_base). And the lift the
    //model was lowered by this tick, for archer_state.
    bool  f_climb_posed = false;
    float climb_base_posed = 0.0f;
    /*
        IN THE AIR, THE HIPS RIDE THE BODY. The model is moved so the posed hips sit at the height
        a landing meets the ground with (air_hip_ref: the routine landing's contact frame, measured
        at load) - whatever mix of clips and overlay put them elsewhere. That height rather than
        standing, 0.08 higher: held at standing, the hold let go of 0.08 in the one tick of
        contact, and letting go over the last ticks instead showed the lead-in's steepest descent. A landing's lead-in is an authored descent onto the floor, a tuck lifts
        the feet by dropping the hips, and in flight none of that is the body moving: the rules'
        box is. `air_hip_weight` eases it in over a crossfade when she leaves the ground, the rope
        or a ledge (Hanging_Braced's hips are 0.16 above standing, and snapping that away read as
        a drop) - which also takes up the 0.08 between standing and the reference at a takeoff -
        and it has nothing to let go of at a landing's contact frame.
    */
    float air_hip_ref = 0.0f;
    float air_hip_weight = 0.0f;
    float climb_lift_posed = 0.0f;

    //--- The backdrop -----------------------------------------------------------------------------
    Object* background_object = NULL;
    int   material_background = 0;
    //Both live on sliders, because "what would this look like" is the question being asked
    //of the image and neither answer is knowable without seeing it move.
    float background_follow = BACKGROUND_FOLLOW;
    float background_scale = 1.0f;
    float background_offset_y = 0.0f;
    //The quad's size before background_scale, worked out once from the widest view the zoom
    //can produce. Kept so the slider has something to scale.
    vec2  background_base = vec2(1.0f,1.0f);

    //How far back the camera sits. A member rather than CAMERA_DISTANCE outright, because the
    //wheel moves it - the define is still the value it starts at and returns to on a restart.
    float camera_distance = CAMERA_DISTANCE;
    /*
        ARCHER_CAM_SIDE or ARCHER_CAM_ORBIT. One for the app rather than one per level, so it is
        not in ArcherLevel: switching scene keeps the camera you chose. Written by the panel under
        physics_mutex, read on the physics thread.
    */
    int   camera_mode = ARCHER_CAM_SIDE;
    //The mode UpdateCamera last ran in, so it can tell the tick the orbit was switched on - the
    //panel writes camera_mode from the render thread and nothing else announces the change.
    int   last_camera_mode = ARCHER_CAM_SIDE;

    //--- Chrome -----------------------------------------------------------------------------------
    bool f_show_engine_ui = false;

    //See ARROW_SPEED_TRANSFER. A member rather than the bare define so the Archer panel can drag
    //it while the game runs - the whole point of a prototype is to find this number by feel, and
    //it belongs to the app rather than to Stage because it is about rigid bodies the rules never
    //see. Written from DrawImGuiUI, which holds physics_mutex, and read on the physics thread.
    float arrow_speed_transfer = ARROW_SPEED_TRANSFER;

    //--- Telemetry --------------------------------------------------------------------------------
    ArcherSnapshot snapshot;
    std::mutex snapshot_mutex;
};

#endif
