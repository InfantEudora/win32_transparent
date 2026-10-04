#include "ApplicationChasm.h"

#ifdef USE_IMGUI
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#ifdef USE_MCP
//Only this from the server: see the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include "Debug.h"
#include "Primitives.h"
#include "Light.h"

#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <unordered_set>

static Debugger* debug = new Debugger("ApplicationChasm",DEBUG_ALL);

#define CHASM_CAM_PITCH_MIN     0.25f       //radians - low enough to see the chasm walls later
#define CHASM_CAM_PITCH_MAX     1.55f       //just short of straight down, where yaw stops meaning anything
#define CHASM_CAM_DIST_MIN      6.0f
#define CHASM_CAM_ORBIT_RATE    0.005f      //radians per mouse count
#define CHASM_CAM_KEY_TURN      1.6f        //radians per second, Q/E
#define CHASM_CAM_KEY_PAN       0.9f        //camera distances per second, arrows/WASD
#define CHASM_CAM_DRAG_PAN      0.0012f     //camera distances per mouse count, right-drag
#define CHASM_CAM_WHEEL_STEP    0.88f       //distance multiplier per wheel notch
#define CHASM_GROUND_Y          -0.05f      //just under the lines, so they never fight its depth
#define CHASM_GRID_VIEW_Y       0.02f
#define PICK_VIEW_Y             0.05f       //over the grid view's lines

ApplicationChasm::ApplicationChasm():Application(){
    app_name = "Chasm";
    debug->Info("Created new ApplicationChasm.\n");
    f_show_scene_window = false;
    f_show_inspector_window = false;
    f_show_engine_window = true;
}

void ApplicationChasm::Init(void){
    GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    //Both names from the members - see the long note at the same call in apps/bomber.
    if (!renderer->Init(shader_vert_name,shader_frag_name,PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    //No skybox until the look is decided (step 4). A flat clear reads the grid lines best.
    renderer->f_render_skybox = false;
    default_shader = new Shader(shader_vert_name,shader_lit_frag_name);

    main_window->Resize(1440,810);

    main_scene = CreateNewScene("Chasm");
    assetmanager = new AssetManager();
    rrand = new RRandom(1);

    /*
        Far plane for the whole map. Step 1's debug camera can pull back past the game's maximum
        zoom-out (README.md, "one zoom range") so the whole grid can be inspected; the near plane
        is kept at 0.5 so that range costs little depth precision on lines lying flat on the ground.
    */
    main_scene->camera->SetupPerspective(renderer->width,renderer->height,40.0f,0.5f,4000.0f);

    BuildScene();
    RegenerateGrid(next_settings);
    {
        std::shared_ptr<const Grid> g = GetGrid();
        FitGround(*g);
        ground_fitted_to = g;
    }
    //Init is the render thread before the physics thread exists, so the camera may be set here.
    FrameMap();

    SetupInput();
#ifdef USE_MCP
    RegisterMCPTools();
#endif
}

void ApplicationChasm::BuildScene(){
    //A key light and a cool fill, the repo's usual pair - see BuildLighting in apps/bomber for why
    //one light alone reads as night. No shadows yet: there is nothing standing to cast one.
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-300,500,200));
    sun->color = vec3(1.0f,0.96f,0.90f);
    sun->brightness = 5.0f;
    sun->f_casts_shadow = false;
    sun->SetLookAt(vec3());
    main_scene->AddObject(sun);

    DirectionalLight* fill = new DirectionalLight();
    fill->name = "Directional Light (Fill)";
    fill->SetPosition(vec3(300,350,-300));
    fill->color = vec3(0.72f,0.80f,1.00f);
    fill->brightness = 1.8f;
    fill->f_casts_shadow = false;
    fill->SetLookAt(vec3());
    main_scene->AddObject(fill);

    //The ground under the lines: a unit quad laid flat and scaled to the map by FitGround.
    Material mat = {};
    mat.name = "chasm_ground";
    mat.glsl_material.color = vec4(0.20f,0.26f,0.19f,1.0f);
    mat.glsl_material.metallic = 0.0f;
    mat.glsl_material.roughness = 0.95f;
    renderer->AddMaterial(mat);

    ground = new Object();
    ground->name = "Ground";
    ground->SetMesh(MakeQuad(1.0f,1.0f));
    ground->SetMaterialSlot(0,renderer->FindMaterialIndex("chasm_ground"));
    //MakeQuad faces +Z; -90 degrees about X lays it down facing up. See core/Primitives.h.
    ground->SetRotation(quat(vec3(1,0,0),-1.5707963f));
    ground->SetPickability(false);
    ground->SetVisualOnly(true);
    main_scene->AddObject(ground);

    pick_view = new Object();
    pick_view->name = "Pick View";
    pick_view->SetVisualOnly(true);
    pick_view_mesh = new Mesh();
    pick_view->SetMesh(pick_view_mesh);
    pick_view->SetPickability(false);
    pick_view->SetCastsShadow(false);
    pick_view->SetVisibility(false);
    pick_view->SetPosition(vec3(0.0f,PICK_VIEW_Y,0.0f));
    main_scene->AddObject(pick_view);

#ifdef DEBUG
    grid_view = new Object();
    grid_view->name = "Grid View";
    grid_view->SetVisualOnly(true);
    grid_view_mesh = new Mesh();
    grid_view->SetMesh(grid_view_mesh);
    grid_view->SetPickability(false);
    grid_view->SetCastsShadow(false);
    grid_view->SetVisibility(false);
    grid_view->SetPosition(vec3(0.0f,CHASM_GRID_VIEW_Y,0.0f));
    main_scene->AddObject(grid_view);
#endif
}

void ApplicationChasm::SetupInput(){
    InputController* input = main_scene->inputcontroller;
    //Arrows and WASD already pan through core's INPUT_MOVE_* / INPUT_TURN_* maps.
#if defined(_WIN32)
    input->AddKeyMap('Q',INPUT_CHASM_ROTATE_LEFT);
    input->AddKeyMap('E',INPUT_CHASM_ROTATE_RIGHT);
    input->AddKeyMap('F',INPUT_CHASM_FRAME);
    input->AddKeyMap('N',INPUT_CHASM_NEXT_SEED);
#endif
}

