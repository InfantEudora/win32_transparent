#ifndef _APPLICATION_BOMBER_H_
#define _APPLICATION_BOMBER_H_

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Application.h"
#include "Texture.h"
#include "Maze.h"

/*
    bomber - a bomberman built on this engine, and a bench for the volumetric blast that drives it.

    Stage 1: a 16x16 field laid out from apps/bomber/assets/meshes/bomber_assets.glb, a character
    that walks it, and a bomb that can be dropped, burns a fuse and goes off once.

    --- WHERE THE RULES ARE ----------------------------------------------------------------------
    In Maze, and NOT here. That class holds the grid, the walker, the fuse and the blast's reach,
    in tiles and ticks, with no engine type in its header - the same split breakout keeps between
    Field and its application. This class is the VIEW plus the wiring: it turns the tile array into
    meshes, three input devices into one intent, and the blast's arm lengths into shader uniforms.

    The test for which side a change belongs on: if running it twice for one tick would change the
    outcome, it is a rule and it goes in Maze.

    --- THE BLAST IS DRAWN TWO WAYS, AND YOU CAN HAVE EITHER OR BOTH -----------------------------
    `f_draw_tiles` renders the cross as ONE VOLUME PER TILE, the way the game is built; `f_draw_cross`
    renders it as a SINGLE volume whose density is shaped like the cross. Both read the same blast
    - same origin, same arm lengths, same clock - so turning both on draws one blast twice and the
    difference on screen is only the technique. Tiles are on by default: they look better, they
    follow the maze for free, and their one weakness is that overlapping instances do not
    depth-sort against each other, which the engine has no answer for yet and which is a known and
    accepted cost rather than a bug to hunt. See the long note at the top of
    shaders/bomber_explosion.frag.

    --- THE BLAST CLOCK IS IN TICKS ---------------------------------------------------------------
    Maze::blast_ticks counts simulation ticks, so sim_pause freezes the fireball mid-expansion,
    sim_step advances it by an exact number of frames, and a screenshot of frame N of a blast is
    reproducible. A wall clock would make all three impossible, and a volumetric effect is
    precisely the kind of thing that has to be looked at a frame at a time.

    --- THREADING ---------------------------------------------------------------------------------
      - Init()               render thread, once. All GL, all mesh building, all registration.
      - RunSimulationTick()  physics thread, once per tick that RUNS, physics_mutex held. Maze::Tick
                             and the view sync live here, so the whole game pauses and steps.
      - UpdateView()         physics thread, every pass including paused ones. Camera and the F5
                             key only - nothing a tick will read.
      - PreRender()          render thread, top of every frame. Where a shader reload happens.
      - PushBlastUniforms()  render thread, inside the custom-material pass with the program bound.
                             Reads the published view state, never the live simulation.
      - DrawImGuiUI()        render thread, physics_mutex held. Never waits on the sim.
      - MCP handlers         their own thread, NO lock. They submit commands and input events.
*/

//Our own input actions, numbered from INPUT_LAST like every other app's. The arrow keys are mapped
//to INPUT_MOVE_* by default for camera work, and a walker is not a camera.
#define INPUT_BOMBER_NORTH          INPUT_LAST+1
#define INPUT_BOMBER_SOUTH          INPUT_LAST+2
#define INPUT_BOMBER_WEST           INPUT_LAST+3
#define INPUT_BOMBER_EAST           INPUT_LAST+4
#define INPUT_BOMBER_DROP           INPUT_LAST+5
#define INPUT_BOMBER_RESTART        INPUT_LAST+6
#define INPUT_BOMBER_RELOAD_SHADER  INPUT_LAST+7

/*
    Our own simulation commands, numbered from SIM_CMD_LAST.

    Both are intent arriving from OUTSIDE the simulation - an MCP call, a panel button - so they go
    on the command queue rather than being done on the caller's thread. The handler runs on the
    physics thread at the top of a tick with physics_mutex held, which is the only place it is safe
    to throw a maze away or light a fuse. The KEYS do not use this path: they are already read on
    the physics thread inside the tick.
*/
#define BOMBER_CMD_RESTART          SIM_CMD_LAST+0
//Drops a bomb on the character's tile with a one-tick fuse, so a blast can be looked at without
//waiting out MAZE_FUSE_TICKS. value[0] is ignored.
#define BOMBER_CMD_DETONATE         SIM_CMD_LAST+1

