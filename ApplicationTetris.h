#ifndef _APPLICATION_TETRIS_H_
#define _APPLICATION_TETRIS_H_

#include <mutex>
#include <vector>
#include <string>

#include "Application.h"
#include "SoundSystem.h"
#include "Playfield.h"

/*
    Tetris on this engine.

    The split is deliberate and it is the same one the rest of the repo uses: tetris/Playfield
    holds the RULES (a plain array, tick counters, no engine types at all) and this class is the
    VIEW plus the wiring - it turns held keys into one-shot actions, turns the board array into
    cubes, and exposes the whole thing over MCP so the game can be played without a human.

    Threading, since everything here depends on it:
      - Init()          render thread, once. All GL, all asset loading, all registration.
      - RunLogic()      physics thread, every tick, physics_mutex held. The game lives here.
      - DrawImGuiUI()   render thread, physics_mutex held. Reads simulation state; never waits.
      - MCP handlers    their own thread, NO lock. They read `snapshot` (below) and submit
                        input events or SimCommands. They never touch the scene.
*/

//Our own input actions, numbered from INPUT_LAST like every other app's. Left/right/soft drop get
//their own codes rather than reusing INPUT_MOVE_*: the arrow keys are mapped to those by default
//for camera movement, and a Tetris board is not a camera.
#define INPUT_TETRIS_LEFT           INPUT_LAST+1
#define INPUT_TETRIS_RIGHT          INPUT_LAST+2
#define INPUT_TETRIS_SOFT_DROP      INPUT_LAST+3
#define INPUT_TETRIS_HARD_DROP      INPUT_LAST+4
#define INPUT_TETRIS_ROTATE_CW      INPUT_LAST+5
#define INPUT_TETRIS_ROTATE_CCW     INPUT_LAST+6
#define INPUT_TETRIS_HOLD           INPUT_LAST+7
#define INPUT_TETRIS_RESTART        INPUT_LAST+8
#define INPUT_TETRIS_TOGGLE_UI      INPUT_LAST+9

//Our own simulation commands, numbered from SIM_CMD_LAST. Restarting is intent arriving from
//OUTSIDE the simulation (a button, an MCP call), which is exactly what the command queue is for.
//value[0] carries the seed, so a replayed restart deals the same pieces.
#define TETRIS_CMD_RESTART          SIM_CMD_LAST+0

//Auto-repeat, in ticks. DAS is the pause before a held left/right starts repeating, ARR the gap
//between repeats. Ticks, never milliseconds - see Scene::GetPhysicsTick.
#define TETRIS_DAS_TICKS            10
#define TETRIS_ARR_TICKS            2

//How long a piece takes to slide to a new column, in ticks. Two is enough to stop the board
//looking like it is teleporting and short enough that it never lags behind the input.
#define TETRIS_SLIDE_TICKS          2

//Debris cubes spawned per cleared cell live this long before they are reaped.
#define TETRIS_DEBRIS_LIFETIME_TICKS 260

/*
    What the MCP tools are allowed to see.

    An MCP tool handler runs on the server's own thread and holds NO lock, so reading the scene
    (or the Playfield) from one is a straight data race against the physics thread. There is no
    read-side equivalent of Scene::SubmitCommand in the engine, so the choice is: pause the sim
    and step it (what the tank app does), or publish a snapshot. This app publishes: RunLogic
    fills this struct at the end of every tick under `snapshot_mutex`, and every tool serves from
    it. The cost is one copy per tick; the benefit is that telemetry never disturbs the game it
    is measuring, which matters when the point is to play the game through the tools.
*/
struct TetrisSnapshot{
    uint64_t tick = 0;
    uint64_t game_ticks = 0;
    int score = 0;
    int lines = 0;
    int level = 1;
    int phase = TETRIS_PHASE_SPAWN;
    int pieces_placed = 0;
    int piece_type = -1;
    int piece_rotation = 0;
    int piece_x = 0;
    int piece_y = 0;
    int ghost_y = 0;
    int hold_type = -1;
    bool f_hold_used = false;
    bool f_paused = false;
    int debris_count = 0;
    uint32_t seed = 1;
    std::vector<int> next_types;
    std::vector<std::string> rows;      //board as ASCII, top row first
};

class ApplicationTetris : public Application{
public:
    ApplicationTetris();
    ~ApplicationTetris();