//--- The grid -----------------------------------------------------------------------------------

void ApplicationChasm::RegenerateGrid(const GridSettings& s){
    std::shared_ptr<Grid> g = std::make_shared<Grid>();
    g->Generate(s);
    auto t0 = std::chrono::steady_clock::now();
    std::shared_ptr<GridPicker> p = std::make_shared<GridPicker>(g);
    float pick_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    debug->Info("Grid: seed %u, %i coarse and %i fine quads, %i leftover triangles of %i, %.1f ms "
                "(+%.1f ms picker)\n",
                s.seed,(int)g->coarse.quads.size(),(int)g->fine.quads.size(),
                g->num_leftover_triangles,g->num_lattice_triangles,g->generate_ms,pick_ms);
    {
        std::lock_guard<std::mutex> lock(grid_mutex);
        grid = g;
        picker = p;
#ifdef DEBUG
        //A report is about one grid; the next one has not been checked.
        check_report.reset();
        selected_issue = -1;
        view_version++;
#endif
    }
    //Plot and quad indices mean nothing in another grid.
    std::lock_guard<std::mutex> lock(pick_mutex);
    hover_pick = GridPick();
    selected_pick = GridPick();
    pick_version++;
}

std::shared_ptr<const Grid> ApplicationChasm::GetGrid(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return grid;
}

std::shared_ptr<const GridPicker> ApplicationChasm::GetPicker(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return picker;
}

//--- Picking -------------------------------------------------------------------------------------

GridPick ApplicationChasm::GetHoverPick(){
    std::lock_guard<std::mutex> lock(pick_mutex);
    return hover_pick;
}

GridPick ApplicationChasm::GetSelectedPick(){
    std::lock_guard<std::mutex> lock(pick_mutex);
    return selected_pick;
}

void ApplicationChasm::SetSelectedPick(const GridPick& p){
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (selected_pick != p){
        selected_pick = p;
        pick_version++;
    }
}

//Physics thread or at a tick boundary: it reads the camera.
bool ApplicationChasm::GroundUnderPixel(int2 px, vec2& out){
    ray r = main_scene->camera->GetPixelRay(px);
    //Looking up or level never meets the ground in front of the camera.
    if (r.direction.y >= -1e-4f){
        return false;
    }
    plane ground_plane;
    ground_plane.pos = vec3(0.0f,0.0f,0.0f);
    ground_plane.normal = vec3(0.0f,1.0f,0.0f);
    vec3 at;
    if (!r.intersects_plane(ground_plane,at)){
        return false;
    }
    out = vec2(at.x,at.z);
    return true;
}

/*
    PHYSICS THREAD, every pass: what is under the cursor, and a left click selects it.

    The hover goes blank whenever the cursor is not over the scene - outside the window, over a
    panel, or with the window unfocused - so a stale highlight never sits where the mouse left it.
    The click is read as an edge on press and acted on only when the cursor is over the scene,
    which is what keeps a click on a panel, or beside the window (release_edge_ignores_focus in
    the memory notes), from also selecting the plot underneath.
*/
void ApplicationChasm::UpdatePick(){
    InputController* input = main_scene->inputcontroller;
    std::shared_ptr<const GridPicker> p = GetPicker();
    bool f_clicked = input->WasKeyPressed(INPUT_CLICK_LEFT);

    GridPick hover;
    bool f_over_scene = p && main_window->f_has_focus && !UIWantsMouse();
    if (f_over_scene){
        int2 px = input->GetRelativeMousePosition();
        f_over_scene = px.x >= 0 && px.y >= 0 && px.x < main_window->width && px.y < main_window->height;
        vec2 at;
        if (f_over_scene && GroundUnderPixel(px,at)){
            hover = p->Pick(at);
        }
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (hover != hover_pick){
        hover_pick = hover;
        pick_version++;
    }else{
        hover_pick.at = hover.at;   //same plot, new point - not worth a rebuild
    }
    if (f_clicked && f_over_scene && selected_pick != hover){
        //A click on nothing - off the map - clears the selection, which is also the way to clear it.
        selected_pick = hover;
        pick_version++;
    }
}

json ApplicationChasm::PickJson(const GridPicker& p, const GridPick& pick){
    if (!pick.f_hit){
        return json{{"hit",false},{"x",pick.at.x},{"z",pick.at.y}};
    }
    const Grid& g = p.GetGrid();
    vec2 corners[4];
    for (int k = 0; k < 4; k++){
        corners[k] = g.fine.pos[g.fine.quads[pick.fine_quad].v[k]];
    }
    const vec2& v = g.fine.pos[pick.plot];
    return json{
        {"hit",true},
        {"x",pick.at.x},
        {"z",pick.at.y},
        {"plot",{{"vertex",pick.plot},{"x",v.x},{"z",v.y},{"sides",p.PlotQuadCount(pick.plot)},
                 {"area",p.PlotArea(pick.plot)},{"on_outline",(bool)g.fine.f_boundary[pick.plot]},
                 {"coarse_corner",pick.plot < (int)g.coarse.pos.size()}}},
        {"fine_cell",{{"quad",pick.fine_quad},{"corner",pick.corner},{"area",p.FineArea(pick.fine_quad)},
                      {"squareness",GridQuadSquareness(corners)}}},
        {"coarse_cell",{{"quad",pick.coarse_quad},{"area",p.CoarseArea(pick.coarse_quad)}}}
    };
}

#define PICK_HOVER_PLOT        0xFFFFE040u
#define PICK_HOVER_CELL         0xFFFFFFFFu
#define PICK_HOVER_COARSE       0xFF80C8FFu
#define PICK_SELECTED_PLOT      0xFFFF50E0u

/*
    RENDER THREAD. The hover as three outlines - plot, fine cell, coarse cell - and the selected
    plot filled in with spokes from its vertex, so the two read differently at a glance. A few
    dozen lines, rebuilt only when either pick changes.
*/
void ApplicationChasm::UpdatePickView(){
    std::shared_ptr<const GridPicker> p = GetPicker();
    int version = pick_version.load();
    if (!p || (p == pick_view_built_for && version == pick_view_built_version)){
        return;
    }
    pick_view_built_for = p;
    pick_view_built_version = version;
    GridPick hover = GetHoverPick();
    GridPick selected = GetSelectedPick();
    const Grid& g = p->GetGrid();

    std::vector<line_vertex> verts;
    std::vector<vec2> segs;
    auto emit = [&verts,&segs](uint32_t color){
        line_vertex v;
        v.color = color;
        for (const vec2& s : segs){
            v.pos = vec3(s.x,0.0f,s.y);
            verts.push_back(v);
        }
        segs.clear();
    };

    if (selected.f_hit){
        p->PlotOutline(selected.plot,segs);
        //The outline again, shrunk toward the vertex a step at a time: a 1-pixel line cannot be
        //thick, but nested copies of it read as fill.
        const vec2 c = g.fine.pos[selected.plot];
        size_t n = segs.size();
        const int RINGS = 5;
        for (int r = 1; r < RINGS; r++){
            float t = (float)r / RINGS;
            for (size_t i = 0; i < n; i++){
                vec2 s = segs[i];
                segs.push_back(c + (s - c) * t);
            }
        }
        emit(PICK_SELECTED_PLOT);
    }
    if (hover.f_hit){
        p->CoarseOutline(hover.coarse_quad,segs);
        emit(PICK_HOVER_COARSE);
        const GridQuad& q = g.fine.quads[hover.fine_quad];
        for (int k = 0; k < 4; k++){
            segs.push_back(g.fine.pos[q.v[k]]);
            segs.push_back(g.fine.pos[q.v[(k + 1) % 4]]);
        }
        emit(PICK_HOVER_CELL);
        p->PlotOutline(hover.plot,segs);
        emit(PICK_HOVER_PLOT);
    }

    if (verts.empty()){
        pick_view->SetVisibility(false);
    }else{
        pick_view_mesh->SetLineMeshData(verts.data(),(int)verts.size());
        pick_view->SetVisibility(true);
    }
}

json ApplicationChasm::GridStatsJson(const Grid& g){
    const GridSettings& s = g.settings;
    char hash[32];
    snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)g.Hash());
    return json{
        {"settings",{
            {"seed",s.seed},
            {"target_fine_cells",s.target_fine_cells},
            {"aspect",s.aspect},
            {"triangle_side",s.triangle_side},
            {"relax_passes_coarse",s.relax_passes_coarse},
            {"relax_passes_fine",s.relax_passes_fine},
            {"relax_strength",s.relax_strength}
        }},
        {"lattice_triangles",g.num_lattice_triangles},
        {"leftover_triangles",g.num_leftover_triangles},
        {"coarse",{{"vertices",g.coarse.pos.size()},{"quads",g.coarse.quads.size()},{"edges",g.coarse.edges.size()}}},
        {"fine",{{"vertices",g.fine.pos.size()},{"quads",g.fine.quads.size()},{"edges",g.fine.edges.size()}}},
        {"bounds",{{"min_x",g.bounds_min.x},{"min_z",g.bounds_min.y},{"max_x",g.bounds_max.x},{"max_z",g.bounds_max.y}}},
        {"generate_ms",g.generate_ms},
        {"hash",hash}
    };
}

