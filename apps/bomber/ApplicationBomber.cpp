#include "ApplicationBomber.h"

#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit - see the note at the top of it.
//Guarded because a build with USE_IMGUI=0 has no library behind this header, and every panel
//function below that would call it is compiled out too.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#ifdef USE_MCP
//MCPServer.h and nothing else from it: with USE_MCP=0 that class is not compiled, and the header
//also pulls winsock in with the include-order constraint it documents - both pointless in a build
//with no debug server. See the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include "Debug.h"
#include "Primitives.h"
#include "type_helpers.h"

#include <math.h>
#include <stdio.h>

static Debugger* debug = new Debugger("ApplicationBomber",DEBUG_ALL);

//Must match the BLAST_MODE_* defines in shaders/bomber_explosion.frag - the value travels as a
//uniform, so the two ends have to agree and there is nowhere for a compiler to notice if they
//stop agreeing.
#define BLAST_MODE_TILE     0
#define BLAST_MODE_CROSS    1

/*
    The wall pattern around a blast site, as offsets in cells from the bomb.

    IDENTICAL AROUND BOTH SITES, and that is the entire experimental control: the two sites draw
    the same blast with the same arm lengths in front of the same obstacles, so anything that
    differs on screen is a difference between the two ways of drawing it.

    The four are chosen to produce three different outcomes rather than to look like a maze:
    the hard wall two cells east clips that arm to one tile, the soft block two cells north does
    the same going the other way, and the two diagonal pillars block nothing at all - an arm runs
    along an axis, so they are there to give the eye something to judge the flame's size against
    and to catch its light.
*/
struct BomberWallOffset{ int dx; int dz; int kind; };
static const BomberWallOffset BOMBER_SITE_WALLS[] = {
    { 2, 0,BOMBER_WALL_HARD},   //clips the east arm to 1 tile
    { 0,-2,BOMBER_WALL_SOFT},   //clips the north arm to 1 tile
    {-2,-2,BOMBER_WALL_HARD},   //pillar, blocks nothing
    { 2, 2,BOMBER_WALL_HARD},   //pillar, blocks nothing
};

//Where the flame sits above the floor: the middle of a wall cube, so a blast reads as being at the
//height of the things it is going to destroy.
#define BOMBER_FLAME_HEIGHT     (BOMBER_WALL_SIZE * 0.5f)

ApplicationBomber::ApplicationBomber():Application(){
    app_name = "Bomber";
    debug->Info("Created new ApplicationBomber.\n");

    //All three engine windows up. The Explosion panel is what this app is driven from, but the
    //Scene tree and the Inspector earn their place here too: there are two sites' worth of volumes
    //and being able to pick one and read its transform is how you check a box is where you think.
    f_show_scene_window = true;
    f_show_inspector_window = true;
    f_show_engine_window = true;
}

void ApplicationBomber::Init(void){
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    /*
        PIPELINE_DEFERRED is not optional here, even though the scene is a few cubes: the deferred
        pass is what fills the G-buffer CustomShaderPass binds on units 1-3, and the blast clamps
        its march to the solid scene using exactly that. Without it a wall standing in the middle
        of the flame would be buried under the full depth of fire instead of the right fraction of
        it.
    */
    renderer->alpha_clip = 0.5f;
    //The plain background. With no skybox the frame clears to black, which is the right backdrop
    //for judging an emissive effect - anything else makes the fire's colour a matter of opinion.
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1280,800);

    /*
        60 ticks per second, not the default 50.

        The blast clock is counted in ticks, so the tick rate IS the frame rate of the explosion.
        A fireball's first fifth of a second is where all its expansion happens, and at 50 Hz that
        is ten frames to say it in. This app simulates almost nothing, so the extra ticks cost a
        loop iteration each.
    */
    SetPhysicsTPS(60.0f);

    main_scene = CreateNewScene("Bomber Scene");

    /*
        Framed so BOTH sites fit, from 45 degrees up.

        That angle is not a matter of taste here: a bomberman blast is a PLUS drawn on the floor,
        and from the low three-quarter view a scene like this usually wants, the two arms running
        away from the camera are foreshortened into the middle and the whole thing reads as a blob.
        It cost a filmstrip to notice, because each frame looked plausible on its own. Halfway up
        is where the cross is legible and the flame still has a visible silhouette.

        Both in frame is the whole point of the default: the two are here to be compared, and a
        default view showing one of them would make that take a camera move every time. The orbit
        is there for going in close on either.
    */
    camera_target = vec3(0.0f,1.0f,0.0f);
    main_scene->camera->SetPosition(vec3(0.0f,17.0f,17.0f));
    main_scene->camera->SetLookAt(camera_target);

    /*
        Draw from the shared stream ONCE, here, before anything else runs. Application::rrand is a
        single shared stream and there is an open backlog item about off-tick draws shifting it out
        from under the simulation; Init is the one place that cannot do that, because it runs
        before the physics thread exists. See core/RRandom.h.
    */
    rrand = new RRandom(1);

    BuildLighting();
    BuildGround();
    BuildWalls();
    BuildBlastNoise();
    BuildExplosion();
    BuildSites();
    BuildKnobTable();

    SetupInput();
    RegisterCommandHandlers();

#ifdef USE_MCP
    //Guarded because this app's own tools call into MCPServer, which USE_MCP=0 does not compile.
    //The CORE tools are switched a different way - see engine.mk.
    RegisterMCPTools();
#endif

    debug->Ok("Bomber ready - %i walls, arms E%.0f W%.0f N%.0f S%.0f, blast life %.0f ticks\n",
              (int)walls.size(),
              site_tiles.arm_limit.x,site_tiles.arm_limit.y,
              site_tiles.arm_limit.z,site_tiles.arm_limit.w,
              blast_life);
}

//--- the scene ----------------------------------------------------------------------------------

vec3 ApplicationBomber::CellCentre(int cx, int cz) const {
    //Centred on the origin rather than running from a corner, so the orbit camera has something
    //symmetric to turn around. When the real maze arrives it will want an origin corner instead;
    //this is the one line that changes.
    return vec3((float)cx * BOMBER_CELL_SIZE,0.0f,(float)cz * BOMBER_CELL_SIZE);
}

void ApplicationBomber::BuildLighting(void){
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-9,13,10));
    sun->color = vec3(1.0f,0.95f,0.88f);
    //Deliberately modest. The flame is the brightest thing in this scene by an order of magnitude
    //and it should stay that way; a sun bright enough to make the cubes pop would flatten the one
    //effect the app exists to look at.
    sun->brightness = 3.2f;
    //Half-extent in world units for the shadow ortho. The ground is 30 across, so 18 covers the
    //part of it anything stands on with room for the walls' shadows to fall off rather than be
    //clipped mid-shadow.
    sun->viewport.zoom = 18.0f;
    sun->SetLookAt(vec3());
    main_scene->AddObject(sun);
}

