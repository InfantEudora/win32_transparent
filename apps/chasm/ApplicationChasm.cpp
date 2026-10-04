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
#include "Texture.h"
#include "Palette.h"

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
#define CHASM_GRID_VIEW_Y       0.40f       //over the ground's relief (TerrainMesh.cpp, GROUND_BUMP)
#define PICK_VIEW_Y             0.45f       //over the grid view's lines
#define SUN_DISTANCE            400.0f      //how far out the sun's shadow camera sits from the view
#define SUN_EXTENT_PER_DISTANCE 0.9f        //shadow half-extent per unit of camera distance

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
    //No skybox: the background is a flat haze colour (Renderer::background_color, set from the
    //palette in BuildScene), which is what the reference's soft look wants behind the map.
    renderer->f_render_skybox = false;
    /*
        Frustum culling on (Renderer.h calls it a measuring prototype, off by default). It suits this
        app: the terrain is 144 large chunks with bounds, and at the game's zoom about a third are in
        view. Measured 2026-10-04 at that zoom: G-buffer 1.11 -> 0.49 ms, colour 1.88 -> 1.29 ms,
        46 of 144 chunks drawn. The shadow pass is never culled, by design.
    */
    renderer->f_frustum_cull = true;
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
    //Init is the render thread before the physics thread exists, so the camera may be set here.
    FrameMap();

    SetupInput();
    RegisterCommandHandlers();
    //Init is the render thread before the physics thread exists, so the zones may be set up here.
    EnsureZonesWorld();
#ifdef USE_MCP
    RegisterMCPTools();
#endif
}

void ApplicationChasm::BuildScene(){
    /*
        THE LIGHT: one sun and the sky (step 4, matched against the A Little Age screenshot).

        The sun stands high in the north-west, so shadows fall short and down-right on screen, as in
        the reference. It casts the shadows and follows the view (FollowSun): one shadow map over
        the whole 768-unit map would be a few texels per house. Its depth range has to reach from
        the sun, SUN_DISTANCE out, past the chasm floor and the skirt - DirectionalLight's own
        default far plane is 100, which would cut the shadow off at the plateau.

        The sky is the renderer's ambient hemisphere, not a second light: a cool, bright sky above a
        dim warm ground is what keeps a shadow blue and readable rather than black, and lights a
        wall facing away from the sun by the sky it does face.
    */
    vec3 sun_dir(-0.50f,0.85f,-0.35f);
    sun_dir.normalize();
    sun_offset = sun_dir * SUN_DISTANCE;

    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->color = vec3(1.0f,0.95f,0.86f);
    //Soft light, as the reference: lit ground about 1.05x its palette colour and shade about 0.7x,
    //so most of it is the sky's and the sun only adds the rest. Measured against
    //art_source/chasm/alittleagedemo.jpg, not judged by eye.
    sun->brightness = 1.7f;
    sun->SetupOrthographic(4096,4096,60.0f,1.0f,SUN_DISTANCE * 2.0f);
    sun->SetPosition(sun_offset);
    sun->SetLookAt(vec3());
    main_scene->AddObject(sun);

    renderer->ambient_sky = vec3(0.64f,0.69f,0.80f);
    //The ground's bounce, warm and fairly bright: a wall facing sideways takes half of this, and at
    //0.30 white plaster read as grey. Flat ground faces the sky and does not see it.
    renderer->ambient_ground = vec3(0.46f,0.42f,0.34f);

    //Everything is drawn with the palette (Palette.h): white, so the texture IS the colour, and
    //fully rough, so no surface throws a specular sheen the reference does not have.
    Texture* palette = renderer->LoadTexture("textures/palette.png");
    Material mat = {};
    mat.name = "chasm_palette";
    mat.glsl_material.color = vec4(1.0f,1.0f,1.0f,1.0f);
    mat.glsl_material.metallic = 0.0f;
    mat.glsl_material.roughness = 1.0f;
    if (palette && !palette->IsEmpty()){
        mat.glsl_material.diffuse_texture = 0;
        mat.glsl_material.handle_diffuse = palette->texture_handle;
        mat.diff_texture = palette;
        //The background is the palette's haze cell, so it moves with the palette when it is edited.
        vec2 uv = PaletteUV(PAL_HAZE,PAL_TEMPERATE);
        vec3 haze = palette->GetValueAt(uv.x,uv.y) / 255.0f;
        renderer->background_color = vec4(haze.x,haze.y,haze.z,1.0f);
    }else{
        debug->Err("No palette (textures/palette.png) - the terrain draws white\n");
    }
    renderer->AddMaterial(mat);
    palette_material = renderer->FindMaterialIndex(mat.name);

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
    input->AddKeyMap('1',INPUT_CHASM_TOOL_HOUSE);
    input->AddKeyMap('2',INPUT_CHASM_TOOL_FIELD);
    input->AddKeyMap('3',INPUT_CHASM_TOOL_ERASE);
#endif
}

