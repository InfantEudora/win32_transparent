#ifndef _APPLICATION_BOMBER_H_
#define _APPLICATION_BOMBER_H_

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Application.h"
#include "Texture.h"

/*
    bomber - a volumetric-effects bench on its way to becoming a bomberman game.

    Stage 0, which is what this is: a ground plane, two identical little maze patches, an orbit
    camera, and one raymarched blast that can be retriggered. The blast is the point; the cubes are
    there so it has something to sit among, to be occluded by, to be stopped by, and to light.

    --- THE TWO BLAST SITES ARE THE SAME BLAST, DRAWN TWO WAYS ------------------------------------
    The left site draws the cross as ONE VOLUME PER TILE, the way the game is actually built; the
    right one draws it as a SINGLE VOLUME whose density is shaped like the cross. Both go off on
    the same tick, from the same button, with the same arm lengths, in front of the same wall
    arrangement - so what differs between them on screen is only the thing being compared. See the
    long note at the top of shaders/bomber_explosion.frag for what each costs; the short version is
    that tiles follow the maze for free but do not depth-sort against each other, and the single
    volume composites perfectly but has to be told how long its arms are.

    Keeping them side by side rather than behind a switch is deliberate. A toggle means looking at
    one, remembering it, and looking at the other, and the differences here are the kind that
    memory is bad at.

    --- WHY A BENCH AND NOT JUST testfx ----------------------------------------------------------
    apps/testfx is the right place to develop a shader in isolation - one fullscreen quad, a file
    list, live reload. This is the place to develop one IN A SCENE, which is a different problem:
    the blast has to be the right size next to a wall, has to be occluded by the wall in front of
    it, has to stop at the wall beside it, and has to light the walls around it. None of those can
    be checked against an empty viewport.

    --- THE BLAST IS OBJECTS, NOT A POST-PASS ----------------------------------------------------
    Every volume is a MESH_MODE_SHADER box drawn by Renderer::CustomShaderPass, exactly like the
    ship's clouds and breakout's shield. So each is positioned and scaled by moving an ordinary
    Object; each only costs the pixels its box covers; and all the tiles of a site share one mesh,
    so the nine of them are one draw call. The shader owns its own depth state - see
    SetBlastUniforms.

    --- THE BLAST CLOCK IS IN TICKS ---------------------------------------------------------------
    `blast_age` is a count of simulation ticks since detonation, not seconds. Durations in this
    codebase are ticks (Scene::GetPhysicsTick), and the payoff here is the usual one: sim_pause
    freezes the fireball mid-expansion, sim_step advances it by an exact number of frames, and a
    screenshot of frame N of a blast is reproducible. A wall clock would make all three impossible,
    and a volumetric effect is precisely the kind of thing that has to be looked at a frame at a
    time.

    --- THREADING ---------------------------------------------------------------------------------
      - Init()               render thread, once. All GL, all mesh building, all registration.
      - RunSimulationTick()  physics thread, once per tick that RUNS, physics_mutex held. The
                             detonation and the blast clock live here, so they pause and step.
      - UpdateView()         physics thread, every pass including paused ones. Camera and the F5
                             key only - nothing a tick will read.
      - PreRender()          render thread, top of every frame. Where a shader reload happens.
      - SetBlastUniforms()   render thread, inside the custom-material pass with the program bound.
                             Reads the published view state, never the live simulation.
      - DrawImGuiUI()        render thread, physics_mutex held. Never waits on the sim.
      - MCP handlers         their own thread, NO lock. They submit commands and read knobs.
*/

//Our own input actions, numbered from INPUT_LAST like every other app's.
#define INPUT_BOMBER_DETONATE       INPUT_LAST+1
#define INPUT_BOMBER_RELOAD_SHADER  INPUT_LAST+2

/*
    Our own simulation commands, numbered from SIM_CMD_LAST.

    Detonating is intent arriving from OUTSIDE the simulation - an MCP call today, a bomb timer
    expiring tomorrow - so it goes on the command queue rather than being done on the caller's
    thread. The handler runs on the physics thread at the top of a tick with physics_mutex held,
    which is the only place it is safe to restart the blast clock. The KEY press does not use this
    path: it is already read on the physics thread inside the tick.
*/
#define BOMBER_CMD_DETONATE         SIM_CMD_LAST+0

