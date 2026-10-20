#include "ApplicationFarside.h"
#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit; guarded because a build with
//USE_IMGUI=0 has no library behind this header.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "Primitives.h"
#include "Debug.h"
#include <cmath>
#include <cstdio>

static Debugger* debug = new Debugger("ApplicationFarside",DEBUG_ALL);

#define INPUT_CLIMB         (INPUT_LAST + 1)
#define INPUT_DESCEND       (INPUT_LAST + 2)
#define GAMEPAD_LEFT_X      (INPUT_LAST + 3)
#define GAMEPAD_LEFT_Y      (INPUT_LAST + 4)
#define GAMEPAD_RIGHT_Y     (INPUT_LAST + 5)

//The atmosphere's shell over the planet's radius.
static const float HALO_SCALE = 1.06f;
//ships.glb's ship.002 is 1.96 long, nose along its +z; the bench's unit is one ship length.
static const float SHIP_MODEL_LENGTH = 1.96f;
//How far the height asked for moves per notch of the wheel, in ship lengths.
static const float WHEEL_STEP = 0.6f;

//Line colours, 0xRRGGBB as line.vert unpacks them.
static const uint32_t LINE_SEEN = 0x40FF60;
static const uint32_t LINE_HIDDEN = 0xFF3828;
static const uint32_t LINE_REGION = 0xFFA020;
static const uint32_t LINE_REGION_DOT = 0x2E7A3A;
static const uint32_t LINE_DROP = 0x40C8FF;
static const uint32_t LINE_WATERLINE = 0x2A4A6A;

static void AddSegment(std::vector<line_vertex>& v, const vec3& a, const vec3& b, uint32_t colour){
    line_vertex x;
    x.pos = a;
    x.color = colour;
    v.push_back(x);
    x.pos = b;
    v.push_back(x);
}

//An oriented box round an asset object's mesh, for the field. Falls back to `fallback_size` when
//the mesh carries no bounds.
static FieldBox BoxAround(Object* object, const vec3& position, const quat& rotation, float scale,
                          const vec3& fallback_size){
    FieldBox box;
    box.rotation = rotation;
    Mesh* mesh = object ? object->GetMesh() : NULL;
    if (mesh && mesh->HasBounds()){
        vec3 lo = mesh->GetBoundsMin();
        vec3 hi = mesh->GetBoundsMax();
        box.centre = position + rotation * ((lo + hi) * (0.5f * scale));
        box.half = (hi - lo) * (0.5f * scale);
    }else{
        box.centre = position;
        box.half = fallback_size * (0.5f * scale);
    }
    return box;
}

ApplicationFarside::ApplicationFarside():Application(){
    debug->Info("Created new ApplicationFarside.\n");
}

//=== building ===================================================================================

Scene* ApplicationFarside::CreateBenchScene(){
    Scene* scene = CreateNewScene("Farside Bench");

    //The star. Placed and aimed every pass by UpdateSun, which also fits its shadow box to the
    //view - the field is far larger than any one shadow map could cover at a useful resolution.
    sun = new DirectionalLight();
    sun->name = "Star";
    sun->color = vec3(1.0f,0.93f,0.84f);
    sun->brightness = settings.sun_brightness;
    sun->SetupOrthographic(4096,4096,60.0f,1.0f,1100.0f);
    sun->SetPosition(vec3(100,60,-100));
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    scene->inputcontroller->AddKeyMap('R',INPUT_CLIMB);
    scene->inputcontroller->AddKeyMap(VK_PRIOR,INPUT_CLIMB);
    scene->inputcontroller->AddKeyMap('F',INPUT_DESCEND);
    scene->inputcontroller->AddKeyMap(VK_NEXT,INPUT_DESCEND);

    //Straight down, captured once. A lookat along the world up axis is degenerate, so the screen's
    //up is given explicitly: -z is up the screen and +x is to the right, which is what every
    //"from screen-up, clockwise" angle in BenchSettings assumes. Left to the lookat to choose, the
    //picture came out turned half round.
    vec3 screen_up = vec3(0,0,-1);
    scene->camera->SetPosition(vec3(0,25,0));
    scene->camera->SetLookAt(vec3(0,0,0),&screen_up);
    overhead_rotation = scene->camera->GetRotation();
    scene->camera->SetupPerspective(renderer->width,renderer->height,settings.fov_deg,0.5f,1500.0f);
    return scene;
}

int ApplicationFarside::AddColourMaterial(const char* name, const vec4& colour, float roughness, bool f_unlit){
    Material m;
    m.name = name;
    m.glsl_material.color = colour;
    m.glsl_material.metallic = 0.05f;
    m.glsl_material.roughness = roughness;
    m.glsl_material.f_unlit = f_unlit ? 1 : 0;
    m.glsl_material.emissive = vec4(0.0f,0.0f,0.0f,0.0f);
    renderer->AddMaterial(m);
    return renderer->FindMaterialIndex(m.name);
}

void ApplicationFarside::BuildMaterials(){
    //The planet shader works out its own colour; the material only gives the lighting its
    //roughness and metallic.
    material_planet = AddColourMaterial("farside_planet",vec4(0.6f,0.5f,0.4f,1.0f),0.9f);
    //Unlit, so a post reads on the night side too - it is a marker, not a model.
    material_post = AddColourMaterial("farside_post",vec4(1.0f,0.45f,0.1f,1.0f),0.5f,true);
    material_station = AddColourMaterial("farside_station",vec4(0.60f,0.62f,0.66f,1.0f),0.45f);
    material_wreck = AddColourMaterial("farside_wreck",vec4(0.17f,0.15f,0.14f,1.0f),0.85f);
}

void ApplicationFarside::BuildShaders(){
    //A solid surface with a colour of its own - all four flags, as for chasm's ground.
    planet_shader = new Shader(shader_vert_name,"shaders/farside_planet.frag");
    planet_shader->f_lit = true;
    planet_shader->f_writes_gbuffer = true;
    planet_shader->f_casts_shadow = true;
    planet_shader->f_solid = true;
    planet_shader->uniform_callback = std::bind(&ApplicationFarside::SetPlanetUniforms,this);
    planet_shader_index = renderer->AddCustomShader(planet_shader);

    //Translucent: none of the flags, drawn last.
    halo_shader = new Shader(shader_vert_name,"shaders/farside_halo.frag");
    halo_shader->uniform_callback = std::bind(&ApplicationFarside::SetHaloUniforms,this);
    halo_shader_index = renderer->AddCustomShader(halo_shader);
}

void ApplicationFarside::BuildBodies(){
    //One sphere mesh for every body the planet shader draws; it finds its body per fragment. At
    //192 segments the silhouette of a 40-length planet sags under a hundredth of a length.
    Mesh* sphere = MakeSphere(1.0f,192,96);
    sphere->mesh_mode = MESH_MODE_SHADER;
    sphere->custom_shader_index = planet_shader_index;

    planet = new Object();
    planet->name = "Planet";
    planet->SetMesh(sphere);
    planet->SetMaterialSlot(0,material_planet);
    main_scene->AddObject(planet);

    moon = new Object();
    moon->name = "Moon";
    moon->SetMesh(sphere);
    moon->SetMaterialSlot(0,material_planet);
    main_scene->AddObject(moon);

    Mesh* shell = MakeSphere(1.0f,128,64);
    shell->mesh_mode = MESH_MODE_SHADER;
    shell->custom_shader_index = halo_shader_index;
    halo = new Object();
    halo->name = "Atmosphere";
    halo->SetMesh(shell);
    halo->SetMaterialSlot(0,-1);
    halo->SetCastsShadow(false);
    halo->SetPickability(false);
    main_scene->AddObject(halo);
}