void ApplicationBomber::BuildGround(void){
    Material m;
    m.name = "bomber_ground";
    m.glsl_material.color = vec4(0.22f,0.23f,0.26f,1.0f);
    m.glsl_material.metallic = 0.05f;
    m.glsl_material.roughness = 0.85f;
    int material = renderer->AddMaterial(m);

    ground = new Object();
    ground->name = "Ground";
    //Wide rather than square: the two sites are side by side on X and nothing ever goes far on Z.
    ground->SetMesh(MakeQuad(30.0f,16.0f));
    //MakeQuad lies in XY facing +Z; a floor is that rotated a quarter turn back about X.
    ground->SetRotation(quat(vec3(1,0,0),-TYPE_PI * 0.5f));
    ground->SetMaterialSlot(0,material);
    ground->SetPickability(false);
    main_scene->AddObject(ground);
}

/*
    The walls, in the two colours a bomberman maze is made of.

    GREEN IS THE INDESTRUCTIBLE PILLAR GRID and BROWN IS THE DESTRUCTIBLE FILL - that is the
    convention the rest of this app will be written against, so it is worth fixing now while it
    costs nothing.

    The same four-wall pattern goes around each site; see BOMBER_SITE_WALLS for what each is for.
*/
void ApplicationBomber::BuildWalls(void){
    Material hard;
    hard.name = "bomber_wall_hard";
    //A green that reads as stone rather than as foliage: desaturated, and dark enough that the
    //flame's orange is what carries the frame.
    hard.glsl_material.color = vec4(0.24f,0.52f,0.28f,1.0f);
    hard.glsl_material.metallic = 0.10f;
    hard.glsl_material.roughness = 0.62f;
    renderer->AddMaterial(hard);

    Material soft;
    soft.name = "bomber_wall_soft";
    soft.glsl_material.color = vec4(0.48f,0.31f,0.17f,1.0f);
    soft.glsl_material.metallic = 0.05f;
    soft.glsl_material.roughness = 0.80f;
    renderer->AddMaterial(soft);

    Material bomb;
    bomb.name = "bomber_bomb";
    bomb.glsl_material.color = vec4(0.08f,0.08f,0.09f,1.0f);
    bomb.glsl_material.metallic = 0.35f;
    bomb.glsl_material.roughness = 0.30f;
    renderer->AddMaterial(bomb);

    //The two sites' cells. Set before the walls so ComputeArmLimits has somewhere to start from.
    site_tiles.mode   = BLAST_MODE_TILE;
    site_tiles.cell_x = -4;
    site_tiles.cell_z =  0;
    site_cross.mode   = BLAST_MODE_CROSS;
    site_cross.cell_x =  4;
    site_cross.cell_z =  0;

    const BomberSite* sites[2] = {&site_tiles,&site_cross};
    for (int s = 0; s < 2; s++){
        for (const BomberWallOffset& w:BOMBER_SITE_WALLS){
            AddWall(sites[s]->cell_x + w.dx,sites[s]->cell_z + w.dz,w.kind);
        }
    }

    //Only now that every wall is placed - an arm walked before its neighbour existed would come
    //back too long, and the failure is a flame reaching through a wall, which reads as a shader
    //bug rather than as an ordering one.
    site_tiles.arm_limit = ComputeArmLimits(site_tiles.cell_x,site_tiles.cell_z);
    site_cross.arm_limit = ComputeArmLimits(site_cross.cell_x,site_cross.cell_z);
}

Object* ApplicationBomber::AddWall(int cx, int cz, int kind){
    bool f_hard = (kind == BOMBER_WALL_HARD);
    int material = renderer->FindMaterialIndex(f_hard ? "bomber_wall_hard" : "bomber_wall_soft");

    Object* wall = new Object();
    char name[64];
    snprintf(name,sizeof(name),"%s Wall (%i,%i)",f_hard ? "Hard" : "Soft",cx,cz);
    wall->name = name;
    //Its own mesh per wall rather than a shared one. Wasteful for eight cubes and deliberately so:
    //a destructible block is going to want to shrink, tilt or be replaced on its own, and a shared
    //mesh is the thing that would quietly do it to all of them at once.
    wall->SetMesh(MakeBox(vec3(BOMBER_WALL_SIZE,BOMBER_WALL_SIZE,BOMBER_WALL_SIZE)));
    //MakeBox is centred on the origin, so half its height puts it on the ground rather than
    //through it.
    wall->SetPosition(CellCentre(cx,cz) + vec3(0.0f,BOMBER_WALL_SIZE * 0.5f,0.0f));
    wall->SetMaterialSlot(0,material);
    main_scene->AddObject(wall);
    walls.push_back(wall);
    occupied.insert(BomberCell(cx,cz));
    return wall;
}

/*
    How far the flame gets in each direction, in tiles.

    Walk out one cell at a time and stop at the first wall - the same loop the game itself will
    run, and the reason the blast needs no clipping afterwards: an arm that was never allowed past
    a wall cannot be drawn through one, in either mode.

    BOTH KINDS OF WALL STOP IT for now. Once soft blocks can be destroyed the flame will occupy the
    block's own tile as it burns it, which is `limit = step` instead of `limit = step - 1` for the
    soft case - one line, called out here because it is the difference between a blast that
    destroys what it touches and one that stops politely in front of it.
*/
vec4 ApplicationBomber::ComputeArmLimits(int cx, int cz) const {
    //(east +X, west -X, north -Z, south +Z), matching the order the shader reads arm_limit in.
    static const int DIR_X[4] = { 1,-1, 0, 0};
    static const int DIR_Z[4] = { 0, 0,-1, 1};

    float limits[4] = {0,0,0,0};
    for (int d = 0; d < 4; d++){
        int reach = 0;
        for (int step = 1; step <= BOMBER_BLAST_RANGE; step++){
            if (occupied.count(BomberCell(cx + DIR_X[d] * step,cz + DIR_Z[d] * step))){
                break;
            }
            reach = step;
        }
        limits[d] = (float)reach;
    }
    return vec4(limits[0],limits[1],limits[2],limits[3]);
}

//--- the volumes ----------------------------------------------------------------------------------