//RENDER THREAD. The quad is unit-sized, so the map's extent is all scale.
void ApplicationChasm::FitGround(const Grid& g){
    vec2 c = (g.bounds_min + g.bounds_max) * 0.5f;
    vec2 size = g.bounds_max - g.bounds_min;
    ground->SetPosition(vec3(c.x,CHASM_GROUND_Y,c.y));
    //Before the rotation the quad's height runs along y, which the rotation lays along z.
    ground->SetScale(vec3(size.x,size.y,1.0f));
}

//--- The camera ----------------------------------------------------------------------------------

void ApplicationChasm::FrameMap(){
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        return;
    }
    vec2 c = (g->bounds_min + g->bounds_max) * 0.5f;
    vec2 size = g->bounds_max - g->bounds_min;
    camera_target = vec3(c.x,0.0f,c.y);
    cam_yaw = 0.0f;
    //Steeper than the default view: at a shallow pitch the near edge drops out of the bottom of
    //the frame long before the far edge leaves the top.
    cam_pitch = 1.2f;
    //Enough to fit the depth, or the width at a 16:9 window, whichever is the larger ask - 1.5
    //being what a 40 degree vertical field of view needs per unit of extent, plus a margin.
    cam_distance = std::max(size.y,size.x / 1.78f) * 1.5f;
    ApplyCamera();
}

void ApplicationChasm::ApplyCamera(){
    Camera* camera = main_scene->camera;
    float cp = cosf(cam_pitch);
    vec3 offset(sinf(cam_yaw) * cp,sinf(cam_pitch),cosf(cam_yaw) * cp);
    vec3 pos = camera_target + offset * cam_distance;
    camera->SetPosition(pos);
    camera->SetLookAt(camera_target);
    cam_last_written = pos;
}