void ApplicationFarside::BuildProps(){
    //Fixed, so every run and every screenshot has the same field. Its own small generator rather
    //than rrand: this is scenery, and the shared stream belongs to the simulation.
    uint32_t seed = 20261019u;
    auto next = [&seed](){
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / 16777216.0f;
    };

    //The asteroid cluster, out past the moon's orbit: three big rocks and a scatter of small ones.
    const char* rock_models[3] = {"asteroid.001","asteroid.002","asteroid.003"};
    vec3 cluster = vec3(-76.0f,0.0f,44.0f);
    for (int i = 0; i < 18; i++){
        float radius = (i < 3) ? 2.2f + 1.8f * next() : 0.3f + 1.1f * next() * next();
        vec3 centre = cluster;
        for (int attempt = 0; attempt < 24; attempt++){
            float a = next() * 2.0f * TYPE_PI;
            float d = 2.0f + next() * 13.0f;
            centre = cluster + vec3(cosf(a) * d,(next() - 0.5f) * 0.8f,sinf(a) * d);
            bool f_clear = true;
            for (const Asteroid& other:asteroids){
                if ((other.centre - centre).length() < other.radius + radius + 0.6f){
                    f_clear = false;
                    break;
                }
            }
            if (f_clear){
                break;
            }
        }
        Object* rock = assetmanager->GetObjectFromAsset(rock_models[i % 3]);
        if (!rock){
            continue;
        }
        rock->name = "Asteroid";
        //The models are about 0.8 across.
        rock->SetScale(vec3(radius * 2.0f / 0.8f));
        rock->SetPosition(centre);
        vec3 axis = vec3(next() - 0.5f,next() - 0.5f,next() - 0.5f);
        axis.normalize();
        rock->SetRotation(quat(axis,next() * 2.0f * TYPE_PI));
        main_scene->AddObject(rock);
        asteroids.push_back(Asteroid{centre,radius});
    }

    //A capital ship lying between the planet and the moon's orbit: cover for a ship flying low.
    {
        Object* hull = assetmanager->GetObjectFromAsset("ship.001");
        if (hull){
            hull->name = "Capital Hull";
            float scale = 6.0f / 1.64f;
            quat rotation = quat(vec3(0,1,0),toradians(35.0f));
            vec3 position = vec3(30.0f,0.0f,-32.0f);
            hull->SetScale(vec3(scale));
            hull->SetRotation(rotation);
            hull->SetPosition(position);
            main_scene->AddObject(hull);
            static_boxes.push_back(BoxAround(hull,position,rotation,scale,vec3(1.47f,0.57f,1.64f)));
        }
    }

    //A wreck: a hull on its side, dark, with debris round it.
    {
        Object* wreck = assetmanager->GetObjectFromAsset("ship.001.hull");
        vec3 position = vec3(27.0f,0.4f,33.0f);
        if (wreck){
            wreck->name = "Wreck";
            float scale = 3.0f;
            quat rotation = quat(vec3(0,1,0),toradians(118.0f)) * quat(vec3(0,0,1),toradians(24.0f)) *
                            quat(vec3(1,0,0),toradians(-9.0f));
            wreck->SetScale(vec3(scale));
            wreck->SetRotation(rotation);
            wreck->SetPosition(position);
            for (int slot = 0; slot < 4; slot++){
                wreck->SetMaterialSlot(slot,material_wreck);
            }
            main_scene->AddObject(wreck);
            static_boxes.push_back(BoxAround(wreck,position,rotation,scale,vec3(1.61f,0.63f,1.96f)));
        }
        for (int i = 0; i < 6; i++){
            vec3 size = vec3(0.3f + next() * 0.7f,0.15f + next() * 0.3f,0.3f + next() * 0.9f);
            float a = next() * 2.0f * TYPE_PI;
            float d = 3.5f + next() * 3.5f;
            vec3 at = position + vec3(cosf(a) * d,(next() - 0.5f) * 0.6f,sinf(a) * d);
            vec3 axis = vec3(next() - 0.5f,next() - 0.5f,next() - 0.5f);
            axis.normalize();
            quat rotation = quat(axis,next() * 2.0f * TYPE_PI);
            Object* piece = new Object();
            piece->name = "Debris";
            piece->SetMesh(MakeBox(size));
            piece->SetMaterialSlot(0,material_wreck);
            piece->SetPosition(at);
            piece->SetRotation(rotation);
            main_scene->AddObject(piece);
            FieldBox box;
            box.centre = at;
            box.half = size * 0.5f;
            box.rotation = rotation;
            static_boxes.push_back(box);
        }
    }

    //A station: a hub, a ring of twelve segments and four spokes, each part its own box in the
    //field, so the gaps in the ring are gaps in the cover too.
    {
        vec3 at = vec3(-34.0f,0.0f,-26.0f);
        auto part = [&](Mesh* mesh, const vec3& offset, const quat& rotation, const vec3& size){
            Object* o = new Object();
            o->name = "Station";
            o->SetMesh(mesh);
            o->SetMaterialSlot(0,material_station);
            o->SetPosition(at + offset);
            o->SetRotation(rotation);
            main_scene->AddObject(o);
            FieldBox box;
            box.centre = at + offset;
            box.half = size * 0.5f;
            box.rotation = rotation;
            static_boxes.push_back(box);
        };
        part(MakeCylinder(1.3f,1.6f,32),vec3(),quat(0,0,0,1),vec3(2.6f,1.6f,2.6f));
        vec3 segment_size = vec3(0.8f,0.7f,2.35f);
        Mesh* segment = MakeBox(segment_size);
        for (int i = 0; i < 12; i++){
            float a = (float)i / 12.0f * 2.0f * TYPE_PI;
            vec3 radial = vec3(cosf(a),0.0f,sinf(a));
            vec3 tangent = vec3(-sinf(a),0.0f,cosf(a));
            part(segment,radial * 4.4f,quat::getquat(tangent,vec3(),vec3(0,1,0)),segment_size);
        }
        vec3 spoke_size = vec3(0.35f,0.35f,3.0f);
        Mesh* spoke = MakeBox(spoke_size);
        for (int i = 0; i < 4; i++){
            float a = ((float)i + 0.5f) / 4.0f * 2.0f * TYPE_PI;
            vec3 radial = vec3(cosf(a),0.0f,sinf(a));
            part(spoke,radial * 2.75f,quat::getquat(radial,vec3(),vec3(0,1,0)),spoke_size);
        }
    }
}

