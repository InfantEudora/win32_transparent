#ifndef _APPLICATION_FARSIDE_H_
#define _APPLICATION_FARSIDE_H_

#include "Application.h"
#include "Field.h"
#include "Flight.h"
#include <mutex>
#include <vector>
#include "tinygltf/json.hpp"
using json = nlohmann::json;

/*
    THE SCALE BENCH - the first build of the Farside concept (docs/README.md, last section).

    A greybox with no game in it: one planet sunk through the flight plane, a moon on an orbit, an
    asteroid cluster, a capital-ship hull, a station and a wreck, two defence posts on the planet,
    and the player's ship with a continuous altitude. Every size, depth, speed and camera number
    is a slider (the Bench panel) and an MCP setting (farside_set), because the question it exists
    to answer is which of those numbers feel right.

    Split three ways: Field.h is the geometry (ground, sight, push-out), Flight.h the ship, and this
    class builds the scene from them each tick and draws the debug picture - the posts' lines of
    sight, the region a post can see at the ship's height, the waterline circles.
*/

//Everything the bench tunes. Read each tick, so a change applies live.
struct BenchSettings{
    //The planet. Its centre sits planet_sink * radius below the plane (README, "The waterline").
    float planet_radius = 20.0f;
    float planet_sink = 0.6f;
    float planet_turn_minutes = 10.0f;      //one turn about its axis
    float planet_tilt_deg = 0.0f;           //axis away from straight at the camera
    //The moon: a ball standing in the plane, on a circular orbit round the planet.
    float moon_radius = 7.0f;
    float moon_sink = 0.0f;
    float moon_orbit = 58.0f;
    float moon_orbit_minutes = 6.0f;
    //The two posts on the planet: one near the cap's edge, which turns round with the planet, and
    //one on the top. Angles from the pole; the waterline is at acos(planet_sink).
    float post_height = 0.3f;
    float post_edge_angle_deg = 46.0f;
    float post_edge_azimuth_deg = 0.0f;
    //The ship's model length. One is the unit everything else is measured in.
    float ship_size = 1.0f;
    //The camera: vertical field of view, and how many ship lengths the screen is tall on the
    //ground and at the top of the altitude range. The zoom follows the altitude asked for.
    float fov_deg = 30.0f;
    float view_ground = 24.0f;
    float view_ceiling = 72.0f;
    //The ceiling: this far above the tallest dome.
    float ceiling_margin = 2.0f;
    //The star, in the plane's terms: azimuth from screen-up (-z) clockwise, elevation above the
    //plane.
    //High enough to light a cap that faces the camera; at 10 degrees it was barely lit at all.
    float sun_azimuth_deg = 40.0f;
    float sun_elevation_deg = 30.0f;
    float sun_brightness = 6.0f;
    float ambient = 0.05f;
    //The debug picture.
    bool f_sight_lines = true;              //each post's line to the ship, green seen, red hidden
    int overlay_post = 0;                   //whose visible region is outlined: 0 edge, 1 top, -1 none
    bool f_waterlines = true;
    bool f_drop_line = true;                //ship to the ground below it
    FlightTuning flight;
};

//A copy of what the HUD shows, taken on the physics thread and drawn on the render thread.
struct BenchHud{
    float height = 0.0f;
    float height_set = 0.0f;
    float ground = 0.0f;
    float ceiling = 0.0f;
    float speed = 0.0f;
    bool f_landed = false;
    bool f_seen[2] = {false,false};
    float ship_px = 0.0f;                   //the ship's length on screen
    float planet_screen = 0.0f;             //the planet's diameter as a fraction of screen height
    float view_lengths = 0.0f;              //ship lengths the screen is tall at the ship
};

//One tunable number, for the panel and the MCP tools alike - a single table so the two can never
//disagree about a name or a range.
struct BenchKnob{
    const char* section;
    const char* name;
    float* value;
    float min;
    float max;
};

class ApplicationFarside : public Application{
public:
    ApplicationFarside();

    void Init(void) override;
    void RunSimulationTick(void) override;
    void UpdateView(void) override;
    void PreRender(void) override;
    void DrawOverlay(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif
    vec3* GetCameraTargetPtr() override{ return &camera_target; }

    BenchSettings settings;
    Field field;
    ShipState ship;

private:
    //--- building (Init, render thread) ---
    Scene* CreateBenchScene();
    void BuildMaterials();
    void BuildShaders();
    void BuildBodies();
    void BuildProps();
    void BuildPosts();
    void BuildShip();
    void BuildStars();
    Object* AddLineObject(const char* name);
    int AddColourMaterial(const char* name, const vec4& colour, float roughness, bool f_unlit = false);

    //--- the tick ---
    //Places every body for this tick and rebuilds `field` from them.
    void UpdateBodies();
    //Where post i stands and what it sees from: the foot on the surface, the eye post_height up.
    void PostPose(int i, vec3& foot, vec3& eye, vec3& up) const;
    FlightInput ReadFlightInput();
    void PlaceObjects();
    float Ceiling() const;

    //--- the view ---
    void UpdateCamera();
    void UpdateSun();
    void BuildDebugLines();
    void TakeHudSnapshot();
    float ScreenLengthsAtShip() const;

    //Uniforms for the two custom shaders, from the renderer on the render thread.
    void SetPlanetUniforms();
    void SetHaloUniforms();

    std::vector<BenchKnob> Knobs();
    json SettingsJson();
    json StatusJson();
    //Puts the ship in one of the comparison spots - see farside_set's `preset`.
    bool ApplyPreset(const std::string& name);
#ifdef USE_MCP
    void RegisterMCPTools();
#endif

    uint64_t bench_tick = 0;
    float planet_angle = 0.0f;
    float moon_angle = 0.0f;
    quat planet_rotation = quat(0,0,0,1);

    //Indices into field.spheres, fixed by the order UpdateBodies adds them in.
    static const int SPHERE_PLANET = 0;
    static const int SPHERE_MOON = 1;

    Object* planet = NULL;
    Object* halo = NULL;
    Object* moon = NULL;
    Object* ship_root = NULL;
    Object* ship_model = NULL;
    Object* posts[2] = {NULL,NULL};
    Object* debug_lines = NULL;
    Object* stars = NULL;
    DirectionalLight* sun = NULL;

    //Props that never move: their objects, and the shapes they stand for in the field.
    struct Asteroid{
        vec3 centre;
        float radius;
    };
    std::vector<Asteroid> asteroids;
    std::vector<FieldBox> static_boxes;

    Shader* planet_shader = NULL;
    int planet_shader_index = -1;
    Shader* halo_shader = NULL;
    int halo_shader_index = -1;

    int material_planet = -1;
    int material_post = -1;
    int material_station = -1;
    int material_wreck = -1;

    //The view, kept on the physics thread.
    vec3 camera_target = {};
    vec3 camera_position = {0,60,0};
    float camera_lengths = 40.0f;           //smoothed screen height in ship lengths
    quat overhead_rotation = quat(0,0,0,1);
    bool f_camera_placed = false;
    float wheel_notches = 0.0f;             //gathered by UpdateView, spent by the tick
    int view_pass = 0;
    bool f_seen[2] = {false,false};         //this tick: does post i have a line to the ship

    //Handed from the physics thread to the render thread.
    std::mutex lines_mutex;
    std::vector<line_vertex> pending_lines;
    bool f_lines_pending = false;
    std::mutex hud_mutex;
    BenchHud hud;
};

#endif
