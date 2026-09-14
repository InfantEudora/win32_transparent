#ifndef _APPLICATION_PINBALL_H_
#define _APPLICATION_PINBALL_H_

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Application.h"
#include "TextMesh.h"

#include "Table.h"
#include "TableBuilder.h"
#include "Mechanisms.h"

/*
    ORBIT OUTPOST - a solid-state pinball machine on this engine.

    STAGE 0 of the plan in apps/pinball/pinball_design.md 4: the static scene. The table is on
    screen, lit, framed and labelled, and nothing on it moves. No ball, no colliders, no rules.

    The point of a stage that does not play is that every coordinate in pinball_design.md 1.5 is a
    GUESS until it is on screen, and the cheapest moment to find out that the bumper nest sits on
    top of the right ramp is before either of them has a collider, a joint or a rule attached.

    --- WHAT IS BUILT FROM WHAT ------------------------------------------------------------------
    The plan has stage 0 loading meshes/table.glb and meshes/parts.glb. Two halves, two answers:

      - The STATIC MACHINE - deck, cabinet, every wall and rail, the ramps - comes from
        core/Primitives and apps/pinball/TableBuilder, driven by Table.h. Not table.glb, and on
        purpose: while the layout is still moving, the geometry IS the layout, and a modelled
        table would have to be re-exported every time a coordinate changed. TableBuilder sweeps
        the same paths the colliders will follow (a wall, a ramp floor, a habitrail wire), so
        there is one source. table.glb is for when the layout has stopped moving.
      - The REPEATED PARTS - bats, bumpers, posts and rubbers, targets, saucer rims, the plunger,
        the ball - come from meshes/parts.glb, modelled in Blender by
        tools/pinball_parts_blender.py exactly as pinball_design.md 3.2 describes (origins at the
        pivots, +X forward, named nodes), and loaded by LoadParts. Every one has a primitive
        fallback, so the app runs without the file and says so in the log.

    The geometry is not the deliverable; the COORDINATES are, and they live in Table.h where the
    colliders will read them too. tools/pinball_plan.py is what checks them - see the note at
    PinPlanEntry below and docs/pinball_findings.md for what it found in the first draft.

    --- THE THIRD FLIPPER ------------------------------------------------------------------------
    Open question 3 was answered "2 + 1", and the design document's coordinate table predates that
    answer - so the upper-left flipper has no published position. Its numbers are invented and are
    argued for at PIN_FLIPPER_U_X in Table.h. It is the single most likely thing on this table to
    move.

    --- THREADING --------------------------------------------------------------------------------
    Nothing simulates yet, so this is short - but it is the split the later stages have to keep:
      - Init()          render thread, once. All GL, all mesh building, all registration.
      - UpdateView()    physics thread, every pass INCLUDING paused ones. The camera move lives
                        here, which is the whole reason it is a view concern: it must keep running
                        while the simulation is paused, or an agent could never pause the table and
                        then look at a corner of it.
      - PreRender()     render thread. Where text meshes are (re)built, because BuildTextMesh ends
                        in glNamedBufferData and the physics thread may not touch GL.
      - DrawImGuiUI()   render thread, physics_mutex held. Never waits on the physics thread.
      - MCP handlers    their own thread, NO lock. They read atomics and post requests; they do not
                        touch the scene.
*/

//Our own input actions, numbered from INPUT_LAST the way every other app does it.
#define INPUT_PINBALL_TOGGLE_UI     INPUT_LAST+1
#define INPUT_PINBALL_TOGGLE_LABELS INPUT_LAST+2
#define INPUT_PINBALL_SHOT_TABLE    INPUT_LAST+3
#define INPUT_PINBALL_SHOT_UPPER    INPUT_LAST+4
#define INPUT_PINBALL_SHOT_LOWER    INPUT_LAST+5
#define INPUT_PINBALL_SHOT_MACHINE  INPUT_LAST+6
#define INPUT_PINBALL_SHOT_ORBIT    INPUT_LAST+7
//Stage 1: the controls a player actually has. Held keys, read with IsKeyDown every tick, so a
//scripted HoldKey from an MCP tool is indistinguishable from a finger (pinball_design.md 0).
//The upper flipper is on the LEFT button, as it is on every machine that has one.
#define INPUT_PINBALL_FLIP_LEFT     INPUT_LAST+8
#define INPUT_PINBALL_FLIP_RIGHT    INPUT_LAST+9
#define INPUT_PINBALL_PLUNGE        INPUT_LAST+10
#define INPUT_PINBALL_SERVE         INPUT_LAST+11

