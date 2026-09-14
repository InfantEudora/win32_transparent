#ifndef _APPLICATION_BREAKOUT_H_
#define _APPLICATION_BREAKOUT_H_

#include <mutex>
#include <atomic>
#include <vector>
#include <string>

#include "Application.h"
#include "SoundSystem.h"
#include "TextMesh.h"
#include "Field.h"

/*
    A paddle game on this engine.

    The split is the one the rest of the repo uses and it is worth taking seriously:
    breakout/Field holds the RULES - a brick array, a swept circle, some tick counters, no engine
    type anywhere in its header - and this class is the VIEW plus the wiring. It turns three
    different input devices into one intent, turns the brick array into lit cubes, hands the
    physics engine the jobs a solver is genuinely better at than a hand-written integrator, and
    exposes the whole thing over MCP so the game can be played and measured without a human.

    Threading, since everything here depends on it:
      - Init()              render thread, once. All GL, all mesh building, all registration.
      - RunSimulationTick() physics thread, once per tick that RUNS, physics_mutex held. The game
                            lives here - it pauses and single-steps with the physics.
      - UpdateView()        physics thread, every pass including paused ones. Chrome only.
      - PreRender()         render thread, top of every frame. Where the text meshes are rebuilt.
      - onContact()         physics thread, INSIDE rp3d's own step. Stages intent, never acts.
      - DrawImGuiUI()       render thread, physics_mutex held. Reads the live game; never waits.
      - MCP handlers        their own thread, NO lock. They read `snapshot` and submit input
                            events or SimCommands. They never touch the scene.

    --- WHAT THE PHYSICS ENGINE IS FOR HERE ------------------------------------------------------
    The ball is NOT a rigid body; see the long note at the top of breakout/Field.h and section 8 of
    docs/breakout_findings.md. reactphysics3d instead owns three things the solver really is the
    right tool for, all of which would be miserable by hand:

      - the DEBRIS a brick bursts into, which tumbles down the field and piles up against the
        bricks still standing, because the bricks are real static colliders;
      - the POWER-UP CAPSULES, which fall under gravity and have to be caught - a genuinely
        physical gameplay object, resolved through onContact against the paddle;
      - the PADDLE itself, as a kinematic body, so it shoves both of the above out of the way
        rather than teleporting through them.
*/

//Our own input actions, numbered from INPUT_LAST like every other app's. The arrow keys are
//mapped to INPUT_MOVE_* by default for camera work, and a paddle is not a camera.
#define INPUT_BREAKOUT_LEFT         INPUT_LAST+1
#define INPUT_BREAKOUT_RIGHT        INPUT_LAST+2
#define INPUT_BREAKOUT_LAUNCH       INPUT_LAST+3
#define INPUT_BREAKOUT_RESTART      INPUT_LAST+4
#define INPUT_BREAKOUT_TOGGLE_UI    INPUT_LAST+5
#define INPUT_BREAKOUT_TOGGLE_MOUSE INPUT_LAST+6
#define INPUT_BREAKOUT_RELOAD_SHADER INPUT_LAST+7
//The analog one. A paddle is the one classic control that is genuinely better analog than
//digital, so the stick drives a scalar axis rather than a pair of fake key presses - and because
//it is a scalar axis, InputController::HoldAxis lets a script steer it exactly as a thumb does.
#define INPUT_BREAKOUT_STEER        INPUT_LAST+8

//Our own simulation commands, numbered from SIM_CMD_LAST. Restarting is intent arriving from
//OUTSIDE the simulation (a button, an MCP call), which is what the command queue is for.
//value[0] carries the seed, so a replayed restart deals the same level.
#define BREAKOUT_CMD_RESTART        SIM_CMD_LAST+0

//World-space labels. Numbered rather than named so one array and one update function cover them.
#define BREAKOUT_LABEL_SCORE        0
#define BREAKOUT_LABEL_LIVES        1
#define BREAKOUT_LABEL_LEVEL        2
#define BREAKOUT_LABEL_SHIELD       3
#define BREAKOUT_LABEL_BANNER       4
#define BREAKOUT_LABEL_COMBO        5
#define BREAKOUT_LABEL_COUNT        6

//How many brick-burst chunks one destroyed brick becomes, and how long they live.
#define BREAKOUT_DEBRIS_PER_BRICK   4
#define BREAKOUT_DEBRIS_TICKS       420
//A cap, because a multiball rally through a checkerboard can break a lot of bricks in a hurry and
//every chunk is a rigid body in the same world the capsules have to fall through.
#define BREAKOUT_MAX_DEBRIS         160

//A capsule that has not been caught by the time it is well below the paddle is gone.
#define BREAKOUT_CAPSULE_TICKS      900