/*
    Fills blast_noise by running shaders/noise3d.comp over it once.

    Generated rather than loaded from disk because it is procedural, tiny in code, and the
    parameters are things worth changing while looking at the result - a 96^3 RGBA8 file would be
    3.4 MB of asset to re-export every time. Cheap enough to be unnoticeable at startup: one
    dispatch over 96^3 texels.

    RENDER THREAD - it is a compute dispatch, and Init is on the render thread.
*/
void ApplicationBomber::BuildBlastNoise(void){
    blast_noise_shader = new Shader();
    blast_noise_shader->CreateComputeShader("shaders/noise3d.comp");

    blast_noise = new Texture();
    blast_noise->name = "blast_noise";
    blast_noise->Create3D(BOMBER_NOISE_RESOLUTION,GL_RGBA8);

    blast_noise_shader->Use();
    blast_noise_shader->Setint("cells_base",BOMBER_NOISE_CELLS);
    //layered=GL_TRUE for a 3D image: the shader writes the whole volume, not one slice.
    glBindImageTexture(0,blast_noise->texture_id,0,GL_TRUE,0,GL_WRITE_ONLY,GL_RGBA8);

    //local_size is 8x8x8 in the shader, so one work group per 8 texels per axis.
    int groups = BOMBER_NOISE_RESOLUTION / 8;
    glDispatchCompute(groups,groups,groups);
    //The blast samples this as a texture, not as an image, so wait for the writes to be visible to
    //texture fetches specifically.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    debug->Info("Built %i^3 blast noise, %i base worley cells\n",
                BOMBER_NOISE_RESOLUTION,BOMBER_NOISE_CELLS);
}

Mesh* ApplicationBomber::BuildBlastCube(int shader_index){
    //A unit cube centred on the origin, so the shader's box is -0.5..+0.5 on every axis. MakeBox
    //winds it counter-clockwise seen from outside, which is what lets the uniform callback flip to
    //GL_FRONT and keep exactly the inside faces.
    Mesh* mesh = MakeBox(vec3(1,1,1));
    if (!mesh){
        debug->Fatal("Failed to build a blast cube\n");
        return NULL;
    }
    mesh->num_references++;
    mesh->mesh_mode = MESH_MODE_SHADER;
    //Says WHICH custom shader draws this mesh, and is the whole reason there are two meshes: it is
    //a property of the mesh, so it is what separates the tile volumes from the cross one.
    mesh->custom_shader_index = shader_index;
    return mesh;
}

void ApplicationBomber::BuildExplosion(void){
    /*
        ONE SOURCE FILE, TWO PROGRAMS.

        Shader::uniform_callback is per-Shader and runs once per pass with that program bound, so
        one program could only be told one blast_mode per frame - and both modes are on screen at
        the same time. Compiling the same .frag twice costs one extra program and keeps the shape
        code in one file, which matters more: if the two modes drifted into two files the bench
        would be comparing two shaders rather than two ways of arranging one.
    */
    tile_shader = new Shader();
    //A compile error must not take the app down: the whole point of the reload loop is that a
    //broken shader is something to read the log of and fix, not something to relaunch after. This
    //is the flag core/Shader.h's f_fatal_on_error exists for, and setting it BEFORE the first
    //build is why Shader::Build is a method rather than only a constructor.
    tile_shader->f_fatal_on_error = false;
    bool f_tile_ok = tile_shader->Build("shaders/default.vert","shaders/bomber_explosion.frag");
    tile_shader->uniform_callback = std::bind(&ApplicationBomber::SetTileUniforms,this);
    tile_shader_index = renderer->AddCustomShader(tile_shader);

    cross_shader = new Shader();
    cross_shader->f_fatal_on_error = false;
    bool f_cross_ok = cross_shader->Build("shaders/default.vert","shaders/bomber_explosion.frag");
    cross_shader->uniform_callback = std::bind(&ApplicationBomber::SetCrossUniforms,this);
    cross_shader_index = renderer->AddCustomShader(cross_shader);

    f_shader_ok = f_tile_ok && f_cross_ok;
    reload_log = tile_shader->compile_log;
    if (!f_shader_ok){
        debug->Err("The explosion shader did not compile - the app runs, F5 reloads it:\n%s\n",
                   reload_log.c_str());
    }

    tile_mesh  = BuildBlastCube(tile_shader_index);
    cross_mesh = BuildBlastCube(cross_shader_index);
}