//--- the art --------------------------------------------------------------------------------------
/*
    One grid cell in world units.

    1.0 because THE ART SAYS SO: every tile in bomber_assets.glb measures 1.0 x 1.0 in X and Z. It
    is not a free parameter and changing it would leave visible seams between tiles rather than
    scaling the board.
*/
#define BOMBER_CELL_SIZE            1.0f

/*
    Height the flame is centred at, in world units.

    Half a wall (wall_brick is 0.88 tall), so a blast reads as being at the height of the things it
    is going to destroy rather than as a fire on the floor.
*/
#define BOMBER_FLAME_HEIGHT         0.44f

/*
    World size of a blast volume's box, per mode.

    A TILE box is two cells - a cell of flame plus the margin its billows need, since the flame has
    to bulge past the tile it belongs to or the cross looks like a row of beads.

    The CROSS box has to contain the whole plus: MAZE_BLAST_RANGE cells each way, plus the flame's
    radius, plus the rise. It is the price of the single volume and it is worth being honest about:
    most of what it marches is empty, which is why BOMBER_CROSS_VIEW_STEPS_MUL is there - the same
    step COUNT over a longer interval is a coarser sample, and the arms go stripy if that is not
    paid for.

    Both are UNIFORM cubes on purpose. The shader's shape maths is done in object space, so a
    non-uniform box would stop an object-space length being a world length - the trap
    raymarch_volume.frag documents at its light march. Keeping them cubes is what lets
    PushBlastUniforms convert a world-unit knob into object units by dividing by one number.
*/
#define BOMBER_TILE_BOX             (2.0f * BOMBER_CELL_SIZE)
#define BOMBER_CROSS_BOX            (2.0f * MAZE_BLAST_RANGE * BOMBER_CELL_SIZE + 2.0f)
#define BOMBER_CROSS_VIEW_STEPS_MUL 2

//Centre tile plus one line of tiles per direction: the most volumes a per-tile blast can need.
#define BOMBER_MAX_BLAST_TILES      (1 + 4 * MAZE_BLAST_RANGE)

//Resolution of the 3D noise the blast samples, and how many worley cells the lowest-frequency
//channel gets. 96^3 RGBA8 is 3.4 MB - smaller than the ship's 128 because a fireball is looked at
//for a second and a half, not stared into.
#define BOMBER_NOISE_RESOLUTION     96
#define BOMBER_NOISE_CELLS          4

/*
    Per-mode shape settings that are NOT knobs, because they are not matters of taste.

    A fireball burns at its front and drags exhausted gas behind it, so it is a hollow shell and its
    front is the hot part. A flame running down a corridor is a jet: solid, and hottest along its
    axis. Making these sliders would only offer the chance to set one of them wrong.
*/
#define BOMBER_TILE_SHELL           0.45f   //fraction of the radius that is burning
#define BOMBER_TILE_CORE_HEAT       0.0f    //0 = hottest at the front
#define BOMBER_CROSS_SHELL          1.00f   //solid: a hollow arm reads as a pipe
#define BOMBER_CROSS_CORE_HEAT      1.0f    //1 = hottest along the skeleton

/*
    One tunable of the blast effect, as both the ImGui panel and the bomber_set MCP tool see it.

    ONE TABLE DRIVES BOTH, which is the whole reason this struct exists: a new knob is one line in
    BuildKnobTable and it immediately has a slider, a name MCP can set it by, and an entry in
    bomber_state. Two lists would drift the first time somebody added a slider and forgot the tool,
    and the failure - a knob that works by hand but not from a script - is one that only shows up
    when the script is the only thing looking.

    Exactly one of `fvalue` and `ivalue` is set; which one says how the value travels.
*/
struct BomberKnob{
    const char* name = NULL;
    float* fvalue = NULL;
    int*   ivalue = NULL;
    float  min_value = 0.0f;
    float  max_value = 1.0f;
    const char* help = NULL;
};

class ApplicationBomber : public Application{
public:
    ApplicationBomber();

