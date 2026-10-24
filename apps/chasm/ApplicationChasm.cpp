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
#include "ChasmHud.h"
#include "RoadMesh.h"
#include "CropMesh.h"
#include "BuildingMesh.h"

#include <math.h>
#include <cmath>
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
/*
    The game's camera (play mode, docs/play_mode_plan.md): out at most to the user's reference view
    of 2026-10-06 - a village, its roads and the woods round it filling the screen, about 50 units
    out (README.md, "one zoom range") - and a pitch that stays a top-down view. Leaving debug mode
    beyond these eases in at CHASM_PLAY_EASE a second rather than jumping.
*/
#define CHASM_PLAY_DIST_MIN     12.0f
#define CHASM_PLAY_DIST_MAX     55.0f
#define CHASM_PLAY_PITCH_MIN    0.60f       //about 34 degrees above the horizon
#define CHASM_PLAY_PITCH_MAX    1.35f       //about 77
#define CHASM_PLAY_EDGE         10.0f       //the orbit point stays this far inside the map's edge
#define CHASM_PLAY_EASE         6.0f
#define CHASM_PLAY_START_DIST   34.0f       //the view entering play mode glides to: the camp and round it
#define CHASM_PLAY_START_PITCH  0.95f
#define CHASM_PLAY_FLY          3.0f        //the glide's ease a second: most of the way in a second
/*
    THE VIEW'S HEIGHT (ViewGroundHeight): the orbit point on the ground in view, a balcony's as the plateau's. A
    level it is on is kept while ground of it lies within CHASM_CAM_LEVEL_HOLD; another is glided to. And the
    camera tilts up over the player's pitch, no further than it must, to clear the ground between it and the
    point (ClearPitch) - a point on a balcony seen from over the plateau.
*/
#define CHASM_CAM_LEVEL_HOLD    2.5f        //world units round the point
#define CHASM_CAM_LEVEL_GLIDE   3.0f        //the ease a second down or up to another level: most of the way in a second
#define CHASM_CAM_RELIEF_EASE   12.0f       //...and over the relief of one level: a ridge does not jolt the view
#define CHASM_CAM_CLEAR         1.5f        //world units the line from the point to the camera keeps over the ground
#define CHASM_CAM_CLEAR_RAMP    3.0f        //...in full from this far out; over 2.2, or flat ground tilts the lowest pitch
#define CHASM_CAM_CLEAR_SAMPLES 12          //ground looked at along it
#define CHASM_CAM_CLEAR_PITCH   1.45f       //the most it tilts up to: about 83 degrees
#define CHASM_CAM_LIFT_EASE     6.0f        //the tilt's ease a second, as the ground along the line steps
#define CHASM_GRID_VIEW_Y       0.40f       //over the ground's relief (TerrainMesh.cpp, GROUND_BUMP)
#define PICK_VIEW_Y             0.45f       //over the grid view's lines
#define SUN_DISTANCE            400.0f      //how far out the sun's shadow camera sits from the view
/*
    THE SHADOW'S BOX (FollowSun): fitted to the ground in view, between the heights its shadows fall on - what the
    corners of the view meet from SUN_FIT_BELOW under the orbit point up to SUN_FIT_ABOVE over it or the plateau.
*/
#define SUN_FIT_BELOW           12.0f       //world units under the point: the next balcony down
#define SUN_FIT_ABOVE           10.0f       //...and over it, or over the plateau below it: trees and houses
#define SUN_FIT_REACH           3.0f        //a corner's line at most this many camera distances: one near the horizon
#define SUN_FIT_MOST            2.5f        //the half-extent at most, in camera distances: resolution near over far
#define SUN_FIT_STEP            1.1f        //the half-extent in steps of this ratio, so it changes rarely and edges keep still

ApplicationChasm::ApplicationChasm():Application(){
    app_name = "Chasm";
    debug->Info("Created new ApplicationChasm.\n");
    f_show_scene_window = false;
    f_show_inspector_window = false;
    f_show_engine_window = true;
    //The player's settings, here - before Start() makes the window - so "start full screen" is known in time.
    LoadSettings();
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

    //A release exe is the game: it starts in play mode, the panels a U away (play_mode_plan.md). A
    //debug exe starts in debug mode, as it always has.
#ifndef DEBUG
    f_show_ui = false;
#endif
    /*
        Frustum culling on (Renderer.h calls it a measuring prototype, off by default). It suits this
        app: the terrain is 144 large chunks with bounds, and at the game's zoom about a third are in
        view. Measured 2026-10-04 at that zoom: G-buffer 1.11 -> 0.49 ms, colour 1.88 -> 1.29 ms,
        46 of 144 chunks drawn. The shadow pass is never culled, by design.
    */
    renderer->f_frustum_cull = true;

    //SSAO on, tuned to the reference's look.
    renderer->f_ssao = true;
    renderer->ssao.radius = 0.75f;
    renderer->ssao.power = 1.25f;
    renderer->ssao.blur_radius = 4;

    default_shader = new Shader(shader_vert_name,shader_lit_frag_name);
    //The colour and depth passes' program for skinned meshes (the crowd bench's figures). NULL by
    //default, and a skinned mesh without it draws nothing and says nothing - see apps/bomber.
    renderer->skinned_shader = new Shader(shader_skinned_vert_name,shader_lit_frag_name);

    //main_window->Resize(1440,900);
    main_window->Resize(1920,1080);

    /*
        THE LOADING SCREEN (core's, Application::BeginLoading - archer's, on black): LOADING, a bar and the step under
        way, in a release exe - the one testers start. A debug exe only logs what each step took, and draws nothing.
        Only here, for Init: a load or a new map later has its own "Loading ..." (RunBusy).
    */
#ifdef DEBUG
    const bool f_loading_screen = false;
#else
    const bool f_loading_screen = true;
#endif
    loading_style.text = HUD_TEXT;
    loading_style.text_dim = HUD_TEXT_DIM;
    loading_style.bar_back = UIColor(255,255,255,40);
    loading_style.bar_fill = HUD_SELECT;
    BeginLoading(5,f_loading_screen);

    main_scene = CreateNewScene("Chasm");
    //Named at once, for everything below that adds the world's objects to it - see world_scene in ApplicationChasm.h.
    world_scene = main_scene;
    assetmanager = new AssetManager();
    rrand = new RRandom(1);

    /*
        Far plane for the whole map. Step 1's debug camera can pull back past the game's maximum
        zoom-out (README.md, "one zoom range") so the whole grid can be inspected; the near plane
        is kept at 0.5 so that range costs little depth precision on lines lying flat on the ground.
    */
    main_scene->camera->SetupPerspective(renderer->width,renderer->height,40.0f,0.5f,4000.0f);

    LoadingStep("shaders");
    BuildScene();
    LoadingStep("props");
    LoadProps();
    BuildSwayMaterials();
    LoadingStep("the map");
    //The title's town first, on a map of its own, captured for CreateTitleScene (test_maps_plan.md, step 6).
    BuildTitleTown();
    RegenerateGrid(next_settings);
    //Init is the render thread before the physics thread exists, so the camera may be set here.
    FrameMap();

    LoadingStep("sound");
    SetupInput();
    RegisterCommandHandlers();
    //The device and the cue table (ApplicationChasmSound.cpp).
    SetupSound();
    //The calendar counts days in ticks of this length (Calendar.h); a different tick would stretch them.
    if (std::fabs(physics_us_per_tick - 1000000.0 / CALENDAR_TICKS_PER_SECOND) > 0.5){
        debug->Warn("The tick is %.0f us, but the calendar counts %i ticks a second\n",physics_us_per_tick,
                    CALENDAR_TICKS_PER_SECOND);
    }
    //Init is the render thread before the physics thread exists, so the zones may be set up here.
    EnsureZonesWorld();
    /*
        THE TITLE (demo_plan.md): a scene of its own beside the world's, the game kept under it. Escape is the game's
        now - one step back, then the title - and the window's X and Alt+F4 ask first (Window::f_close_asks_app). A
        build without the panels - the one testers get - starts on it; a debug build starts in the world, as it
        always has, the title an Escape away.
    */
    //The ship build's world under the title is only there to be replaced: no colony is in play until one is chosen.
#ifndef USE_IMGUI
    f_game_on = false;
#endif
    /*
        RECORDING FOR TESTERS (ApplicationChasmRecord.cpp): recordings beside the saves, found from the exe as they are;
        and always recording in a build without the panels - the one testers get - unless they have turned it off in
        Settings. With the panels it would take over the recording an agent starts with input_record, so there the
        panel turns it on and the setting is not read.
    */
    recordings_dir = ChasmFolder("recordings");
#ifndef USE_IMGUI
    f_always_record = settings.GetBool("always_record");
#endif
    colony_name = "Seed " + std::to_string(next_settings.seed);
    LoadingStep("the title");
    CreateTitleScene();
    main_window->f_escape_closes_window = false;
    main_window->f_close_asks_app = true;
#ifndef USE_IMGUI
    /*
        Straight in, not RequestActiveScene. A direct write is right HERE and nowhere else (apps/archer, apps/bomber):
        Init runs before the physics thread exists, so nothing else reads main_scene yet. Requested, the world stayed
        active until the physics thread's first pass, and a frame that started before that ran the world's PreRender
        - whose uploads add their objects to whichever scene is active by then: the title got the world's blanket of
        clouds over its diorama (2026-10-18, once the loading screen made the first frame come sooner).
    */
    main_scene = title_scene;
#endif
#ifdef USE_MCP
    RegisterMCPTools();
#endif
    EndLoading();
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

        This is the sun as matched, which the title keeps. In the world it then moves with the year (UpdateSun,
        docs/seasons_plan.md "The sun"): higher in summer and lower in winter, warmer or cooler.
    */
    sun_offset = ChasmSunDirection(CHASM_SUN_ELEVATION,CHASM_SUN_AZIMUTH) * SUN_DISTANCE;

    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->color = ChasmSunColour(CHASM_SUN_KELVIN);
    //Soft light, as the reference: lit ground about 1.05x its palette colour and shade about 0.7x,
    //so most of it is the sky's and the sun only adds the rest. Measured against
    //art_source/chasm/alittleagedemo.jpg, not judged by eye.
    sun->brightness = CHASM_SUN_BRIGHTNESS;
    sun->SetupOrthographic(4096,4096,60.0f,1.0f,SUN_DISTANCE * 2.0f);
    sun->SetPosition(sun_offset);
    sun->SetLookAt(vec3());
    world_scene->AddObject(sun);

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
        //And the swamp's haze on its pools, so it too follows an edit of the PNG.
        vec2 suv = PaletteUV(PAL_SWAMP_MIST,PAL_EFFECTS);
        swamp_haze = palette->GetValueAt(suv.x,suv.y) / 255.0f;
        //The rows the seasons blend, kept as loaded: UpdateSeasonPalette rewrites the temperate one.
        palette_texture = palette;
        if (palette->img_data && palette->height % PALETTE_ROWS == 0){
            int channels = (palette->image_format == GL_RGBA) ? 4 : 3;
            size_t band = (size_t)palette->width * (size_t)(palette->height / PALETTE_ROWS) * (size_t)channels;
            palette_rows.assign(palette->img_data,palette->img_data + band * (PAL_WINTER + 1));
            //Summer kept (Palette.h): the broadleaves' green, never faded. Uploaded with the first fade.
            std::copy(palette_rows.begin(),palette_rows.begin() + band,palette->img_data + band * PAL_SUMMER);
        }
    }else{
        debug->Err("No palette (textures/palette.png) - the terrain draws white\n");
    }
    renderer->AddMaterial(mat);
    palette_material = renderer->FindMaterialIndex(mat.name);
    //The same palette for things the GPU moves (core's animated instance sets): the mist's puffs.
    Material puff = mat;
    puff.name = "chasm_palette_puff";
    puff.glsl_material.wind_mode = INSTANCE_MOTION_PUFF;
    renderer->AddMaterial(puff);
    puff_material = renderer->FindMaterialIndex(puff.name);
    BuildWaterScene();
    BuildGroundShader();
    BuildCloudScene();
    BuildHearthScene();
    BuildGhostScene();
    BuildWeatherScene();

    pick_view = new Object();
    pick_view->name = "Pick View";
    pick_view->SetVisualOnly(true);
    pick_view_mesh = new Mesh();
    pick_view->SetMesh(pick_view_mesh);
    pick_view->SetPickability(false);
    pick_view->SetCastsShadow(false);
    pick_view->SetVisibility(false);
    pick_view->SetPosition(vec3(0.0f,PICK_VIEW_Y,0.0f));
    world_scene->AddObject(pick_view);

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
    world_scene->AddObject(grid_view);
#endif
}

void ApplicationChasm::SetupInput(){
    InputController* input = main_scene->inputcontroller;
    /*
        EVERY KEY IS THE VIEWER'S, NOT RECORDED. Chasm's simulation input is its commands: a zone, the date,
        a needs edit each goes into a recording as a `cmd` line, whoever made it. The keys only move the
        camera, pick a tool, show the fog, or set the time - and recorded, a replay OWNED them while it ran
        (InputController.h, StartReplay): it played the run's camera turns back, and dropped the person's own
        keys, so a replay could not be sped up, paused or looked round (the user, 2026-10-08). A key added
        here goes through view_key, so it stays out of recordings too.
    */
    auto view_key = [&](uint32_t key, uint32_t action){
        input->AddKeyMap(key,action);
        input->SetRecorded(action,false);
    };
    //Arrows and WASD already pan through core's INPUT_MOVE_* / INPUT_TURN_* maps, and shift changes what a
    //drag paints - the viewer's as well.
    const uint32_t core_view[] = {INPUT_TURN_LEFT,INPUT_TURN_RIGHT,INPUT_TURN_UP,INPUT_TURN_DOWN,INPUT_MOVE_LEFT,
                                  INPUT_MOVE_RIGHT,INPUT_MOVE_UP,INPUT_MOVE_DOWN,INPUT_SHIFT};
    for (uint32_t action : core_view){
        input->SetRecorded(action,false);
    }
#if !defined(__ANDROID__)
    view_key('Q',INPUT_CHASM_ROTATE_LEFT);
    view_key('E',INPUT_CHASM_ROTATE_RIGHT);
    view_key(VK_ESCAPE,INPUT_CHASM_MENU);         //the game's since the title (demo_plan.md): no longer the window's close
    view_key(VK_RETURN,INPUT_CHASM_CONFIRM);
    view_key(VK_TAB,INPUT_CHASM_STATS);           //the Statistics page (statistics_plan.md)
    view_key(VK_F5,INPUT_CHASM_QUICK_SAVE);       //F9 and F10 are core's recording
    view_key(VK_F8,INPUT_CHASM_QUICK_LOAD);
    view_key(VK_F12,INPUT_CHASM_REPORT);
    view_key(VK_F11,INPUT_CHASM_FPS);
    view_key('F',INPUT_CHASM_FRAME);
    view_key('N',INPUT_CHASM_NEXT_SEED);
    view_key('B',INPUT_CHASM_PREVIOUS_SEED);
    view_key('1',INPUT_CHASM_TOOL_HOUSE);
    view_key('2',INPUT_CHASM_TOOL_FIELD);
    view_key('3',INPUT_CHASM_TOOL_ERASE);
    view_key('4',INPUT_CHASM_TOOL_GARDEN);
    view_key('5',INPUT_CHASM_TOOL_LOT);
    view_key('6',INPUT_CHASM_TOOL_ROAD);
    view_key('7',INPUT_CHASM_TOOL_WALL);
    view_key('8',INPUT_CHASM_TOOL_STORE);
    view_key('9',INPUT_CHASM_TOOL_WOODCUTTER);
    view_key('0',INPUT_CHASM_TOOL_WATER);
    view_key('H',INPUT_CHASM_TOOL_WINCH);     //H for hoist: the digits are all taken
    view_key('C',INPUT_CHASM_TOOL_CAMP);
    view_key('J',INPUT_CHASM_TOOL_BRIDGE);
    view_key('G',INPUT_CHASM_TOOL_BUILDERS);
    view_key('R',INPUT_CHASM_TOOL_RANGE);
    view_key('T',INPUT_CHASM_TOOL_TOWER);
    view_key('K',INPUT_CHASM_TOOL_ROPEWALK);
    view_key('M',INPUT_CHASM_TOOL_STONECUTTER);   //M for mason (stone_plan.md)
    view_key('O',INPUT_CHASM_TOOL_MINE);          //O for ore (stone_plan.md step 5)
    view_key('P',INPUT_CHASM_TOOL_STEAM);         //P for plume (steam_plan.md)
    view_key('L',INPUT_CHASM_TOOL_CONDENSER);     //L for liquid (steam_plan.md step 3)
    view_key('Y',INPUT_CHASM_TOOL_TERRACE);       //Y, the last letter free (terraces_plan.md stage 2)
    view_key('I',INPUT_CHASM_TOOL_SAIL_LOFT);     //I, the one after it (steam_plan.md step 4): WASD and U are core's
    view_key('V',INPUT_CHASM_FOG);
    //Time (gameplay_plan.md P1): space pauses, - and = (the + key) slow down and speed up.
    view_key(VK_SPACE,INPUT_CHASM_PAUSE);
    view_key(VK_OEM_MINUS,INPUT_CHASM_SLOWER);
    view_key(VK_OEM_PLUS,INPUT_CHASM_FASTER);
#ifdef DEBUG
    //Debug tools, in either mode: the keys are the viewer's, the zombie and the archer they put down recorded commands
    //(zombies_plan.md, test_maps_plan.md step 5).
    view_key('Z',INPUT_CHASM_ZOMBIE);
    view_key('X',INPUT_CHASM_ARCHER);
#endif
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
    //Before the mesh, which draws the seams into the walls, and the forest, which stands the outcrops.
    std::shared_ptr<DepositData> deposits = std::make_shared<DepositData>();
    BuildDeposits(*g,*w->picker,*t,*deposits);
    w->deposits = deposits;
    std::shared_ptr<TerrainMeshData> mesh = std::make_shared<TerrainMeshData>();
    BuildTerrainMesh(*g,*t,deposits.get(),*mesh);
    w->mesh = mesh;
    std::shared_ptr<ForestData> forest = std::make_shared<ForestData>();
    BuildForest(*g,*w->picker,*t,*mesh,deposits.get(),*forest);
    w->forest = forest;
    debug->Info("Forest: %i props in %.1f ms\n",(int)forest->props.size(),forest->build_ms);
    int kinds[DEPOSIT_KINDS] = {};
    for (const Deposit& d : deposits->deposits){
        kinds[d.kind]++;
    }
    debug->Info("Deposits: %i seams on %i balconies (%i triangles of crystal in the walls), %i outcrops, the mountain's "
                "foot %i plots, %.1f ms\n",kinds[DEPOSIT_SEAM],deposits->balconies,mesh->num_seam_triangles,
                kinds[DEPOSIT_OUTCROP],kinds[DEPOSIT_MOUNTAIN] ? (int)deposits->deposits[0].plots.size() : 0,
                deposits->build_ms);
    std::shared_ptr<MistData> mist = std::make_shared<MistData>();
    BuildMist(*g,*t,*mesh,*mist);
    w->mist = mist;
    std::shared_ptr<WaterMeshData> water = std::make_shared<WaterMeshData>();
    BuildWaterMesh(*g,*t,*water);
    w->water = water;
    std::shared_ptr<ExplorationSight> sight = std::make_shared<ExplorationSight>();
    sight->Build(*g,*w->picker,*t);
    w->sight = sight;
    std::shared_ptr<AreaData> areas = std::make_shared<AreaData>();
    BuildAreas(*w->picker,*t,*forest,*sight,*areas);
    w->areas = areas;
    debug->Info("Areas: %i to discover, %.1f ms\n",(int)areas->areas.size(),areas->build_ms);
    debug->Info("Chasm: %i mist puffs (%.1f ms), %i rivers, %i falls, %i wet vertices, water %.1f ms, "
                "sight %.1f ms\n",(int)mist->puffs.size(),mist->build_ms,(int)t->rivers.size(),
                (int)t->falls.size(),t->wet_count,water->build_ms,sight->build_ms);
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

    Each level is a GROUND, not a plane: its height plus the small bump, and on the plateau the
    relief (Terrain::GroundHeight) - hills, and peaks up to relief_max north of the mountain's foot.
    So the ray is marched down through the band each level's ground can lie in, and where it first
    goes under that ground the crossing is found by bisection. The levels are tried from the top
    down, and the first crossing that lands on a plot OF THAT LEVEL is what the ray hits: one over
    the chasm lands on a floor plot and is passed over, and the ray goes on down to the floor. That
    is right even with hills, because going under the plateau's ground over the chasm means the ray
    is already inside the chasm. Walls are never hit - nothing is built on a wall.

    Intersecting each level's flat plane, as this did while the ground was flat, lands a pick on a
    hill off by the parallax - a plot behind the one under the cursor.
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
    const Terrain& t = *w->terrain;
    const vec3 o = r.origin;
    const vec3 d = r.direction;
    auto at_t = [&](float s){ return o + d * s; };
    //The highest ground first: the first ground the ray meets is what it sees. A plot's top is a
    //whole number of steps down (Terrain.h), so the heights to try are those.
    for (int l = 0; l <= TERRAIN_FLOOR_STEPS; l++){
        const float level_h = -TERRAIN_STEP * (float)l;
        //The band this height's ground lies in: the relief is the plateau's alone; one unit covers
        //the bump either way.
        float top = level_h + ((l == 0) ? t.relief_max : 0.0f) + 1.0f;
        float bottom = level_h - 1.0f;
        float s0 = std::max(0.0f,(top - o.y) / d.y);
        float s1 = (bottom - o.y) / d.y;
        if (s1 <= s0){
            continue;
        }
        auto under = [&](float s){
            vec3 p = at_t(s);
            return p.y <= t.GroundHeight(vec2(p.x,p.z),level_h);
        };
        /*
            Steps of a unit across the ground, or two up and down for a ray looking nearly straight
            down - the relief changes over several units, so a step that size cannot jump a hill.
            Capped, for a ray grazing a long way across the map.
        */
        float across = std::sqrt(d.x * d.x + d.z * d.z);
        float step = std::min(1.0f / std::max(across,1e-4f),2.0f / -d.y);
        int steps = std::min(4000,(int)std::ceil((s1 - s0) / step));
        step = (s1 - s0) / (float)std::max(1,steps);
        float prev = s0;
        float hit = -1.0f;
        if (under(s0)){
            hit = s0;
        }
        for (int k = 1; k <= steps && hit < 0.0f; k++){
            float s = s0 + step * (float)k;
            if (under(s)){
                float a = prev;
                float b = s;
                for (int i = 0; i < 20; i++){
                    float m = (a + b) * 0.5f;
                    if (under(m)){
                        b = m;
                    }else{
                        a = m;
                    }
                }
                hit = b;
            }
            prev = s;
        }
        if (hit < 0.0f){
            continue;
        }
        vec3 at = at_t(hit);
        GridPick pick = w->picker->Pick(vec2(at.x,at.z));
        if (pick.f_hit && t.steps[pick.plot] == l){
            return pick;
        }
        none.at = vec2(at.x,at.z);
    }
    return none;
}

/*
    What is under the MOUSE (ramparts_plan.md). The engine reads the surface there every frame - its position and
    normal, from the G-buffer (Renderer::ReadPickingAsync) - wherever a pickable object is drawn, which the terrain's
    and the zones' chunks are (surface_ids). Stepped a little back into what it hit, that names the plot whose ground,
    building or castle piece it is - the top of a tall wall its own plot, where the ray against the ground would reach
    the plot behind it - and its height names the LAYER: the plot's place, ground or deck, nearest it. A frame old, as
    the engine's answer always is. Over anything else - a person, a tree, water, the sky - the ray, as before.
*/
#define PICK_SURFACE_INSET      0.06f   //back into the surface hit, along its normal: inside what was hit

GridPick ApplicationChasm::PickUnderMouse(int2 px){
    InputController* input = main_scene->inputcontroller;
    objectid_t id = input->GetHoveredObjectID();
    if (id != OBJECTID_INVALID && std::find(surface_ids.begin(),surface_ids.end(),id) != surface_ids.end()){
        GridPick pick = PickSurface(input->GetHoveredPosition(),input->GetHoveredNormal());
        if (pick.f_hit){
            return pick;
        }
    }
    return PickUnderPixel(px);
}

GridPick ApplicationChasm::PickSurface(const vec3& at, const vec3& normal){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w){
        return GridPick();
    }
    vec3 in = at - normal * PICK_SURFACE_INSET;
    GridPick pick = w->picker->Pick(vec2(in.x,in.z));
    if (!pick.f_hit){
        return pick;
    }
    pick.at = vec2(at.x,at.z);
    pick.f_surface = true;
    std::shared_ptr<const ZoneState> z = GetZones();
    pick.layer = (z && z->world == w) ? SurfaceLayer(*w,*z,pick.plot,at) : WALK_GROUND;
    return pick;
}