/*
    Collision filtering, because "everything collides with everything" is wrong for this scene in
    one specific and fatal way: a power-up capsule that lands on top of a brick STAYS THERE. It is
    a dynamic body resting on a static one, the solver puts it to sleep, and a gameplay object the
    player was meant to chase sits motionless in the middle of the wall until its reap timer runs
    out. Measured, not predicted - see docs/breakout_findings.md 8.3.

    So a capsule is filtered down to the two things it is allowed to touch: the arena that keeps it
    inside the world, and the paddle that catches it. Debris keeps colliding with everything,
    because debris piling up against the bricks still standing is the whole reason the bricks have
    colliders at all.
*/
#define BREAKOUT_CAT_ARENA          0x01    //walls and the out-of-shot floor
#define BREAKOUT_CAT_BRICK          0x02
#define BREAKOUT_CAT_PADDLE         0x04
#define BREAKOUT_CAT_DEBRIS         0x08
#define BREAKOUT_CAT_CAPSULE        0x10

/*
    The shield's ripples: where the ball struck it and how long ago, in ticks.

    Three of them, because the shader takes them as three separate vec3 uniforms - see
    SetShieldUniforms and docs/breakout_findings.md 4.1 on why it cannot be an array.
*/
#define BREAKOUT_MAX_RIPPLES        3
struct BreakoutRipple{
    float x = 0.0f;         //world x along the shield
    float age = -1.0f;      //ticks since the impact; negative means this slot is idle
};

/*
    What the MCP tools are allowed to see.

    A tool handler runs on the server's own thread and holds NO lock, so reading the Field from one
    is a straight data race against the physics thread. RunSimulationTick fills this at the end of
    every tick under `snapshot_mutex` and every tool serves from it. The cost is one copy per tick;
    the benefit is that telemetry never disturbs the game it is measuring, which matters when the
    whole point is to play the game through the tools. Scene::AtTickBoundary is the alternative and
    is equally correct, but it stops the simulation for as long as the read takes, and a game being
    played by a script is read far more often than it is written.
*/
struct BreakoutSnapshot{
    uint64_t tick = 0;
    uint64_t game_ticks = 0;
    int   phase = BREAKOUT_PHASE_READY;
    int   score = 0;
    int   lives = 0;
    int   level = 1;
    int   combo = 0;
    int   best_combo = 0;
    int   bricks_left = 0;
    int   bricks_broken = 0;
    int   shield_saves = 0;
    int   powerups_caught = 0;
    float shield_charge = 1.0f;
    float paddle_x = 0.0f;
    float paddle_vx = 0.0f;
    float paddle_half_w = 0.0f;
    int   powerup_wide_ticks = 0;
    int   powerup_slow_ticks = 0;
    int   debris_bodies = 0;
    int   capsules = 0;
    bool  f_paused = false;
    uint32_t seed = 1;
    BreakoutBall balls[BREAKOUT_MAX_BALLS];
    std::vector<std::string> rows;      //the wall as ASCII, top row first

    /*
        The last paddle bounce, kept so the DESIGNED angle can be measured rather than asserted.

        The whole argument for integrating the ball by hand is that where it hits the paddle
        decides the angle it leaves at, and a solver cannot express that. A claim like that should
        be checkable from outside, and a bounce lasts one tick - far too short for a tool that
        polls. So the tick that produces one writes it down here and it stays until the next.
        tools/breakout_bot.py angles reads these pairs back out of a real rally.
    */
    uint64_t last_hit_tick = 0;
    float last_hit_offset = 0.0f;       //-1..+1 across the paddle
    float last_hit_angle = 0.0f;        //degrees from straight up, + to the right
    float last_hit_speed = 0.0f;

    //Where the falling power-up capsules are, so a scripted player can go and catch one - which
    //is also how the contact-callback path gets exercised without a human.
    struct CapsuleView{
        float x = 0.0f;
        float y = 0.0f;
        int   kind = 0;
    };
    std::vector<CapsuleView> capsule_views;
    //Filled only while the tunnelling probe is running - see breakout_ball_probe. Plain ints
    //here, not the atomics they are copied from: a snapshot has to be COPYABLE, and this whole
    //struct exists to be handed across a thread boundary by value.
    int   probe_ticks_left = 0;
    int   probe_escapes = 0;
    float probe_speed = 0.0f;
    int   probe_bricks = 0;             //bricks broken since the run started
    int   probe_overruns = 0;           //Field::resolution_overruns since the run started
};

