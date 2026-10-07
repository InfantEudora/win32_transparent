#ifndef _APPLICATION_CHASM_H_
#define _APPLICATION_CHASM_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>

#include "Application.h"
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"
#include "TerrainMesh.h"
#include "ChasmWorld.h"
#include "Zones.h"
#include "ZoneMesh.h"
#include "ChasmSave.h"
#include "Walkers.h"
#include "Calendar.h"
#include "Economy.h"
#include "Exploration.h"

/*
    PLAY MODE'S SELECTION (docs/selection_plan.md): a building - by id, with the plot it was clicked on
    - or a person, by id. View state only: nothing a tick reads.
*/
#define PLAY_PICK_NONE          0
#define PLAY_PICK_BUILDING      1       //a building or field, by id
#define PLAY_PICK_PERSON        2       //an EconomyWorker, by id
struct PlayPick{
    int kind = PLAY_PICK_NONE;
    uint32_t id = 0;
    int plot = -1;                      //a building's plot it was picked on (a store's chips name it)
    bool operator==(const PlayPick& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const PlayPick& o) const { return !(*this == o); }
};

//The people's figures in chasm_props.glb, by sex: the adults, then the children (who wait for births).
#define PERSON_FIGURES          4
#define PERSON_ADULT_MALE       0
#define PERSON_ADULT_FEMALE     1
#define PERSON_KID_MALE         2
#define PERSON_KID_FEMALE       3
extern const char* person_asset_names[PERSON_FIGURES];

//A place on the overlay the mouse can press, drawn this frame: what pressing it does.
#define OVERLAY_HIT_BLOCK       0       //a panel: the click is the overlay's, nothing under it is picked
#define OVERLAY_HIT_TOOL        1       //the build bar: take up tool `value`
#define OVERLAY_HIT_STORE_ALLOW 2       //a store's card: let it take good `value`, or not
#define OVERLAY_HIT_CLOSE       3       //a card's close mark: clear the selection
struct OverlayHit{
    vec2 a, b;
    int action = OVERLAY_HIT_BLOCK;
    int value = 0;
};

/*
    chasm - a top-down colony sim on a Townscaper-style irregular grid. See docs/README.md for the
    game and the rules every step follows, and docs/grid_plan.md for the build order.

    STEP 1: the grid generator, a top-down camera, and the grid drawn as lines with its debug
    views and checks. STEP 2: picking - the plot, fine cell and coarse cell under the cursor,
    highlighted, and a click to select (GridPick.h). STEP 3: terrain levels from the chasm's
    feature lines, and the terrain drawn - ground, cliff walls, skirt (Terrain.h, TerrainMesh.h).

    --- THREADS -----------------------------------------------------------------------------------
    Everything generated is one ChasmWorld - grid, picker, terrain, the mesh data - IMMUTABLE once
    built and held by shared_ptr. Regenerating builds a new one on whichever thread asked (the
    panel, an MCP handler) and swaps the pointer under grid_mutex; a reader takes a copy of the
    pointer under the same mutex and then reads freely. So nothing ever waits on a generation
    except whoever asked for it, no reader can see half a world, and no reader can pair one grid
    with another grid's terrain.

    The camera state (camera_target and cam_*) belongs to the physics thread's UpdateView, which
    runs with physics_mutex held. The panel writes it directly (it holds that mutex too); an MCP
    handler goes through Scene::AtTickBoundary.

    --- DEBUG ONLY ---------------------------------------------------------------------------------
    The grid's line view, its layers and the checks exist only in a debug build (docs/README.md,
    "Checks live in the app"). A release build generates the same world and draws the terrain.
*/


