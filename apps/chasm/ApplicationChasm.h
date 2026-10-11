#ifndef _APPLICATION_CHASM_H_
#define _APPLICATION_CHASM_H_

#include <atomic>
#include <map>
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
#include "Seasons.h"

/*
    PLAY MODE'S SELECTION (docs/selection_plan.md): a building - by id, with the plot it was clicked on
    - or a person, by id. View state only: nothing a tick reads.
*/
#define PLAY_PICK_NONE          0
#define PLAY_PICK_BUILDING      1       //a building or field, by id
#define PLAY_PICK_PERSON        2       //an EconomyWorker, by id
#define PLAY_PICK_GATE          3       //a wall's gate, by its plot - in `id` as in `plot` (defence_plan.md)
#define PLAY_PICK_CRATE         4       //a crate lying about, by its id (crates_plan.md)
#define PLAY_PICK_GROUND        5       //a garden or lot still being built, by its patch's lowest plot (yards_plan.md)
#define PLAY_PICK_GROUP         6       //two or more grown-ups at once, in play_group - `id` 0 ("Several people selected")
struct PlayPick{
    int kind = PLAY_PICK_NONE;
    uint32_t id = 0;
    int plot = -1;                      //a building's plot it was picked on (a store's chips name it)
    bool operator==(const PlayPick& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const PlayPick& o) const { return !(*this == o); }
};

//The people's figures in chasm_props.glb, by sex: the adults, then the children (lifeline_plan.md).
#define PERSON_FIGURES          4
#define PERSON_ADULT_MALE       0
#define PERSON_ADULT_FEMALE     1
#define PERSON_KID_MALE         2
#define PERSON_KID_FEMALE       3
extern const char* person_asset_names[PERSON_FIGURES];
//An archer's bow and quiver in chasm_props.glb (ApplicationChasmWorkersView.cpp), stand-ins where it has none.
#define GEAR_ASSETS             2
extern const char* gear_asset_names[GEAR_ASSETS];
//Which of them a person is drawn as now (ApplicationChasmWorkersView.cpp).
int PersonFigure(const EconomyWorker& k, uint64_t tick);
/*
    The Shoot clip's beats (range_plan.md "Shooting"), as fractions of one turn of it, so a re-export at another frame
    rate keeps them. Read off its hands, frame by frame, in the 2026-10-11 export (120 frames): the right hand is over
    his shoulder at the quiver by frame 12, swings down to the bow and nocks from 34, holds at full draw from 68, and
    the hands part at 93-94. A re-animated clip needs them read again.
*/
#define SHOOT_TAKE              0.10f   //the arrow comes out of the quiver, into his right hand
#define SHOOT_NOCK              0.30f   //...is on the string: from here it points through the bow's grip
#define SHOOT_RELEASE           0.78f   //...and leaves the bow
//The archery range's props in chasm_props.glb (ApplicationChasmRange.cpp): target, hay bale, arrow.
extern const char* range_asset_names[];
//How a crate is turned where it lies, from its id (ApplicationChasmCrates.cpp) - its outline turns the same.
float CrateYaw(uint32_t id);
//How high a person's route is at `along` down leg `seg` - on the ground or the deck his route says it is on
//(ramparts_plan.md) - and how high he stands now (ApplicationChasmWorkersView.cpp).
float RouteHeight(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k, int seg, float along);
float WorkerHeight(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k);
//The turn that points +z along `d`, +y as near `up` as it goes - a bow at its arrow, an arrow along its flight. `d` must
//not be along `up` (ApplicationChasmWorkersView.cpp).
quat TurnZTo(const vec3& d, const vec3& up);
//The layer of plot `plot` nearest the height of `at` - its ground, if it has ground to stand on, or a deck's.
int SurfaceLayer(const ChasmWorld& w, const ZoneState& z, int plot, const vec3& at);

//A place on the overlay the mouse can press, drawn this frame: what pressing it does.
#define OVERLAY_HIT_BLOCK       0       //a panel: the click is the overlay's, nothing under it is picked
#define OVERLAY_HIT_TOOL        1       //the build bar: take up tool `value`
#define OVERLAY_HIT_STORE_ALLOW 2       //a store's card: let it take good `value`, or not
#define OVERLAY_HIT_CLOSE       3       //a card's close mark: clear the selection
#define OVERLAY_HIT_SITE_FIRST  4       //a site's card: put it first, or back in the queue
#define OVERLAY_HIT_FIELD_CROP  5       //a field's card: its starting crop to `value` (fields_plan.md)
#define OVERLAY_HIT_FIELD_NEXT  6       //...its next crop to `value`, a crop or ZONE_CROP_AUTO
#define OVERLAY_HIT_PAUSE       7       //the calendar's pause button: the clock stops, or goes again
#define OVERLAY_HIT_SPEED       8       //the calendar's speed button: the next speed, round to normal after the fastest
#define OVERLAY_HIT_FIELD_LAND  9       //a field's card: given to house `value`, 0 automatic (land_plan.md)
#define OVERLAY_HIT_PERSON      10      //the neediest list: select person `value`, and the view goes to him
#define OVERLAY_HIT_RELEASE     11      //a posted person's card: released from his post (defence_plan.md)
#define OVERLAY_HIT_TAKE_TOOL   12      //NEEDS ATTENTION's hint: take up tool `value` - and keep it up, unlike the grid
#define OVERLAY_HIT_BUILDING    13      //NEEDS ATTENTION's place: select building `value`, and the view goes to it
#define OVERLAY_HIT_GATE        14      //a gate's card: its door shut, or opened (defence_plan.md)
#define OVERLAY_HIT_LADDER      15      //a guard tower's card: its ladder, its door, to the next open side
#define OVERLAY_HIT_TOWER_GUARDS 16     //a guard tower's card: it takes `value` guards (defence_plan.md, step 8)
#define OVERLAY_HIT_GUARD_RELEASE 17    //a guard released from his tower: person `value`, 0 the one selected
#define OVERLAY_HIT_STORE_ALLOW_GROUP 18 //a store's card: let it take all of group `value` (HudGoodsGroup), or none
#define OVERLAY_HIT_KEEP        19      //a builders' hut's or ropewalk's card: the colony keeps `value` of what it makes
#define OVERLAY_HIT_MAKE_MORE   20      //...an order for `value` more of it - 0 cancels the one running (goods_plan.md)
#define OVERLAY_HIT_GATHER      21      //a posted person's card: gather a scout's pack first, or stop (defence_plan.md)
#define OVERLAY_HIT_FAMILY      22      //a house's or the camp's card: select person `value`, indoors or out - the view stays
#define OVERLAY_HIT_GROUP_DROP  23      //a group's card: person `value` taken out of the group (selection_plan.md)
#define OVERLAY_HIT_TENT        24      //a posted person's card: take a tent first, or not (settlements_plan.md)
#define OVERLAY_HIT_PITCH       25      //a person's card: the tent he carries pitched where he stands
#define OVERLAY_HIT_SETTLE      26      //a person's card: sent into another settlement and there, he moves in without a tent
struct OverlayHit{
    vec2 a, b;
    int action = OVERLAY_HIT_BLOCK;
    int value = 0;
};