void ApplicationFarside::BuildPosts(){
    Mesh* base = MakeCylinder(0.16f,0.18f,16);
    Mesh* head = MakeBox(vec3(0.2f,0.12f,0.2f));
    Mesh* barrel = MakeBox(vec3(0.05f,0.05f,0.28f));
    const char* names[2] = {"Post (edge)","Post (top)"};
    for (int i = 0; i < 2; i++){
        Object* root = new Object();
        root->name = names[i];
        Mesh* meshes[3] = {base,head,barrel};
        vec3 offsets[3] = {vec3(0,0.09f,0),vec3(0,0.24f,0),vec3(0,0.25f,-0.18f)};
        for (int k = 0; k < 3; k++){
            Object* piece = new Object();
            piece->name = names[i];
            piece->SetMesh(meshes[k]);
            piece->SetMaterialSlot(0,material_post);
            piece->SetPosition(offsets[k]);
            root->AttachChild(piece);
        }
        main_scene->AddObject(root);
        posts[i] = root;
    }
}

void ApplicationFarside::BuildShip(){
    ship_root = new Object();
    ship_root->name = "Ship";
    main_scene->AddObject(ship_root);

    ship_model = assetmanager->GetObjectFromAsset("ship.002");
    if (ship_model){
        ship_model->name = "Ship Model";
        ship_root->AttachChild(ship_model);
    }else{
        debug->Err("ships.glb has no ship.002 - is apps/ship/assets on the search path?\n");
    }

    //A little light at the tail, so the ship can be found on a planet's night side. The root's
    //forward is -z, so the tail is +z.
    PointLight* engine = new PointLight();
    engine->name = "Engine Glow";
    engine->color = vec3(0.3f,0.75f,1.0f);
    engine->brightness = 1.5f;
    engine->SetPosition(vec3(0.0f,0.15f,0.6f));
    ship_root->AttachChild(engine);
}

void ApplicationFarside::BuildStars(){
    //Far below the plane, so they drift slower than everything else as the camera moves: a cheap
    //sense of depth. Short dashes rather than points, which a line pass does not draw.
    uint32_t seed = 7u;
    auto next = [&seed](){
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / 16777216.0f;
    };
    std::vector<line_vertex> v;
    for (int i = 0; i < 5000; i++){
        vec3 p = vec3((next() - 0.5f) * 3000.0f,-350.0f - next() * 250.0f,(next() - 0.5f) * 3000.0f);
        float b = next();
        b = 40.0f + b * b * b * 215.0f;
        float warm = next();
        uint32_t r = (uint32_t)fminf(255.0f,b * (0.85f + 0.3f * warm));
        uint32_t g = (uint32_t)fminf(255.0f,b * 0.95f);
        uint32_t bl = (uint32_t)fminf(255.0f,b * (1.15f - 0.3f * warm));
        float len = 0.5f + 0.6f * next();
        AddSegment(v,p,p + vec3(len,0.0f,len * 0.3f),(r << 16) | (g << 8) | bl);
    }
    stars->GetMesh()->SetLineMeshData(v.data(),(int)v.size());
}

Object* ApplicationFarside::AddLineObject(const char* name){
    Object* o = new Object();
    o->name = name;
    Mesh* mesh = new Mesh();
    o->SetMesh(mesh);
    //A single degenerate segment until there is something to draw, so the mesh is never empty.
    std::vector<line_vertex> v;
    AddSegment(v,vec3(),vec3(),0);
    mesh->SetLineMeshData(v.data(),(int)v.size());
    o->SetPickability(false);
    o->SetCastsShadow(false);
    o->SetVisualOnly(true);
    main_scene->AddObject(o);
    return o;
}

void ApplicationFarside::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(shader_vert_name,shader_frag_name,PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;
    renderer->background_color = vec4(0.004f,0.006f,0.012f,1.0f);
    default_shader = new Shader(shader_vert_name,shader_lit_frag_name);

    //60 so a turn rate or a climb rate per second reads the same per tick as per frame.
    SetPhysicsTPS(60.0f);
    rrand = new RRandom(1);

    main_window->Resize(1600,900);
    main_scene = CreateBenchScene();

    assetmanager = new AssetManager();
    gltfloader.LoadGLTFFile("meshes/ships.glb");
    GetAllAssetsFromGLTF();

    BuildMaterials();
    BuildShaders();
    BuildBodies();
    BuildProps();
    BuildPosts();
    BuildShip();
    stars = AddLineObject("Stars");
    BuildStars();
    debug_lines = AddLineObject("Bench Lines");

    InputController* input = main_window->inputcontroller;
    input->AddGamePadMap(0,GAMEPAD_LEFT_X);
    input->AddGamePadMap(1,GAMEPAD_LEFT_Y);
    input->AddGamePadMap(3,GAMEPAD_RIGHT_Y);

    UpdateBodies();
    ApplyPreset("start");
    PlaceObjects();

#ifdef USE_MCP
    RegisterMCPTools();
#endif
    debug->Ok("Farside bench ready: planet radius %.0f, %i asteroids, %i boxes\n",
              settings.planet_radius,(int)asteroids.size(),(int)static_boxes.size());
}

//=== the tick ===================================================================================

float ApplicationFarside::Ceiling() const{
    return field.HighestGround() + settings.ceiling_margin;
}

void ApplicationFarside::UpdateBodies(){
    float seconds = (float)bench_tick * GetPhysicsTimestep();

    //The planet turns about its own axis, which points at the camera unless tilted.
    float turn = settings.planet_turn_minutes * 60.0f;
    planet_angle = (turn > 0.0f) ? 2.0f * TYPE_PI * fmodf(seconds / turn,1.0f) : 0.0f;
    quat tilt = quat(vec3(1,0,0),toradians(settings.planet_tilt_deg));
    planet_rotation = tilt * quat(vec3(0,1,0),planet_angle);

    float orbit = settings.moon_orbit_minutes * 60.0f;
    moon_angle = (orbit > 0.0f) ? 2.0f * TYPE_PI * fmodf(seconds / orbit,1.0f) : 0.0f;

    field.Clear();
    FieldSphere p;
    p.centre = vec3(0.0f,-settings.planet_sink * settings.planet_radius,0.0f);
    p.radius = settings.planet_radius;
    p.rotation = planet_rotation;
    p.f_ground = true;
    field.spheres.push_back(p);                 //SPHERE_PLANET

    //Counter-clockwise seen from above, keeping one face to the planet.
    FieldSphere m;
    m.centre = vec3(cosf(moon_angle) * settings.moon_orbit,-settings.moon_sink * settings.moon_radius,
                    -sinf(moon_angle) * settings.moon_orbit);
    m.radius = settings.moon_radius;
    m.rotation = quat(vec3(0,1,0),moon_angle);
    m.f_ground = true;
    field.spheres.push_back(m);                 //SPHERE_MOON

    for (const Asteroid& a:asteroids){
        FieldSphere s;
        s.centre = a.centre;
        s.radius = a.radius;
        field.spheres.push_back(s);
    }
    field.boxes = static_boxes;
}