//App keys, past core's - Q/E turn the view, F frames the whole map, N is the next seed, 1-7 pick
//(and pick again to drop) the house, field, erase, garden, town, road and walker tools.
#define INPUT_CHASM_ROTATE_LEFT     INPUT_LAST+1
#define INPUT_CHASM_ROTATE_RIGHT    INPUT_LAST+2
#define INPUT_CHASM_FRAME           INPUT_LAST+3
#define INPUT_CHASM_NEXT_SEED       INPUT_LAST+4
#define INPUT_CHASM_TOOL_HOUSE      INPUT_LAST+5
#define INPUT_CHASM_TOOL_FIELD      INPUT_LAST+6
#define INPUT_CHASM_TOOL_ERASE      INPUT_LAST+7
#define INPUT_CHASM_TOOL_GARDEN     INPUT_LAST+8
#define INPUT_CHASM_TOOL_LOT        INPUT_LAST+9
#define INPUT_CHASM_TOOL_ROAD       INPUT_LAST+10
#define INPUT_CHASM_TOOL_WALKER     INPUT_LAST+11
#define INPUT_CHASM_PREVIOUS_SEED   INPUT_LAST+12
#define INPUT_CHASM_TOOL_STORE      INPUT_LAST+13
#define INPUT_CHASM_TOOL_WOODCUTTER INPUT_LAST+14
#define INPUT_CHASM_TOOL_WATER      INPUT_LAST+15
#define INPUT_CHASM_TOOL_WINCH      INPUT_LAST+16
#define INPUT_CHASM_PAUSE          INPUT_LAST+17     //space; core's own is the Pause key
#define INPUT_CHASM_SLOWER         INPUT_LAST+18
#define INPUT_CHASM_FASTER         INPUT_LAST+19
#define INPUT_CHASM_TOOL_CAMP      INPUT_LAST+20
#define INPUT_CHASM_FOG            INPUT_LAST+21     //V: the clouds over unexplored ground, in debug mode
#define INPUT_CHASM_TOOL_BRIDGE    INPUT_LAST+22     //J, for jetty: B is the previous seed

//The app's simulation commands, past core's.
#define CHASM_CMD_ZONE              SIM_CMD_LAST+0      //subtype: plot or coarse cell; value[0]: ZoneOp;
                                                        //value[1]: ground kind for ZONE_OP_GROUND_PAINT
#define CHASM_CMD_WALKER            SIM_CMD_LAST+1      //subtype: WalkerOp; value[0]: home plot; value[1]: goal plot
#define CHASM_CMD_CALENDAR          SIM_CMD_LAST+2      //sets the date: subtype the day, value[0] the tick in it

//What a walker command asks for (step 10). Never renumber - recordings refer to these by value.
enum WalkerOp{
    WALKER_OP_NONE = 0,
    WALKER_OP_SPAWN,        //a debug walker between home and goal
    WALKER_OP_CLEAR         //every walker gone
};

//What a left click does.
enum ChasmTool{
    CHASM_TOOL_SELECT = 0,
    CHASM_TOOL_HOUSE,
    CHASM_TOOL_FIELD,
    CHASM_TOOL_ERASE,
    CHASM_TOOL_GARDEN,
    CHASM_TOOL_LOT,
    CHASM_TOOL_ROAD,        //step 10: ground of kind road
    CHASM_TOOL_WALKER,      //click a home, then a goal: a debug walker between them
    CHASM_TOOL_STORE,       //docs/buildings_plan.md: buildings of these kinds, a drag each
    CHASM_TOOL_WOODCUTTER,
    CHASM_TOOL_WATER,
    CHASM_TOOL_WINCH,       //docs/buildings_plan.md step 2: on the rim above a balcony
    CHASM_TOOL_CAMP,        //docs/people_plan.md: tents and fires
    CHASM_TOOL_BRIDGE,      //docs/bridge_plan.md: press on one bank, let go on the other
    CHASM_TOOL_COUNT
};

class ApplicationChasm : public Application{
public:
    ApplicationChasm();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;