//Our own SimCommand types, numbered from SIM_CMD_LAST like every app's (core/SimCommand.h).
//PLACE_BALL: teleport the ball to `position` (SIM_CMD_FLAG_POSITION) with `velocity`
//(SIM_CMD_FLAG_VELOCITY); either may be absent. SERVE: the ball back to the plunger, still.
#define PIN_CMD_PLACE_BALL          (SIM_CMD_LAST + 0)
#define PIN_CMD_SERVE               (SIM_CMD_LAST + 1)

/*
    THE CAMERA, which the answer to open question 2 settles: fixed, framing the whole machine the
    way the reference artwork does - and able to move somewhere and back for an effect or when the
    ball enters an area.

    A "shot" is a named framing: where the camera sits, what it looks at, and how wide. The camera
    is always moving toward the current shot, which means the fixed view and the effect move are
    the same mechanism with different targets rather than two code paths that have to agree. Stage
    0 only ever changes shots on demand (a key, an MCP call), which is also how the later stages
    will trigger them - a mission start or a ramp entry just picks a different index.

    The move is a critically-damped-ish exponential rather than a spline: it has no overshoot, no
    duration to tune, and interrupting it mid-flight to go somewhere else is free, which a spline
    with a start and end keyframe is not.
*/
#define PIN_SHOT_TABLE      0       //the artwork framing - the playfield, filling the frame
#define PIN_SHOT_UPPER      1       //the orbit, the wormholes and the FUEL lanes
#define PIN_SHOT_LOWER      2       //down at flipper height, where the game is actually played
#define PIN_SHOT_MACHINE    3       //back off far enough to see the cabinet and the backbox too
/*
    PIN_SHOT_ORBIT is not a framing, it is a MODE, and it is the odd one out in this list
    deliberately.

    The other four are fixed: the camera eases to a place somebody chose and stops. This one hands
    the camera over - middle-drag to swing round the table, shift+middle-drag to slide the pivot,
    wheel to pull in and out - so that a height relationship can be looked at from the side, which
    is the one thing a near-overhead fixed shot can never show. A ramp that clears a bumper cap by
    0.13 and a ramp that is resting on it look identical from above.

    It shares the shot list rather than sitting beside it as a separate flag because everything
    that switches shots - a key, the debug panel, an MCP call - then switches to this too, with no
    second mechanism to keep in step. What differs is only what UpdateCameraShot does when it is
    the current one.
*/
#define PIN_SHOT_ORBIT      4
#define PIN_SHOT_COUNT      5

struct PinShot{
    const char* name;
    vec3  position;
    vec3  target;
    float fov;
};

class ApplicationPinball : public Application{
public:
    ApplicationPinball();
    ~ApplicationPinball();