void ApplicationFarside::PostPose(int i, vec3& foot, vec3& eye, vec3& up) const{
    float angle = (i == 0) ? toradians(settings.post_edge_angle_deg) : 0.0f;
    float azimuth = (i == 0) ? toradians(settings.post_edge_azimuth_deg) : 0.0f;
    vec3 local = vec3(sinf(angle) * cosf(azimuth),cosf(angle),sinf(angle) * sinf(azimuth));
    const FieldSphere& body = field.spheres[SPHERE_PLANET];
    up = body.rotation * local;
    foot = body.centre + up * body.radius;
    eye = foot + up * settings.post_height;
}

FlightInput ApplicationFarside::ReadFlightInput(){
    FlightInput in;
    InputController* input = main_scene->inputcontroller;
    float wheel = wheel_notches;
    wheel_notches = 0.0f;
    //A background window must not fly the ship on input meant for another application, while a
    //scripted hold (farside_hold) still gets through - IsInputLive is that predicate.
    if (!input->IsInputLive()){
        return in;
    }
    auto deadzone = [](float v){ return (fabsf(v) < 0.15f) ? 0.0f : v; };
    in.thrust = deadzone(input->GetNormalizedAnalogValue(GAMEPAD_LEFT_Y));
    in.turn = deadzone(input->GetNormalizedAnalogValue(GAMEPAD_LEFT_X));
    in.climb = deadzone(input->GetNormalizedAnalogValue(GAMEPAD_RIGHT_Y));
    if (input->IsKeyDown(INPUT_TURN_UP) || input->IsKeyDown(INPUT_MOVE_UP)){
        in.thrust = 1.0f;
    }
    if (input->IsKeyDown(INPUT_TURN_DOWN) || input->IsKeyDown(INPUT_MOVE_DOWN)){
        in.thrust = -1.0f;
    }
    if (input->IsKeyDown(INPUT_TURN_LEFT) || input->IsKeyDown(INPUT_MOVE_LEFT)){
        in.turn = -1.0f;
    }
    if (input->IsKeyDown(INPUT_TURN_RIGHT) || input->IsKeyDown(INPUT_MOVE_RIGHT)){
        in.turn = 1.0f;
    }
    if (input->IsKeyDown(INPUT_CLIMB)){
        in.climb = 1.0f;
    }
    if (input->IsKeyDown(INPUT_DESCEND)){
        in.climb = -1.0f;
    }
    in.climb_step = wheel * WHEEL_STEP;
    return in;
}

void ApplicationFarside::RunSimulationTick(){
    bench_tick++;
    UpdateBodies();
    StepFlight(ship,ReadFlightInput(),field,settings.flight,Ceiling(),GetPhysicsTimestep());
    //The posts' sight of the ship, by the same rule the overlay draws - one ray, whatever blocks it.
    for (int i = 0; i < 2; i++){
        vec3 foot,eye,up;
        PostPose(i,foot,eye,up);
        f_seen[i] = !field.Blocked(eye,ship.position);
    }
    PlaceObjects();
}

void ApplicationFarside::PlaceObjects(){
    const FieldSphere& p = field.spheres[SPHERE_PLANET];
    planet->SetPosition(p.centre);
    planet->SetRotation(p.rotation);
    planet->SetScale(vec3(p.radius));
    halo->SetPosition(p.centre);
    halo->SetScale(vec3(p.radius * HALO_SCALE));

    const FieldSphere& m = field.spheres[SPHERE_MOON];
    moon->SetPosition(m.centre);
    moon->SetRotation(m.rotation);
    moon->SetScale(vec3(m.radius));

    for (int i = 0; i < 2; i++){
        vec3 foot,eye,up;
        PostPose(i,foot,eye,up);
        posts[i]->SetPosition(foot);
        posts[i]->SetRotation(quat::getquat(vec3(0,1,0),up));
    }

    ship_root->SetPosition(ship.position);
    ship_root->SetRotation(ship.orientation);
    if (ship_model){
        //Nose along the root's forward (-z), centred on the root, one ship_size long.
        float scale = settings.ship_size / SHIP_MODEL_LENGTH;
        quat half_turn = quat(vec3(0,1,0),TYPE_PI);
        vec3 centre;
        Mesh* mesh = ship_model->GetMesh();
        if (mesh && mesh->HasBounds()){
            centre = (mesh->GetBoundsMin() + mesh->GetBoundsMax()) * 0.5f;
        }
        ship_model->SetScale(vec3(scale));
        ship_model->SetRotation(half_turn);
        ship_model->SetPosition(-(half_turn * (centre * scale)));
    }
}

//=== the view ===================================================================================

void ApplicationFarside::UpdateView(){
    InputController* input = main_scene->inputcontroller;
    //The wheel moves the height asked for. Read every pass, so the delta never piles up behind a
    //gate, and handed to the next tick, which is where the height is changed.
    float wheel = (float)input->GetDelta(INPUT_MOUSE_WHEEL);
    if (main_window->f_has_focus && !UIWantsMouse()){
        wheel_notches += wheel;
    }
    UpdateCamera();
    UpdateSun();
    //The overlay's sight region is a few thousand rays; a few times a second is plenty to read.
    if ((view_pass++ % 4) == 0){
        BuildDebugLines();
    }
    TakeHudSnapshot();
}

void ApplicationFarside::UpdateCamera(){
    Camera* camera = main_scene->camera;
    //The zoom follows the altitude ASKED FOR (README, "The zoom follows the altitude"), not the
    //height the ship happens to be at - crossing a planet lifts the ship but leaves the framing.
    float ceiling = Ceiling();
    float t = (ceiling > 1e-3f) ? clamp(ship.height_set / ceiling,0.0f,1.0f) : 0.0f;
    float lengths = settings.view_ground + (settings.view_ceiling - settings.view_ground) * t;
    float tan_half = tanf(toradians(settings.fov_deg) * 0.5f);
    if (!f_camera_placed){
        camera_lengths = lengths;
    }
    camera_lengths += (lengths - camera_lengths) * 0.08f;
    float distance = camera_lengths * 0.5f / tan_half;
    vec3 goal = ship.position + vec3(0.0f,distance,0.0f);
    if (!f_camera_placed){
        camera_position = goal;
        f_camera_placed = true;
    }
    camera_position = camera_position + (goal - camera_position) * 0.15f;
    camera_target = ship.position;
    camera->SetPosition(camera_position);
    camera->SetRotation(overhead_rotation);
    camera->SetupPerspective(renderer->width,renderer->height,settings.fov_deg,
                             fmaxf(0.3f,distance * 0.05f),distance + 900.0f);
}

void ApplicationFarside::UpdateSun(){
    float az = toradians(settings.sun_azimuth_deg);
    float el = toradians(settings.sun_elevation_deg);
    vec3 to_sun = vec3(sinf(az) * cosf(el),sinf(el),-cosf(az) * cosf(el));
    //The shadow box follows the view: square, wide enough for the screen's diagonal.
    float aspect = (float)renderer->width / fmaxf(1.0f,(float)renderer->height);
    float half = camera_lengths * 0.5f * sqrtf(1.0f + aspect * aspect) * 1.15f + 8.0f;
    vec3 focus = vec3(camera_position.x,0.0f,camera_position.z);
    sun->SetPosition(focus + to_sun * 500.0f);
    sun->SetLookAt(focus);
    sun->viewport.zoom = half;
    sun->viewport.znear = 1.0f;
    sun->viewport.zfar = 1100.0f;
    sun->brightness = settings.sun_brightness;
    renderer->ambient_sky = vec3(settings.ambient * 0.8f,settings.ambient * 0.9f,settings.ambient * 1.2f);
    renderer->ambient_ground = vec3(settings.ambient * 0.5f);
}