/*
    NEEDS ATTENTION (docs/issues_plan.md, ApplicationChasmIssues.cpp): one row of what wants doing. Worked out
    from the published state whenever it is asked for, never saved. Tiers in the order they are listed.
*/
#define ISSUE_DANGER            0       //a next step whose deadline is under two days: red
#define ISSUE_COUNTDOWN         1       //a choice the game makes for the player when its time runs out
#define ISSUE_STUCK             2       //something placed that cannot work as it is
#define ISSUE_NEXT              3       //a next step: the tutorial
struct ChasmIssue{
    const char* kind = "";              //for the tool: choose_crop, build_water, ...
    int tier = ISSUE_NEXT;
    float order = 0.0f;                 //within its tier, the lower first
    std::string text;                   //as drawn, upper case
    std::string value;                  //at the row's right; may be empty
    float left = -1.0f;                 //a countdown's share still to run, 1 to 0; -1 none
    uint32_t building = 0;              //the place it is about - a click selects it - or 0
    int tool = -1;                      //...or the tool a click takes up
};
//Every issue, most urgent first.
void ChasmIssues(const ZoneState& z, const EconomyState& e, std::vector<ChasmIssue>& out);
//The middle of a building's plots, or of a field's cells - where the view goes to show it. False if it is gone.
bool ChasmBuildingPoint(const ChasmWorld& w, const ZoneState& z, uint32_t id, vec2& out);

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
//(and pick again to drop) the house, field, erase, garden, lot, road and wall tools.
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
#define INPUT_CHASM_TOOL_BUILDERS  INPUT_LAST+23     //G, for guild: B again
#define INPUT_CHASM_TOOL_WALL      INPUT_LAST+24     //7, beside the road: the debug walker's key, free since it went
#define INPUT_CHASM_TOOL_RANGE     INPUT_LAST+25     //R: the archery range (range_plan.md)
#define INPUT_CHASM_TOOL_TOWER     INPUT_LAST+26     //T: the guard tower (defence_plan.md)
#define INPUT_CHASM_TOOL_ROPEWALK  INPUT_LAST+27     //K, for knot: the ropewalk (goods_plan.md)

//The app's simulation commands, past core's.
#define CHASM_CMD_ZONE              SIM_CMD_LAST+0      //subtype: plot or coarse cell; value[0]: ZoneOp;
                                                        //value[1]: ground kind for ZONE_OP_GROUND_PAINT
#define CHASM_CMD_WALKER            SIM_CMD_LAST+1      //RETIRED with the debug walker (2026-10-07): never reuse
#define CHASM_CMD_CALENDAR          SIM_CMD_LAST+2      //sets the date: subtype the day, value[0] the tick in it
#define CHASM_CMD_NEEDS             SIM_CMD_LAST+3      //a person's needs set (a debug edit): subtype his id;
                                                        //value[0] health, value[1] water, in ticks - -1 leaves it
#define CHASM_CMD_ORDER             SIM_CMD_LAST+4      //a person posted (docs/defence_plan.md): subtype his id;
                                                        //value[0] the plot he is sent to, -1 released; value[1]
                                                        //its layer + 1, 0 its top (ramparts_plan.md); value[2] 1
                                                        //for one of a group's, spread round it (selection_plan.md)
#define CHASM_CMD_CRATE             SIM_CMD_LAST+5      //a crate put down (a debug edit, docs/crates_plan.md):
                                                        //subtype the plot; value[0] the good, value[1] how many
#define CHASM_CMD_STOCK             SIM_CMD_LAST+6      //a building's stock of a good set (a debug edit): subtype
                                                        //the building; value[0] the good, value[1] how many;
                                                        //value[2] 1 for a camp's pantry, not its supplies
#define CHASM_CMD_GUARD             SIM_CMD_LAST+7      //a guard posted by hand (docs/defence_plan.md, step 8): subtype
                                                        //the person; value[0] the tower, 0 released from his
#define CHASM_CMD_GUARDS            SIM_CMD_LAST+8      //a tower's guard count: subtype the tower; value[0] the count
#define CHASM_CMD_KEEP              SIM_CMD_LAST+9      //a made good's kept number (docs/goods_plan.md): subtype the good;
                                                        //value[0] the count; value[1] 1 for an order of that many more
                                                        //instead, a count of 0 cancelling it
#define CHASM_CMD_GATHER            SIM_CMD_LAST+10     //a scout's pack gathered first (docs/defence_plan.md, "Scouting"):
                                                        //subtype the person; value[0] 1 to gather, 0 straight on
#define CHASM_CMD_TENT              SIM_CMD_LAST+11     //a tent taken first (docs/settlements_plan.md): subtype the person;
                                                        //value[0] 1 to take one, 0 straight on
#define CHASM_CMD_PITCH             SIM_CMD_LAST+12     //the tent he carries pitched where he stands: subtype the person
#define CHASM_CMD_SETTLE            SIM_CMD_LAST+13     //sent into another settlement, he moves in without a tent: subtype the person

