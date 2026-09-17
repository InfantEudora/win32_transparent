#ifndef _APPLICATION_BOMBER_H_
#define _APPLICATION_BOMBER_H_

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Application.h"
#include "Texture.h"
#include "Maze.h"
#include "Hallway.h"

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
/*
    Gives the player the exit key, or takes it back. value[0] is 1 to give, 0 to take.

    IT SETS THE RULE, NOT THE ANIMATION. The door is `Maze::f_has_key` now; the clip follows it in
    SyncView. A button that posed the door directly would be overwritten on the next tick and would
    also be lying, since walking through is decided by the rules.
*/
#define BOMBER_CMD_DOOR             SIM_CMD_LAST+2
/*
    Lays a pickup on the tile the player is standing on, so the next tick collects it.
    value[0] is a MazeItem.

    FOR TESTING, like BOMBER_CMD_DETONATE, and for the same reason: waiting for a board to hand out
    its one crystal by playing takes minutes. It goes through the REAL path - Maze::TickItems does
    the granting a tick later - rather than adding to the score directly, so what is exercised is
    the rule and not a shortcut around it.
*/
#define BOMBER_CMD_GIVE             SIM_CMD_LAST+3

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
    //A pool of animated turds. Decoration, but it is the asset that multi-root rigs were fixed for.
    void BuildTurds();
    /*
        The exit, as an archway with a swinging leaf under it.

        RENDER THREAD, ONCE, from Init - it takes objects out of the AssetManager and adds one to
        the scene, and it is deliberately NOT part of RebuildField, so a restart leaves it standing.
        See the long note at the definition for why the clip goes on the ARCHWAY and not on the
        leaf it actually drives.
    */
    void BuildDoor();
    //One archway with its leaf attached and Door_Opening loaded onto it, added to the scene and
    //shut. RENDER THREAD. Three of these exist - the board's exit and the corridor's two ends.
    Object* MakeDoorway(const char* name);
    //--- the corridor between levels ------------------------------------------------------------
    /*
        Every object a corridor can ever need, at its maximum size, built once.

        RENDER THREAD, ONCE, from Init - the same rule the enemies and the turds keep, because the
        corridor is laid out from the PHYSICS thread and nothing there may upload a mesh. A shorter
        corridor hides the surplus; nothing is ever created or destroyed for one.
    */
    void BuildHallway();
    //The player stepped into the open exit. PHYSICS THREAD.
    void BeginHallway();
    /*
        The near door has finished shutting: throw the old board away, lay out the next one, and
        pick the corridor up and turn it to meet it.

        THE ONE MOMENT THIS IS SAFE, and it is safe because the corridor is a closed box by now -
        both doors shut, no skybox, nothing of either board visible. So the whole thing can be moved
        and rotated with the player and the camera inside it and nobody can tell. PHYSICS THREAD.
    */
    void CommitHallway();
    //The player stepped into the far doorway and is now standing on the next board. PHYSICS THREAD.
    void EndHallway();
    //Positions and shows the corridor, runs the pop-in and drives its two doors. PHYSICS THREAD.
    void SyncHallwayView();
    //World position of a corridor cell, in LOCAL coordinates and fractional so a mid-step walker
    //lands between two of them.
    vec3 HallCellCentre(float x, float z) const;
    //Puts a door instantly at one end of its clip and holds it there - the rate-0 case. Used where
    //a door has to START open, which no amount of playing forwards can express.
    void ParkDoor(Object* arch, bool f_open);

    /*
        Brings the door's animation in step with the rules. PHYSICS THREAD, from SyncView.

        `f_snap` rewinds the clip first, which is how a NEW BOARD gets a shut door on its first
        frame instead of one swinging shut on a board it was never open on.
    */
    void SetDoorOpen(bool f_open, bool f_snap = false);
    /*
        Spins the treasure and shrinks away whatever was just picked up.

        PHYSICS THREAD, from SyncView, every tick. Both are TWEENS rather than clips, which is the
        line this app now draws: a motion with a shape to it (the door swinging) is authored in
        Blender and played through Object's animation path; a spin, a bob or a fade is arithmetic
        against state the rules already hold, and a .glb round trip to change its speed would be
        worse than a constant.
    */
    void TickPickupView();
    /*
        One skinned skeleton with its clips loaded onto it, ready to be shown and posed.

        RENDER THREAD ONLY - it uploads a mesh. `f_report` logs which of the clip's tracks actually
        bound to a bone, and is passed true for the first actor of each kind only.
    */
    Skeleton* BuildSkinnedActor(const char* skin_name, const char* node_name,
                                const char* const* clip_names, int num_clips, bool f_report);
    //The quarter-turn a scattered piece takes on a cell, hashed from the cell so it is the same
    //every rebuild and never touches the simulation's random stream.
    float CellYaw(int cx, int cz) const;
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
        straight over water on purpose, that showed up as the blast drawn over the pond instead
        of stopping at its surface. BuildWater sets Shader::f_writes_gbuffer, which puts the tile's
        SHAPE back in the deferred pass while leaving its colour custom, and both halves of the
        problem go with it.

        Built both ways to check it was worth having: the flag changes 15% of the pond's pixels and
        nothing at all outside it.
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
    /*
        Ticks left of a taken pickup's shrink, per cell, and how many cells are doing one.

        VIEW STATE, NOT A RULE, and that is the whole reason it is here rather than in Maze: the
        pickup is gone from the game the instant it is taken - health is granted, the score moves -
        and what is left is a picture of it getting smaller. Running the tick twice would change
        nothing about the outcome, which is this app's test for which side of the line something
        belongs on.

        The counter is what keeps the per-tick cost at one comparison. RefreshCells starts a shrink
        when it finds a visible pickup the rules no longer have; TickPickupView runs them down.
    */
    int  cell_item_shrink[MAZE_H][MAZE_W];
    int  num_item_shrinking = 0;
    /*
        The coins and diamonds of this field, so the spin costs a walk of three or four objects per
        tick rather than 256 cells. Rebuilt with the field; never destroyed through here, since
        field_objects owns them.
    */
    std::vector<Object*> spin_items;
    /*
        The shield the player wears while one is running.

        A CHILD OF `character`, which does three things at once: it follows the player with no code
        at all, it turns with them, and ~Object frees it when RebuildField destroys the character -
        so there is no second lifetime to keep in step. Its local transform is identity because the
        artist placed it on the character in the .glb and the two nodes share an origin.
    */
    Object* shield_worn = NULL;
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
    /*
        The animated turds, handed out to whichever cells have one.

        A pool rather than one per cell because the generator scatters them - see BOMBER_MAX_TURDS.
        RebuildField takes them in order and hides the rest; unlike the enemies, which cell a given
        one is on changes every restart, so nothing may assume the index means anything.
    */
    std::vector<Object*> turd_objects;
    float turd_y = 0.0f;
    /*
        The exit: the archway object, with the door leaf attached under it as a child.

        ONE POINTER FOR TWO OBJECTS on purpose - everything this class does afterwards goes through
        the archway, because that is what holds the clip and what the scene knows about. The leaf is
        reachable as its only child if it is ever needed.

        THE RULES DO NOT KNOW ABOUT IT YET. It is scenery standing in the border wall, and the only
        thing that opens it is the button. Walking through it does nothing, because `Maze` has no
        door tile - that is the next piece of work, not something the view should fake.
    */
    Object* door_arch = NULL;
    bool f_door_open = false;

    //--- the corridor ---------------------------------------------------------------------------
    Hallway hall;
    //Which of the two is being simulated. They are NEVER both ticked - see the note at the top of
    //Hallway.h - and they overlap on screen only until the near door shuts.
    bool f_in_hallway = false;
    /*
        Ticks from the corridor sealing to the board being swapped.

        Not zero, and that is the user-visible half of the rule: the commit is when the near door
        has FINISHED shutting, not when it starts. Long enough to cover the clip.
    */
    int  hall_commit_ticks = 0;
    bool f_hall_committed = false;
    //Counts UP while the corridor rises out of the floor. View only; the rules have the cells there
    //from the first tick, and the pop-in is a tween like the coins and the shrink.
    int  hall_build_ticks = 0;
    uint32_t drawn_hall_version = 0;
    //World position of local cell (1,0) - the near doorway. The corridor's whole placement is this
    //plus `hall.forward`, which is what lets CommitHallway move and turn it in two lines.
    vec3 hall_origin = vec3(0,0,0);
    //The seed the next board will be laid out from, chosen when the corridor begins so the board is
    //ready the moment the corridor seals.
    uint32_t hall_next_seed = 0;
    Object* hall_floor[HALL_MAX_LEN][HALL_W];
    //Padded by one on each side: index px is local x = px - 1, so the two side walls are px 0 and
    //px HALL_W+1 and the end caps fall out of IsPassable.
    Object* hall_wall[HALL_MAX_LEN][HALL_W + 2];
    /*
        The corridor's own two archways.

        The near one is NOT the board's exit door even though it stands in the same place: that one
        belongs to the board and is carried off to the next one at the commit, which would happen
        while this is still in shot. The far one is the board's ENTRY, and it stays standing at
        BOMBER_ENTRY_X/Z for the whole level afterwards - it is the door you came in by.
    */
    Object* hall_near_arch = NULL;
    Object* hall_far_arch = NULL;
    //What the two doors are DRAWN as, compared against the rules every tick - the same shape as
    //f_door_open, and for the same reason: SetAnimationRate would otherwise wake a parked clip
    //every tick for ever.
    bool f_hall_near_drawn_open = false;
    bool f_hall_far_drawn_open = false;
    //Counts down while the camera walks back from the corridor to where it was over the board.
    int  hall_camera_return = 0;
    /*
        Where the camera was when the corridor began, so it can be put back exactly there.

        Saved rather than recomputed, because the player may have orbited the board to somewhere they
        like and a transition that quietly straightened it out would be taking that away. The next
        board is laid out at the same origin, so the same absolute framing is the right one.
    */
    vec3 cam_return_pos = vec3(0,0,0);
    vec3 cam_return_target = vec3(0,0,0);
    /*
        Ticks into the old board sinking out of sight, or -1 for not sinking.

        The reverse of the corridor's pop-in, and it starts at the SEAL rather than at the commit:
        the board has to be gone before RebuildField throws it away, or the throw is the pop this is
        here to avoid.
    */
    int  board_sink_ticks = -1;
    //Where the sink spreads from - the exit the player just left through. Cells nearest it go first,
    //so the level collapses away behind them.
    vec3 board_sink_from = vec3(0,0,0);
    //How far each sinking object has been moved so far, applied as a delta each tick.
    void SinkBoard();
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
    float blast_radius = 0.5f;
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
    int   num_view_steps = 32;
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