float ApplicationFarside::ScreenLengthsAtShip() const{
    float depth = camera_position.y - ship.position.y;
    return 2.0f * depth * tanf(toradians(settings.fov_deg) * 0.5f);
}

void ApplicationFarside::BuildDebugLines(){
    std::vector<line_vertex> v;
    float ceiling = Ceiling();

    //Where each ground body crosses the plane: the edge of the part that can be reached.
    if (settings.f_waterlines){
        for (const FieldSphere& s:field.spheres){
            if (!s.f_ground || s.radius <= fabsf(s.centre.y)){
                continue;
            }
            float r = sqrtf(s.radius * s.radius - s.centre.y * s.centre.y);
            const int n = 160;
            for (int k = 0; k < n; k++){
                float a0 = (float)k / n * 2.0f * TYPE_PI;
                float a1 = (float)(k + 1) / n * 2.0f * TYPE_PI;
                AddSegment(v,vec3(s.centre.x + cosf(a0) * r,0.02f,s.centre.z + sinf(a0) * r),
                             vec3(s.centre.x + cosf(a1) * r,0.02f,s.centre.z + sinf(a1) * r),LINE_WATERLINE);
            }
        }
    }

    //The region the chosen post can see, for a ship at the height the player's is at now: an
    //outline between sampled points it sees and points it does not, on a grid snapped to the
    //world so it does not crawl as the camera moves.
    int which = settings.overlay_post;
    if (which == 0 || which == 1){
        vec3 foot,eye,up;
        PostPose(which,foot,eye,up);
        float aspect = (float)renderer->width / fmaxf(1.0f,(float)renderer->height);
        float view = ScreenLengthsAtShip();
        float cell = powf(2.0f,roundf(log2f(fmaxf(view,1.0f) / 64.0f)));
        cell = fmaxf(cell,0.25f);
        float half_w = view * aspect * 0.55f;
        float half_h = view * 0.55f;
        int cols = (int)(2.0f * half_w / cell) + 2;
        int rows = (int)(2.0f * half_h / cell) + 2;
        if (cols * rows > 40000){
            cols = rows = 0;
        }
        float x0 = floorf((camera_position.x - half_w) / cell) * cell;
        float z0 = floorf((camera_position.z - half_h) / cell) * cell;
        float h = ship.f_landed ? settings.flight.clearance : fmaxf(ship.height,settings.flight.clearance);
        std::vector<uint8_t> seen((size_t)cols * rows);
        std::vector<float> ys((size_t)cols * rows);
        for (int j = 0; j < rows; j++){
            for (int i = 0; i < cols; i++){
                float x = x0 + i * cell;
                float z = z0 + j * cell;
                float y = fminf(field.Ground(x,z).height + h,ceiling);
                ys[(size_t)j * cols + i] = y;
                seen[(size_t)j * cols + i] = field.Blocked(eye,vec3(x,y,z)) ? 0 : 1;
            }
        }
        for (int j = 0; j < rows; j++){
            for (int i = 0; i < cols; i++){
                size_t at = (size_t)j * cols + i;
                float x = x0 + i * cell;
                float z = z0 + j * cell;
                if (i + 1 < cols && seen[at] != seen[at + 1]){
                    float y = fmaxf(ys[at],ys[at + 1]) + 0.04f;
                    AddSegment(v,vec3(x + cell * 0.5f,y,z - cell * 0.5f),vec3(x + cell * 0.5f,y,z + cell * 0.5f),LINE_REGION);
                }
                if (j + 1 < rows && seen[at] != seen[at + cols]){
                    float y = fmaxf(ys[at],ys[at + cols]) + 0.04f;
                    AddSegment(v,vec3(x - cell * 0.5f,y,z + cell * 0.5f),vec3(x + cell * 0.5f,y,z + cell * 0.5f),LINE_REGION);
                }
                //Sparse ticks on the seen side, so the outline says which side is which.
                if (seen[at] && (i % 4) == 0 && (j % 4) == 0){
                    float y = ys[at] + 0.04f;
                    float d = cell * 0.35f;
                    AddSegment(v,vec3(x - d,y,z),vec3(x + d,y,z),LINE_REGION_DOT);
                    AddSegment(v,vec3(x,y,z - d),vec3(x,y,z + d),LINE_REGION_DOT);
                }
            }
        }
    }

    if (settings.f_sight_lines){
        for (int i = 0; i < 2; i++){
            vec3 foot,eye,up;
            PostPose(i,foot,eye,up);
            AddSegment(v,eye,ship.position,f_seen[i] ? LINE_SEEN : LINE_HIDDEN);
        }
    }

    //Altitude from straight above is hard to read; a line down to the ground and a cross there.
    if (settings.f_drop_line && !ship.f_landed){
        vec3 below = vec3(ship.position.x,ship.ground.height + 0.03f,ship.position.z);
        AddSegment(v,ship.position,below,LINE_DROP);
        AddSegment(v,below + vec3(-0.35f,0,0),below + vec3(0.35f,0,0),LINE_DROP);
        AddSegment(v,below + vec3(0,0,-0.35f),below + vec3(0,0,0.35f),LINE_DROP);
    }

    std::lock_guard<std::mutex> lock(lines_mutex);
    pending_lines.swap(v);
    f_lines_pending = true;
}

void ApplicationFarside::TakeHudSnapshot(){
    BenchHud h;
    h.height = ship.height;
    h.height_set = ship.height_set;
    h.ground = ship.ground.height;
    h.ceiling = Ceiling();
    h.speed = ship.velocity.length();
    h.f_landed = ship.f_landed;
    h.f_seen[0] = f_seen[0];
    h.f_seen[1] = f_seen[1];
    float view = ScreenLengthsAtShip();
    h.view_lengths = view;
    h.ship_px = (view > 1e-3f) ? (float)renderer->height * settings.ship_size / view : 0.0f;
    //The planet's outline is its equator, at the centre's depth.
    const FieldSphere& p = field.spheres[SPHERE_PLANET];
    float planet_view = 2.0f * (camera_position.y - p.centre.y) * tanf(toradians(settings.fov_deg) * 0.5f);
    h.planet_screen = (planet_view > 1e-3f) ? 2.0f * p.radius / planet_view : 0.0f;
    std::lock_guard<std::mutex> lock(hud_mutex);
    hud = h;
}

//RENDER THREAD.
void ApplicationFarside::PreRender(void){
    std::vector<line_vertex> lines;
    {
        std::lock_guard<std::mutex> lock(lines_mutex);
        if (!f_lines_pending){
            return;
        }
        lines.swap(pending_lines);
        f_lines_pending = false;
    }
    if (lines.empty()){
        AddSegment(lines,vec3(),vec3(),0);
    }
    debug_lines->GetMesh()->SetLineMeshData(lines.data(),(int)lines.size());
}