/*
    The volumes of both sites, plus the bomb markers and the per-site light.

    THE TILE SITE GETS EVERY TILE OF ITS MAXIMUM CROSS, including the ones the maze blocks. They
    are created once and never touched again: the shader discards the ones past that direction's
    arm limit, and it discards them from the tile's own world position rather than from anything
    the app has to keep in step. So changing a blast's range or knocking a wall down is a uniform
    changing, not objects being created and destroyed on the physics thread while the render thread
    walks the list.

    All of a site's tiles share ONE mesh, so the nine of them are one draw call - which is also the
    reason they cannot depth-sort against each other, the trade-off this whole comparison is about.
*/
void ApplicationBomber::BuildSites(void){
    int bomb_material = renderer->FindMaterialIndex("bomber_bomb");

    //Offsets of every tile of a full cross, centre first. Built rather than written out so that
    //raising BOMBER_BLAST_RANGE needs nothing here.
    std::vector<BomberCell> tile_offsets;
    tile_offsets.push_back(BomberCell(0,0));
    for (int step = 1; step <= BOMBER_BLAST_RANGE; step++){
        tile_offsets.push_back(BomberCell( step,0));
        tile_offsets.push_back(BomberCell(-step,0));
        tile_offsets.push_back(BomberCell(0,-step));
        tile_offsets.push_back(BomberCell(0, step));
    }

    BomberSite* sites[2] = {&site_tiles,&site_cross};
    const char* names[2] = {"Tiles","Cross"};
    for (int s = 0; s < 2; s++){
        BomberSite& site = *sites[s];
        site.origin = CellCentre(site.cell_x,site.cell_z) + vec3(0.0f,BOMBER_FLAME_HEIGHT,0.0f);

        char name[96];
        if (site.mode == BLAST_MODE_TILE){
            for (size_t i = 0; i < tile_offsets.size(); i++){
                Object* v = new Object();
                snprintf(name,sizeof(name),"%s Tile (%i,%i)",names[s],
                         tile_offsets[i].first,tile_offsets[i].second);
                v->name = name;
                v->SetMesh(tile_mesh);
                v->SetPosition(CellCentre(site.cell_x + tile_offsets[i].first,
                                          site.cell_z + tile_offsets[i].second)
                               + vec3(0.0f,BOMBER_FLAME_HEIGHT,0.0f));
                v->SetScale(vec3(BOMBER_TILE_BOX,BOMBER_TILE_BOX,BOMBER_TILE_BOX));
                //No material: the shader computes its own colour and never touches the material
                //buffer.
                v->SetMaterialSlot(0,-1);
                //Belt and braces. MESH_MODE_SHADER meshes do not go through DeferredPass at all,
                //so the box never reaches the object-id buffer and could not be picked anyway.
                v->SetPickability(false);
                main_scene->AddObject(v);
                site.tiles.push_back(v);
            }
        }else{
            Object* v = new Object();
            snprintf(name,sizeof(name),"%s Volume",names[s]);
            v->name = name;
            v->SetMesh(cross_mesh);
            v->SetPosition(site.origin);
            v->SetScale(vec3(BOMBER_CROSS_BOX,BOMBER_CROSS_BOX,BOMBER_CROSS_BOX));
            v->SetMaterialSlot(0,-1);
            v->SetPickability(false);
            main_scene->AddObject(v);
            site.cross = v;
        }

        //Something to see where the bomb is while nothing is burning, and a preview of the object
        //the game will actually put there.
        site.bomb = new Object();
        snprintf(name,sizeof(name),"%s Bomb",names[s]);
        site.bomb->name = name;
        site.bomb->SetMesh(MakeSphere(0.38f,20,10));
        site.bomb->SetPosition(CellCentre(site.cell_x,site.cell_z) + vec3(0.0f,0.38f,0.0f));
        site.bomb->SetMaterialSlot(0,bomb_material);
        main_scene->AddObject(site.bomb);

        /*
            The flame's own light on the walls around it, parked dark until something detonates.

            ONE PER SITE, not one shared between them, and that is not tidiness: a single light in
            the middle would reach both sites at different distances and light them differently,
            and then a difference on screen would no longer be a difference between the two
            volumes. It is also not decoration - the volumes are emissive, so they light THEMSELVES
            correctly, but nothing in a deferred renderer makes an emissive volume light the
            SURFACES near it. Without this the walls stay sun-lit while a fireball burns next to
            them and the blast reads as a sticker over the scene rather than as something in it.
        */
        site.light = new PointLight();
        snprintf(name,sizeof(name),"%s Blast Light",names[s]);
        site.light->name = name;
        site.light->SetPosition(site.origin);
        site.light->color = vec3(1.00f,0.62f,0.26f);
        site.light->brightness = 0.0f;
        //No shadow: it is inside a volume that is already deciding what the fire can see, and a
        //shadow pass for a light that is dark 95% of the time is a pass paid for 95% too often.
        site.light->f_casts_shadow = false;
        main_scene->AddObject(site.light);
    }
}

//The two Shader::uniform_callbacks. Each is the shared push with its own mode's box size and the
//per-mode constants that are not knobs - see BOMBER_TILE_SHELL and friends for why those two are
//constants rather than sliders.
void ApplicationBomber::SetTileUniforms(void){
    PushBlastUniforms(tile_shader,site_tiles,BLAST_MODE_TILE,BOMBER_TILE_BOX);
}

void ApplicationBomber::SetCrossUniforms(void){
    PushBlastUniforms(cross_shader,site_cross,BLAST_MODE_CROSS,BOMBER_CROSS_BOX);
}