int SurfaceLayer(const ChasmWorld& w, const ZoneState& z, int plot, const vec3& at){
    const Terrain& t = *w.terrain;
    int best = WALK_GROUND;
    //Its ground, if there is ground to stand on there - none under a block or a stair.
    float best_d = ZoneCastleShuts(z,plot) ? INFINITY : std::fabs(at.y - t.GroundHeight(vec2(at.x,at.z),t.Height(plot)));
    int ls[WALK_MOST_DECKS];
    int n = WalkDeckLayers(z,plot,ls);
    for (int i = 0; i < n; i++){
        float d = std::fabs(at.y - BuildingLayerHeight(w,z,plot,ls[i]));
        if (d < best_d){
            best_d = d;
            best = ls[i];
        }
    }
    return best;
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
    bool f_held = input->IsKeyDown(INPUT_CLICK_LEFT);
    bool f_right = input->WasKeyPressed(INPUT_CLICK_RIGHT);

    GridPick hover;
    bool f_over_scene = p && main_window->f_has_focus && !UIWantsMouse();
    int2 px = input->GetRelativeMousePosition();
    //The overlay's hover (the top bar's FOOD lists its foods): the mouse over the window, unless a script pinned one.
    if (!f_overlay_mouse_pinned){
        bool f_in = main_window->f_has_focus && px.x >= 0 && px.y >= 0 && px.x < main_window->width && px.y < main_window->height;
        overlay_mouse_x = f_in ? px.x : -1;
        overlay_mouse_y = f_in ? px.y : -1;
    }else{
        /*
            ...and the scene's hover too (chasm_select op hover): at the script's pixel, which the engine reads in
            the mouse's place (InputController::PinPickPixel), with or without focus - so a person's hover outline
            is in the next screenshot. The mouse's buttons do nothing meanwhile.
        */
        px = int2(overlay_mouse_x.load(),overlay_mouse_y.load());
        f_over_scene = p && !UIWantsMouse();
        f_clicked = false;
        f_held = false;
    }
    if (f_over_scene){
        f_over_scene = px.x >= 0 && px.y >= 0 && px.x < main_window->width && px.y < main_window->height;
        if (f_over_scene){
            hover = PickUnderMouse(px);
        }
    }
    //A script's hover (chasm_tool hover_x/hover_z): the plot there, and the mouse paints nothing meanwhile.
    if (f_hover_pinned && p){
        vec2 pin;
        {
            std::lock_guard<std::mutex> lock(pick_mutex);
            pin = hover_pin;
        }
        hover = p->Pick(pin);
        f_over_scene = false;
        //With a scripted button it is pressed, held and let go there as the mouse's would be.
        int button = script_left.load();
        if (button >= 0){
            f_over_scene = true;
            f_held = (button == 1);
            f_clicked = f_held && !f_script_left_was;
            px = int2(-1,-1);       //and the real mouse is nowhere, so the overlay takes nothing
        }
        f_script_left_was = (button == 1);
    }else{
        f_script_left_was = false;
    }
    if (f_script_right.exchange(false)){
        f_right = true;
    }
    /*
        A road being drawn (line_works_plan.md) is dropped by a right press, or by the tool being put
        down. Before the play overlay: the press that drops a line is not also the right click that puts
        the tool down.
    */
    bool f_line_dropped = false;
    if (line_drag != LINE_DRAG_NONE && !ChasmLineTool(paint_tool)){
        DropLine();
        line_drag = LINE_DRAG_NONE;
    }else if (line_drag == LINE_DRAG_DRAWING && f_right){
        DropLine();
        f_line_dropped = true;
    }
    //...and any other drag (drag_plan.md) the same way - by another tool taken up too, whose drag it is not.
    if (area_drag != LINE_DRAG_NONE && paint_tool != drag_tool){
        DropDrag();
        area_drag = LINE_DRAG_NONE;
    }else if (area_drag == LINE_DRAG_DRAWING && f_right){
        DropDrag();
        f_line_dropped = true;
    }
    //...and a map tool's (water_tools_plan.md), whose release makes a new map - so never one left over from a tool put down.
    if (f_map_dragging && (paint_tool != map_drag_tool || f_right)){
        DropMapDrag();
        f_line_dropped = f_line_dropped || f_right;
    }
    //Play mode's own selection, and its overlay taking the mouse where it is drawn (selection_plan.md).
    if (PlayMode()){
        UpdatePlayPick(hover,px,f_over_scene,f_clicked);
        if (f_line_dropped){
            f_right_press = false;
        }
    }else{
        //In debug mode the overlay is the HUD at the top alone - its calendar's buttons, its figures that open the
        //Statistics page, and its panels, which paint and select nothing beneath them.
        OverlayHit hit;
        if (f_over_scene && OverlayHitAt(px,&hit)){
            f_over_scene = false;
            hover = GridPick();
            if (f_clicked && hit.action == OVERLAY_HIT_STATS){
                OpenStats();
            }else if (f_clicked && hit.action == OVERLAY_HIT_REPLAY_STOP){
                RequestReplayStop();        //the replay's bar (ApplicationChasmRecord.cpp)
            }else if (f_clicked){
                PressTimeButton(hit.action);
            }
            f_clicked = false;
        }
    }
    //A paint tool takes the left button; selecting is what the button does with no tool.
    bool f_painting = (paint_tool != CHASM_TOOL_SELECT);
    if (f_painting){
        UpdatePaint(hover,f_over_scene,f_clicked,f_held);
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
        //A test map (test_maps_plan.md) starts with nobody on it, and nothing under the clouds.
        const bool f_chasm = w && GridKindHasChasm(w->grid->settings.kind);
        zones.Reset(w);
        PublishZones();
        //A new map is a new colony: its first day of spring, and its settlers' camp, a tent each - or the title's town.
        if (f_chasm){
            zones.PlaceStartCamp(EconomySettlerCount());
            PublishZones();
        }else if (w->grid->settings.kind == GRID_KIND_TITLE){
            zones.PlaceTitleTown();
            PublishZones();
        }
        SetCalendar(0);
        //Nothing explored yet: the camp clears its own ground on the first tick (play_mode_plan.md).
        ResetExploration();
        if (w && !f_chasm){
            exploration.RevealAll();
            PublishExploration();
        }
    }
    if (economy.GetWorld() != w){
        economy.Reset(w);
        PublishEconomy();
    }
    if (walkers.GetWorld() != w){
        walkers.Reset(w);
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

uint32_t ApplicationChasm::PaintOwner(int tool){
    bool f_yard = tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT;
    //A winch, a tower or a mine is one plot and a bridge is placed whole: with one selected, the tool still makes a new one.
    int kind = ToolKind(tool);
    bool f_extend = kind != ZONE_KIND_NONE && kind != ZONE_KIND_BRIDGE && !ZoneKindOnePlot(kind);
    if ((!f_yard && !f_extend) || !PlayMode()){
        return 0;
    }
    PlayPick sel;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
    }
    std::shared_ptr<const ZoneState> z = GetZones();
    if (sel.kind != PLAY_PICK_BUILDING || !z || sel.id >= z->buildings.size() || z->buildings[sel.id].size <= 0){
        return 0;
    }
    if (f_extend){
        //Extended as a yard is painted (the user's, 2026-10-10): with another kind selected, a building of its own.
        return (z->buildings[sel.id].kind == kind) ? sel.id : 0;
    }
    //A house or a hut: named, so a garden for a hut is refused with a reason rather than given to whatever is near.
    return ZoneKindHasYard(z->buildings[sel.id].kind) ? sel.id : 0;
}

void ApplicationChasm::SubmitZone(int op, int index, int kind, uint32_t stroke, uint32_t owner){
    if (index < 0){
        return;
    }
    SimCommand cmd;
    cmd.type = CHASM_CMD_ZONE;
    //The player's intent, decided by the view from the camera - which a replay does not reproduce,
    //so the command itself is what gets recorded (SIM_CMD_FLAG_RECORD).
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = (uint32_t)index;
    cmd.value[0] = (float)op;
    cmd.value[1] = (float)kind;
    cmd.value[2] = (float)stroke;     //exact: strokes stay far below a float's 2^24
    //A play-mode command is a PLAY command: held to the clouds, and what it builds is a construction
    //site (construction_plan.md). The mode is read here, by the view, and travels in the command - with
    //the yard's owner a garden or lot is painted for (Zones.h, ZoneCommandValue3).
    cmd.value[3] = ZoneCommandValue3(PlayMode(),owner);
    main_scene->SubmitCommand(cmd);
}

bool ApplicationChasm::ApplyZoneCommand(Zones& zones, const Exploration& ex, const EconomyState* e, int op, uint32_t index,
                                        int value1, uint32_t stroke, bool f_play, uint32_t owner, bool f_crates){
    std::shared_ptr<const ChasmWorld> zw = zones.GetWorld();
    if (e && e->world != zw){
        e = NULL;
    }
    auto refuse = [&](const char* why){
        zones.last_refusal = why;
        zones.last_arch_lost = 0;
        return false;
    };
    /*
        A PLAY command (value[3]'s bit 0, play_mode_plan.md) is held to the clouds: none on ground not yet explored.
        Every debug tool's command, and every recording made before play mode, has 0 there, so they paint as they
        always did. A bridge has two ends, and neither may be under the clouds.
    */
    if (f_play && zw){
        if (!ZoneCommandExploredIn(ex,*zw,op,(int)index) || (op == ZONE_OP_BRIDGE && !ZoneCommandExploredIn(ex,*zw,op,value1))){
            return refuse("under the clouds");
        }
        //Nor over a crate (crates_plan.md, the user's): it has to be cleared away first. Play commands only, like the
        //clouds; a debug placement goes ahead, and the crate under it is lost (Economy::LoseCoveredCrates).
        if (f_crates && e && EconomyCrateInTheWay(*e,*zw,op,(int)index,value1)){
            return refuse("a crate lies there");
        }
        //Nor outside every settlement's territory (settlements_plan.md): play commands only, as for the clouds.
        if (e && !EconomyCommandInTerritory(*e,*zw,op,(int)index)){
            return refuse("outside the settlement");
        }
    }
    /*
        ONE CAMP TO A COLONY (settlements_plan.md, the user's 2026-10-12): a camp painted on an empty plot of a colony
        that has one - in play or debug - is that camp's, wherever it stands, as a tent pitched there is
        (EconomyColonyCamp; THE CAMP, Zones.h).
    */
    const ZoneState& z = zones.State();
    //A gate's door the zombies broke (zombies_plan.md step 4) hangs open until it is repaired: it shuts on nothing. A
    //gate two plots wide is one door, so either plot's break counts.
    if (op == ZONE_OP_GATE_SHUT && value1 && e && zw && index < z.gate.size()){
        bool f_broken = EconomyGateBroken(*e,(int)index);
        int nb[8];
        int n = ZonePlotNeighbours(*zw,(int)index,nb,8);
        for (int i = 0; i < n && !f_broken; i++){
            f_broken = z.gate[nb[i]] != ZONE_GATE_NONE && EconomyGateBroken(*e,nb[i]);
        }
        if (f_broken){
            return refuse("its door is broken");
        }
    }
    if (!owner && (op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD) && value1 == ZONE_KIND_CAMP && e
        && index < z.building.size() && !z.building[index]){
        owner = EconomyColonyCamp(*e,z,(int)index);
    }
    return zones.Apply(op,index,value1,stroke,f_play,owner);
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
            //value[3]: the play flag, and above it a yard's owner (ZoneCommandValue3) - the rest is ApplyZoneCommand's.
            bool f_play = ZoneCommandPlay(cmd.value[3]);
            std::shared_ptr<const ChasmWorld> zw = zones.GetWorld();
            int op = (int)cmd.value[0];
            bool f_changed = ApplyZoneCommand(zones,exploration,(zw && economy.State().world == zw) ? &economy.State() : NULL,
                                              op,cmd.subtype,(int)cmd.value[1],(uint32_t)cmd.value[2],f_play,
                                              ZoneCommandOwner(cmd.value[3]));
            {
                std::lock_guard<std::mutex> lock(grid_mutex);
                zone_last_refusal = zones.last_refusal;
                zone_last_arch_lost = zones.last_arch_lost;
            }
            if (!f_changed && !zones.last_refusal.empty()){
                zone_refusals++;
            }
            //A player's command that took an arch's floors down with it says so (arches_plan.md step 4); a debug
            //placement's, and the builders', say nothing.
            if (f_changed && f_play && zones.last_arch_lost > 0){
                arch_notices++;
            }
            //For the sound (ApplicationChasmSound.cpp): something placed, or taken away. A chip's command - a crop, a
            //priority, a store's goods - is not one: its press already clicked.
            if (f_changed){
                int v1 = (int)cmd.value[1];
                if (op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD || op == ZONE_OP_FIELD_PAINT
                    || op == ZONE_OP_GROUND_PAINT || op == ZONE_OP_BRIDGE || op == ZONE_OP_RANGE_PAINT
                    || (op == ZONE_OP_CASTLE && v1 != 0)){
                    zone_placed++;
                }else if (op == ZONE_OP_BUILD_REMOVE || op == ZONE_OP_BUILD_ERASE || op == ZONE_OP_FIELD_ERASE
                          || op == ZONE_OP_GROUND_ERASE || op == ZONE_OP_CASTLE){
                    zone_erased++;
                }
            }
            if (f_changed){
                PublishZones();
            }
            zone_commands_done++;
            return OBJECTID_INVALID;
        });
    RegisterTimeCommands();
    RegisterEconomyCommands();
}

void ApplicationChasm::RunSimulationTick(void){
    //The title's own ticks (it ticks as any active scene does): the world waits under it, not run.
    if (TitleShowing()){
        return;
    }
    EnsureZonesWorld();
    //Before the calendar moves on: this tick's work is done on this tick's date.
    TickEconomy();
    TickExploration();
    //The demo's goals and its end, as rules, on what this tick's discoveries are (ApplicationChasmGoals.cpp).
    TickGoals();
    TickCalendar();
}

//--- Saves and replays (step 6) ---------------------------------------------------------------------

//From the published copies, so any thread may make one.
ChasmSave ApplicationChasm::MakeSave(){
    ChasmSave s;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w){
        return s;
    }
    s.settings = w->grid->settings;
    char hash[32];
    snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)w->grid->Hash());
    s.world_hash = hash;
    if (z && z->world == w){
        s.buildings = ZoneSaveBuildings(*z);
        s.next_building = (uint32_t)z->buildings.size();
        s.stroke = z->stroke;
        s.stroke_building = z->stroke_building;
        for (size_t v = 0; v < z->ground.size(); v++){
            if (z->ground[v]){
                //A garden, lot, road or wall still a site carries ZONE_GROUND_SAVED_SITE with its kind, a
                //wall's gate ZONE_GROUND_SAVED_GATE - and _SHUT with its door shut; a road or wall its line.
                ZoneSavedGround g;
                g.plot = (int)v;
                g.kind = (int)z->ground[v] | (!z->ground_built[v] ? ZONE_GROUND_SAVED_SITE : 0)
                         | (z->gate[v] ? ZONE_GROUND_SAVED_GATE : 0) | (ZoneGateShut(*z,(int)v) ? ZONE_GROUND_SAVED_SHUT : 0);
                g.line = z->line[v];
                g.owner = ZoneGroundOwner(*z,(int)v);      //whose yard (yards_plan.md), 0 nobody's
                g.arch = z->ArchBuiltOf((int)v);            //an arch's floors built over it (arches_plan.md)
                s.grounds.push_back(g);
            }
        }
        //The debug castle's pieces (ramparts_plan.md), as they are painted: a piece over a piece after it, on the same
        //plot - each at the plot's level.
        for (size_t v = 0; v < z->castle.size(); v++){
            ZoneSavedCastle c;
            c.plot = (int)v;
            c.base = ZoneCastleBase(*z,(int)v);
            if (z->castle[v]){
                c.piece = z->castle[v];
                s.castle.push_back(c);
            }
            if (z->castle_over[v]){
                c.piece = z->castle_over[v];
                s.castle.push_back(c);
            }
        }
        //Terraces (terraces_plan.md stage 2).
        for (size_t v = 0; v < z->terrace.size(); v++){
            if (ZoneTerraced(*z,(int)v)){
                ZoneSavedTerrace tr;
                tr.plot = (int)v;
                tr.top = z->terrace[v];
                tr.f_built = z->terrace_built[v] != 0;
                s.terraces.push_back(tr);
            }
        }
    }
    s.calendar_tick = calendar_shown;
    std::shared_ptr<const Exploration> ex = GetExploration();
    if (ex){
        s.explored = ex->ToString();
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (e && e->world == w){
        s.economy = EconomySaveState(*e);
    }
    return s;
}

/*
    PHYSICS THREAD, or with physics_mutex held (the panel, a tool through AtTickBoundary) - it
    replaces simulation state. The map is generated again only if the save was made on another one;
    a save of this map just repaints, in a few milliseconds.
*/
bool ApplicationChasm::LoadSave(const ChasmSave& save, std::string& error){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w || !(w->grid->settings == save.settings)){
        RegenerateGrid(save.settings);
        next_settings = save.settings;
        w = GetWorld();
    }
    char hash[32];
    snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)w->grid->Hash());
    if (!save.world_hash.empty() && save.world_hash != hash){
        //Not fatal: the generator has changed since the save, and the paint goes back on through
        //the rules, so nothing invalid can come of it - but it may not land where it was.
        debug->Warn("Save was made on world %s, these settings now give %s - the plots may differ\n",
                    save.world_hash.c_str(),hash);
    }
    int refused = zones.Restore(w,save.buildings,save.next_building,save.grounds,save.stroke,save.stroke_building,save.terraces);
    refused += zones.RestoreCastle(save.castle);
    paint_stroke = save.stroke;
    PublishZones();
    walkers.Reset(w);
    //After the zones: the stocks and workers name their buildings.
    economy.Restore(w,save.economy,zones.State(),walkers,save.calendar_tick);
    //What had been seen; a save from before exploration existed has seen everything.
    ResetExploration();
    if (save.explored.empty() || !exploration.FromString(save.explored)){
        exploration.RevealAll();
    }
    PublishExploration();
    //What it had seen marked again, telling nothing, and so its DISCOVERIES - now, not on the next tick: a load while
    //paused would show the build bar of the colony before it until the clock ran.
    LookForSeams(*w,0);
    LookForAreas(*w,0,false);
    UpdateDiscoveries(*w);
    PublishEconomy();
    SetCalendar(save.calendar_tick);
    if (refused){
        error = std::to_string(refused) + " plots, cells or ground refused by the rules";
        debug->Warn("LoadSave: %s\n",error.c_str());
    }
    return refused == 0;
}

//A save, with the meta a save from the menus has - so a recording names its colony and the build that made it.
json ApplicationChasm::CaptureRecordingState(){
    json j = ChasmSaveToJson(MakeSave());
    std::string colony;
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        colony = colony_name;
    }
    j["meta"] = json{{"title",SaveTitle()},{"kind","recording"},{"colony",colony},{"build",BuildName()}};
    return j;
}

void ApplicationChasm::RestoreRecordingState(const json& state){
    ChasmSave save;
    std::string error;
    if (!ChasmSaveFromJson(state,save,error)){
        debug->Err("Replay start state: %s - replaying from the current state\n",error.c_str());
        return;
    }
    LoadSave(save,error);
    //A recording from another build may not replay as it ran (demo_plan.md, step 4), so it says so.
    const json meta = state.value("meta",json::object());
    auto meta_text = [&](const char* key){
        return (meta.is_object() && meta.contains(key) && meta[key].is_string()) ? meta[key].get<std::string>() : std::string();
    };
    std::string build = meta_text("build");
    if (!build.empty() && build != BuildName()){
        ShowNote("Recorded by " + build + ": this build may replay it differently");
    }
    //One started from the title's list is entered as a load is: its colony's name for its saves, the view on it.
    if (f_menu_replay.exchange(false)){
        std::string colony = meta_text("colony");
        EnteredColony(colony.empty() ? "Seed " + std::to_string(save.settings.seed) : colony);
    }
}

//The debug castle's piece over a plot's piece (ramparts_plan.md), for the tools: null with none.
static json CastleOverJson(const ZoneState& z, int plot){
    if (ZoneCastleOverShape(z,plot) == ZONE_CASTLE_NONE){
        return json();
    }
    return json{{"shape",ZoneCastleShapeName(ZoneCastleOverShape(z,plot))},{"storeys",ZoneCastleOverStoreys(z,plot)},
                {"layer",ZoneCastleOverLayer(z,plot)}};
}

/*
    The state a tick ends in, as the replay trace sees it (Application::HashSimState): the world the
    simulation runs on and what is painted on it. The base class's per-object hash is deliberately
    not called - every object here is view, and the sun moves with the camera.
*/
void ApplicationChasm::HashSimState(StateHash& h){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    h.Begin("world");
    h.Add(w ? w->grid->Hash() : (uint64_t)0);
    const ZoneState& z = zones.State();
    h.Begin("zones");
    h.Bytes(z.storeys.data(),z.storeys.size());
    h.Bytes(z.standing.data(),z.standing.size());
    h.Bytes(z.field.data(),z.field.size() * sizeof(uint32_t));
    h.Bytes(z.ground.data(),z.ground.size());
    h.Bytes(z.ground_built.data(),z.ground_built.size());
    //Roads' and walls' lines - their place in the queue - and walls' gates (line_works_plan.md).
    h.Bytes(z.line.data(),z.line.size() * sizeof(uint32_t));
    h.Bytes(z.gate.data(),z.gate.size());
    //Yards (yards_plan.md): each owned plot with its owner - only those, so zones with no yard hash as before.
    for (size_t v = 0; v < z.ground_owner.size(); v++){
        if (z.ground_owner[v]){
            h.Add(((uint64_t)v << 32) | z.ground_owner[v]);
        }
    }
    //Which of a field's cells are built (fields_plan.md).
    h.Bytes(z.field_built.data(),z.field_built.size());
    //Arches' floors built (arches_plan.md step 3) - only where there are some, so zones with none hash as before.
    for (size_t v = 0; v < z.arch_built.size(); v++){
        if (z.arch_built[v]){
            h.Add(((uint64_t)v << 32) | z.arch_built[v]);
        }
    }
    //The debug castle's pieces (ramparts_plan.md) - likewise only where there are some.
    for (size_t v = 0; v < z.castle.size(); v++){
        if (z.castle[v]){
            h.Add(((uint64_t)v << 32) | 0xCA500u | z.castle[v]);
            h.Add(((uint64_t)v << 32) | 0xBA5E00u | (uint8_t)ZoneCastleBase(z,(int)v));     //its level
        }
        if (z.castle_over[v]){
            h.Add(((uint64_t)v << 32) | 0x0E000u | z.castle_over[v]);
        }
    }
    //Terraces (terraces_plan.md stage 2) - likewise only where there are some, so a colony without any hashes as before.
    for (size_t v = 0; v < z.terrace.size(); v++){
        if (ZoneTerraced(z,(int)v)){
            h.Add(((uint64_t)v << 32) | 0x7E000u | ((uint32_t)(uint8_t)z.terrace[v] << 1) | (z.terrace_built[v] ? 1u : 0u));
        }
    }
    //The buildings (docs/buildings_plan.md): which each plot is part of, the table, the drag in progress.
    h.Begin("buildings");
    h.Bytes(z.building.data(),z.building.size() * sizeof(uint32_t));
    for (size_t id = 0; id < z.buildings.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        //A store's goods apart: 16 bits since provisions (defence_plan.md), which would run into the size here.
        h.Add((uint64_t)b.kind | ((uint64_t)b.crop << 8) | ((uint64_t)(uint32_t)b.size << 24)
              | ((uint64_t)b.f_priority << 56) | ((uint64_t)(b.next_crop & 0x7F) << 57));
        h.Add(b.allow);
        //A field given to a house (land_plan.md), with its id - only where given, so most tables hash as before.
        if (b.land_of){
            h.Add(((uint64_t)id << 32) | b.land_of);
        }
        //A guard tower's ladder, its door (defence_plan.md) - towers only, likewise.
        if (b.kind == ZONE_KIND_TOWER){
            h.Add(((uint64_t)id << 32) | (uint32_t)b.ladder);
        }
        //A workplace the player stopped (stopping_work_plan.md) - only where stopped, likewise.
        if (b.f_stopped){
            h.Add(((uint64_t)id << 32) | 0x5709u);
        }
    }
    h.Add(((uint64_t)z.stroke << 32) | z.stroke_building);
    HashEconomy(h);
    h.Begin("calendar");
    h.Add(calendar_tick);
    h.Begin("explored");
    h.Add(exploration.Hash());
}