    void Init(void) override;
    void PreRender(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif

    //Lets the core camera_get/camera_set tools see and move the point the camera orbits.
    vec3* GetCameraTargetPtr() override { return &camera_target; }

private:
    //--- setup --------------------------------------------------------------------------------
    void SetupInput();
    void RegisterCommandHandlers();
    void BuildLighting();
    void LoadAssets();
    /*
        Builds the enemies' skinned skeletons and loads their clips onto them.

        RENDER THREAD, ONCE, from Init - it uploads a mesh, and RebuildField (which is where the
        enemies are actually placed) runs on the physics thread. See the note at the definition.
    */
    void BuildEnemies();
    /*
        Destroys whatever field is standing and builds the one Maze currently describes.

        PHYSICS THREAD, from inside the restart command handler - which is the only place it is
        safe, and the same place ApplicationBreakout::NewGame does its own clear-out. The handler
        runs at the top of a tick with physics_mutex held, so the render thread is not walking the
        object list while objects are being destroyed and added. Nothing in here touches GL:
        AssetManager::GetObjectFromAsset only shares a mesh that is already on the card.
    */
    void RebuildField();
    /*
        One object from a loaded GLB node, placed on a cell.

        `y_offset` comes from the node's own translation - see the note in LoadAssets. `yaw` is used
        when `f_random_yaw` is false; when it is true the piece takes one of four quarter turns
        derived from the cell, which is variety for free on anything square and symmetric.
    */
    Object* AddCellObject(const char* asset_name, int cx, int cz, float y_offset,
                          float yaw, bool f_random_yaw);
    /*
        Brings the view back in step with the board after a block was destroyed or an item taken.

        PHYSICS THREAD, from SyncView, and it only ever calls SetVisibility - see the note on
        cell_block for why that is the entire mechanism.
    */
    void RefreshCells();

    //World centre of grid cell (cx,cz), at y=0. The board is centred on the origin so the orbit
    //camera has something symmetric to turn around.
    vec3 CellCentre(int cx, int cz) const;

    //--- the blast volumes ----------------------------------------------------------------------
    //Runs shaders/noise3d.comp once to fill blast_noise with tileable 3D worley. Render thread -
    //it is a compute dispatch. Shared with the ship app; see shared_assets/shaders/noise3d.comp.
    void BuildBlastNoise();
    /*
        Builds BOTH shaders and both sets of volumes.

        The two Shader objects are compiled FROM THE SAME .frag, differing only in the blast_mode
        uniform their callback sets. Two programs are needed rather than one because
        Shader::uniform_callback is per-Shader and is called once per pass with that program bound,
        while both modes can be on screen at the same time - one program could only be told one
        mode per frame. Two meshes for the same reason: mesh_mode/custom_shader_index are
        properties of the MESH, and that index is how a volume says which program draws it.
    */
    void BuildExplosion();
    //A fresh unshared unit cube tagged MESH_MODE_SHADER, bound to `shader_index`.
    Mesh* BuildBlastCube(int shader_index);
    //The two Shader::uniform_callbacks. They exist as separate functions only because std::bind
    //needs a target; both are PushBlastUniforms with their own mode's constants.
    void SetTileUniforms();
    void SetCrossUniforms();
    /*
        Pushes everything both modes share, converting each world-unit knob into the object units
        the shader works in by dividing by `box_world`. RENDER THREAD, program already bound.

        The conversion lives here, at the boundary, rather than in the shader: the knobs are then
        in metres and tiles - which is what they are worth talking about in and what the MCP tool
        reports - while the march stays in the space ray_box_dst needs.
    */
    void PushBlastUniforms(Shader* shader, int mode, float box_world, bool f_enabled);
    //--- the water tiles ------------------------------------------------------------------------
    /*
        Builds shaders/bomber_water.frag and points tile_water's SHARED mesh at it.

        Tagging the shared mesh is what makes one call reach every water tile on the board -
        GetObjectFromAsset hands out objects that share their asset's mesh - and it is also what
        makes the tag safe, since tile_water is the only asset using that mesh. The same line
        against tile_grass would turn every floor in the field into water.

        CALLED BEFORE BuildExplosion, and that is not tidiness. Renderer::CustomShaderPass draws
        custom shaders in REGISTRATION ORDER; the water is opaque and the blast is not, so
        registering the water second would paint the tiles over the fire.

        WHAT IT WOULD HAVE COST, and why it does not: a MESH_MODE_SHADER mesh does not go through
        the deferred pass, so water tiles would stop being pickable AND would vanish from the
        G-BUFFER - which the blast clamps its raymarch to. Since Maze::BlocksBlast lets flame run
        straight over water on purpose, that showed up as a fireball spilling below the waterline
        on exactly the tiles a blast is allowed to cross. Measured at 2.6% of the frame against
        the same blast over grass, so it is a real artifact and not a worry. BuildWater sets
        Shader::f_writes_gbuffer, which puts the tile's SHAPE back in the deferred pass while
        leaving its colour custom, and both halves of the problem go with it.
    */
    void BuildWater();
    //Shader::uniform_callback for it: the four knobs plus the clock. RENDER THREAD, program bound.
    void SetWaterUniforms();

    //Recompiles shaders/bomber_explosion.frag into BOTH programs, and the water shader with them.
    //RENDER THREAD ONLY - serviced from PreRender, because the key that asks for it is read on
    //the physics thread.
    void ReloadExplosionShader();

    //--- the loop -----------------------------------------------------------------------------
    //Turns the held keys into the one direction Maze wants. See its definition for the
    //keep-what-you-have rule, which is what makes cornering feel right.
    int ReadDirection();
    //Moves the character, the bomb and the blast volumes to where Maze says they are, and
    //publishes the blast clock for the render thread. PHYSICS THREAD, end of the tick.
    void SyncView();

    void BuildKnobTable();
    BomberKnob* FindKnob(const std::string& name);

    //--- MCP ----------------------------------------------------------------------------------
#ifdef USE_MCP
    void RegisterMCPTools();
#endif
    json StateJson();
    /*
        The board as text: one row per line, plus a second grid saying which zone style each cell
        came from. Shared by bomber_state and bomber_restart rather than living inside one of them,
        because both advertise it - and the first version had it inline in bomber_state only, so
        bomber_restart's schema promised a map it never returned.
    */
    json MapJson();

    //--- UI -----------------------------------------------------------------------------------
#ifdef USE_IMGUI
    void RenderBomberPanel();
#endif

    //--- the game -----------------------------------------------------------------------------
    Maze maze;
    //Seed of the field currently standing, and the one the next automatic restart will use. The
    //seed travels IN the restart command, so a replayed restart lays out the same maze.
    uint32_t current_seed = 1;
    uint32_t next_auto_seed = 2;

    //--- scene --------------------------------------------------------------------------------
    vec3 camera_target = vec3(0,0,0);
    DirectionalLight* sun = NULL;
    /*
        Everything the field is made of: floors, blocks, decoration and buried pickups, one Object
        each. Kept as a flat list purely so RebuildField can destroy them all.

        `cell_block` and `cell_item` INDEX INTO IT BY CELL, which an earlier version of this app
        deliberately did not do: it rebuilt the field wholesale, on the grounds that a bomberman
        board only changes on a restart. Destructible blocks ended that. A hedge burning is a change
        to one cell, and throwing four hundred objects away to express it would be absurd.

        WHAT IS INDEXED IS NEVER CREATED OR DESTROYED MID-GAME - only shown and hidden. A destroyed
        block is its object turned invisible; a revealed pickup is its object turned visible. So the
        object list is fixed for the life of a field and no allocation, no deletion and no
        Scene::AddObject happens while the game is running - which is the same call BuildExplosion
        makes about the blast volumes, for the same reason.

        It works because a cell's FLOOR never changes: every destructible tile turns into GRASS when
        it goes, and the floor under a soft block is already the grass tile. The floor-variety loop
        at the end of Maze::NewGame skips soft blocks precisely so that stays true.
    */
    std::vector<Object*> field_objects;
    Object* cell_block[MAZE_H][MAZE_W];
    Object* cell_item[MAZE_H][MAZE_W];
    Object* character = NULL;
    Object* bomb = NULL;
    /*
        One SKINNED, ANIMATED skeleton per enemy the board can ever hold - MAZE_MAX_ENEMIES of them,
        built once by BuildEnemies on the render thread and never rebuilt.

        Held as Object* rather than Skeleton* because everything this class does to them afterwards
        - SetPosition, SetVisibility, SwitchToAnimation - is Object's. The skeleton half only
        matters while they are being built.
    */
    std::vector<Object*> enemy_objects;
    //Y offsets taken from each GLB node's own translation - see LoadAssets for why.
    float character_y = 0.0f;
    float bomb_y = 0.0f;
    float enemy_y = 0.0f;
    /*
        The last Maze::field_version this view was brought up to date with.

        The whole of the incremental update: when it differs from the maze's, RefreshCells walks the
        board once and fixes what is visible. One integer comparison per tick buys that, instead of
        256 SetVisibility calls every tick or a list of pending changes to keep in step with the
        rules - and it cannot drift, because the refresh reads the rules rather than a record of
        what it was told about them.
    */
    uint32_t drawn_field_version = 0;

    //--- the blast volumes ------------------------------------------------------------------------
    Shader* tile_shader = NULL;
    int     tile_shader_index = -1;
    Mesh*   tile_mesh = NULL;
    Shader* cross_shader = NULL;
    int     cross_shader_index = -1;
    Mesh*   cross_mesh = NULL;
    //The per-tile renderer's volumes, moved onto the blast's tiles when one goes off. Created once
    //and never destroyed: a blast is frequent and object churn on the physics thread while the
    //render thread walks the list is exactly what Scene::AddObject warns about.
    std::vector<Object*> blast_tiles;
    Object* blast_cross = NULL;
    //The light the fire throws on the field around it.
    PointLight* blast_light = NULL;

    Texture* blast_noise = NULL;
    Shader*  blast_noise_shader = NULL;

    //--- the water tiles --------------------------------------------------------------------------
    //One shader, no mesh and no objects of its own: it draws the tile_water objects the field
    //builder already makes, because BuildWater tagged the mesh they share.
    Shader* water_shader = NULL;
    int     water_shader_index = -1;

    /*
        --- the blast clock, view side ---------------------------------------------------------
        Written by the physics thread at the end of SyncView, read by the render thread in
        PushBlastUniforms. Plain values crossing a thread boundary with no lock: a torn read is one
        frame of a fireball being one tick stale in one component, which is not worth a mutex - but
        it is worth saying out loud rather than leaving someone to wonder, which is the same call
        apps/testfx makes about its mouse.

        A NEGATIVE age means no blast is live. The shader takes that as its early-out, so nothing
        has to hide the volumes - a visibility flag flipped from the physics thread would be a
        write the render thread reads mid-frame, for no gain over a uniform it reads anyway.
    */
    float blast_age_view = -1.0f;
    float blast_seed_view = 0.0f;
    vec3  blast_origin_view = vec3(0,0,0);
    vec4  blast_arms_view = vec4(0,0,0,0);

    //Which renderer draws the blast. Both may be on at once, which draws it twice - see the note
    //at the top of this file.
    bool f_draw_tiles = true;
    bool f_draw_cross = false;

    /*
        --- the knobs ---------------------------------------------------------------------------
        Written by the panel (render thread) and by bomber_set (an MCP thread), read by
        PushBlastUniforms (render thread), so all three go through knob_mutex. The panel could get
        away without it, but bomber_set cannot, and one rule for the table is cheaper to keep true
        than two.

        SHARED BY BOTH MODES, which is the point: a difference on screen has to be a difference
        between the two ways of drawing the blast and not between two sets of settings. The handful
        of values that genuinely differ are the BOMBER_TILE_SHELL / BOMBER_CROSS_SHELL pair and the
        two CORE_HEAT constants above, and they are constants precisely so they cannot drift apart
        by accident.

        Lengths are in WORLD UNITS here and converted per volume in PushBlastUniforms.
    */
    std::mutex knob_mutex;
    std::vector<BomberKnob> knobs;

    float blast_life = (float)MAZE_BLAST_TICKS;
    //World radius of the flame tube. 0.75 makes a tile's fireball 1.5 across against the 1.0 cell
    //it belongs to, so neighbouring tiles overlap by half a cell and the cross reads as one
    //connected blast rather than as beads - a tile's flame is still GROWING when its neighbour
    //lights, so anything near the cell pitch leaves gaps exactly where the cross should join.
    float blast_radius = 0.75f;
    float rim_softness = 0.22f;
    float turbulence = 0.85f;
    float noise_scale = 1.20f;
    float outflow = 0.35f;
    float rise = 0.40f;             //world units over a whole life
    //Per WORLD unit, so halving every length in the app doubles what this has to be to keep the
    //same optical depth through the flame. That is why it is 6 here and was 3 at a 2.0 cell.
    float blast_density = 6.0f;
    //Ticks each ring of tiles waits behind the one nearer the bomb. Per-tile renderer only - the
    //single cross volume grows its arms continuously instead.
    float tile_delay = 3.0f;
    float heat = 0.90f;
    float emission_strength = 0.55f;
    float smoke_albedo = 0.55f;
    //A SCALE on the sun's brightness, not a replacement for it. Small because a volume integrates
    //radiance over density and step length while a surface runs it through a BRDF and an NdotL -
    //see the long note at this uniform in the .frag, which is where the number was earned.
    float sun_intensity = 0.12f;
    float light_absorption = 1.0f;
    int   num_view_steps = 40;
    int   num_light_steps = 4;
    float light_falloff = 2.0f;
    float max_radiance = 10.0f;
    //How much light a sample must be able to scatter before the shader marches towards the lights
    //for it. The single biggest saving in the effect and the one most worth being able to turn
    //off: 0 restores the pre-optimisation picture exactly. See scatter_cutoff in the .frag.
    float scatter_cutoff = 0.002f;
    /*
        Window pixels across one pixel of the blast, and therefore how much of the window's
        resolution the march is actually paying for. 1 is off.

        ONE NUMBER FOR BOTH, deliberately - see Renderer::SetCustomShaderScale. 2 is a quarter of
        the fragments and 2x2 blocks; 4 is a sixteenth and 4x4. It is a knob rather than a constant
        because "how blocky should the fire be" is a judgement about how it sits next to the
        board's own art, and that is a thing to slide, not to argue about.

        Read on the RENDER THREAD by PreRender, written by the panel and by bomber_set. A plain
        int either side of that boundary for the same reason the blast clock is - see the note on
        blast_age_view.
    */
    int   blast_pixel_scale = 2;

    /*
        --- the water ------------------------------------------------------------------------------
        Four knobs, and all four are about the PATTERN. THE COLOUR IS NOT HERE ON PURPOSE: it comes
        from tile_water's own material, so the hue is an art decision and lives in the .glb next to
        the rest of the board's palette. A colour picker in the debug panel would be a second place
        to set something the asset already says, and the two would disagree the first time anyone
        re-exported.

        Each is explained at its uniform in shaders/bomber_water.frag. The ranges in BuildKnobTable
        are what is worth exploring rather than what is legal.
    */
    float water_scale = 4.0f;
    float water_speed = 1.0f;
    float water_depth = 0.55f;
    float caustic_width = 0.18f;
    /*
        The water's clock, view side: simulation ticks, published by SyncView and read by
        SetWaterUniforms on the render thread. The same unlocked hand-off as blast_age_view and for
        the same reasons - see the note there.

        IN TICKS, so the water freezes under sim_pause and advances exactly one frame per sim_step.
        A wall clock here would make a screenshot of the board unreproducible and would put the
        water on a different clock from the fire, which is the one thing you cannot have the moment
        you film a blast going off next to a pond.
    */
    float water_time_view = 0.0f;
    //0 off, 1 the marched interval, 2 the G-buffer the shader reads. Matches f_show_box in
    //shaders/bomber_explosion.frag.
    int   debug_view = 0;

    //Peak brightness and reach of the blast light. default.frag lights surfaces with
    //brightness/distance, so 4 puts about 4 on the wall one cell away: bright enough to go orange,
    //short of blowing it white.
    float blast_light_brightness = 4.0f;
    float blast_light_radius = 0.8f;

    /*
        --- the input lock -------------------------------------------------------------------------
        Ignores the keyboard, the gamepad and the mouse, leaving only input that arrives through
        MCP. FOR AUTOMATED TESTING, and it exists because the app is on screen while it is being
        driven: a hand on the mouse moves the camera between a camera_set and the screenshot that
        follows it, which silently reframes a comparison, and a stray key walks the character out
        from under a scripted test. Both produce a plausible-looking picture of the wrong thing,
        which is the worst kind of wrong.

        WHAT IT CANNOT DO, said out loud: InputController mixes scripted holds into the same
        KeyState as real keys, so nothing downstream can tell them apart. The lock therefore accepts
        the game's controls only while InputController::HasSyntheticHolds() says a scripted hold is
        running - so a human pressing a key during that exact window still gets through. That is
        good enough for its purpose (nobody is playing during a scripted test) and the honest fix
        would be in core, not here: see engine_notes.md.

        The camera and the shader-reload key have no such loophole - they are simply switched off.
    */
    bool f_lock_human_input = false;

    //--- camera ---------------------------------------------------------------------------------
    /*
        The wheel, accumulated and bled off rather than applied as it arrives.

        A member rather than the function-static ApplicationShip uses: same behaviour, but a static
        inside UpdateView is shared by every instance of the class and survives a scene reload,
        which is two surprises waiting for whoever adds a second camera.
    */
    float mouse_wheel_sum = 0.0f;

    //--- requests across the thread boundary ---------------------------------------------------
    //A shader reload is GL work, so the F5 key and the panel button only raise this and PreRender
    //acts on it. Same shape as ApplicationBreakout's.
    std::atomic<bool> f_shader_reload_requested{false};
    //What the last reload's compiler said, for the panel and for bomber_state.
    std::mutex reload_mutex;
    std::string reload_log;
    bool f_shader_ok = true;
};

#endif