//What a left click does.
enum ChasmTool{
    CHASM_TOOL_SELECT = 0,
    CHASM_TOOL_HOUSE,
    CHASM_TOOL_FIELD,
    CHASM_TOOL_ERASE,
    CHASM_TOOL_GARDEN,
    CHASM_TOOL_LOT,
    CHASM_TOOL_ROAD,        //step 10: ground of kind road
    CHASM_TOOL_STORE,       //docs/buildings_plan.md: buildings of these kinds, a drag each
    CHASM_TOOL_WOODCUTTER,
    CHASM_TOOL_WATER,
    CHASM_TOOL_WINCH,       //docs/buildings_plan.md step 2: on the rim above a balcony
    CHASM_TOOL_CAMP,        //docs/people_plan.md: tents and fires
    CHASM_TOOL_BRIDGE,      //docs/bridge_plan.md: press on one bank, let go on the other
    CHASM_TOOL_BUILDERS,    //docs/builders_plan.md: a builders' hut, a drag
    CHASM_TOOL_WALL,        //line_works_plan.md step 3: a palisade, drawn as a line like a road
    CHASM_TOOL_RANGE,       //range_plan.md: an archery range, painted on coarse cells as a field is
    CHASM_TOOL_TOWER,       //defence_plan.md: a guard tower, a plot each - on a wall's plot too
    CHASM_TOOL_ROPEWALK,    //goods_plan.md: a ropewalk, a drag of up to four plots
    CHASM_TOOL_CASTLE,      //ramparts_plan.md: the debug castle's pieces, from the panel only - no key, never in play
    CHASM_TOOL_COUNT
};

//The tools that paint COARSE CELLS rather than plots: the field, and the range painted like one.
inline bool ChasmCellTool(int tool){
    return tool == CHASM_TOOL_FIELD || tool == CHASM_TOOL_RANGE;
}

//The tools that draw a LINE and place it on release (UpdateLineDrag), and the ground each lays.
inline bool ChasmLineTool(int tool){
    return tool == CHASM_TOOL_ROAD || tool == CHASM_TOOL_WALL;
}
inline int ChasmLineGround(int tool){
    return (tool == CHASM_TOOL_WALL) ? ZONE_GROUND_WALL : ZONE_GROUND_ROAD;
}

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
    /*
        What is under the MOUSE: the surface the engine read there, if it is the ground's or the zones' (surface_ids) -
        its plot and its layer (PickSurface) - else the ray, PickUnderPixel. Physics thread.
    */
    GridPick PickUnderMouse(int2 px);
    //The plot a surface at `at` facing `normal` belongs to, and the layer of it nearest that height (ramparts_plan.md).
    GridPick PickSurface(const vec3& at, const vec3& normal);
    std::vector<objectid_t> surface_ids;        //the terrain's and the zones' chunks: pickable, read by PickUnderMouse

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
    //Someone picked from the neediest list (ApplicationChasmSelect.cpp): the physics thread names him, the
    //camera glides to where he is - at the zoom it has, unlike the glide to the camp.
    std::atomic<uint32_t> cam_seek_person{0};
    std::atomic<uint32_t> cam_seek_building{0};     //...and a place picked from NEEDS ATTENTION, the same way
    bool f_cam_fly_keep_zoom = false;
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
    //The seasons' colours (docs/seasons_plan.md): the palette's temperate row rewritten on the GPU as the
    //season fades, from its rows as loaded. Render thread (ApplicationChasmSnow.cpp).
    Texture* palette_texture = NULL;
    std::vector<uint8_t> palette_rows;              //pixel rows of palette rows 0..PAL_WINTER, as loaded
    uint32_t palette_season_key = 0xFFFFFFFFu;      //the blend last written: from, to and its 255ths
    void UpdateSeasonPalette();

    std::mutex pick_mutex;
    //A script's hover, in place of the mouse's (chasm_tool hover_x/hover_z); the point under pick_mutex.
    std::atomic<bool> f_hover_pinned{false};
    vec2 hover_pin;
    //...and its buttons (chasm_tool button, right_click), so a script can drag a road: the left one
    //-1 the mouse's, 0 up, 1 down; a right press waiting for the next pass.
    std::atomic<int> script_left{-1};
    bool f_script_left_was = false;             //physics thread: for the edge of a scripted press
    std::atomic<bool> f_script_right{false};
    //...and a left click at a pixel in play mode (chasm_select op press), where the hover's is never on the
    //overlay: what the player's click there does, the overlay's buttons included. The pixel under pick_mutex.
    std::atomic<bool> f_script_press{false};
    std::atomic<bool> f_script_press_right{false};  //...made with the right button: an order (defence_plan.md)
    std::atomic<bool> f_script_press_shift{false};  //...with Shift held: a person added to the selection, or taken out
    int2 script_press_px;
    /*
        Where the mouse is over the window, for what the overlay shows on hover - the top bar's FOOD, its foods
        (goods_plan.md). Set every pass (physics thread), read by the drawing (render thread); -1 off the window.
        A script's pixel in its place while pinned (chasm_select op hover), so it can be looked at with no mouse.
    */
    std::atomic<int> overlay_mouse_x{-1};
    std::atomic<int> overlay_mouse_y{-1};
    std::atomic<bool> f_overlay_mouse_pinned{false};
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
    int zone_last_arch_lost = 0;                //under grid_mutex: the arch floors the last zone command took down
    std::atomic<uint32_t> zone_commands_done{0};
    void EnsureZonesWorld();                    //physics thread: a new map empties the zones
    void PublishZones();                        //physics thread
    std::shared_ptr<const ZoneState> GetZones();
    //Any thread; queued for the next tick. `owner`: the building a garden or lot is painted for (yards_plan.md),
    //or the one a building tool extends.
    void SubmitZone(int op, int index, int kind = 0, uint32_t stroke = 0, uint32_t owner = 0);
    //Whose yard the GARDEN or LOT tool paints for: the building selected in play, if it has a yard; else 0,
    //whatever the plot touches (yards_plan.md). A building tool's: the selected building of its kind, which it
    //extends (its card's Extend); else 0, a stroke of its own. Any thread.
    uint32_t PaintOwner(int tool);
    void RegisterCommandHandlers();

    std::atomic<int> paint_tool{CHASM_TOOL_SELECT};
    std::atomic<int> castle_shape{ZONE_CASTLE_BLOCK};   //the castle tool's piece (ramparts_plan.md), set in the panel
    std::atomic<int> castle_storeys{2};
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
    /*
        A building tool pressed on a building: its storey waits for the release, since only then is it known
        to be a click and not the start of a drag - which used to add a floor to the plot it started on. -1
        none. Physics thread.
    */
    int paint_press_plot = -1;
    int paint_press_kind = 0;
    uint32_t paint_press_owner = 0;
    void UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held);

    /*
        THE LINE DRAG (docs/line_works_plan.md, ApplicationChasmRoads.cpp): the road and wall tools draw
        their line FREEHAND and place it on release. While the button is held, `line_chain` is the plots
        the cursor has crossed, in order and joined along fine edges; nothing is sent until the button
        comes up, when the chain goes out as one stroke. A right press drops it. Written by the physics
        thread, read by the previews (the ghost, the pick view) under pick_mutex; line_version moves with
        every change.
    */
    enum LineDrag{ LINE_DRAG_NONE = 0, LINE_DRAG_DRAWING, LINE_DRAG_DROPPED };
    int line_drag = LINE_DRAG_NONE;             //physics thread; DROPPED waits for the left button to come up
    std::vector<int> line_chain;                //under pick_mutex
    bool f_line_erase = false;                  //under pick_mutex: a shift-drag, taking road (or wall) up
    int line_ground = ZONE_GROUND_ROAD;         //under pick_mutex: what the line lays - its tool's, at the press
    std::atomic<uint32_t> line_version{0};
    void UpdateLineDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held);
    void PlaceLine();
    void DropLine();
    //Whether a line of `ground` may take `plot` - or, erasing, take its ground up: the release's rule and the
    //previews' colour. `ex` may be NULL (all explored). Any thread, on published state.
    bool LinePlotAllowed(const ChasmWorld& w, const ZoneState& z, const Exploration* ex, bool f_erase, int ground,
                         int plot, const char** why);

    /*
        --- Roads and walking (step 10, ApplicationChasmRoads.cpp; docs/roads_plan.md) ---------------
        A road is ground, painted through the zone commands. `walkers` is the plot graph the economy's
        people find their ways on (Walkers.h) - no state of its own beyond what follows from the world
        and the zones; the debug walker it once carried was retired 2026-10-07.
    */
    Walkers walkers;