    /*
        --- Saves and replays (step 6) -----------------------------------------------------------
        A recording's start state IS a save (ChasmSave.h), so a replay starts from one. The state
        hash is what the simulation decides with and nothing else: the world it runs on, and the
        zones - not the camera, not the sun that follows it.
    */
    json CaptureRecordingState() override;
    void RestoreRecordingState(const json& state) override;
    void HashSimState(StateHash& hash) override;
    ChasmSave MakeSave();
    //PHYSICS THREAD or with physics_mutex held. Regenerates the map if the save's settings differ.
    bool LoadSave(const ChasmSave& save, std::string& error);
    void PreRender(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    //Any thread but the render thread's own frame. Builds a new grid and its picker, and makes
    //both current together.
    void RegenerateGrid(const GridSettings& s);
    std::shared_ptr<const ChasmWorld> GetWorld();
    std::shared_ptr<const Grid> GetGrid();
    std::shared_ptr<const GridPicker> GetPicker();

    /*
        --- Picking (step 2) --------------------------------------------------------------------
        The VIEW's idea of what is under the cursor and what was clicked. Neither is simulation
        state: painting a plot (step 5) will be a command that names a plot, and the selection
        is only where the person's eye is. Written on the physics thread in UpdateView and by the
        tools, read by the render thread and the panel - all under pick_mutex.
    */
    GridPick GetHoverPick();
    GridPick GetSelectedPick();
    void SetSelectedPick(const GridPick& p);
    //What is under a window pixel, through the camera, on whichever level the ray meets first.
    //A miss (sky, or off the map) comes back with f_hit false.
    GridPick PickUnderPixel(int2 px);

    //--- The camera: an orbit around a point on the ground ----------------------------------------
    vec3 camera_target;
    float cam_yaw = 0.0f;           //radians; 0 looks toward -z (north)
    float cam_pitch = 0.95f;        //radians above the horizon
    float cam_distance = 120.0f;
    /*
        Entering play mode, the view glides to the colony's start - the camp - unless it already looks
        at explored ground from within the game's zoom (UpdateCamera). Pending until there is a camp to
        go to; the player's own camera input calls the glide off.
    */
    bool f_cam_was_play = false;
    bool f_cam_fly_pending = false;
    bool f_cam_flying = false;
    vec2 cam_fly_to;
    //Physics thread. Puts the camera over the whole map.
    void FrameMap();

private:
    std::mutex grid_mutex;
    std::shared_ptr<const ChasmWorld> world;
    GridSettings next_settings;     //what the panel edits; the physics thread never reads it

    //--- The terrain's chunks: one Object each, built on first need and reused after ----------------
    std::vector<Object*> terrain_chunks;
    std::shared_ptr<const TerrainMeshData> terrain_uploaded;    //render thread
    bool f_terrain_shown = true;                                //render thread
    std::atomic<bool> f_view_terrain{true};
    int palette_material = -1;      //the one material everything is drawn with (Palette.h)
    void UploadTerrain();

    std::mutex pick_mutex;
    //A script's hover, in place of the mouse's (chasm_tool hover_x/hover_z); the point under pick_mutex.
    std::atomic<bool> f_hover_pinned{false};
    vec2 hover_pin;
    //...and its buttons (chasm_tool button, right_click), so a script can drag a road: the left one
    //-1 the mouse's, 0 up, 1 down; a right press waiting for the next pass.
    std::atomic<int> script_left{-1};
    bool f_script_left_was = false;             //physics thread: for the edge of a scripted press
    std::atomic<bool> f_script_right{false};
    GridPick hover_pick;
    GridPick selected_pick;
    std::atomic<int> pick_version{0};
    void UpdatePick();
    json PickJson(const GridPicker& p, const GridPick& pick);
    json PickBuildingJson(const GridPick& pick);
    json BuildingJson(const ChasmWorld& w, const ZoneState& z, uint32_t id);
    void RenderBuildingInfo(const ChasmWorld& w, const ZoneState& z);
    static int ToolKind(int tool);

    /*
        --- Zones (step 5) --------------------------------------------------------------------------
        `zones` is SIMULATION state, the physics thread's alone: changed only by CHASM_CMD_ZONE
        commands as a tick drains them. After each change a copy is published (zone_snapshot, under
        grid_mutex) for everyone else - the view, the panel, the tools - which is the world's
        pattern again: readers get an immutable copy, never the live state.
    */
    Zones zones;
    std::shared_ptr<const ZoneState> zone_snapshot;
    std::string zone_last_refusal;              //under grid_mutex
    std::atomic<uint32_t> zone_commands_done{0};
    void EnsureZonesWorld();                    //physics thread: a new map empties the zones
    void PublishZones();                        //physics thread
    std::shared_ptr<const ZoneState> GetZones();
    void SubmitZone(int op, int index, int kind = 0, uint32_t stroke = 0);   //any thread; queued for the next tick
    void RegisterCommandHandlers();

    std::atomic<int> paint_tool{CHASM_TOOL_SELECT};
    int paint_last_index = -1;                  //physics thread: what a drag last painted
    std::atomic<int> bridge_from{-1};           //the bridge tool's press: the bank it starts from, -1 none
    std::atomic<uint32_t> zone_refusals{0};     //bumped on each refused zone command - the play overlay shows why
    uint32_t refusal_seen = 0;                  //render thread: the last one shown, and since when
    std::chrono::steady_clock::time_point refusal_since;
    /*
        The drag in progress (docs/buildings_plan.md: a drag is one building): a number for each press,
        carried by every zone command the drag makes. Physics thread. A load sets it to the save's, so a
        stroke after it is never mistaken for the one the save was in the middle of.
    */
    uint32_t paint_stroke = 0;
    void UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held);