/*
    PHYSICS THREAD, from UpdatePick, with a paint tool in hand: the mouse made into zone commands.

    A press paints what is under it; with shift it takes a storey off instead. A CLICK on a building -
    let go on the plot it was pressed on - adds a storey, sent on the release, so a drag that starts on
    a building adds none (paint_press_plot). Held and dragged, it paints each NEW plot or cell the cursor
    reaches, once: a drag never stacks storeys, so sweeping across a street lays one floor and clicking
    raises it. With a building of the tool's kind selected, what it paints extends that one (PaintOwner).
    Erase takes the house off a plot if there is one, otherwise the field off its cell. The road and wall
    tools draw a line instead and place it on release (UpdateLineDrag), and since 2026-10-18 so does every
    other tool that paints (UpdateAreaDrag, drag_plan.md): what is left here is the one-plot kinds, a click
    each, and the debug castle's pieces. `f_held` is the left button's, the mouse's or a script's (UpdatePick).
*/
void ApplicationChasm::UpdatePaint(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held){
    InputController* input = main_scene->inputcontroller;
    //A press on a building, waiting: held off its plot it was a drag, starting there with no storey (a paint on a
    //built plot only picks the stroke's building); let go on it, a click - one storey more.
    if (paint_press_plot >= 0 && f_held && (!f_over_scene || !hover.f_hit || hover.plot != paint_press_plot)){
        SubmitZone(ZONE_OP_BUILD_PAINT,paint_press_plot,paint_press_kind,paint_stroke,paint_press_owner);
        paint_press_plot = -1;
    }else if (paint_press_plot >= 0 && !f_held){
        SubmitZone(ZONE_OP_BUILD_ADD,paint_press_plot,paint_press_kind,paint_stroke,paint_press_owner);
        paint_press_plot = -1;
    }
    if (ChasmLineTool(paint_tool)){
        UpdateLineDrag(hover,f_over_scene,f_clicked,f_held);
        return;
    }
    //The debug map tools (water_tools_plan.md): a river's course, a pond's size, a height stroke or a cliff, placed on
    //release as a new map.
    if (ChasmMapTool(paint_tool)){
        UpdateMapDrag(hover,f_over_scene,f_clicked,f_held);
        return;
    }
    /*
        THE BRIDGE TOOL (bridge_plan.md): pressed on one bank, it remembers where; let go over the other,
        it asks for the whole bridge in one command. The view draws the chain meanwhile, green or red.
    */
    if (paint_tool == CHASM_TOOL_BRIDGE){
        if (f_clicked && f_over_scene && hover.f_hit){
            bridge_from = hover.plot;
            pick_version++;
        }else if (!f_held && bridge_from >= 0){
            if (f_over_scene && hover.f_hit && hover.plot != bridge_from){
                SubmitZone(ZONE_OP_BRIDGE,bridge_from,hover.plot,0);
            }
            bridge_from = -1;
            pick_version++;
        }
        return;
    }
    bridge_from = -1;
    if (DragTool(paint_tool)){
        UpdateAreaDrag(hover,f_over_scene,f_clicked,f_held);
        return;
    }
    if (!f_held){
        paint_last_index = -1;
    }
    if (!f_over_scene || !hover.f_hit || !(f_clicked || f_held)){
        return;
    }
    bool f_shift = input->IsKeyDown(INPUT_SHIFT);
    int tool = paint_tool;
    int index = ChasmCellTool(tool) ? hover.coarse_quad : hover.plot;
    //A winch, a tower, a mine is placed, never dragged (drag_plan.md): a click each, and holding on paints no more.
    if (!f_clicked && ZoneKindOnePlot(ToolKind(tool))){
        return;
    }
    if (!f_clicked && index == paint_last_index){
        return;     //still on what the drag already painted
    }
    //A press starts a stroke: every command the drag makes from here carries its number.
    if (f_clicked){
        paint_stroke = (paint_stroke + 1) & 0x3FFFFF;
    }
    paint_last_index = index;

    int op = ZONE_OP_NONE;
    int ground = ZONE_GROUND_NONE;
    if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT){
        ground = (tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : ZONE_GROUND_LOT;
        op = f_shift ? ZONE_OP_GROUND_ERASE : ZONE_OP_GROUND_PAINT;
    }else if (ToolKind(tool) != ZONE_KIND_NONE){
        std::shared_ptr<const ZoneState> z = GetZones();
        bool f_on_building = z && !z->storeys.empty() && z->storeys[hover.plot];
        if (f_shift){
            op = f_clicked ? ZONE_OP_BUILD_REMOVE : ZONE_OP_BUILD_ERASE;
        }else if (f_clicked && f_on_building){
            //A click or a drag's start - the release says which (the top of this function).
            paint_press_plot = index;
            paint_press_kind = ToolKind(tool);
            paint_press_owner = PaintOwner(tool);
            return;
        }else{
            op = f_clicked ? ZONE_OP_BUILD_ADD : ZONE_OP_BUILD_PAINT;
        }
        ground = ToolKind(tool);
    }else if (tool == CHASM_TOOL_FIELD){
        op = f_shift ? ZONE_OP_FIELD_ERASE : ZONE_OP_FIELD_PAINT;
    }else if (tool == CHASM_TOOL_RANGE || tool == CHASM_TOOL_DOCK){
        op = f_shift ? ZONE_OP_FIELD_ERASE : ChasmCellToolOp(tool);     //erased as a field is (range_plan.md)
    }else if (tool == CHASM_TOOL_CASTLE){
        //The debug castle (ramparts_plan.md): the panel's piece on every plot the drag reaches; shift takes one off.
        op = ZONE_OP_CASTLE;
        ground = f_shift ? 0 : ZoneCastlePiece(castle_shape,castle_storeys);
    }else if (tool == CHASM_TOOL_ERASE){
        std::shared_ptr<const ZoneState> z = GetZones();
        if (z && ZoneCastleShape(*z,hover.plot) != ZONE_CASTLE_NONE){
            op = ZONE_OP_CASTLE;    //the castle's stone first: an arch stands over the road under it
            ground = 0;
        }else if (z && !z->storeys.empty() && z->storeys[hover.plot]){
            op = ZONE_OP_BUILD_ERASE;
        }else if (z && !z->ground.empty() && z->ground[hover.plot]){
            op = ZONE_OP_GROUND_ERASE;
        }else if (z && !z->field.empty() && z->field[hover.coarse_quad]){
            op = ZONE_OP_FIELD_ERASE;
            index = hover.coarse_quad;
        }
    }
    if (op != ZONE_OP_NONE){
        //A garden or lot painted with a house or hut selected is its yard (yards_plan.md); a building painted
        //with one of its kind selected extends it.
        bool f_owned = op == ZONE_OP_GROUND_PAINT || op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD;
        SubmitZone(op,index,ground,paint_stroke,f_owned ? PaintOwner(tool) : 0);
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
        world_scene->AtTickBoundary([&](){
            while (zone_chunks.size() < n){
                Object* chunk = new Object();
                chunk->name = "Zones " + std::to_string(zone_chunks.size());
                chunk->SetMesh(new Mesh());
                chunk->SetMaterialSlot(0,palette_material);
                chunk->SetVisualOnly(true);
                chunk->SetVisibility(false);
                world_scene->AddObject(chunk);
                zone_chunks.push_back(chunk);
                surface_ids.push_back(chunk->GetID());     //pickable: the engine reads its surface (PickUnderMouse)
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
    /*
        The fields' growth (economy_plan.md, P3) is economy state, not the zones', so it moves no
        chunk_version: each field's look - its stage, and the crop sown on it (fields_plan.md) - is worked
        out here, and the chunks of a field whose look moved since it was drawn are rebuilt with the rest.
        An empty field is tilled earth. With no economy for this world every field is drawn ripe.
    */
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::vector<uint8_t> stage(z->buildings.size(),(uint8_t)FIELD_STAGE_RIPE);
    if (e && e->world == w){
        for (const EconomyField& f : e->fields){
            if (f.building < stage.size()){
                stage[f.building] = f.f_sown ? FieldLook(FieldStageOf(EconomyFieldGrowth(*e,*z,f.building)),f.crop)
                                             : FieldLook(FIELD_STAGE_BARE,-1);
            }
        }
    }
    std::vector<uint8_t> f_restage(n,0);
    bool f_any_restage = false;
    if (f_new_world){
        zone_field_stage.clear();
    }
    for (size_t id = 0; id < stage.size(); id++){
        //A field first seen here is drawn by its chunk_version anyway; marking it costs nothing more.
        if (id >= zone_field_stage.size() || zone_field_stage[id] != stage[id]){
            f_any_restage = f_any_restage || z->buildings[id].kind == ZONE_KIND_FIELD;
        }
    }
    if (f_any_restage){
        const Grid& g = *w->grid;
        for (size_t c = 0; c < z->field.size(); c++){
            uint32_t id = z->field[c];
            if (id && (id >= zone_field_stage.size() || zone_field_stage[id] != stage[id])){
                for (int k = 0; k < 4; k++){
                    int chunk = TerrainChunkOfQuad(g,*w->mesh,(int)c * 4 + k);
                    if (chunk >= 0 && chunk < (int)n){
                        f_restage[chunk] = 1;
                    }
                }
            }
        }
    }
    zone_field_stage = stage;
    /*
        What the zombies have done (zombies_plan.md, step 4d) is economy state too, drawn the same way: its look in steps
        (ZoneMesh.h), and the chunks under whatever moved a step rebuilt with the rest.
    */
    if (f_new_world){
        zone_harm_look = ZoneHarmLook();
    }
    ZoneHarmLook harm;
    if (e && e->world == w){
        ZoneHarmLookOf(*e,&zone_harm_look,harm);
    }
    ZoneHarmChunks(*w,*z,zone_harm_look,harm,f_restage);
    zone_harm_look = harm;
    std::vector<vertex> verts;
    for (size_t i = 0; i < n; i++){
        uint32_t v = z->chunk_version[i];
        //Built at a version (not "changed" flags), so nothing is lost if two changes land between
        //frames. A chunk never painted sits at 0 and needs no mesh.
        if (!f_restage[i] && (zone_chunk_built[i] == v || (v == 0 && zone_chunk_built[i] == 0xFFFFFFFFu))){
            continue;
        }
        zone_chunk_built[i] = v;
        BuildZoneChunk(*w,*z,(int)i,verts,&zone_field_stage,&zone_harm_look);
        if (verts.empty()){
            zone_chunks[i]->SetVisibility(false);
        }else{
            zone_chunks[i]->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            UseGroundShader(zone_chunks[i]->GetMesh());     //after: SetMeshData resets the mode
            zone_chunks[i]->SetVisibility(true);
        }
    }
}

//--- The forest (step 7) ---------------------------------------------------------------------------

//RENDER THREAD, at Init: the modelled props, one asset per node of chasm_props.glb.
void ApplicationChasm::LoadProps(){
    gltfloader.LoadGLTFFile("meshes/chasm_props.glb");
    std::vector<std::string> names(prop_asset_names,prop_asset_names + PROP_KIND_COUNT);
    names.insert(names.end(),person_asset_names,person_asset_names + PERSON_FIGURES);
    names.insert(names.end(),gear_asset_names,gear_asset_names + GEAR_ASSETS);
    names.insert(names.end(),forest_bare_names,forest_bare_names + FOREST_OAK_KINDS);
    names.insert(names.end(),forest_fruit_names,forest_fruit_names + FOREST_FRUIT_KINDS);
    names.push_back(CHASM_CRATE_ASSET);
    names.push_back(CHASM_RUBBLE_ASSET);
    names.push_back(CHASM_CRYSTAL_ASSET);
    for (const char* mine : {CHASM_MINE_ASSETS}){
        names.push_back(mine);      //the mine's props (stone_plan.md step 5)
    }
    names.push_back(CHASM_ZOMBIE_ASSET);
    names.push_back(CHASM_ANGEL_ASSET);      //zombies_plan.md step 6: rising from where a person sank
    names.push_back(CHASM_BIRD_ASSET);       //birds_plan.md: its shape keys come with it
    names.insert(names.end(),range_asset_names,range_asset_names + RANGE_PROPS);
    names.insert(names.end(),dock_asset_names,dock_asset_names + DOCK_PROPS);    //steam_plan.md step 5: the zeppelin
    GetAssetsFromGLTF(names);
    int loaded = 0;
    for (int k = 0; k < PROP_KIND_COUNT; k++){
        //Asked of the mesh, which makes no copy - an Object from GetObjectFromAsset is a new one.
        Mesh* mesh = assetmanager->GetMeshFromAsset(prop_asset_names[k]);
        f_props_loaded[k] = (mesh != NULL);
        if (f_props_loaded[k]){
            //Every set of this kind shares the asset's mesh, so tagging it once puts them all on it.
            UseGroundShader(mesh);
            loaded++;
        }else{
            debug->Warn("Prop '%s' not in chasm_props.glb - it will not be placed\n",prop_asset_names[k]);
        }
    }
    debug->Info("Props: %i of %i kinds loaded\n",loaded,PROP_KIND_COUNT);
    //A ripe plant's fruit (foraging_plan.md): without its mesh in the file, the plant is drawn bare of it.
    for (int f = 0; f < FOREST_FRUIT_KINDS; f++){
        Mesh* mesh = assetmanager->GetMeshFromAsset(forest_fruit_names[f]);
        f_fruit_loaded[f] = (mesh != NULL);
        if (mesh){
            UseGroundShader(mesh);
        }else{
            debug->Warn("'%s' not in chasm_props.glb - ripe plants will not show their fruit\n",forest_fruit_names[f]);
        }
    }
    //The crate (crates_plan.md) is in the same file, though no forest grows it.
    Mesh* crate = assetmanager->GetMeshFromAsset(CHASM_CRATE_ASSET);
    f_crate_loaded = (crate != NULL);
    if (crate){
        UseGroundShader(crate);
    }else{
        debug->Warn("'%s' not in chasm_props.glb - crates will not be drawn\n",CHASM_CRATE_ASSET);
    }
    //Rubble (stone_plan.md): its own pile once modelled; until then the rock cluster, shrunk to one.
    if (Mesh* rubble = assetmanager->GetMeshFromAsset(CHASM_RUBBLE_ASSET)){
        UseGroundShader(rubble);
        rubble_asset = CHASM_RUBBLE_ASSET;
        rubble_scale = 1.0f;
    }else if (f_props_loaded[PROP_ROCK_CLUSTER]){
        debug->Info("'%s' not in chasm_props.glb - rubble is drawn as a small rock cluster\n",CHASM_RUBBLE_ASSET);
        rubble_asset = prop_asset_names[PROP_ROCK_CLUSTER];
        rubble_scale = 0.35f;
    }
    //...and a seam's crystal lying (EconomyCrateCrystal); without its heap, drawn as the rubble.
    if (Mesh* crystal = assetmanager->GetMeshFromAsset(CHASM_CRYSTAL_ASSET)){
        UseGroundShader(crystal);
        crystal_asset = CHASM_CRYSTAL_ASSET;
    }else{
        debug->Info("'%s' not in chasm_props.glb - floatstone lying is drawn as rubble\n",CHASM_CRYSTAL_ASSET);
    }
    //...and the zombie (zombies_plan.md): a figure, so drawn as the people are, not by the ground's shader - no snow
    //settles on someone walking - and skinned when it has a rig.
    f_zombie_loaded = assetmanager->GetMeshFromAsset(CHASM_ZOMBIE_ASSET) != NULL;
    if (f_zombie_loaded){
        BuildZombieSkin();
    }else{
        debug->Warn("'%s' not in chasm_props.glb - zombies will not be drawn\n",CHASM_ZOMBIE_ASSET);
    }
    //...and the angel (step 6), drawn as a figure too.
    f_angel_loaded = assetmanager->GetMeshFromAsset(CHASM_ANGEL_ASSET) != NULL;
    if (!f_angel_loaded){
        debug->Warn("'%s' not in chasm_props.glb - no angel rises from the dead\n",CHASM_ANGEL_ASSET);
    }
    //The archery range's props (range_plan.md): the GLB's where it has them, stand-ins where not.
    BuildRangeScene();
    BuildDockScene();
    BuildSeasonLooks();
    //The people's figures are in the same file (ApplicationChasmWorkersView.cpp), so they are made here.
    BuildWorkerScene();
    BuildCrowdBench();
    BuildBirdScene();
}

/*
    A prop gives way to what is BUILT (EconomyPropCovered): a house's standing storey on its plot, a garden or
    lot built on it, a built field over its cell - and to a LAID road near it (RoadNear: a tree a plot away from
    a road still leans over it). A site's trees stand, as a planned road's do, until a woodcutter fells them
    (woodcutting_plan.md). `f_by_road`, if given, says it was the road alone - a felled tree's log may still
    lie beside one.
*/
bool ApplicationChasm::PropHidden(const PropInstance& p, const ZoneState* z, bool* f_by_road){
    if (f_by_road){
        *f_by_road = false;
    }
    if (!z){
        return false;
    }
    //An outcrop under its mine (stone_plan.md step 5) stays in sight: the mine is a pit in it, its crane beside.
    if (p.kind == PROP_OUTCROP && p.plot >= 0 && z->KindOf(p.plot) == ZONE_KIND_MINE){
        return false;
    }
    if (EconomyPropCovered(*z,p.plot,p.coarse)){
        return true;
    }
    if (z->grounds > 0 && RoadNear(*z->world,*z,p.plot,vec2(p.pos.x,p.pos.z),ROAD_PROP_CLEARANCE)){
        if (f_by_road){
            *f_by_road = true;
        }
        return true;
    }
    return false;
}

#define FELLED_LOG_OFFSET   1.1f    //a felled tree's log lies this far from its stump, at the stump's scale
#define FELLED_LOG_TURN     0.5f    //and this far round from the way the tree faced, radians
#define FELLED_LOG_PULL     0.6f    //beside a road: dragged out from its stump in steps of this
#define FELLED_LOG_CLEAR    0.7f    //until this far from the road's centre line - half its width and the log's

//A prop's PROP_STATE_* in the economy, standing when there is none for it.
static uint8_t PropStateOf(const EconomyState* e, size_t i){
    return (e && i < e->prop_state.size()) ? e->prop_state[i] : (uint8_t)PROP_STATE_STANDING;
}

/*
    RENDER THREAD. One chunk's sets, each kind's visible props as transforms. A tree the economy has
    felled (economy_plan.md, P2) is drawn as the stump it left - and while its log still lies there,
    the log beside it, the way the tree fell. Both at the tree's own scale - well under a forest
    stump's, which reads right: a forest stump is an old giant's, these are the trunks he cuts.
    Beside a laid road (line_works_plan.md: the trees felled to clear it) the stump is under the road's
    verge and gives way to it, but the log is wood still lying there, so it is drawn - fallen away from
    the road rather than across it.
*/
void ApplicationChasm::RebuildPropChunk(const ChasmWorld& w, const ZoneState* z, const EconomyState* e, int chunk){
    const ForestData& f = *w.forest;
    const int set_count = ForestSetCount();
    std::vector<std::vector<fmat4>> sets(set_count);
    uint64_t tick = calendar_shown.load();
    for (int i : f.chunk_props[chunk]){
        const PropInstance& p = f.props[i];
        if (!f_forest_flowers && (p.kind == PROP_FLOWERS_A || p.kind == PROP_FLOWERS_B)){
            continue;
        }
        //A broadleaf standing is drawn in its stage's set (Seasons.h); the stage is kept, to see it move.
        int set = p.kind;
        int stage = FOREST_STAGE_GREEN;
        if (ForestSeasonal(p.kind) != FOREST_SEASONAL_NONE){
            stage = ForestStage(p.kind,(size_t)i,tick);
            forest_prop_stage[i] = (uint8_t)stage;
            set = season_look_set[p.kind][stage];
        }
        bool f_by_road = false;
        bool f_hidden = PropHidden(p,z,&f_by_road);
        uint8_t state = PropStateOf(e,(size_t)i);
        if (f_hidden && !(f_by_road && state == PROP_STATE_LOG)){
            continue;
        }
        if (state == PROP_STATE_STANDING){
            fmat4 m = Object::ComposeTransformScale(p.pos,quat(vec3(0.0f,1.0f,0.0f),p.yaw),vec3(p.scale,p.scale,p.scale));
            sets[set].push_back(m);
            //Ripe (foraging_plan.md): its fruit over it, where it is.
            int fruit = ForestFruitMesh(p.kind);
            if (fruit >= 0 && f_fruit_loaded[fruit] && e && EconomyFruitOn(*e,i) > 0 && ForestFruitShows(stage)){
                sets[ForestFruitBase() + fruit].push_back(m);
            }
            continue;
        }
        //A rock broken (stone_plan.md) leaves no stump: its stone lies there as rubble, which the crates draw.
        if (!ForestIsTree(p.kind)){
            continue;
        }
        float s = p.scale;
        if (!f_hidden){
            sets[PROP_STUMP].push_back(Object::ComposeTransformScale(p.pos,quat(vec3(0.0f,1.0f,0.0f),p.yaw),
                                                                     vec3(s,s,s)));
        }
        if (state == PROP_STATE_LOG){
            float yaw = p.yaw + FELLED_LOG_TURN;
            vec2 at = vec2(p.pos.x,p.pos.z) + vec2(std::sin(yaw),std::cos(yaw)) * (FELLED_LOG_OFFSET * s);
            /*
                Felled to clear a road: it lies off the road, not across it. The log lies the way the tree
                fell, its near end at the stump - which beside a road is in the road's verge - so where the
                way it fell puts it on the road it is dragged out along that line as far as it takes, the
                least first, and of the eight ways round the stump (from the way it faced) the first that
                puts its near end and its middle clear of the road's surface. Any log, not only a hidden
                stump's: the economy fells a little wider than the view hides (ECONOMY_ROAD_VERGE).
            */
            vec2 foot(p.pos.x,p.pos.z);
            auto clear = [&](const vec2& q){
                GridPick under = w.picker->Pick(q);
                return !under.f_hit || !RoadNear(w,*z,under.plot,q,FELLED_LOG_CLEAR,true);
            };
            if (z && z->grounds > 0 && (!clear(foot) || !clear(at))){
                float half = FELLED_LOG_OFFSET * s;
                bool f_placed = false;
                for (int pull = 0; pull < 4 && !f_placed; pull++){
                    for (int turn = 0; turn < 8 && !f_placed; turn++){
                        float y_try = p.yaw + FELLED_LOG_TURN + (float)turn * 0.7853982f;
                        vec2 dir(std::sin(y_try),std::cos(y_try));
                        vec2 near_end = foot + dir * ((float)pull * FELLED_LOG_PULL);
                        if (clear(near_end) && clear(near_end + dir * half)){
                            yaw = y_try;
                            at = near_end + dir * half;
                            f_placed = true;
                        }
                    }
                }
            }
            float y = (p.plot >= 0) ? w.terrain->GroundHeight(at,w.terrain->Height(p.plot)) : p.pos.y;
            sets[PROP_LOG].push_back(Object::ComposeTransformScale(vec3(at.x,y,at.y),quat(vec3(0.0f,1.0f,0.0f),yaw),
                                                                   vec3(s,s,s)));
        }
    }
    bool f_show = f_view_forest;
    for (int k = 0; k < set_count; k++){
        Object* o = prop_sets[(size_t)chunk * set_count + k];
        if (!o){
            continue;
        }
        bool f_any = !sets[k].empty();
        o->SetInstances(std::move(sets[k]));
        o->SetVisibility(f_show && f_any);
    }
}

/*
    RENDER THREAD. A new map fills every chunk's sets (making more only if it has more chunks than
    any map before it); after that only the chunks whose zones moved are rebuilt, and the view
    toggle shows or hides them all.
*/
void ApplicationChasm::UploadForest(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w || !w->forest){
        return;
    }
    const ForestData& f = *w->forest;
    std::shared_ptr<const ZoneState> zs = GetZones();
    const ZoneState* z = (zs && zs->world == w) ? zs.get() : NULL;
    std::shared_ptr<const EconomyState> es = GetEconomy();
    const EconomyState* e = (es && es->world == w) ? es.get() : NULL;
    bool f_show = f_view_forest;
    size_t chunks = f.chunk_props.size();
    //Flowers in spring and summer only: a change of season that brings or takes them rebuilds every chunk.
    int season = CalendarDateOf(calendar_shown.load()).season;
    bool f_flowers = (season == SEASON_SPRING || season == SEASON_SUMMER);
    bool f_flowers_moved = (f_flowers != f_forest_flowers);
    f_forest_flowers = f_flowers;

    if (w != forest_built_world){
        forest_built_world = w;
        const int set_count = ForestSetCount();
        size_t need = chunks * set_count;
        if (prop_sets.size() < need){
            //New objects join the scene the physics thread walks - under its lock.
            world_scene->AtTickBoundary([&](){
                while (prop_sets.size() < need){
                    int s = (int)(prop_sets.size() % set_count);
                    //A kind's own set, from its asset; or a broadleaf's stage, on its look's mesh; or a plant's fruit.
                    int fruit = s - ForestFruitBase();
                    int k = (s < PROP_KIND_COUNT) ? s : (fruit < 0) ? season_look_kind[s - PROP_KIND_COUNT] : PROP_BUSH_A;
                    Object* o = NULL;
                    if (s < PROP_KIND_COUNT){
                        o = f_props_loaded[k] ? assetmanager->GetObjectFromAsset(prop_asset_names[k]) : NULL;
                    }else if (fruit >= 0){
                        o = f_fruit_loaded[fruit] ? assetmanager->GetObjectFromAsset(forest_fruit_names[fruit]) : NULL;
                    }else{
                        o = new Object();
                        o->SetMesh(season_look_mesh[s - PROP_KIND_COUNT]);
                    }
                    if (o){
                        o->name = (fruit >= 0) ? std::string(forest_fruit_names[fruit]) + " set"
                                : std::string(prop_asset_names[k]) +
                                  ((s < PROP_KIND_COUNT) ? "" : std::string(" ") + ForestStageName(season_look_stage[s - PROP_KIND_COUNT])) + " set";
                        /*
                            The GLB names a material it carries no image for; the palette is ours - a swaying kind's
                            copy of it (ApplicationChasmWeather.cpp), and its fruit its plant's, so the two move as one.
                        */
                        int sways = k;
                        for (int plant = 0; fruit >= 0 && plant < PROP_KIND_COUNT; plant++){
                            if (ForestFruitMesh(plant) == fruit){
                                sways = plant;
                                break;
                            }
                        }
                        o->SetMaterialSlot(0,PropMaterial(sways));
                        o->SetPickability(false);
                        o->SetVisualOnly(true);
                        //A plant's fruit is a few berries: no shadow worth the pass, as ground cover's.
                        o->SetCastsShadow(fruit < 0 && PropCastsShadow(k));
                        o->SetInstances(std::vector<fmat4>());
                        o->SetVisibility(false);
                        world_scene->AddObject(o);
                    }
                    prop_sets.push_back(o);
                }
            });
        }
        for (size_t i = need; i < prop_sets.size(); i++){
            if (prop_sets[i]){
                prop_sets[i]->SetInstances(std::vector<fmat4>());
                prop_sets[i]->SetVisibility(false);
            }
        }
        forest_zone_version.assign(chunks,0);
        forest_prop_chunk.assign(f.props.size(),0);
        forest_prop_stage.assign(f.props.size(),0xFF);
        forest_stage_clock = -1;
        for (size_t c = 0; c < chunks; c++){
            for (int i : f.chunk_props[c]){
                forest_prop_chunk[i] = (int)c;
            }
        }
        for (size_t c = 0; c < chunks; c++){
            RebuildPropChunk(*w,z,e,(int)c);
            forest_zone_version[c] = z ? z->chunk_version[c] : 0;
        }
        forest_prop_state = e ? e->prop_state : std::vector<uint8_t>();
        forest_felled_version = e ? e->felled_version : 0xFFFFFFFFu;
        forest_fruit.assign(f.props.size(),0);
        for (size_t i = 0; e && i < f.props.size(); i++){
            forest_fruit[i] = EconomyFruitOn(*e,(int)i) > 0 ? 1 : 0;
        }
        forest_fruit_version = e ? e->fruit_version : 0xFFFFFFFFu;
        f_forest_shown = f_show;
        return;
    }

    /*
        The felled trees: when the economy's felled_version moves, the props whose state differs
        from what the sets were built with name the chunks to rebuild - a tree felled rebuilds its
        own chunk, not the forest. Compared prop by prop rather than trusting the version alone, so an
        economy that comes or goes (a load, a new game) is handled the same way.
    */
    std::vector<uint8_t> f_felled(chunks,0);
    uint32_t felled_version = e ? e->felled_version : 0xFFFFFFFFu;
    if (felled_version != forest_felled_version){
        forest_felled_version = felled_version;
        size_t count = f.props.size();
        for (size_t i = 0; i < count; i++){
            uint8_t was = (i < forest_prop_state.size()) ? forest_prop_state[i] : (uint8_t)PROP_STATE_STANDING;
            if (was != PropStateOf(e,i)){
                f_felled[forest_prop_chunk[i]] = 1;
            }
        }
        forest_prop_state = e ? e->prop_state : std::vector<uint8_t>();
    }
    //...and the plants ripe or picked bare (foraging_plan.md) the same way, by the economy's fruit_version.
    uint32_t fruit_version = e ? e->fruit_version : 0xFFFFFFFFu;
    if (fruit_version != forest_fruit_version){
        forest_fruit_version = fruit_version;
        forest_fruit.resize(f.props.size(),0);
        for (size_t i = 0; i < f.props.size(); i++){
            uint8_t ripe = (e && EconomyFruitOn(*e,(int)i) > 0) ? 1 : 0;
            if (ripe != forest_fruit[i]){
                forest_fruit[i] = ripe;
                f_felled[forest_prop_chunk[i]] = 1;
            }
        }
    }
    //The broadleaves whose stage has moved on since their chunk was built (ApplicationChasmSeasons.cpp).
    std::vector<uint8_t> f_staged(chunks,0);
    ForestStagesMoved(f,f_staged);

    bool f_toggle = (f_show != f_forest_shown) || f_flowers_moved;
    f_forest_shown = f_show;
    for (size_t c = 0; c < chunks; c++){
        uint32_t version = z ? z->chunk_version[c] : 0;
        if (!f_toggle && !f_felled[c] && !f_staged[c] && forest_zone_version[c] == version){
            continue;
        }
        forest_zone_version[c] = version;
        RebuildPropChunk(*w,z,e,(int)c);
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
    const char* biome = (w && w->picker.get() == &p) ? TerrainBiomeName(w->terrain->biome[pick.plot]) : "?";
    //The plot's ground as the walkers judge it: its height, and whether it is wet (a river or a pool).
    float ground = (w && w->picker.get() == &p) ? w->terrain->ground[pick.plot] : 0.0f;
    bool f_wet = (w && w->picker.get() == &p) && w->terrain->wet[pick.plot];
    //Whose land it is (settlements_plan.md): the settlement's id, 0 nobody's.
    std::shared_ptr<const EconomyState> e = GetEconomy();
    uint32_t settled = (e && w && e->world == w && pick.plot < (int)e->settlement.size()) ? e->settlement[pick.plot] : 0;
    return json{
        {"hit",true},
        {"x",pick.at.x},
        {"z",pick.at.y},
        {"plot",{{"vertex",pick.plot},{"x",v.x},{"z",v.y},{"level",level},{"biome",biome},{"sides",p.PlotQuadCount(pick.plot)},
                 {"area",p.PlotArea(pick.plot)},{"on_outline",(bool)g.fine.f_boundary[pick.plot]},
                 {"coarse_corner",pick.plot < (int)g.coarse.pos.size()},{"ground",ground},{"wet",f_wet},
                 {"settlement",settled}}},
        {"fine_cell",{{"quad",pick.fine_quad},{"corner",pick.corner},{"area",p.FineArea(pick.fine_quad)},
                      {"squareness",GridQuadSquareness(corners)}}},
        {"coarse_cell",{{"quad",pick.coarse_quad},{"area",p.CoarseArea(pick.coarse_quad)}}},
        {"building",PickBuildingJson(pick)}
    };
}

//The building on the picked plot, or the field the picked coarse cell is part of - or null.
json ApplicationChasm::PickBuildingJson(const GridPick& pick){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !z || z->world != w || !pick.f_hit){
        return nullptr;
    }
    uint32_t id = z->building[pick.plot];
    return BuildingJson(*w,*z,id ? id : z->field[pick.coarse_quad]);
}

json ApplicationChasm::BuildingJson(const ChasmWorld& w, const ZoneState& z, uint32_t id){
    ZoneBuildingInfo b = ZoneBuildingFigures(w,z,id);
    if (b.id == 0){
        return nullptr;
    }
    json j{{"id",b.id},{"kind",ZoneKindName(b.kind)},{"size",b.size},{"area",b.floor_area}};
    if (b.kind == ZONE_KIND_FIELD){
        j["crop"] = ZoneCropName(b.crop);           //its starting crop, "none" until chosen
        j["next"] = ZoneCropName(b.next_crop);      //a crop, or "auto"
        j["site_cells"] = b.site_cells;
    }else if (b.kind == ZONE_KIND_RANGE || b.kind == ZONE_KIND_DOCK){
        j["site_cells"] = b.site_cells;             //range_plan.md, steam_plan.md step 5: cells still a site
    }else{
        j["storeys"] = b.storeys;
    }
    //A guard tower (defence_plan.md): its junction, the guards that gives it, and its ladder - its door.
    if (b.kind == ZONE_KIND_TOWER){
        for (size_t v = 0; v < z.building.size(); v++){
            if (z.building[v] == id){
                int ladder = ZoneTowerLadder(z,(int)v);
                j["junction"] = w.picker->PlotQuadCount((int)v);
                j["guards"] = ZoneTowerGuards(w,(int)v);
                j["ladder"] = (ladder >= 0) ? json{{"plot",ladder},{"x",w.grid->fine.pos[ladder].x},{"z",w.grid->fine.pos[ladder].y}}
                                            : json(nullptr);
                break;
            }
        }
    }
    //Its yard (yards_plan.md): the gardens and lots it owns, by kind.
    if (ZoneKindHasYard(b.kind)){
        j["yard"] = json{{"gardens",ZoneYardPlots(z,id,ZONE_GROUND_GARDEN)},{"lots",ZoneYardPlots(z,id,ZONE_GROUND_LOT)}};
    }
    j["economy"] = EconomyBuildingJson(id);
    return j;
}

/*
    The building under the cursor - or, for a field, the one selected - in the panel: what it is, how
    big, and for a field its crop, which can be changed there for the whole field.
*/
void ApplicationChasm::RenderBuildingInfo(const ChasmWorld& w, const ZoneState& z){
#ifdef USE_IMGUI
    GridPick hover = GetHoverPick();
    GridPick selected = GetSelectedPick();
    uint32_t id = 0;
    if (hover.f_hit){
        id = z.building[hover.plot] ? z.building[hover.plot] : z.field[hover.coarse_quad];
    }
    ZoneBuildingInfo b = ZoneBuildingFigures(w,z,id);
    if (b.id){
        if (b.kind == ZONE_KIND_FIELD){
            ImGui::Text("under the cursor: field %u, %i cells, %.0f units, %s",b.id,b.size,b.floor_area,
                        ZoneCropName(b.crop));
        }else if (b.kind == ZONE_KIND_RANGE){
            ImGui::Text("under the cursor: range %u, %i cells, %.0f units",b.id,b.size,b.floor_area);
        }else{
            ImGui::Text("under the cursor: %s %u, %i plots, %i storeys in all, %.0f units of floor",
                        ZoneKindName(b.kind),b.id,b.size,b.storeys,b.floor_area);
        }
        RenderEconomyInfo(b.id);
    }else{
        ImGui::TextDisabled("under the cursor: no building");
    }
    RenderSelectedStore(selected.f_hit ? selected.plot : -1);
    RenderSelectedCrate(selected.f_hit ? selected.plot : -1);
    uint32_t field = selected.f_hit ? z.field[selected.coarse_quad] : 0;
    ZoneBuildingInfo f = ZoneBuildingFigures(w,z,field);
    if (f.id){
        //Its two choices (fields_plan.md): the crop it starts with, and the next one, with the yield each gets.
        std::shared_ptr<const EconomyState> e = GetEconomy();
        const EconomyField* rec = (e && e->world == z.world) ? EconomyFieldOf(*e,f.id) : NULL;
        int last = rec ? rec->last : -1;
        ImGui::Text("selected field %u starts with:",f.id);
        for (int c = 0; c < ZONE_CROP_COUNT; c++){
            ImGui::SameLine();
            std::string label = std::string(ZoneCropName(c)) + "##crop";
            if (ImGui::RadioButton(label.c_str(),f.crop == c) && f.crop != c){
                SubmitZone(ZONE_OP_FIELD_CROP,selected.coarse_quad,c);
            }
        }
        //...or AUTO, as NEEDS ATTENTION's countdown sets it (issues_plan.md).
        ImGui::SameLine();
        if (ImGui::RadioButton("auto##crop",f.crop == ZONE_CROP_AUTO) && f.crop != ZONE_CROP_AUTO){
            SubmitZone(ZONE_OP_FIELD_CROP,selected.coarse_quad,ZONE_CROP_AUTO);
        }
        ImGui::Text("  next:");
        ImGui::SameLine();
        std::string auto_label = std::string("auto (") + ZoneCropName(EconomyAutoCrop(last)) + ")##next";
        if (ImGui::RadioButton(auto_label.c_str(),f.next_crop == ZONE_CROP_AUTO) && f.next_crop != ZONE_CROP_AUTO){
            SubmitZone(ZONE_OP_FIELD_NEXT,selected.coarse_quad,ZONE_CROP_AUTO);
        }
        for (int c = 0; c < ZONE_CROP_COUNT; c++){
            ImGui::SameLine();
            char label[48];
            snprintf(label,sizeof(label),"%s %i%%##next",ZoneCropName(c),EconomyCropYield(last,c));
            if (ImGui::RadioButton(label,f.next_crop == c) && f.next_crop != c){
                SubmitZone(ZONE_OP_FIELD_NEXT,selected.coarse_quad,c);
            }
        }
    }else{
        ImGui::TextDisabled("select a field (Select tool, click) to choose its crop");
    }
#else
    (void)w;
    (void)z;
#endif
}

//The building kind a paint tool builds, or ZONE_KIND_NONE for the others.
int ApplicationChasm::ToolKind(int tool){
    switch (tool){
        case CHASM_TOOL_HOUSE:      return ZONE_KIND_HOUSE;
        case CHASM_TOOL_STORE:      return ZONE_KIND_STORE;
        case CHASM_TOOL_WOODCUTTER: return ZONE_KIND_WOODCUTTER;
        case CHASM_TOOL_WATER:      return ZONE_KIND_WATER;
        case CHASM_TOOL_WINCH:      return ZONE_KIND_WINCH;
        case CHASM_TOOL_CAMP:       return ZONE_KIND_CAMP;
        case CHASM_TOOL_BRIDGE:     return ZONE_KIND_BRIDGE;
        case CHASM_TOOL_BUILDERS:   return ZONE_KIND_BUILDERS;
        case CHASM_TOOL_TOWER:      return ZONE_KIND_TOWER;
        case CHASM_TOOL_ROPEWALK:   return ZONE_KIND_ROPEWALK;
        case CHASM_TOOL_STONECUTTER: return ZONE_KIND_STONECUTTER;
        case CHASM_TOOL_MINE:       return ZONE_KIND_MINE;
        case CHASM_TOOL_WELL:       return ZONE_KIND_WELL;
        case CHASM_TOOL_STEAM:      return ZONE_KIND_STEAM;
        case CHASM_TOOL_CONDENSER:  return ZONE_KIND_CONDENSER;
        case CHASM_TOOL_SAIL_LOFT:  return ZONE_KIND_SAIL_LOFT;
        default:                    return ZONE_KIND_NONE;
    }
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
    /*
        Play mode selects by outline (selection_plan.md) and shows a paint tool as a GHOST of what it would
        place (UpdateGhost) - a bridge's chain too - so the line mesh is debug mode's alone. Checked every
        frame, since the mode does not move the pick; drawn again whole once it is back.
    */
    if (PlayMode()){
        pick_view->SetVisibility(false);
        pick_view_built_version = -1;
        return;
    }
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
    /*
        Everything on the ground of the plot it is about: a pick on the floor is drawn on the floor,
        one on a hill on the hill (Terrain::GroundHeight; the object itself sits PICK_VIEW_Y over
        it). A segment longer than a unit is cut into pieces each laid on the ground, so a straight
        line between two points on a slope cannot pass under the hill between them.
    */
    const Terrain& terrain = *w->terrain;
    float level_h = 0.0f;
    //Up on a deck (ramparts_plan.md): flat at its height instead - the plot the cursor is on the castle's top of.
    bool f_on_deck = false;
    float deck_h = 0.0f;
    /*
        On a TERRACE (terraces_plan.md): on its top, not the ground buried under it - as on the castle's deck. `ground_plot`
        names the plot an outline is round, whose top its edge lies on - a point on the edge between a terrace and the
        ground below it is either's; with none, each point's own plot.
    */
    std::shared_ptr<const ZoneState> zones_on = GetZones();
    const ZoneState* zg = (zones_on && zones_on->world == w) ? zones_on.get() : NULL;
    const GridPicker* picker_on = w->picker.get();
    int ground_plot = -1;
    auto emit = [&verts,&segs,&level_h,&terrain,&f_on_deck,&deck_h,zg,picker_on,&ground_plot](uint32_t color){
        line_vertex v;
        v.color = color;
        auto put = [&](const vec2& p){
            float y = terrain.GroundHeight(p,level_h);
            if (zg){
                int under = ground_plot;
                if (under < 0){
                    GridPick pk = picker_on->Pick(p);
                    under = pk.f_hit ? pk.plot : -1;
                }
                if (ZoneTerraceStands(*zg,under)){
                    y = ZoneTerraceTop(*zg,under);
                }
            }
            v.pos = vec3(p.x,f_on_deck ? deck_h : y,p.y);
            verts.push_back(v);
        };
        for (size_t i = 0; i + 1 < segs.size(); i += 2){
            vec2 a = segs[i];
            vec2 b = segs[i + 1];
            int pieces = std::max(1,(int)std::ceil((b - a).length()));
            for (int k = 0; k < pieces; k++){
                put(a + (b - a) * ((float)k / pieces));
                put(a + (b - a) * ((float)(k + 1) / pieces));
            }
        }
        segs.clear();
    };

    if (selected.f_hit){
        level_h = terrain.Height(selected.plot);
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
        ground_plot = selected.plot;
        emit(PICK_SELECTED_PLOT);
        ground_plot = -1;
    }
    //Drawing a road or wall (line_works_plan.md): the chain so far, each plot green where the line may take it.
    std::vector<int> chain;
    bool f_erase = false;
    int ground = ZONE_GROUND_ROAD;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        chain = line_chain;
        f_erase = f_line_erase;
        ground = line_ground;
    }
    std::shared_ptr<const ZoneState> line_zones = GetZones();
    if (!chain.empty() && line_zones && line_zones->world == w){
        std::shared_ptr<const Exploration> ex = GetExploration();
        for (int v : chain){
            level_h = terrain.Height(v);
            p->PlotOutline(v,segs);
            emit(LinePlotAllowed(*w,*line_zones,ex.get(),f_erase,ground,v,NULL) ? PICK_PAINT_OK : PICK_PAINT_REFUSED);
        }
    }
    /*
        A map tool's drag (water_tools_plan.md), on the plateau's ground: a river's middle and its two edges at the
        panel's width; a height stroke's line and its reach at the panel's radius, green raising and blue lowering; a
        pond's water's edge and its bank, green - red where it would not fit (ChasmPondFits).
    */
    std::vector<vec2> drag;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        drag = map_drag;
    }
    int map_tool = paint_tool;
    if (!drag.empty() && (map_tool == CHASM_TOOL_RIVER || map_tool == CHASM_TOOL_POND || map_tool == CHASM_TOOL_HEIGHT)){
        level_h = terrain_levels[TERRAIN_PLATEAU].height;
        uint32_t colour = PICK_PAINT_OK;
        const int sides = 48;
        auto circle = [&](const vec2& c, float r){
            for (int k = 0; k < sides; k++){
                float a0 = 6.2831853f * (float)k / sides;
                float a1 = 6.2831853f * (float)(k + 1) / sides;
                segs.push_back(c + vec2(std::cos(a0),std::sin(a0)) * r);
                segs.push_back(c + vec2(std::cos(a1),std::sin(a1)) * r);
            }
        };
        if (map_tool == CHASM_TOOL_RIVER || map_tool == CHASM_TOOL_HEIGHT){
            bool f_river = map_tool == CHASM_TOOL_RIVER;
            float half = f_river ? water_river_width.load() * 0.5f : relief_radius.load();
            for (size_t i = 0; i + 1 < drag.size(); i++){
                vec2 d = drag[i + 1] - drag[i];
                float l = d.length();
                if (l < 1e-4f){
                    continue;
                }
                vec2 side(-d.y / l * half,d.x / l * half);
                for (float s : {0.0f,1.0f,-1.0f}){
                    segs.push_back(drag[i] + side * s);
                    segs.push_back(drag[i + 1] + side * s);
                }
            }
            if (!f_river){
                circle(drag.front(),half);
                circle(drag.back(),half);
                colour = (relief_height.load() < 0.0f) ? PICK_HOVER_COARSE : PICK_PAINT_OK;
            }
        }else if (drag.size() == 2){
            float radius = std::min(GRID_PLACED_POND_LARGEST,(drag[1] - drag[0]).length());
            bool f_ok = radius >= GRID_PLACED_POND_SMALLEST && ChasmPondFits(g.layout,drag[0],radius,g.bounds_min,g.bounds_max);
            circle(drag[0],radius);
            circle(drag[0],radius + GRID_POND_BANK);
            colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }
        emit(colour);
    }
    /*
        The CLIFF tool (step 6): during its drag the plots the stroke would paint - blue going down, green taken back up
        at a depth of 0, red where it would not take them (TerrainCliffPlots, CliffPlotTaken); before the press the same
        for a stroke of the one plot under the cursor, so the width shows as a brush.
    */
    if (map_tool == CHASM_TOOL_CLIFF){
        std::vector<int> band;
        {
            std::lock_guard<std::mutex> lock(pick_mutex);
            band = cliff_band;
        }
        int depth = cliff_depth.load();
        if (band.empty() && hover.f_hit && hover.plot >= 0){
            GridCliffStroke s;
            s.points.push_back(g.fine.pos[hover.plot]);
            s.width = cliff_width.load();
            s.depth = depth;
            TerrainCliffPlots(g,s,band);
        }
        for (int v : band){
            level_h = terrain.Height(v);
            p->PlotOutline(v,segs);
            emit(!CliffPlotTaken(terrain,v,depth) ? PICK_PAINT_REFUSED : (depth > 0) ? PICK_HOVER_COARSE : PICK_PAINT_OK);
        }
    }
    //Any other drag (drag_plan.md): what its release sends, plot by plot or cell by cell, as the rules will take it.
    if (const DragPlan* plan = CurrentDragPlan()){
        for (size_t i = 0; i < plan->commands.size(); i++){
            const DragCommand& c = plan->commands[i];
            bool f_cell = c.op == ZONE_OP_FIELD_PAINT || c.op == ZONE_OP_RANGE_PAINT || c.op == ZONE_OP_FIELD_ERASE;
            int at = f_cell ? g.fine.quads[c.index * 4].v[2] : c.index;     //a cell's middle vertex, for its height
            level_h = terrain.Height(at);
            ground_plot = at;
            if (f_cell){
                p->CoarseOutline(c.index,segs);
            }else{
                p->PlotOutline(c.index,segs);
            }
            emit((plan->refused[i] || plan->f_erase || plan->tool == CHASM_TOOL_ERASE) ? PICK_PAINT_REFUSED : PICK_PAINT_OK);
        }
        ground_plot = -1;
    }
    if (hover.f_hit){
        level_h = terrain.Height(hover.plot);
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
        if (ToolKind(tool) != ZONE_KIND_NONE){
            bool f_ok = (!z.storeys.empty() && z.storeys[hover.plot]) ||
                        ZoneCanBuild(*w,z,hover.plot,ToolKind(tool),0,NULL);
            plot_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }else if (ChasmCellTool(tool)){
            bool f_ok = (!z.field.empty() && z.field[hover.coarse_quad]) || ZoneCanField(*w,z,hover.coarse_quad,NULL);
            coarse_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }else if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT || ChasmLineTool(tool)){
            bool f_ok = ZoneCanGround(*w,z,hover.plot,NULL,ChasmLineTool(tool) ? ChasmLineGround(tool) : ZONE_GROUND_GARDEN);
            plot_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }else if (tool == CHASM_TOOL_CASTLE && !z.castle.empty()){
            //At the level a drag started here would take (ZoneCastleLevelFor).
            bool f_ok = ZoneCanCastle(*w,z,hover.plot,castle_shape,castle_storeys,ZoneCastleLevelFor(*w,z,hover.plot,0),NULL);
            plot_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }else if (tool == CHASM_TOOL_TERRACE && !z.terrace.empty()){
            //By the square (terraces_plan.md stage 2): green if any of its plots would take the terrace a click there makes,
            //or is one already - a drag started there extends it.
            std::vector<int> square;
            ZoneCellPlots(*w,hover.coarse_quad,square);
            int top = ZoneTerraceTopOver(*w,z,square,(int)square.size());
            bool f_ok = false;
            for (int v : square){
                f_ok = f_ok || ZoneTerraced(z,v) || ZoneCanTerrace(*w,z,v,top,NULL);
            }
            coarse_colour = f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED;
        }
        //Dragging a bridge: its whole chain, green where it may stand, red where it may not.
        int from = bridge_from.load();
        if (tool == CHASM_TOOL_BRIDGE && from >= 0 && z.world == w){
            std::vector<int> chain;
            bool f_ok = ZoneBridgeChain(*w,z,from,hover.plot,chain,NULL);
            for (int v : chain){
                level_h = terrain.Height(v);
                p->PlotOutline(v,segs);
                emit(f_ok ? PICK_PAINT_OK : PICK_PAINT_REFUSED);
            }
            level_h = terrain.Height(hover.plot);
        }
        ground_plot = g.fine.quads[hover.coarse_quad * 4].v[2];     //the square's middle: a terrace is by the square
        p->CoarseOutline(hover.coarse_quad,segs);
        emit(coarse_colour);
        ground_plot = -1;
        const GridQuad& q = g.fine.quads[hover.fine_quad];
        for (int k = 0; k < 4; k++){
            segs.push_back(g.fine.pos[q.v[k]]);
            segs.push_back(g.fine.pos[q.v[(k + 1) % 4]]);
        }
        emit(PICK_HOVER_CELL);
        std::shared_ptr<const ZoneState> zd = GetZones();
        f_on_deck = hover.layer > 0 && zd && zd->world == w;
        deck_h = f_on_deck ? BuildingLayerHeight(*w,*zd,hover.plot,hover.layer) : 0.0f;
        ground_plot = hover.plot;
        p->PlotOutline(hover.plot,segs);
        emit(plot_colour);
        ground_plot = -1;
        f_on_deck = false;
    }

    if (verts.empty()){
        pick_view->SetVisibility(false);
    }else{
        pick_view_mesh->SetLineMeshData(verts.data(),(int)verts.size());
        pick_view->SetVisibility(true);
    }
}