#ifdef USE_MCP
    void RegisterRoadTools();
#endif

    /*
        --- The economy (docs/economy_plan.md, P2 + P3; ApplicationChasmEconomy.cpp) -----------------
        `economy` is SIMULATION state, the physics thread's alone, ticked after the zones; a copy is
        published after every tick that changed it (economy_snapshot, under grid_mutex) - the zones'
        pattern again. The view reads the copy: felled trees and stumps, the workers, the fields' growth.
        Its commands are CHASM_CMD_NEEDS, a debug edit of a person's health and water, CHASM_CMD_ORDER,
        a person sent to a post or released (defence_plan.md), CHASM_CMD_GUARD and CHASM_CMD_GUARDS, a guard
        posted by hand or released and a tower's guard count (step 8), and CHASM_CMD_CRATE, a debug crate put
        down (crates_plan.md) - recorded, so a replay makes the same edit on the same tick.
    */
    Economy economy;
    std::shared_ptr<const EconomyState> economy_snapshot;
    std::atomic<uint32_t> needs_commands_done{0};
    std::atomic<uint32_t> order_commands_done{0};      //CHASM_CMD_GUARD's too: a right click either way, refused alike
    std::atomic<uint32_t> guards_commands_done{0};
    std::atomic<uint32_t> keep_commands_done{0};
    std::atomic<uint32_t> crate_commands_done{0};
    std::atomic<uint32_t> stock_commands_done{0};
    std::atomic<uint32_t> crate_refusals{0};
    std::string crate_last_refusal;             //under grid_mutex, like order_last_refusal
    //An order the economy refused, and why (Economy::Order) - the play overlay shows it as it does a
    //refused placement. The text under grid_mutex.
    std::atomic<uint32_t> order_refusals{0};
    std::string order_last_refusal;
    uint32_t order_refusal_seen = 0;            //render thread: the last one shown
    /*
        A GROUP's orders (selection_plan.md, "Several people selected", step 3), told as ONE line once all of them are
        done - "2 of 3 sent - no way there" - rather than a refusal each: how many (0 none waiting), the commands count
        they are all done at, the refusals before them, and when they were sent (steady clock), not to wait for ever.
    */
    std::atomic<uint32_t> group_orders{0};
    std::atomic<uint32_t> group_orders_done_at{0};
    std::atomic<uint32_t> group_orders_refused_at{0};
    std::atomic<int64_t> group_orders_since{0};
    //A player's command that took an arch's floors down (arches_plan.md step 4): told in the refusal's place, in amber.
    std::atomic<uint32_t> arch_notices{0};
    uint32_t arch_notice_seen = 0;              //render thread: the last one shown
    std::string refusal_shown;                  //render thread: the refusal on screen, either kind, whole
    bool f_refusal_notice = false;              //render thread: ...or the arch's notice, which is no refusal
    float refusal_seconds = 3.0f;               //render thread: ...on screen this long - a rules' notice longer
    //A NOTICE from the rules (Economy.h; defence_plan.md, "Scouting") told in the refusal's place, in amber - render thread.
    uint32_t notices_seen = 0;                  //EconomyState::notices_made already told of
    const ChasmWorld* notices_world = nullptr;  //the map they were counted on
    //A DEATH told on the play overlay (needs_plan.md step 5) - render thread.
    size_t deaths_seen = 0;                     //the dead already told of
    const ChasmWorld* deaths_world = nullptr;   //the map they were counted on: a new map's dead are old news
    std::string death_shown;
    std::chrono::steady_clock::time_point death_since;
    void TickEconomy();                         //physics thread, from RunSimulationTick
    void PublishEconomy();                      //physics thread
    std::shared_ptr<const EconomyState> GetEconomy();
    void HashEconomy(StateHash& h);
    void RegisterEconomyCommands();
    void SubmitNeeds(uint32_t person, int health, int water);  //any thread; -1 leaves one as it is
    //Any thread; plot -1 releases him, layer -1 its top; f_spread for one of a group's (Economy::Order).
    void SubmitOrder(uint32_t person, int plot, int layer = -1, bool f_spread = false);
    void SubmitGuard(uint32_t person, uint32_t tower);          //any thread; tower 0 releases him from his
    void SubmitGuards(uint32_t tower, int count);               //any thread
    void SubmitKeep(int good, int count, bool f_order);         //any thread; f_order: `count` more, 0 cancelling
    void SubmitGather(uint32_t person, bool f_on);              //any thread; refused as an order is
    void SubmitTent(uint32_t person, bool f_on);                //any thread; refused as an order is
    void SubmitPitch(uint32_t person);                          //any thread; refused as an order is
    void SubmitSettle(uint32_t person);                         //any thread; refused as an order is
    void SubmitCrate(int plot, int good, int amount);           //any thread; a debug edit
    json EconomyBuildingJson(uint32_t id);      //a building's stock, what it takes, its worker