    /*
        THE LINE DRAG (docs/line_works_plan.md, ApplicationChasmRoads.cpp): the road tool draws its road
        FREEHAND and places it on release. While the button is held, `line_chain` is the plots the cursor
        has crossed, in order and joined along fine edges; nothing is sent until the button comes up, when
        the chain goes out as one stroke. A right press drops it. Written by the physics thread, read by
        the previews (the ghost, the pick view) under pick_mutex; line_version moves with every change.
    */
    enum LineDrag{ LINE_DRAG_NONE = 0, LINE_DRAG_DRAWING, LINE_DRAG_DROPPED };
    int line_drag = LINE_DRAG_NONE;             //physics thread; DROPPED waits for the left button to come up
    std::vector<int> line_chain;                //under pick_mutex
    bool f_line_erase = false;                  //under pick_mutex: a shift-drag, taking road up
    std::atomic<uint32_t> line_version{0};
    void UpdateLineDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held);
    void PlaceLine();
    void DropLine();
    //Whether a line may take `plot` - or, erasing, take its road up: the release's rule and the previews'
    //colour. `ex` may be NULL (all explored). Any thread, on published state.
    bool LinePlotAllowed(const ChasmWorld& w, const ZoneState& z, const Exploration* ex, bool f_erase, int plot,
                         const char** why);

    /*
        --- Roads and walkers (step 10, ApplicationChasmRoads.cpp; docs/roads_plan.md) ---------------
        A road is ground, painted through the zone commands. `walkers` is simulation state like
        `zones`, the physics thread's alone, changed by CHASM_CMD_WALKER and by the tick, and
        published the same way: a copy after each change, under grid_mutex. The view draws a figure
        per walker and, in a debug build, its path.
    */
    Walkers walkers;
    std::shared_ptr<const WalkerSet> walker_snapshot;
    std::string walker_last_refusal;            //under grid_mutex
    std::atomic<uint32_t> walker_commands_done{0};
    int walker_tool_home = -1;                  //physics thread: the walker tool's first click
    void PublishWalkers();                      //physics thread
    std::shared_ptr<const WalkerSet> GetWalkers();
    void SubmitWalker(int op, int home, int goal);
    void RegisterWalkerCommands();
    void TickWalkers();                         //physics thread, from RunSimulationTick
    void HashWalkers(StateHash& h);
    void UpdateWalkerTool(const GridPick& hover, bool f_shift);
    Mesh* walker_mesh = NULL;
    std::vector<Object*> walker_objects;        //render thread: one per walker, reused
    std::atomic<bool> f_view_walkers{true};
    void BuildWalkerScene();                    //from BuildScene
    void UploadWalkers();                       //render thread, from PreRender
#ifdef DEBUG
    Object* walker_path_view = NULL;
    Mesh* walker_path_mesh = NULL;
    uint32_t walker_path_built = 0xFFFFFFFFu;
#endif
#ifdef USE_MCP
    void RegisterWalkerTools();
#endif
#ifdef USE_IMGUI
    void RenderWalkerPanel();
#endif

    /*
        --- The economy (docs/economy_plan.md, P2 + P3; ApplicationChasmEconomy.cpp) -----------------
        `economy` is SIMULATION state, the physics thread's alone, ticked after the walkers; a copy is
        published after every tick that changed it (economy_snapshot, under grid_mutex) - the zones'
        pattern again. The view reads the copy: felled trees and stumps, the workers, the fields' growth.
    */
    Economy economy;
    std::shared_ptr<const EconomyState> economy_snapshot;
    void TickEconomy();                         //physics thread, from RunSimulationTick
    void PublishEconomy();                      //physics thread
    std::shared_ptr<const EconomyState> GetEconomy();
    void HashEconomy(StateHash& h);
    json EconomyBuildingJson(uint32_t id);      //a building's stock, what it takes, its worker