void ApplicationBomber::PushBlastUniforms(Shader* shader, const BomberSite& site,
                                          int mode, float box_world){
    if (!shader){
        return;
    }
    //World units -> this volume's object units. One number because the boxes are uniform cubes;
    //see the note on BOMBER_TILE_BOX for why they have to be.
    float to_object = 1.0f / box_world;

    shader->Setint("blast_mode",mode);

    //The clock, from the physics thread's published view state. See the note on blast_age_view.
    shader->Setfloat("blast_age",blast_age_view);
    shader->Setfloat("blast_seed",blast_seed_view);

    //The maze, as the shader needs it.
    shader->Setvec4("arm_limit",site.arm_limit);
    shader->Setvec3("blast_origin",site.origin);
    shader->Setfloat("cell_world",BOMBER_CELL_SIZE);
    shader->Setfloat("cell_object",BOMBER_CELL_SIZE * to_object);

    //Per-mode, and deliberately not knobs.
    shader->Setfloat("shell_thickness",
                     (mode == BLAST_MODE_CROSS) ? BOMBER_CROSS_SHELL : BOMBER_TILE_SHELL);
    shader->Setfloat("core_heat",
                     (mode == BLAST_MODE_CROSS) ? BOMBER_CROSS_CORE_HEAT : BOMBER_TILE_CORE_HEAT);

    {
        std::lock_guard<std::mutex> lock(knob_mutex);
        shader->Setfloat("blast_life",blast_life);
        shader->Setfloat("tile_delay",tile_delay);
        //The two length knobs, converted. Everything below them is already dimensionless.
        shader->Setfloat("blast_radius",blast_radius * to_object);
        shader->Setfloat("rise",rise * to_object);

        shader->Setfloat("rim_softness",rim_softness);
        shader->Setfloat("turbulence",turbulence);
        shader->Setfloat("noise_scale",noise_scale);
        shader->Setfloat("outflow",outflow);
        shader->Setfloat("blast_density",blast_density);
        shader->Setfloat("heat",heat);
        shader->Setfloat("emission_strength",emission_strength);
        shader->Setfloat("smoke_albedo",smoke_albedo);
        shader->Setfloat("sun_intensity",sun_intensity);
        shader->Setfloat("light_absorption",light_absorption);
        /*
            The cross box is three times the tile box on a side, so the same step COUNT is three
            times the step LENGTH, and the arms go visibly stripy. Paying for that here rather than
            with a second knob keeps the two modes on one setting: move the slider and both get
            proportionally finer, which is what makes a quality comparison between them fair.
        */
        int steps = (mode == BLAST_MODE_CROSS) ? num_view_steps * BOMBER_CROSS_VIEW_STEPS_MUL
                                               : num_view_steps;
        shader->Setint("num_view_steps",steps);
        shader->Setint("num_light_steps",num_light_steps);
        shader->Setfloat("light_falloff",light_falloff);
        shader->Setfloat("max_radiance",max_radiance);
        shader->Setint("f_show_box",debug_view);
    }

    if (blast_noise){
        glBindTextureUnit(TEXUNIT_APP_RESERVED,blast_noise->texture_id);
    }

    /*
        Render the box's INSIDE faces only, write no depth, and do not depth TEST either. Copied
        knowingly from ApplicationShip::SetVolumeUniforms, which has the long version of why; the
        short version is three separate reasons that happen to want the same three calls:

          - BACK faces rather than front is what makes the volume survive the camera being inside
            the box, and it guarantees exactly one fragment per pixel so the volume is blended once
            instead of twice. The shader intersects the box analytically and does not care which of
            the two it was handed.
          - NO DEPTH WRITE because a volume must not occlude anything drawn after it.
          - NO DEPTH TEST because rasterising back faces puts this fragment's depth at the FAR side
            of the box, so a wall standing INSIDE the volume is nearer and the fixed-function test
            would throw the fragment away - on exactly the pixels that needed fire in front of the
            wall. With the test off the shader owns depth entirely: it clamps the march to the
            G-buffer's world position and discards when the scene is in front of the box.

        Renderer::CustomShaderPass restores GL_BACK / depth writes / the depth test right after
        this pass, so none of it leaks into the next frame's depth passes.
    */
    glCullFace(GL_FRONT);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

/*
    Recompiles shaders/bomber_explosion.frag into BOTH programs, without a rebuild or a relaunch.
    RENDER THREAD ONLY - it is GL work, which is why the F5 key only raises a flag.

    Shader::Reload does the work, including putting each new program back in
    renderer->custom_shaders at the index it already holds and re-releasing every source file
    (#included ones too). A reload is always soft whatever f_fatal_on_error says: on failure the
    old program keeps drawing and compile_log says why, which is the only useful behaviour when the
    whole point is to be editing the file.

    Both are reloaded even if the first fails, so one broken mode cannot leave the other stale -
    they are the same source, and a half-reloaded pair would be a genuinely confusing thing to be
    looking at.
*/
void ApplicationBomber::ReloadExplosionShader(void){
    bool f_ok = true;
    std::string log;
    if (tile_shader){
        f_ok = tile_shader->Reload() && f_ok;
        log = tile_shader->compile_log;
    }
    if (cross_shader){
        f_ok = cross_shader->Reload() && f_ok;
    }
    {
        std::lock_guard<std::mutex> lock(reload_mutex);
        f_shader_ok = f_ok;
        reload_log = log;
    }
    if (f_ok){
        debug->Ok("Reloaded shaders/bomber_explosion.frag into both programs\n");
    }else{
        debug->Err("shaders/bomber_explosion.frag did not compile; the previous one is still "
                   "drawing:\n%s\n",log.c_str());
    }
}

//--- the blast ----------------------------------------------------------------------------------

void ApplicationBomber::Detonate(void){
    blast_start_tick = main_scene->GetPhysicsTick();
    f_blast_live = true;
    blast_count++;
    //Both sites, off the one clock. That is what makes them comparable frame by frame: at any tick
    //the two are exactly the same number of ticks into the same blast.
    debug->Info("Detonation %u at tick %llu\n",blast_count,(unsigned long long)blast_start_tick);
}

/*
    One tick of the blast. Physics thread, inside the tick, so it pauses and single-steps with
    everything else.

    Two things happen here and they are deliberately both on this side of the thread boundary: the
    lights' brightness, because it is a property of objects the renderer reads, and the published
    age, because the shaders must see a value that came from a whole number of ticks rather than
    from wherever the render thread happened to look.
*/
void ApplicationBomber::UpdateBlast(void){
    float age = -1.0f;
    if (f_blast_live){
        age = (float)(main_scene->GetPhysicsTick() - blast_start_tick);
        /*
            The TILE site outlives the cross one: its outermost ring does not ignite until
            range * tile_delay ticks in, and then has a full life of its own to burn. So the clock
            has to run until the LAST tile is done or the far tiles would be cut off mid-flame.
            The cross site simply draws nothing past its own life, which it already checks.
        */
        float last_tile_out = blast_life + tile_delay * (float)BOMBER_BLAST_RANGE;
        if (age > last_tile_out){
            //Burnt out. The clock goes negative rather than the objects being hidden - see the
            //note on blast_age_view.
            f_blast_live = false;
            age = -1.0f;
        }
    }

    /*
        Each site's light follows its own fire.

        A flat brightness for the blast's whole life lights the walls as brightly while the last
        smoke drifts as it does at the detonation, which is exactly backwards - the flash is the
        moment the room should go orange. So: a very fast rise over the first few ticks and a
        fourth-power decay, which is steeper than the fire's own cooling curve because a point
        light has no smoke to hide behind.

        It also RISES with the flame, using the same `rise` the shader applies, so the highlight on
        the wall tops tracks where the fire actually is.
    */
    BomberSite* sites[2] = {&site_tiles,&site_cross};
    for (int s = 0; s < 2; s++){
        BomberSite& site = *sites[s];
        if (!site.light){
            continue;
        }
        if (age >= 0.0f){
            float t = clamp(age / max(blast_life,1.0f),0.0f,1.0f);
            float ignite = clamp(age / 3.0f,0.0f,1.0f);
            float decay = powf(1.0f - t,4.0f);
            site.light->brightness = blast_light_brightness * ignite * decay;
            site.light->radius = blast_light_radius;
            site.light->SetPosition(site.origin + vec3(0.0f,rise * t * t,0.0f));
        }else{
            site.light->brightness = 0.0f;
        }
    }

    //Published last, so the render thread never sees an age that is ahead of the lights.
    blast_age_view = age;
    //Derived from the count rather than drawn from rrand: a detonation has to look different from
    //the last one, and has to differ THE SAME WAY on a replay. An arithmetic seed does both
    //without touching the shared random stream, which has an open backlog item about exactly that.
    //The multiplier is irrational-ish so consecutive blasts land far apart in the noise.
    blast_seed_view = (float)blast_count * 0.6180339887f;
}

//--- input, commands and the loop ----------------------------------------------------------------

void ApplicationBomber::SetupInput(void){
    InputController* input = main_scene->inputcontroller;

    //Two mappings for the trigger, because muscle memory differs and both cost nothing:
    //KeyState::f_isdown counts HELD MAPPINGS rather than being a boolean, so an action stays down
    //while either of its keys is.
    input->AddKeyMap(VK_SPACE,INPUT_BOMBER_DETONATE);
    input->AddKeyMap('B',INPUT_BOMBER_DETONATE);
    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_BOMBER_DETONATE);

    input->AddKeyMap(VK_F5,INPUT_BOMBER_RELOAD_SHADER);

    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature - and pausing
    //mid-fireball is the single most useful thing you can do to a volumetric effect.
    input->AddKeyMap('P',INPUT_PAUSE);
}

