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


//App keys, past core's - Q/E turn the view, F frames the whole map, N is the next seed, 1/2/3 pick
//(and pick again to drop) the house, field and erase tools.
#define INPUT_CHASM_ROTATE_LEFT     INPUT_LAST+1
#define INPUT_CHASM_ROTATE_RIGHT    INPUT_LAST+2
#define INPUT_CHASM_FRAME           INPUT_LAST+3
#define INPUT_CHASM_NEXT_SEED       INPUT_LAST+4
#define INPUT_CHASM_TOOL_HOUSE      INPUT_LAST+5
#define INPUT_CHASM_TOOL_FIELD      INPUT_LAST+6
#define INPUT_CHASM_TOOL_ERASE      INPUT_LAST+7

//The app's simulation commands, past core's.
#define CHASM_CMD_ZONE              SIM_CMD_LAST+0      //subtype: plot or coarse cell; value[0]: ZoneOp

//What a left click does.
enum ChasmTool{
    CHASM_TOOL_SELECT = 0,
    CHASM_TOOL_HOUSE,
    CHASM_TOOL_FIELD,
    CHASM_TOOL_ERASE
};

class ApplicationChasm : public Application{
public:
    ApplicationChasm();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
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
    void SubmitZone(int op, int index);         //any thread; queued for the next tick
    void RegisterCommandHandlers();

    std::atomic<int> paint_tool{CHASM_TOOL_SELECT};
    int paint_last_index = -1;                  //physics thread: what a drag last painted
    void UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked);

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