    void Init(void) override;
    void RunLogic(void) override;
    void DrawImGuiUI(void) override;
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    SoundSystem* soundsystem = NULL;

    //The rules. Touched ONLY from the physics thread (RunLogic and the command handlers, which
    //also run there) - everything else goes through `snapshot`.
    Playfield game;

    vec3 camera_target = vec3(6.0f,9.5f,0.0f);

private:
    //--- Setup, all on the render thread from Init() ---------------------------------------
    void BuildMaterials();
    void BuildWell();
    void BuildViewObjects();
    void SetupCamera();
    void SetupInput();
    void RegisterCommandHandlers();
    void RegisterMCPTools();

    //--- Per tick, physics thread ----------------------------------------------------------
    //Held keys -> one action per tick. DAS/ARR live here rather than in Playfield so the rules
    //never have to ask how long something has been held.
    void GatherInput(TetrisInput& out);
    void HandleEvents(const TetrisEvents& events);
    void SyncBoardView();
    void SyncPieceView();
    void SyncPreviewView();
    void StartCollapseAnimation();
    void SpawnClearDebris();
    void UpdateDebris();
    void UpdateCameraShake();
    void PublishSnapshot();
    void NewGame(uint32_t seed);

    //--- HUD ---------------------------------------------------------------------------------
    void RenderTetrisHUD();

    //--- MCP, any thread ---------------------------------------------------------------------
    //Serialises `snapshot` (never the live game) into the shape every tool returns. Safe from an
    //MCP thread, which is the only kind that calls it.
    json BuildStateJson();

    //--- The view ----------------------------------------------------------------------------
    //One object per board cell, created once and hidden when its cell is empty. A fixed grid
    //rather than objects created and destroyed as pieces land: 200 cubes cost nothing, and
    //creating/destroying 4 objects every second or so would churn the renderer's object list and
    //the physics world for no benefit.
    Object* cell_objects[TETRIS_BOARD_H][TETRIS_BOARD_W] = {};
    Object* piece_objects[4] = {};      //the active piece
    Object* ghost_objects[4] = {};      //where it would land
    Object* hold_objects[4] = {};
    Object* next_objects[TETRIS_NEXT_QUEUE_SHOWN][4] = {};

    //Last position each active-piece cube was asked to move to, so a slide is only requested when
    //the target actually changes (Scene::MoveObjectOverTicks replaces an in-flight motion, so
    //re-requesting the same one every tick would restart it every tick and it would never finish).
    vec3 piece_cell_targets[4] = {};
    int last_piece_type = -1;

    //While the collapse animation is running the board view is driven by MoveObjectOverTicks
    //instead of by the array, so SyncBoardView has to keep its hands off it.
    bool f_collapse_animating = false;

    //Materials, resolved once by name in Init. Indices, because that is what Object::material_slot
    //takes - and because Renderer::AddMaterial cannot be trusted to return the right one (see
    //docs/tetris_findings.md).
    int material_piece[TETROMINO_COUNT] = {};
    int material_ghost = 0;
    int material_flash = 0;
    int material_frame = 0;
    int material_back = 0;

    //--- Physics garnish ---------------------------------------------------------------------
    struct TetrisDebris{
        Object* object = NULL;
        uint64_t reap_tick = 0;
    };
    std::vector<TetrisDebris> debris;

    //--- Input translation state (physics thread only) ---------------------------------------
    int das_ticks_left = 0;         //ticks until a held direction starts repeating
    int arr_ticks_left = 0;
    int das_direction = 0;          //-1, 0, +1: which way the held repeat is going

    //--- Camera shake (physics thread only) --------------------------------------------------
    //Hand-rolled rather than MoveObjectOverTicks: a shake is a there-and-back, and a motion
    //request replaces the one in flight rather than queueing behind it.
    float shake_amount = 0.0f;
    int shake_ticks = 0;

    //--- Cross-thread ------------------------------------------------------------------------
    TetrisSnapshot snapshot;
    std::mutex snapshot_mutex;
    uint32_t current_seed = 1;
    //Bumped by the restart command so a fresh game gets fresh pieces when nobody names a seed.
    uint32_t next_auto_seed = 2;

    bool f_show_engine_ui = false;  //F1. Off by default so the game screen is the game.
    bool f_sound_enabled = true;
    bool f_debris_enabled = true;
};

#endif