/*
    PHYSICS THREAD, every pass. The view only - nothing here is read by a tick.

    Middle-drag orbits, right-drag (or shift+middle) pans, the wheel zooms, arrows/WASD pan and
    Q/E turn. Mouse deltas are drained every pass whether or not a button is held: InputController
    only clears a delta that was read, so a read behind the button gate would let movement pile up
    and spend it all on the first frame of the next drag (apps/ship's UpdateView has the history).
*/
void ApplicationChasm::UpdateCamera(){
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - cam_last_update).count();
    cam_last_update = now;
    dt = std::max(0.0f,std::min(dt,0.1f));

    /*
        A camera_set from a tool moves the camera without touching cam_*, and the line at the end
        would put it straight back. So if the camera is not where this left it, adopt where it is:
        the orbit is re-derived from its position about camera_target, which the same tool may
        also have moved (GetCameraTargetPtr).
    */
    vec3 here = camera->GetPosition();
    if ((here - cam_last_written).length() > 1e-3f){
        vec3 off = here - camera_target;
        float len = off.length();
        if (len > 1e-3f){
            cam_distance = len;
            cam_pitch = asinf(std::max(-1.0f,std::min(1.0f,off.y / len)));
            cam_yaw = atan2f(off.x,off.z);
        }
    }

    int dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);
    cam_wheel += (float)input->GetDelta(INPUT_MOUSE_WHEEL);

    vec3 forward(-sinf(cam_yaw),0.0f,-cosf(cam_yaw));
    vec3 right(cosf(cam_yaw),0.0f,-sinf(cam_yaw));

    bool f_mouse = main_window->f_has_focus && !UIWantsMouse();
    if (f_mouse){
        bool f_shift = input->IsKeyDown(INPUT_SHIFT);
        if (input->IsKeyDown(INPUT_CLICK_RIGHT) || (input->IsKeyDown(INPUT_CLICK_MIDDLE) && f_shift)){
            //Grab the ground: it follows the mouse, so the pivot goes the other way.
            float k = cam_distance * CHASM_CAM_DRAG_PAN;
            camera_target = camera_target - right * ((float)dx * k) + forward * ((float)dy * k);
        }else if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
            cam_yaw -= (float)dx * CHASM_CAM_ORBIT_RATE;
            cam_pitch += (float)dy * CHASM_CAM_ORBIT_RATE;
        }
        if (cam_wheel != 0.0f){
            cam_distance *= powf(CHASM_CAM_WHEEL_STEP,cam_wheel);
        }
    }
    cam_wheel = 0.0f;

    if (input->IsInputLive()){
        float pan = cam_distance * CHASM_CAM_KEY_PAN * dt;
        if (input->IsKeyDown(INPUT_MOVE_UP) || input->IsKeyDown(INPUT_TURN_UP)) camera_target += forward * pan;
        if (input->IsKeyDown(INPUT_MOVE_DOWN) || input->IsKeyDown(INPUT_TURN_DOWN)) camera_target -= forward * pan;
        if (input->IsKeyDown(INPUT_MOVE_LEFT) || input->IsKeyDown(INPUT_TURN_LEFT)) camera_target -= right * pan;
        if (input->IsKeyDown(INPUT_MOVE_RIGHT) || input->IsKeyDown(INPUT_TURN_RIGHT)) camera_target += right * pan;
        if (input->IsKeyDown(INPUT_CHASM_ROTATE_LEFT)) cam_yaw += CHASM_CAM_KEY_TURN * dt;
        if (input->IsKeyDown(INPUT_CHASM_ROTATE_RIGHT)) cam_yaw -= CHASM_CAM_KEY_TURN * dt;
    }

    //Pulled back as far as the whole map and a little more - step 1's camera is for inspecting
    //the grid, not the game's zoom range (README.md).
    float dist_max = 4000.0f;
    std::shared_ptr<const Grid> g = GetGrid();
    if (g){
        vec2 size = g->bounds_max - g->bounds_min;
        dist_max = std::max(size.x,size.y) * 1.5f;
    }
    cam_pitch = std::max(CHASM_CAM_PITCH_MIN,std::min(CHASM_CAM_PITCH_MAX,cam_pitch));
    cam_distance = std::max(CHASM_CAM_DIST_MIN,std::min(dist_max,cam_distance));
    camera_target.y = 0.0f;
    ApplyCamera();
}

void ApplicationChasm::UpdateView(void){
    InputController* input = main_scene->inputcontroller;
    UpdateCamera();
    //After the camera, so the ray goes through the view this pass will draw.
    UpdatePick();
    if (input->WasKeyPressed(INPUT_CHASM_FRAME) && input->IsInputLive()){
        FrameMap();
    }
    /*
        N regenerates with the next seed. Done right here, on the physics thread with the pass's
        lock held, which stalls the window for the length of one generation - fine for a debug key,
        and it keeps the grid swap on one well-known thread.
    */
    if (input->WasKeyPressed(INPUT_CHASM_NEXT_SEED) && input->IsInputLive()){
        std::shared_ptr<const Grid> g = GetGrid();
        GridSettings s = g ? g->settings : next_settings;
        s.seed++;
        RegenerateGrid(s);
    }
}

//--- The frame ----------------------------------------------------------------------------------

//RENDER THREAD, before the scene draws: the only place GL may be touched for the meshes below.
void ApplicationChasm::PreRender(void){
    std::shared_ptr<const Grid> g = GetGrid();
    if (g && g != ground_fitted_to){
        FitGround(*g);
        ground_fitted_to = g;
    }
    UpdatePickView();
#ifdef DEBUG
    UpdateGridView();
#endif
}

#ifdef DEBUG
namespace {

uint32_t LerpColor(uint32_t a, uint32_t b, float t){
    t = std::max(0.0f,std::min(1.0f,t));
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8){
        float ca = (float)((a >> shift) & 0xFF);
        float cb = (float)((b >> shift) & 0xFF);
        out |= ((uint32_t)(ca + (cb - ca) * t) & 0xFF) << shift;
    }
    return out;
}

uint64_t EdgeKey(int a, int b){
    if (a > b){
        std::swap(a,b);
    }
    return ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
}

}

//Line colours, 0xAARRGGBB.
#define VIEW_FINE           0xFF4A5560u
#define VIEW_COARSE         0xFFC8D0D8u
#define VIEW_VALENCE_3      0xFF30B8FFu     //blue: three quads meet
#define VIEW_VALENCE_5      0xFFFFA830u     //orange: five
#define VIEW_VALENCE_6      0xFFFF3838u     //red: six
#define VIEW_VALENCE_ODD    0xFFFF40FFu     //magenta: anything else, which the check fails
#define VIEW_SQUARE_GOOD    0xFF40D060u
#define VIEW_SQUARE_BAD     0xFFFF3030u
#define VIEW_ISSUE_FAIL     0xFFFF2020u
#define VIEW_ISSUE_LOOK     0xFFFFE040u
#define VIEW_ISSUE_SELECTED 0xFFFFFFFFu