    void Init(void) override;
    void UpdateView(void) override;
    void RunSimulationTick(void) override;
    void PreRender(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif
    vec3* GetCameraTargetPtr() override { return &camera_target; }

    /*
        --- STAGE 1: THE SIMULATION ------------------------------------------------------------
        Everything below runs on the physics thread inside RunSimulationTick, or is read at a
        tick boundary. The ball is a plain Object with a sphere body; the flippers and the plunger
        are the mechanisms in Mechanisms.h. Every static shape on the table got its collider in
        the same builder that made its mesh, from the same path - see AddSweptColliders.
    */
    //A chain of static box colliders along a path, one per segment, each overlapping its
    //neighbours by half a width so a ball cannot find the join. The same numbers MakeSweptBox
    //draws with: half_width sideways, y_below and y_above from the path's own height. Segments
    //that climb are tilted to match, which is what makes it the ramp floor's collider too. The
    //body is the object's own (added if it has none), static, in the TABLE category.
    void AddSweptColliders(Object* object, const PinPath& path, float half_width,
                           float y_below, float y_above, float bounciness, float friction);
    //One static box collider the size of the box the object was drawn as.
    void AddBoxCollider(Object* object, const vec3& size, float bounciness, float friction);
    //A static capsule standing on the deck with its round caps buried below it and above the
    //ball's band, so the ball only ever meets the straight middle - posts and pop bumpers.
    void AddPostCollider(Object* object, float radius, float bounciness, float friction);

    //The one dynamic body that matters. Built in BuildLauncher, parked against the plunger.
    void BuildBall();
    //Back to the plunger, still. Physics thread only; what the drain, an escape and the SERVE
    //command all end in.
    void ServeBall();
    //Mitigation 2 of pinball_design.md 2.2: sweep the ball's path for this tick and stop it
    //short of anything it would pass into. Physics thread, before the world steps.
    void TunnelGuard();
    void RegisterCommandHandlers();
    json BuildTelemetryJson();

    Flipper* flipper_left = NULL;
    Flipper* flipper_right = NULL;
    Flipper* flipper_upper = NULL;
    Plunger* plunger = NULL;
    Object*  plunger_rod = NULL;        //render only; follow the tip each tick
    Object*  plunger_knob = NULL;
    Object*  glass = NULL;              //the invisible ceiling; a body and nothing else

    //Counters, all written on the physics thread and read from anywhere. They are the
    //verification: escapes and guard hits are the tunnelling detectors, and a table that works
    //has zero of the first and a max_speed that never reaches the clamp.
    std::atomic<uint32_t> drains{0};
    std::atomic<uint32_t> escapes{0};
    std::atomic<uint32_t> guard_hits{0};
    std::atomic<uint32_t> speed_clamps{0};
    float max_speed_seen = 0.0f;
    //What the last guard intervention was, for the telemetry: where, how fast, off what normal.
    vec3 last_guard_point = {};
    vec3 last_guard_normal = {};
    float last_guard_speed = 0.0f;

private:
    //--- Setup, all on the render thread from Init() -------------------------------------------
    /*
        meshes/parts.glb, if it exists: the repeated parts, modelled in Blender by
        tools/pinball_parts_blender.py the way pinball_design.md 3.2 asks. Loaded before anything
        is built, so every builder below can ask for a part and get either the modelled mesh or
        the primitive it stands in for. Missing file, missing part - the primitive is used and
        the log says so. Nothing about the layout depends on which one turns up.
    */
    void LoadParts();
    bool f_parts_loaded = false;
    bool HasPart(const char* part);
    //An Object for a named part, or for `fallback` if the part is not there; positioned, named,
    //coloured with the app's own material and added to the scene. Colour comes from the app
    //rather than the .glb because the app knows what a thing IS (the well's rim is orange, a
    //wormhole's teal) and the file only knows what it looks like in Blender.
    Object* NewPartObject(const char* part, const char* name, Mesh* fallback,
                          const vec3& position, int material);

    void BuildMaterials();
    void BuildEnvironment();
    void BuildDeckAndCabinet();
    void BuildLowerPlayfield();
    void BuildLauncher();
    void BuildUpperPlayfield();
    void BuildScoringCluster();
    void BuildRamps();
    void BuildBackbox();
    void BuildFeatureLabels();
    void BuildLights();
    void SetupCamera();
    void SetupInput();
#ifdef USE_MCP
    void RegisterMCPTools();
#endif

    //--- Small builders ------------------------------------------------------------------------
    /*
        Every one of these returns the Object so a caller can keep hold of the few it needs, and
        adds it to the scene either way. They take a CENTRE and a full SIZE, matching
        core/Primitives' convention, so a box's collider in stage 1 is the same numbers with no
        offset to remember.

        `yaw_degrees` is a rotation about +Y, which is the only rotation anything flat on this
        table ever needs - and the reason it is the only one is that the machine is not tilted, the
        gravity is. See the frame note at the top of Table.h.
    */
    Object* AddBox(const char* name, const vec3& centre, const vec3& size,
                   int material, float yaw_degrees = 0.0f);
    Object* AddCylinderObject(const char* name, const vec3& centre, float radius, float height,
                              int material, int segments = 20);
    Object* AddMeshObject(const char* name, Mesh* mesh, const vec3& position, int material);
    //A wall standing on a path, in one call - the shape most of this table is made of. Mesh AND
    //collider chain, from the one path; `bounciness` is the collider's, and is the rail default
    //unless the wall is rubber.
    Object* AddWall(const char* name, const PinPath& path, float height, float thickness,
                    int material, float bounciness = PIN_RAIL_BOUNCINESS);
    //A lamp insert: the flush disc in the deck that lights up under a rollover or a target. Drawn
    //a hair proud of the deck so it does not z-fight with it.
    Object* AddInsert(const char* name, float x, float z, float radius, int material,
                      float y = 0.0f);
    //A post with its rubber ring. Two objects, one call, because they are never apart.
    void    AddPost(const char* name, float x, float z);
    //A flipper on its hinge, parked at rest. The pivot is the OBJECT ORIGIN, which is what the
    //hinge joint needs and what a Blender flipper_bat has to match (pinball_design.md 3.2). The
    //plan entry is recorded here; the mechanism itself is Mechanisms.h's Flipper.
    Flipper* AddFlipper(const char* name, float x, float z, float length, float rest_degrees,
                        float up_degrees, bool f_mirrored);

    //--- The camera ----------------------------------------------------------------------------
    void SetShot(int shot);
    //Physics thread. Middle-drag to orbit, shift+middle-drag to pan the pivot, wheel to zoom.
    //Only runs while current_shot is PIN_SHOT_ORBIT.
    void UpdateOrbitControls();
    //Place the camera from the orbit's spherical parameters. Also how an MCP caller drives it,
    //since an agent has no mouse to drag.
    void ApplyOrbit();
    //Read (yaw, pitch, distance) back OUT of wherever the camera currently is, so that entering
    //orbit mode from a fixed shot continues from that framing instead of jumping.
    void SeedOrbitFromCamera();
    //Physics thread, from UpdateView. Eases the live camera toward the current shot.
    void UpdateCameraShot();

    //--- MCP, any thread -----------------------------------------------------------------------
    json BuildLayoutJson();

    /*
        THE PLAN, AS DATA - every shape on the deck a ball could meet, recorded as it is built.

        Table.h holds the coordinates, but the coordinates alone do not say whether a ball FITS: a
        lane is the gap between two walls that are defined in two different places, a ramp's mouth
        is where two rails and a floor happen to meet the deck, and "can the ball get from the
        plunger to the left wormhole" is a question about all of them at once. The first draft of
        this layout had an inlane a ball could not enter and an orbit return that fed the outlane,
        and neither was visible in the numbers.

        So each builder below appends what it built here - the path, the thickness, the radius -
        and pinball_layout emits the lot under "plan". tools/pinball_plan.py reads that, never a
        copy of the numbers: it draws the deck at true scale, fattens every wall by the ball's
        radius, floods from the plunger and reports what the ball cannot reach. Because it is fed
        by the app, it cannot drift from the app, which is the fault the old copy-the-numbers
        clearance script had by construction.

        `a`..`d` mean different things per kind; each Record* call documents its own. Deliberately
        a flat struct and not a hierarchy - it exists to be dumped as JSON and nothing else.
    */
    struct PinPlanEntry{
        std::string kind;       //"wall", "ramp", "post", "disc", "box", "flipper"
        std::string name;
        std::vector<vec3> points;
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    };
    std::vector<PinPlanEntry> plan;
    PinPlanEntry& RecordPlan(const char* kind, const char* name){
        plan.push_back(PinPlanEntry());
        plan.back().kind = kind;
        plan.back().name = name;
        return plan.back();
    }

    //--- The view ------------------------------------------------------------------------------
    /*
        Meshes generated once in Init and shared by pointer wherever the same shape repeats, the
        way ApplicationBreakout shares its unit cube: one instanced draw call for every post on the
        table rather than one per post. Each takes a reference of its own so it outlives any
        individual Object.
    */
    Mesh* unit_box_mesh = NULL;
    Mesh* post_mesh = NULL;
    Mesh* rubber_mesh = NULL;
    Mesh* insert_mesh = NULL;
    Mesh* bumper_body_mesh = NULL;
    Mesh* bumper_cap_mesh = NULL;
    Mesh* ball_mesh = NULL;
    Mesh* saucer_mesh = NULL;
    Mesh* skybox_mesh = NULL;

    Object* ball_object = NULL;         //parked in the plunger lane; scenery until stage 1

    //--- Materials, resolved once by name in Init ----------------------------------------------
    int material_deck = 0;
    int material_deck_line = 0;
    int material_cabinet = 0;
    int material_chrome = 0;
    int material_rail = 0;
    int material_ramp = 0;
    int material_orange = 0;
    int material_teal = 0;
    int material_cream = 0;
    int material_rubber = 0;
    int material_ball = 0;
    int material_lamp_lit = 0;
    int material_lamp_dark = 0;
    int material_backglass = 0;
    int material_label = 0;
    int material_backdrop = 0;

    //--- World-space labels, render thread only ------------------------------------------------
    /*
        One flat label per feature, lying on the deck next to the thing it names.

        Text is geometry here, not ImGui (core/TextMesh.h), which is the only reason this is worth
        doing: an ImGui overlay vanishes from an `include_ui:false` screenshot, and the whole
        argument for stage 0 is that somebody - including an agent with no eyes on the monitor -
        can look at the table and check the layout. A labelled screenshot IS the deliverable.
    */
    GlyphSet glyphs;
    std::vector<Object*> feature_labels;
    bool f_show_labels = true;

    //--- Camera --------------------------------------------------------------------------------
    //The named shots, SOLVED in SetupCamera from Table.h rather than typed - see MakeShot in the
    //.cpp. Filled once, on the render thread, before anything reads them.
    PinShot shots[PIN_SHOT_COUNT];
    vec3  camera_target = vec3(PIN_CENTRE_X,0.0f,0.40f);
    int   current_shot = PIN_SHOT_TABLE;
    //Where the camera is easing toward. Written by SetShot (physics thread, or an MCP thread
    //through the atomic below), read by UpdateCameraShot.
    vec3  shot_position = vec3();
    vec3  shot_target = vec3();
    float shot_fov = 38.0f;
    float live_fov = 38.0f;
    //An MCP handler holds no lock and must not touch the scene, so it raises a request here and
    //the next UpdateView picks it up. -1 means nothing pending.
    std::atomic<int> requested_shot{-1};

    /*
        The orbit camera's state, in spherical terms about camera_target.

            position = target + distance * (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch))

        so yaw 0 puts the camera on the PLAYER'S side of the machine looking up-table, yaw +90 puts
        it off the right-hand cabinet rail, and pitch +90 is straight overhead. Degrees, because
        every other angle in this app is.

        Spherical rather than the accumulate-a-quaternion-per-drag approach apps/tank uses. Two
        reasons, both about this being a measuring instrument: the state cannot drift or gimbal-flip
        however long it is dragged, and - the one that matters more - three numbers an MCP caller
        can just SET is a camera an agent can aim. "Look at it from the left, almost edge on" is
        yaw -90, pitch 8, and there is no way to express that by sending mouse deltas.

        The cost is that pitch is clamped short of the poles and the camera cannot roll over the
        top. On a table that is flat and always viewed from above, that is not a loss.
    */
    float orbit_yaw = 0.0f;
    float orbit_pitch = 55.0f;
    float orbit_distance = 18.0f;
    //How far past the poles the pitch may go. Not 90: at the pole the camera's forward is parallel
    //to world up and the look-at matrix has no way to choose an up vector, so the view rolls
    //arbitrarily.
    static constexpr float ORBIT_PITCH_LIMIT = 88.0f;

    /*
        An orbit placement posted by an MCP handler, for the next UpdateView to apply.

        A mutex rather than the atomics the other two requests use, because this one is FOUR
        numbers that have to land together: a half-applied placement - new yaw against an old
        distance - would move the camera somewhere nobody asked for and would be a real pain to
        recognise from a screenshot. It is contended once per tool call, so the lock costs nothing.

        Taken ONLY by the MCP thread and the physics thread, and never while renderer->physics_mutex
        is held, so it cannot participate in a deadlock with it.
    */
    struct PinOrbitRequest{
        bool  f_pending = false;
        bool  f_set_yaw = false,   f_set_pitch = false;
        bool  f_set_distance = false, f_set_target = false;
        float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
        vec3  target = vec3();
    };
    PinOrbitRequest orbit_request;
    std::mutex orbit_request_mutex;

    /*
        Two things the orbit turns on, and the reason it needs them.

        Seen from above - which is every fixed shot - the table lights itself fine. Seen from the
        SIDE, which is the entire point of having an orbit, it was unreadable: the key light rakes
        down the deck so a cabinet wall edge-on catches almost nothing, and this app renders with
        the skybox off against a transparent window, so the silhouette that was left had the
        DESKTOP showing through it. The first side-on screenshot was a black band.

        So entering the orbit lights a lamp at the camera and puts a plain card behind the machine.
        Leaving takes both away. Neither changes the simulation or the fixed shots - they are
        instrument lighting, and both are in the panel so they can be turned off when the question
        is what the table really looks like.

        THE CARD IS NOT THE SKYBOX, and that was tried first. Switching f_render_skybox back on in
        orbit mode does give an opaque background - of a brightly lit woolshop full of coloured
        wool, against which the machine reads as a dark blob. A backdrop for a measuring view has
        exactly one job, which is to not be interesting. Five unlit slabs in a flat dark grey do it
        and the photo does not.
    */
    bool f_orbit_backdrop = true;
    bool f_orbit_light = true;
    //Floor and four walls, boxed around the machine and hidden unless the orbit is live. Slabs
    //rather than an inside-out box, so every face the camera sees is an ordinary outward-facing
    //one and there is no winding to invert.
    std::vector<Object*> backdrop_objects;
    void BuildOrbitBackdrop();
    void SetBackdropVisible(bool f_visible);
    //Parked at zero brightness in BuildLights and only lit while orbiting. A point light's
    //intensity goes as brightness SQUARED over distance (see the note in BuildLights), so
    //ApplyOrbit rescales this every time it moves rather than leaving a fixed value that would
    //wash the table out at close range and do nothing from across the room.
    PointLight* orbit_light = NULL;

    //--- Tilt ----------------------------------------------------------------------------------
    /*
        The tilt angle is the single most important feel parameter on the machine - it sets how
        fast the ball comes down - so pinball_design.md 1.1 asks for a slider and a setter from day
        one, and it gets them before there is anything to feel.

        Nothing falls yet, so all this currently does is write the world's gravity vector. That is
        deliberately not nothing: it means the number is live and wrong values show up as a wrong
        gravity in the Engine panel rather than as a surprise in stage 1.
    */
    float tilt_degrees = PIN_TILT_DEGREES;
    void  ApplyTilt();
    //Same story as requested_shot: set by an MCP thread, consumed on the physics thread.
    std::atomic<bool> f_tilt_dirty{false};
    std::atomic<float> requested_tilt{PIN_TILT_DEGREES};

    bool f_show_engine_ui = false;      //F1. Off by default so the screenshot is the table.
};

#endif
