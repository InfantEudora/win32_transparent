#ifndef _APPLICATION_ARCHER_H_
#define _APPLICATION_ARCHER_H_

#include <mutex>
#include <vector>
#include <string>

#include "Application.h"
#include "Stage.h"
#include "Puppet.h"
#include "Terrain.h"
#include "Bow.h"

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

    KEYBOARD ONLY, and laid out so that no key means two things:

        A / D, Left / Right     run
        S                       drop through a one-way platform; let go of a ledge
        Space                   jump (hold for height, tap for a hop); climb up from a hang
        J                       hold to draw the bow, release to loose
        Up / Down               tilt the aim, whether or not the bow is drawn
        K                       kick - shoves props hard, breaks walls
        E                       action - take the rope                 (later slice)
        L                       knife                                 (later slice)
        R                       restart
        F1                      the engine's ImGui panels

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
#define ARCHER_MASK_LEVEL           (ARCHER_CAT_PROP | ARCHER_CAT_DEBRIS)
#define ARCHER_MASK_ARCHER          0
#define ARCHER_MASK_PROP            (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP | ARCHER_CAT_ARCHER | ARCHER_CAT_DEBRIS)
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
//What the archer collides with WHILE SWINGING, which is the one time the solver owns them. Off the
//rope it is ARCHER_MASK_ARCHER (nothing), because Stage resolves everything itself - but on the
//rope Stage is not resolving anything, so without this the swing passes through the floor.
#define ARCHER_MASK_ON_ROPE         (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP)

/*
    The rope, as bodies.

    Eight links over the six units the level declares, which is 0.75 each - short enough that the
    rope bends visibly rather than swinging as a plank, long enough that the solver is not holding
    thirty constraints together for a piece of set dressing. The links are light against the
    archer's 70kg on purpose: a rope that weighs as much as the person on it swings like a wrecking
    ball rather than like a rope.
*/
#define ROPE_SEGMENTS               8
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

/*
    How much of an arrow's speed the thing it hits takes, 0..1.

    A RATIO rather than a force, because it is scaled by the struck body's own mass when it is
    applied - so a target board and a crate pick up the same velocity from the same arrow, and this
    one number stays meaningful instead of needing a sibling per prop. At 0.04 a full-draw arrow
    hands a target 1.8 units a second, which topples a standing board and rocks a crate without
    launching either. Tunable live in the Archer panel.
*/
#define ARROW_SPEED_TRANSFER        0.040f

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
class ArcherModel : public Skeleton{
public:
    //Radians of yaw this clip has turned her through since it started. ADDED to the facing the
    //Puppet asks for, rather than replacing it - a turn authored in a clip is a turn relative to
    //wherever the character was already pointing. Reset when the clip changes.
    float clip_yaw = 0.0f;
    void ApplyRootMotion(const RootMotionDelta& delta) override;
};

/*
    The backdrop.

    One quad, unlit, a long way behind the play plane, sized so that it still covers the view when
    the camera is zoomed all the way out. It is PARALLAX rather than scenery: `background_follow`
    is the fraction of the camera's motion it copies, so 1.0 pins it to the camera and it reads as
    infinitely far away, and 0.0 nails it to the world and it slides past at the same rate as the
    ground. Anything between is a distance.

    The image is 1152x1536 - PORTRAIT, against a 16:9 view - so it is fitted to COVER rather than
    to contain: scaled until it fills the width, with the overflow running off the top and bottom.
    Letterboxing a backdrop is worse than cropping one.
*/
#define BACKGROUND_ASSET            "images/background1.png"
#define BACKGROUND_IMAGE_ASPECT     0.75f   //1152/1536
#define BACKGROUND_DEPTH            40.0f   //behind the play plane, in world units
#define BACKGROUND_FOLLOW           0.85f   //0 = nailed to the world, 1 = pinned to the camera
#define BACKGROUND_COVER            1.15f   //margin over the view it has to fill

//The camera trails the archer rather than being welded to them - see UpdateCamera.
#define CAMERA_DISTANCE             26.0f
//And the wheel moves it in and out. Proportional rather than a fixed step, so a notch feels the
//same close up and far away - 10% of wherever it currently is.
#define CAMERA_ZOOM_PER_NOTCH       0.10f
#define CAMERA_DISTANCE_MIN         5.0f    //close enough to read a hand
#define CAMERA_DISTANCE_MAX         60.0f   //the whole of a screen's worth of level
#define CAMERA_HEIGHT               3.2f
#define CAMERA_LEAD                 3.0f    //world units ahead, in the direction of travel
#define CAMERA_SMOOTH               0.10f   //per-tick lerp toward the ideal

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
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float facing = 1.0f;
    int   mode = MODE_AIR;
    bool  f_on_ground = false;
    int   coyote_ticks = 0;

    int   bow_mode = BOW_IDLE;
    int   draw_ticks = 0;
    float draw_power = 0.0f;
    float aim_deg = 0.0f;
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
    };
    std::vector<TargetView> targets;

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
    //How many arrows are currently riding a prop rather than sitting in the world. Reported
    //because it is the one piece of this that is invisible until something moves: an arrow pinned
    //to the wrong prop, or to a prop it stopped being in, looks exactly like a correct one right
    //up until that prop is kicked.
    int   arrows_on_props = 0;
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
};

