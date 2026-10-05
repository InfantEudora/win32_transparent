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
#define INPUT_CHASM_TOOL_TOWN       INPUT_LAST+9
#define INPUT_CHASM_TOOL_ROAD       INPUT_LAST+10
#define INPUT_CHASM_TOOL_WALKER     INPUT_LAST+11
#define INPUT_CHASM_PREVIOUS_SEED   INPUT_LAST+12

//The app's simulation commands, past core's.
#define CHASM_CMD_ZONE              SIM_CMD_LAST+0      //subtype: plot or coarse cell; value[0]: ZoneOp;
                                                        //value[1]: ground kind for ZONE_OP_GROUND_PAINT
#define CHASM_CMD_WALKER            SIM_CMD_LAST+1      //subtype: WalkerOp; value[0]: home plot; value[1]: goal plot

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
    CHASM_TOOL_TOWN,
    CHASM_TOOL_ROAD,        //step 10: ground of kind road
    CHASM_TOOL_WALKER       //click a home, then a goal: a debug walker between them
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
    GridPick hover_pick;
    GridPick selected_pick;
    std::atomic<int> pick_version{0};
    void UpdatePick();
    json PickJson(const GridPicker& p, const GridPick& pick);

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
    void SubmitZone(int op, int index, int kind = 0);   //any thread; queued for the next tick
    void RegisterCommandHandlers();

    std::atomic<int> paint_tool{CHASM_TOOL_SELECT};
    int paint_last_index = -1;                  //physics thread: what a drag last painted
    void UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked);

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
    void RebuildPropChunk(const ChasmWorld& w, const ZoneState* z, int chunk);
    std::shared_ptr<const ChasmWorld> forest_built_world;
    std::vector<uint32_t> forest_zone_version;      //per chunk: the zones' version it was hidden for
    std::atomic<bool> f_view_forest{true};
    bool f_forest_shown = true;
    void LoadProps();
    void UploadForest();
    bool PropHidden(const PropInstance& p, const ZoneState* z);

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
    std::shared_ptr<const ChasmWorld> zone_built_world;
    void UploadZones();

    //The cursor's highlight, rebuilt only when a pick or the grid changes. All builds - this is
    //the game's cursor, not a debug view.
    Object* pick_view = NULL;
    Mesh* pick_view_mesh = NULL;
    std::shared_ptr<const ChasmWorld> pick_view_built_for;  //render thread
    int pick_view_built_version = -1;
    void UpdatePickView();

    void BuildScene();
    void SetupInput();
    void UpdateCamera();
    void ApplyCamera();

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