//--- the maze's vocabulary ----------------------------------------------------------------------
/*
    One cell of the grid, in world units, and how much of it a wall cube fills.

    Named now, with only eight cubes to place, because every single thing that comes next - the
    maze generator, the blast reaching along the grid, a walking player - measures itself in cells,
    and a 2.0 buried in eight constructor calls is eight places to fix later.
*/
#define BOMBER_CELL_SIZE            2.0f
#define BOMBER_WALL_SIZE            1.8f

//The two kinds of wall a bomberman maze is made of. Green is the indestructible pillar grid, brown
//is the destructible fill - which is what the blast is eventually for.
#define BOMBER_WALL_HARD            0
#define BOMBER_WALL_SOFT            1

//How many tiles the flame reaches from the bomb, before the maze has its say. The classic's
//starting bomb, and the number the FLAME power-up would raise.
#define BOMBER_BLAST_RANGE          2
//Centre tile plus one line of tiles per direction. Nine at range 2.
#define BOMBER_MAX_TILES            (1 + 4 * BOMBER_BLAST_RANGE)

/*
    World size of a volume's box, per mode.

    A TILE box is two cells, which is a cell of flame plus the margin its billows need - the flame
    has to be able to bulge past the tile it belongs to or the cross looks like a row of beads.

    The CROSS box has to contain the whole plus: range cells each way (4 units), plus the flame's
    own radius, plus the rise. 12 gives that with room to spare. It is the price of the single
    volume and it is worth being honest about: most of what it marches is empty, which is why
    BOMBER_CROSS_VIEW_STEPS below is higher - the same step COUNT over a longer interval is a
    coarser sample, and the arms go stripy if that is not paid for.

    Both are UNIFORM cubes on purpose. The shader's shape maths is done in object space, so a
    non-uniform box would stop an object-space length being a world length - the trap
    raymarch_volume.frag documents at its light march. Keeping them cubes is what lets
    PushBlastUniforms convert a world-unit knob into object units by dividing by one number.
*/
#define BOMBER_TILE_BOX             (2.0f * BOMBER_CELL_SIZE)
#define BOMBER_CROSS_BOX            12.0f
#define BOMBER_CROSS_VIEW_STEPS_MUL 2

//How long a blast lives, in ticks, as the DEFAULT for the knob below. At 60 TPS this is a little
//under two seconds, which is about right for a fireball that ends as drifting smoke.
#define BOMBER_BLAST_LIFE_TICKS     110.0f

//Resolution of the 3D noise the blast samples, and how many worley cells the lowest-frequency
//channel gets. 96^3 RGBA8 is 3.4 MB - smaller than the ship's 128 because a fireball is looked at
//for a second and a half, not stared into.
#define BOMBER_NOISE_RESOLUTION     96
#define BOMBER_NOISE_CELLS          4

/*
    Per-mode shape settings that are NOT knobs, because they are not matters of taste.

    A fireball burns at its front and drags exhausted gas behind it, so it is a hollow shell and
    its front is the hot part. A flame running down a corridor is a jet: solid, and hottest along
    its axis. Making these sliders would only offer the chance to set one of them wrong.
*/
#define BOMBER_TILE_SHELL           0.45f   //fraction of the radius that is burning
#define BOMBER_TILE_CORE_HEAT       0.0f    //0 = hottest at the front
#define BOMBER_CROSS_SHELL          1.00f   //solid: a hollow arm reads as a pipe
#define BOMBER_CROSS_CORE_HEAT      1.0f    //1 = hottest along the skeleton