#ifdef USE_MCP
    void RegisterEconomyTools();
#endif
#ifdef USE_IMGUI
    void RenderEconomyInfo(uint32_t id);        //in the building info, for the building under the cursor
    void RenderSelectedStore(int plot);         //what the selected store takes, to be set there
#endif

    /*
        --- Exploration and the clouds (docs/play_mode_plan.md steps 2-4, ApplicationChasmExplore.cpp) ---
        `exploration` is SIMULATION state, the physics thread's alone: cleared round what is built and
        round the people as the tick goes (Exploration.h), saved and hashed, a copy published when it
        changes. The clouds are its view: puffs over what is not explored, shown in play mode and in
        debug mode only when asked for.
    */
    Exploration exploration;
    std::shared_ptr<const Exploration> exploration_snapshot;   //under grid_mutex
    std::vector<uint8_t> explored_plot;         //physics thread: per fine vertex, already cleared round
    std::vector<uint8_t> explored_cell;         //per coarse quad, the same for a field's cells
    uint32_t explored_zones_version = 0xFFFFFFFFu;
    void ResetExploration();                    //physics thread: a new map, nothing explored
    void TickExploration();                     //physics thread, from RunSimulationTick
    void PublishExploration();                  //physics thread
    std::shared_ptr<const Exploration> GetExploration();
    //Whether a zone command's plot or cell is explored - the rule a PLAY command is held to.
    bool ZoneCommandExplored(const ChasmWorld& w, int op, int index);
    std::atomic<bool> f_view_clouds{false};     //debug mode: the clouds drawn anyway (play mode always draws them)
    Mesh* cloud_meshes[2] = {};
    std::vector<Object*> cloud_sets;            //chunk * 2 + shade
    std::shared_ptr<const Exploration> clouds_built_for;
    std::shared_ptr<const ChasmWorld> clouds_built_world;
    bool f_clouds_shown = false;
    //Each lattice point's place and the ground its puff sits on - per map, as the exploration changes often.
    struct CloudSpot{
        vec2 p;
        float ground;       //the highest ground under the puff's footprint
        vec2 low_at;        //and the lowest, for a cliff the puffs stack down
        float low;
        uint32_t hash;
    };
    std::vector<CloudSpot> cloud_lattice;
    std::shared_ptr<const ChasmWorld> cloud_lattice_world;
    void BuildCloudScene();                     //from BuildScene
    void UploadClouds();                        //render thread, from PreRender
    /*
        THE GHOST (ApplicationChasmGhost.cpp): in play mode, what the tool in hand would place under the
        cursor - see-through, green where the rules allow it and red where they refuse, with an outline
        in the same colour - in place of the debug line mesh.
    */
    Shader* ghost_shader = NULL;
    int ghost_shader_index = -1;
    Object* ghost = NULL;
    bool f_ghost_ok = true;                     //render thread: the tint the shader is given
    struct GhostKey{
        const void* world = NULL;
        uint32_t zones_version = 0;
        int plot = -1, coarse = -1, tool = -1;
        bool f_explored = false;
        uint32_t line_version = 0;              //a road being drawn: its chain (0 with none)
        int bridge_from = -1;                   //a bridge being dragged: the bank it starts from
        bool operator==(const GhostKey& o) const{
            return world == o.world && zones_version == o.zones_version && plot == o.plot && coarse == o.coarse
                   && tool == o.tool && f_explored == o.f_explored && line_version == o.line_version
                   && bridge_from == o.bridge_from;
        }
    };
    GhostKey ghost_built;
    void BuildGhostScene();                     //from BuildScene
    void SetGhostUniforms();
    void UpdateGhost();                         //render thread, from PreRender

    /*
        --- The economy, drawn (ApplicationChasmWorkersView.cpp, and the forest and zones below) -----
        Each worker a walker's figure in his job's tunic, with what he carries on him; one object per
        worker, reused, placed every frame from the published state. Render thread.
    */
    Mesh* worker_meshes[2 * 4] = {};                //sex * 4 + load (ApplicationChasmWorkersView.cpp)
    std::vector<Object*> worker_objects;
    std::vector<int> worker_object_mesh;            //per object: which of worker_meshes it has, -1 none
    void BuildWorkerScene();                        //from BuildScene
    void UploadWorkers();                           //from PreRender
    //The woodcutters' log piles outside their huts (ApplicationChasmPiles.cpp). Render thread.
    Object* pile_set = NULL;
    std::shared_ptr<const ChasmWorld> pile_built_world;
    std::vector<std::pair<uint32_t,int>> pile_built;    //hut, logs - as last drawn
    uint32_t pile_built_zones = 0;
    void UploadPiles();                             //from PreRender

    /*
        --- The calendar and the speed (gameplay_plan.md P1, ApplicationChasmTime.cpp) ---------------
        `calendar_tick` is SIMULATION state, the physics thread's alone: one more every tick, reset with
        the zones on a new map, set by a load (Calendar.h). `calendar_shown` is its copy for everyone
        else, written after each tick. The SPEED is the viewer's, not the simulation's: it sets how many
        ticks a second run (Application::physics_time_factor) and never what a tick is, so a replay is
        the same at any speed and the speed is neither saved nor hashed.
    */
    uint64_t calendar_tick = 0;
    std::atomic<uint64_t> calendar_shown{0};
    std::atomic<int> speed_index{0};            //into the speeds in ApplicationChasmTime.cpp
    void TickCalendar();                        //physics thread, from RunSimulationTick
    void SetCalendar(uint64_t tick);            //physics thread: a load, a new map, the date command
    void RegisterTimeCommands();
    std::atomic<uint32_t> calendar_commands_done{0};
    void UpdateTimeKeys();                      //physics thread, from UpdateView: pause and speed
    void SetSpeed(int index);                   //any thread
    int SpeedCount();
    float SpeedFactor(int index);
    //Any thread: the winter snow's mean front (CalendarSnowFrontZ) for the date shown, on this map.
    float SnowFrontNow();
    void DrawOverlay(void) override;            //render thread: the date and the speed, every build