//--- The grid -----------------------------------------------------------------------------------

void ApplicationChasm::RegenerateGrid(const GridSettings& s){
    std::shared_ptr<ChasmWorld> w = std::make_shared<ChasmWorld>();
    std::shared_ptr<Grid> g = std::make_shared<Grid>();
    g->Generate(s);
    w->grid = g;
    auto t0 = std::chrono::steady_clock::now();
    w->picker = std::make_shared<GridPicker>(g);
    float pick_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    w->features = TerrainFeatureLines(*g);
    std::shared_ptr<Terrain> t = std::make_shared<Terrain>();
    t->Build(*g,w->features);
    w->terrain = t;
    std::shared_ptr<TerrainMeshData> mesh = std::make_shared<TerrainMeshData>();
    BuildTerrainMesh(*g,*t,*mesh);
    w->mesh = mesh;
    debug->Info("Grid: seed %u, %i coarse and %i fine quads, %i leftover triangles of %i, %.1f ms "
                "(+%.1f ms picker, %.1f ms levels, %.1f ms mesh: %i triangles, %i in walls, %i chunks)\n",
                s.seed,(int)g->coarse.quads.size(),(int)g->fine.quads.size(),
                g->num_leftover_triangles,g->num_lattice_triangles,g->generate_ms,pick_ms,
                t->build_ms,mesh->build_ms,mesh->num_triangles,mesh->num_wall_triangles,(int)mesh->chunks.size());
    {
        std::lock_guard<std::mutex> lock(grid_mutex);
        world = w;
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

std::shared_ptr<const ChasmWorld> ApplicationChasm::GetWorld(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return world;
}

std::shared_ptr<const Grid> ApplicationChasm::GetGrid(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    return w ? w->grid : NULL;
}

std::shared_ptr<const GridPicker> ApplicationChasm::GetPicker(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    return w ? w->picker : NULL;
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

/*
    Physics thread or at a tick boundary: it reads the camera.

    The terrain is a few flat levels, so the ray is tried against each level's plane from the top
    down, and the first plane whose point lands on a plot OF THAT LEVEL is what the ray hits. A
    point on the plateau's plane over the chasm lands on a floor plot and is passed over; the ray
    goes on down to the floor. Walls are never hit - nothing is built on a wall.
*/
GridPick ApplicationChasm::PickUnderPixel(int2 px){
    GridPick none;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w){
        return none;
    }
    ray r = main_scene->camera->GetPixelRay(px);
    //Looking up or level never meets the ground in front of the camera.
    if (r.direction.y >= -1e-4f){
        return none;
    }
    for (int l = 0; l < TERRAIN_NUM_LEVELS; l++){
        plane level_plane;
        level_plane.pos = vec3(0.0f,terrain_levels[l].height,0.0f);
        level_plane.normal = vec3(0.0f,1.0f,0.0f);
        vec3 at;
        if (!r.intersects_plane(level_plane,at)){
            continue;
        }
        GridPick pick = w->picker->Pick(vec2(at.x,at.z));
        if (pick.f_hit && w->terrain->level[pick.plot] == l){
            return pick;
        }
        none.at = vec2(at.x,at.z);
    }
    return none;
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
        if (f_over_scene){
            hover = PickUnderPixel(px);
        }
    }
    //A paint tool takes the left button; selecting is what the button does with no tool.
    bool f_painting = (paint_tool != CHASM_TOOL_SELECT);
    if (f_painting){
        UpdatePaint(hover,f_over_scene,f_clicked);
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (hover != hover_pick){
        hover_pick = hover;
        pick_version++;
    }else{
        hover_pick.at = hover.at;   //same plot, new point - not worth a rebuild
    }
    if (!f_painting && f_clicked && f_over_scene && selected_pick != hover){
        //A click on nothing - off the map - clears the selection, which is also the way to clear it.
        selected_pick = hover;
        pick_version++;
    }
}

//--- Zones (step 5) ------------------------------------------------------------------------------

std::shared_ptr<const ZoneState> ApplicationChasm::GetZones(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return zone_snapshot;
}

//PHYSICS THREAD. Zones belong to one map; when the map has been regenerated they start empty.
void ApplicationChasm::EnsureZonesWorld(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (zones.GetWorld() != w){
        zones.Reset(w);
        PublishZones();
    }
}

//PHYSICS THREAD, after every change: a copy for everyone else (see `zones` in the header).
void ApplicationChasm::PublishZones(){
    std::shared_ptr<const ZoneState> copy = std::make_shared<ZoneState>(zones.State());
    {
        std::lock_guard<std::mutex> lock(grid_mutex);
        zone_snapshot = copy;
    }
    //The hover's red-or-not depends on the zones, so the cursor is redrawn with them.
    pick_version++;
}

void ApplicationChasm::SubmitZone(int op, int index){
    if (index < 0){
        return;
    }
    SimCommand cmd;
    cmd.type = CHASM_CMD_ZONE;
    cmd.subtype = (uint32_t)index;
    cmd.value[0] = (float)op;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::RegisterCommandHandlers(){
    /*
        The ONLY way the zones change. The op and the index travel in the command, so a recorded
        run repaints exactly the same plots on replay (step 6) - which is why the mouse is never
        read here: what was under the cursor is decided when the command is made, by the view.
    */
    main_scene->RegisterCommandHandler(CHASM_CMD_ZONE,
        [this](const SimCommand& cmd) -> objectid_t {
            EnsureZonesWorld();
            bool f_changed = zones.Apply((int)cmd.value[0],cmd.subtype);
            {
                std::lock_guard<std::mutex> lock(grid_mutex);
                zone_last_refusal = zones.last_refusal;
            }
            if (f_changed){
                PublishZones();
            }
            zone_commands_done++;
            return OBJECTID_INVALID;
        });
}

void ApplicationChasm::RunSimulationTick(void){
    EnsureZonesWorld();
}

/*
    PHYSICS THREAD, from UpdatePick, with a paint tool in hand: the mouse made into zone commands.

    A press paints what is under it - and on a house adds a storey; with shift it takes one off
    instead. Held and dragged, it paints each NEW plot or cell the cursor reaches, once: a drag
    never stacks storeys, so sweeping across a street lays one floor and clicking raises it.
    Erase takes the house off a plot if there is one, otherwise the field off its cell.
*/
void ApplicationChasm::UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked){
    InputController* input = main_scene->inputcontroller;
    bool f_held = input->IsKeyDown(INPUT_CLICK_LEFT);
    if (!f_held){
        paint_last_index = -1;
    }
    if (!f_over_scene || !hover.f_hit || !(f_clicked || f_held)){
        return;
    }
    bool f_shift = input->IsKeyDown(INPUT_SHIFT);
    int tool = paint_tool;
    int index = (tool == CHASM_TOOL_FIELD) ? hover.coarse_quad : hover.plot;
    if (!f_clicked && index == paint_last_index){
        return;     //still on what the drag already painted
    }
    paint_last_index = index;

    int op = ZONE_OP_NONE;
    if (tool == CHASM_TOOL_HOUSE){
        if (f_shift){
            op = f_clicked ? ZONE_OP_HOUSE_REMOVE : ZONE_OP_HOUSE_ERASE;
        }else{
            op = f_clicked ? ZONE_OP_HOUSE_ADD : ZONE_OP_HOUSE_PAINT;
        }
    }else if (tool == CHASM_TOOL_FIELD){
        op = f_shift ? ZONE_OP_FIELD_ERASE : ZONE_OP_FIELD_PAINT;
    }else if (tool == CHASM_TOOL_ERASE){
        std::shared_ptr<const ZoneState> z = GetZones();
        if (z && !z->storeys.empty() && z->storeys[hover.plot]){
            op = ZONE_OP_HOUSE_ERASE;
        }else if (z && !z->field.empty() && z->field[hover.coarse_quad]){
            op = ZONE_OP_FIELD_ERASE;
            index = hover.coarse_quad;
        }
    }
    if (op != ZONE_OP_NONE){
        SubmitZone(op,index);
    }
}

/*
    RENDER THREAD. Rebuilds the zone mesh of every chunk whose zones moved since it was last built -
    a click rebuilds one or two chunks, not the map. A new map hides them all until something is
    painted on it.
*/
void ApplicationChasm::UploadZones(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w){
        return;
    }
    size_t n = w->mesh->chunks.size();
    if (zone_chunks.size() < n){
        main_scene->AtTickBoundary([&](){
            while (zone_chunks.size() < n){
                Object* chunk = new Object();
                chunk->name = "Zones " + std::to_string(zone_chunks.size());
                chunk->SetMesh(new Mesh());
                chunk->SetMaterialSlot(0,palette_material);
                chunk->SetPickability(false);
                chunk->SetVisualOnly(true);
                chunk->SetVisibility(false);
                main_scene->AddObject(chunk);
                zone_chunks.push_back(chunk);
            }
        });
    }
    bool f_new_world = (w != zone_built_world);
    if (f_new_world){
        zone_built_world = w;
        zone_chunk_built.assign(zone_chunks.size(),0xFFFFFFFFu);
        for (Object* o : zone_chunks){
            o->SetVisibility(false);
        }
    }
    if (!z || z->world != w){
        return;
    }
    std::vector<vertex> verts;
    for (size_t i = 0; i < n; i++){
        uint32_t v = z->chunk_version[i];
        //Built at a version (not "changed" flags), so nothing is lost if two changes land between
        //frames. A chunk never painted sits at 0 and needs no mesh.
        if (zone_chunk_built[i] == v || (v == 0 && zone_chunk_built[i] == 0xFFFFFFFFu)){
            continue;
        }
        zone_chunk_built[i] = v;
        BuildZoneChunk(*w,*z,(int)i,verts);
        if (verts.empty()){
            zone_chunks[i]->SetVisibility(false);
        }else{
            zone_chunks[i]->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            zone_chunks[i]->SetVisibility(true);
        }
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
    //The level, if the picker is the current world's - a pick is only ever about that one.
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    const char* level = (w && w->picker.get() == &p) ? terrain_levels[w->terrain->level[pick.plot]].name : "?";
    return json{
        {"hit",true},
        {"x",pick.at.x},
        {"z",pick.at.y},
        {"plot",{{"vertex",pick.plot},{"x",v.x},{"z",v.y},{"level",level},{"sides",p.PlotQuadCount(pick.plot)},
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
#define PICK_PAINT_OK           0xFF60FF60u
#define PICK_PAINT_REFUSED      0xFFFF3030u

/*
    RENDER THREAD. The hover as three outlines - plot, fine cell, coarse cell - and the selected
    plot filled in with spokes from its vertex, so the two read differently at a glance. A few
    dozen lines, rebuilt only when either pick changes.
*/
void ApplicationChasm::UpdatePickView(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    int version = pick_version.load();
    if (!w || (w == pick_view_built_for && version == pick_view_built_version)){
        return;
    }
    pick_view_built_for = w;
    pick_view_built_version = version;
    GridPick hover = GetHoverPick();
    GridPick selected = GetSelectedPick();
    const GridPicker* p = w->picker.get();
    const Grid& g = *w->grid;

    std::vector<line_vertex> verts;
    std::vector<vec2> segs;
    //Everything at the height of the plot it is about: a pick on the floor is drawn on the floor.
    float y = 0.0f;
    auto emit = [&verts,&segs,&y](uint32_t color){
        line_vertex v;
        v.color = color;
        for (const vec2& s : segs){
            v.pos = vec3(s.x,y,s.y);
            verts.push_back(v);
        }
        segs.clear();
    };

    if (selected.f_hit){
        y = w->terrain->Height(selected.plot);
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
        y = w->terrain->Height(hover.plot);
        /*
            With a paint tool, what it would paint is drawn green, or red where the rules refuse it
            (Zones.h) - so a refusal is seen before the click, not after. An existing house or field
            counts as fine: clicking it is a storey more, or nothing.
        */
        int tool = paint_tool;
        std::shared_ptr<const ZoneState> zs = GetZones();
        ZoneState empty;
        const ZoneState& z = (zs && zs->world == w) ? *zs : empty;
        uint32_t coarse_colour = PICK_HOVER_COARSE;
        uint32_t plot_colour = PICK_HOVER_PLOT;
        if (tool == CHASM_TOOL_HOUSE){
            bool f_ok = (!z.storeys.empty() && z.storeys[hover.plot]) || ZoneCanHouse(*w,z,hover.plot,NULL);
            plot_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }else if (tool == CHASM_TOOL_FIELD){
            bool f_ok = (!z.field.empty() && z.field[hover.coarse_quad]) || ZoneCanField(*w,z,hover.coarse_quad,NULL);
            coarse_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }
        p->CoarseOutline(hover.coarse_quad,segs);
        emit(coarse_colour);
        const GridQuad& q = g.fine.quads[hover.fine_quad];
        for (int k = 0; k < 4; k++){
            segs.push_back(g.fine.pos[q.v[k]]);
            segs.push_back(g.fine.pos[q.v[(k + 1) % 4]]);
        }
        emit(PICK_HOVER_CELL);
        p->PlotOutline(hover.plot,segs);
        emit(plot_colour);
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

/*
    RENDER THREAD. Hands the current world's terrain chunks to the GPU, once per world - and shows
    or hides them when the view toggle changes.

    Under the tick-boundary lock, because it adds Objects to the scene and the physics thread walks
    that list (scene_addobject_during_tick in the memory notes). The meshes are uploaded inside it
    as well, which is GL - fine, since this IS the render thread; the lock only keeps the physics
    thread out for the few milliseconds it takes.
*/
void ApplicationChasm::UploadTerrain(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    bool f_show = f_view_terrain;
    if (!w || (w->mesh == terrain_uploaded && f_show == f_terrain_shown)){
        return;
    }
    const TerrainMeshData& m = *w->mesh;
    bool f_new_mesh = (w->mesh != terrain_uploaded);
    main_scene->AtTickBoundary([&](){
        while (terrain_chunks.size() < m.chunks.size()){
            Object* chunk = new Object();
            chunk->name = "Terrain " + std::to_string(terrain_chunks.size());
            chunk->SetMesh(new Mesh());
            chunk->SetMaterialSlot(0,palette_material);
            chunk->SetPickability(false);
            chunk->SetVisualOnly(true);
            chunk->SetVisibility(false);
            main_scene->AddObject(chunk);
            terrain_chunks.push_back(chunk);
        }
        for (size_t i = 0; i < terrain_chunks.size(); i++){
            bool f_has = i < m.chunks.size() && !m.chunks[i].verts.empty();
            if (f_has && f_new_mesh){
                std::vector<vertex>& verts = const_cast<std::vector<vertex>&>(m.chunks[i].verts);
                terrain_chunks[i]->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            }
            terrain_chunks[i]->SetVisibility(f_has && f_show);
        }
    });
    terrain_uploaded = w->mesh;
    f_terrain_shown = f_show;
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

/*
    PHYSICS THREAD, after the camera: the sun's shadow map over what is in view. archer's
    FollowView, the same two parts: the ortho is sized to the camera's distance, and the light moves
    only by whole texels of its own map - a sun that slides by fractions of a texel re-rasterises
    every shadow edge a little differently each pass, and the edges crawl while the view pans.
*/
void ApplicationChasm::FollowSun(){
    if (!sun){
        return;
    }
    sun->viewport.zoom = std::max(20.0f,cam_distance * SUN_EXTENT_PER_DISTANCE);
    vec3 target(camera_target.x,0.0f,camera_target.z);
    sun->SetPosition(target + sun_offset);
    sun->SetLookAt(target);
    float texel = (2.0f * sun->viewport.zoom) / sun->viewport.width;
    vec3 left = sun->GetLeft();
    vec3 up = sun->GetUp();
    vec3 eye = sun->GetPosition();
    float l = eye.dot(left);
    float u = eye.dot(up);
    eye += left * (roundf(l / texel) * texel - l) + up * (roundf(u / texel) * texel - u);
    sun->SetPosition(eye);
}

void ApplicationChasm::UpdateView(void){
    InputController* input = main_scene->inputcontroller;
    UpdateCamera();
    FollowSun();
    //The tool keys toggle: the active tool's key again puts it down.
    if (input->IsInputLive()){
        const int keys[3] = {INPUT_CHASM_TOOL_HOUSE,INPUT_CHASM_TOOL_FIELD,INPUT_CHASM_TOOL_ERASE};
        const int tools[3] = {CHASM_TOOL_HOUSE,CHASM_TOOL_FIELD,CHASM_TOOL_ERASE};
        for (int i = 0; i < 3; i++){
            if (input->WasKeyPressed(keys[i])){
                paint_tool = (paint_tool == tools[i]) ? CHASM_TOOL_SELECT : tools[i];
                pick_version++;
            }
        }
    }
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
    UploadTerrain();
    UploadZones();
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
#define VIEW_PIN_OUTLINE    0xFFB0B0B0u     //the map's four edges and the vertices sliding on them
#define VIEW_PIN_RIM        0xFFFF60D0u
#define VIEW_PIN_SHARD      0xFF60F0F0u
#define VIEW_PIN_FIXED      0xFFFF2020u

/*
    RENDER THREAD. Rebuilds the line mesh when the world, the report or a layer changed, and never
    otherwise. Lines lie on the terrain: each vertex at its level's height, and an edge between two
    levels drops at its midpoint - where the cliff is cut - so the grid reads as draped over the
    terrain rather than floating through its walls.
*/
void ApplicationChasm::UpdateGridView(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const GridCheckReport> report = GetCheckReport();
    int version = view_version.load();
    if (!w || (w == grid_view_built_for && version == grid_view_built_version)){
        return;
    }
    grid_view_built_for = w;
    grid_view_built_version = version;
    auto t0 = std::chrono::steady_clock::now();
    const Grid* g = w->grid.get();
    const Terrain& terrain = *w->terrain;
    /*
        FLAT draws every layer at y = 0, as the grid is, rather than on the terrain. On the terrain a
        cell that straddles a cliff is drawn from the plateau down to the floor, and from above it
        reads as a long spoke - which hides its real shape, the thing the squareness layer is for.
        Hide the terrain to see the floor's cells under the plateau.
    */
    const bool f_flat = f_view_flat;
    auto H = [&terrain,f_flat](int v){
        return f_flat ? 0.0f : terrain.Height(v);
    };

    std::vector<line_vertex> verts;
    auto line = [&verts](const vec2& a, const vec2& b, uint32_t color, float ya = 0.0f, float yb = 0.0f){
        line_vertex v;
        v.color = color;
        v.pos = vec3(a.x,ya,a.y);
        verts.push_back(v);
        v.pos = vec3(b.x,yb,b.y);
        verts.push_back(v);
    };
    //A grid edge, on the terrain.
    auto edge = [&](int a, int b, uint32_t color){
        float ha = H(a);
        float hb = H(b);
        const vec2& pa = g->fine.pos[a];
        const vec2& pb = g->fine.pos[b];
        if (ha == hb){
            line(pa,pb,color,ha,ha);
        }else{
            vec2 mid = (pa + pb) * 0.5f;
            line(pa,mid,color,ha,ha);
            line(mid,pb,color,hb,hb);
        }
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
                edge(e.first,e.second,VIEW_COARSE);
            }else if (f_view_fine){
                edge(e.first,e.second,VIEW_FINE);
            }
        }
    }

    if (f_view_pins){
        //Every line the grid pins to, a touch above its ground, and a tick up from every vertex
        //pinned to it - so a gap between a line and its chain shows as ticks off the line.
        const int base = g->feature_line_base;
        auto line_colour = [base](int l){
            return (l < base) ? VIEW_PIN_OUTLINE : (l == base) ? VIEW_PIN_RIM : VIEW_PIN_SHARD;
        };
        //The rim's line is on the plateau and a shard's on its top - each on its high side.
        auto line_height = [base,f_flat](int l){
            if (f_flat || l <= base){
                return terrain_levels[TERRAIN_PLATEAU].height;
            }
            return terrain_levels[TERRAIN_SHARD].height;
        };
        //The smoothed lines the chains lie on (TerrainFeatureLines), not the settings' corners.
        for (size_t f = 0; f < w->features.size(); f++){
            int l = base + (int)f;
            const std::vector<vec2>& pts = w->features[f].points;
            for (size_t i = 0; i + 1 < pts.size(); i++){
                line(pts[i],pts[i + 1],line_colour(l),line_height(l) + 0.3f,line_height(l) + 0.3f);
            }
        }
        for (size_t v = 0; v < g->fine.pos.size(); v++){
            int pin = g->fine.pin[v];
            if (pin == GRID_PIN_FREE){
                continue;
            }
            float h = H((int)v);
            const vec2& p = g->fine.pos[v];
            if (pin == GRID_PIN_FIXED){
                line(p,p,VIEW_PIN_FIXED,h,h + unit * 1.5f);
            }else{
                line(p,p,line_colour(pin),h,h + unit * 0.4f);
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
            float h = H((int)i);
            line(p - vec2(r,0),p + vec2(r,0),c,h,h);
            line(p - vec2(0,r),p + vec2(0,r),c,h,h);
            line(p - vec2(r,r) * 0.7f,p + vec2(r,r) * 0.7f,c,h,h);
            line(p - vec2(r,-r) * 0.7f,p + vec2(r,-r) * 0.7f,c,h,h);
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
                line(in[k],in[(k + 1) % 4],col,H(q.v[k]),H(q.v[(k + 1) % 4]));
            }
        }
    }

    //The last check's issues, if it was run on THIS grid: a ring on the spot and a tall post, so a
    //failure is findable from a camera pulled back over the whole map.
    if (f_view_issues && report && report->hash == w->grid->Hash()){
        int sel = selected_issue.load();
        for (int i = 0; i < (int)report->issues.size(); i++){
            const GridIssue& issue = report->issues[i];
            bool f_sel = (i == sel);
            uint32_t c = f_sel ? VIEW_ISSUE_SELECTED : (issue.f_failure ? VIEW_ISSUE_FAIL : VIEW_ISSUE_LOOK);
            float r = unit * (f_sel ? 0.6f : 0.35f);
            //At the ground under it: the issue names a vertex or a quad, and either way the nearest
            //plot says which level the spot is on.
            GridPick under = w->picker->Pick(issue.where);
            float h = under.f_hit ? H(under.plot) : 0.0f;
            const int SEGMENTS = 12;
            for (int s = 0; s < SEGMENTS; s++){
                float a0 = 6.2831853f * s / SEGMENTS;
                float a1 = 6.2831853f * (s + 1) / SEGMENTS;
                line(issue.where + vec2(cosf(a0),sinf(a0)) * r,issue.where + vec2(cosf(a1),sinf(a1)) * r,c,h,h);
            }
            line(issue.where,issue.where,c,h,h + unit * (issue.f_failure ? 6.0f : 2.5f));
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
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w){
        return NULL;
    }
    std::shared_ptr<GridCheckReport> r = std::make_shared<GridCheckReport>(RunGridChecks(*w->grid));
    RunTerrainChecks(*w->grid,*w->terrain,w->features,*r);
    std::shared_ptr<const ZoneState> z = GetZones();
    if (z){
        RunZoneChecks(*w,*z,*r);
    }
    debug->Info("Grid check: %s in %.1f ms\n",r->f_pass ? "PASS" : "FAIL",r->check_ms);
    for (const GridCheckResult& c : r->results){
        debug->Info("  %-14s %s  %s\n",c.name.c_str(),c.f_skipped ? "skip" : (c.f_pass ? "pass" : "FAIL"),c.detail.c_str());
    }
    std::lock_guard<std::mutex> lock(grid_mutex);
    //Only if the world is still the one checked - a regeneration meanwhile makes the report stale.
    if (world == w){
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
        b = f_view_pins;        if (ImGui::Checkbox("pins",&b)){ f_view_pins = b; changed = true; }
        ImGui::SameLine();
        b = f_view_terrain;     if (ImGui::Checkbox("terrain",&b)){ f_view_terrain = b; }
        ImGui::SameLine();
        b = f_view_flat;        if (ImGui::Checkbox("flat",&b)){ f_view_flat = b; changed = true; }
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

    if (ImGui::CollapsingHeader("Paint",ImGuiTreeNodeFlags_DefaultOpen)){
        int tool = paint_tool;
        //"##tool" suffixes: "Field" and friends are plain words other widgets may use too.
        if (ImGui::RadioButton("Select##tool",tool == CHASM_TOOL_SELECT)) tool = CHASM_TOOL_SELECT;
        ImGui::SameLine();
        if (ImGui::RadioButton("House (1)##tool",tool == CHASM_TOOL_HOUSE)) tool = CHASM_TOOL_HOUSE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Field (2)##tool",tool == CHASM_TOOL_FIELD)) tool = CHASM_TOOL_FIELD;
        ImGui::SameLine();
        if (ImGui::RadioButton("Erase (3)##tool",tool == CHASM_TOOL_ERASE)) tool = CHASM_TOOL_ERASE;
        if (tool != paint_tool){
            paint_tool = tool;
            pick_version++;
        }
        ImGui::TextDisabled("click paints, drag paints more; on a house a click adds a storey, shift takes one off");
        std::shared_ptr<const ChasmWorld> zw = GetWorld();
        std::shared_ptr<const ZoneState> z = GetZones();
        if (zw && z && z->world == zw){
            ZoneStats st = ComputeZoneStats(*zw,*z);
            ImGui::Text("%i houses, %i storeys, %.0f units of floor",st.houses,st.storeys,st.house_floor_area);
            ImGui::Text("%i fields, %.0f units of field",st.fields,st.field_area);
        }
        std::string refusal;
        {
            std::lock_guard<std::mutex> lock(grid_mutex);
            refusal = zone_last_refusal;
        }
        if (!refusal.empty()){
            ImGui::TextColored(ImVec4(1.0f,0.45f,0.35f,1.0f),"last refused: %s",refusal.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Pick",ImGuiTreeNodeFlags_DefaultOpen)){
        std::shared_ptr<const ChasmWorld> pw = GetWorld();
        const GridPicker* p = pw ? pw->picker.get() : NULL;
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
            ImGui::Text("%s: plot %i on the %s, %i sides, %.1f units%s",label,pick.plot,
                        terrain_levels[pw->terrain->level[pick.plot]].name,p->PlotQuadCount(pick.plot),
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
                {"pins",{{"type","boolean"},{"description","the pinned lines and a tick on every vertex pinned to them"}}},
                {"terrain",{{"type","boolean"},{"description","the terrain mesh (all builds)"}}},
                {"flat",{{"type","boolean"},{"description","draw the grid layers at y = 0 instead of on the terrain - the true cell shapes near cliffs; pair with terrain:false"}}},
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
            layer("pins",f_view_pins);
            layer("flat",f_view_flat);
            if (changed){
                view_version++;
            }
#endif
            if (args.contains("terrain")){
                f_view_terrain = args["terrain"].get<bool>();
            }
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
                                    {"issues",f_view_issues.load()},{"pins",f_view_pins.load()},
                                    {"flat",f_view_flat.load()},{"terrain",f_view_terrain.load()}};
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
            GridPick pick;
            if (args.contains("px") && args.contains("py")){
                int2 px;
                px.x = args["px"].get<int>();
                px.y = args["py"].get<int>();
                //The camera is the physics thread's; read it between ticks.
                main_scene->AtTickBoundary([&](){
                    pick = PickUnderPixel(px);
                });
                if (!pick.f_hit){
                    result["error"] = "that pixel's ray meets no level of the map";
                }
                f_point = true;
            }else if (args.contains("x") && args.contains("z")){
                pick = p->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                f_point = true;
            }
            if (f_point){
                result["pick"] = PickJson(*p,pick);
                if (args.value("select",false)){
                    SetSelectedPick(pick);
                }
            }
            result["hover"] = PickJson(*p,GetHoverPick());
            result["selected"] = PickJson(*p,GetSelectedPick());
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_paint",
        "Paint a zone, exactly as the mouse does: the command goes through the simulation's queue "
        "and is applied on the next tick. op: house_add (a house, or a storey more), house_paint (a "
        "house only if there is none - what a drag does), house_remove (a storey less), "
        "house_erase, field_paint, field_erase. Name the place as world x/z, or directly as plot "
        "(a fine vertex, for house ops) or cell (a coarse quad, for field ops). Returns whether it "
        "changed anything, the refusal if not (the rules: a house needs flat ground all round its "
        "plot and no field; a field needs a flat coarse cell and no house; at most 4 storeys), and "
        "the zone totals. Needs the simulation running (not paused) to be applied.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"enum",{"house_add","house_paint","house_remove","house_erase","field_paint","field_erase"}}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"cell",{{"type","integer"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }},
            {"required",{"op"}}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            std::string name = args.value("op",std::string());
            int op = ZONE_OP_NONE;
            for (int i = 1; i < ZONE_OP_COUNT; i++){
                if (name == ZoneOpName(i)){
                    op = i;
                }
            }
            if (op == ZONE_OP_NONE){
                return json{{"error","unknown op " + name}};
            }
            bool f_field = (op == ZONE_OP_FIELD_PAINT || op == ZONE_OP_FIELD_ERASE);
            int index = -1;
            if (args.contains("x") && args.contains("z")){
                GridPick pick = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                if (pick.f_hit){
                    index = f_field ? pick.coarse_quad : pick.plot;
                }
            }else if (args.contains(f_field ? "cell" : "plot")){
                index = args[f_field ? "cell" : "plot"].get<int>();
            }
            if (index < 0){
                return json{{"error","no plot or cell there"}};
            }
            SimCommand cmd;
            cmd.type = CHASM_CMD_ZONE;
            cmd.subtype = (uint32_t)index;
            cmd.value[0] = (float)op;
            uint32_t before = zone_commands_done.load();
            //SubmitCommandAndWait, not SubmitZone: the tool reports what the command DID, so it has
            //to have run - see Application::SubmitCommandAndWait.
            SubmitCommandAndWait(cmd);
            bool f_ran = zone_commands_done.load() != before;
            std::shared_ptr<const ZoneState> z = GetZones();
            std::string refusal;
            {
                std::lock_guard<std::mutex> lock(grid_mutex);
                refusal = zone_last_refusal;
            }
            json result{{"op",name},{f_field ? "cell" : "plot",index},{"ran",f_ran},{"refusal",refusal}};
            if (z && z->world == w){
                ZoneStats st = ComputeZoneStats(*w,*z);
                result["totals"] = json{{"houses",st.houses},{"storeys",st.storeys},{"floor_area",st.house_floor_area},
                                        {"fields",st.fields},{"field_area",st.field_area}};
                if (!f_field){
                    result["storeys_here"] = z->storeys[index];
                }else{
                    result["field_here"] = (bool)z->field[index];
                }
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_tool",
        "Pick the mouse's tool, as keys 1/2/3 do: select, house, field, erase. With a paint tool the "
        "hover outline goes green where a click would paint and red where the rules refuse.",
        json{
            {"type","object"},
            {"properties",{
                {"tool",{{"type","string"},{"enum",{"select","house","field","erase"}}}}
            }}
        },
        [this](const json& args) -> json {
            const char* names[4] = {"select","house","field","erase"};
            std::string t = args.value("tool",std::string());
            for (int i = 0; i < 4; i++){
                if (t == names[i]){
                    paint_tool = i;
                    pick_version++;
                }
            }
            return json{{"tool",names[paint_tool.load()]}};
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
