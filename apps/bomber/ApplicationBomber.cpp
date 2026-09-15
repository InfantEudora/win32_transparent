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
    Every node this app takes out of bomber_assets.glb, by its name in the file.

    THE ORDER OF MazeTile AND MazeDecor IS MIRRORED HERE, which is what lets the field builder
    index straight into these with the tile byte rather than running a switch. A tile type without
    a mesh would be a blank cell, so the arrays are sized by the enum's COUNT and the compiler
    complains if one gets out of step.

    A MAZE_TILE_WALL cell gets a grass tile AND a wall on top of it: wall_brick is only 0.75 deep
    against a 1.0 cell, so without a floor under it you would see through the gap to the void.
*/
static const char* BOMBER_TILE_ASSET[MAZE_TILE_COUNT] = {
    "tile_grass",   //MAZE_TILE_GRASS
    "tile_brick",   //MAZE_TILE_BRICK
    "tile_rock",    //MAZE_TILE_ROCK
    "tile_water",   //MAZE_TILE_WATER
    "tile_grass",   //MAZE_TILE_WALL - the floor under the wall; the wall itself is added on top
};
static const char* BOMBER_DECOR_ASSET[MAZE_DECOR_COUNT] = {
    NULL,               //MAZE_DECOR_NONE
    "grass_flowers",
    "grass_plant",
    "turd",
    "bridge",
};
/*
    Which way the bridge model's planks run at each yaw.

    The asset is very nearly square in plan (0.89 x 0.94), so which of the two is "along the planks"
    cannot be read off its bounding box - it has to be looked at once and then written down. These
    two are that, and flipping them is the whole fix if a re-export turns the model.
*/
#define BOMBER_BRIDGE_YAW_X     (TYPE_PI * 0.5f)    //planks run east-west
#define BOMBER_BRIDGE_YAW_Z     0.0f                //planks run north-south

#define BOMBER_WALL_ASSET   "wall_brick"
#define BOMBER_BOMB_ASSET   "bomb"
#define BOMBER_CHAR_ASSET   "character"

ApplicationBomber::ApplicationBomber():Application(){
    app_name = "Bomber";
    debug->Info("Created new ApplicationBomber.\n");

    //All three engine windows up. The Bomber panel is what this app is driven from, but the Scene
    //tree and the Inspector earn their place here too: there are a few hundred objects in the
    //field and being able to pick one and read its transform is how you check a tile is where you
    //think it is.
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
        PIPELINE_DEFERRED is not optional here: the deferred pass is what fills the G-buffer
        CustomShaderPass binds on units 1-3, and the blast clamps its march to the solid scene
        using exactly that. Without it a wall standing in the middle of the flame would be buried
        under the full depth of fire instead of the right fraction of it.
    */
    renderer->alpha_clip = 0.5f;
    //No skybox: the frame clears to black, which is the right backdrop for judging an emissive
    //effect - anything else makes the fire's colour a matter of opinion.
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1280,800);

    /*
        60 ticks per second, not the default 50.

        The blast clock is counted in ticks, so the tick rate IS the frame rate of the explosion. A
        fireball's first fifth of a second is where all its expansion happens, and at 50 Hz that is
        ten frames to say it in. It also makes MAZE_STEP_TICKS a round third of a second.
    */
    SetPhysicsTPS(60.0f);

    main_scene = CreateNewScene("Bomber Scene");
    assetmanager = new AssetManager();

    /*
        Framed on the whole 16x16 board from 45 degrees up.

        That angle is not a matter of taste: a bomberman blast is a PLUS drawn on the floor, and
        from a low three-quarter view the two arms running away from the camera foreshorten into
        the middle and the whole thing reads as a blob. Halfway up is where the cross is legible
        and the flame still has a visible silhouette. The orbit is there for going in close.
    */
    camera_target = vec3(0.0f,0.0f,0.0f);
    main_scene->camera->SetPosition(vec3(0.0f,13.0f,11.0f));
    main_scene->camera->SetLookAt(camera_target);

    /*
        Draw from the shared stream ONCE, here, before anything else runs. Application::rrand is a
        single shared stream and there is an open backlog item about off-tick draws shifting it out
        from under the simulation; Init is the one place that cannot do that, because it runs
        before the physics thread exists. NOTE that the maze does NOT use it - Maze carries its own
        seeded generator for exactly this reason. See core/RRandom.h and Maze.h.
    */
    rrand = new RRandom(1);

    BuildLighting();
    LoadAssets();
    BuildBlastNoise();
    BuildExplosion();
    BuildKnobTable();

    //The field itself. Init is the render thread and nothing is running yet, so this is the one
    //RebuildField that needs no command behind it.
    maze.NewGame(current_seed);
    RebuildField();

    SetupInput();
    RegisterCommandHandlers();

#ifdef USE_MCP
    //Guarded because this app's own tools call into MCPServer, which USE_MCP=0 does not compile.
    //The CORE tools are switched a different way - see engine.mk.
    RegisterMCPTools();
#endif

    debug->Ok("Bomber ready - %ix%i field, seed %u, %i objects\n",
              MAZE_W,MAZE_H,current_seed,(int)field_objects.size());
}

//--- the scene ------------------------------------------------------------------------------------

vec3 ApplicationBomber::CellCentre(int cx, int cz) const {
    //The board is centred on the origin rather than running from a corner, so the orbit camera has
    //something symmetric to turn around and the default framing needs no offset. Maze itself knows
    //nothing about this - it counts from (0,0) - which is the whole point of keeping world units
    //out of that header.
    return vec3(((float)cx - (MAZE_W - 1) * 0.5f) * BOMBER_CELL_SIZE,
                0.0f,
                ((float)cz - (MAZE_H - 1) * 0.5f) * BOMBER_CELL_SIZE);
}