class ApplicationBreakout : public Application, public rp3d::EventListener{
public:
    ApplicationBreakout();
    ~ApplicationBreakout();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
    void PreRender(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    //Physics thread, from inside rp3d's own step. Stages what it saw and does nothing else -
    //creating or destroying a body from in here walks rp3d's arrays while it is iterating them.
    void onContact(const rp3d::CollisionCallback::CallbackData& data) override;

    SoundSystem* soundsystem = NULL;

    //The rules. Touched ONLY from the physics thread (RunSimulationTick and the command handlers,
    //which also run there) and from DrawImGuiUI, which holds physics_mutex. Everything else goes
    //through `snapshot`.
    Field game;

    vec3 camera_target = vec3(11.0f,13.0f,0.0f);

private:
    //--- Setup, all on the render thread from Init() -------------------------------------------
    void BuildMaterials();
    void BuildArena();
    void BuildBricks();
    void BuildBallsAndPaddle();
    void BuildShield();
    void BuildTextLabels();
    void SetupFieldShadows();
    void SetupCamera();
    void SetupInput();
    void RegisterCommandHandlers();
#ifdef USE_MCP
    void RegisterMCPTools();
#endif

    //--- Per tick, physics thread --------------------------------------------------------------
    void GatherInput(BreakoutInput& out);
    void HandleEvents(const BreakoutEvents& events);
    void SyncBrickView();
    void SyncBallView();
    void SyncPaddleView();
    void SpawnBrickDebris(const BreakoutBrickHit& hit);
    void SpawnCapsule(float x, float y);
    void UpdateDebris();
    void UpdateCapsules();
    void UpdateRipples();
    void UpdateCameraShake();
    void UpdateBallProbe();
    void PublishSnapshot();
    void NewGame(uint32_t seed);

    //--- The custom shader ---------------------------------------------------------------------
    //Fires on the RENDER thread with the shield program already bound, once per frame. Reads
    //plain members the tick wrote; the render thread already holds physics_mutex while a frame is
    //drawn, so nothing here needs a lock of its own.
    void SetShieldUniforms();
    //Rebuilds the shield's fragment stage from disk and swaps it in at the index the mesh already
    //points at. Bound to a key and an ImGui button, because iterating on a shader through
    //rebuild-and-restart cycles is miserable.
    void ReloadShieldShader();

    //--- HUD -------------------------------------------------------------------------------------
#ifdef USE_IMGUI
    void RenderBreakoutHUD();
#endif

    //--- MCP, any thread -------------------------------------------------------------------------
    json BuildStateJson();

    //--- The view ---------------------------------------------------------------------------------
    /*
        One object per brick CELL, created once and hidden while the cell is empty - the same
        fixed-grid choice tetris/ApplicationTetris makes, and for the same reason: 88 hidden cubes
        cost nothing, the view is rebuilt from the array every tick so there is no incremental
        update to get wrong, and nothing churns the renderer's object list or the physics world
        mid-rally.
    */
    class Brick : public virtual Object{
    public:
        int col = 0;
        int row = 0;
    };
    Brick* brick_objects[BREAKOUT_ROWS][BREAKOUT_COLS] = {};

    Object* ball_objects[BREAKOUT_MAX_BALLS] = {};
    Object* paddle_object = NULL;
    Object* shield_object = NULL;
    PointLight* ball_light = NULL;
    Camera* field_camera = NULL;

    //Meshes generated once in Init and shared by pointer across every object that uses them.
    //Each takes a reference of its own, the same way AssetManager holds one for an asset's mesh:
    //a brick burst can destroy a lot of objects at once, and Object::DeleteMesh frees the mesh
    //when the last holder lets go.
    //One unit cube covers the bricks, the walls, the paddle, the debris and the capsules - they
    //differ only by scale and material, and sharing the mesh means the renderer emits ONE
    //instanced draw call for all of them rather than five.
    Mesh* unit_mesh = NULL;
    Mesh* ball_mesh = NULL;
    //The shield's quad has to be its own mesh: mesh_mode is a property of the MESH, so tagging a
    //shared one MESH_MODE_SHADER would drag every other object using it into the custom pass.
    Mesh* shield_mesh = NULL;

    //--- World-space text, render thread only -----------------------------------------------------
    /*
        Text is geometry here, not ImGui - core/TextMesh.h - so a label is an ordinary Object
        holding an ordinary Mesh, lit and shadowed like a brick, and it survives a screenshot.
        Rebuilt in PreRender rather than in the tick that changes the score: BuildTextMesh ends in
        glNamedBufferData and the physics thread may not touch GL. The numbers cross the thread
        boundary through `snapshot`; the strings are formatted on the far side of it.
    */
    struct BreakoutLabel{
        Object* object = NULL;
        //Refilled in place rather than replaced. Mesh has no destructor, so a label rebuilt by
        //delete/new would leak a VBO and a VAO per point scored.
        Mesh*   mesh = NULL;
        char    text[80] = {};
        float   scale = 1.0f;
        int     align = TEXT_ALIGN_LEFT;
    };
    void SetLabelText(int label_id, const char* text);
    GlyphSet glyphs;
    BreakoutLabel labels[BREAKOUT_LABEL_COUNT];

    //--- Materials, resolved once by name in Init -------------------------------------------------
    //Indices, because that is what Object::SetMaterialSlot takes.
    int material_brick_row[BREAKOUT_ROWS] = {};     //a hue per row, for the plain bricks
    int material_brick_tough = 0;
    int material_brick_armoured = 0;
    int material_brick_prize = 0;
    int material_brick_solid = 0;
    int material_ball = 0;
    int material_paddle = 0;
    int material_paddle_wide = 0;
    int material_wall = 0;
    int material_back = 0;
    int material_text = 0;
    int material_text_hot = 0;
    int material_capsule[POWERUP_COUNT] = {};

    //--- The shield's custom shader ------------------------------------------------------------
    Shader* shield_shader = NULL;
    int     shield_shader_index = -1;
    BreakoutRipple ripples[BREAKOUT_MAX_RIPPLES];
    int     next_ripple = 0;
    //Ticks, published by the tick for the shader to animate against. Deliberately not a real
    //clock: the effect then pauses and single-steps with the game, which is what makes it
    //possible to screenshot one exact frame of it.
    float   shield_time_ticks = 0.0f;
    float   shield_charge_view = 1.0f;
    float   shield_flare_view = 0.0f;
    //Where the nearest ball is, so the shield can bloom as it comes down. World XY.
    vec3    shield_focus = vec3(11.0f,12.0f,0.0f);

    //--- Physics garnish and gameplay ------------------------------------------------------------
    struct BreakoutDebris{
        Object* object = NULL;
        uint64_t reap_tick = 0;
    };
    std::vector<BreakoutDebris> debris;

    struct BreakoutCapsule{
        Object* object = NULL;
        int kind = 0;
        uint64_t reap_tick = 0;
    };
    std::vector<BreakoutCapsule> capsules;
    //Filled by onContact (inside rp3d's step), drained by RunSimulationTick. Ids rather than
    //pointers, because the object may be destroyed between the two.
    std::vector<objectid_t> staged_catches;

    //--- Input translation state (physics thread only) --------------------------------------------
    //Accumulated raw mouse movement, converted to world units. Read and cleared once per tick.
    float mouse_paddle_delta = 0.0f;
    bool  f_mouse_control = true;

    //The last paddle bounce, mirrored into the snapshot every tick - see BreakoutSnapshot.
    uint64_t last_hit_tick = 0;
    float last_hit_offset = 0.0f;
    float last_hit_angle = 0.0f;
    float last_hit_speed = 0.0f;

    //--- Camera shake (physics thread only) --------------------------------------------------------
    //Hand-rolled rather than MoveObjectOverTicks: a shake is a there-and-back, and a plain motion
    //request replaces the one in flight rather than queueing behind it.
    float shake_amount = 0.0f;
    int   shake_ticks = 0;

    //--- The tunnelling probe -----------------------------------------------------------------------
    /*
        The measurement behind the "what is the ball?" decision, kept in the app so it can be run
        again after any change to the sweep. It fires a ball at a known speed at the brick wall
        with the paddle parked under it, counts how many bricks it breaks over N ticks, and counts
        ESCAPES - ticks on which a ball ended up outside the arena, which is what a failed
        collision test looks like from outside. Driven by breakout_ball_probe over MCP.
    */
    //Written by the MCP thread that starts a run, counted down and added to by the physics
    //thread. Atomic because those really are two threads: the probe is a measurement harness
    //bolted onto the side of the simulation, deliberately outside the command queue, because a
    //recording of a run should not contain the instrument that was watching it.
    std::atomic<int> probe_ticks_left{0};
    std::atomic<int> probe_escapes{0};
    float probe_speed = 0.0f;   //written before probe_ticks_left is raised, read only after
    //Baselines taken when a run starts, so the counters below report THIS run rather than the
    //whole session.
    int probe_bricks_at_start = 0;
    int probe_overruns_at_start = 0;

    //--- Cross-thread ---------------------------------------------------------------------------
    BreakoutSnapshot snapshot;
    std::mutex snapshot_mutex;
    uint32_t current_seed = 1;
    uint32_t next_auto_seed = 2;

    //Raised by the F5 key (physics thread) and by the HUD button (render thread), serviced in
    //PreRender. Compiling a shader is GL work, and the physics thread may not touch the context -
    //so the key cannot do the reload itself, only ask for one.
    std::atomic<bool> f_shader_reload_requested{false};

    bool f_show_engine_ui = false;  //F1. Off by default so the game screen is the game.
    bool f_sound_enabled = true;
    bool f_debris_enabled = true;
};

#endif