/*
    RENDER THREAD. Rebuilds the line mesh when the grid, the report or a layer changed, and never
    otherwise. Everything is drawn in the grid's own units, flat at y = 0 (the object sits a hair
    above the ground).
*/
void ApplicationChasm::UpdateGridView(){
    std::shared_ptr<const Grid> g = GetGrid();
    std::shared_ptr<const GridCheckReport> report = GetCheckReport();
    int version = view_version.load();
    if (!g || (g == grid_view_built_for && version == grid_view_built_version)){
        return;
    }
    grid_view_built_for = g;
    grid_view_built_version = version;
    auto t0 = std::chrono::steady_clock::now();

    std::vector<line_vertex> verts;
    auto line = [&verts](const vec2& a, const vec2& b, uint32_t color, float ya = 0.0f, float yb = 0.0f){
        line_vertex v;
        v.color = color;
        v.pos = vec3(a.x,ya,a.y);
        verts.push_back(v);
        v.pos = vec3(b.x,yb,b.y);
        verts.push_back(v);
    };
    const float unit = g->settings.triangle_side;

    if (f_view_fine || f_view_coarse){
        //A fine quad's edges 0 and 3 lie on its parent's outline (Subdivide in Grid.cpp), so those
        //are the coarse edges - drawn as the fine segments they are, bends and all.
        std::unordered_set<uint64_t> coarse_edges;
        if (f_view_coarse){
            coarse_edges.reserve(g->fine.quads.size() * 2);
            for (const GridQuad& q : g->fine.quads){
                coarse_edges.insert(EdgeKey(q.v[0],q.v[1]));
                coarse_edges.insert(EdgeKey(q.v[3],q.v[0]));
            }
        }
        verts.reserve(g->fine.edges.size() * 2);
        for (const auto& e : g->fine.edges){
            bool f_coarse = f_view_coarse && coarse_edges.count(EdgeKey(e.first,e.second));
            if (f_coarse){
                line(g->fine.pos[e.first],g->fine.pos[e.second],VIEW_COARSE);
            }else if (f_view_fine){
                line(g->fine.pos[e.first],g->fine.pos[e.second],VIEW_FINE);
            }
        }
    }

    if (f_view_valence){
        //A star at every vertex where other than four quads meet - the irregular ones, which are
        //what makes the grid look the way it does. Four is the plain case and is left unmarked.
        float r = unit * 0.07f;
        for (size_t i = 0; i < g->fine.pos.size(); i++){
            int v = g->fine.valence[i];
            if (g->fine.f_boundary[i] || v == 4){
                continue;
            }
            uint32_t c = (v == 3) ? VIEW_VALENCE_3 : (v == 5) ? VIEW_VALENCE_5 : (v == 6) ? VIEW_VALENCE_6 : VIEW_VALENCE_ODD;
            const vec2& p = g->fine.pos[i];
            line(p - vec2(r,0),p + vec2(r,0),c);
            line(p - vec2(0,r),p + vec2(0,r),c);
            line(p - vec2(r,r) * 0.7f,p + vec2(r,r) * 0.7f,c);
            line(p - vec2(r,-r) * 0.7f,p + vec2(r,-r) * 0.7f,c);
        }
    }

    if (f_view_squareness){
        //Each fine quad's outline drawn again inset toward its centre, coloured from square
        //(green) to badly skewed (red). Inset so neighbours' colours do not land on one edge.
        for (int qi = 0; qi < (int)g->fine.quads.size(); qi++){
            const GridQuad& q = g->fine.quads[qi];
            vec2 p[4];
            for (int k = 0; k < 4; k++){
                p[k] = g->fine.pos[q.v[k]];
            }
            vec2 c = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
            //0.5 squareness and below is full red: below that the quad is plainly a diamond.
            float t = (1.0f - GridQuadSquareness(p)) * 2.0f;
            uint32_t col = LerpColor(VIEW_SQUARE_GOOD,VIEW_SQUARE_BAD,t);
            vec2 in[4];
            for (int k = 0; k < 4; k++){
                in[k] = c + (p[k] - c) * 0.75f;
            }
            for (int k = 0; k < 4; k++){
                line(in[k],in[(k + 1) % 4],col);
            }
        }
    }

    //The last check's issues, if it was run on THIS grid: a ring on the spot and a tall post, so a
    //failure is findable from a camera pulled back over the whole map.
    if (f_view_issues && report && report->hash == g->Hash()){
        int sel = selected_issue.load();
        for (int i = 0; i < (int)report->issues.size(); i++){
            const GridIssue& issue = report->issues[i];
            bool f_sel = (i == sel);
            uint32_t c = f_sel ? VIEW_ISSUE_SELECTED : (issue.f_failure ? VIEW_ISSUE_FAIL : VIEW_ISSUE_LOOK);
            float r = unit * (f_sel ? 0.6f : 0.35f);
            const int SEGMENTS = 12;
            for (int s = 0; s < SEGMENTS; s++){
                float a0 = 6.2831853f * s / SEGMENTS;
                float a1 = 6.2831853f * (s + 1) / SEGMENTS;
                line(issue.where + vec2(cosf(a0),sinf(a0)) * r,issue.where + vec2(cosf(a1),sinf(a1)) * r,c);
            }
            line(issue.where,issue.where,c,0.0f,unit * (issue.f_failure ? 6.0f : 2.5f));
        }
    }

    grid_view_vertices = (int)verts.size();
    if (verts.empty()){
        grid_view->SetVisibility(false);
    }else{
        grid_view_mesh->SetLineMeshData(verts.data(),(int)verts.size());
        grid_view->SetVisibility(true);
    }
    grid_view_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

//--- The checks --------------------------------------------------------------------------------

//Any thread but the render thread's frame: costs about one more generation.
std::shared_ptr<const GridCheckReport> ApplicationChasm::RunChecks(){
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        return NULL;
    }
    std::shared_ptr<GridCheckReport> r = std::make_shared<GridCheckReport>(RunGridChecks(*g));
    debug->Info("Grid check: %s in %.1f ms\n",r->f_pass ? "PASS" : "FAIL",r->check_ms);
    for (const GridCheckResult& c : r->results){
        debug->Info("  %-14s %s  %s\n",c.name.c_str(),c.f_skipped ? "skip" : (c.f_pass ? "pass" : "FAIL"),c.detail.c_str());
    }
    std::lock_guard<std::mutex> lock(grid_mutex);
    //Only if the grid is still the one checked - a regeneration meanwhile makes the report stale.
    if (grid == g){
        check_report = r;
        selected_issue = -1;
        view_version++;
    }
    return r;
}

std::shared_ptr<const GridCheckReport> ApplicationChasm::GetCheckReport(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return check_report;
}