void ApplicationBomber::RegisterCommandHandlers(void){
    main_scene->RegisterCommandHandler(BOMBER_CMD_DETONATE,
        [this](const SimCommand& cmd) -> objectid_t {
            (void)cmd;
            Detonate();
            return OBJECTID_INVALID;
        });
}

/*
    Physics thread, every pass - including the ones that simulate nothing. Camera and the reload
    key only: neither is simulation, and nothing here may write something a tick will read.

    THE CAMERA IS ApplicationShip's, not the shorter one apps/testfx uses, and the differences are
    all things that were got wrong here first:

      - It reads INPUT_MOUSE_DELTA_X/Y, the raw unaccelerated movement, rather than the cursor
        position delta. Raw keeps reporting once the pointer is against the edge of the screen and
        is not bent by the pointer acceleration curve, which is what mouse-look wants.

      - It reads them EVERY pass, outside the button gate. This is the one that bites:
        InputController only clears the delta of a map that was actually read this pass, so a read
        behind the button gate lets movement pile up for the whole time the button is NOT held, and
        the first frame of a drag applies all of it at once. The camera jumps, and it jumps further
        the longer you waited before dragging.

      - It is gated on the window having focus, and it gives up the mouse to the debug UI, so
        dragging a slider in the Explosion panel does not also swing the camera.

      - The wheel is accumulated and bled off rather than applied as it arrives, which is what
        makes the zoom coast to a stop instead of stepping.
*/
void ApplicationBomber::UpdateView(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;
    Camera* camera = main_scene->camera;
    if (!camera){
        return;
    }

    if (input->WasKeyReleased(INPUT_BOMBER_RELOAD_SHADER)){
        //NOT a GL call: it only raises a flag that PreRender acts on. UpdateView runs on the
        //physics thread, which may not touch the context at all.
        f_shader_reload_requested = true;
    }

    //Drained every pass whether or not the drag is active - see the block above for why.
    int cam_dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int cam_dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);

    //Middle mouse orbits around camera_target, shift+middle pans both camera and pivot. Same
    //scheme and same sensitivities as ApplicationShip and ApplicationTank.
    if (main_window->f_has_focus && !UIWantsMouse() && input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        if (input->IsKeyDown(INPUT_SHIFT)){
            //Move the camera, carrying the pivot with it so the viewing angle is left alone.
            vec3 d = camera->MoveSidewaysBy(-cam_dx / 100.0f);
            d += camera->MoveUpBy(cam_dy / 100.0f);
            camera_target += d;
        }else{
            //Up/down rotates the camera position around the camera's own left axis.
            vec3 p = camera->GetPosition() - camera_target;
            vec3 axis = camera->GetLeft();
            quat q(axis,-cam_dy / 50.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);

            //Re-aim at the pivot keeping the current up, which allows a full 360 over the top.
            vec3 up = camera->GetUp();
            camera->SetLookAt(camera_target,&up);

            //Left/right rotates around the world Y axis, lookat included.
            p = camera->GetPosition() - camera_target;
            axis = vec3(0,1,0);
            q.set_rotation(axis,-cam_dx / 50.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);
            camera->RotateBy(q);
        }
    }

    /*
        Wheel zoom, focused only - otherwise it tracks a wheel being used in another application.
        InputController drops the delta while unfocused as well, so this is belt and braces.

        Dollies along the view direction by a fraction of the distance to the pivot, so the step
        shrinks as it closes in, and bleeds the accumulator off by /1.1 a pass so a flick of the
        wheel coasts. Clamped at both ends, and the clamp is not defensive tidiness: a step
        proportional to the distance is geometric in BOTH directions, so scrolling out compounds
        and a few seconds of it put the camera far enough away that the blast is a speck on a black
        screen - which looks exactly like a shader that stopped drawing.
    */
    if (main_window->f_has_focus){
        if (mouse_wheel_sum != 0.0f){
            vec3 diff = camera->GetPosition() - camera_target;
            float distance = diff.length();
            float step = distance * mouse_wheel_sum / 50.0f;
            float target = clamp(distance - step,2.0f,60.0f);
            camera->MoveForwardBy(distance - target);
            mouse_wheel_sum /= 1.1f;
            if (fabsf(mouse_wheel_sum) < 0.01f){
                mouse_wheel_sum = 0.0f;
            }
        }
        if (!UIWantsMouse()){
            mouse_wheel_sum += (float)input->GetDelta(INPUT_MOUSE_WHEEL);
        }else{
            //Read and throw away, so a scroll over a panel does not pile up and then arrive all at
            //once the moment the pointer leaves it.
            input->GetDelta(INPUT_MOUSE_WHEEL);
        }
    }
}

/*
    Physics thread, once per tick that actually runs, physics_mutex held.

    The detonation key is read HERE rather than in UpdateView because it is simulation: it changes
    what the world does, so it has to be paused, stepped and replayed with everything else. That is
    also why it is WasKeyPressed and not IsKeyDown - a detonation is an edge, and an edge read on a
    pass that does not tick would be cleared before any gameplay saw it (backlog item 84; the long
    version is on InputController::ApplyTickInput).
*/
void ApplicationBomber::RunSimulationTick(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    if (main_scene->inputcontroller->WasKeyPressed(INPUT_BOMBER_DETONATE)){
        Detonate();
    }
    UpdateBlast();
}

//Frame thread, top of every frame, before anything is drawn.
void ApplicationBomber::PreRender(void){
    //A shader reload is GL work, so the key and the panel button only raise a flag and it is
    //serviced here. Cheap: one atomic read on a frame where nothing was asked for.
    if (f_shader_reload_requested){
        f_shader_reload_requested = false;
        ReloadExplosionShader();
    }
}

//--- the knobs ------------------------------------------------------------------------------------