#ifdef USE_IMGUI
    void RenderTimePanel();
#endif
#ifdef USE_MCP
    void RegisterTimeTools();
#endif
    //A view-only override of the snow's cover for looking at winter on any date: < 0 is off. Never
    //read by the simulation (chasm_time snow_cover, debug builds).
    std::atomic<float> snow_cover_override{-1.0f};

    /*
        --- Winter, drawn (gameplay_plan.md P1, ApplicationChasmSnow.cpp) ------------------------------
        The ground, the zones and the forest drawn by shaders/chasm_ground.frag, which puts what faces
        up north of the snow's front onto the palette's frozen row. Render thread.
    */
    Shader* ground_shader = NULL;
    int ground_shader_index = -1;
    void BuildGroundShader();                   //from BuildScene
    void SetGroundUniforms();
    void UseGroundShader(Mesh* mesh);           //a mesh drawn with the palette, onto the ground shader

    char save_name[64] = "village";     //the panel's name field
    std::string save_status;            //render thread: what the last save/load said

    /*
        --- The forest (step 7) ---------------------------------------------------------------------
        One INSTANCE SET (Object::SetInstances) per terrain chunk per kind: a chunk's pines are one
        object holding every pine's transform, so 35,000 props are about 2,000 objects and the
        Renderer never walks them one by one. (One Object per prop cost 40 ms a frame - see
        grid_plan.md step 7.) A set holds only the props not hidden by a zone - a house on the
        plot, a field over the cell - and a chunk's sets are rebuilt when its zones move. The sets
        are made once and reused when the map is regenerated. Render thread.
    */
    bool f_props_loaded[PROP_KIND_COUNT] = {};
    std::vector<Object*> prop_sets;                 //chunk * PROP_KIND_COUNT + kind
    //`e`: the economy for this world, or NULL - a felled tree is a stump, with its log while it lies.
    void RebuildPropChunk(const ChasmWorld& w, const ZoneState* z, const EconomyState* e, int chunk);
    std::shared_ptr<const ChasmWorld> forest_built_world;
    std::vector<uint32_t> forest_zone_version;      //per chunk: the zones' version it was hidden for
    std::vector<uint8_t> forest_prop_state;         //per prop: the PROP_STATE_* the sets were built with
    std::vector<int> forest_prop_chunk;             //per prop: the chunk whose sets hold it
    uint32_t forest_felled_version = 0xFFFFFFFFu;   //the economy's felled_version they were built at
    std::atomic<bool> f_view_forest{true};
    bool f_forest_shown = true;
    void LoadProps();
    void UploadForest();
    bool PropHidden(const PropInstance& p, const ZoneState* z, bool* f_by_road = NULL);

    /*
        --- The chasm's own look (step 9, ApplicationChasmWater.cpp) -----------------------------------
        The rivers and the falls: one object each, drawn by chasm_water.glsl and uploaded once per
        world. The mist: one instance set per terrain chunk per shade, re-posed every frame from the
        simulation's clock (Mist.h); the foam: one set for every fall, the same way. Render thread.
    */
    Shader* water_sheet_shader = NULL;
    Shader* water_flat_shader = NULL;
    int water_sheet_shader_index = -1;
    int water_flat_shader_index = -1;
    int water_material = -1;
    Object* water_flat = NULL;
    //The swamp's pools (biomes_plan.md step 4): the rivers' shader again, as its own program so its
    //flow can be the stillness of standing water.
    Shader* water_pool_shader = NULL;
    int water_pool_shader_index = -1;
    Object* water_pools = NULL;
    void SetWaterPoolUniforms();
    vec3 swamp_haze = vec3(0.77f,0.80f,0.75f);     //the palette's swamp-mist cell, read in BuildScene
    Object* water_sheets = NULL;
    std::shared_ptr<const ChasmWorld> water_built_world;
    bool f_water_shown = true;
    Mesh* mist_meshes[MIST_VARIANTS] = {};
    Mesh* foam_mesh = NULL;
    std::vector<Object*> mist_sets;                 //chunk * MIST_VARIANTS + variant
    //The palette with core's INSTANCE_MOTION_PUFF, which the mist's sets are drawn with: their puffs
    //bob and breathe on the GPU, so a set is filled once per world (Mist.h).
    int puff_material = -1;
    std::shared_ptr<const ChasmWorld> mist_built_world;
    bool f_mist_shown = true;
    Object* foam_set = NULL;
    std::atomic<bool> f_view_water{true};
    std::atomic<bool> f_view_mist{true};
    void BuildWaterScene();         //from BuildScene: shaders, materials, objects, puff meshes
    void UploadWater();
    void UpdateMist();
    void SetWaterSheetUniforms();
    void SetWaterFlatUniforms();
    double SimSeconds();

    std::vector<Object*> zone_chunks;
    std::vector<uint32_t> zone_chunk_built;     //render thread: chunk_version each was built at
    std::vector<uint8_t> zone_field_stage;      //render thread: per building id, the stage a field was drawn at
    std::shared_ptr<const ChasmWorld> zone_built_world;
    void UploadZones();

    //The cursor's highlight, rebuilt only when a pick or the grid changes. All builds - this is
    //the game's cursor, not a debug view.
    Object* pick_view = NULL;
    Mesh* pick_view_mesh = NULL;
    std::shared_ptr<const ChasmWorld> pick_view_built_for;  //render thread
    int pick_view_built_version = -1;
    void UpdatePickView();

    /*
        --- Play mode's selection (docs/selection_plan.md, ApplicationChasmSelect.cpp) --------------------
        The hover and the selection (pick_mutex), what the overlay drew that the mouse can press (written
        by DrawOverlay on the render thread, read by UpdatePick on the physics thread, under pick_mutex),
        and the OUTLINE-ONLY objects of the buildings outlined: the selected one, the hovered one, and a
        selected person's house and workplace - buildings are merged per chunk, so each is rebuilt alone.
    */
    PlayPick play_hover;
    PlayPick play_selected;
    std::atomic<uint32_t> play_pick_version{0};
    std::vector<OverlayHit> overlay_hits;
    std::vector<OverlayHit> overlay_hits_drawing;           //render thread, filled while drawing
    int2 right_press_px;                                    //physics thread: where a right press began
    bool f_right_press = false;
    /*
        The selection's outline-only objects: 0 the selected building, 1 the hovered one, 2 every building
        that BELONGS WITH the selection merged into one, 3 the ground that does (a woodcutter's lot) - see
        RelatedPlaces in ApplicationChasmSelect.cpp.
    */
    static const int SELECT_OUTLINES = 4;
    Object* select_outline[SELECT_OUTLINES] = {};
    uint32_t select_outline_id[SELECT_OUTLINES] = {};       //render thread: the building each holds (0, 1)
    uint32_t select_outline_zones[SELECT_OUTLINES] = {};    //...built at this zones version
    std::vector<uint32_t> select_related_buildings;         //render thread: what slot 2 was built from
    std::vector<int> select_related_plots;                  //...and slot 3
    PlayPick PlayPickUnder(int2 px, const GridPick& hover);
    void UpdatePlayPick(GridPick& hover, int2 px, bool& f_over_scene, bool& f_clicked);   //physics thread
    bool OverlayHitAt(int2 px, OverlayHit* hit);
    void UploadSelection();                                 //render thread, from PreRender
    void DrawBuildBar(float s);                             //render thread, from DrawOverlay
    void DrawSelectionOverlay(float s);                     //render thread, from DrawOverlay: card + pins