/*
    One tunable of the effect, as both the ImGui panel and the bomber_set MCP tool see it.

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

//A cell of the grid. Ordered so it can live in a std::set, which is all the occupancy map this
//app needs until there is a maze big enough to want an array.
typedef std::pair<int,int> BomberCell;

/*
    Everything one blast site owns.

    The two sites differ ONLY in `mode` and therefore in which shader and mesh they use; every
    other field is filled the same way from the same layout code, which is what makes the
    comparison worth anything.
*/
struct BomberSite{
    int mode = 0;                       //BLAST_MODE_TILE / _CROSS, matching the shader's defines
    int cell_x = 0;
    int cell_z = 0;
    vec3 origin;                        //world centre of the bomb
    //How far the flame may reach in each direction, in TILES: (east +X, west -X, north -Z,
    //south +Z). Walked out from the bomb at Init and stopped at the first wall.
    vec4 arm_limit = vec4(0,0,0,0);
    //The volumes. TILE mode fills `tiles` with BOMBER_MAX_TILES of them sharing one mesh, so they
    //are a single draw call; CROSS mode has the one `cross`.
    std::vector<Object*> tiles;
    Object* cross = NULL;
    //The bomb itself, as something to see when nothing is burning.
    Object* bomb = NULL;
    //The light this site throws on its own walls. One per site, so the two do not light each
    //other and a difference on screen is a difference in the volume.
    PointLight* light = NULL;
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
    void BuildGround();
    void BuildWalls();
    void BuildSites();
    //Adds one wall cube at grid coordinates (cx,cz). `kind` is BOMBER_WALL_HARD or _SOFT.
    Object* AddWall(int cx, int cz, int kind);
    //World centre of grid cell (cx,cz). The grid is centred on the origin so the orbit camera has
    //something symmetric to turn around.
    vec3 CellCentre(int cx, int cz) const;
    //Walks out from (cx,cz) in each direction and stops at the first wall, giving the four arm
    //lengths in tiles. Both wall kinds stop the flame for now; once soft blocks can be destroyed
    //the flame will occupy the block's own tile as it burns, which is one line here.
    vec4 ComputeArmLimits(int cx, int cz) const;

    //--- the volumes --------------------------------------------------------------------------
    //Runs shaders/noise3d.comp once to fill blast_noise with tileable 3D worley. Render thread -
    //it is a compute dispatch. Shared with the ship app; see shared_assets/shaders/noise3d.comp.
    void BuildBlastNoise();
    /*
        Builds BOTH shaders and both sites' volumes.

        The two Shader objects are compiled FROM THE SAME .frag, differing only in the blast_mode
        uniform their callback sets. Two programs are needed rather than one because
        Shader::uniform_callback is per-Shader and is called once per pass with that program bound,
        while both modes are on screen at the same time - one program could only be told one mode
        per frame. Two meshes for the same reason: mesh_mode/custom_shader_index are properties of
        the MESH, and that index is how a volume says which program draws it.
    */
    void BuildExplosion();
    //A fresh unshared unit cube tagged MESH_MODE_SHADER, bound to `shader_index`. Generated rather
    //than taken from an asset for the same two reasons ApplicationShip::BuildVolumeCube gives:
    //asset meshes are shared by pointer, so tagging one would turn every other user of it into a
    //volume, and the box the shader marches has to agree with the mesh's real extents.
    Mesh* BuildBlastCube(int shader_index);
    //The two Shader::uniform_callbacks, one per mode. Both are PushBlastUniforms with the mode's
    //own constants; they exist as separate functions only because std::bind needs a target.
    void SetTileUniforms();
    void SetCrossUniforms();
    /*
        Pushes everything both modes share, converting each world-unit knob into the object units
        the shader works in by dividing by `box_world`. RENDER THREAD, program already bound.

        The conversion lives here, at the boundary, rather than in the shader: the knobs are then
        in metres and tiles - which is what they are worth talking about in and what the MCP tool
        reports - while the march stays in the space ray_box_dst needs.
    */
    void PushBlastUniforms(Shader* shader, const BomberSite& site, int mode, float box_world);
    //Recompiles shaders/bomber_explosion.frag into BOTH programs. RENDER THREAD ONLY - serviced
    //from PreRender, because the key that asks for it is read on the physics thread.
    void ReloadExplosionShader();

    //--- the blast ----------------------------------------------------------------------------
    //Restarts the blast clock for BOTH sites. PHYSICS THREAD ONLY, inside a tick: either from
    //RunSimulationTick (the key) or from the BOMBER_CMD_DETONATE handler (MCP, the UI button).
    void Detonate();
    //Advances the lights and publishes the view state. Called once per tick from
    //RunSimulationTick, so the effect is paused and stepped along with everything else.
    void UpdateBlast();