void ApplicationBomber::BuildLighting(void){
    /*
        A key light and a fill, which is two lights rather than one for a reason worth stating:
        default.frag's ambient term is a hardcoded 0.1 * albedo and there is no app-side lever on
        it, so a single sun leaves every surface facing away from it at a tenth brightness. On a
        board made of cubes that is half of what you are looking at, and the field came out reading
        as night-time. The fill is the repo's usual answer - apps/breakout and apps/tetris both
        carry one - and it costs nothing, because it casts no shadow.
    */
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-9,14,7));
    sun->color = vec3(1.0f,0.96f,0.90f);
    sun->brightness = 6.0f;
    //Half-extent in world units for the shadow ortho. The board is 16 across, so 12 covers it with
    //room for the walls' shadows to fall off the edge rather than be clipped mid-shadow.
    sun->viewport.zoom = 12.0f;
    sun->SetLookAt(vec3());
    main_scene->AddObject(sun);

    //From the opposite side and cool, so the faces the sun misses read as sky-lit rather than as
    //unlit. Dim enough that the sun still says where the light is coming from, and NO SHADOW: a
    //second shadow pass over four hundred objects to fake an ambient term would be a poor trade.
    DirectionalLight* fill = new DirectionalLight();
    fill->name = "Directional Light (Fill)";
    fill->SetPosition(vec3(8,10,-9));
    fill->color = vec3(0.72f,0.80f,1.00f);
    fill->brightness = 2.2f;
    fill->f_casts_shadow = false;
    fill->viewport.zoom = 12.0f;
    fill->SetLookAt(vec3());
    main_scene->AddObject(fill);

    /*
        The flame's own light on the field around it, parked dark until something goes off.

        Not decoration: the volumes are emissive, so they light THEMSELVES correctly, but nothing
        in a deferred renderer makes an emissive volume light the SURFACES near it. Without this
        the walls stay sun-lit while a fireball burns next to them and the blast reads as a sticker
        over the scene rather than as something in it.
    */
    blast_light = new PointLight();
    blast_light->name = "Blast Light";
    blast_light->SetPosition(vec3(0.0f,BOMBER_FLAME_HEIGHT,0.0f));
    blast_light->color = vec3(1.00f,0.62f,0.26f);
    blast_light->brightness = 0.0f;
    //No shadow: it is inside a volume that is already deciding what the fire can see, and a shadow
    //pass for a light that is dark most of the time is a pass paid for far too often.
    blast_light->f_casts_shadow = false;
    main_scene->AddObject(blast_light);
}

/*
    Pulls every node this app needs out of the one GLB and leaves them in the AssetManager, where
    the field builder can stamp out as many copies as it likes - each copy sharing the one mesh, so
    two hundred and fifty-six floor tiles are a handful of draw calls rather than two hundred and
    fifty-six.

    --- WHAT IS TAKEN FROM THE FILE, AND WHAT IS NOT ---------------------------------------------
    Only the MESH and the MATERIALS. The node's X and Z translation is Blender layout - the artist
    spreads the pieces out so they do not sit inside each other - and is thrown away, because this
    app decides where things go.

    THE NODE'S Y TRANSLATION IS KEPT, and that is not an inconsistency. It is the height the piece
    has to sit at for its feet or its top to land on the ground plane, which is a property of the
    model and is visible in Blender as "does it stand on the floor". Three of the eleven need one:
    tile_rock is modelled 0.32 low, and the bomb and the character are modelled below their own
    origins. Taking it means the app never carries a table of per-asset fudge heights that has to
    be re-derived every time something is re-exported - the check stays "does it look right in
    Blender", which is the only check the artist can actually run.

    RENDER THREAD ONLY: GetAssetsFromGLTF says so itself, and means it - it uploads meshes.
*/
void ApplicationBomber::LoadAssets(void){
    //LoadGLTFFile returns nothing, so a missing file shows up as every GetAssetsFromGLTF below
    //failing to find its node. That is noisy but clear in stderr, and the AddCellObject guard
    //turns it into a blank board rather than a crash.
    gltfloader.LoadGLTFFile("meshes/bomber_assets.glb");

    //Everything with a mesh. Naming them rather than calling GetAllAssetsFromGLTF so that a node
    //added to the file for some other purpose does not silently become a game asset.
    GetAssetsFromGLTF("tile_grass","tile_brick","tile_rock","tile_water",
                      BOMBER_WALL_ASSET,
                      "grass_flowers","grass_plant","turd","bridge",
                      BOMBER_BOMB_ASSET,BOMBER_CHAR_ASSET);

    //The two that move, so their height does not have to be looked up every tick.
    character_y = gltfloader.GetNodePosition(BOMBER_CHAR_ASSET).y;
    bomb_y      = gltfloader.GetNodePosition(BOMBER_BOMB_ASSET).y;
}

Object* ApplicationBomber::AddCellObject(const char* asset_name, int cx, int cz,
                                         float y_offset, float yaw, bool f_random_yaw){
    Object* object = assetmanager->GetObjectFromAsset(asset_name);
    if (!object){
        debug->Err("No asset called %s - is it in bomber_assets.glb?\n",asset_name);
        return NULL;
    }
    char name[64];
    snprintf(name,sizeof(name),"%s (%i,%i)",asset_name,cx,cz);
    object->name = name;
    object->SetPosition(CellCentre(cx,cz) + vec3(0.0f,y_offset,0.0f));

    /*
        A quarter-turn of variety on the scattered pieces, derived from the CELL rather than drawn
        from a random stream.

        Deterministic on purpose: it costs no state, it survives a rebuild of the same seed
        unchanged, and - the real reason - it cannot shift the simulation's random stream, which is
        the open problem RRandom.h describes. A hash of two small ints is plenty when the answer is
        one of four angles.
    */
    if (f_random_yaw){
        uint32_t h = (uint32_t)(cx * 73856093) ^ (uint32_t)(cz * 19349663);
        object->SetRotation(quat(vec3(0,1,0),(float)(h & 3) * (TYPE_PI * 0.5f)));
    }else{
        object->SetRotation(quat(vec3(0,1,0),yaw));
    }

    main_scene->AddObject(object);
    field_objects.push_back(object);
    return object;
}