json ApplicationChasm::GridStatsJson(const Grid& g){
    //Each river as a few points along its course, source first, so a tool can find its banks.
    json rivers = json::array();
    //The placed ones (water_tools_plan.md) are the last, after the layout's own (ChasmPlaceWater).
    const size_t placed_from = g.layout.rivers.size() - std::min(g.layout.rivers.size(),g.settings.water.rivers.size());
    for (const GridRiverLine& r : g.layout.rivers){
        json pts = json::array();
        size_t n = r.points.size();
        for (size_t i = 0; i < n; i += std::max<size_t>(1,n / 8)){
            pts.push_back({r.points[i].x,r.points[i].y});
        }
        if (n > 0 && (pts.empty() || pts.back()[0] != r.points.back().x || pts.back()[1] != r.points.back().y)){
            pts.push_back({r.points.back().x,r.points.back().y});      //the end too: a fall's, a branch's junction
        }
        static const char* const ends[4] = {"river","branch","stream","river_off_edge"};
        rivers.push_back(json{{"kind",ends[std::max(0,std::min(3,r.end))]},{"joins",r.joins},{"width",r.width},{"points",pts},
                              {"placed",(size_t)(&r - g.layout.rivers.data()) >= placed_from}});
    }
    //Each pond placed by hand that fits (water_tools_plan.md); those that did not are ponds_dropped.
    json ponds = json::array();
    for (const ChasmLayout::Pond& k : g.layout.ponds){
        ponds.push_back(json{{"x",k.centre.x},{"z",k.centre.y},{"radius",k.radius}});
    }
    //Each spring by where it pours from, so a camera can be sent to one.
    json springs = json::array();
    for (const GridSpring& s : g.layout.springs){
        springs.push_back(json{{"lip",{s.lip.x,s.lip.y}},{"out",{s.out.x,s.out.y}},{"band",s.band},{"width",s.width},
                               {"stream",s.stream}});
    }
    //Each balcony by its side and its two ends on the rim, so a camera can be sent to one.
    json cracks = json::array();
    for (const ChasmLayout::Crack& c : g.layout.cracks){
        cracks.push_back(json{{"start",{c.points.front().x,c.points.front().y}},{"end",{c.points.back().x,c.points.back().y}},
                              {"half_width",c.half_width}});
    }
    json balconies = json::array();
    for (const GridFeature& f : g.layout.features){
        if (f.kind != GRID_FEATURE_BALCONY || f.line.points.empty()){
            continue;
        }
        const vec2& a = f.line.points.front();
        const vec2& b = f.line.points.back();
        const vec2& m = f.line.points[f.line.points.size() / 2];
        balconies.push_back(json{{"side",f.side > 0 ? "east" : "west"},{"start",{a.x,a.y}},{"end",{b.x,b.y}},
                                 {"middle",{m.x,m.y}}});
    }
    //The cliffs painted by hand (water_tools_plan.md step 6): what they came to, on the terrain made from this grid.
    json cliffs{{"strokes",g.layout.cliff_strokes.size()}};
    std::shared_ptr<const ChasmWorld> world = GetWorld();
    if (world && world->grid.get() == &g){
        cliffs["plots"] = world->terrain->cliff_plots;
        cliffs["refused"] = world->terrain->cliff_refused;
    }
    const GridSettings& s = g.settings;
    char hash[32];
    snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)g.Hash());
    return json{
        {"settings",{
            {"kind",(s.kind >= 0 && s.kind < GRID_KINDS) ? grid_kind_names[s.kind] : "?"},
            {"seed",s.seed},
            {"target_fine_cells",s.target_fine_cells},
            {"aspect",s.aspect},
            {"triangle_side",s.triangle_side},
            {"relax_passes_coarse",s.relax_passes_coarse},
            {"relax_passes_fine",s.relax_passes_fine},
            {"relax_strength",s.relax_strength},
            {"water",GridWaterToJson(s.water)},
            {"relief",GridReliefToJson(s.relief)},
            {"cliffs",GridCliffsToJson(s.cliffs)}
        }},
        {"lattice_triangles",g.num_lattice_triangles},
        {"leftover_triangles",g.num_leftover_triangles},
        {"coarse",{{"vertices",g.coarse.pos.size()},{"quads",g.coarse.quads.size()},{"edges",g.coarse.edges.size()}}},
        {"fine",{{"vertices",g.fine.pos.size()},{"quads",g.fine.quads.size()},{"edges",g.fine.edges.size()}}},
        {"bounds",{{"min_x",g.bounds_min.x},{"min_z",g.bounds_min.y},{"max_x",g.bounds_max.x},{"max_z",g.bounds_max.y}}},
        {"generate_ms",g.generate_ms},
        {"hash",hash},
        {"layout",{
            {"rifts",g.layout.rifts},
            {"cracks",(int)g.layout.cracks.size()},
            {"balconies_east",g.layout.balconies[1]},
            {"balconies_west",g.layout.balconies[0]},
            {"ledges",g.layout.count[GRID_FEATURE_LEDGE]},
            {"shards",g.layout.count[GRID_FEATURE_SHARD]},
            {"columns",g.layout.count[GRID_FEATURE_COLUMN]},
            {"rivers",g.layout.rivers.size()},
            {"river_tries",{{"falls",g.layout.river_tries[0]},{"near_a_fall",g.layout.river_tries[1]},
                            {"start_shut",g.layout.river_tries[2]},{"no_source",g.layout.river_tries[3]}}},
            {"small_river",g.layout.f_river_small},
            {"branches_wanted",g.layout.branches_wanted},
            {"balcony_list",balconies},
            {"crack_list",cracks},
            {"river_list",rivers},
            {"spring_list",springs},
            {"pond_list",ponds},
            {"ponds_dropped",g.layout.ponds_dropped},
            {"cliffs",cliffs},
            {"attempts",g.layout.attempts},
            {"layout_ms",g.layout.generate_ms}
        }}
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
    world_scene->AtTickBoundary([&](){
        while (terrain_chunks.size() < m.chunks.size()){
            Object* chunk = new Object();
            chunk->name = "Terrain " + std::to_string(terrain_chunks.size());
            chunk->SetMesh(new Mesh());
            chunk->SetMaterialSlot(0,palette_material);
            chunk->SetVisualOnly(true);
            chunk->SetVisibility(false);
            world_scene->AddObject(chunk);
            terrain_chunks.push_back(chunk);
            surface_ids.push_back(chunk->GetID());
        }
        for (size_t i = 0; i < terrain_chunks.size(); i++){
            bool f_has = i < m.chunks.size() && !m.chunks[i].verts.empty();
            if (f_has && f_new_mesh){
                std::vector<vertex>& verts = const_cast<std::vector<vertex>&>(m.chunks[i].verts);
                terrain_chunks[i]->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
                UseGroundShader(terrain_chunks[i]->GetMesh());  //after: SetMeshData resets the mode
            }
            terrain_chunks[i]->SetVisibility(f_has && f_show);
        }
    });
    terrain_uploaded = w->mesh;
    f_terrain_shown = f_show;
}