#ifdef USE_MCP
    void RegisterSelectTools();
    void RegisterBridgeTools();
#endif

    void BuildScene();
    void SetupInput();
    void UpdateCamera();
    void ApplyCamera();
    /*
        --- Play mode (docs/play_mode_plan.md) --------------------------------------------------------
        DEBUG MODE is the panels shown: the debug views, the tools, the free camera. PLAY MODE is the
        panels hidden (U, core's f_show_ui; a release exe starts there, a ship exe has no panels):
        the game's camera and none of the debug views or tools. A VIEW, never a rule - nothing a tick
        reads may depend on it. Any thread.
    */
    bool PlayMode() const;
    bool f_view_was_play = false;               //render thread: the mode the debug views were last set for
    void UpdatePlayModeViews();                 //render thread, from PreRender

    DirectionalLight* sun = NULL;
    //Where the sun stands relative to what it lights, set in BuildScene.
    vec3 sun_offset;
    void FollowSun();

    vec3 cam_last_written;          //see UpdateCamera: how a camera_set from a tool is noticed
    std::chrono::steady_clock::time_point cam_last_update;
    float cam_wheel = 0.0f;

    json GridStatsJson(const Grid& g);

#ifdef DEBUG
    /*
        The grid's line view and its layers. A layer toggle or a new grid bumps view_version, and
        PreRender rebuilds the lines once for it - a change costs a rebuild, a frame costs nothing.
    */
    std::atomic<bool> f_view_fine{true};
    std::atomic<bool> f_view_coarse{true};
    std::atomic<bool> f_view_valence{true};
    std::atomic<bool> f_view_squareness{false};
    std::atomic<bool> f_view_issues{true};
    std::atomic<bool> f_view_pins{true};
    std::atomic<bool> f_view_flat{false};   //every layer at y = 0 instead of on the terrain
    std::atomic<int> view_version{0};
    std::atomic<int> selected_issue{-1};

    Object* grid_view = NULL;
    Mesh* grid_view_mesh = NULL;
    std::shared_ptr<const ChasmWorld> grid_view_built_for;  //render thread
    int grid_view_built_version = -1;
    float grid_view_ms = 0.0f;
    int grid_view_vertices = 0;
    void UpdateGridView();

    //The last check's report, for the grid whose hash it carries. Under grid_mutex.
    std::shared_ptr<const GridCheckReport> check_report;
    std::shared_ptr<const GridCheckReport> RunChecks();
    std::shared_ptr<const GridCheckReport> GetCheckReport();
    json CheckReportJson(const GridCheckReport& r, int max_issues);
    //Physics thread (or with physics_mutex held): the camera onto issue `i` of the last report.
    void FocusIssue(int i);
#endif

#ifdef USE_MCP
    void RegisterMCPTools();
#endif
#ifdef USE_IMGUI
    void RenderChasmPanel();
#endif
};

#endif