void ApplicationBomber::RebuildField(void){
    //Throw away whatever is standing. Destroy() only MARKS; DeleteDestroyedObjects is what frees
    //them and takes them out of the scene, and it is safe HERE because we hold physics_mutex - the
    //render thread is not walking the object list. Same reasoning as ApplicationBreakout::NewGame.
    for (Object* object:field_objects){
        if (object){
            object->Destroy();
        }
    }
    field_objects.clear();
    if (character){
        character->Destroy();
        character = NULL;
    }
    if (bomb){
        bomb->Destroy();
        bomb = NULL;
    }
    renderer->DeleteDestroyedObjects();

    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            uint8_t t = maze.tile[z][x];
            //The floor. Every cell gets one, walls included - see the note on BOMBER_TILE_ASSET.
            const char* tile_asset = BOMBER_TILE_ASSET[t < MAZE_TILE_COUNT ? t : 0];
            float tile_y = gltfloader.GetNodePosition(tile_asset).y;
            //Floor tiles are turned at random too. They are square and the texture is not, so four
            //orientations is four times as much board for nothing.
            AddCellObject(tile_asset,x,z,tile_y,0.0f,true);

            if (t == MAZE_TILE_WALL){
                AddCellObject(BOMBER_WALL_ASSET,x,z,
                              gltfloader.GetNodePosition(BOMBER_WALL_ASSET).y,0.0f,true);
            }

            uint8_t d = maze.decor[z][x];
            if (d != MAZE_DECOR_NONE && d < MAZE_DECOR_COUNT && BOMBER_DECOR_ASSET[d]){
                /*
                    A BRIDGE IS NOT SCENERY and must not be turned at random: it is the thing that
                    makes the water under it crossable, and the way it is turned is a RULE - the
                    cell's pass_axis, which the walker reads to refuse a step across the rope rails.
                    So the model takes its angle from that axis and the picture always agrees with
                    what the game will actually let you do. A row of them shares one axis, which is
                    what turns three planks at three angles into one bridge.
                */
                bool f_yaw = (d != MAZE_DECOR_BRIDGE);
                float yaw = (maze.pass_axis[z][x] == MAZE_AXIS_X) ? BOMBER_BRIDGE_YAW_X
                                                                  : BOMBER_BRIDGE_YAW_Z;
                AddCellObject(BOMBER_DECOR_ASSET[d],x,z,
                              gltfloader.GetNodePosition(BOMBER_DECOR_ASSET[d]).y,yaw,f_yaw);
            }
        }
    }

    //The two that move. Not in field_objects: they outlive a rebuild conceptually, and keeping
    //them separate is what stops the loop above destroying the thing the player is.
    character = assetmanager->GetObjectFromAsset(BOMBER_CHAR_ASSET);
    if (character){
        character->name = "Character";
        main_scene->AddObject(character);
    }
    bomb = assetmanager->GetObjectFromAsset(BOMBER_BOMB_ASSET);
    if (bomb){
        bomb->name = "Bomb";
        bomb->SetVisibility(false);
        main_scene->AddObject(bomb);
    }

    SyncView();
}

//--- the blast volumes ----------------------------------------------------------------------------

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
    //GL_FRONT and keep exactly the inside faces. Generated rather than taken from an asset because
    //asset meshes are shared by pointer, and tagging one MESH_MODE_SHADER would turn every other
    //user of it into a volume.
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
        one program could only be told one blast_mode per frame - and both modes can be on screen
        at once. Compiling the same .frag twice costs one extra program and keeps the shape code in
        one file, which matters more: if the two modes drifted into two files the comparison would
        be between two shaders rather than between two ways of arranging one.
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

    /*
        The volumes, created once and never destroyed.

        A blast is frequent, and creating and destroying objects on the physics thread while the
        render thread walks the list is exactly the hazard Scene::AddObject warns about. So the
        most a blast can ever need is built now and parked; SyncView moves them onto the tiles of
        whatever is currently burning, and the shader draws nothing when the clock says nothing is.

        All the tile volumes share ONE mesh, so the nine of them are one draw call - which is also
        the reason they cannot depth-sort against each other.
    */
    for (int i = 0; i < BOMBER_MAX_BLAST_TILES; i++){
        Object* v = new Object();
        char name[48];
        snprintf(name,sizeof(name),"Blast Tile %i",i);
        v->name = name;
        v->SetMesh(tile_mesh);
        v->SetScale(vec3(BOMBER_TILE_BOX,BOMBER_TILE_BOX,BOMBER_TILE_BOX));
        //No material: the shader computes its own colour and never touches the material buffer.
        v->SetMaterialSlot(0,-1);
        //Belt and braces. MESH_MODE_SHADER meshes do not go through DeferredPass at all, so the
        //box never reaches the object-id buffer and could not be picked anyway.
        v->SetPickability(false);
        main_scene->AddObject(v);
        blast_tiles.push_back(v);
    }

    blast_cross = new Object();
    blast_cross->name = "Blast Cross Volume";
    blast_cross->SetMesh(cross_mesh);
    blast_cross->SetScale(vec3(BOMBER_CROSS_BOX,BOMBER_CROSS_BOX,BOMBER_CROSS_BOX));
    blast_cross->SetMaterialSlot(0,-1);
    blast_cross->SetPickability(false);
    main_scene->AddObject(blast_cross);
}

//The two Shader::uniform_callbacks. Each is the shared push with its own mode's box size, plus the
//flag saying whether that renderer is wanted at all - see PushBlastUniforms.
void ApplicationBomber::SetTileUniforms(void){
    PushBlastUniforms(tile_shader,BLAST_MODE_TILE,BOMBER_TILE_BOX,f_draw_tiles);
}

void ApplicationBomber::SetCrossUniforms(void){
    PushBlastUniforms(cross_shader,BLAST_MODE_CROSS,BOMBER_CROSS_BOX,f_draw_cross);
}