//RENDER THREAD, with the planet shader bound.
void ApplicationFarside::SetPlanetUniforms(){
    //What the default shader is handed and a custom one is not - see chasm's SetGroundUniforms.
    planet_shader->Setint("f_normal_mapping",(int)renderer->f_normal_mapping);
    planet_shader->Setfloat("alpha_clip",renderer->alpha_clip);

    vec3 centres[2];
    vec4 inverses[2];
    float radii[2];
    int styles[2] = {0,1};
    int ids[2] = {SPHERE_PLANET,SPHERE_MOON};
    for (int i = 0; i < 2; i++){
        const FieldSphere& s = field.spheres[ids[i]];
        quat inv = s.rotation;
        inv.inverse();
        centres[i] = s.centre;
        inverses[i] = vec4(inv.x,inv.y,inv.z,inv.w);
        radii[i] = s.radius;
    }
    planet_shader->Setint("body_count",2);
    planet_shader->Setvec3v("body_centre",centres,2);
    planet_shader->Setvec4v("body_inverse",inverses,2);
    planet_shader->Setfloatv("body_radius",radii,2);
    planet_shader->Setintv("body_style",styles,2);
    planet_shader->Setvec3("sun_direction",sun ? -sun->GetForward() : vec3(0,1,0));
}

//RENDER THREAD, with the halo shader bound.
void ApplicationFarside::SetHaloUniforms(){
    halo_shader->Setvec3("sun_direction",sun ? -sun->GetForward() : vec3(0,1,0));
    halo_shader->Setfloat("inner",1.0f / HALO_SCALE);
    //Back faces only, so the glow shows beyond the planet's edge and never over it, and no depth
    //write, so it covers nothing drawn after it. CustomShaderSubPass resets both afterwards.
    glCullFace(GL_FRONT);
    glDepthMask(GL_FALSE);
}

//RENDER THREAD, no lock: everything comes from the snapshot.
void ApplicationFarside::DrawOverlay(){
    BenchHud h;
    {
        std::lock_guard<std::mutex> lock(hud_mutex);
        h = hud;
    }
    //Top middle: the debug panels take the left and right edges.
    const float size = 17.0f;
    const float step = 23.0f;
    float x = (float)main_window->width * 0.5f;
    float y = 46.0f;
    uint32_t title = UIColor(140,200,255);
    uint32_t text = UIColor(210,220,230);
    uint32_t seen = UIColor(90,255,110);
    uint32_t hidden = UIColor(255,90,70);
    char line[200];

    overlay->AddText("FARSIDE SCALE BENCH",vec2(x,y),size,title,UI_ALIGN_CENTER);
    y += step;
    snprintf(line,sizeof(line),"height %.1f (asked %.1f)   ground %.1f   ceiling %.1f   speed %.1f   %s",
             h.height,h.height_set,h.ground,h.ceiling,h.speed,h.f_landed ? "LANDED" : "flying");
    overlay->AddText(line,vec2(x,y),size,text,UI_ALIGN_CENTER);
    y += step;
    snprintf(line,sizeof(line),"ship %.0f px long   screen %.0f lengths tall   planet %.0f%% of screen height",
             h.ship_px,h.view_lengths,h.planet_screen * 100.0f);
    overlay->AddText(line,vec2(x,y),size,text,UI_ALIGN_CENTER);
    y += step;
    snprintf(line,sizeof(line),"edge post: %s",h.f_seen[0] ? "SEES YOU" : "hidden");
    overlay->AddText(line,vec2(x - 12.0f,y),size,h.f_seen[0] ? seen : hidden,UI_ALIGN_RIGHT);
    snprintf(line,sizeof(line),"top post: %s",h.f_seen[1] ? "SEES YOU" : "hidden");
    overlay->AddText(line,vec2(x + 12.0f,y),size,h.f_seen[1] ? seen : hidden,UI_ALIGN_LEFT);

    overlay->AddText("W/S thrust   A/D turn   R/F or wheel: altitude   land: descend to 0, slowly",
                     vec2((float)main_window->width - 16.0f,(float)main_window->height - 16.0f),
                     15.0f,UIColor(150,160,175),UI_ALIGN_RIGHT);
}

//=== settings, presets and tools ================================================================

std::vector<BenchKnob> ApplicationFarside::Knobs(){
    BenchSettings& s = settings;
    FlightTuning& f = settings.flight;
    return {
        {"Planet","planet_radius",&s.planet_radius,4.0f,80.0f},
        {"Planet","planet_sink",&s.planet_sink,0.0f,0.95f},
        {"Planet","planet_turn_minutes",&s.planet_turn_minutes,0.0f,30.0f},
        {"Planet","planet_tilt_deg",&s.planet_tilt_deg,0.0f,30.0f},
        {"Moon","moon_radius",&s.moon_radius,1.0f,20.0f},
        {"Moon","moon_sink",&s.moon_sink,0.0f,0.95f},
        {"Moon","moon_orbit",&s.moon_orbit,25.0f,150.0f},
        {"Moon","moon_orbit_minutes",&s.moon_orbit_minutes,0.0f,20.0f},
        {"Posts","post_height",&s.post_height,0.05f,3.0f},
        {"Posts","post_edge_angle_deg",&s.post_edge_angle_deg,0.0f,90.0f},
        {"Posts","post_edge_azimuth_deg",&s.post_edge_azimuth_deg,0.0f,360.0f},
        {"Ship","ship_size",&s.ship_size,0.25f,4.0f},
        {"Flight","flight_accel",&f.accel,1.0f,60.0f},
        {"Flight","flight_speed_low",&f.speed_low,1.0f,40.0f},
        {"Flight","flight_speed_high",&f.speed_high,1.0f,60.0f},
        {"Flight","flight_turn_rate",&f.turn_rate,0.5f,8.0f},
        {"Flight","flight_drag",&f.drag,0.0f,5.0f},
        {"Flight","flight_climb_rate",&f.climb_rate,0.5f,30.0f},
        {"Flight","flight_follow_rate",&f.follow_rate,0.5f,20.0f},
        {"Flight","flight_land_speed",&f.land_speed,0.1f,6.0f},
        {"Flight","flight_land_slope_deg",&f.land_slope_deg,5.0f,80.0f},
        {"View","fov_deg",&s.fov_deg,10.0f,70.0f},
        {"View","view_ground",&s.view_ground,6.0f,120.0f},
        {"View","view_ceiling",&s.view_ceiling,12.0f,300.0f},
        {"View","ceiling_margin",&s.ceiling_margin,0.5f,20.0f},
        {"Light","sun_azimuth_deg",&s.sun_azimuth_deg,0.0f,360.0f},
        {"Light","sun_elevation_deg",&s.sun_elevation_deg,0.0f,80.0f},
        {"Light","sun_brightness",&s.sun_brightness,0.0f,20.0f},
        {"Light","ambient",&s.ambient,0.0f,0.5f},
    };
}