//--- The camera ----------------------------------------------------------------------------------

bool ApplicationChasm::PlayMode() const{
#ifdef USE_IMGUI
    return !f_show_ui;
#else
    return true;    //no panels to show: a ship exe is only ever the game
#endif
}

/*
    RENDER THREAD. The debug views leave with the panels and come back with them: hidden on entering
    play mode whatever their toggles say, and rebuilt - so shown as their toggles say - on leaving it.
*/
void ApplicationChasm::UpdatePlayModeViews(){
    bool f_play = PlayMode();
    if (f_play == f_view_was_play){
        return;
    }
    f_view_was_play = f_play;
    pick_version++;     //the cursor's line mesh is debug mode's: drawn again, or not, for the mode
#ifdef DEBUG
    if (f_play){
        if (grid_view){
            grid_view->SetVisibility(false);
        }
    }else{
        view_version++;
    }
#endif
}

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

/*
    PHYSICS THREAD, as a colony is entered from a menu (EnteredColony): the view put straight on it, as play mode
    starts - on whoever stands nearest the middle of its people, or its camp with none - rather than gliding there
    across a map the player has just arrived on.
*/
void ApplicationChasm::FrameColony(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    vec2 at = (w->grid->bounds_min + w->grid->bounds_max) * 0.5f;
    if (e && e->world == w && !e->workers.empty()){
        vec2 mean;
        for (const EconomyWorker& k : e->workers){
            mean += k.pos;
        }
        mean = mean * (1.0f / (float)e->workers.size());
        float best = 1e30f;
        for (const EconomyWorker& k : e->workers){
            float d = (k.pos - mean).length();
            if (d < best){
                best = d;
                at = k.pos;
            }
        }
    }else if (e && e->world == w && e->camp_plot >= 0){
        at = w->grid->fine.pos[e->camp_plot];
    }
    camera_target = vec3(at.x,ViewGroundHeight(*w,at,camera_target.y),at.y);
    cam_yaw = 0.0f;
    cam_pitch = CHASM_PLAY_START_PITCH;
    cam_distance = CHASM_PLAY_START_DIST;
    cam_lift = 0.0f;
    //Arrived: no glide to the camp to follow, as entering play mode would start.
    f_cam_fly_pending = false;
    f_cam_flying = false;
    f_cam_was_play = PlayMode();
    ApplyCamera();
}

void ApplicationChasm::ApplyCamera(){
    //The world's, even from the title - where a colony is entered (FrameColony) while the title is the active scene.
    Camera* camera = (world_scene ? world_scene : main_scene)->camera;
    float pitch = cam_pitch + cam_lift;
    float cp = cosf(pitch);
    vec3 offset(sinf(cam_yaw) * cp,sinf(pitch),cosf(cam_yaw) * cp);
    vec3 pos = camera_target + offset * cam_distance;
    camera->SetPosition(pos);
    camera->SetLookAt(camera_target);
    cam_last_written = pos;
}

/*
    THE VIEW'S HEIGHT (the user's, 2026-10-14): the orbit point on the ground the player looks at - the plateau with
    its relief, a balcony, a shard - so a village below the rim is turned about and lit as one on top is; on the
    plateau's height, the view looked past it at the air over the balcony, and the shadow ended halfway across.
    Not the chasm's floor, under the cloud deck, nor bare rock: over those the point keeps the height it has, so
    crossing the chasm does not dive 70 units and climb back. A level it is on is kept while ground of it lies
    within CHASM_CAM_LEVEL_HOLD, so panning along a rim does not bob between the two. Off the map: as it is.
*/
float ApplicationChasm::ViewGroundHeight(const ChasmWorld& w, const vec2& at, float now_y) const{
    const Terrain& t = *w.terrain;
    //The height of the level the view may settle on at p, or NAN: off the map, the floor, bare rock.
    auto level_at = [&](const vec2& p){
        GridPick k = w.picker->Pick(p);
        if (!k.f_hit || k.plot < 0 || k.plot >= (int)t.level.size()
            || t.level[k.plot] == TERRAIN_FLOOR || t.level[k.plot] == TERRAIN_BARE){
            return NAN;
        }
        return t.Height(k.plot);
    };
    float level = level_at(at);
    if (std::isnan(level)){
        return now_y;
    }
    float want = t.GroundHeight(at,level);
    if (std::fabs(want - now_y) <= TERRAIN_STEP * 0.5f){
        return want;
    }
    for (int i = 0; i < 8; i++){
        float a = (float)i * 0.78539816f;
        float beside = level_at(at + vec2(cosf(a),sinf(a)) * CHASM_CAM_LEVEL_HOLD);
        if (!std::isnan(beside) && std::fabs(t.GroundHeight(at,beside) - now_y) <= TERRAIN_STEP * 0.5f){
            return t.GroundHeight(at,beside);
        }
    }
    return want;
}

/*
    CLEARANCE: the least pitch, `pitch` or more, at which the line from the orbit point out to the camera keeps
    CHASM_CAM_CLEAR over the ground it passes - so with the point on a balcony, the camera is neither inside the
    cliff behind it nor looking at the rim instead of the point. Bare rock stands as any ground does; the floor is
    far below.

    The line starts ON the ground, so the clearance grows from nothing at the point to all of it CHASM_CAM_CLEAR_RAMP
    out. Asked in full everywhere, it cannot be had next to the point even over flat ground - and it was asked only
    of ground above the point, a switch that a centimetre of relief threw: zoomed in, the nearest sample is half a
    unit out, so the plateau's ripples flicked the view between 54 and 69 degrees as it panned (measured 2026-10-23;
    zoomed out the sample is far enough that it never showed). Ramped, flat ground asks at most
    atan(CLEAR / RAMP), 27 degrees, under the play pitch, and the answer moves with the ground rather than jumping.
*/
float ApplicationChasm::ClearPitch(const ChasmWorld& w, float pitch) const{
    const Terrain& t = *w.terrain;
    vec2 back(sinf(cam_yaw),cosf(cam_yaw));         //from the point toward the camera, on the ground
    vec2 at(camera_target.x,camera_target.z);
    float reach = cam_distance * cosf(pitch);
    float need = pitch;
    for (int i = 1; i <= CHASM_CAM_CLEAR_SAMPLES; i++){
        float h = reach * (float)i / CHASM_CAM_CLEAR_SAMPLES;
        vec2 p = at + back * h;
        GridPick k = w.picker->Pick(p);
        if (!k.f_hit || k.plot < 0 || k.plot >= (int)t.level.size() || t.level[k.plot] == TERRAIN_FLOOR){
            continue;
        }
        float clear = CHASM_CAM_CLEAR * std::min(1.0f,h / CHASM_CAM_CLEAR_RAMP);
        float rise = t.GroundHeight(p,t.Height(k.plot)) + clear - camera_target.y;
        if (h > 0.0f){
            need = std::max(need,atanf(rise / h));     //ground below the point asks a negative angle: nothing
        }
    }
    return std::min(need,std::max(pitch,CHASM_CAM_CLEAR_PITCH));
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
            cam_lift = 0.0f;    //the pitch it is at now is all the player's
        }
    }

    //Where the view stood before this pass's input, for the play mode's hard limits below.
    const float distance_before = cam_distance;
    const float pitch_before = cam_pitch;
    const vec3 target_before = camera_target;

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

    if (KeysLive()){
        float pan = cam_distance * CHASM_CAM_KEY_PAN * dt;
        if (input->IsKeyDown(INPUT_MOVE_UP) || input->IsKeyDown(INPUT_TURN_UP)) camera_target += forward * pan;
        if (input->IsKeyDown(INPUT_MOVE_DOWN) || input->IsKeyDown(INPUT_TURN_DOWN)) camera_target -= forward * pan;
        if (input->IsKeyDown(INPUT_MOVE_LEFT) || input->IsKeyDown(INPUT_TURN_LEFT)) camera_target -= right * pan;
        if (input->IsKeyDown(INPUT_MOVE_RIGHT) || input->IsKeyDown(INPUT_TURN_RIGHT)) camera_target += right * pan;
        if (input->IsKeyDown(INPUT_CHASM_ROTATE_LEFT)) cam_yaw += CHASM_CAM_KEY_TURN * dt;
        if (input->IsKeyDown(INPUT_CHASM_ROTATE_RIGHT)) cam_yaw -= CHASM_CAM_KEY_TURN * dt;
    }

    std::shared_ptr<const Grid> g = GetGrid();
    bool f_play = PlayMode();
    if (f_play && !f_cam_was_play){
        f_cam_fly_pending = true;
    }
    f_cam_was_play = f_play;
    if (!f_play){
        f_cam_fly_pending = false;
        f_cam_flying = false;
    }
    if (f_cam_fly_pending){
        /*
            Into play mode - from debug, or a release exe starting: to the camp, the colony's start,
            unless the view already shows explored ground from within the game's zoom (then the player
            was looking at the village, and stays). Waits for the economy to have found the camp.
        */
        std::shared_ptr<const EconomyState> e = GetEconomy();
        std::shared_ptr<const ChasmWorld> cw = GetWorld();
        std::shared_ptr<const Exploration> ex = GetExploration();
        if (e && cw && e->world == cw && e->camp_plot >= 0){
            f_cam_fly_pending = false;
            bool f_seen = ex && !ex->Empty() && ex->Explored(vec2(camera_target.x,camera_target.z))
                          && cam_distance <= CHASM_PLAY_DIST_MAX * 1.05f;
            if (!f_seen){
                f_cam_flying = true;
                f_cam_fly_keep_zoom = false;
                cam_fly_to = cw->grid->fine.pos[e->camp_plot];
            }
        }
    }
    //Someone picked from the neediest list (cam_seek_person): to where he is now, at the zoom the view has.
    uint32_t seek = cam_seek_person.exchange(0);
    if (seek && f_play){
        std::shared_ptr<const EconomyState> e = GetEconomy();
        for (size_t i = 0; e && i < e->workers.size(); i++){
            if (e->workers[i].id == seek){
                f_cam_flying = true;
                f_cam_fly_keep_zoom = true;
                cam_fly_to = e->workers[i].pos;
                break;
            }
        }
    }
    //...and a place picked from NEEDS ATTENTION (issues_plan.md): to its middle, the same way.
    uint32_t seek_building = cam_seek_building.exchange(0);
    if (seek_building && f_play){
        std::shared_ptr<const ChasmWorld> cw = GetWorld();
        std::shared_ptr<const ZoneState> z = GetZones();
        vec2 at;
        if (cw && z && z->world == cw && ChasmBuildingPoint(*cw,*z,seek_building,at)){
            f_cam_flying = true;
            f_cam_fly_keep_zoom = true;
            cam_fly_to = at;
        }
    }
    int seek_plot = cam_seek_plot.exchange(-1);
    if (seek_plot >= 0 && f_play){
        std::shared_ptr<const ChasmWorld> cw = GetWorld();
        if (cw && seek_plot < (int)cw->grid->fine.pos.size()){
            f_cam_flying = true;
            f_cam_fly_keep_zoom = true;
            cam_fly_to = cw->grid->fine.pos[seek_plot];
        }
    }
    if (f_cam_flying){
        //The player taking the camera over ends the glide where it is.
        bool f_moved = (camera_target - target_before).length() > 1e-5f || cam_distance != distance_before
                       || cam_pitch != pitch_before;
        vec2 at(camera_target.x,camera_target.z);
        if (f_moved){
            f_cam_flying = false;
        }else{
            float k = std::min(1.0f,CHASM_PLAY_FLY * dt);
            at = at + (cam_fly_to - at) * k;
            camera_target.x = at.x;
            camera_target.z = at.y;
            if (!f_cam_fly_keep_zoom){
                cam_distance += (CHASM_PLAY_START_DIST - cam_distance) * k;
                cam_pitch += (CHASM_PLAY_START_PITCH - cam_pitch) * k;
            }
            bool f_zoomed = f_cam_fly_keep_zoom || std::fabs(cam_distance - CHASM_PLAY_START_DIST) < 0.05f;
            if ((cam_fly_to - at).length() < 0.05f && f_zoomed){
                f_cam_flying = false;
            }
        }
    }
    if (f_play){
        /*
            The game's camera. The player's own input stops dead at a limit - the wheel past the
            furthest zoom does nothing, rather than going out and springing back. Only a view that
            was already outside (leaving a debug view far out, a camera_set) eases in, so that glides
            rather than jumps; input may still move it inward, never further out.
        */
        float k = std::min(1.0f,CHASM_PLAY_EASE * dt);
        auto limit = [k](float& v, float before, float lo, float hi){
            if (v > hi && v > before){
                v = std::max(hi,before);
            }
            if (v < lo && v < before){
                v = std::min(lo,before);
            }
            float want = std::max(lo,std::min(hi,v));
            v += (want - v) * k;
        };
        limit(cam_distance,distance_before,CHASM_PLAY_DIST_MIN,CHASM_PLAY_DIST_MAX);
        limit(cam_pitch,pitch_before,CHASM_PLAY_PITCH_MIN,CHASM_PLAY_PITCH_MAX);
        if (g){
            limit(camera_target.x,target_before.x,g->bounds_min.x + CHASM_PLAY_EDGE,g->bounds_max.x - CHASM_PLAY_EDGE);
            limit(camera_target.z,target_before.z,g->bounds_min.y + CHASM_PLAY_EDGE,g->bounds_max.y - CHASM_PLAY_EDGE);
        }
    }else{
        //Pulled back as far as the whole map and a little more - the debug camera is for inspecting
        //the grid, not the game's zoom range (README.md).
        float dist_max = 4000.0f;
        if (g){
            vec2 size = g->bounds_max - g->bounds_min;
            dist_max = std::max(size.x,size.y) * 1.5f;
        }
        cam_pitch = std::max(CHASM_CAM_PITCH_MIN,std::min(CHASM_CAM_PITCH_MAX,cam_pitch));
        cam_distance = std::max(CHASM_CAM_DIST_MIN,std::min(dist_max,cam_distance));
    }
    /*
        The orbit point rides the ground in view (ViewGroundHeight): the relief of its level quickly, so
        panning over a ridge does not jolt the view, and another level - a balcony under the rim - glided to.
        Then, in play, the tilt that clears the ground between it and the camera (ClearPitch), eased as that
        ground steps; the debug camera looks where it is put.
    */
    std::shared_ptr<const ChasmWorld> world = GetWorld();
    if (world && world->terrain && world->picker){
        float ground_y = ViewGroundHeight(*world,vec2(camera_target.x,camera_target.z),camera_target.y);
        float rate = (std::fabs(ground_y - camera_target.y) <= TERRAIN_STEP * 0.5f) ? CHASM_CAM_RELIEF_EASE : CHASM_CAM_LEVEL_GLIDE;
        camera_target.y += (ground_y - camera_target.y) * std::min(1.0f,rate * dt);
    }
    float lift = (f_play && world && world->terrain && world->picker) ? ClearPitch(*world,cam_pitch) - cam_pitch : 0.0f;
    cam_lift += (lift - cam_lift) * std::min(1.0f,CHASM_CAM_LIFT_EASE * dt);
    ApplyCamera();
}

/*
    PHYSICS THREAD, after the camera: the sun's shadow map over what is in view. archer's
    FollowView, the same two parts: the ortho is sized to the view, and the light moves only by
    whole texels of its own map - a sun that slides by fractions of a texel re-rasterises every
    shadow edge a little differently each pass, and the edges crawl while the view pans.

    Sized to THE GROUND IN VIEW (the user's, 2026-10-14): where the corners of the view meet the
    heights its shadows fall on, seen from the sun. A square of a fixed size round the orbit point at
    height 0 left the far half of a balcony under the rim outside the map, dark: the view meets lower
    ground further out, and the sun sees it shifted. A caster outside the box cannot shade a point in
    it - the sun sees the two in one place - so the receivers are all it must hold.
*/
void ApplicationChasm::FollowSun(){
    if (!sun){
        return;
    }
    //The sun's own axes: they depend only on its direction.
    sun->SetPosition(camera_target + sun_offset);
    sun->SetLookAt(camera_target);
    const vec3 sun_left = sun->GetLeft();
    const vec3 sun_up = sun->GetUp();
    vec3 toward = sun_offset;
    toward.normalize();
    float l0 = 1e30f, l1 = -1e30f, u0 = 1e30f, u1 = -1e30f;
    auto hold = [&](const vec3& p){
        float l = p.dot(sun_left);
        float u = p.dot(sun_up);
        l0 = std::min(l0,l);
        l1 = std::max(l1,l);
        u0 = std::min(u0,u);
        u1 = std::max(u1,u);
    };
    hold(camera_target);
    //The view's corners, out to the lowest and the highest ground its shadows fall on - or to SUN_FIT_REACH.
    Camera* camera = main_scene->camera;
    vec3 eye = camera->GetPosition();
    vec3 forward = camera->GetForward();
    vec3 up = camera->GetUp();
    vec3 left = camera->GetLeft();
    float ty = tanf(camera->viewport.fov * 0.5f * 0.017453293f);
    float tx = ty * std::max(0.1f,camera->viewport.aspect);
    float lo = std::max(TERRAIN_DECK_Y,camera_target.y - SUN_FIT_BELOW);
    float hi = std::max(camera_target.y,terrain_levels[TERRAIN_PLATEAU].height) + SUN_FIT_ABOVE;
    float reach = cam_distance * SUN_FIT_REACH;
    for (int c = 0; c < 4; c++){
        vec3 dir = forward + up * ((c & 1) ? ty : -ty) + left * ((c & 2) ? tx : -tx);
        dir.normalize();
        for (float y : {lo,hi}){
            float t = (dir.y < -1e-4f) ? (y - eye.y) / dir.y : reach;
            hold(eye + dir * std::max(0.0f,std::min(t,reach)));
        }
    }
    //Square, round the middle of what it holds; its half-extent in steps of SUN_FIT_STEP.
    float half = std::max(l1 - l0,u1 - u0) * 0.5f;
    half = std::max(20.0f,std::min(half,cam_distance * SUN_FIT_MOST));
    sun->viewport.zoom = powf(SUN_FIT_STEP,ceilf(logf(half) / logf(SUN_FIT_STEP)));
    vec3 middle = sun_left * ((l0 + l1) * 0.5f) + sun_up * ((u0 + u1) * 0.5f) + toward * camera_target.dot(toward);
    sun->SetPosition(middle + sun_offset);
    sun->SetLookAt(middle);
    float texel = (2.0f * sun->viewport.zoom) / sun->viewport.width;
    vec3 sun_eye = sun->GetPosition();
    float l = sun_eye.dot(sun_left);
    float u = sun_eye.dot(sun_up);
    sun_eye += sun_left * (roundf(l / texel) * texel - l) + sun_up * (roundf(u / texel) * texel - u);
    sun->SetPosition(sun_eye);
}