void ApplicationBomber::PushBlastUniforms(Shader* shader, int mode, float box_world, bool f_enabled){
    if (!shader){
        return;
    }
    //World units -> this volume's object units. One number because the boxes are uniform cubes;
    //see the note on BOMBER_TILE_BOX for why they have to be.
    float to_object = 1.0f / box_world;

    shader->Setint("blast_mode",mode);

    /*
        A renderer that is switched off is told the blast is over rather than having its objects
        hidden.

        One uniform, written on the thread that is already writing uniforms, against a visibility
        flag that the physics thread would have to write while the render thread reads it. The
        shader's very first line discards on a negative age, so this costs a fragment prologue on
        the box's pixels and nothing else.
    */
    shader->Setfloat("blast_age",f_enabled ? blast_age_view : -1.0f);
    shader->Setfloat("blast_seed",blast_seed_view);

    //The maze, as the shader needs it. Arm limits are in TILES for both modes; cell_object is what
    //the cross mode multiplies them by to get a skeleton length.
    shader->Setvec4("arm_limit",blast_arms_view);
    shader->Setvec3("blast_origin",blast_origin_view);
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
            The cross box is several times the tile box on a side, so the same step COUNT is
            several times the step LENGTH and the arms go visibly stripy. Paying for that here
            rather than with a second knob keeps the two modes on one setting: move the slider and
            both get proportionally finer, which is what makes a quality comparison fair.
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

    Both are reloaded even if the first fails, so one broken mode cannot leave the other stale -
    they are the same source, and a half-reloaded pair would be a genuinely confusing thing to be
    looking at. A reload is always soft whatever f_fatal_on_error says: on failure the old program
    keeps drawing and compile_log says why.
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

//--- input, commands and the loop -----------------------------------------------------------------

void ApplicationBomber::SetupInput(void){
    InputController* input = main_scene->inputcontroller;

    //Arrows and WASD and the d-pad, because muscle memory differs and all three cost nothing:
    //KeyState::f_isdown counts HELD MAPPINGS rather than being a boolean, so an action stays down
    //while any of its keys is.
    input->AddKeyMap(VK_UP,INPUT_BOMBER_NORTH);
    input->AddKeyMap('W',INPUT_BOMBER_NORTH);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_UP,INPUT_BOMBER_NORTH);
    input->AddKeyMap(VK_DOWN,INPUT_BOMBER_SOUTH);
    input->AddKeyMap('S',INPUT_BOMBER_SOUTH);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_DOWN,INPUT_BOMBER_SOUTH);
    input->AddKeyMap(VK_LEFT,INPUT_BOMBER_WEST);
    input->AddKeyMap('A',INPUT_BOMBER_WEST);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_BOMBER_WEST);
    input->AddKeyMap(VK_RIGHT,INPUT_BOMBER_EAST);
    input->AddKeyMap('D',INPUT_BOMBER_EAST);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_RIGHT,INPUT_BOMBER_EAST);

    input->AddKeyMap(VK_SPACE,INPUT_BOMBER_DROP);
    input->AddKeyMap('B',INPUT_BOMBER_DROP);
    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_BOMBER_DROP);

    input->AddKeyMap('R',INPUT_BOMBER_RESTART);
    input->AddKeyMap(VK_F5,INPUT_BOMBER_RELOAD_SHADER);

    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature - and pausing
    //mid-fireball is the single most useful thing you can do to a volumetric effect.
    input->AddKeyMap('P',INPUT_PAUSE);
}