/*
    One table, driving the panel's sliders, the bomber_set tool and the bomber_state readout.

    Built here rather than as a static initialiser because every entry points at a member of THIS
    object. Ranges are what is worth exploring, not what is legal - the shader clamps what it has
    to, and a slider whose useful third is two pixels wide is a slider nobody tunes with.

    Lengths are in WORLD UNITS. PushBlastUniforms converts them per volume, which is what lets one
    slider mean the same thing to a 4-unit tile box and a 12-unit cross box - and without that the
    two sites could not be compared at all.
*/
void ApplicationBomber::BuildKnobTable(void){
    knobs.clear();
    knobs.push_back({"blast_life",&blast_life,NULL,20.0f,400.0f,
        "how long one blast lasts, in simulation ticks"});
    knobs.push_back({"blast_radius",&blast_radius,NULL,0.2f,3.0f,
        "radius of the flame tube, in WORLD units - a cell is 2.0"});
    knobs.push_back({"tile_delay",&tile_delay,NULL,0.0f,20.0f,
        "ticks each ring of tiles waits behind the one nearer the bomb (per-tile site only)"});
    knobs.push_back({"rim_softness",&rim_softness,NULL,0.01f,1.0f,
        "width of the fade at the front's edge, as a fraction of the radius"});
    knobs.push_back({"turbulence",&turbulence,NULL,0.0f,2.0f,
        "how far the noise pushes the front in and out - 0 is a smooth sphere"});
    knobs.push_back({"noise_scale",&noise_scale,NULL,0.25f,6.0f,
        "how many times the noise tiles across the flame's diameter - the billow size"});
    knobs.push_back({"outflow",&outflow,NULL,0.0f,1.5f,
        "how far the billows are dragged outward over a life"});
    knobs.push_back({"rise",&rise,NULL,0.0f,3.0f,
        "how far the flame floats up over a life, in WORLD units"});
    knobs.push_back({"blast_density",&blast_density,NULL,0.5f,60.0f,
        "density per world unit at the heart of the front"});
    knobs.push_back({"heat",&heat,NULL,0.1f,3.0f,
        "scales the temperature field before the colour ramp"});
    knobs.push_back({"emission_strength",&emission_strength,NULL,0.0f,25.0f,
        "radiance the hottest gas emits"});
    knobs.push_back({"smoke_albedo",&smoke_albedo,NULL,0.0f,2.0f,
        "how much of the scene's light cold smoke bounces back"});
    knobs.push_back({"sun_intensity",&sun_intensity,NULL,0.0f,1.0f,
        "what one unit of the sun's brightness is worth to the smoke - see the .frag"});
    knobs.push_back({"light_absorption",&light_absorption,NULL,0.0f,4.0f,
        "how fast light is extinguished through the medium"});
    knobs.push_back({"num_view_steps",NULL,&num_view_steps,4.0f,128.0f,
        "steps along the view ray - the app's main cost. The cross site gets double"});
    knobs.push_back({"num_light_steps",NULL,&num_light_steps,0.0f,16.0f,
        "steps towards each light per view step, for the smoke only. 0 disables scattering"});
    knobs.push_back({"light_falloff",&light_falloff,NULL,0.5f,3.0f,
        "attenuation exponent: brightness/pow(distance,this)"});
    knobs.push_back({"max_radiance",&max_radiance,NULL,0.5f,40.0f,
        "ceiling on in-scattered radiance at one sample"});
    knobs.push_back({"blast_light_brightness",&blast_light_brightness,NULL,0.0f,200.0f,
        "peak brightness of the point light each site throws on its walls"});
    knobs.push_back({"blast_light_radius",&blast_light_radius,NULL,0.05f,5.0f,
        "source size of that light, for the penumbra estimate"});
    knobs.push_back({"debug_view",NULL,&debug_view,0.0f,2.0f,
        "0 off, 1 the marched interval, 2 the G-buffer the shader is handed"});
}

BomberKnob* ApplicationBomber::FindKnob(const std::string& name){
    for (BomberKnob& knob:knobs){
        if (name == knob.name){
            return &knob;
        }
    }
    return NULL;
}

json ApplicationBomber::BlastStateJson(void){
    json result;
    //Read without the lock: these are the two published floats and the point of them is that a
    //reader never blocks the simulation for them. See the note in the header.
    float age = blast_age_view;
    result["blast_age"] = age;
    result["blast_live"] = age >= 0.0f;
    result["blast_count"] = blast_count;
    //The normalised age is what every curve in the shader is actually a function of, so it is
    //worth reporting alongside the raw tick count rather than leaving it to be recomputed.
    result["blast_progress"] = (age >= 0.0f) ? clamp(age / max(blast_life,1.0f),0.0f,1.0f) : -1.0f;
    result["tick"] = main_scene ? main_scene->GetPhysicsTick() : 0;

    //Both sites, so a caller can see that they really do have the same arms.
    const BomberSite* sites[2] = {&site_tiles,&site_cross};
    const char* names[2] = {"tiles","cross"};
    json sites_json = json::array();
    for (int s = 0; s < 2; s++){
        sites_json.push_back(json{
            {"name",names[s]},
            {"mode",(sites[s]->mode == BLAST_MODE_CROSS) ? "one volume shaped like the cross"
                                                         : "one volume per tile"},
            {"cell",json::array({sites[s]->cell_x,sites[s]->cell_z})},
            {"origin",json::array({sites[s]->origin.x,sites[s]->origin.y,sites[s]->origin.z})},
            {"arm_limit_tiles",json{
                {"east",sites[s]->arm_limit.x},
                {"west",sites[s]->arm_limit.y},
                {"north",sites[s]->arm_limit.z},
                {"south",sites[s]->arm_limit.w}}},
            {"volumes",(sites[s]->mode == BLAST_MODE_CROSS) ? 1 : (int)sites[s]->tiles.size()}
        });
    }
    result["sites"] = sites_json;

    json values = json::object();
    {
        std::lock_guard<std::mutex> lock(knob_mutex);
        for (const BomberKnob& knob:knobs){
            values[knob.name] = knob.fvalue ? json(*knob.fvalue) : json(*knob.ivalue);
        }
    }
    result["knobs"] = values;

    {
        std::lock_guard<std::mutex> lock(reload_mutex);
        result["shader_ok"] = f_shader_ok;
        result["compile_log"] = reload_log;
    }
    return result;
}

