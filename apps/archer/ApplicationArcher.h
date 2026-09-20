#ifndef _APPLICATION_ARCHER_H_
#define _APPLICATION_ARCHER_H_

#include <mutex>
#include <vector>
#include <string>

#include "Application.h"
#include "Stage.h"

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

    WHAT THE BODY IS AND IS NOT FOR. The intention was that a kinematic body moved by velocity
    would shove the crates out of its way for free, which is what a kinematic character controller
    normally buys you. IT DOES NOT WORK HERE, and the reason is structural rather than a tuning
    miss: that trick assumes the character is STOPPED by what it pushes, so the overlap stays
    shallow. Stage knows nothing about props - it resolves the archer against the level and walks
    straight through a crate - so the solver instead gets a deep overlap between a light body and
    one of infinite mass, every tick, and resolves it the only way it can. Measured twice, at two
    different collision masks; the second time a target board left at 58 units a second. So the
    archer collides with NOTHING (ARCHER_MASK_ARCHER is 0) and shoves props through KickProps(),
    which sets a bounded velocity and cannot accumulate.

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
        S                       drop through a one-way platform
        Space                   jump (hold for height, tap for a hop)
        J                       hold to draw the bow, release to loose
        Up / Down               tilt the aim, whether or not the bow is drawn
        E                       action - grab a ledge, take the rope  (later slices)
        K                       knife                                 (later slice)
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
#define INPUT_ARCHER_KNIFE          INPUT_LAST+9
#define INPUT_ARCHER_RESTART        INPUT_LAST+10
#define INPUT_ARCHER_TOGGLE_UI      INPUT_LAST+11

//Our own simulation commands, numbered from SIM_CMD_LAST. Both are intent arriving from OUTSIDE
//the simulation - a key, an MCP call, later a replay - which is what the command queue is for:
//the caller is on the wrong thread, and the handler runs at one known place in the tick.
#define ARCHER_CMD_RESTART          SIM_CMD_LAST+0
#define ARCHER_CMD_AIM              SIM_CMD_LAST+1      //value[0] = degrees, relative to facing

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

//And what each one is allowed to touch.
//
//THE ARCHER TOUCHES NOTHING, which is 0 and means exactly that - "in no category, collides with
//nothing" is a real filter, not an unset one (see the note on the bits in core/Object.h).
//
//Two separate things led here, and they are worth keeping apart because conflating them cost an
//afternoon:
//
//  1. THE REAL BUG was not a filter at all. Every prop had gravity switched off, because a body
//     from AddPhysics starts with it off and SetStatic(false) does not turn it on. No gravity
//     means no weight on the floor, no normal force and therefore NO FRICTION - so anything
//     touched once slid or drifted forever, and the level filled up with props at y = 113 doing
//     2 units a second. It reads exactly like the solver exploding. See MakePlanarBody.
//  2. THE FILTER IS STILL RIGHT, for its own reason: Stage resolves the archer against the level
//     by hand, and a kinematic body of infinite mass arguing with that same static geometry is a
//     second opinion on a settled question. Nothing good comes of it even now that the props
//     behave.
//
//So the archer is out of the solver's hands and pushes props through KickProps() instead, which
//is bounded by construction. With gravity finally correct, letting the solver do the pushing is a
//reasonable thing for the kick-and-break slice to re-evaluate - it was never given a fair trial.
#define ARCHER_MASK_LEVEL           (ARCHER_CAT_PROP | ARCHER_CAT_DEBRIS)
#define ARCHER_MASK_ARCHER          0
#define ARCHER_MASK_PROP            (ARCHER_CAT_LEVEL | ARCHER_CAT_PROP | ARCHER_CAT_ARCHER | ARCHER_CAT_DEBRIS)
#define ARCHER_MASK_DEBRIS          ARCHER_MASK_PROP

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
    How hard the archer shoves a prop they walk into, in units per second, and how much of a lift
    the shove has in it.

    A SPEED THE PROP IS SET TO, never a force added to it, which is the whole point: a set velocity
    cannot accumulate over the ticks the archer spends inside the prop, so there is no amount of
    walking into a crate that launches it. The lift is what makes a kicked crate hop rather than
    grind along the floor, and is small enough that it cannot be used as a step.
*/
#define ARCHER_KICK_SPEED           7.0f
#define ARCHER_KICK_LIFT            2.2f
//Below this the archer is loitering rather than walking into something, and a prop resting against
//a standing character should stay where it is.
#define ARCHER_KICK_MIN_SPEED       1.5f

//The camera trails the archer rather than being welded to them - see UpdateCamera.
#define CAMERA_DISTANCE             26.0f
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
};

//One prop, and the Object plus body that shows it. Kept so an arrow's raycast hit - which comes
//back as an rp3d body - can be turned into "that was target 2".
struct PropView{
    Object* object = NULL;
    int   kind = PROP_CRATE;
    int   index = -1;           //index into Stage::props, or -1 for a brick in a wall
    bool  f_knocked = false;
    //Half extents as built, so KickProps can test an overlap without asking rp3d for a shape. A
    //toppled board's box is no longer axis-aligned, which makes this an approximation - and the
    //right one: it is deciding whether the archer is walking into something, not resolving a
    //contact, and a knocked-over prop is not something you can walk into any more anyway.
    vec3  half_extents = vec3(0.5f,0.5f,0.5f);
    //Whether the archer was inside this prop LAST tick. A kick fires on the leading edge of that
    //and nowhere else - see KickProps.
    bool  f_kick_contact = false;
    //Fell out of the level and has been retired - see ReapFallenProps.
    bool  f_lost = false;
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
    void BuildProps();
    void BuildArcher();
    void BuildArrowViews();
    void BuildAimArc();
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
    //The archer's push on the props, in place of the solver contact that used to do it. See the
    //note on the definition for the two measurements that led here.
    void KickProps();
    void SyncArrowViews();
    void SyncAimArc();
    //Cuts the aim arc short at the first PROP it would hit - the half of "what will this arrow
    //hit" that Stage cannot answer. See the note on the definition.
    int  TruncateArcAgainstProps(v2* points, int count);
    void UpdateCamera();
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
    int material_crate = 0;
    int material_target = 0;
    int material_target_hit = 0;
    int material_arrow = 0;
    int material_dot = 0;
    int material_dot_hot = 0;

    //--- The scene --------------------------------------------------------------------------------
    Object* archer_object = NULL;
    //The sun, kept because UpdateCamera drags it along with the view every tick - the level is 84
    //units wide and one shadow ortho cannot cover that, so the light follows the camera.
    DirectionalLight* sun_light = NULL;
    std::vector<Object*> block_objects;         //parallel to Stage::blocks
    std::vector<PropView> prop_views;
    Object* arrow_objects[ARROW_MAX_LIVE] = {};
    Object* arc_objects[AIM_ARC_POINTS] = {};

    //Where the camera would like to be, before smoothing. Kept between ticks so the lerp has
    //something to lerp from.
    vec3 camera_ideal = vec3(0.0f,3.0f,0.0f);

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