#ifdef USE_MCP
    void RegisterEconomyTools();
#endif
#ifdef USE_IMGUI
    void RenderEconomyInfo(uint32_t id);        //in the building info, for the building under the cursor
    void RenderSelectedStore(int plot);         //what the selected store takes, to be set there
    void RenderSelectedCrate(int plot);         //the crate on the selected plot, or one to put down there
    void RenderPeoplePanel();                   //the villagers, their health and water to be set
    std::map<uint64_t,float> needs_editing;     //render thread: a slider being dragged, by person and bar
    int crate_edit_good = 0;                    //render thread: what the panel's crate holds, and how much
    int crate_edit_amount = 10;
#endif

    /*
        --- Exploration and the clouds (docs/play_mode_plan.md steps 2-4, ApplicationChasmExplore.cpp) ---
        `exploration` is SIMULATION state, the physics thread's alone: cleared by what the people see as
        the tick goes (Exploration.h), saved and hashed, a copy published when it changes. The clouds
        are its view: puffs over what is not explored, shown in play mode and in debug mode only when
        asked for.
    */
    Exploration exploration;
    std::shared_ptr<const Exploration> exploration_snapshot;   //under grid_mutex
    /*
        Physics thread: per cell, the eye's ground it was last looked round from (NAN never). A look
        from the same place and height clears the same cells again, and explored stays explored, so it
        is skipped - the daily walk to work costs nothing after the first. Not saved: emptied with the
        exploration on a load, after which a look is taken once more and clears nothing the save had not.
    */
    std::vector<float> looked_from;
    //The ground a person at `at`, in exploration cell `cell`, looks from: his own plot's, at the cell's middle.
    static float SightEyeGround(const ChasmWorld& w, const Exploration& ex, int cell, const vec2& at);
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

    //The hearths (needs_plan.md step 3, ApplicationChasmHearth.cpp): a warm home's chimney smoke and lit windows.
    Mesh* hearth_puff_mesh = NULL;
    Mesh* hearth_pane_mesh = NULL;
    int smoke_material = -1;                    //the palette moving as core's INSTANCE_MOTION_LIFE
    int glow_material = -1;                     //...and unlit
    Object* hearth_smoke = NULL;
    Object* hearth_panes = NULL;
    std::shared_ptr<const ChasmWorld> hearth_built_world;
    uint32_t hearth_built_zones = 0xFFFFFFFFu;
    std::vector<uint8_t> hearth_built_heat;     //the economy's heat they were built for
    void BuildHearthScene();                    //from BuildScene
    void UploadHearth();                        //render thread, from PreRender
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
        bool f_settled = true;                  //inside a settlement's territory (settlements_plan.md)
        uint32_t line_version = 0;              //a road being drawn: its chain (0 with none)
        int bridge_from = -1;                   //a bridge being dragged: the bank it starts from
        uint32_t owner = 0;                     //a yard being painted: the building it is for (yards_plan.md)
        uint32_t crates_version = 0;            //the crates, which nothing may be built over (crates_plan.md)
        uint32_t felled_version = 0;            //the trees, which the price tag counts (woodcutting_plan.md)
        bool operator==(const GhostKey& o) const{
            return world == o.world && zones_version == o.zones_version && plot == o.plot && coarse == o.coarse
                   && tool == o.tool && f_explored == o.f_explored && f_settled == o.f_settled && line_version == o.line_version
                   && bridge_from == o.bridge_from && owner == o.owner && crates_version == o.crates_version
                   && felled_version == o.felled_version;
        }
    };
    GhostKey ghost_built;
    void BuildGhostScene();                     //from BuildScene
    void SetGhostUniforms();
    void UpdateGhost();                         //render thread, from PreRender
    /*
        THE PRICE TAG (the user's, 2026-10-11): what the ghost would cost to build, by the cursor - worked out with the
        ghost, from the rules' own costs (EconomyKindStoreyCost and the rest), so it is what the site will ask for. And
        hovering a tool on the build bar, what one plot or cell of it costs, about: a plot's cost goes by its area.
    */
    std::string ghost_cost_text;                //under pick_mutex: the tag as drawn, empty for none (chasm_tool reads it)
    bool f_ghost_cost_ok = true;                //render thread: whether the ghost is green, for the tag's colour
    int bar_tip_tool = -1;                      //render thread: the tool hovered on the build bar, -1 none
    vec2 bar_tip_a, bar_tip_b;                  //...and its cell
    const ChasmWorld* cost_typical_world = nullptr;     //render thread: the map cost_typical_plot was found on
    int cost_typical_plot = -1;                 //...its plot of the median area, the bar tip's "about"
    std::string ToolCostTip(int tool);          //render thread: the bar tip's words for a tool, empty for none
    void DrawCostTips(float s);                 //render thread, from DrawOverlay in play mode: the tag and the bar tip

    /*
        --- The economy, drawn (ApplicationChasmWorkersView.cpp, and the forest and zones below) -----
        Each worker a walker's figure in his job's tunic, with what he carries on him; one object per
        worker, reused, placed every frame from the published state. Render thread.
    */