    void BuildKnobTable();
    BomberKnob* FindKnob(const std::string& name);

    //--- MCP ----------------------------------------------------------------------------------
#ifdef USE_MCP
    void RegisterMCPTools();
#endif
    json BlastStateJson();

    //--- UI -----------------------------------------------------------------------------------
#ifdef USE_IMGUI
    void RenderExplosionPanel();
#endif

    //--- scene --------------------------------------------------------------------------------
    vec3 camera_target = vec3(0,1.2f,0);
    Object* ground = NULL;
    std::vector<Object*> walls;
    //Which cells a wall stands in, for ComputeArmLimits. A set rather than a grid array because
    //there is no maze yet to size an array against, and the lookup is two dozen cells.
    std::set<BomberCell> occupied;
    DirectionalLight* sun = NULL;

    //The two sites. Index 0 is the per-tile one, index 1 the single-volume one; the panel and the
    //MCP state report them by name rather than by index.
    BomberSite site_tiles;
    BomberSite site_cross;

    //--- the shaders ----------------------------------------------------------------------------
    Shader* tile_shader = NULL;
    int     tile_shader_index = -1;
    Mesh*   tile_mesh = NULL;
    Shader* cross_shader = NULL;
    int     cross_shader_index = -1;
    Mesh*   cross_mesh = NULL;

    Texture* blast_noise = NULL;
    Shader*  blast_noise_shader = NULL;

    //--- the blast clock, simulation side -----------------------------------------------------
    //Physics thread only. blast_start_tick is meaningless while f_blast_live is false.
    uint64_t blast_start_tick = 0;
    bool     f_blast_live = false;
    //How many have gone off. Feeds the seed below, so it is part of the simulation rather than a
    //statistic - two detonations must differ, and must differ the SAME WAY on a replay.
    uint32_t blast_count = 0;

    /*
        --- the blast clock, view side ---------------------------------------------------------
        Written by the physics thread at the end of UpdateBlast, read by the render thread in
        PushBlastUniforms. Two plain floats crossing a thread boundary with no lock: a torn read is
        one frame of a fireball being one tick stale in one component, which is not worth a mutex -
        but it is worth saying out loud rather than leaving someone to wonder, which is the same
        call apps/testfx makes about its mouse.

        A NEGATIVE age means no blast is live. The shader takes that as its early-out, so nothing
        has to hide the objects - a visibility flag flipped from the physics thread would be a
        write the render thread reads mid-frame, for no gain over a uniform it reads anyway.
    */
    float blast_age_view = -1.0f;
    float blast_seed_view = 0.0f;

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

    float blast_life = BOMBER_BLAST_LIFE_TICKS;
    /*
        World radius of the flame tube.

        1.5 makes a tile's fireball 3.0 across against the 2.0 cell it belongs to, so neighbouring
        tiles overlap by half a cell and the per-tile cross reads as one connected blast. 1.2 - a
        diameter only slightly over the cell pitch - was not enough, because a tile's flame is
        still GROWING when its neighbour lights: the outer rings are younger and smaller, so the
        gaps opened up exactly where the cross was supposed to join. Beads, not a blast.
    */
    float blast_radius = 1.5f;
    float rim_softness = 0.22f;
    float turbulence = 0.85f;
    float noise_scale = 1.20f;
    float outflow = 0.35f;
    float rise = 0.80f;             //world units over a whole life
    float blast_density = 3.0f;
    //Ticks each ring of tiles waits behind the one nearer the bomb. TILE mode only - the single
    //cross volume grows its arms continuously instead, and the difference between those two is
    //one of the things worth looking at here.
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
    //0 off, 1 the marched interval, 2 the G-buffer the shader reads. Matches f_show_box in
    //shaders/bomber_explosion.frag.
    int   debug_view = 0;

    //Peak brightness and reach of each site's light - see UpdateBlast. default.frag lights
    //surfaces with brightness/distance, so 8 puts about 2.8 on a wall one cell away: bright enough
    //to go orange, short of blowing it white, which 45 did to the entire scene including the
    //ground out to the edge of the plane.
    float blast_light_brightness = 8.0f;
    float blast_light_radius = 1.2f;

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