void ApplicationChasm::UpdateView(void){
    InputController* input = main_scene->inputcontroller;
    //The economy as this pass's ticks left it (TickEconomy), before anything here reads the copy.
    if (f_economy_unpublished){
        f_economy_unpublished = false;
        PublishEconomy();
    }
    //The sound, every pass, before anything below can return: it plays on over the title and the menus.
    UpdateSound();
    //A replay ended, a quit waiting for its recording, a bug report written (ApplicationChasmRecord.cpp).
    ServiceRecording();
    //The player's settings, on a change - and the music's and the sounds' volumes (ApplicationChasmMenu.cpp).
    ApplySettings();
    //THE TITLE (ApplicationChasmTitle.cpp): its pass alone while it shows - nothing of the world moves.
    if (TitleShowing()){
        UpdateTitle(input);
        return;
    }
    //A season's first dawn autosaves (ApplicationChasmMenu.cpp).
    UpdateAutosave();
    //A question over the game - the window's X, leaving the colony: it has the input, and nothing under it moves.
    if (main_window->f_close_asked.exchange(false)){
        OpenAsk(CHASM_ASK_QUIT);
    }
    //A death's pop-up, the end's page (demo_plan.md step 7; ApplicationChasmGoals.cpp): from what the ticks just told.
    UpdateEnd();
    if (UpdateAsk(input) || RunBusy()){
        return;
    }
    //The Statistics page (ApplicationChasmStats.cpp): Tab opens it, and while it is up it has the input.
    if (UpdateStats(input)){
        return;
    }
    //The pause menu (ApplicationChasmMenu.cpp), and the lists it opens: the input while it is up.
    if (UpdatePause(input)){
        return;
    }
    //Escape: one step back - the tool in hand, the selection, then the pause menu (demo_plan.md).
    if (input->WasKeyPressed(INPUT_CHASM_MENU) && KeysLive()){
        EscapeStep();
    }
    QuickKeys(input);
    UpdateCamera();
    //The storm at the view (ApplicationChasmWeather.cpp), which the sun dims under.
    UpdateWeather();
    UpdateSun();
    FollowSun();
    UpdateTimeKeys();
    /*
        Play mode builds (construction_plan.md): the tool keys work there too, and what they place is a
        construction site. The seed, frame and fog keys are debug mode's alone.
    */
    const bool f_debug_keys = !PlayMode();
    //The tool keys toggle: the active tool's key again puts it down. Their presses are read either way, so one typed
    //into a text field does not wait and fire once the field lets go.
    {
        const bool f_live = KeysLive();
        const int keys[23] = {INPUT_CHASM_TOOL_HOUSE,INPUT_CHASM_TOOL_FIELD,INPUT_CHASM_TOOL_ERASE,
                              INPUT_CHASM_TOOL_GARDEN,INPUT_CHASM_TOOL_LOT,INPUT_CHASM_TOOL_ROAD,
                              INPUT_CHASM_TOOL_STORE,INPUT_CHASM_TOOL_WOODCUTTER,
                              INPUT_CHASM_TOOL_WATER,INPUT_CHASM_TOOL_WINCH,INPUT_CHASM_TOOL_CAMP,
                              INPUT_CHASM_TOOL_BRIDGE,INPUT_CHASM_TOOL_BUILDERS,INPUT_CHASM_TOOL_WALL,
                              INPUT_CHASM_TOOL_RANGE,INPUT_CHASM_TOOL_TOWER,INPUT_CHASM_TOOL_ROPEWALK,
                              INPUT_CHASM_TOOL_STONECUTTER,INPUT_CHASM_TOOL_MINE,INPUT_CHASM_TOOL_STEAM,
                              INPUT_CHASM_TOOL_CONDENSER,INPUT_CHASM_TOOL_TERRACE,INPUT_CHASM_TOOL_SAIL_LOFT};
        const int tools[23] = {CHASM_TOOL_HOUSE,CHASM_TOOL_FIELD,CHASM_TOOL_ERASE,
                               CHASM_TOOL_GARDEN,CHASM_TOOL_LOT,CHASM_TOOL_ROAD,
                               CHASM_TOOL_STORE,CHASM_TOOL_WOODCUTTER,CHASM_TOOL_WATER,CHASM_TOOL_WINCH,
                               CHASM_TOOL_CAMP,CHASM_TOOL_BRIDGE,CHASM_TOOL_BUILDERS,CHASM_TOOL_WALL,
                               CHASM_TOOL_RANGE,CHASM_TOOL_TOWER,CHASM_TOOL_ROPEWALK,CHASM_TOOL_STONECUTTER,
                               CHASM_TOOL_MINE,CHASM_TOOL_STEAM,CHASM_TOOL_CONDENSER,CHASM_TOOL_TERRACE,
                               CHASM_TOOL_SAIL_LOFT};
        //Play mode's tools still waiting for a discovery (demo_plan.md step 6) are not on the bar, and their keys do
        //nothing - and one in hand from before (debug mode, or another colony) is put down.
        uint32_t locked = ToolsLocked();
        for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++){
            if (input->WasKeyPressed(keys[i]) && f_live && !((locked >> tools[i]) & 1u)){
                paint_tool = (paint_tool == tools[i]) ? CHASM_TOOL_SELECT : tools[i];
                pick_version++;
                SignalUI("click");
            }
        }
        int held = paint_tool.load();
        if (held > 0 && held < 32 && ((locked >> held) & 1u)){
            paint_tool = CHASM_TOOL_SELECT;
            pick_version++;
        }
    }
    //After the camera, so the ray goes through the view this pass will draw.
    UpdatePick();
    if (input->WasKeyPressed(INPUT_CHASM_FRAME) && KeysLive() && f_debug_keys){
        FrameMap();
    }
    //V shows debug mode the fog of war as play mode has it (the same as View > fog of war).
    if (input->WasKeyPressed(INPUT_CHASM_FOG) && KeysLive() && f_debug_keys){
        f_view_clouds = !f_view_clouds;
    }
#ifdef DEBUG
    //Z drops a zombie on the plot under the cursor (zombies_plan.md) - in play mode too: it is how a game is tried out.
    if (input->WasKeyPressed(INPUT_CHASM_ZOMBIE) && KeysLive()){
        GridPick h = GetHoverPick();
        if (h.f_hit && h.plot >= 0){
            SubmitZombies(h.plot,1);
        }
    }
    //...and X an archer to shoot them: the People panel's "Put an archer down" by key - the best eye, the archery its
    //slider says, posted there. The same three lines as the button's; a change to what a test archer is wants both.
    //The slider is the panel's, read here under the physics_mutex it is written under.
    if (input->WasKeyPressed(INPUT_CHASM_ARCHER) && KeysLive()){
        GridPick h = GetHoverPick();
        if (h.f_hit && h.plot >= 0){
            int skills[SKILL_COUNT] = {};
            skills[SKILL_PRECISION] = 10;
            SubmitPerson(h.plot,0,0,skills,0,person_archer_percent * (ARCHERY_FULL / 100),true,person_archer_kills);
        }
    }
#endif
    /*
        N regenerates with the next seed. Done right here, on the physics thread with the pass's
        lock held, which stalls the window for the length of one generation - fine for a debug key,
        and it keeps the grid swap on one well-known thread.
    */
    bool f_next = input->WasKeyPressed(INPUT_CHASM_NEXT_SEED);
    bool f_previous = input->WasKeyPressed(INPUT_CHASM_PREVIOUS_SEED);
    if ((f_next || f_previous) && KeysLive() && f_debug_keys){
        std::shared_ptr<const Grid> g = GetGrid();
        GridSettings s = g ? g->settings : next_settings;
        if (f_next){
            s.seed++;
        }else if (s.seed > 0){
            s.seed--;
        }
        RegenerateGrid(s);
    }
}

//--- The frame ----------------------------------------------------------------------------------