json ApplicationFarside::SettingsJson(){
    json out = json::object();
    for (const BenchKnob& k:Knobs()){
        out[k.name] = *k.value;
    }
    out["sight_lines"] = settings.f_sight_lines;
    out["overlay_post"] = settings.overlay_post;
    out["waterlines"] = settings.f_waterlines;
    out["drop_line"] = settings.f_drop_line;
    return out;
}

json ApplicationFarside::StatusJson(){
    float ceiling = Ceiling();
    float view = ScreenLengthsAtShip();
    const FieldSphere& p = field.spheres[SPHERE_PLANET];
    const FieldSphere& m = field.spheres[SPHERE_MOON];
    float planet_view = 2.0f * (camera_position.y - p.centre.y) * tanf(toradians(settings.fov_deg) * 0.5f);
    json posts_json = json::array();
    for (int i = 0; i < 2; i++){
        vec3 foot,eye,up;
        PostPose(i,foot,eye,up);
        posts_json.push_back(json{
            {"name",i == 0 ? "edge" : "top"},
            {"eye",json::array({eye.x,eye.y,eye.z})},
            {"sees_ship",f_seen[i]},
        });
    }
    return json{
        {"tick",bench_tick},
        {"ship",{
            {"position",json::array({ship.position.x,ship.position.y,ship.position.z})},
            {"yaw_deg",ship.yaw * 180.0f / TYPE_PI},
            {"speed",ship.velocity.length()},
            {"height",ship.height},
            {"height_set",ship.height_set},
            {"ground",ship.ground.height},
            {"on_body",ship.ground.sphere},
            {"landed",ship.f_landed},
        }},
        {"ceiling",ceiling},
        {"posts",posts_json},
        {"view",{
            {"screen_lengths_at_ship",view},
            {"ship_px",(view > 1e-3f) ? (float)renderer->height * settings.ship_size / view : 0.0f},
            {"planet_fraction_of_screen_height",(planet_view > 1e-3f) ? 2.0f * p.radius / planet_view : 0.0f},
            {"camera",json::array({camera_position.x,camera_position.y,camera_position.z})},
        }},
        {"planet_angle_deg",planet_angle * 180.0f / TYPE_PI},
        {"moon_position",json::array({m.centre.x,m.centre.y,m.centre.z})},
    };
}

bool ApplicationFarside::ApplyPreset(const std::string& name){
    float ceiling = Ceiling();
    auto flying = [&](float x, float z, float yaw_deg, float height_set){
        ship = ShipState();
        ship.yaw = toradians(yaw_deg);
        ship.height_set = clamp(height_set,0.0f,ceiling);
        ship.position = vec3(x,0.0f,z);
        ship.position.y = fminf(field.Ground(x,z).height + fmaxf(ship.height_set,settings.flight.clearance),ceiling);
    };
    //Landed on the planet `angle` degrees from its pole, at `azimuth` in the planet's own frame.
    auto landed = [&](float angle, float azimuth){
        ship = ShipState();
        const FieldSphere& body = field.spheres[SPHERE_PLANET];
        float a = toradians(angle);
        float b = toradians(azimuth);
        vec3 local = vec3(sinf(a) * cosf(b),cosf(a),sinf(a) * sinf(b));
        ship.landed_local = local * (body.radius + settings.flight.clearance);
        vec3 tangent = vec3(-sinf(b),0.0f,cosf(b));
        ship.landed_forward = tangent;
        ship.landed_on = SPHERE_PLANET;
        ship.f_landed = true;
        ship.height_set = 0.0f;
        ship.position = body.centre + body.rotation * ship.landed_local;
    };
    if (name == "start"){
        //Close enough that the planet is in view, heading for it.
        flying(-28.0f,10.0f,-90.0f,2.0f);
    }else if (name == "low"){
        flying(-30.0f,6.0f,-90.0f,0.5f);
    }else if (name == "landed"){
        //On the far side of the cap from the edge post, at the same distance from the pole.
        landed(settings.post_edge_angle_deg,settings.post_edge_azimuth_deg + 180.0f);
    }else if (name == "landed_near"){
        //Beside the edge post, where it can see you.
        landed(settings.post_edge_angle_deg - 6.0f,settings.post_edge_azimuth_deg + 8.0f);
    }else if (name == "cruise"){
        flying(0.0f,30.0f,-90.0f,ceiling);
    }else if (name == "starcom"){
        //The framing of the reference screenshot: the planet's outline 70% of the screen's height.
        //The outline is the planet's equator, well below the ship, so the camera distance that
        //frames it depends on how high the ship sits - which depends on the zoom asked for. A few
        //rounds of that loop settle it.
        float x = -4.0f;
        float z = 6.0f;
        float tan_half = tanf(toradians(settings.fov_deg) * 0.5f);
        float planet_y = field.spheres[SPHERE_PLANET].centre.y;
        float planet_distance = settings.planet_radius * 2.0f / 0.7f * 0.5f / tan_half;
        float span = fmaxf(settings.view_ceiling - settings.view_ground,1e-3f);
        float t = 0.5f;
        for (int i = 0; i < 8; i++){
            float ship_y = fminf(field.Ground(x,z).height + t * ceiling,ceiling);
            float lengths = 2.0f * (planet_distance - (ship_y - planet_y)) * tan_half;
            t = clamp((lengths - settings.view_ground) / span,0.0f,1.0f);
        }
        flying(x,z,-60.0f,t * ceiling);
    }else{
        return false;
    }
    f_camera_placed = false;
    return true;
}