json ApplicationChasm::CheckReportJson(const GridCheckReport& r, int max_issues){
    json results = json::array();
    for (const GridCheckResult& c : r.results){
        results.push_back(json{{"name",c.name},{"result",c.f_skipped ? "skip" : (c.f_pass ? "pass" : "fail")},{"detail",c.detail}});
    }
    json issues = json::array();
    for (int i = 0; i < (int)r.issues.size() && i < max_issues; i++){
        const GridIssue& is = r.issues[i];
        issues.push_back(json{{"n",i},{"kind",is.kind},{"index",is.index},{"x",is.where.x},{"z",is.where.y},
                              {"failure",is.f_failure}});
    }
    char hash[32];
    snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)r.hash);
    return json{
        {"pass",r.f_pass},
        {"hash",hash},
        {"results",results},
        {"squareness_mean",r.squareness_mean},
        {"squareness_min",r.squareness_min},
        {"valence",{{"3",r.valence_hist[3]},{"4",r.valence_hist[4]},{"5",r.valence_hist[5]},{"6",r.valence_hist[6]},
                     {"other",r.valence_hist[0] + r.valence_hist[1] + r.valence_hist[2] + r.valence_hist[7]}}},
        {"issues_total",r.issues.size()},
        {"issues",issues},
        {"check_ms",r.check_ms}
    };
}

void ApplicationChasm::FocusIssue(int i){
    std::shared_ptr<const GridCheckReport> r = GetCheckReport();
    if (!r || i < 0 || i >= (int)r->issues.size()){
        return;
    }
    selected_issue = i;
    view_version++;
    camera_target = vec3(r->issues[i].where.x,0.0f,r->issues[i].where.y);
    cam_distance = std::min(cam_distance,40.0f);
    ApplyCamera();
}
#endif

//--- The panel ----------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationChasm::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderChasmPanel();
}