//RENDER THREAD, before the scene draws: the only place GL may be touched for the meshes below.
void ApplicationChasm::PreRender(void){
    ServiceDiorama();       //a capture or a reload asked for (ApplicationChasmTitle.cpp): GL, so here
    //The title: nothing of the world is drawn, so none of it is uploaded; its own animations run on the clock.
    if (TitleShowing()){
        static const auto t0 = std::chrono::steady_clock::now();
        renderer->motion_seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        //The palette is the title's too: a build that opens on the title has drawn no frame of the world, and the
        //summer row its broadleaves' green sits on goes up only with the first fade - magenta until then.
        UpdateSeasonPalette();
        UploadTitleBirds();
        ClearWeather();
        return;
    }
    //The clock of everything the GPU animates: the simulation's, so a pause holds the mist still.
    renderer->motion_seconds = (float)SimSeconds();
    UpdateSeasonPalette();
    UploadTerrain();
    UploadZones();
    UploadForest();
    UploadWeather();
    UploadWater();
    UpdateMist();
    UploadWorkers();
    UploadBirds();
    UploadCrowdBench();
    UploadPiles();
    UploadCrates();
    UploadMines();
    UploadZombies();
    UploadFallen();
    UploadRange();
    UploadDocks();
    UploadRopewalks();
    UploadBorders();
    UploadClouds();
    UploadHearth();
    UploadSteamPlumes();
    UpdatePickView();
    UpdateGhost();
    UpdateSlopeGrid();
    UploadSelection();
    UpdatePlayModeViews();
#ifdef DEBUG
    if (!PlayMode()){
        UpdateGridView();
    }
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
#define VIEW_CAMP_POOR      0xFF606878u     //a camp spot that passes, from its lowest score...
#define VIEW_CAMP_GOOD      0xFF40D060u     //...to the best
#define VIEW_CAMP_BEST      0xFFFFFFFFu     //the camp, and its survey
#define VIEW_CAMP_REFUSED   0xFFFF3030u

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
    /*
        With the areas shown, what is seen moves as the people look round: built again for the exploration it shows,
        at most once a second - the whole view is rebuilt, and scouts change the exploration every few ticks.
    */
    std::shared_ptr<const Exploration> explored = f_view_areas ? GetExploration() : nullptr;
    const bool f_seen_moved = explored && explored->version != grid_view_explored_version
                              && std::chrono::steady_clock::now() - grid_view_areas_built > std::chrono::seconds(1);
    if (!w || (w == grid_view_built_for && version == grid_view_built_version && !f_seen_moved)){
        return;
    }
    grid_view_built_for = w;
    grid_view_built_version = version;
    if (explored){
        grid_view_explored_version = explored->version;
        grid_view_areas_built = std::chrono::steady_clock::now();
    }
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
    //On the ground (Terrain::ground per vertex, GroundHeight anywhere else), so the layers lie on
    //hills instead of disappearing into them.
    auto H = [&terrain,f_flat](int v){
        if (f_flat){
            return 0.0f;
        }
        return terrain.ground.empty() ? terrain.Height(v) : terrain.ground[v];
    };
    auto G = [&terrain,f_flat](const vec2& p, float height){
        return f_flat ? 0.0f : terrain.GroundHeight(p,height);
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
        if (terrain.SameGround(a,b)){
            line(pa,pb,color,ha,hb);
        }else{
            //Across a cliff: each half on its own plot's ground, dropping at the midpoint.
            vec2 mid = (pa + pb) * 0.5f;
            line(pa,mid,color,ha,G(mid,terrain.Height(a)));
            line(mid,pb,color,G(mid,terrain.Height(b)),hb);
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
        //By what each line is (Grid::LineKind): rims magenta, everything standing in the chasm cyan.
        auto line_colour = [g](int l){
            int kind = g->LineKind(l);
            return (kind < 0) ? VIEW_PIN_OUTLINE : (kind == GRID_FEATURE_RIM) ? VIEW_PIN_RIM : VIEW_PIN_SHARD;
        };
        //A rim's line is on the plateau and a shard's on its top - each on its high side.
        auto line_level = [g](int l){
            int kind = g->LineKind(l);
            return kind < 0 ? TERRAIN_PLATEAU : (int)TerrainLevelOfKind(kind);
        };
        //The smoothed lines the chains lie on (TerrainFeatureLines), not the settings' corners -
        //each point on its own level's ground, so a rim follows the hills along its top.
        for (size_t f = 0; f < w->features.size(); f++){
            int l = base + (int)f;
            float level = terrain_levels[line_level(l)].height;
            const std::vector<vec2>& pts = w->features[f].points;
            for (size_t i = 0; i + 1 < pts.size(); i++){
                line(pts[i],pts[i + 1],line_colour(l),G(pts[i],level) + 0.3f,G(pts[i + 1],level) + 0.3f);
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
            float h = under.f_hit ? G(issue.where,terrain.Height(under.plot)) : 0.0f;
            const int SEGMENTS = 12;
            for (int s = 0; s < SEGMENTS; s++){
                float a0 = 6.2831853f * s / SEGMENTS;
                float a1 = 6.2831853f * (s + 1) / SEGMENTS;
                line(issue.where + vec2(cosf(a0),sinf(a0)) * r,issue.where + vec2(cosf(a1),sinf(a1)) * r,c,h,h);
            }
            line(issue.where,issue.where,c,h,h + unit * (issue.f_failure ? 6.0f : 2.5f));
        }
    }

    //Where a new colony would camp: a post on every spot looked at, as tall as its score against the best's,
    //short and red where refused; the camp in white with the ground its survey reaches out to.
    if (f_view_camp){
        std::vector<ZoneCampSpot> spots;
        ZoneCampSpots(*w,EconomySettlerCount(),spots);
        int best = -1;
        for (int i = 0; i < (int)spots.size(); i++){
            if (!spots[i].refused && (best < 0 || spots[i].score > spots[best].score)){
                best = i;
            }
        }
        float top = (best >= 0) ? std::max(1e-3f,spots[best].score) : 1.0f;
        auto ring = [&](const vec2& c, float r, uint32_t col, int segments){
            for (int s = 0; s < segments; s++){
                vec2 a = c + vec2(cosf(6.2831853f * s / segments),sinf(6.2831853f * s / segments)) * r;
                vec2 b = c + vec2(cosf(6.2831853f * (s + 1) / segments),sinf(6.2831853f * (s + 1) / segments)) * r;
                line(a,b,col,G(a,0.0f) + 0.2f,G(b,0.0f) + 0.2f);
            }
        };
        for (int i = 0; i < (int)spots.size(); i++){
            const ZoneCampSpot& s = spots[i];
            const vec2& at = g->fine.pos[s.plot];
            float h = H(s.plot);
            uint32_t c = s.refused ? VIEW_CAMP_REFUSED : (i == best) ? VIEW_CAMP_BEST
                                   : LerpColor(VIEW_CAMP_POOR,VIEW_CAMP_GOOD,s.score / top);
            float tall = s.refused ? 1.5f : 2.0f + 10.0f * s.score / top;
            ring(at,unit * 0.5f,c,12);
            line(at,at,c,h,h + unit * tall);
        }
        if (best >= 0){
            ring(g->fine.pos[spots[best].plot],ZONE_CAMP_SURVEY,VIEW_CAMP_BEST,64);
        }
    }

    /*
        THE AREAS (areas_plan.md): each one's edge round its cells in its kind's colour - bright once any of it is seen,
        dim before - and a post at its middle, tall once seen. On the sight's raster, at each cell's ground. Where two
        overlap - a river over the plateau - both edges are drawn.
    */
    if (f_view_areas && w->areas && w->sight){
        const ExplorationSight& sight = *w->sight;
        const int width = sight.width;
        const int height = sight.height;
        std::vector<uint8_t> in((size_t)width * height,0);
        auto corner = [&](int x, int z){
            return sight.origin + vec2((float)x,(float)z) * EXPLORATION_CELL;
        };
        for (const Area& a : w->areas->areas){
            bool f_seen = false;
            for (int c : a.cells){
                f_seen = f_seen || (explored && c < (int)explored->cells.size() && explored->cells[(size_t)c]);
                in[(size_t)c] = 1;
            }
            uint32_t col = f_seen ? AreaKindColour(a.kind) : LerpColor(0xFF202020u,AreaKindColour(a.kind),0.35f);
            for (int c : a.cells){
                int x = c % width;
                int z = c / width;
                float y = f_flat ? 0.0f : sight.ground[(size_t)c] + 0.3f;
                if (x == 0 || !in[(size_t)c - 1])                  line(corner(x,z),corner(x,z + 1),col,y,y);
                if (x == width - 1 || !in[(size_t)c + 1])          line(corner(x + 1,z),corner(x + 1,z + 1),col,y,y);
                if (z == 0 || !in[(size_t)(c - width)])            line(corner(x,z),corner(x + 1,z),col,y,y);
                if (z == height - 1 || !in[(size_t)(c + width)])   line(corner(x,z + 1),corner(x + 1,z + 1),col,y,y);
            }
            int mid = (int)std::floor((a.middle.y - sight.origin.y) / EXPLORATION_CELL) * width
                      + (int)std::floor((a.middle.x - sight.origin.x) / EXPLORATION_CELL);
            float y = (f_flat || mid < 0 || mid >= (int)sight.ground.size()) ? 0.0f : sight.ground[(size_t)mid];
            line(a.middle,a.middle,col,y,y + (f_seen ? 12.0f : 5.0f));
            for (int c : a.cells){
                in[(size_t)c] = 0;
            }
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
    //Straight onto the ground the camera eases to anyway (UpdateCamera), so the jump does not
    //finish with a drift.
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    const vec2& at = r->issues[i].where;
    float y = w ? w->terrain->GroundHeight(at,terrain_levels[TERRAIN_PLATEAU].height) : 0.0f;
    camera_target = vec3(at.x,y,at.y);
    cam_distance = std::min(cam_distance,40.0f);
    ApplyCamera();
}
#endif

//--- The panel ----------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationChasm::DrawImGuiUI(void){
    RenderApplicationUI();
    if (TitleShowing()){
        RenderTitlePanel();
        return;
    }
    RenderChasmPanel();
}

//A tool's radio button on the Paint panel: its label ("##tool": "Field" and friends are plain words other widgets may
//use too), the tool, and its key - shown on hovering, so four fit in a row; NULL for a tool of the panel's only.
struct PaintToolButton{
    const char* label;
    int tool;
    const char* key;
};

/*
    A group of the Paint panel's tools, under its title: radio buttons in lined-up columns, four to a row (the user's,
    2026-10-23) - fewer only where the panel is too narrow for four, so none runs off its edge.
*/
static void PaintToolGroup(const char* title, const PaintToolButton* tools, int n, int& tool){
    if (title){
        ImGui::SeparatorText(title);
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    auto width = [&](int i){
        return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(tools[i].label,NULL,true).x;
    };
    int columns = 4;
    for (; columns > 1; columns--){
        float total = 0.0f;
        for (int c = 0; c < columns; c++){
            float widest = 0.0f;
            for (int i = c; i < n; i += columns){
                widest = std::max(widest,width(i));
            }
            total += widest + style.CellPadding.x * 2.0f;
        }
        if (total <= ImGui::GetContentRegionAvail().x){
            break;
        }
    }
    if (ImGui::BeginTable(title ? title : "##tools",columns,ImGuiTableFlags_SizingFixedFit)){
        for (int i = 0; i < n; i++){
            ImGui::TableNextColumn();
            if (ImGui::RadioButton(tools[i].label,tool == tools[i].tool)){
                tool = tools[i].tool;
            }
            if (tools[i].key && ImGui::IsItemHovered()){
                ImGui::SetTooltip("key %s",tools[i].key);
            }
        }
        ImGui::EndTable();
    }
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
    ImGui::TextDisabled("arrows/WASD pan  Q/E turn  F frames the map  N/B next/previous seed");

    std::shared_ptr<const Grid> g = GetGrid();

    if (ImGui::CollapsingHeader("Generate",ImGuiTreeNodeFlags_DefaultOpen)){
        //Water placed by hand stays with the map until taken away (water_tools_plan.md): Generate, the seed buttons
        //and N keep it, as chasm_generate does - the water tools' list is where it goes.
        if (g){
            next_settings.water = g->settings.water;
            next_settings.relief = g->settings.relief;
            next_settings.cliffs = g->settings.cliffs;
        }
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
        //The chasm, or a test map (test_maps_plan.md).
        ImGui::Combo("kind",&next_settings.kind,grid_kind_names,GRID_KINDS);
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
        ImGui::SameLine();
        if (ImGui::Button("Previous seed") && next_settings.seed > 0){
            next_settings.seed--;
            RegenerateGrid(next_settings);
            g = GetGrid();
        }
        if (g){
            //What the seed made of the chasm (Grid::layout) - the thing flipping through seeds is for.
            const ChasmLayout& l = g->layout;
            int ends[4] = {};
            for (const GridRiverLine& r : l.rivers){
                ends[std::max(0,std::min(3,r.end))]++;
            }
            ImGui::Text("chasm: %i rift%s, %i crack%s; balconies %i east, %i west; islands: %i shards, "
                        "%i columns, %i ledges",
                        l.rifts,l.rifts == 1 ? "" : "s",(int)l.cracks.size(),l.cracks.size() == 1 ? "" : "s",l.balconies[1],l.balconies[0],
                        l.count[GRID_FEATURE_SHARD],l.count[GRID_FEATURE_COLUMN],l.count[GRID_FEATURE_LEDGE]);
            ImGui::Text("water: %i rivers%s, %i branches, %i swallow-hole streams, %i springs",
                        ends[GRID_RIVER_FALL] + ends[GRID_RIVER_EDGE],l.f_river_small ? " (small)" : "",ends[GRID_RIVER_JOIN],
                        ends[GRID_RIVER_SINK],(int)l.springs.size());
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
        b = f_view_forest;      if (ImGui::Checkbox("forest",&b)){ f_view_forest = b; }
        ImGui::SameLine();
        b = f_view_water;       if (ImGui::Checkbox("water",&b)){ f_view_water = b; }
        ImGui::SameLine();
        b = f_view_mist;        if (ImGui::Checkbox("mist",&b)){ f_view_mist = b; }
        ImGui::SameLine();
        b = f_view_clouds;      if (ImGui::Checkbox("fog of war (V)",&b)){ f_view_clouds = b; }
        ImGui::SameLine();
        b = f_view_flat;        if (ImGui::Checkbox("flat",&b)){ f_view_flat = b; changed = true; }
        ImGui::SameLine();
        b = f_view_camp;        if (ImGui::Checkbox("camp spots",&b)){ f_view_camp = b; changed = true; }
        ImGui::SameLine();
        b = f_view_areas;       if (ImGui::Checkbox("areas",&b)){ f_view_areas = b; changed = true; }
        if (changed){
            view_version++;
        }
        //The areas shown (areas_plan.md): each in its colour, and whether it is seen yet.
        std::shared_ptr<const ChasmWorld> aw = GetWorld();
        if (f_view_areas && aw && aw->areas){
            std::shared_ptr<const Exploration> ex = GetExploration();
            for (size_t i = 0; i < aw->areas->areas.size(); i++){
                const Area& a = aw->areas->areas[i];
                bool f_seen = false;
                for (int c : a.cells){
                    f_seen = f_seen || (ex && c < (int)ex->cells.size() && ex->cells[(size_t)c]);
                }
                uint32_t col = AreaKindColour(a.kind);
                ImVec4 colour(((col >> 16) & 0xFF) / 255.0f,((col >> 8) & 0xFF) / 255.0f,(col & 0xFF) / 255.0f,
                              f_seen ? 1.0f : 0.45f);
                ImGui::TextColored(colour,"%2i  %-9s %2i  %5i cells  %s",(int)i,AreaKindName(a.kind),a.which,
                                   (int)a.cells.size(),f_seen ? "seen" : "not seen");
            }
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
        static const PaintToolButton general[] = {
            {"Select##tool",CHASM_TOOL_SELECT,NULL},
            {"Erase##tool",CHASM_TOOL_ERASE,"3"},
        };
        //A building a drag, each kind's: the people's houses and workplaces first, then what is placed by the land.
        //The well is a debug one (water_tools_plan.md), for test maps with no river: the panel's only, never in play.
        static const PaintToolButton buildings[] = {
            {"House##tool",CHASM_TOOL_HOUSE,"1"},
            {"Store##tool",CHASM_TOOL_STORE,"8"},
            {"Woodcutter##tool",CHASM_TOOL_WOODCUTTER,"9"},
            {"Water##tool",CHASM_TOOL_WATER,"0"},
            {"Builders##tool",CHASM_TOOL_BUILDERS,"G"},
            {"Stonecutter##tool",CHASM_TOOL_STONECUTTER,"M"},
            {"Ropewalk##tool",CHASM_TOOL_ROPEWALK,"K"},
            {"Sail loft##tool",CHASM_TOOL_SAIL_LOFT,"I"},
            {"Steam##tool",CHASM_TOOL_STEAM,"P"},
            {"Condenser##tool",CHASM_TOOL_CONDENSER,"L"},
            {"Mine##tool",CHASM_TOOL_MINE,"O"},
            {"Tower##tool",CHASM_TOOL_TOWER,"T"},
            {"Winch##tool",CHASM_TOOL_WINCH,"H"},
            {"Bridge##tool",CHASM_TOOL_BRIDGE,"J"},
            {"Camp##tool",CHASM_TOOL_CAMP,"C"},
            {"Well##tool",CHASM_TOOL_WELL,NULL},
        };
        //Painted on the ground - plots, cells or a line - rather than standing on it.
        static const PaintToolButton ground[] = {
            {"Field##tool",CHASM_TOOL_FIELD,"2"},
            {"Garden##tool",CHASM_TOOL_GARDEN,"4"},
            {"Lot##tool",CHASM_TOOL_LOT,"5"},
            {"Range##tool",CHASM_TOOL_RANGE,"R"},
            {"Dock##tool",CHASM_TOOL_DOCK,NULL},
            {"Road##tool",CHASM_TOOL_ROAD,"6"},
            {"Wall##tool",CHASM_TOOL_WALL,"7"},
            {"Terrace##tool",CHASM_TOOL_TERRACE,"Y"},
        };
        //Water and height placed by hand (water_tools_plan.md, ChasmMapTool): a new map each, so the panel's only, never
        //in play.
        static const PaintToolButton terrain[] = {
            {"River##tool",CHASM_TOOL_RIVER,NULL},
            {"Pond##tool",CHASM_TOOL_POND,NULL},
            {"Height##tool",CHASM_TOOL_HEIGHT,NULL},
            {"Cliff##tool",CHASM_TOOL_CLIFF,NULL},
        };
        //The debug castle (ramparts_plan.md): a prototype, so the panel's only - no key, no build bar.
        static const PaintToolButton prototype[] = {
            {"Castle##tool",CHASM_TOOL_CASTLE,NULL},
        };
        PaintToolGroup(NULL,general,(int)(sizeof(general) / sizeof(general[0])),tool);
        PaintToolGroup("Buildings",buildings,(int)(sizeof(buildings) / sizeof(buildings[0])),tool);
        PaintToolGroup("Ground",ground,(int)(sizeof(ground) / sizeof(ground[0])),tool);
        PaintToolGroup("Terrain - each a new map",terrain,(int)(sizeof(terrain) / sizeof(terrain[0])),tool);
        if (ChasmMapTool(tool)){
            RenderMapTools();
        }
        PaintToolGroup("Prototype",prototype,(int)(sizeof(prototype) / sizeof(prototype[0])),tool);
        if (tool == CHASM_TOOL_CASTLE){
            int shape = castle_shape;
            int storeys = castle_storeys;
            if (ImGui::RadioButton("Block##castle",shape == ZONE_CASTLE_BLOCK)) shape = ZONE_CASTLE_BLOCK;
            ImGui::SameLine();
            if (ImGui::RadioButton("Arch##castle",shape == ZONE_CASTLE_ARCH)) shape = ZONE_CASTLE_ARCH;
            ImGui::SameLine();
            if (ImGui::RadioButton("Stair##castle",shape == ZONE_CASTLE_STAIR)) shape = ZONE_CASTLE_STAIR;
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderInt("storeys##castle",&storeys,1,ZONE_CASTLE_MAX_STOREYS);
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("a stair rises its last storey; an arch is 2 at least. On a piece: an arch 2 above its top, "
                               "or a stair 1 above an arch, goes on it; shift takes the top piece off");
            ImGui::PopStyleColor();
            if (shape != castle_shape || storeys != castle_storeys){
                castle_shape = shape;
                castle_storeys = storeys;
                pick_version++;
            }
        }
        if (tool != paint_tool){
            paint_tool = tool;
            pick_version++;
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("a drag is one building - start it on one to extend it; a click adds a storey, shift takes one off");
        ImGui::PopStyleColor();
        std::shared_ptr<const ChasmWorld> zw = GetWorld();
        std::shared_ptr<const ZoneState> z = GetZones();
        if (zw && z && z->world == zw){
            ZoneStats st = ComputeZoneStats(*zw,*z);
            ImGui::TextWrapped("%i houses (%.0f floor), %i stores (%.0f), %i woodcutters, %i water collectors, %i winches, "
                               "%i builders' huts",
                               st.buildings[ZONE_KIND_HOUSE],st.floor_area[ZONE_KIND_HOUSE],st.buildings[ZONE_KIND_STORE],
                               st.floor_area[ZONE_KIND_STORE],st.buildings[ZONE_KIND_WOODCUTTER],st.buildings[ZONE_KIND_WATER],
                               st.buildings[ZONE_KIND_WINCH],st.buildings[ZONE_KIND_BUILDERS]);
            ImGui::TextWrapped("%i fields on %i cells, %.0f units of field",st.buildings[ZONE_KIND_FIELD],st.field_cells,
                               st.floor_area[ZONE_KIND_FIELD]);
            ImGui::TextWrapped("%i garden and %i lot plots, %.0f units of ground; %i road plots",st.gardens,st.lots,
                               st.ground_area,st.roads);
            RenderBuildingInfo(*zw,*z);
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

    RenderTimePanel();
    RenderSunPanel();
    RenderWeatherPanel();
    RenderMusicPanel();
    RenderPeoplePanel();
    RenderZombiePanel();
    RenderGoalsPanel();

    if (ImGui::CollapsingHeader("Save")){
        ImGui::InputText("name##save",save_name,sizeof(save_name));
        ImGui::SameLine();
        if (ImGui::Button("Save##save")){
            std::string error;
            bool f_saved = ChasmSaveWrite(save_name,ChasmSaveToJson(MakeSave()),error);
            save_status = f_saved ? "saved " + ChasmSavePath(save_name) : error;
            if (f_saved){
                SetColonySave(save_name);
            }
        }
        {
            std::lock_guard<std::mutex> lock(menu_text_mutex);
            if (!colony_save.empty()){
                ImGui::TextDisabled("this game's save: %s",colony_save.c_str());
            }
        }
        //The panel holds physics_mutex, so loading - which replaces simulation state - is safe here.
        for (const std::string& name : ChasmSaveList()){
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton("load")){
                json j;
                ChasmSave save;
                std::string error;
                if (!ChasmSaveRead(name,j,error) || !ChasmSaveFromJson(j,save,error)){
                    save_status = error;
                }else{
                    save_status = LoadSave(save,error) ? "loaded " + name : error;
                    SetColonySave(name);    //loaded even with plots refused, as LoadSave says
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopID();
        }
        if (!save_status.empty()){
            ImGui::TextDisabled("%s",save_status.c_str());
        }
        ImGui::TextDisabled("F9 records (from this state), F10 replays the last recording");
        //A tester's build records from every autosave (ApplicationChasmRecord.cpp); here it would take over input_record.
        bool f_always = f_always_record.load();
        if (ImGui::Checkbox("Always recording, as a tester's build",&f_always)){
            f_always_record = f_always;
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
    RegisterCrowdBenchTool();
    RegisterBirdTools();
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
        "chasm_grid returns, with what the seed made of the chasm under \"layout\". Blocks for the "
        "length of one generation (a few hundred ms). For flipping through seeds: frame_map puts the "
        "whole map in view (pitch 89 looks straight down) and include_screenshot returns a PNG of the "
        "new map - include_ui false for the scene alone.",
        json{
            {"type","object"},
            {"properties",{
                {"seed",{{"type","integer"}}},
                {"frame_map",{{"type","boolean"},{"description","put the whole new map in view"}}},
                {"pitch",{{"type","number"},{"description","with frame_map: degrees above the horizon, 89 for straight down"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"},{"description","with include_screenshot: the debug panels too (default true)"}}},
                {"target_fine_cells",{{"type","integer"},{"description","fine cells to aim for; the count comes out close, not exact"}}},
                {"aspect",{{"type","number"},{"description","map width (x) over depth (z)"}}},
                {"triangle_side",{{"type","number"},{"description","lattice triangle side in world units; a fine cell is about a quarter of it"}}},
                {"relax_passes_coarse",{{"type","integer"}}},
                {"relax_passes_fine",{{"type","integer"}}},
                {"relax_strength",{{"type","number"},{"description","0..1, how far toward its best-fit square a vertex moves per pass"}}},
                {"kind",{{"type","string"},{"description","the map's kind (test_maps_plan.md): chasm, the game's; flat - a "
                                                         "test map, flat and dry with no chasm and nobody on it; title - "
                                                         "the title's town (TitleTown.h), to look at in the world; or cracks - "
                                                         "the chasm as cracked mud (MudCracks.h), its terrain only"}}},
                {"defaults",{{"type","boolean"},{"description","start from the default settings instead of the current grid's"}}},
                {"water",{{"type","object"},{"description","water placed by hand (docs/water_tools_plan.md), the whole of it: "
                                                          "{\"rivers\":[{\"points\":[[x,z],...],\"width\":4}],\"ponds\":[{\"x\":0,"
                                                          "\"z\":0,\"radius\":6}]} - world x,z, a river's source first and a few "
                                                          "points a course; {} for none. A river ends where it was "
                                                          "drawn: at the map's edge it runs off, in another river's water it joins "
                                                          "it, over a rim it falls, anywhere else it sinks into a swallow hole. A pond "
                                                          "is still water pressed into the plateau, left out (layout.ponds_dropped) "
                                                          "where it reaches the map's edge, a rim, a crack or the mountain. "
                                                          "Kept, as every setting is, until given again; chasm_grid's river_list "
                                                          "marks the placed ones"}}},
                {"relief",{{"type","object"},{"description","height painted by hand (docs/water_tools_plan.md step 5), the whole "
                                                           "of it: {\"strokes\":[{\"points\":[[x,z],...],\"radius\":12,\"height\":6}]} "
                                                           "- one point a hill, several a ridge along them; height -20..20 (below 0 "
                                                           "a hollow or a valley), radius 4..60, falling off smoothly; strokes add "
                                                           "up; {} for none. On the plateau, before the rivers flatten it; lowering "
                                                           "fades out near a drop. Kept until given again, as the water is"}}},
                {"cliffs",{{"type","object"},{"description","cliffs painted by hand (docs/water_tools_plan.md step 6), the whole "
                                                           "of them: {\"strokes\":[{\"points\":[[x,z],...],\"width\":1,\"depth\":1}]} "
                                                           "- the plot nearest each point, and at a width past 1 (plots across, "
                                                           "1..5) every plot within half of it of the line through them, set to "
                                                           "depth steps of 6 below the plateau (1..4; 0 takes earlier strokes' "
                                                           "plots back up), later strokes over earlier ones; {} for none. Only "
                                                           "the plateau's dry plots, off the map's edge, the mountain and a pond; "
                                                           "chasm_grid's layout.cliffs says what they came to. Kept until given "
                                                           "again, as the water is"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const Grid> g = GetGrid();
            GridSettings s = (g && !args.value("defaults",false)) ? g->settings : GridSettings();
            if (args.contains("water")){
                std::string error;
                if (!GridWaterFromJson(args["water"],s.water,error)){
                    return json{{"error",error}};
                }
            }
            if (args.contains("relief")){
                std::string error;
                if (!GridReliefFromJson(args["relief"],s.relief,error)){
                    return json{{"error",error}};
                }
            }
            if (args.contains("cliffs")){
                std::string error;
                if (!GridCliffsFromJson(args["cliffs"],s.cliffs,error)){
                    return json{{"error",error}};
                }
            }
            if (args.contains("kind")){
                int kind = GridKindByName(args["kind"].is_string() ? args["kind"].get<std::string>() : std::string());
                if (kind < 0){
                    return json{{"error","kind is chasm, flat, title or cracks"}};
                }
                s.kind = kind;
            }
            s.seed = args.value("seed",s.seed);
            s.target_fine_cells = std::max(100,std::min(2000000,args.value("target_fine_cells",s.target_fine_cells)));
            s.aspect = args.value("aspect",s.aspect);
            s.triangle_side = args.value("triangle_side",s.triangle_side);
            s.relax_passes_coarse = args.value("relax_passes_coarse",s.relax_passes_coarse);
            s.relax_passes_fine = args.value("relax_passes_fine",s.relax_passes_fine);
            s.relax_strength = args.value("relax_strength",s.relax_strength);
            RegenerateGrid(s);
            if (args.value("frame_map",false)){
                //As chasm_view moves the camera: between ticks, pitch in degrees.
                main_scene->AtTickBoundary([&](){
                    FrameMap();
                    if (args.contains("pitch")){
                        cam_pitch = std::max(CHASM_CAM_PITCH_MIN,std::min(CHASM_CAM_PITCH_MAX,
                                             args["pitch"].get<float>() / 57.29578f));
                        ApplyCamera();
                    }
                });
            }
            bool f_shot = args.value("include_screenshot",false);
            if (f_shot && renderer){
                //One frame thrown away, so the one returned has the new world uploaded and drawn.
                renderer->RequestScreenshot(false);
            }
            return MaybeAttachScreenshot(GridStatsJson(*GetGrid()),f_shot,args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_view",
        "Move the camera and, in a debug build, switch the grid view's layers. The camera orbits a "
        "point on the ground: target_x/target_z, distance, yaw and pitch in degrees (pitch above the "
        "horizon; yaw 0 looks toward -z). frame_map puts the whole map in view; focus_issue sends "
        "the camera to issue n of the last chasm_check and highlights it. Layers: fine, coarse, "
        "valence (stars where 3, 5 or 6 quads meet), squareness (each quad inset, green square to red "
        "skewed), issues (the last check's marks). Returns the camera, the layers and how many cells "
        "of the exploration's raster are explored (2 units a cell); screenshot optional.",
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
                {"forest",{{"type","boolean"},{"description","the trees, rocks and bushes (all builds)"}}},
                {"water",{{"type","boolean"},{"description","the rivers and falls (all builds)"}}},
                {"mist",{{"type","boolean"},{"description","the chasm's mist and the falls' foam (all builds)"}}},
                {"clouds",{{"type","boolean"},{"description","the clouds over unexplored ground in debug mode (play mode always draws them)"}}},
                {"play",{{"type","boolean"},{"description","play mode (the panels hidden, the game camera, the clouds) or debug mode - U at the desk"}}},
                {"flat",{{"type","boolean"},{"description","draw the grid layers at y = 0 instead of on the terrain - the true cell shapes near cliffs; pair with terrain:false"}}},
                {"camp",{{"type","boolean"},{"description","where a new colony would camp, and each spot's score (debug builds)"}}},
                {"areas",{{"type","boolean"},{"description","the areas to discover (areas_plan.md): each edged in its kind's colour, bright once seen, a post at its middle (debug builds; chasm_areas lists them)"}}},
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
            layer("camp",f_view_camp);
            layer("areas",f_view_areas);
            if (changed){
                view_version++;
            }
#endif
            if (args.contains("terrain")){
                f_view_terrain = args["terrain"].get<bool>();
            }
            if (args.contains("forest")){
                f_view_forest = args["forest"].get<bool>();
            }
            if (args.contains("water")){
                f_view_water = args["water"].get<bool>();
            }
            if (args.contains("mist")){
                f_view_mist = args["mist"].get<bool>();
            }
            if (args.contains("clouds")){
                f_view_clouds = args["clouds"].get<bool>();
            }
            //Play mode is the panels hidden - what U does at the desk (play_mode_plan.md).
            if (args.contains("play")){
                f_show_ui = !args["play"].get<bool>();
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
                                    {"flat",f_view_flat.load()},{"camp",f_view_camp.load()},{"areas",f_view_areas.load()},
                                    {"terrain",f_view_terrain.load()},
                                    {"forest",f_view_forest.load()},{"water",f_view_water.load()},{"mist",f_view_mist.load()}};
#endif
            std::shared_ptr<const Exploration> ex = GetExploration();
            if (ex && !ex->Empty()){
                result["explored"] = json{{"cells",ex->CountExplored()},{"of",(int)ex->cells.size()}};
            }
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
        "off the map clears it. With no point, returns the current hover and selection. With to_x/to_z "
        "as well, the way a villager would walk from x/z to there: the A* route's plots (Walkers::"
        "PlanRoute) or none, and whether the straight line is clear (EconomyStraightClear).",
        json{
            {"type","object"},
            {"properties",{
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"px",{{"type","integer"},{"description","window pixel, from the left"}}},
                {"py",{{"type","integer"},{"description","window pixel, from the top"}}},
                {"select",{{"type","boolean"}}},
                {"trees_within",{{"type","number"},{"description","list the trees within this distance of x/z, nearest first, standing, log or stump"}}},
                {"sight",{{"type","boolean"},{"description","what a person standing here would see (play_mode_plan.md, sight): the eye's ground, how many cells, and the cells as a save's explored run lengths - read only"}}},
                {"to_x",{{"type","number"},{"description","with to_z: the walk from x/z to here"}}},
                {"to_z",{{"type","number"}}},
                {"posted",{{"type","boolean"},{"description","that walk as a guard or someone under an order takes it - a castle's decks too (ramparts_plan.md)"}}},
                {"from_layer",{{"type","integer"},{"description","that walk from this layer of x/z's plot, in half storeys - 0 its ground"}}},
                {"surface",{{"type","array"},{"items",{{"type","number"}}},
                            {"description","[x,y,z]: a surface point, as the engine reads one under the mouse - with `normal`, the plot "
                                           "and layer the mouse there picks (PickSurface, ramparts_plan.md)"}}},
                {"normal",{{"type","array"},{"items",{{"type","number"}}},{"description","[x,y,z]: that surface's normal"}}},
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
            //A surface the mouse would read: what it picks, plot and layer (PickUnderMouse).
            if (args.contains("surface") && args["surface"].is_array() && args["surface"].size() == 3){
                const json& sv = args["surface"];
                vec3 at(sv[0].get<float>(),sv[1].get<float>(),sv[2].get<float>());
                vec3 n(0.0f,1.0f,0.0f);
                if (args.contains("normal") && args["normal"].is_array() && args["normal"].size() == 3){
                    n = vec3(args["normal"][0].get<float>(),args["normal"][1].get<float>(),args["normal"][2].get<float>());
                }
                GridPick sp = PickSurface(at,n);
                result["surface_pick"] = json{{"hit",sp.f_hit},{"plot",sp.f_hit ? sp.plot : -1},{"layer",sp.layer}};
            }
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
                //What is painted there - the plot's ground kind and whether it is built - for scripts.
                std::shared_ptr<const ZoneState> z = GetZones();
                if (pick.f_hit && z && pick.plot < (int)z->ground.size()){
                    result["zone_ground"] = ZoneGroundName(z->ground[pick.plot]);
                    result["zone_ground_built"] = (bool)z->ground_built[pick.plot];
                    result["zone_line"] = z->line[pick.plot];
                    result["zone_gate"] = (bool)z->gate[pick.plot];
                    //The debug castle's piece on it (ramparts_plan.md).
                    if (ZoneCastleShape(*z,pick.plot) != ZONE_CASTLE_NONE){
                        result["zone_castle"] = json{{"shape",ZoneCastleShapeName(ZoneCastleShape(*z,pick.plot))},
                                                     {"storeys",ZoneCastleStoreys(*z,pick.plot)},
                                                     {"layer",ZoneCastleLayer(*z,pick.plot)},
                                                     {"level",ZoneCastleBase(*z,pick.plot)},
                                                     {"top_y",(z->world && z->world == GetWorld())
                                                               ? BuildingLayerHeight(*z->world,*z,pick.plot,ZoneCastleLayer(*z,pick.plot))
                                                               : 0.0f},
                                                     {"over",CastleOverJson(*z,pick.plot)}};
                    }
                    //Its ground (terraces_plan.md stage 2): a terrace's top, or the terrain's - and the step to each
                    //neighbour, open, a wall, a stair or a building's upper door, with its storey (ZoneTerraceStep, stage 3).
                    std::shared_ptr<const ChasmWorld> zw = GetWorld();
                    if (zw && z->world == zw){
                        result["zone_ground_y"] = ZoneGroundAt(*zw,*z,pick.plot);
                        if (ZoneTerraced(*z,pick.plot)){
                            result["zone_terrace"] = json{{"top",(int)z->terrace[pick.plot]},
                                                          {"built",(bool)z->terrace_built[pick.plot]}};
                        }
                        json steps = json::array();
                        int nb[8];
                        int n = ZonePlotNeighbours(*zw,pick.plot,nb,8);
                        static const char* step_names[] = {"open","wall","stair","door"};
                        for (int i = 0; i < n; i++){
                            int step = ZoneTerraceStep(*zw,*z,pick.plot,nb[i]);
                            json s = json{{"plot",nb[i]},{"ground_y",ZoneGroundAt(*zw,*z,nb[i])},{"step",step_names[step]}};
                            if (step == ZONE_STEP_DOOR){
                                s["door_storey"] = std::max(ZoneUpperDoor(*zw,*z,pick.plot,nb[i]),ZoneUpperDoor(*zw,*z,nb[i],pick.plot));
                            }
                            steps.push_back(s);
                        }
                        result["zone_steps"] = steps;
                    }
                    //An arch over it (arches_plan.md): its storeys, the building it is part of, and its floor's height.
                    if (z->ArchOf(pick.plot) > 0){
                        result["zone_arch"] = json{{"storeys",z->ArchOf(pick.plot)},{"owner",z->ArchOwnerOf(pick.plot)},
                                                   {"floors",ZoneArchFloors(z->ArchOf(pick.plot))},
                                                   {"built",z->ArchBuiltOf(pick.plot)},
                                                   {"floor_y",z->ArchFloorOf(pick.plot,0) * ZONE_STOREY_HEIGHT * 0.5f}};
                    }
                }
                //What a person standing here would see (Exploration.h, SIGHT), on a blank raster of its
                //own: the game's exploration is not touched, so a script may ask anywhere at any time.
                std::shared_ptr<const ChasmWorld> sw = GetWorld();
                if (pick.f_hit && args.value("sight",false) && sw && sw->sight){
                    Exploration seen;
                    seen.Reset(sw->grid->bounds_min,sw->grid->bounds_max);
                    int cell = seen.CellAt(pick.at);
                    if (cell >= 0){
                        float eye_ground = SightEyeGround(*sw,seen,cell,pick.at);
                        seen.See(*sw->sight,cell,eye_ground);
                        result["sight"] = json{{"eye_ground",eye_ground},{"cells",seen.CountExplored()},
                                               {"seen",seen.ToString()}};
                    }
                }
                //The walk from here to to_x/to_z, as a villager would take it - for checking what shuts
                //a way (a palisade, line_works_plan.md) without sending anyone.
                if (pick.f_hit && args.contains("to_x") && args.contains("to_z")){
                    vec2 to_at(args["to_x"].get<float>(),args["to_z"].get<float>());
                    GridPick to = p->Pick(to_at);
                    if (to.f_hit){
                        std::vector<WalkPlace> places;
                        std::vector<float> speeds;
                        bool f_route = false, f_straight = false;
                        //Posted (ramparts_plan.md): as a guard or someone under an order walks, decks and all.
                        int who = args.value("posted",false) ? WALK_WHO_POSTED : WALK_WHO_VILLAGER;
                        WalkPlace from;
                        from.plot = pick.plot;
                        from.layer = (uint8_t)std::max(0,std::min(255,args.value("from_layer",0)));
                        //The walkers are the physics thread's; plan between ticks.
                        main_scene->AtTickBoundary([&](){
                            const ZoneState& zs = zones.State();
                            std::shared_ptr<const ChasmWorld> wz = zones.GetWorld();
                            if (wz){
                                f_route = walkers.PlanRoute(zs,from,to.plot,who,places,speeds);
                                f_straight = EconomyStraightClear(*wz,zs,pick.at,to.at,pick.plot,to.plot);
                            }
                        });
                        json plots = json::array();
                        json layers = json::array();
                        for (const WalkPlace& wp : places){
                            plots.push_back(wp.plot);
                            layers.push_back((int)wp.layer);
                        }
                        result["route"] = json{{"found",f_route},{"plots",plots},{"layers",layers},{"straight_clear",f_straight}};
                    }
                }
                //The trees round the point, nearest first, with what the economy has done to them.
                float within = args.value("trees_within",0.0f);
                std::shared_ptr<const ChasmWorld> w = GetWorld();
                if (within > 0.0f && w && w->forest){
                    std::shared_ptr<const EconomyState> e = GetEconomy();
                    vec2 at(args.value("x",pick.at.x),args.value("z",pick.at.y));
                    std::vector<std::pair<float,int>> found;
                    const std::vector<PropInstance>& props = w->forest->props;
                    for (size_t i = 0; i < props.size(); i++){
                        float d = (vec2(props[i].pos.x,props[i].pos.z) - at).length();
                        if (props[i].kind <= PROP_SNOW_PINE && d <= within){
                            found.push_back(std::make_pair(d,(int)i));
                        }
                    }
                    std::sort(found.begin(),found.end());
                    static const char* states[] = {"standing","log","stump"};
                    json trees = json::array();
                    for (size_t t = 0; t < found.size() && t < 40; t++){
                        const PropInstance& pr = props[found[t].second];
                        int st = (e && found[t].second < (int)e->prop_state.size()) ? e->prop_state[found[t].second] : 0;
                        trees.push_back(json{{"prop",found[t].second},{"kind",prop_asset_names[pr.kind]},{"x",pr.pos.x},
                                             {"z",pr.pos.z},{"state",states[std::min(2,st)]}});
                    }
                    result["trees"] = trees;
                }
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
        "plot and no field; a field needs a flat coarse cell and no house; at most 4 storeys), "
        "arch_floors_lost (an arch's built floors it took down), and the zone totals. Needs the simulation running (not paused) to be applied. "
        "BUILDINGS (docs/buildings_plan.md): the house_* ops are build_paint/add/remove/erase, for any "
        "kind (house, store, woodcutter, water); calls with the same `stroke` are one drag and so one "
        "building - without one, each call starts its own. FIELDS (docs/fields_plan.md): a field painted "
        "with play:true is a site (2 wood a cell), by debug there at once - empty either way; field_crop sets "
        "its STARTING crop (wheat, greens, beans, or hemp - not food, for rope: docs/goods_plan.md), which its "
        "farmer then sows; field_next its NEXT crop for "
        "every sowing after (a crop, or auto: the most food, never hemp); field_land gives it to `house` (a house's "
        "building id, 0 automatic - docs/land_plan.md); field_priority puts a field still being built first "
        "(`on`, default true; false puts it back) - a building's is chasm_economy site_priority. A field's "
        "starting crop may be set while it is still a site. GATES (docs/defence_plan.md, step 3): gate_shut "
        "shuts the door of the gate at the place (`on`, default true; false opens it), the gate plots joined to "
        "it too - shut, nobody walks through, the colony's own people neither. The result names the building at "
        "the place, and `gate_here` says none, open or shut. TOWERS: tower_ladder moves a guard tower's ladder - its "
        "door - toward the neighbour at to_x/to_z or `toward` (a plot), or with neither to its next open side. "
        "THE DEBUG CASTLE (docs/ramparts_plan.md): castle puts a grey stone piece on the plot - `shape` block (solid, a "
        "deck on top), arch (a deck over an open passage, a road may run under it) or stair (rises its last storey), "
        "`storeys` high - or with shape none takes it off, the top one first. Painted where a piece stands, an arch "
        "two storeys or more over the piece's top goes OVER it, its pillars on it, and a stair one storey over an arch "
        "goes ON it, climbing over the passage (`over`); anything else takes the piece's place. Debug only (refused "
        "with play). Its decks are walked only by the posted: an order onto a piece stands him on its top deck. "
        "`castle_here` reports the plot's piece. STOPPING WORK (docs/stopping_work_plan.md): work_stop stops the "
        "workplace at the place - a water or steam collector, condenser, woodcutter's hut, ropewalk or builders' hut (`on`, default true; "
        "false sets it working again) - so it takes nobody and its people go free; range_stop the same for an archery "
        "range, by a cell of it. CANCEL (docs/construction_plan.md): site_cancel takes the building at the plot off whole "
        "while nothing of it stands - a site card's Cancel, a bridge too - field_cancel the same by a cell of a field or "
        "range; what it was brought is left as a crate (origin site). TERRACES (docs/terraces_plan.md stage 2): terrace puts a "
        "terrace on the plot, its top `top` storeys up (by default the plot's own highest ground rounded up), standing at "
        "once by debug, a site with play:true; terrace_erase takes it off, with nothing on it.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"enum",{"house_add","house_paint","house_remove","house_erase","field_paint","field_erase",
                                                  "ground_paint","ground_erase","build_add","build_paint","build_remove",
                                                  "build_erase","field_crop","field_next","field_land","field_priority",
                                                  "gate_shut","range_paint","tower_ladder","castle","work_stop","range_stop",
                                                  "site_cancel","field_cancel","terrace","terrace_erase"}}}},
                {"top",{{"type","integer"},{"description","for terrace: its top in storeys (whole storeys from 0)"}}},
                {"shape",{{"type","string"},{"enum",{"block","arch","stair","none"}},
                          {"description","for castle: the debug castle's piece (ramparts_plan.md) - none takes it off"}}},
                {"storeys",{{"type","integer"},{"description","for castle: its height, 1 to 4 (an arch 2 at least; a stair rises "
                                                             "its last storey); default 2"}}},
                {"on",{{"type","boolean"},{"description","for field_priority: first (default) or back in the queue; "
                                                        "for gate_shut: shut (default) or open; for work_stop and range_stop: "
                                                        "stopped (default) or working"}}},
                {"kind",{{"type","string"},{"enum",{"house","store","woodcutter","water","winch","builders","tower","ropewalk","stonecutter","mine","well","steam","condenser","sail_loft"}},{"description","for build_* (default house); a well is a debug tool, refused with play:true (water_tools_plan.md)"}}},
                {"crop",{{"type","string"},{"enum",{"wheat","greens","beans","hemp","auto"}},{"description","for field_crop (a crop) and field_next (a crop or auto)"}}},
                {"stroke",{{"type","integer"},{"description","calls with the same stroke are one drag: one building"}}},
                {"house",{{"type","integer"},{"description","for field_land: the house's building id, 0 automatic"}}},
                {"owner",{{"type","integer"},{"description","for ground_paint of a garden or lot: the house or woodcutter whose "
                                                            "yard it is - it must touch it or its yard (docs/yards_plan.md); 0 "
                                                            "(default) whatever it touches. For build_paint/build_add: the "
                                                            "building of `kind` to extend - the plot must join it, or is refused "
                                                            "(what painting with it selected does); 0 (default) the stroke's"}}},
                {"ground",{{"type","string"},{"enum",{"garden","lot","road","wall"}},
                           {"description","for ground_paint; for ground_erase, wall takes a wall off (or a gate off its road) and nothing else"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"cell",{{"type","integer"}}},
                {"play",{{"type","boolean"},{"description","as a PLAY command: refused under the clouds (default false: a debug command, as every tool's)"}}},
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
            int op = ZoneOpByName(name);
            if (op <= ZONE_OP_NONE){
                return json{{"error","unknown op " + name}};
            }
            bool f_field = ZoneOpOnCell(op);
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
            cmd.flags = SIM_CMD_FLAG_RECORD;     //a tool painting is a player too - see SubmitZone
            bool f_owned = op == ZONE_OP_GROUND_PAINT || op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD;
            cmd.value[3] = ZoneCommandValue3(args.value("play",false),f_owned ? args.value("owner",0u) : 0u);
            cmd.subtype = (uint32_t)index;
            cmd.value[0] = (float)op;
            std::string ground_name = args.value("ground",std::string("garden"));
            int ground = ZONE_GROUND_GARDEN;
            for (int k = ZONE_GROUND_GARDEN; k < ZONE_GROUND_COUNT; k++){
                if (ground_name == ZoneGroundName(k)){
                    ground = k;
                }
            }
            cmd.value[1] = (float)ground;
            if (op >= ZONE_OP_BUILD_PAINT && op <= ZONE_OP_BUILD_ERASE){
                int kind = ZoneKindByName(args.value("kind",std::string("house")));
                if (kind <= ZONE_KIND_NONE || kind == ZONE_KIND_FIELD){
                    return json{{"error","no such building kind"}};
                }
                cmd.value[1] = (float)kind;
            }else if (op == ZONE_OP_FIELD_CROP || op == ZONE_OP_FIELD_NEXT){
                //A crop or "auto", for the start as for what follows (fields_plan.md, issues_plan.md).
                int crop = ZoneCropByName(args.value("crop",std::string(op == ZONE_OP_FIELD_NEXT ? "auto" : "wheat")));
                if (crop < 0 || crop == ZONE_CROP_NONE){
                    return json{{"error","no such crop"}};
                }
                cmd.value[1] = (float)crop;
            }else if (op == ZONE_OP_FIELD_LAND){
                cmd.value[1] = (float)args.value("house",0u);
            }else if (op == ZONE_OP_FIELD_PRIORITY || op == ZONE_OP_GATE_SHUT || op == ZONE_OP_WORK_STOP || op == ZONE_OP_RANGE_STOP){
                cmd.value[1] = args.value("on",true) ? 1.0f : 0.0f;
            }else if (op == ZONE_OP_TOWER_LADDER){
                //Toward the neighbour at to_x/to_z, or `toward` (a plot) - without either, the next open side.
                int toward = args.value("toward",-1);
                if (args.contains("to_x") && args.contains("to_z")){
                    GridPick to = w->picker->Pick(vec2(args["to_x"].get<float>(),args["to_z"].get<float>()));
                    toward = to.f_hit ? to.plot : -1;
                }
                cmd.value[1] = (float)toward;
            }else if (op == ZONE_OP_CASTLE){
                //The debug castle (ramparts_plan.md): a shape and its storeys, or shape "none" to take it off.
                std::string shape_name = args.value("shape",std::string("block"));
                int shape = (shape_name == "none") ? ZONE_CASTLE_NONE : ZoneCastleShapeByName(shape_name);
                if (shape < 0){
                    return json{{"error","no such castle shape - block, arch, stair or none"}};
                }
                cmd.value[1] = (shape == ZONE_CASTLE_NONE) ? 0.0f
                                                           : (float)ZoneCastlePiece(shape,args.value("storeys",2));
            }else if (op == ZONE_OP_TERRACE){
                //A terrace (terraces_plan.md stage 2): `top` in storeys, or by default the one a drag from this plot
                //alone would give it (its highest ground rounded up) - or the terrace it extends.
                std::shared_ptr<const ZoneState> z = GetZones();
                int top = (z && z->world == w) ? ZoneTerraceTopOver(*w,*z,std::vector<int>(1,index)) : ZONE_NO_TERRACE;
                cmd.value[1] = (float)args.value("top",top);
            }
            //A PLAY command for what a discovery has not brought yet is refused, as the build bar hides its tool
            //(demo_plan.md step 6).
            if (args.value("play",false)){
                int tool = -1;
                if (op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD){
                    int kind = (int)cmd.value[1];
                    tool = (kind == ZONE_KIND_WATER) ? CHASM_TOOL_WATER : (kind == ZONE_KIND_WINCH) ? CHASM_TOOL_WINCH
                         : (kind == ZONE_KIND_TOWER) ? CHASM_TOOL_TOWER : (kind == ZONE_KIND_STONECUTTER) ? CHASM_TOOL_STONECUTTER
                         : (kind == ZONE_KIND_MINE) ? CHASM_TOOL_MINE : (kind == ZONE_KIND_STEAM) ? CHASM_TOOL_STEAM
                         : (kind == ZONE_KIND_CONDENSER) ? CHASM_TOOL_CONDENSER
                         : (kind == ZONE_KIND_SAIL_LOFT) ? CHASM_TOOL_SAIL_LOFT : -1;
                }else if (op == ZONE_OP_GROUND_PAINT && ground == ZONE_GROUND_WALL){
                    tool = CHASM_TOOL_WALL;
                }else if (op == ZONE_OP_RANGE_PAINT){
                    tool = CHASM_TOOL_RANGE;
                }else if (op == ZONE_OP_DOCK_PAINT){
                    tool = CHASM_TOOL_DOCK;
                }
                int need = ChasmToolNeeds(tool);
                if (need >= 0 && !((discovered_mask.load() >> need) & 1u)){
                    return json{{"error",std::string("not discovered yet: ") + ChasmDiscoveryName(need)}};
                }
            }
            //A tool's strokes in a range of their own, clear of the mouse's (UpdatePaint).
            if (args.contains("stroke")){
                cmd.value[2] = (float)((1u << 22) + (args["stroke"].get<uint32_t>() & 0x3FFFFF));
            }
            uint32_t before = zone_commands_done.load();
            //SubmitCommandAndWait, not SubmitZone: the tool reports what the command DID, so it has
            //to have run - see Application::SubmitCommandAndWait.
            SubmitCommandAndWait(cmd);
            bool f_ran = zone_commands_done.load() != before;
            std::shared_ptr<const ZoneState> z = GetZones();
            std::string refusal;
            int arch_lost = 0;
            {
                std::lock_guard<std::mutex> lock(grid_mutex);
                refusal = zone_last_refusal;
                arch_lost = zone_last_arch_lost;
            }
            json result{{"op",name},{f_field ? "cell" : "plot",index},{"ran",f_ran},{"refusal",refusal},
                        {"arch_floors_lost",arch_lost}};
            if (z && z->world == w){
                ZoneStats st = ComputeZoneStats(*w,*z);
                result["totals"] = json{{"houses",st.buildings[ZONE_KIND_HOUSE]},{"stores",st.buildings[ZONE_KIND_STORE]},
                                        {"woodcutters",st.buildings[ZONE_KIND_WOODCUTTER]},
                                        {"water_collectors",st.buildings[ZONE_KIND_WATER]},
                                        {"winches",st.buildings[ZONE_KIND_WINCH]},
                                        {"builders_huts",st.buildings[ZONE_KIND_BUILDERS]},
                                        {"fields",st.buildings[ZONE_KIND_FIELD]},{"ranges",st.buildings[ZONE_KIND_RANGE]},
                                        {"towers",st.buildings[ZONE_KIND_TOWER]},
                                        {"steam_collectors",st.buildings[ZONE_KIND_STEAM]},
                                        {"condensers",st.buildings[ZONE_KIND_CONDENSER]},
                                        {"sail_lofts",st.buildings[ZONE_KIND_SAIL_LOFT]},
                                        {"docks",st.buildings[ZONE_KIND_DOCK]},
                                        {"built_plots",st.built_plots},
                                        {"storeys",st.storeys},{"field_cells",st.field_cells},
                                        {"floor_area",st.floor_area[ZONE_KIND_HOUSE]},{"field_area",st.floor_area[ZONE_KIND_FIELD]}};
                if (!f_field){
                    result["storeys_here"] = z->storeys[index];
                    result["standing_here"] = z->standing[index];
                    result["ground_here"] = ZoneGroundName(z->ground[index]);
                    result["ground_built_here"] = (bool)z->ground_built[index];
                    result["ground_owner_here"] = ZoneGroundOwner(*z,index);    //whose yard, 0 nobody's (yards_plan.md)
                    result["line_here"] = z->line[index];
                    result["gate_here"] = !z->gate[index] ? "none" : ZoneGateShut(*z,index) ? "shut" : "open";
                    result["castle_here"] = json{{"shape",ZoneCastleShapeName(ZoneCastleShape(*z,index))},
                                                 {"storeys",ZoneCastleStoreys(*z,index)},{"layer",ZoneCastleLayer(*z,index)},
                                                 {"over",CastleOverJson(*z,index)}};
                    result["building_here"] = BuildingJson(*w,*z,z->building[index]);
                }else{
                    result["field_here"] = (bool)z->field[index];
                    result["building_here"] = BuildingJson(*w,*z,z->field[index]);
                }
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_save",
        "Save the map: its generation settings (seed, size, feature lines) and everything painted on "
        "it, to saves/<name>.json beside the app. Nothing generated is stored - loading generates "
        "the map again. The same JSON is what an input recording starts from (its `state` line), so "
        "input_record captures a save and input_replay starts from it. Returns the path and totals.",
        json{
            {"type","object"},
            {"properties",{{"name",{{"type","string"}}}}},
            {"required",{"name"}}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            ChasmSave s = MakeSave();
            std::string error;
            if (!ChasmSaveWrite(name,ChasmSaveToJson(s),error)){
                return json{{"error",error}};
            }
            SetColonySave(name);
            return json{{"path",ChasmSavePath(name)},{"buildings",s.buildings.size()},
                        {"world_hash",s.world_hash},{"saves",ChasmSaveList()}};
        });

    MCPServer::Get()->RegisterTool("chasm_load",
        "Load saves/<name>.json: generate its map again if the current one has other settings, then "
        "paint its zones back through the rules (so nothing invalid can come of a stale save). "
        "Returns what was loaded, and a warning if the settings now generate a different map than "
        "the one saved. With no name, lists the saves.",
        json{
            {"type","object"},
            {"properties",{{"name",{{"type","string"}}}}}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            if (name.empty()){
                return json{{"saves",ChasmSaveList()}};
            }
            json j;
            ChasmSave save;
            std::string error;
            if (!ChasmSaveRead(name,j,error) || !ChasmSaveFromJson(j,save,error)){
                return json{{"error",error}};
            }
            bool f_ok = false;
            main_scene->AtTickBoundary([&](){
                f_ok = LoadSave(save,error);
            });
            SetColonySave(name);
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            char hash[32];
            snprintf(hash,sizeof(hash),"%016llx",(unsigned long long)w->grid->Hash());
            json result{{"loaded",name},{"ok",f_ok},{"buildings",save.buildings.size()},{"world_hash",hash}};
            if (!error.empty()){
                result["error"] = error;
            }
            if (!save.world_hash.empty() && save.world_hash != hash){
                result["warning"] = "saved on world " + save.world_hash + ", the settings now give " + hash;
            }
            return result;
        });

    MCPServer::Get()->RegisterTool("chasm_tool",
        "Pick the mouse's tool, as keys 1-0, H, C, J, G, R, T and K do: select, house, field, erase, garden, lot, road, store, "
        "woodcutter, water, winch, camp, bridge, builders, wall, range, tower, ropewalk - and the debug panel's castle, well, river, "
        "pond, height and cliff, never in play (water_tools_plan.md: `width`, `height`, `radius` and `depth` set the map tools' "
        "sizes, as the panel's sliders do). With a paint tool the hover outline goes green where a click would "
        "paint and red where the rules refuse - in play mode, the ghost of what it would place, `ghost` its colour (green, "
        "yellow where only a crate lying there stops it, red, or none) and `cost` its price "
        "tag as last drawn (empty for none; the ghost follows a frame behind, so ask again after moving the hover), "
        "`slope_grid` how many plots or cells the grid round it shows the land allowing, allowing on a foundation "
        "(stone, docs/terraces_plan.md) and refusing the tool - "
        "during a drag the whole of it, `drag_steps` the plots or cells it has crossed. "
        "hover_x/hover_z pin the hover to that point (a script has no mouse), until a call with hover_release.",
        json{
            {"type","object"},
            {"properties",{
                {"tool",{{"type","string"},{"enum",{"select","house","field","erase","garden","lot","road",
                                                    "store","woodcutter","water","winch","camp","bridge","builders","wall",
                                                    "range","tower","ropewalk","castle","stonecutter","mine","well",
                                                    "river","pond","height","cliff","steam","condenser","terrace",
                                                    "sail_loft","dock"}}}},
                {"hover_x",{{"type","number"}}},
                {"hover_z",{{"type","number"}}},
                {"hover_release",{{"type","boolean"},{"description","the hover follows the mouse again"}}},
                {"button",{{"type","string"},{"enum",{"down","up","mouse"}},
                           {"description","the left button at the pinned hover: down presses and holds it, up lets go, "
                                          "mouse gives it back - with hover_x/hover_z between, a script's drag (every "
                                          "painting tool draws its trail and places it on up, docs/drag_plan.md; the "
                                          "winch, tower and mine place on the click alone). Allow a frame between calls."}}},
                {"right_click",{{"type","boolean"},{"description","a right press: drops a road or other drag being drawn"}}},
                {"width",{{"type","number"},{"description","the river tool's width for the next river, as the panel's slider "
                                                         "(water_tools_plan.md) - or, with the cliff tool in hand (picked in this "
                                                         "call or before), the next cliff's width in plots, 1..5"}}},
                {"height",{{"type","number"},{"description","the height tool's height for the next stroke, -20..20"}}},
                {"radius",{{"type","number"},{"description","...and its radius, 4..60"}}},
                {"depth",{{"type","integer"},{"description","the cliff tool's depth for the next stroke, in steps below the "
                                                         "plateau: 1..4, or 0 to take painted cliffs back up"}}}
            }}
        },
        [this](const json& args) -> json {
            //In ChasmTool order - every tool, so the name returned is always one of these. One short (the range's,
            //2026-10-09) left a NULL at the end that every name was compared against: any call crashed.
            const char* names[] = {"select","house","field","erase","garden","lot","road",
                                   "store","woodcutter","water","winch","camp","bridge","builders","wall",
                                   "range","tower","ropewalk","castle","stonecutter","mine","well","river","pond","height",
                                   "cliff","steam","condenser","terrace","sail_loft","dock"};
            static_assert(sizeof(names) / sizeof(names[0]) == CHASM_TOOL_COUNT,"a name for every ChasmTool");
            //The map tools' sizes (water_tools_plan.md), the panel's sliders' for a script - `width` the cliff's in plots
            //when that is the tool in hand, else the river's.
            bool f_cliff = args.value("tool",std::string()) == "cliff" ||
                           (!args.contains("tool") && paint_tool.load() == CHASM_TOOL_CLIFF);
            if (args.contains("width") && args["width"].is_number() && f_cliff){
                cliff_width = std::max(1,std::min(GRID_CLIFF_WIDEST,(int)std::lround(args["width"].get<float>())));
                pick_version++;
            }else if (args.contains("width") && args["width"].is_number()){
                water_river_width = std::max(GRID_PLACED_RIVER_NARROWEST,std::min(GRID_PLACED_RIVER_WIDEST,args["width"].get<float>()));
            }
            if (args.contains("depth") && args["depth"].is_number()){
                cliff_depth = std::max(0,std::min(GRID_CLIFF_DEEPEST,(int)std::lround(args["depth"].get<float>())));
                pick_version++;
            }
            if (args.contains("height") && args["height"].is_number()){
                relief_height = std::max(-GRID_RELIEF_HIGHEST,std::min(GRID_RELIEF_HIGHEST,args["height"].get<float>()));
            }
            if (args.contains("radius") && args["radius"].is_number()){
                relief_radius = std::max(GRID_RELIEF_NARROWEST,std::min(GRID_RELIEF_WIDEST,args["radius"].get<float>()));
            }
            std::string t = args.value("tool",std::string());
            std::string locked;
            for (int i = 0; i < CHASM_TOOL_COUNT; i++){
                //Play mode's tools still waiting for a discovery are refused, as their keys are (demo_plan.md step 6) - and
                //the debug well always (water_tools_plan.md).
                if (t == names[i] && ToolLocked(i)){
                    locked = (i == CHASM_TOOL_WELL || ChasmMapTool(i))
                                 ? std::string("a debug tool, never in play mode")
                                                    : std::string("not discovered yet: ") + ChasmDiscoveryName(ChasmToolNeeds(i));
                }else if (t == names[i]){
                    paint_tool = i;
                    pick_version++;
                }
            }
            if (args.contains("hover_x") && args.contains("hover_z")){
                std::lock_guard<std::mutex> lock(pick_mutex);
                hover_pin = vec2(args["hover_x"].get<float>(),args["hover_z"].get<float>());
                f_hover_pinned = true;
            }
            std::string button = args.value("button",std::string());
            if (button == "down"){
                script_left = 1;
            }else if (button == "up"){
                script_left = 0;
            }else if (button == "mouse"){
                script_left = -1;
            }
            if (args.value("right_click",false)){
                f_script_right = true;
            }
            if (args.value("hover_release",false)){
                f_hover_pinned = false;
                script_left = -1;
            }
            int tool = paint_tool.load();
            int left = script_left.load();
            size_t line = 0;
            size_t drag = 0;
            size_t band = 0;
            std::string cost;
            int slope[3];
            int look = ghost_cost_look.load();
            {
                std::lock_guard<std::mutex> lock(pick_mutex);
                line = line_chain.size();
                drag = drag_chain.size();
                band = cliff_band.size();
                cost = ghost_cost_text;
                for (int i = 0; i < 3; i++){
                    slope[i] = slope_counts[i];
                }
            }
            json result{{"tool",(tool >= 0 && tool < CHASM_TOOL_COUNT) ? names[tool] : "?"},
                        {"hover_pinned",f_hover_pinned.load()},
                        {"button",(left < 0) ? "mouse" : (left ? "down" : "up")},
                        {"line_plots",line},
                        {"drag_steps",drag},
                        {"ghost",(look == GHOST_GREEN) ? "green" : (look == GHOST_YELLOW) ? "yellow" : (look == GHOST_RED) ? "red" : "none"},
                        {"cost",cost},
                        {"slope_grid",{{"allowed",slope[ZONE_LAND_ALLOWED]},{"foundation",slope[ZONE_LAND_FOUNDATION]},
                                       {"refused",slope[ZONE_LAND_REFUSED]}}}};
            //The cliff tool's sizes, and during its drag the plots the stroke would paint (water_tools_plan.md step 6).
            if (tool == CHASM_TOOL_CLIFF){
                result["cliff_depth"] = cliff_depth.load();
                result["cliff_width"] = cliff_width.load();
                result["cliff_plots"] = band;
            }
            if (!locked.empty()){
                result["refused"] = locked;
            }
            return result;
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

    MCPServer::Get()->RegisterTool("chasm_camp",
        "Where a new colony camps on the current map, and why (Zones.cpp, WHERE A NEW COLONY CAMPS): every spot "
        "looked at along the rivers, best first, with its score and the parts of it - flat (the share of its "
        "survey a house may stand on, and a field lie on), trees (1 at half the survey's plots wooded), middle "
        "(level with the map's middle) - or why it was refused. chasm_view's 'camp' layer draws them.",
        json{
            {"type","object"},
            {"properties",{
                {"limit",{{"type","integer"},{"description","spots to list (default 20)"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            auto t0 = std::chrono::steady_clock::now();
            std::vector<ZoneCampSpot> spots;
            ZoneCampSpots(*w,EconomySettlerCount(),spots);
            float ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
            //Best first, the refused after: the order ZoneStartCampPlot picks in.
            std::stable_sort(spots.begin(),spots.end(),[](const ZoneCampSpot& a, const ZoneCampSpot& b){
                if ((a.refused == NULL) != (b.refused == NULL)){
                    return a.refused == NULL;
                }
                return a.score > b.score;
            });
            auto round2 = [](float f){ return std::round((double)f * 100.0) / 100.0; };
            int passed = 0;
            for (const ZoneCampSpot& s : spots){
                passed += s.refused ? 0 : 1;
            }
            json list = json::array();
            int limit = args.value("limit",20);
            for (int i = 0; i < (int)spots.size() && i < limit; i++){
                const ZoneCampSpot& s = spots[i];
                json j{{"plot",s.plot},{"x",round2(w->grid->fine.pos[s.plot].x)},{"z",round2(w->grid->fine.pos[s.plot].y)}};
                if (s.refused){
                    j["refused"] = s.refused;
                }
                if (s.f_fallback){
                    j["fallback"] = true;   //no spot by a river passed
                }
                if (s.score > 0.0f){
                    j["score"] = round2(s.score);
                    j["flat"] = round2(s.flat);
                    j["trees"] = round2(s.trees);
                    j["middle"] = round2(s.middle);
                    j["house_share"] = round2(s.house_share);
                    j["field_share"] = round2(s.field_share);
                    j["tree_share"] = round2(s.tree_share);
                }
                list.push_back(j);
            }
            json result{{"looked_at",(int)spots.size()},{"passed",passed},{"ms",round2(ms)},{"spots",list}};
            int camp = ZoneStartCampPlot(*w,EconomySettlerCount());
            if (camp >= 0){
                result["camp"] = json{{"plot",camp},{"x",round2(w->grid->fine.pos[camp].x)},{"z",round2(w->grid->fine.pos[camp].y)}};
            }
            return result;
        });
    RegisterRoadTools();
    RegisterTimeTools();
    RegisterIssueTools();
    RegisterEconomyTools();
    RegisterZombieTools();
    RegisterSelectTools();
    RegisterBridgeTools();
    RegisterTitleTools();
    RegisterStatsTools();
    RegisterSoundTools();
    RegisterAreaTools();
}
#endif