#ifdef USE_IMGUI
void ApplicationFarside::DrawImGuiUI(){
    RenderDebugMenuBar();
    RenderApplicationUI();

    //On the right the first time; imgui.ini remembers wherever it is moved to after that.
    ImGui::SetNextWindowPos(ImVec2((float)main_window->width - 400.0f,40.0f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f,(float)main_window->height - 80.0f),ImGuiCond_FirstUseEver);
    ImGui::Begin("Bench");
    //A window first made while the app was minimised gets saved a few pixels across - ImGui had no
    //display to size it to - and imgui.ini brings it back that way every run, past FirstUseEver.
    if (ImGui::GetWindowSize().x < 200.0f || ImGui::GetWindowSize().y < 200.0f){
        ImGui::SetWindowPos(ImVec2((float)main_window->width - 400.0f,40.0f));
        ImGui::SetWindowSize(ImVec2(390.0f,(float)main_window->height - 80.0f));
    }
    //Narrow sliders, so the names beside them fit.
    ImGui::PushItemWidth(170.0f);
    const char* presets[] = {"start","low","landed","landed_near","cruise","starcom"};
    ImGui::TextDisabled("Put the ship at:");
    for (int i = 0; i < 6; i++){
        if (i > 0 && i != 3){
            ImGui::SameLine();
        }
        if (ImGui::Button(presets[i])){
            //DrawImGuiUI holds physics_mutex, so this is between ticks, as a tool's change is.
            ApplyPreset(presets[i]);
        }
    }
    std::string section;
    for (const BenchKnob& k:Knobs()){
        if (section != k.section){
            section = k.section;
            ImGui::Separator();
            ImGui::TextDisabled("%s",k.section);
        }
        ImGui::SliderFloat(k.name,k.value,k.min,k.max);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Debug picture");
    ImGui::Checkbox("sight lines",&settings.f_sight_lines);
    ImGui::Checkbox("waterlines",&settings.f_waterlines);
    ImGui::Checkbox("drop line",&settings.f_drop_line);
    ImGui::Text("visible region of:");
    ImGui::SameLine();
    ImGui::RadioButton("edge post",&settings.overlay_post,0);
    ImGui::SameLine();
    ImGui::RadioButton("top post",&settings.overlay_post,1);
    ImGui::SameLine();
    ImGui::RadioButton("none",&settings.overlay_post,-1);
    ImGui::PopItemWidth();
    ImGui::End();
}
#endif

#ifdef USE_MCP
void ApplicationFarside::RegisterMCPTools(){
    MCPServer::Get()->RegisterTool("farside_status",
        "The scale bench's state: the ship (position, yaw, speed, height above the ground, the "
        "height asked for, which body it is over, landed), the ceiling, each defence post's eye "
        "and whether it sees the ship, and the scale on screen - how many ship lengths the screen "
        "is tall at the ship, the ship's length in pixels and the planet's diameter as a fraction "
        "of the screen's height. Plus every setting farside_set accepts.",
        json{ {"type","object"},{"properties",json::object()} },
        [this](const json& args) -> json {
            json out;
            main_scene->AtTickBoundary([&](){
                out = StatusJson();
                out["settings"] = SettingsJson();
            });
            return out;
        });

    MCPServer::Get()->RegisterTool("farside_set",
        "Change the bench between ticks. `settings` is an object of any names farside_status lists "
        "(numbers are clamped to the panel's ranges; sight_lines, waterlines and drop_line are "
        "booleans; overlay_post is 0 edge, 1 top, -1 none). `preset` puts the ship at a comparison "
        "spot: start, low, landed (far side of the cap from the edge post), landed_near (beside "
        "it), cruise (at the ceiling) or starcom (the reference screenshot's framing). `ship` "
        "places it by hand: x, z, yaw_deg, height (the height asked for). Returns farside_status.",
        json{
            {"type","object"},
            {"properties",{
                {"settings",{{"type","object"}}},
                {"preset",{{"type","string"}}},
                {"ship",{{"type","object"},{"description","x, z, yaw_deg, height - any subset"}}}
            }}
        },
        [this](const json& args) -> json {
            json out;
            std::string error;
            main_scene->AtTickBoundary([&](){
                if (args.contains("settings") && args["settings"].is_object()){
                    std::vector<BenchKnob> knobs = Knobs();
                    for (auto& item:args["settings"].items()){
                        const std::string& key = item.key();
                        const json& value = item.value();
                        bool f_known = false;
                        for (const BenchKnob& k:knobs){
                            if (key == k.name && value.is_number()){
                                *k.value = clamp(value.get<float>(),k.min,k.max);
                                f_known = true;
                            }
                        }
                        if (value.is_boolean()){
                            if (key == "sight_lines"){ settings.f_sight_lines = value.get<bool>(); f_known = true; }
                            if (key == "waterlines"){ settings.f_waterlines = value.get<bool>(); f_known = true; }
                            if (key == "drop_line"){ settings.f_drop_line = value.get<bool>(); f_known = true; }
                        }
                        if (key == "overlay_post" && value.is_number()){
                            settings.overlay_post = (int)clamp(value.get<float>(),-1.0f,1.0f);
                            f_known = true;
                        }
                        if (!f_known){
                            error += "unknown or mistyped setting '" + key + "'; ";
                        }
                    }
                    //Bodies follow their settings now, not at the next tick, so a preset below
                    //lands on the planet as it is about to be.
                    UpdateBodies();
                }
                if (args.contains("preset") && args["preset"].is_string()){
                    if (!ApplyPreset(args["preset"].get<std::string>())){
                        error += "unknown preset; ";
                    }
                }
                if (args.contains("ship") && args["ship"].is_object()){
                    const json& s = args["ship"];
                    float ceiling = Ceiling();
                    ship.f_landed = false;
                    ship.landed_on = -1;
                    ship.velocity = vec3();
                    if (s.contains("x") && s["x"].is_number()){ ship.position.x = s["x"].get<float>(); }
                    if (s.contains("z") && s["z"].is_number()){ ship.position.z = s["z"].get<float>(); }
                    if (s.contains("yaw_deg") && s["yaw_deg"].is_number()){ ship.yaw = toradians(s["yaw_deg"].get<float>()); }
                    if (s.contains("height") && s["height"].is_number()){
                        ship.height_set = clamp(s["height"].get<float>(),0.0f,ceiling);
                    }
                    ship.position.y = fminf(field.Ground(ship.position.x,ship.position.z).height +
                                            fmaxf(ship.height_set,settings.flight.clearance),ceiling);
                    f_camera_placed = false;
                }
                PlaceObjects();
                out = StatusJson();
                out["settings"] = SettingsJson();
            });
            if (!error.empty()){
                out["error"] = error;
            }
            return out;
        });

    MCPServer::Get()->RegisterTool("farside_hold",
        "Fly the ship as a player would: hold one control for a number of ticks (60 a second). "
        "action is thrust, reverse, left, right, climb or descend. Holds stack, so a turn while "
        "thrusting is two calls. Blocks until the hold has run (unless the simulation is paused - "
        "then pair it with sim_step) and returns farside_status.",
        json{
            {"type","object"},
            {"properties",{
                {"action",{{"type","string"},{"enum",json::array({"thrust","reverse","left","right","climb","descend"})}}},
                {"ticks",{{"type","number"},{"description","default 60, capped at 1200"}}},
                {"wait",{{"type","boolean"},{"description","block until the hold is done, default true"}}}
            }},
            {"required",json::array({"action"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input || !args.contains("action") || !args["action"].is_string()){
                return json{ {"error","action is required"} };
            }
            std::string action = args["action"].get<std::string>();
            uint32_t code = 0;
            if (action == "thrust"){ code = INPUT_TURN_UP; }
            else if (action == "reverse"){ code = INPUT_TURN_DOWN; }
            else if (action == "left"){ code = INPUT_TURN_LEFT; }
            else if (action == "right"){ code = INPUT_TURN_RIGHT; }
            else if (action == "climb"){ code = INPUT_CLIMB; }
            else if (action == "descend"){ code = INPUT_DESCEND; }
            else{
                return json{ {"error","unknown action"} };
            }
            float ticks = 60.0f;
            if (args.contains("ticks") && args["ticks"].is_number()){
                ticks = clamp(args["ticks"].get<float>(),1.0f,1200.0f);
            }
            input->HoldKey(code,(uint32_t)ticks);
            bool f_wait = true;
            if (args.contains("wait") && args["wait"].is_boolean()){
                f_wait = args["wait"].get<bool>();
            }
            if (f_wait && !main_scene->IsPhysicsPaused()){
                Sleep((DWORD)(ticks * GetPhysicsTimestep() * 1000.0f) + 50);
            }
            json out;
            main_scene->AtTickBoundary([&](){
                out = StatusJson();
            });
            return out;
        });
}
#endif