//--- MCP -----------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationBomber::RegisterMCPTools(void){
    MCPServer::Get()->RegisterTool("bomber_detonate",
        "Set off BOTH blast sites at once, restarting the clock from tick 0 - the same thing the "
        "space bar does. The left site (-8 on X) draws the cross as one volume per tile, the right "
        "one (+8 on X) as a single volume shaped like the cross; they share one clock and one set "
        "of tunables so that what differs on screen is only the thing being compared. Returns the "
        "blast state; pass include_screenshot to see it. NOTE that the flame is at its brightest "
        "within a few ticks and is smoke by the end of its life, so a screenshot taken right after "
        "this shows the flash and nothing else. To look at a particular frame: sim_pause, "
        "bomber_detonate, then sim_step the number of ticks you want - the whole effect is driven "
        "by the tick counter, so frame N is reproducible.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after detonating"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = BOMBER_CMD_DETONATE;
            //SubmitCommandAndWait, not SubmitUICommand: an MCP handler holds no lock, so it may
            //wait - and it has to, or it would report the state of the blast it just replaced.
            SubmitCommandAndWait(cmd);
            return MaybeAttachScreenshot(BlastStateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_state",
        "The blast clock, both sites, and every tunable of the explosion effect. `blast_age` is in "
        "SIMULATION TICKS since detonation and is -1 when nothing is burning; `blast_progress` is "
        "that normalised to 0..1 over the blast's life, which is what every curve in the shader is "
        "a function of. `sites` reports each site's arm lengths in tiles, walked out from the bomb "
        "and stopped at the first wall - both sites have the same walls around them, so the two "
        "should always agree. Also reports whether the shader compiled and what the compiler said.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the current frame"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BlastStateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_set",
        "Set one tunable of the explosion by name - the names and current values are the `knobs` "
        "object bomber_state returns. Both sites share every one of them, which is the point: a "
        "difference on screen has to be a difference between the two ways of drawing the blast and "
        "not between two sets of settings. Lengths are in WORLD units (a grid cell is 2.0) and are "
        "converted per volume, so one value means the same thing to both boxes. The value is held "
        "on the C++ side and pushed to the programs on the render thread every frame, so it "
        "survives a shader reload and never writes to a program mid-draw. Setting `debug_view` to "
        "1 reads out the marched interval and 2 reads out the G-buffer the shader is handed, which "
        "is how to tell a black screen caused by the shape from one caused by the box.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","tunable to set, as listed by bomber_state"}}},
                {"value", {{"type","number"},{"description","the new value; clamped to the tunable's range"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the change"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }},
            {"required",json::array({"name","value"})}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            float value = args.value("value",0.0f);
            {
                std::lock_guard<std::mutex> lock(knob_mutex);
                BomberKnob* knob = FindKnob(name);
                if (!knob){
                    return json{ {"error","no tunable called \"" + name + "\" - see bomber_state"} };
                }
                //Clamped rather than refused. Every one of these is a number whose useful range is
                //a matter of taste, and a tool that rejects 0.6 because the slider stops at 0.5 is
                //a tool that makes you go and read the table.
                float clamped = clamp(value,knob->min_value,knob->max_value);
                if (knob->fvalue){
                    *knob->fvalue = clamped;
                }else{
                    *knob->ivalue = (int)(clamped + 0.5f);
                }
            }
            return MaybeAttachScreenshot(BlastStateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_reload_shader",
        "Recompile shaders/bomber_explosion.frag from disk and swap it into BOTH programs, without "
        "restarting the app - the edit-and-look loop. One source file serves both blast modes, so "
        "a reload always does the pair; a shader that fails to compile leaves the last working one "
        "drawing and reports the GLSL error rather than taking the app down. The tunables keep "
        "their values, since they live on the C++ side. This is bomber's own reload; the core "
        "shader_reload tool does the same for any shader in the app by file-name filter.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the reload"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            //Compiling is GL work, so this raises the same flag the F5 key does and waits for
            //PreRender to service it - otherwise the reply would say a reload was ASKED FOR rather
            //than what the compiler said about it, and the compiler's answer is the whole value of
            //the call.
            f_shader_reload_requested = true;
            for (int i = 0;i < 200 && f_shader_reload_requested;i++){
                Sleep(10);
            }
            if (f_shader_reload_requested){
                return json{ {"error","the render thread did not compile it in time"} };
            }
            return MaybeAttachScreenshot(BlastStateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });
}
#endif //USE_MCP

//--- UI -------------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI unconditionally;
//with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationBomber::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderExplosionPanel();
}

void ApplicationBomber::RenderExplosionPanel(void){
    ImGui::Begin("Explosion");

    //Which is which. Two sentences that save walking the scene tree, and the left/right is the
    //only thing about this screen that is not self-evident.
    ImGui::TextDisabled("LEFT  site: one volume per tile (%i boxes, 1 draw call)",
                        (int)site_tiles.tiles.size());
    ImGui::TextDisabled("RIGHT site: one volume shaped like the cross");
    ImGui::TextDisabled("arms E%.0f W%.0f N%.0f S%.0f tiles, walked to the first wall",
                        site_tiles.arm_limit.x,site_tiles.arm_limit.y,
                        site_tiles.arm_limit.z,site_tiles.arm_limit.w);
    ImGui::Separator();

    float age = blast_age_view;
    if (age >= 0.0f){
        ImGui::Text("Burning - tick %.0f of %.0f",age,blast_life);
        ImGui::ProgressBar(clamp(age / max(blast_life,1.0f),0.0f,1.0f),ImVec2(-1,0));
    }else{
        ImGui::TextDisabled("Idle");
        //A bar either way, so the panel does not change height when something goes off and the
        //controls under it do not jump out from under the pointer mid-drag.
        ImGui::ProgressBar(0.0f,ImVec2(-1,0),"---");
    }
    ImGui::Text("%u detonations",blast_count);

    if (ImGui::Button("Detonate (Space)")){
        //SubmitUICommand, never SubmitCommandAndWait: this runs on the render thread with
        //physics_mutex held, and waiting here for the physics thread - which needs that same mutex
        //to drain the queue - would deadlock instantly. See Application::SubmitCommandAndWait.
        SimCommand cmd;
        cmd.type = BOMBER_CMD_DETONATE;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    if (ImGui::Button(main_scene->IsPhysicsPaused() ? "Resume" : "Pause")){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload shader (F5)")){
        //A flag, not a call: this is the render thread, but PreRender is where a reload belongs so
        //there is one path rather than two. See ReloadExplosionShader.
        f_shader_reload_requested = true;
    }

    {
        std::lock_guard<std::mutex> lock(reload_mutex);
        if (!f_shader_ok){
            ImGui::TextColored(ImVec4(1.0f,0.4f,0.3f,1.0f),"Shader did not compile");
            ImGui::TextWrapped("%s",reload_log.c_str());
        }
    }

    ImGui::Separator();

    //One loop over the table that bomber_set also reads, so a new knob is one line in
    //BuildKnobTable and it appears here and over MCP at the same moment.
    std::lock_guard<std::mutex> lock(knob_mutex);
    for (BomberKnob& knob:knobs){
        if (knob.fvalue){
            ImGui::SliderFloat(knob.name,knob.fvalue,knob.min_value,knob.max_value);
        }else{
            ImGui::SliderInt(knob.name,knob.ivalue,(int)knob.min_value,(int)knob.max_value);
        }
        if (ImGui::IsItemHovered() && knob.help){
            ImGui::SetTooltip("%s",knob.help);
        }
    }

    ImGui::End();
}
#endif //USE_IMGUI