//RENDER THREAD with physics_mutex held - so the camera state may be written directly here.
void ApplicationChasm::RenderChasmPanel(){
    //A size and a place for the first run only - after that imgui.ini remembers where it was put.
    //Without it the window opens as an empty sliver over the Engine panel. Placed from the
    //window's own width, not ImGui's DisplaySize: started --minimized, the first frame's display
    //is next to nothing wide and the panel would land on the left, behind the Engine panel.
    float right = (float)std::max(main_window->width,800);
    ImGui::SetNextWindowPos(ImVec2(right - 470.0f,30.0f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f,640.0f),ImGuiCond_FirstUseEver);
    ImGui::Begin("Chasm");
    ImGui::TextDisabled("middle-drag orbits  right-drag pans  wheel zooms");
    ImGui::TextDisabled("arrows/WASD pan  Q/E turn  F frames the map  N next seed");

    std::shared_ptr<const Grid> g = GetGrid();

    if (ImGui::CollapsingHeader("Generate",ImGuiTreeNodeFlags_DefaultOpen)){
        int seed = (int)next_settings.seed;
        if (ImGui::InputInt("seed",&seed)){
            next_settings.seed = (uint32_t)std::max(0,seed);
        }
        ImGui::InputInt("fine cells",&next_settings.target_fine_cells,1000,10000);
        next_settings.target_fine_cells = std::max(100,std::min(2000000,next_settings.target_fine_cells));
        ImGui::SliderFloat("aspect",&next_settings.aspect,0.5f,3.0f);
        ImGui::SliderFloat("triangle side",&next_settings.triangle_side,2.0f,32.0f);
        ImGui::SliderInt("relax coarse",&next_settings.relax_passes_coarse,0,200);
        ImGui::SliderInt("relax fine",&next_settings.relax_passes_fine,0,200);
        ImGui::SliderFloat("relax strength",&next_settings.relax_strength,0.0f,1.0f);
        //"##run": the header above is also "Generate", and two items with one ID share their clicks.
        if (ImGui::Button("Generate##run")){
            RegenerateGrid(next_settings);
            g = GetGrid();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next seed")){
            next_settings.seed++;
            RegenerateGrid(next_settings);
            g = GetGrid();
        }
        ImGui::SameLine();
        if (ImGui::Button("Defaults")){
            uint32_t keep = next_settings.seed;
            next_settings = GridSettings();
            next_settings.seed = keep;
        }
        if (g){
            ImGui::Text("seed %u: %i coarse quads, %i fine quads, %i fine vertices",g->settings.seed,
                        (int)g->coarse.quads.size(),(int)g->fine.quads.size(),(int)g->fine.pos.size());
            ImGui::Text("%i of %i lattice triangles left unmerged",g->num_leftover_triangles,g->num_lattice_triangles);
            ImGui::Text("%.0f x %.0f units, generated in %.1f ms",g->bounds_max.x - g->bounds_min.x,
                        g->bounds_max.y - g->bounds_min.y,g->generate_ms);
            ImGui::Text("hash %016llx",(unsigned long long)g->Hash());
        }
    }

#ifdef DEBUG
    if (ImGui::CollapsingHeader("View",ImGuiTreeNodeFlags_DefaultOpen)){
        bool changed = false;
        bool b;
        b = f_view_fine;        if (ImGui::Checkbox("fine edges",&b)){ f_view_fine = b; changed = true; }
        ImGui::SameLine();
        b = f_view_coarse;      if (ImGui::Checkbox("coarse edges",&b)){ f_view_coarse = b; changed = true; }
        b = f_view_valence;     if (ImGui::Checkbox("valence",&b)){ f_view_valence = b; changed = true; }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.19f,0.72f,1.0f,1),"3");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.66f,0.19f,1),"5");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.22f,0.22f,1),"6");
        b = f_view_squareness;  if (ImGui::Checkbox("squareness",&b)){ f_view_squareness = b; changed = true; }
        ImGui::SameLine();
        b = f_view_issues;      if (ImGui::Checkbox("check issues",&b)){ f_view_issues = b; changed = true; }
        if (changed){
            view_version++;
        }
        ImGui::TextDisabled("%i line vertices, built in %.1f ms",grid_view_vertices,grid_view_ms);
    }

    if (ImGui::CollapsingHeader("Check",ImGuiTreeNodeFlags_DefaultOpen)){
        if (ImGui::Button("Run checks")){
            RunChecks();
        }
        std::shared_ptr<const GridCheckReport> r = GetCheckReport();
        if (!r){
            ImGui::TextDisabled("not run on this grid");
        }else{
            ImGui::SameLine();
            ImGui::TextColored(r->f_pass ? ImVec4(0.3f,0.9f,0.4f,1) : ImVec4(1,0.3f,0.3f,1),
                               r->f_pass ? "PASS" : "FAIL");
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f ms",r->check_ms);
            for (const GridCheckResult& c : r->results){
                ImVec4 col = c.f_skipped ? ImVec4(0.6f,0.6f,0.6f,1) : (c.f_pass ? ImVec4(0.3f,0.9f,0.4f,1) : ImVec4(1,0.3f,0.3f,1));
                ImGui::TextColored(col,"%-13s",c.name.c_str());
                ImGui::SameLine();
                ImGui::TextWrapped("%s",c.detail.c_str());
            }
            if (!r->issues.empty() && ImGui::TreeNode("issues","%i marked - click one to go there",(int)r->issues.size())){
                int sel = selected_issue.load();
                for (int i = 0; i < (int)r->issues.size(); i++){
                    const GridIssue& is = r->issues[i];
                    char label[96];
                    snprintf(label,sizeof(label),"%s %s %i  (%.0f, %.0f)",is.f_failure ? "FAIL" : "look",
                             is.kind.c_str(),is.index,is.where.x,is.where.y);
                    //By position in the list: one quad can be listed twice (folded, and least square).
                    ImGui::PushID(i);
                    if (ImGui::Selectable(label,i == sel)){
                        FocusIssue(i);
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
#else
    ImGui::TextDisabled("The grid view and the checks are in debug builds only.");
#endif

    if (ImGui::CollapsingHeader("Pick",ImGuiTreeNodeFlags_DefaultOpen)){
        std::shared_ptr<const GridPicker> p = GetPicker();
        auto show = [&](const char* label, const GridPick& pick){
            if (!p || !pick.f_hit){
                ImGui::TextDisabled("%s: nothing",label);
                return;
            }
            const Grid& pg = p->GetGrid();
            vec2 corners[4];
            for (int k = 0; k < 4; k++){
                corners[k] = pg.fine.pos[pg.fine.quads[pick.fine_quad].v[k]];
            }
            ImGui::Text("%s: plot %i, %i sides, %.1f units%s",label,pick.plot,p->PlotQuadCount(pick.plot),
                        p->PlotArea(pick.plot),pg.fine.f_boundary[pick.plot] ? " (outline)" : "");
            ImGui::Text("    fine cell %i, %.1f units, square %.2f",pick.fine_quad,p->FineArea(pick.fine_quad),
                        GridQuadSquareness(corners));
            ImGui::Text("    coarse cell %i, %.1f units",pick.coarse_quad,p->CoarseArea(pick.coarse_quad));
        };
        show("hover",GetHoverPick());
        show("selected",GetSelectedPick());
        ImGui::TextDisabled("left-click selects a plot; clicking off the map clears it");
    }

    if (ImGui::CollapsingHeader("Camera")){
        ImGui::Text("target %.1f, %.1f  distance %.1f",camera_target.x,camera_target.z,cam_distance);
        ImGui::Text("yaw %.0f  pitch %.0f degrees",cam_yaw * 57.29578f,cam_pitch * 57.29578f);
        if (ImGui::Button("Frame map")){
            FrameMap();
        }
    }
    ImGui::End();
}
#endif

//--- MCP ----------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterMCPTools(){
    MCPServer::Get()->RegisterTool("chasm_grid",
        "The current grid: its settings, vertex/quad/edge counts at both levels (coarse and fine), "
        "how many lattice triangles were left unmerged, its extent in world units (x and z; the grid "
        "is flat), generation time and its hash. Equal hashes mean the same grid bit for bit.",
        json{
            {"type","object"},
            {"properties",{
                {"include_screenshot",{{"type","boolean"},{"description","return a screenshot of the current frame"}}},
                {"include_ui",{{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const Grid> g = GetGrid();
            json result = g ? GridStatsJson(*g) : json{{"error","no grid"}};
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_generate",
        "Generate a new grid and make it current. Every argument is optional and defaults to the "
        "current grid's value, so {\"seed\":7} is the same map shape with another seed. Returns what "
        "chasm_grid returns. Blocks for the length of one generation (tens to hundreds of ms).",
        json{
            {"type","object"},
            {"properties",{
                {"seed",{{"type","integer"}}},
                {"target_fine_cells",{{"type","integer"},{"description","fine cells to aim for; the count comes out close, not exact"}}},
                {"aspect",{{"type","number"},{"description","map width (x) over depth (z)"}}},
                {"triangle_side",{{"type","number"},{"description","lattice triangle side in world units; a fine cell is about a quarter of it"}}},
                {"relax_passes_coarse",{{"type","integer"}}},
                {"relax_passes_fine",{{"type","integer"}}},
                {"relax_strength",{{"type","number"},{"description","0..1, how far toward its best-fit square a vertex moves per pass"}}},
                {"defaults",{{"type","boolean"},{"description","start from the default settings instead of the current grid's"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const Grid> g = GetGrid();
            GridSettings s = (g && !args.value("defaults",false)) ? g->settings : GridSettings();
            s.seed = args.value("seed",s.seed);
            s.target_fine_cells = std::max(100,std::min(2000000,args.value("target_fine_cells",s.target_fine_cells)));
            s.aspect = args.value("aspect",s.aspect);
            s.triangle_side = args.value("triangle_side",s.triangle_side);
            s.relax_passes_coarse = args.value("relax_passes_coarse",s.relax_passes_coarse);
            s.relax_passes_fine = args.value("relax_passes_fine",s.relax_passes_fine);
            s.relax_strength = args.value("relax_strength",s.relax_strength);
            RegenerateGrid(s);
            return GridStatsJson(*GetGrid());
        });

    MCPServer::Get()->RegisterTool("chasm_view",
        "Move the camera and, in a debug build, switch the grid view's layers. The camera orbits a "
        "point on the ground: target_x/target_z, distance, yaw and pitch in degrees (pitch above the "
        "horizon; yaw 0 looks toward -z). frame_map puts the whole map in view; focus_issue sends "
        "the camera to issue n of the last chasm_check and highlights it. Layers: fine, coarse, "
        "valence (stars where 3, 5 or 6 quads meet), squareness (each quad inset, green square to red "
        "skewed), issues (the last check's marks). Returns the camera and layers; screenshot optional.",
        json{
            {"type","object"},
            {"properties",{
                {"target_x",{{"type","number"}}},
                {"target_z",{{"type","number"}}},
                {"distance",{{"type","number"}}},
                {"yaw",{{"type","number"}}},
                {"pitch",{{"type","number"}}},
                {"frame_map",{{"type","boolean"}}},
                {"focus_issue",{{"type","integer"}}},
                {"fine",{{"type","boolean"}}},
                {"coarse",{{"type","boolean"}}},
                {"valence",{{"type","boolean"}}},
                {"squareness",{{"type","boolean"}}},
                {"issues",{{"type","boolean"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
#ifdef DEBUG
            bool changed = false;
            auto layer = [&](const char* name, std::atomic<bool>& flag){
                if (args.contains(name)){
                    flag = args[name].get<bool>();
                    changed = true;
                }
            };
            layer("fine",f_view_fine);
            layer("coarse",f_view_coarse);
            layer("valence",f_view_valence);
            layer("squareness",f_view_squareness);
            layer("issues",f_view_issues);
            if (changed){
                view_version++;
            }
#endif
            json result;
            main_scene->AtTickBoundary([&](){
                if (args.value("frame_map",false)){
                    FrameMap();
                }
#ifdef DEBUG
                if (args.contains("focus_issue")){
                    FocusIssue(args["focus_issue"].get<int>());
                }
#endif
                camera_target.x = args.value("target_x",camera_target.x);
                camera_target.z = args.value("target_z",camera_target.z);
                cam_distance = args.value("distance",cam_distance);
                cam_yaw = args.value("yaw",cam_yaw * 57.29578f) / 57.29578f;
                cam_pitch = std::max(CHASM_CAM_PITCH_MIN,std::min(CHASM_CAM_PITCH_MAX,
                                     args.value("pitch",cam_pitch * 57.29578f) / 57.29578f));
                ApplyCamera();
                result["camera"] = json{{"target_x",camera_target.x},{"target_z",camera_target.z},
                                        {"distance",cam_distance},{"yaw",cam_yaw * 57.29578f},{"pitch",cam_pitch * 57.29578f}};
            });
#ifdef DEBUG
            result["layers"] = json{{"fine",f_view_fine.load()},{"coarse",f_view_coarse.load()},
                                    {"valence",f_view_valence.load()},{"squareness",f_view_squareness.load()},
                                    {"issues",f_view_issues.load()}};
#endif
            //The screenshot is of a frame drawn after this returns, and PreRender rebuilds the
            //lines at the top of that frame - so new layers are already in the picture.
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_pick",
        "What is on the ground at a point: the plot (a fine vertex - what the player clicks and will "
        "paint; its number of sides and area), the fine cell (quad) holding the point and its "
        "squareness, and the coarse cell that is its parent. Give the point either as world "
        "x/z, or as a window pixel px/py, which goes through the camera exactly as the mouse does. "
        "select makes it the selection (drawn filled), as a left click would; select with a point "
        "off the map clears it. With no point, returns the current hover and selection.",
        json{
            {"type","object"},
            {"properties",{
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"px",{{"type","integer"},{"description","window pixel, from the left"}}},
                {"py",{{"type","integer"},{"description","window pixel, from the top"}}},
                {"select",{{"type","boolean"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const GridPicker> p = GetPicker();
            if (!p){
                return json{{"error","no grid"}};
            }
            json result;
            bool f_point = false;
            vec2 at;
            if (args.contains("px") && args.contains("py")){
                int2 px;
                px.x = args["px"].get<int>();
                px.y = args["py"].get<int>();
                bool f_ground = false;
                //The camera is the physics thread's; read it between ticks.
                main_scene->AtTickBoundary([&](){
                    f_ground = GroundUnderPixel(px,at);
                });
                if (!f_ground){
                    result["error"] = "that pixel's ray does not meet the ground";
                }
                f_point = f_ground;
            }else if (args.contains("x") && args.contains("z")){
                at = vec2(args["x"].get<float>(),args["z"].get<float>());
                f_point = true;
            }
            if (f_point){
                GridPick pick = p->Pick(at);
                result["pick"] = PickJson(*p,pick);
                if (args.value("select",false)){
                    SetSelectedPick(pick);
                }
            }
            result["hover"] = PickJson(*p,GetHoverPick());
            result["selected"] = PickJson(*p,GetSelectedPick());
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

#ifdef DEBUG
    MCPServer::Get()->RegisterTool("chasm_check",
        "Run the grid's checks on the current grid - the same command as the panel's Run checks "
        "button, debug builds only. deterministic (regenerated from the same settings gives the same "
        "hash), pinned (the hash matches the one pinned for this seed at default settings, or says "
        "how to pin it), edges (none shared by more than two quads), valence (every interior vertex "
        "meets 3-6 edges), folded (every fine quad convex), squareness (a figure: mean and worst). "
        "Issues carry a position and are marked in the view; chasm_view focus_issue goes to one.",
        json{
            {"type","object"},
            {"properties",{
                {"max_issues",{{"type","integer"},{"description","issues to list (default 20); all are marked in the view"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const GridCheckReport> r = RunChecks();
            if (!r){
                return json{{"error","no grid"}};
            }
            return CheckReportJson(*r,args.value("max_issues",20));
        });
#else
    //Registered anyway, to say why: an unknown tool is a JSON-RPC error with no `result`, and a
    //script written against the debug build reads that as a hang or a crash rather than an answer.
    MCPServer::Get()->RegisterTool("chasm_check",
        "Not in this build: the grid's checks are compiled into debug builds only (docs/README.md, "
        "\"Checks live in the app\"). Run the debug exe to use them.",
        json{{"type","object"},{"properties",json::object()}},
        [](const json& args) -> json {
            (void)args;
            return json{{"error","chasm_check is debug-only; this is a release build"}};
        });
#endif
}
#endif