void ApplicationBomber::RegisterCommandHandlers(void){
    main_scene->RegisterCommandHandler(BOMBER_CMD_RESTART,
        [this](const SimCommand& cmd) -> objectid_t {
            //The seed travels IN the command, so a recorded restart lays out the same maze on
            //replay. 0 means "pick a fresh one".
            uint32_t seed = (uint32_t)cmd.value[0];
            current_seed = seed ? seed : next_auto_seed++;
            maze.NewGame(current_seed);
            RebuildField();
            debug->Ok("New field, seed %u\n",current_seed);
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(BOMBER_CMD_DETONATE,
        [this](const SimCommand& cmd) -> objectid_t {
            (void)cmd;
            //Drop one where the character is and light it short, so a blast can be looked at
            //without waiting out the fuse. Goes through the same fields a placed bomb does rather
            //than calling Explode directly, so what is being looked at is a real bomb.
            if (!maze.f_bomb && !maze.f_blast){
                maze.f_bomb = true;
                maze.bomb_x = maze.tile_x;
                maze.bomb_z = maze.tile_z;
            }
            maze.fuse_ticks = 1;
            return OBJECTID_INVALID;
        });
}

/*
    The held keys, as the one direction Maze wants.

    KEEP WHAT YOU HAVE IF IT IS STILL HELD, otherwise take the first one that is. That rule is the
    difference between a walker that corners and one that fights you: with a fixed priority order,
    holding right and then also pressing up turns you up only if up happens to sort first, and
    letting go of up drops you back to right in a way that feels like a dropped input. Keeping the
    current heading while it is valid means a diagonal press is a request to turn WHEN THE CURRENT
    WAY IS RELEASED, which is what a thumb expects.
*/
int ApplicationBomber::ReadDirection(void){
    InputController* input = main_scene->inputcontroller;
    static const int ACTION[MAZE_NUM_DIRS] = {
        INPUT_BOMBER_EAST,INPUT_BOMBER_WEST,INPUT_BOMBER_NORTH,INPUT_BOMBER_SOUTH
    };

    if (maze.facing >= 0 && maze.facing < MAZE_NUM_DIRS && input->IsKeyDown(ACTION[maze.facing])){
        return maze.facing;
    }
    for (int d = 0; d < MAZE_NUM_DIRS; d++){
        if (input->IsKeyDown(ACTION[d])){
            return d;
        }
    }
    return MAZE_DIR_NONE;
}

/*
    Physics thread, once per tick that actually runs, physics_mutex held.

    The game is HERE and nowhere else, which is what makes the whole thing pause, single-step and
    replay together. The drop key is WasKeyPressed rather than IsKeyDown because a bomb is an edge,
    and an edge read on a pass that does not tick would be cleared before any gameplay saw it
    (backlog item 84; the long version is on InputController::ApplyTickInput).
*/
void ApplicationBomber::RunSimulationTick(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    /*
        While the input lock is on, the game's controls are accepted only when a SCRIPTED hold is
        running. See the note on f_lock_human_input for why that test and not a better one: the
        engine mixes scripted holds into the same KeyState as real keys, so this is as close as the
        app can get without changing core.

        The keys are still READ either way. WasKeyPressed consumes an edge, and an edge left unread
        for the duration of a test would arrive the instant the lock lifted - a bomb dropped by a
        key pressed a minute ago is worse than one that was ignored.
    */
    bool f_restart = input->WasKeyPressed(INPUT_BOMBER_RESTART);
    bool f_drop = input->WasKeyPressed(INPUT_BOMBER_DROP);
    int direction = ReadDirection();
    if (f_lock_human_input && !input->HasSyntheticHolds()){
        f_restart = false;
        f_drop = false;
        direction = MAZE_DIR_NONE;
    }

    if (f_restart){
        SimCommand cmd;
        cmd.type = BOMBER_CMD_RESTART;
        cmd.value[0] = 0.0f;    //0 means "pick a fresh seed"
        main_scene->SubmitCommand(cmd);
    }

    MazeInput in;
    in.direction = direction;
    in.f_place_bomb = f_drop;
    maze.Tick(in);

    SyncView();
}

/*
    Puts the view where the rules say it is. Physics thread, end of the tick.

    Everything here is a WRITE DERIVED FROM Maze and never the other way round. That one-way rule
    is what lets the whole view be thrown away and rebuilt (RebuildField) without the game noticing,
    and it is why the character's world position is computed from CharX/CharZ every tick rather
    than being integrated here.
*/
void ApplicationBomber::SyncView(void){
    //--- the character ---------------------------------------------------------------------------
    if (character){
        //CharX/CharZ are in TILES and fractional across a step; the cell size lives on this side.
        vec3 base = CellCentre(0,0);
        character->SetPosition(vec3(base.x + maze.CharX() * BOMBER_CELL_SIZE,
                                    character_y,
                                    base.z + maze.CharZ() * BOMBER_CELL_SIZE));
        /*
            Face the way it walks.

            TWO CONVENTIONS MEET HERE AND THEY POINT OPPOSITE WAYS, which is the whole reason this
            table needs a comment rather than being four obvious numbers:

              - the ENGINE's forward axis is -Z. Setting yaw 0 and asking object_get for
                world_forward returns (0,0,-1), so yaw t gives forward (-sin t, 0, -cos t).
              - the MODEL faces +Z, which is BLENDER'S forward and exactly what an export should
                produce - it is not an art fault and there is no export setting to change it. Point
                the camera due south of the character at yaw 0 and you are looking at its face.

            So the yaw that makes the character LOOK in direction D is the one that puts the
            engine's forward at -D, which is what these four are.

            Worth measuring rather than reasoning about: two rounds of getting this wrong were
            really the camera being dragged between the camera_set and the screenshot, which
            silently reframes the test. object_get's world_forward is the honest instrument - it
            needs no picture - and a screenshot is only needed once, to settle which way the ART
            faces relative to that.
        */
        static const float YAW[MAZE_NUM_DIRS] = {
             TYPE_PI * 0.5f,    //EAST  +X
            -TYPE_PI * 0.5f,    //WEST  -X
             TYPE_PI,           //NORTH -Z
             0.0f               //SOUTH +Z - the rest pose
        };
        if (maze.facing >= 0 && maze.facing < MAZE_NUM_DIRS){
            character->SetRotation(quat(vec3(0,1,0),YAW[maze.facing]));
        }
    }

    //--- the bomb --------------------------------------------------------------------------------
    if (bomb){
        bomb->SetVisibility(maze.f_bomb);
        if (maze.f_bomb){
            bomb->SetPosition(CellCentre(maze.bomb_x,maze.bomb_z) + vec3(0.0f,bomb_y,0.0f));
        }
    }

    //--- the blast -------------------------------------------------------------------------------
    float age = maze.f_blast ? (float)maze.blast_ticks : -1.0f;
    vec3 origin = CellCentre(maze.blast_x,maze.blast_z) + vec3(0.0f,BOMBER_FLAME_HEIGHT,0.0f);

    /*
        Move the per-tile volumes onto the cells that are burning.

        Done every tick rather than once when a blast starts, because it is nine SetPosition calls
        against the cost of a flag saying whether it has been done - and because the shader works
        out which ring a tile is in FROM ITS POSITION, so a volume left in last blast's place would
        not be a stale-looking tile, it would be a tile with the wrong delay and the wrong arm.

        Volumes past the blast's actual reach are parked ON the centre rather than moved away: the
        shader discards them by arm limit anyway, and stacking them costs one discarded fragment
        each while moving them somewhere far away would put a box in the middle of the board.
    */
    int slot = 0;
    if (slot < (int)blast_tiles.size()){
        blast_tiles[slot++]->SetPosition(origin);
    }
    for (int d = 0; d < MAZE_NUM_DIRS && slot < (int)blast_tiles.size(); d++){
        for (int step = 1; step <= MAZE_BLAST_RANGE && slot < (int)blast_tiles.size(); step++){
            bool f_reached = maze.f_blast && step <= maze.arm[d];
            vec3 p = f_reached
                   ? CellCentre(maze.blast_x + Maze::DirX(d) * step,
                                maze.blast_z + Maze::DirZ(d) * step) + vec3(0.0f,BOMBER_FLAME_HEIGHT,0.0f)
                   : origin;
            blast_tiles[slot++]->SetPosition(p);
        }
    }
    if (blast_cross){
        blast_cross->SetPosition(origin);
    }

    /*
        The light follows the fire.

        A flat brightness for the blast's whole life lights the field as brightly while the last
        smoke drifts as it does at the detonation, which is exactly backwards - the flash is the
        moment the board should go orange. So: a very fast rise over the first few ticks and a
        fourth-power decay, which is steeper than the fire's own cooling curve because a point
        light has no smoke to hide behind. It also RISES with the flame, using the same `rise` the
        shader applies.
    */
    if (blast_light){
        if (age >= 0.0f){
            float t = clamp(age / max(blast_life,1.0f),0.0f,1.0f);
            float ignite = clamp(age / 3.0f,0.0f,1.0f);
            float decay = powf(1.0f - t,4.0f);
            blast_light->brightness = blast_light_brightness * ignite * decay;
            blast_light->radius = blast_light_radius;
            blast_light->SetPosition(origin + vec3(0.0f,rise * t * t,0.0f));
        }else{
            blast_light->brightness = 0.0f;
        }
    }

    //Published last, so the render thread never sees an age that is ahead of the volumes.
    blast_origin_view = origin;
    blast_arms_view = vec4((float)maze.arm[MAZE_DIR_EAST],(float)maze.arm[MAZE_DIR_WEST],
                           (float)maze.arm[MAZE_DIR_NORTH],(float)maze.arm[MAZE_DIR_SOUTH]);
    blast_age_view = age;
    //Derived from the count rather than drawn from a random stream: a blast has to look different
    //from the last one, and has to differ THE SAME WAY on a replay. The multiplier is
    //irrational-ish so consecutive blasts land far apart in the noise.
    blast_seed_view = (float)maze.blast_count * 0.6180339887f;
}

/*
    Physics thread, every pass - including the ones that simulate nothing. Camera and the reload key
    only: neither is simulation, and nothing here may write something a tick will read.

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
        dragging a slider in the Bomber panel does not also swing the camera.

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

    /*
        Hover and click-to-select, which is what puts something in the Inspector.

        THE APP HAS TO ASK FOR THIS. Picking is not on by default - CheckObjectSelection is a
        protected method on Application and an app that never calls it simply has no selection, with
        no error to say so. Every app that wants an Inspector calls it from UpdateView; this one did
        not, which looked exactly like picking being broken.
    */
    CheckObjectSelection();

    //Drained every pass whether or not anything will act on them - see the block above. Draining
    //while the lock is on matters for the same reason it matters behind a button gate: a test's
    //worth of mouse movement would otherwise arrive in one lump the moment the lock lifted.
    int cam_dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int cam_dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);
    int wheel = input->GetDelta(INPUT_MOUSE_WHEEL);

    if (f_lock_human_input){
        //Everything below this line is a human moving the camera or asking for a shader reload, and
        //under the lock neither happens. MCP still drives the camera through camera_set, which does
        //not come through here at all - that is the point of the lock.
        return;
    }

    if (input->WasKeyReleased(INPUT_BOMBER_RELOAD_SHADER)){
        //NOT a GL call: it only raises a flag that PreRender acts on. UpdateView runs on the
        //physics thread, which may not touch the context at all.
        f_shader_reload_requested = true;
    }

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
        and a few seconds of it put the camera far enough away that the board is a speck on a black
        screen - which looks exactly like a renderer that stopped drawing.
    */
    if (main_window->f_has_focus){
        if (mouse_wheel_sum != 0.0f){
            vec3 diff = camera->GetPosition() - camera_target;
            float distance = diff.length();
            float step = distance * mouse_wheel_sum / 50.0f;
            float target = clamp(distance - step,1.5f,60.0f);
            camera->MoveForwardBy(distance - target);
            mouse_wheel_sum /= 1.1f;
            if (fabsf(mouse_wheel_sum) < 0.01f){
                mouse_wheel_sum = 0.0f;
            }
        }
        //`wheel` was read at the top of this function, so a scroll over a panel is dropped rather
        //than piling up and arriving all at once the moment the pointer leaves it.
        if (!UIWantsMouse()){
            mouse_wheel_sum += (float)wheel;
        }
    }
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

//--- the knobs --------------------------------------------------------------------------------------

/*
    One table, driving the panel's sliders, the bomber_set tool and the bomber_state readout.

    Built here rather than as a static initialiser because every entry points at a member of THIS
    object. Ranges are what is worth exploring, not what is legal - the shader clamps what it has
    to, and a slider whose useful third is two pixels wide is a slider nobody tunes with.

    Lengths are in WORLD UNITS. PushBlastUniforms converts them per volume, which is what lets one
    slider mean the same thing to a 2-unit tile box and a 6-unit cross box.
*/
void ApplicationBomber::BuildKnobTable(void){
    knobs.clear();
    knobs.push_back({"blast_life",&blast_life,NULL,20.0f,400.0f,
        "how long one blast lasts, in simulation ticks"});
    knobs.push_back({"blast_radius",&blast_radius,NULL,0.1f,2.0f,
        "radius of the flame tube, in WORLD units - a grid cell is 1.0"});
    knobs.push_back({"tile_delay",&tile_delay,NULL,0.0f,20.0f,
        "ticks each ring of tiles waits behind the one nearer the bomb (per-tile renderer only)"});
    knobs.push_back({"rim_softness",&rim_softness,NULL,0.01f,1.0f,
        "width of the fade at the front's edge, as a fraction of the radius"});
    knobs.push_back({"turbulence",&turbulence,NULL,0.0f,2.0f,
        "how far the noise pushes the front in and out - 0 is a smooth sphere"});
    knobs.push_back({"noise_scale",&noise_scale,NULL,0.25f,6.0f,
        "how many times the noise tiles across the flame's diameter - the billow size"});
    knobs.push_back({"outflow",&outflow,NULL,0.0f,1.5f,
        "how far the billows are dragged outward over a life"});
    knobs.push_back({"rise",&rise,NULL,0.0f,2.0f,
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
        "steps along the view ray - the app's main cost. The cross renderer gets double"});
    knobs.push_back({"num_light_steps",NULL,&num_light_steps,0.0f,16.0f,
        "steps towards each light per view step, for the smoke only. 0 disables scattering"});
    knobs.push_back({"light_falloff",&light_falloff,NULL,0.5f,3.0f,
        "attenuation exponent: brightness/pow(distance,this)"});
    knobs.push_back({"max_radiance",&max_radiance,NULL,0.5f,40.0f,
        "ceiling on in-scattered radiance at one sample"});
    knobs.push_back({"blast_light_brightness",&blast_light_brightness,NULL,0.0f,60.0f,
        "peak brightness of the point light the fire throws on the field"});
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

json ApplicationBomber::MapJson(void){
    json out;
    static const char GLYPH[MAZE_TILE_COUNT] = {'.',',',':','~','#'};
    static const char ZONE_GLYPH[MAZE_STYLE_COUNT] = {'B','L','O'};
    json rows = json::array();
    json zones = json::array();
    for (int z = 0; z < MAZE_H; z++){
        std::string row;
        std::string zrow;
        for (int x = 0; x < MAZE_W; x++){
            char c = GLYPH[maze.tile[z][x] < MAZE_TILE_COUNT ? maze.tile[z][x] : 0];
            if (maze.decor[z][x] == MAZE_DECOR_BRIDGE){
                //Which WAY the bridge runs, because that is the half of it a caller
                //cannot guess and the half that decides whether a step is legal.
                c = (maze.pass_axis[z][x] == MAZE_AXIS_X) ? '-' : '|';
            }
            if (x == maze.tile_x && z == maze.tile_z){
                c = '@';
            }else if (maze.f_bomb && x == maze.bomb_x && z == maze.bomb_z){
                c = 'o';
            }
            row += c;
            zrow += (maze.tile[z][x] == MAZE_TILE_WALL && (x == 0 || z == 0 ||
                     x == MAZE_W - 1 || z == MAZE_H - 1))
                  ? ' '
                  : ZONE_GLYPH[maze.zone[z][x] < MAZE_STYLE_COUNT ? maze.zone[z][x] : 0];
        }
        rows.push_back(row);
        zones.push_back(zrow);
    }
    out["map"] = rows;
    out["map_legend"] = ". grass  , brick  : rock  ~ water  - bridge east-west  "
                           "| bridge north-south  # wall  @ character  o bomb";
    out["zones"] = zones;
    out["zone_legend"] = "B bomber pillar grid  L labyrinth  O open plaza";

    return out;
}

json ApplicationBomber::StateJson(void){
    json result;
    result["seed"] = current_seed;
    result["tick"] = main_scene ? main_scene->GetPhysicsTick() : 0;

    //The character and the bomb, in TILES - which is the space every rule is written in, so it is
    //the space a caller should be reasoning and asserting in too.
    result["character"] = json{
        {"tile",json::array({maze.tile_x,maze.tile_z})},
        {"stepping",maze.step_ticks > 0},
        {"facing",maze.facing},
        {"x",maze.CharX()},
        {"z",maze.CharZ()}
    };
    result["bomb"] = json{
        {"live",maze.f_bomb},
        {"tile",json::array({maze.bomb_x,maze.bomb_z})},
        {"fuse_ticks",maze.fuse_ticks}
    };
    result["blast"] = json{
        {"live",maze.f_blast},
        {"tile",json::array({maze.blast_x,maze.blast_z})},
        {"age_ticks",maze.blast_ticks},
        {"count",maze.blast_count},
        {"arm_tiles",json{
            {"east",maze.arm[MAZE_DIR_EAST]},
            {"west",maze.arm[MAZE_DIR_WEST]},
            {"north",maze.arm[MAZE_DIR_NORTH]},
            {"south",maze.arm[MAZE_DIR_SOUTH]}}}
    };
    result["renderers"] = json{{"tiles",f_draw_tiles},{"cross",f_draw_cross}};
    result["input_locked"] = f_lock_human_input;
    /*
        What the mouse is over and what was last clicked.

        Here because picking is otherwise invisible to anything but a human looking at the
        Inspector, and "is picking working" then has no answer that is not a screenshot of a panel.
        These two separate the two ways it can be broken: no `hovered` means the readback or the
        mouse position is wrong, `hovered` without `selected` means the click edge is not arriving.
    */
    result["hovered"] = hovered_object ? hovered_object->name : std::string();
    result["selected"] = selected_object ? selected_object->name : std::string();
    //Where the pointer is as the ENGINE sees it, which is the other half of the picking chain: a
    //hover that is empty while the mouse is off the window means nothing is wrong at all.
    if (main_scene && main_scene->inputcontroller){
        int2 m = main_scene->inputcontroller->GetRelativeMousePosition();
        result["mouse"] = json{
            {"over_window",main_scene->inputcontroller->IsMouseOverWindow()},
            {"x",m.x},{"y",m.y},
            {"hovered_id",(int)main_scene->inputcontroller->GetHoveredObjectID()}
        };
    }
    //How much of the field the walker can actually get to. The one number that says whether a seed
    //produced a playable board or a pretty one with most of itself walled off.
    result["reachable_cells"] = maze.reachable_cells;

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

//--- MCP ---------------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationBomber::RegisterMCPTools(void){
    MCPServer::Get()->RegisterTool("bomber_state",
        "Everything about the game as the RULES see it: the character's tile, whether it is "
        "mid-step, the live bomb and its fuse, the blast and how many tiles each of its arms "
        "reached, how many cells of the field the walker can actually get to, and the blast "
        "effect's tunables. Positions are in TILES on a 16x16 grid, which is the space every rule "
        "is written in - assert in tiles, not in world units. `include_map` adds the board as text "
        "plus a second grid naming which zone style each cell came from.",
        json{
            {"type","object"},
            {"properties", {
                {"include_map", {{"type","boolean"},{"description","also return the tile grid and the zone grid as 16 strings each"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the current frame"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            json result = StateJson();
            if (args.value("include_map",false)){
                result.update(MapJson());
            }
            return MaybeAttachScreenshot(result,
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_input",
        "Hold one of the game's controls for a number of SIMULATION TICKS, exactly as a thumb "
        "would - the event goes through InputController, so it is read by the same code a key "
        "press is and it works while the simulation is paused and being single-stepped. "
        "`action` is north/south/east/west/bomb. A direction wants enough ticks to cross a tile "
        "(20 at the default walk speed); `bomb` is an edge, so one tick is enough. The call "
        "returns as soon as the hold is queued - step or wait for it to play out, then read "
        "bomber_state.",
        json{
            {"type","object"},
            {"properties", {
                {"action", {{"type","string"},{"description","north, south, east, west or bomb"}}},
                {"ticks", {{"type","number"},{"description","how many simulation ticks to hold it, default 20"}}}
            }},
            {"required",json::array({"action"})}
        },
        [this](const json& args) -> json {
            std::string action = args.value("action",std::string());
            uint32_t mapped = 0;
            if (action == "north"){ mapped = INPUT_BOMBER_NORTH; }
            else if (action == "south"){ mapped = INPUT_BOMBER_SOUTH; }
            else if (action == "west"){ mapped = INPUT_BOMBER_WEST; }
            else if (action == "east"){ mapped = INPUT_BOMBER_EAST; }
            else if (action == "bomb"){ mapped = INPUT_BOMBER_DROP; }
            else {
                return json{ {"error","action must be north, south, east, west or bomb"} };
            }
            int ticks = (int)args.value("ticks",20.0f);
            if (ticks < 1){
                ticks = 1;
            }
            main_scene->inputcontroller->HoldKey(mapped,(uint32_t)ticks);
            return json{ {"held",action}, {"ticks",ticks} };
        });

    MCPServer::Get()->RegisterTool("bomber_bomb",
        "Drop a bomb on the character's tile and set its fuse to one tick, so it goes off on the "
        "next tick that runs - the way to look at a blast without waiting out the two-second fuse. "
        "To study a particular frame of one: sim_pause, bomber_bomb, then sim_step the number of "
        "ticks you want, because the whole effect is driven by the tick counter and frame N is "
        "reproducible.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame afterwards"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = BOMBER_CMD_DETONATE;
            //SubmitCommandAndWait, not SubmitUICommand: an MCP handler holds no lock, so it may
            //wait - and it has to, or it would report the state from before the bomb was placed.
            SubmitCommandAndWait(cmd);
            return MaybeAttachScreenshot(StateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_restart",
        "Lay out a fresh field and put the character back on its spawn. The same seed always "
        "produces the same maze, so a layout worth looking at can be got back; pass 0 or leave it "
        "out for a new one.",
        json{
            {"type","object"},
            {"properties", {
                {"seed", {{"type","number"},{"description","field seed; 0 or absent picks a fresh one"}}},
                {"include_map", {{"type","boolean"},{"description","also return the new tile grid"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the new field"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = BOMBER_CMD_RESTART;
            cmd.value[0] = args.value("seed",0.0f);
            SubmitCommandAndWait(cmd);
            json result = StateJson();
            if (args.value("include_map",false)){
                result.update(MapJson());
            }
            return MaybeAttachScreenshot(result,
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_set",
        "Set one tunable of the blast effect by name - the names and current values are the "
        "`knobs` object bomber_state returns. Both renderers share every one of them, which is the "
        "point: a difference on screen has to be a difference between the two ways of drawing the "
        "blast and not between two sets of settings. Lengths are in WORLD units (a grid cell is "
        "1.0) and are converted per volume, so one value means the same thing to both boxes. The "
        "value is held on the C++ side and pushed to the programs on the render thread every "
        "frame, so it survives a shader reload and never writes to a program mid-draw. Setting "
        "`debug_view` to 1 reads out the marched interval and 2 reads out the G-buffer the shader "
        "is handed, which is how to tell a black screen caused by the shape from one caused by the "
        "box. `draw_tiles` and `draw_cross` are not knobs but are accepted here too: they switch "
        "the two renderers on and off, and both on draws the same blast twice.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","tunable to set, as listed by bomber_state, or draw_tiles / draw_cross"}}},
                {"value", {{"type","number"},{"description","the new value; clamped to the tunable's range"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the change"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }},
            {"required",json::array({"name","value"})}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            float value = args.value("value",0.0f);
            if (name == "draw_tiles"){
                f_draw_tiles = value != 0.0f;
            }else if (name == "draw_cross"){
                f_draw_cross = value != 0.0f;
            }else{
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
            return MaybeAttachScreenshot(StateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_lock_input",
        "Ignore the keyboard, the gamepad and the mouse, leaving only input that arrives through "
        "MCP. TURN THIS ON BEFORE ANY SCRIPTED TEST. The app is on screen while it is being driven, "
        "so a hand on the mouse moves the camera between a camera_set and the screenshot after it, "
        "and a stray key walks the character out from under the test - both produce a "
        "plausible-looking picture of the wrong thing. With the lock on, camera_set stays put and "
        "bomber_input is the only thing that moves the character. One caveat, stated because it "
        "cannot be fixed from this side: scripted holds and real keys land in the same place in the "
        "engine, so a key pressed during the exact ticks a bomber_input hold is running still gets "
        "through.",
        json{
            {"type","object"},
            {"properties", {
                {"locked", {{"type","boolean"},{"description","true to ignore human input, false to hand it back"}}}
            }},
            {"required",json::array({"locked"})}
        },
        [this](const json& args) -> json {
            f_lock_human_input = args.value("locked",false);
            return json{ {"input_locked",f_lock_human_input} };
        });

    MCPServer::Get()->RegisterTool("bomber_reload_shader",
        "Recompile shaders/bomber_explosion.frag from disk and swap it into BOTH programs, without "
        "restarting the app - the edit-and-look loop. One source file serves both blast renderers, "
        "so a reload always does the pair; a shader that fails to compile leaves the last working "
        "one drawing and reports the GLSL error rather than taking the app down. The tunables keep "
        "their values, since they live on the C++ side.",
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
            return MaybeAttachScreenshot(StateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });
}
#endif //USE_MCP

//--- UI ------------------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI unconditionally;
//with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationBomber::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderBomberPanel();
}

void ApplicationBomber::RenderBomberPanel(void){
    ImGui::Begin("Bomber");

    ImGui::TextDisabled("arrows/WASD walk   space drops a bomb   R is a new field");
    ImGui::Text("Field seed %u   %i cells reachable",current_seed,maze.reachable_cells);
    //Visible in the panel as well as over MCP, because a lock that is on and forgotten looks
    //exactly like a keyboard that has stopped working.
    ImGui::Checkbox("lock out keyboard/mouse (MCP only)",&f_lock_human_input);
    if (f_lock_human_input){
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.7f,0.2f,1.0f),"LOCKED");
    }
    ImGui::Text("Character  tile (%2i,%2i)%s",maze.tile_x,maze.tile_z,
                maze.step_ticks > 0 ? "  walking" : "");

    if (maze.f_bomb){
        ImGui::Text("Bomb at (%2i,%2i)",maze.bomb_x,maze.bomb_z);
        //A bar rather than a number: a fuse is a countdown, and the thing worth knowing at a
        //glance is how much of it is left rather than its value in ticks.
        ImGui::ProgressBar(clamp((float)maze.fuse_ticks / (float)MAZE_FUSE_TICKS,0.0f,1.0f),
                           ImVec2(-1,0),"fuse");
    }else if (maze.f_blast){
        ImGui::Text("Blast at (%2i,%2i)  arms E%i W%i N%i S%i",maze.blast_x,maze.blast_z,
                    maze.arm[MAZE_DIR_EAST],maze.arm[MAZE_DIR_WEST],
                    maze.arm[MAZE_DIR_NORTH],maze.arm[MAZE_DIR_SOUTH]);
        ImGui::ProgressBar(clamp((float)maze.blast_ticks / max(blast_life,1.0f),0.0f,1.0f),
                           ImVec2(-1,0),"burning");
    }else{
        ImGui::TextDisabled("No bomb");
        //A bar either way, so the panel does not change height when something goes off and the
        //controls under it do not jump out from under the pointer mid-drag.
        ImGui::ProgressBar(0.0f,ImVec2(-1,0),"---");
    }
    ImGui::Text("%u blasts this field",maze.blast_count);

    if (ImGui::Button("Bomb now")){
        //SubmitUICommand, never SubmitCommandAndWait: this runs on the render thread with
        //physics_mutex held, and waiting here for the physics thread - which needs that same mutex
        //to drain the queue - would deadlock instantly. See Application::SubmitCommandAndWait.
        SimCommand cmd;
        cmd.type = BOMBER_CMD_DETONATE;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    if (ImGui::Button("New field (R)")){
        SimCommand cmd;
        cmd.type = BOMBER_CMD_RESTART;
        cmd.value[0] = 0.0f;
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

    ImGui::Separator();
    //The two renderers. Both on draws the same blast twice, which is the A/B - see the note at the
    //top of the header.
    ImGui::Checkbox("draw per-tile volumes",&f_draw_tiles);
    ImGui::Checkbox("draw single cross volume",&f_draw_cross);

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