class ApplicationArcher : public Application{
public:
    ApplicationArcher();
    ~ApplicationArcher();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    //The rules. Touched ONLY from the physics thread (RunSimulationTick and the command handlers,
    //which also run there) and from DrawImGuiUI, which holds physics_mutex. Everything else goes
    //through `snapshot`.
    Stage stage;

    vec3 camera_target = vec3(0.0f,3.0f,0.0f);

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
    //Recomputes which blocks the terrain covers and applies f_show_blockout to them. NO GL, so
    //unlike BuildTerrain this is safe from NewGame on the physics thread. See the definition.
    void ApplyBlockoutVisibility();
    //Show or hide the blockout boxes the terrain replaced. See the definition.
    void SetBlockoutVisible(bool f_visible);
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
    //hips. Checked against KICK_ACTIVE_FROM/TO rather than setting them - see Puppet::kick_strike.
    void MeasureKickClip();
    void BuildArrowViews();
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
    //core/physics/Physics.h for what happens when one does.
    Object* MakePlanarBody(Mesh* mesh, const char* name, const vec3& position, const vec3& size,
                           int material, uint32_t category, uint32_t collide_mask,
                           float mass, bool f_static);

    //--- Per tick, physics thread -----------------------------------------------------------------
    void GatherInput(ArcherInput& out);
    void HandleEvents(const StageEvents& events);
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
    //Hand Stage the links it may catch, BEFORE the tick, the same way the props are handed over.
    void RefreshRopePoints();
    //The handoff, both ways. See the note on AttachArcherToRope.
    void AttachArcherToRope(int segment);
    void DetachArcherFromRope(bool f_jump);
    //Read the swinging body back into Stage, so the rules, the camera and the telemetry all know
    //where the archer is while the solver is the one moving them.
    void SyncArcherFromRope();
    void PumpRope(float move_axis);
    void SyncArrowViews();
    void SyncAimArc();
    //Cuts the aim arc short at the first PROP it would hit - the half of "what will this arrow
    //hit" that Stage cannot answer. See the note on the definition.
    int  TruncateArcAgainstProps(v2* points, int count);
    void UpdateCamera();
    //Puts the camera where camera_target and camera_distance say, and drags the backdrop
    //and the sun along with it. Called from the tick AND from the wheel, which is why it is
    //its own function - a zoom has to show while the simulation is paused.
    void PlaceCamera();
    void UpdateTargets();
    //Retires props that have been knocked out of the level, so they stop falling forever.
    void ReapFallenProps();
    void PublishSnapshot();
    void NewGame();
#ifdef USE_MCP
    json BuildStateJson();
    //Blocks until `ticks` more simulation ticks have run, or the timeout expires. Every tool that
    //acts rather than observes ends in one of these, so a caller never has to sleep and guess.
    void WaitTicks(int ticks);
#endif

    //--- Meshes and materials ---------------------------------------------------------------------
    Mesh* unit_mesh = NULL;         //a 1x1x1 box, scaled per block
    Mesh* arrow_mesh = NULL;
    Mesh* dot_mesh = NULL;          //the aim arc's beads

    int material_ground = 0;
    int material_ledge = 0;
    int material_platform = 0;
    int material_breakable = 0;
    int material_archer = 0;
    //The character model's own colour - see the note where it is assigned.
    int material_archer_skin = 0;
    //A second archer colour for MODE_HANG / MODE_CLIMB. With no animation yet, the colour IS the
    //state readout - it is what makes "is he hanging or is he stuck in the wall" answerable from a
    //screenshot, which is how this app gets checked over MCP.
    int material_archer_hang = 0;
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
    std::vector<Object*> block_objects;         //parallel to Stage::blocks
    std::vector<PropView> prop_views;
    std::vector<DebrisView> debris;

    //--- The terrain ------------------------------------------------------------------------------
    //One Object per test bay, so each variant can be hidden on its own and object_list names them
    //separately over MCP. Empty when ARCHER_TEST_BAY is off.
    std::vector<Object*> terrain_objects;
    //Which block objects BuildTerrain hid, so the debug view can put them back without having to
    //work out again which ones melted. Indices into block_objects.
    std::vector<int> melted_blocks;
    //The debug view: the blockout boxes underneath the terrain. Off by default; F2 toggles it, and
    //so does the terrain_blockout MCP tool.
    bool f_show_blockout = false;

    //--- The rope ---------------------------------------------------------------------------------
    std::vector<Object*> rope_segments;         //top link first
    //The joint holding the archer to a link while MODE_ROPE, and NULL the rest of the time. Held
    //because it has to be destroyed again - a swing that cannot be let go of is not a swing.
    rp3d::BallAndSocketJoint* rope_joint = NULL;
    std::vector<rp3d::BallAndSocketJoint*> rope_joints;   //the links to each other, and to the anchor
    Object* rope_anchor_object = NULL;
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
    //Pins an arrow to the prop it just hit, and lets one go again. Releasing matters more than it
    //looks: the arrow pool is a 24-slot ring, and a recycled slot still holding a prop would fire
    //the NEXT arrow welded to a crate.
    void  StickArrowToProp(int index, Object* prop, const v2& point);
    void  ReleaseStuckArrows(Object* prop);     //NULL releases every one of them
    Object* arc_objects[AIM_ARC_POINTS] = {};

    //Where the camera would like to be, before smoothing. Kept between ticks so the lerp has
    //something to lerp from.
    vec3 camera_ideal = vec3(0.0f,3.0f,0.0f);

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