#define WORKER_LOADS        5       //what a figure can carry: nothing, a log, a sack, a pail, a tent (WorkersView's LOAD_*)
    Mesh* worker_meshes[PERSON_FIGURES * WORKER_LOADS] = {};    //figure (PERSON_*) * WORKER_LOADS + load
    std::vector<Object*> worker_objects;
    std::vector<int> worker_object_mesh;            //per object: which of worker_meshes it has, -1 none
    /*
        A RIGGED figure - one whose node in chasm_props.glb has a skin - is drawn skinned instead: per
        worker a Skeleton sharing the figure's one skinned mesh, playing the file's clips, with each load
        a child of the bone that carries it. The others stay static until they are rigged.
    */
    std::string worker_skin_name[PERSON_FIGURES];   //its skin, "" while the figure is not rigged
    Mesh* worker_skin_mesh[PERSON_FIGURES] = {};
    Mesh* worker_skin_load[PERSON_FIGURES * WORKER_LOADS] = {};     //figure * WORKER_LOADS + load: in its bone's space
    float worker_figure_fit[PERSON_FIGURES] = {};   //its height over the man's (the loads follow it)
    float worker_figure_height[PERSON_FIGURES] = {};    //its height as drawn, feet to crown: what a pick near him spans
    float worker_walk_speed[PERSON_FIGURES] = {};   //world units a second its walk clip carries it at rate 1
    bool f_worker_idle_clip = false;                //the file has an idle clip to stand in
    /*
        AN ARCHER'S KIT (range_plan.md "Shooting"), on an archer or a guard with a rigged figure: a BOW in his left
        hand, placed every frame from his hands - upright as he carries it, square to his arrow while he shoots -
        and a QUIVER on his back, a child of his spine. The arrow in his right hand is drawn with the range's
        arrows (worker_hand_arrows). The file's `bow` and `quiver` where it has them, stand-ins where not.
    */
    Mesh* worker_bow_mesh = NULL;                   //in the figures' units, its grip at the origin, facing +z
    Mesh* worker_skin_quiver[PERSON_FIGURES] = {};  //in that figure's Spine's space
    bool f_worker_shoot_clip = false;               //the file has the Shoot clip
    float worker_shoot_seconds = 0.0f;              //its length: one shot
    vec3 worker_release_at[PERSON_FIGURES];         //where his arrow leaves the bow: from his feet, facing +z, world units
    std::vector<fmat4> worker_hand_arrows;          //this frame's arrows in hand, world: UploadWorkers fills, UploadRange draws
    struct WorkerSkin{
        Object* figure = NULL;                      //the Skeleton, built the first time he is this figure
        Object* load[WORKER_LOADS] = {};            //indexed by load; [0], carrying nothing, is never built
        Object* bow = NULL;                         //his kit, shown while he is an archer
        Object* quiver = NULL;
        Bone* arm[4] = {};                          //Arm.Lower.L, Hand.L, Arm.Lower.R, Hand.R: where the bow and arrow go
        int clip = 0;                               //WORKER_CLIP_*, as last set
        float rate = 1.0f;
    };
    std::vector<std::array<WorkerSkin,PERSON_FIGURES>> worker_skins;    //per worker, per figure
    void BuildWorkerScene();                        //from BuildScene
    void UploadWorkers();                           //from PreRender
    /*
        THE CROWD BENCH (ApplicationChasmCrowdBench.cpp): a measurement, not a feature - skinned
        adult_male figures walking in place around the camera target, sharing one mesh, to find what
        skinned villagers cost at 100-200. MCP tool chasm_crowd_bench. Render thread, except the request.
    */
    Mesh* crowd_mesh = NULL;                        //chasm_props.glb's adult_male, skinned; shared by every figure
    std::vector<Object*> crowd_objects;             //Skeletons, built as asked for, hidden and frozen beyond crowd_shown
    std::vector<Object*> crowd_plain;               //the comparison: plain Objects with the static figure
    int crowd_shown = 0;
    std::mutex crowd_mutex;                         //the request: count, centre and kind, from the MCP thread
    int crowd_want = 0;
    vec3 crowd_centre;
    bool f_crowd_static = false;
    int crowd_request = 0;                          //bumped with each request...
    int crowd_applied = 0;                          //...and the last one the render thread laid out
    void BuildCrowdBench();                         //from LoadProps
    void UploadCrowdBench();                        //from PreRender
    void RegisterCrowdBenchTool();                  //from RegisterMCPTools
    //The woodcutters' log piles outside their huts (ApplicationChasmPiles.cpp). Render thread.
    Object* pile_set = NULL;
    std::shared_ptr<const ChasmWorld> pile_built_world;
    std::vector<std::pair<uint32_t,int>> pile_built;    //hut, logs - as last drawn
    uint32_t pile_built_zones = 0;
    void UploadPiles();                             //from PreRender
    //The crates lying about (ApplicationChasmCrates.cpp, crates_plan.md): one set of chasm_props.glb's crate.
    #define CHASM_CRATE_ASSET "crate_a"
    Object* crate_set = NULL;
    bool f_crate_loaded = false;                    //LoadProps found it
    std::shared_ptr<const ChasmWorld> crate_built_world;
    uint32_t crate_built_version = 0xFFFFFFFFu;     //the economy's crates_version, as last drawn
    void UploadCrates();                            //from PreRender
    /*
        The archery ranges (ApplicationChasmRange.cpp, range_plan.md): targets, hay bales and arrows, an instance
        set each - chasm_props.glb's prop by its name in range_asset_names, or a stand-in until it is modelled.
    */
    #define RANGE_PROPS         4                   //target, hay bale, arrow (stuck), arrow_full (in hand and in flight)
    Object* range_set[RANGE_PROPS] = {};
    Mesh* range_mesh[RANGE_PROPS] = {};
    std::shared_ptr<const ChasmWorld> range_built_world;
    uint32_t range_built_zones = 0xFFFFFFFFu;       //the zones' version, as last drawn
    std::vector<int> range_target_cell;             //each target drawn: its cell, where it stands, how it is turned,
    std::vector<vec3> range_target_pos;
    std::vector<float> range_target_yaw;
    std::vector<float> range_target_back;           //...and how far behind it its backstop stands, where misses land
    uint64_t range_arrows_key = ~0ull;              //the arrows as last drawn: rebuilt only when they change
    bool f_range_flying = false;                    //arrow_full's set had arrows last frame
    void BuildRangeScene();                         //from LoadProps
    void UploadRange();                             //from PreRender
    void BuildRangeProps(const ChasmWorld& w, const ZoneState* z);  //from UploadRange, when the zones change
    /*
        THE SHOT CLOCK (range_plan.md "Shooting"): a trainee on the line shoots one shot a turn of the Shoot clip from
        the tick he arrives, its length stretched so his stint holds a whole number - all of it read from his stint's
        timer, so his clip, his arrows in the air and his arrows in the target keep one time, and nothing is saved.
        False when he is not training.
    */
    struct ShotClock{
        float elapsed = 0.0f;                       //seconds into his stint
        float cycle = 0.0f;                         //seconds a shot
        int shots = 0;                              //shots a stint
        uint64_t start = 0;                         //the tick his stint began: with his id, which stint this is
    };
    bool ShotClockOf(const EconomyWorker& k, uint64_t tick, ShotClock& out) const;     //ApplicationChasmRange.cpp
    //The ropewalks' walks, wheels, coils and sheaves (ApplicationChasmRopewalk.cpp, goods_plan.md): one mesh for all.
    Object* ropewalk_object = NULL;
    std::shared_ptr<const ChasmWorld> ropewalk_built_world;
    uint32_t ropewalk_built_zones = 0xFFFFFFFFu;    //the zones' version, as last drawn
    std::vector<std::array<uint32_t,3>> ropewalk_built;     //ropewalk, coils, sheaves - as last drawn
    void UploadRopewalks();                         //from PreRender
    //SETTLEMENTS, drawn (docs/settlements_plan.md, ApplicationChasmBorders.cpp): the territories' border posts, and
    //with a person selected, the walk to the plot under the cursor - the render thread's own route search for it.
    Object* border_object = NULL;
    std::shared_ptr<const ChasmWorld> border_built_world;
    uint32_t border_built_zones = 0xFFFFFFFFu;      //the territories' zones version, as last drawn
    void UploadBorders();                           //from PreRender
    Walkers walk_tip_walkers;
    std::array<int,4> walk_tip_key = {{-1,-1,-1,-1}};   //person, the plot he is on, the plot under the cursor, zones
    float walk_tip_length = -1.0f;                  //...and its walk, -1 no way there
    void DrawWalkTip(float s);                      //render thread, from DrawOverlay in play mode

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
    std::atomic<int> speed_index{0};            //into the play speeds in ApplicationChasmTime.cpp, -1 for another factor
    void TickCalendar();                        //physics thread, from RunSimulationTick
    void SetCalendar(uint64_t tick);            //physics thread: a load, a new map, the date command
    void RegisterTimeCommands();
    std::atomic<uint32_t> calendar_commands_done{0};
    void UpdateTimeKeys();                      //physics thread, from UpdateView: pause and speed
    //Whether the keys are the game's: input live (the window ours), and the debug UI not taking them - a text field
    //being typed in, a save's name (the user's, 2026-10-11: typing one changed tools, paused, and N made a new map).
    bool KeysLive();
    void PressTimeButton(int action);           //physics thread, from UpdatePick: OVERLAY_HIT_PAUSE or _SPEED
    void SetSpeed(int index);                   //any thread: a play speed
    void SetSpeedFactor(float factor);          //any thread: any factor 1-PHYSICS_TIME_FACTOR_MAX (the tool, the panel)
    int SpeedCount();
    float SpeedFactor(int index);
    int NearestSpeed();                         //the play speed nearest the factor running now
    //Published once a pass, after its ticks (UpdateView): a batched pass (fast-forward) runs several, and
    //only the last one's copy would ever be looked at. Physics thread.
    bool f_economy_unpublished = false;
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
    void SetIceUniforms(Shader* s);     //the water's freezing, from the same front (ApplicationChasmSnow.cpp)
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
    std::vector<Object*> prop_sets;                 //chunk * ForestSetCount() + set: the kinds' own, then the looks
    /*
        The broadleaves' stages (Seasons.h, ApplicationChasmSeasons.cpp): a mesh per seasonal kind and
        stage, its LOOK, each drawn as one more set per chunk after the kinds' own.
    */
    std::vector<Mesh*> season_look_mesh;            //by look
    std::vector<int> season_look_kind;
    std::vector<int> season_look_stage;
    int season_look_set[PROP_KIND_COUNT][FOREST_STAGES] = {};  //the set a standing prop of kind, stage is drawn in
    void BuildSeasonLooks();                        //from LoadProps
    int ForestSetCount() const { return PROP_KIND_COUNT + (int)season_look_mesh.size(); }
    std::vector<uint8_t> forest_prop_stage;         //per prop: the stage its chunk's sets were built with
    int64_t forest_stage_clock = -1;                //the quarter day the stages were last looked at
    void ForestStagesMoved(const ForestData& f, std::vector<uint8_t>& dirty);
    //`e`: the economy for this world, or NULL - a felled tree is a stump, with its log while it lies.
    void RebuildPropChunk(const ChasmWorld& w, const ZoneState* z, const EconomyState* e, int chunk);
    std::shared_ptr<const ChasmWorld> forest_built_world;
    std::vector<uint32_t> forest_zone_version;      //per chunk: the zones' version it was hidden for
    std::vector<uint8_t> forest_prop_state;         //per prop: the PROP_STATE_* the sets were built with
    std::vector<int> forest_prop_chunk;             //per prop: the chunk whose sets hold it
    uint32_t forest_felled_version = 0xFFFFFFFFu;   //the economy's felled_version they were built at
    std::atomic<bool> f_view_forest{true};
    bool f_forest_shown = true;
    bool f_forest_flowers = true;                   //the flowers drawn: spring and summer (docs/seasons_plan.md)
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
    std::vector<uint32_t> play_group;       //the people of a PLAY_PICK_GROUP selection, two or more, in the order added
    /*
        ...and the people selected before a building was: a house's card, which names its family, is reached by
        selecting the house - so a Shift+click on a name there adds to these rather than to the house. Forgotten when
        the selection is cleared.
    */
    std::vector<uint32_t> play_people_before;
    std::atomic<uint32_t> play_pick_version{0};
    /*
        The selection as PEOPLE (selection_plan.md, "Several people selected"): one is a person selected as ever, two
        or more a GROUP - grown-ups only, since only they take orders - none, nothing. Any thread; they take pick_mutex.
    */
    void SelectPeople(std::vector<uint32_t> ids);
    //Shift+click: a grown-up added to the selection, or taken out of it - on a card's name, to the people before it.
    void ToggleSelected(uint32_t person, bool f_card_name = false);
    std::vector<uint32_t> SelectedPeople();     //the group, or the one person selected, or none
    void SelectLocked(const PlayPick& p);       //anything but people selected (pick_mutex held): the people kept
    void PruneGroup();                          //physics thread: the dead taken out of the group
    /*
        Who each drawn figure is, by object id: a person's figure - static or his skeleton - and the load on it.
        The people are PICKABLE, so the engine reads back which of them is under the cursor (PlayPickUnder).
        Rebuilt every frame by UploadWorkers (render thread); under pick_mutex.
    */
    std::vector<std::pair<objectid_t,uint32_t>> person_objects;
    std::vector<OverlayHit> overlay_hits;
    std::vector<OverlayHit> overlay_hits_drawing;           //render thread, filled while drawing
    int2 right_press_px;                                    //physics thread: where a right press began
    bool f_right_press = false;
    /*
        THE BOX (selection_plan.md, "Several people selected", step 2): with no tool in hand a left press is a CLICK,
        acted on when let go within RIGHT_CLICK_SLOP of where it began - moved further, a BOX, which on letting go
        selects every grown-up out of doors whose figure's middle is inside it, or adds them with Shift. A drag never
        clears: an empty box leaves the selection as it was.
    */
    int2 left_press_px;                                     //physics thread: where a left press in select mode began
    bool f_left_press = false;                              //...ours, and not let go yet
    bool f_box_drag = false;                                //...and moved past the slop: a box
    bool f_box_shown = false;                               //under pick_mutex: the box drawn, box_a to box_b in pixels
    bool f_box_scripted = false;                            //...held there by chasm_select op box hold, not the mouse
    vec2 box_a, box_b;
    std::vector<uint32_t> box_people;                       //under pick_mutex: who it would take, outlined as hovered
    std::vector<uint32_t> PeopleInBox(vec2 a, vec2 b);      //physics or tool thread
    void TakeBox(const std::vector<uint32_t>& ids, bool f_add);    //the box let go: its people selected, or added
    /*
        The selection's outline-only objects: 0 the selected building, 1 the hovered one, 2 every building
        that BELONGS WITH the selection merged into one, 3 the ground that does (a woodcutter's lot) - see
        RelatedPlaces in ApplicationChasmSelect.cpp.
    */
    static const int SELECT_OUTLINES = 4;
    Object* select_outline[SELECT_OUTLINES] = {};
    uint32_t select_outline_id[SELECT_OUTLINES] = {};       //render thread: the building each holds (0, 1) - or ground, as a site names it
    uint32_t select_outline_zones[SELECT_OUTLINES] = {};    //...built at this zones version
    std::vector<uint32_t> select_related_buildings;         //render thread: what slot 2 was built from
    std::vector<int> select_related_plots;                  //...and slot 3
    //A crate's outlines (crates_plan.md): the selected one, the hovered one - outline-only copies of the crate.
    Object* crate_outline[2] = {};
    //Where the selected person is going: dots along his route and a ring at its end. Render thread; rebuilt when its key moves.
    Object* path_marks = NULL;
    uint64_t path_marks_key = 0;
    PlayPick PlayPickUnder(int2 px, const GridPick& hover);
    void UpdatePlayPick(GridPick& hover, int2 px, bool& f_over_scene, bool& f_clicked);   //physics thread
    bool OverlayHitAt(int2 px, OverlayHit* hit);
    //MCP thread: the engine's pick read at `px` (InputController::PinPickPixel) - waits for an answer read there,
    //a few frames; false if none came. The pin stays until RestorePickPin.
    bool WaitForPickAt(int2 px);
    void RestorePickPin();                                  //...back to the hover's pin (chasm_select op hover), or none
    void UploadSelection();                                 //render thread, from PreRender
    float DrawBuildBar(float s);                            //render thread, from DrawOverlay; the top of it and its BUILDING line
    float DrawIssues(float s, float bottom);                //render thread, from DrawOverlay: over the grid (issues_plan.md); its top
    float DrawJobsOverview(float s);                        //render thread, from DrawOverlay: builders_plan.md; its top
    void DrawNeediest(float s, float bottom);               //render thread, from DrawOverlay: over the jobs
    void DrawSelectionOverlay(float s);                     //render thread, from DrawOverlay: card + pins
    vec2 build_bar_min;                                     //render thread: where the tools' grid was last drawn, for the overview
    vec2 build_bar_max;
    float build_stack_top = 0.0f;                           //render thread: the top of the grid and all stacked over it
    float build_stack_left = 0.0f;                          //...and its left edge: the grid's, or a wider panel's over it
#ifdef USE_MCP
    void RegisterIssueTools();
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
    std::atomic<bool> f_view_camp{false};   //the spots a new colony might camp on, and their scores
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
