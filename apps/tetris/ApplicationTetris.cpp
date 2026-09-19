#include "ApplicationTetris.h"
#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit - see the note at the top of it.
//Guarded because a build with USE_IMGUI=0 has no library behind this header, and every panel
//function below that would call it is compiled out too.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#include "Debug.h"
#include "Primitives.h"
#include "type_helpers.h"
#ifdef USE_MCP
//MCPServer.h and nothing else from it: with USE_MCP=0 that class is not compiled, and the
//header also pulls winsock in with the include-order constraint it documents - both
//pointless in a build with no debug server. See the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

static Debugger* debug = new Debugger("ApplicationTetris",DEBUG_ALL);

/*
    Where the board sits in the world. Cell (x,y) is a 1x1x1 cube centred on the integer point
    (x,y,0), so board coordinates ARE world coordinates and nothing needs converting. The well's
    walls and floor are the same cube, scaled.

    The camera is orthographic and looks straight down -Z at this plane, which is what makes a 3D
    scene read as a 2D playfield - see SetupCamera, and §5 of docs/tetris_agent_brief.md for why
    that is the right shape on an engine with no 2D renderer at all.
*/
#define BOARD_ORIGIN_Z      0.0f
#define CELL_VISUAL_SCALE   0.92f   //a hair under 1, so neighbouring blocks have a seam
#define GHOST_VISUAL_SCALE  0.55f   //the ghost is a small marker, not a solid block

//Where the previews live, in the same world coordinates. To the RIGHT of the well, because the
//engine's own debug panels dock to the LEFT (Application::RenderApplicationUI) and would cover
//anything put there.
#define PREVIEW_X           13.5f
#define HOLD_Y              17.0f
//The preview column was pulled UP and tightened when the stats ladder below it gained a BEST
//line: the third preview used to sit at y 5.0 and the score caption now reaches 4.6, and a
//preview piece drawn through the word SCORE reads as a rendering fault. 13.0/3.2 puts the last
//one at 6.6 - clear of the text, and still clear of the HOLD piece above it.
#define NEXT_Y              13.0f
#define NEXT_SPACING_Y      3.2f

//The text column, in those same world coordinates. Left-aligned a little inside the preview
//column so the captions line up with each other rather than with the pieces they label.
//TEXT_SCALE is in ems and one em is one world unit per line, so 0.8 makes a line of text a
//little shorter than a block is tall - see core/TextMesh.h.
#define TEXT_X              11.6f
#define TEXT_SCALE          0.8f

/*
    The debris tray - docs/tetris_findings.md §7.5.

    A shelf to the LEFT of the well and a little in FRONT of it, which is the arrangement that
    makes the whole idea cheap. The camera is orthographic and looks straight down -Z, so depth
    costs nothing on screen: putting the shelf at z 1..5 and launching the chunks from z 1.8 puts
    them past the well's left wall (which is only 2 deep) without any of them having to clear a
    22-unit obstacle, and without a single extra pixel of the board being covered.

    The x range is bounded by what the CAMERA can see, not by taste. Init resizes the window to
    1200x900 and the orthographic zoom is the vertical half-extent, so the visible width is
    11.5 * 4/3 = 15.33 either side of the camera target at x 6 - that is x -9.33 on the left, and
    the far wall has to stand inside that. A tray any wider would pile chunks up off the edge of
    the screen, which is the one thing a feature whose entire point is "the player can see what
    they did" must not do.

    The DEPTH is chosen for the same reason from the other direction. The camera is looking along
    z, so depth is the one dimension the player cannot see - a tray 4 deep swallowed the whole cap
    in a single layer and read as a stripe. 2.2 holds about a third of it per layer, so the pile
    grows UPWARD, which is the direction that shows.

    Nothing here is reachable from the well: the shelf sits in front of it in z, the board is a
    plain array, and no board cube has a collider at all. The rules cannot be touched from in
    here even by accident, which is the condition the finding attaches to the idea.
*/
#define TRAY_X_MIN          -8.0f
#define TRAY_X_MAX          -1.0f
#define TRAY_Z_MIN           1.4f
#define TRAY_Z_MAX           3.6f
#define TRAY_FLOOR_Y        -1.0f   //the same shelf height as the well's own floor
#define TRAY_SHELF_Y        -0.5f   //...so this is the surface chunks come to rest on
#define TRAY_WALL_TOP        2.5f   //only as tall as a full tray needs; a taller one is a grey slab
#define TRAY_LIP_TOP         0.2f   //the front lip is kept LOW, or it would hide the pile it holds

//Where a chunk is born. In front of the well's left wall (which ends at z +1) so it can simply
//fly left, and in front of the blocks (which reach z +0.46) so it is never briefly inside one.
#define DEBRIS_SPAWN_Z       1.8f
//The widest column index, so the fan-out below spans the tray exactly rather than by a fudge.
#define TRAY_BOARD_SPREAD   (TETRIS_BOARD_W - 1)

//Where a score popup floats, in front of everything else on the board. The game-over banner is
//at z 1.2 and the debris passes through z ~1.8, so 2.6 is the first depth nothing else uses.
#define POPUP_Z              2.6f
#define POPUP_X              4.5f    //the middle of the well, cells 0..9
#define POPUP_TEXT_SCALE     0.85f

static vec3 CellWorldPosition(int x, int y){
    return vec3((float)x,(float)y,BOARD_ORIGIN_Z);
}

//A preview piece's four cubes, centred on `anchor`. The piece's cells are offsets into a box of
//GetTetrominoBoxSize, so centring is just subtracting half that box.
static vec3 PreviewCellPosition(const vec3& anchor, int type, int index){
    const TetrominoCell* cells = GetTetrominoCells(type,0);
    float half = (GetTetrominoBoxSize(type) - 1) * 0.5f;
    return vec3(anchor.x + cells[index].x - half,
                anchor.y + cells[index].y - half,
                anchor.z);
}

ApplicationTetris::ApplicationTetris():Application(){
    //The window title. HERE rather than in Init(), because Start() creates the window and Init()
    //runs after it. Not "Tetris" alone: the ImGui HUD further down is already
    //ImGui::Begin("Tetris"), and two Begin() calls sharing a name are one window - see
    //Application::app_name.
    app_name = "Tetris Engine";
    debug->Info("Created new application.\n");
}

ApplicationTetris::~ApplicationTetris(){
}

//--- Setup ----------------------------------------------------------------------------------

void ApplicationTetris::Init(void){
    //Deferred rather than MSAA: it is what gives the object-id buffer, and so mouse picking and
    //the Inspector. A Tetris board does not strictly need picking, but being able to click a
    //block and read it in the Inspector paid for itself twice while getting the layout right.
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //Still constructed even though this app loads no assets: the engine reaches for it
    //unguarded in places (the Scene panel's asset list, the object_spawn command handler).
    assetmanager = new AssetManager();

    //One 1x1x1 cube centred on its own origin, shared by every object in the app. This used to
    //be data/unit_cube.obj loaded through the OBJ loader, purely because the engine had no way
    //to make a cube; backlog item 16 removed that reason, and with it this app's only asset file.
    block_mesh = MakeBox(vec3(1,1,1));
    if (!block_mesh){
        debug->Fatal("Failed to build the block mesh\n");
    }
    //A reference for the app's own pointer, the same way AssetManager holds one for an asset's
    //mesh. Object::DeleteMesh deletes the mesh when its last Object lets go, and a line clear
    //can destroy a lot of cubes at once - without this, the count could reach zero while
    //block_mesh is still about to be handed to the next Object.
    block_mesh->num_references++;

    main_scene = CreateNewScene("Tetris");
    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-14.0f,0));
    main_scene->physics_world->SetDebugRendering(false);

    {   //Without a light everything renders black. Aimed at the middle of the well, with the
        //shadow ortho (viewport.zoom) wide enough to cover the board AND the previews, or the
        //previews fall outside the shadow map and go dark.
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        sun->SetPosition(vec3(-6,24,16));
        sun->SetLookAt(vec3(5,10,0));
        sun->color = vec3(1.0f,0.96f,0.90f);
        sun->brightness = 4.5f;
        sun->viewport.zoom = 26;            //half-extent in world units; the board alone is 20 tall
        main_scene->AddObject(sun);
    }
    {   //A dim fill from the front so the faces pointing at the camera are not pure shadow. A
        //second directional light costs one entry in the light SSBO and nothing else.
        DirectionalLight* fill = new DirectionalLight();
        fill->name = "Fill";
        fill->SetPosition(vec3(14,6,20));
        fill->SetLookAt(vec3(5,10,0));
        fill->color = vec3(0.6f,0.7f,1.0f);
        fill->brightness = 1.2f;
        fill->f_casts_shadow = false;
        fill->viewport.zoom = 26;
        main_scene->AddObject(fill);
    }

    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    //Six handles over four files. Deliberately: SoundSystem gives each HANDLE its own OpenAL
    //source, and a source cannot overlap itself - registering the same file twice is the only way
    //a move and a lock landing on the same tick can both be heard. See docs/tetris_findings.md.
    soundsystem->AppendFile("sound/click.wav","move");
    soundsystem->AppendFile("sound/click.wav","lock");
    soundsystem->AppendFile("sound/bleep.wav","rotate");
    soundsystem->AppendFile("sound/bleep.wav","hold");
    soundsystem->AppendFile("sound/floop.wav","clear");
    soundsystem->AppendFile("sound/hax.wav","gameover");

    BuildMaterials();
    BuildWell();
    BuildDebrisTray();
    BuildViewObjects();
    BuildTextLabels();
    SetupFieldShadows();
    SetupCamera();
    SetupInput();
    RegisterCommandHandlers();
#ifdef USE_MCP
    //Guarded because this app's own tools call into MCPServer, which USE_MCP=0 does not
    //compile. The CORE tools are switched a different way - see engine.mk.
    RegisterMCPTools();
#endif

    //60 ticks per second, not the engine's default 50, and taken from the RULES rather than
    //written here as a literal: the gravity table in Playfield.cpp is denominated in 60Hz frames,
    //so the rate is the rules' to declare and this app's to apply. See TETRIS_TPS in Playfield.h
    //for why the dependency runs that way. Written as a literal, changing one and not the other
    //would leave the table quietly meaning something it does not say.
    SetPhysicsTPS(TETRIS_TPS);

    //Before the first NewGame, so the very first frame already shows what there is to beat.
    LoadBestScore();
    NewGame(current_seed);

    main_window->Resize(1024,600);

    //One tick so the first frame is not an empty board.
    main_scene->StepPhysics(1);
}

void ApplicationTetris::BuildMaterials(){
    //One material per tetromino, in the colours everybody expects. emissive.w is what actually
    //makes a block glow - emissive.rgb alone is clamped to 1 and so can never be brighter than a
    //fully lit white surface (see core/Material.h).
    struct PieceColour{
        const char* name;
        vec4 color;
    };
    static const PieceColour piece_colours[TETROMINO_COUNT] = {
        { "tetris_i", vec4(0.00f,0.82f,0.92f,1.0f) },   //cyan
        { "tetris_j", vec4(0.13f,0.29f,0.85f,1.0f) },   //blue
        { "tetris_l", vec4(0.95f,0.52f,0.08f,1.0f) },   //orange
        { "tetris_o", vec4(0.95f,0.83f,0.10f,1.0f) },   //yellow
        { "tetris_s", vec4(0.20f,0.80f,0.25f,1.0f) },   //green
        { "tetris_t", vec4(0.65f,0.22f,0.85f,1.0f) },   //purple
        { "tetris_z", vec4(0.90f,0.18f,0.22f,1.0f) },   //red
    };

    for (int i = 0; i < TETROMINO_COUNT; i++){
        Material m;
        m.name = piece_colours[i].name;
        m.glsl_material.color = piece_colours[i].color;
        m.glsl_material.metallic = 0.15f;
        m.glsl_material.roughness = 0.45f;
        //A little self-illumination so a block in the shadow of the stack above it still reads
        //as its own colour rather than as a dark grey lump.
        m.glsl_material.emissive = vec4(piece_colours[i].color.x,
                                        piece_colours[i].color.y,
                                        piece_colours[i].color.z,
                                        0.18f);
        renderer->AddMaterial(m);
        //Looked up by name rather than taken from AddMaterial's return value: that returns
        //materials.size()-1 even when the name already existed, so it is only correct for a
        //material that is genuinely new. See docs/tetris_findings.md.
        material_piece[i] = renderer->FindMaterialIndex(m.name);
    }

    {   //The ghost: dark, unlit, barely there - it marks a position, it is not a block.
        Material m;
        m.name = "tetris_ghost";
        m.glsl_material.color = vec4(0.55f,0.58f,0.65f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 1.0f;
        m.glsl_material.emissive = vec4(0.5f,0.55f,0.7f,0.5f);
        renderer->AddMaterial(m);
        material_ghost = renderer->FindMaterialIndex(m.name);
    }
    {   //What a completed row turns into while it flashes.
        Material m;
        m.name = "tetris_flash";
        m.glsl_material.color = vec4(1.0f,1.0f,1.0f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 0.2f;
        m.glsl_material.emissive = vec4(1.0f,1.0f,1.0f,3.0f);
        renderer->AddMaterial(m);
        material_flash = renderer->FindMaterialIndex(m.name);
    }
    {   //The well's walls and floor.
        Material m;
        m.name = "tetris_frame";
        m.glsl_material.color = vec4(0.32f,0.34f,0.40f,1.0f);
        m.glsl_material.metallic = 0.6f;
        m.glsl_material.roughness = 0.35f;
        renderer->AddMaterial(m);
        material_frame = renderer->FindMaterialIndex(m.name);
    }
    /*
        The same frame, warmer and then hot. The well's side walls wear one of these three
        depending on how high the stack has got (TETRIS_WARN_ROWS / TETRIS_DANGER_ROWS), which
        gives the player the one thing the board never told them: that they are running out of
        room. Slightly emissive, so the change is visible in the corner of the eye at the moment
        the player is looking hardest at the middle of the board.

        Three materials swapped by slot rather than one material whose colour is written every
        tick - a Material belongs to the RENDERER and is shared by index, so editing it in place
        would repaint the tray and anything else that ever adopts this frame.
    */
    {
        Material m;
        m.name = "tetris_frame_warn";
        m.glsl_material.color = vec4(0.52f,0.42f,0.22f,1.0f);
        m.glsl_material.metallic = 0.6f;
        m.glsl_material.roughness = 0.35f;
        m.glsl_material.emissive = vec4(0.55f,0.36f,0.06f,0.35f);
        renderer->AddMaterial(m);
        material_frame_warn = renderer->FindMaterialIndex(m.name);
    }
    {
        Material m;
        m.name = "tetris_frame_danger";
        m.glsl_material.color = vec4(0.60f,0.20f,0.20f,1.0f);
        m.glsl_material.metallic = 0.6f;
        m.glsl_material.roughness = 0.35f;
        m.glsl_material.emissive = vec4(0.70f,0.12f,0.10f,0.7f);
        renderer->AddMaterial(m);
        material_frame_danger = renderer->FindMaterialIndex(m.name);
    }
    {   //Text. Emissive because a label sits in front of the dark back panel with no light of
        //its own aimed at it, and unlit letterforms at this size read as grey smudges.
        Material m;
        m.name = "tetris_text";
        m.glsl_material.color = vec4(0.86f,0.89f,0.96f,1.0f);
        m.glsl_material.metallic = 0.1f;
        m.glsl_material.roughness = 0.5f;
        m.glsl_material.emissive = vec4(0.86f,0.89f,0.96f,0.5f);
        renderer->AddMaterial(m);
        material_text = renderer->FindMaterialIndex(m.name);
    }
    {   //The one label that has to shout. emissive.w is a multiplier, so it has to stay near 1
        //here: at the 2-3 the flashing row uses, every channel clips and the red comes out white.
        //A banner that is merely bright is not worth losing its colour for.
        Material m;
        m.name = "tetris_text_hot";
        m.glsl_material.color = vec4(1.0f,0.35f,0.30f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 0.4f;
        m.glsl_material.emissive = vec4(1.0f,0.22f,0.16f,1.0f);
        renderer->AddMaterial(m);
        material_text_hot = renderer->FindMaterialIndex(m.name);
    }
    {   //The back panel the blocks cast their shadows onto. Dark, so lit blocks pop off it.
        Material m;
        m.name = "tetris_back";
        m.glsl_material.color = vec4(0.06f,0.07f,0.10f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 0.9f;
        renderer->AddMaterial(m);
        material_back = renderer->FindMaterialIndex(m.name);
    }
}

//Every view object in this app is the same unit cube with a scale, a position and one material
//slot, so this is the one place that knows how to make one. `mesh` is shared by pointer across
//every cube - SetMesh takes a reference, so the mesh outlives any individual object.
static Object* MakeCube(Mesh* mesh, Scene* scene, const char* name,
                        const vec3& position, const vec3& scale, int material_index){
    if (!mesh){
        return NULL;
    }
    Object* object = new Object();
    object->SetMesh(mesh);
    object->name = name;
    object->SetPosition(position);
    object->SetScale(scale);
    //A generated mesh carries no material names, so unlike the asset-loaded cube this replaced
    //there is nothing for Renderer::UpdateObjectMaterials to resolve over this slot on the next
    //frame. The index is simply the answer. See docs/engine_backlog.md item 14 for the invariant.
    object->SetMaterialSlot(0,material_index);
    scene->AddObject(object);
    return object;
}

void ApplicationTetris::BuildWell(){
    //Walls and floor are single scaled cubes, and they carry static box colliders so the debris
    //spawned by a line clear has something to land in rather than falling through the world.
    struct WellPart{
        const char* name;
        vec3 position;
        vec3 scale;
    };
    static const WellPart parts[] = {
        { "Well Left",  vec3(-1.0f, 9.5f, 0.0f), vec3(1.0f,22.0f,2.0f) },
        { "Well Right", vec3(10.0f, 9.5f, 0.0f), vec3(1.0f,22.0f,2.0f) },
        { "Well Floor", vec3( 4.5f,-1.0f, 0.0f), vec3(13.0f,1.0f,2.0f) },
    };
    for (int i = 0; i < (int)(sizeof(parts)/sizeof(parts[0])); i++){
        Object* part = MakeCube(block_mesh,main_scene,parts[i].name,parts[i].position,parts[i].scale,material_frame);
        if (!part){
            continue;
        }
        //The two side walls are kept: they are what changes colour as the stack climbs. The
        //floor is not - see well_walls in the header.
        if (i < 2){
            well_walls[i] = part;
        }
        Physics* p = part->AddPhysics(main_scene->physics_world);
        if (p){
            //A body from AddPhysics starts STATIC with gravity off, which is exactly what a wall
            //wants - so there is nothing to opt into here. The collider's half extents are in the
            //object's LOCAL space, and the object is scaled, so they are half the scale.
            p->AddBoxCollider(parts[i].scale * 0.5f,vec3(),quat().identity(),1.0f);
            p->SetBounciness(0.2f);
            p->SetFrictionCoefficient(0.8f);
        }
    }
    //The back panel. No collider: debris is meant to tumble forward out of the well, and a wall
    //behind it only ever produced blocks wedged in a corner.
    MakeCube(block_mesh,main_scene,"Well Back",vec3(4.5f,9.5f,-1.2f),vec3(12.0f,22.0f,0.4f),material_back);
}

/*
    The tray the cleared rows end up in.

    Five static boxes, all of them ordinary scaled cubes like the well. What makes this worth its
    own function rather than three more rows in BuildWell's table is what it is FOR: the well's
    walls exist so the game can be played, and these exist so the game can be looked back on. See
    the TRAY_* block at the top of this file for the geometry and for why it is where it is.

    The lips at the front and back are deliberately short - TRAY_LIP_TOP, barely a chunk high.
    They only have to stop a chunk rolling out sideways in z, and the front one is between the
    camera and the pile, so anything taller would hide the thing it is there to keep.
*/
void ApplicationTetris::BuildDebrisTray(){
    float mid_x = (TRAY_X_MIN + TRAY_X_MAX) * 0.5f;
    float mid_z = (TRAY_Z_MIN + TRAY_Z_MAX) * 0.5f;
    float width = TRAY_X_MAX - TRAY_X_MIN;
    float depth = TRAY_Z_MAX - TRAY_Z_MIN;
    float wall_h = TRAY_WALL_TOP - (TRAY_FLOOR_Y - 0.5f);
    float wall_y = (TRAY_WALL_TOP + TRAY_FLOOR_Y - 0.5f) * 0.5f;
    float lip_h = TRAY_LIP_TOP - (TRAY_FLOOR_Y - 0.5f);
    float lip_y = (TRAY_LIP_TOP + TRAY_FLOOR_Y - 0.5f) * 0.5f;

    struct TrayPart{
        const char* name;
        vec3 position;
        vec3 scale;
    };
    const TrayPart parts[] = {
        { "Tray Floor", vec3(mid_x,TRAY_FLOOR_Y,mid_z),         vec3(width,1.0f,depth) },
        //The far wall, at the left-hand end. Chunks from the right of a row arrive fastest and
        //this is what stops them; the ones that reach it stack against it, which is exactly the
        //shape a full tray should have.
        { "Tray Left",  vec3(TRAY_X_MIN,wall_y,mid_z),          vec3(1.0f,wall_h,depth) },
        { "Tray Back",  vec3(mid_x,lip_y,TRAY_Z_MIN),           vec3(width,lip_h,0.4f) },
        { "Tray Front", vec3(mid_x,lip_y,TRAY_Z_MAX),           vec3(width,lip_h,0.4f) },
    };
    for (int i = 0; i < (int)(sizeof(parts)/sizeof(parts[0])); i++){
        Object* part = MakeCube(block_mesh,main_scene,parts[i].name,parts[i].position,parts[i].scale,material_frame);
        if (!part){
            continue;
        }
        //Nothing in the tray is worth selecting, and the front lip sits in front of the pile -
        //leaving it pickable would make it swallow every click aimed at a chunk behind it.
        part->SetPickability(false);
        Physics* p = part->AddPhysics(main_scene->physics_world);
        if (p){
            p->AddBoxCollider(parts[i].scale * 0.5f,vec3(),quat().identity(),1.0f);
            //Deader than the well: a chunk that has arrived should settle and go to sleep rather
            //than keep bouncing. Sleeping bodies are what make the cap affordable at all.
            p->SetBounciness(0.05f);
            p->SetFrictionCoefficient(0.9f);
        }
    }
    //Dark backing so the pile reads against something, exactly like the well's. No collider - it
    //sits behind the back lip, which is what actually keeps the chunks in.
    MakeCube(block_mesh,main_scene,"Tray Back Panel",
             vec3(mid_x,wall_y,TRAY_Z_MIN - 0.6f),vec3(width + 1.0f,wall_h,0.4f),material_back);
}

void ApplicationTetris::BuildViewObjects(){
    //The board: one cube per cell, made once and hidden while its cell is empty. 200 objects is
    //nothing to this renderer, and it means a piece landing never allocates.
    for (int y = 0; y < TETRIS_BOARD_H; y++){
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            char name[32];
            snprintf(name,sizeof(name),"Cell %i,%i",x,y);
            Object* cell = MakeCube(block_mesh,main_scene,name,CellWorldPosition(x,y),
                                    vec3(CELL_VISUAL_SCALE),material_piece[0]);
            if (cell){
                cell->Hide();
            }
            cell_objects[y][x] = cell;
        }
    }
    for (int i = 0; i < 4; i++){
        piece_objects[i] = MakeCube(block_mesh,main_scene,"Piece Cell",vec3(),vec3(CELL_VISUAL_SCALE),material_piece[0]);
        ghost_objects[i] = MakeCube(block_mesh,main_scene,"Ghost Cell",vec3(),vec3(GHOST_VISUAL_SCALE),material_ghost);
        hold_objects[i]  = MakeCube(block_mesh,main_scene,"Hold Cell",vec3(),vec3(CELL_VISUAL_SCALE),material_piece[0]);
        if (piece_objects[i]){ piece_objects[i]->Hide(); }
        if (ghost_objects[i]){ ghost_objects[i]->Hide(); }
        if (hold_objects[i]){ hold_objects[i]->Hide(); }
        //The ghost must not be clickable: it sits in front of the stack and would swallow every
        //pick aimed at a real block.
        if (ghost_objects[i]){ ghost_objects[i]->SetPickability(false); }
    }
    for (int n = 0; n < TETRIS_NEXT_QUEUE_SHOWN; n++){
        for (int i = 0; i < 4; i++){
            next_objects[n][i] = MakeCube(block_mesh,main_scene,"Next Cell",vec3(),vec3(CELL_VISUAL_SCALE),material_piece[0]);
            if (next_objects[n][i]){
                next_objects[n][i]->Hide();
            }
        }
    }
}

/*
    The world-space labels.

    Everything the board says in words goes through here. It is deliberately NOT ImGui: ImGui is
    the debug layer, it does not appear in a screenshot (backlog item 19), and a game should be
    able to ship without it. These are meshes in the scene like everything else.
*/
void ApplicationTetris::BuildTextLabels(){
    /*
        The advance and the line height come from fonts_glyphs.json, which the export script
        writes beside the .glb because glTF has nowhere to put font metrics. This set is
        monospaced, so one advance covers all 95 characters - that is the whole of what an atlas
        would have carried, and why there is no atlas here (see core/TextMesh.h).
    */
    if (!LoadGlyphSetFromGLB(glyphs,"meshes/glyphs_unispace.glb",0.509167f,1.0f)){
        //Not fatal. Without glyphs the board still plays perfectly; it just says nothing, which
        //is exactly the state this app was in before there was any text at all.
        debug->Warn("No glyphs loaded - the board will play without labels\n");
        return;
    }

    struct LabelSetup{
        int         id;
        vec3        position;
        float       scale;
        int         align;
        int         material;
        const char* text;
    };
    static const LabelSetup setup[] = {
        //Captions sit just above the thing they name.
        { TETRIS_LABEL_HOLD,     vec3(TEXT_X,HOLD_Y + 2.0f,0.0f), TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "HOLD" },
        { TETRIS_LABEL_NEXT,     vec3(TEXT_X,NEXT_Y + 2.0f,0.0f), TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "NEXT" },
        /*
            The stats ladder, below the next queue (which ends at NEXT_Y - 2*NEXT_SPACING_Y =
            5.0) and above the bottom of what the camera can see (y -2.0, from the orthographic
            half-extent of 11.5 about a target at 9.5). Score gets two lines because it is the
            number people actually look at; everything else gets one, spaced 1.1 apart - a line
            of text at TEXT_SCALE is 0.8 tall, so that is a clear gap rather than a crowded one.
        */
        { TETRIS_LABEL_SCORE,    vec3(TEXT_X, 4.0f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "SCORE\n0" },
        //Directly under the score it is measured against, because that is the only place a best
        //means anything. Without it a restart throws the whole game away with no record of it.
        { TETRIS_LABEL_BEST,     vec3(TEXT_X, 2.2f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "BEST 0" },
        { TETRIS_LABEL_LINES,    vec3(TEXT_X, 1.1f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "LINES 0" },
        { TETRIS_LABEL_LEVEL,    vec3(TEXT_X, 0.0f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "LEVEL 1" },
        //The run bonuses, under the stats and in the hot material, because they are the two
        //numbers that are TEMPORARY - a player who cannot see the combo is about to break has no
        //reason to care that it exists. Empty (and so hidden) whenever no run is going.
        { TETRIS_LABEL_COMBO,    vec3(TEXT_X,-1.1f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   1, "COMBO x9" },
        //Centred over the middle of the well (cells 0..9, so x 4.5) and in FRONT of the stack -
        //the blocks reach z +0.46, so 1.2 clears them with room for the glyphs' own 0.1 depth.
        { TETRIS_LABEL_GAMEOVER, vec3(4.5f,11.0f,1.2f),           1.30f,      TEXT_ALIGN_CENTER, 1, "GAME OVER\nPRESS R" },
    };

    for (int i = 0; i < (int)(sizeof(setup)/sizeof(setup[0])); i++){
        const LabelSetup& ls = setup[i];
        TetrisLabel& label = labels[ls.id];

        label.object = new Object();
        label.object->name = "Label";
        label.object->SetPosition(ls.position);
        label.object->SetMaterialSlot(0,ls.material ? material_text_hot : material_text);
        //Text must not swallow a pick aimed at the board behind it, and there is nothing useful
        //to inspect about a label anyway.
        label.object->SetPickability(false);
        label.scale = ls.scale;
        label.align = ls.align;

        //Before AddObject, so the object is never in the scene without a mesh for the renderer
        //to batch. SetLabelText is what actually builds it.
        SetLabelText(ls.id,ls.text);
        main_scene->AddObject(label.object);
    }

    //Built with its text so it has a mesh, then emptied so it starts hidden. Giving a label no
    //text is how it is switched off - BuildTextMesh returns NULL for a string with no ink, which
    //is a documented outcome rather than a failure.
    SetLabelText(TETRIS_LABEL_GAMEOVER,"");
    SetLabelText(TETRIS_LABEL_COMBO,"");

    //The banner is the other label that sits over the stack rather than beside it, so it wants
    //the same exemption the popups do - see BuildPopups.
    if (labels[TETRIS_LABEL_GAMEOVER].object){
        labels[TETRIS_LABEL_GAMEOVER].object->SetCastsShadow(false);
    }

    BuildPopups();
}

/*
    The score popups, made once and reused.

    Each is built holding the longest string it will ever be asked to spell, for one reason:
    Mesh::SetMeshData re-uploads into a buffer that already exists, so a pool primed at full size
    never grows a buffer again for the rest of the run. The text is then emptied and the object
    hidden, which is the same "no text means off" convention the game-over banner uses.
*/
void ApplicationTetris::BuildPopups(){
    for (int i = 0; i < TETRIS_POPUP_SLOTS; i++){
        TetrisPopup& popup = popups[i];
        popup.object = new Object();
        popup.object->name = "Score Popup";
        popup.object->SetMaterialSlot(0,material_text_hot);
        popup.object->SetPickability(false);
        //A popup floats OVER the stack, which is the one place in this app where letting text
        //throw a shadow goes wrong: the lamp is above the well, so "TETRIS" lands as a dark
        //smear across the blocks underneath it and reads as a rendering fault rather than as a
        //shadow. The side labels never showed this because nothing is behind them.
        popup.object->SetCastsShadow(false);

        TextLayout layout;
        layout.scale = POPUP_TEXT_SCALE;
        layout.align = TEXT_ALIGN_CENTER;
        layout.matid = 0;
        //The longest line any announcement can produce is "T-SPIN MINI TRIPLE"; prime with it so
        //the vertex buffer is never grown again after Init.
        popup.mesh = BuildTextMesh(glyphs,"T-SPIN MINI TRIPLE\n+99999\nB2B COMBO x99",layout,NULL);
        if (popup.mesh){
            popup.object->SetMesh(popup.mesh);
        }
        popup.object->Hide();
        main_scene->AddObject(popup.object);
    }
}

/*
    What a clear says. The wording is the entire point of the feature - "+1200" on its own does
    not tell anyone WHY it was 1200 - so every bonus that contributed gets named on the third
    line rather than being folded silently into the total.
*/
void ApplicationTetris::FormatClearText(const TetrisClearEvent& clear, char* out, size_t out_size) const{
    if (clear.kind == TETRIS_ANNOUNCE_LEVEL){
        snprintf(out,out_size,"LEVEL %i\n%i TICKS/CELL",
                 clear.level,Playfield::GravityTicksForLevel(clear.level));
        return;
    }

    //Line 3 first, because it is the one that may be empty and an empty line at the end of a
    //string is a line of nothing that still pushes the popup's origin around.
    char bonus[32] = {};
    if (clear.f_back_to_back && clear.combo > 0){
        snprintf(bonus,sizeof(bonus),"\nB2B COMBO x%i",clear.combo);
    }else if (clear.f_back_to_back){
        snprintf(bonus,sizeof(bonus),"\nBACK TO BACK");
    }else if (clear.combo > 0){
        snprintf(bonus,sizeof(bonus),"\nCOMBO x%i",clear.combo);
    }

    /*
        The headline. A perfect clear and a T-spin are both announced BY NAME - each is rare, each
        is worth several times the clear it looks like, and a player who is never told they exist
        will never go looking for them. That is the whole argument for this popup.

        A T-spin that cleared nothing has no line name at all, so it prints as just "T-SPIN" and
        its 400 points; the space before the (empty) line name would be a trailing space, so the
        two cases are formatted apart rather than papered over with one format string.
    */
    char name[40];
    if (clear.f_perfect_clear){
        snprintf(name,sizeof(name),"PERFECT CLEAR");
    }else if (clear.spin != TETRIS_SPIN_NONE && clear.lines > 0){
        snprintf(name,sizeof(name),"%s %s",Playfield::SpinName(clear.spin),Playfield::LineClearName(clear.lines));
    }else if (clear.spin != TETRIS_SPIN_NONE){
        snprintf(name,sizeof(name),"%s",Playfield::SpinName(clear.spin));
    }else{
        snprintf(name,sizeof(name),"%s",Playfield::LineClearName(clear.lines));
    }
    snprintf(out,out_size,"%s\n+%i%s",name,clear.points,bonus);
}

/*
    RENDER THREAD. Takes whatever the simulation scored since the last frame, gives each one a
    slot, and ages the ones already up.

    `tick` is the simulation tick, not a frame count, and that is what makes a popup last the
    same length of time on any machine - and makes it freeze, rather than race away, when the
    game is paused or single-stepped. It is the same reason the camera shake is a tick counter.
*/
void ApplicationTetris::UpdatePopups(uint64_t tick){
    std::vector<TetrisClearEvent> fresh;
    uint64_t flush_tick = 0;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        fresh.swap(pending_clears);
        flush_tick = popup_flush_tick;
    }

    for (size_t i = 0; i < fresh.size(); i++){
        /*
            A new announcement RETIRES the ones already up.

            They are all centred on the same column over the well, and a clear arrives about
            every 50 ticks while a popup lives for 130 - so left to overlap they print straight
            through each other and neither can be read. Which is the point at which the feature
            stops telling the player anything, which was the whole reason for it.

            Overlapping them was the first version and it looked like a fault. The pool stays at
            three slots even so: the cost is three hidden objects, and a spawn that has to find a
            free slot cannot be the thing that drops an announcement on a frame that ran long.
        */
        int slot = 0;
        for (int s = 0; s < TETRIS_POPUP_SLOTS; s++){
            //Only its OWN kind, or a level up would erase the tetris that earned it - the two
            //arrive on the same tick. See TETRIS_ANNOUNCE_* in the header.
            if (popups[s].f_active && popups[s].kind == fresh[i].kind){
                popups[s].f_active = false;
                if (popups[s].object){
                    popups[s].object->Hide();
                }
            }
            if (!popups[s].f_active){
                slot = s;
            }
        }
        TetrisPopup& popup = popups[slot];
        popup.kind = fresh[i].kind;
        if (!popup.object){
            continue;
        }

        char text[80];
        FormatClearText(fresh[i],text,sizeof(text));

        TextLayout layout;
        layout.scale = POPUP_TEXT_SCALE;
        layout.align = TEXT_ALIGN_CENTER;
        layout.matid = 0;
        Mesh* mesh = BuildTextMesh(glyphs,text,layout,popup.mesh);
        if (!mesh){
            continue;
        }
        if (!popup.mesh){
            popup.mesh = mesh;
            popup.object->SetMesh(mesh);
        }
        //A tetris, a T-spin, a back-to-back or a perfect clear is the game going well and should
        //look different from a single scraped off the bottom. So is a level up.
        bool f_special = (fresh[i].kind == TETRIS_ANNOUNCE_LEVEL)
                      || (fresh[i].lines == 4)
                      || (fresh[i].spin != TETRIS_SPIN_NONE)
                      || fresh[i].f_back_to_back || fresh[i].f_perfect_clear;
        popup.object->SetMaterialSlot(0,f_special ? material_text_hot : material_text);
        popup.spawn_tick = fresh[i].tick;
        popup.origin_y = fresh[i].world_y;
        popup.f_active = true;
    }

    for (int s = 0; s < TETRIS_POPUP_SLOTS; s++){
        TetrisPopup& popup = popups[s];
        if (!popup.f_active || !popup.object){
            continue;
        }
        //Ticks elapsed. Unsigned, so a restart that rewinds nothing still cannot wrap: the
        //spawn tick is the SCENE's tick, which only ever goes up.
        uint64_t age = (tick > popup.spawn_tick) ? (tick - popup.spawn_tick) : 0;
        //Older than the current game, or simply expired - either way it comes down.
        if (age >= TETRIS_POPUP_TICKS || popup.spawn_tick < flush_tick){
            popup.object->Hide();
            popup.f_active = false;
            continue;
        }
        float t = (float)age / (float)TETRIS_POPUP_TICKS;
        popup.object->SetPosition(vec3(POPUP_X,popup.origin_y + TETRIS_POPUP_RISE * t,POPUP_Z));

        /*
            Scale stands in for a fade. The material is shared with every other label, so turning
            one popup transparent would turn all of them transparent - and a per-popup material
            would be a material per slot for the sake of two seconds of alpha. Snapping up on the
            way in and collapsing to nothing on the way out reads as the same thing and costs a
            transform.
        */
        float scale = 1.0f;
        if (age < TETRIS_POPUP_GROW_TICKS){
            scale = 0.55f + 0.45f * ((float)age / (float)TETRIS_POPUP_GROW_TICKS);
        }else if (age > TETRIS_POPUP_TICKS - TETRIS_POPUP_SHRINK_TICKS){
            uint64_t left = TETRIS_POPUP_TICKS - age;
            scale = (float)left / (float)TETRIS_POPUP_SHRINK_TICKS;
        }
        popup.object->SetScale(vec3(scale));
        popup.object->Show();
    }
}

void ApplicationTetris::SetLabelText(int label_id, const char* text){
    if (label_id < 0 || label_id >= TETRIS_LABEL_COUNT || !text){
        return;
    }
    TetrisLabel& label = labels[label_id];
    if (!label.object){
        return;
    }
    //Most frames change nothing. Comparing the string is cheaper than rebuilding a mesh by a
    //wide margin, and it is what keeps this safe to call unconditionally every single frame.
    if (strncmp(label.text,text,sizeof(label.text)) == 0){
        return;
    }
    snprintf(label.text,sizeof(label.text),"%s",text);

    TextLayout layout;
    layout.scale = label.scale;
    layout.align = label.align;
    //Slot 0, which is where this Object's material was assigned. The glyph meshes carry matid 0
    //themselves, but the string is rebuilt from scratch so it has to be restated.
    layout.matid = 0;

    Mesh* mesh = BuildTextMesh(glyphs,label.text,layout,label.mesh);
    if (!mesh){
        //No ink: the empty string, or spaces. Hide rather than draw nothing, and keep the mesh -
        //it still holds the last thing this label said, ready to be overwritten.
        label.object->Hide();
        return;
    }
    if (!label.mesh){
        label.mesh = mesh;
        label.object->SetMesh(mesh);
    }
    label.object->Show();
}

/*
    Frame thread, once per frame, before the scene is drawn and while no lock is held.

    This is where text gets built, because building it ends in glNamedBufferData and the physics
    thread may not touch GL. What crosses the thread boundary is the SNAPSHOT - plain numbers,
    published under snapshot_mutex at the end of every tick - and the strings are formatted here,
    on this side of it. Nothing has to be published as text, and the simulation never waits.
*/
void ApplicationTetris::PreRender(void){
    if (!glyphs.IsValid()){
        return;
    }

    int score = 0;
    int lines = 0;
    int level = 1;
    int combo = -1;
    int best = 0;
    bool f_back_to_back = false;
    bool f_gameover = false;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        score = snapshot.score;
        lines = snapshot.lines;
        level = snapshot.level;
        combo = snapshot.combo;
        best = snapshot.best_score;
        f_back_to_back = snapshot.f_back_to_back;
        f_gameover = (snapshot.phase == TETRIS_PHASE_GAMEOVER);
    }

    char text[64];
    snprintf(text,sizeof(text),"SCORE\n%i",score);
    SetLabelText(TETRIS_LABEL_SCORE,text);
    //While the game in progress IS the best, say so rather than printing the same number twice -
    //"BEST 4200" sitting under "SCORE 4200" reads like a bug, and "NEW BEST" is the better news.
    if (score > 0 && score >= best){
        SetLabelText(TETRIS_LABEL_BEST,"NEW BEST");
    }else{
        snprintf(text,sizeof(text),"BEST %i",best);
        SetLabelText(TETRIS_LABEL_BEST,text);
    }
    snprintf(text,sizeof(text),"LINES %i",lines);
    SetLabelText(TETRIS_LABEL_LINES,text);
    snprintf(text,sizeof(text),"LEVEL %i",level);
    SetLabelText(TETRIS_LABEL_LEVEL,text);

    //The run bonuses, shown only while there is a run. A combo of 0 is the first clear of a run
    //and pays nothing, so it is not worth a line of screen either - Playfield::combo is offset by
    //one precisely so "is there a combo" and "what does it multiply" are the same test.
    text[0] = 0;
    if (combo > 0 && f_back_to_back){
        snprintf(text,sizeof(text),"COMBO x%i B2B",combo);
    }else if (combo > 0){
        snprintf(text,sizeof(text),"COMBO x%i",combo);
    }else if (f_back_to_back){
        snprintf(text,sizeof(text),"B2B READY");
    }
    SetLabelText(TETRIS_LABEL_COMBO,text);

    //Text or no text is the whole of the banner's state.
    SetLabelText(TETRIS_LABEL_GAMEOVER,f_gameover ? "GAME OVER\nPRESS R" : "");

    UpdatePopups(main_scene ? main_scene->GetPhysicsTick() : 0);
}

/*
    A point light over the well, and the occluder field that shadows it.

    The sun and the fill are directional, and a directional light is the one kind the engine could
    already shadow: one depth map rendered from its own viewpoint covers every receiver, because
    every ray is parallel. A point light has no single viewpoint, and the textbook answer - six
    faces of a cube map, per light, per frame - buys omnidirectionality that a board seen from a
    fixed orthographic camera cannot spend. So this light is shadowed by marching the occluder
    field instead: one top-down texture of what is where, built once and reusable by any number of
    lights. See Renderer::RenderFieldPass and CalcFieldShadow in shaders/default.frag.

    Tetris is a good first host for it because the board is already the flat, extruded, top-down
    arrangement the field describes exactly - a stack of 1x1x1 cubes on a plane is a prism field
    and nothing else.
*/
void ApplicationTetris::SetupFieldShadows(){
    //In front of the board and above centre, so the stack throws long shadows down the back panel
    //rather than short ones hidden behind the blocks casting them. A scene object with a name, so
    //it can be dragged in the Inspector or moved with object_set_transform to see the shadows
    //sweep - which is most of how this gets checked.
    lamp = new PointLight();
    lamp->name = "Lamp";
    lamp->SetPosition(vec3(4.5f,10.0f,7.0f));
    lamp->color = vec3(1.0f,0.86f,0.60f);
    //Bright for a point light, because the shader applies a point light's brightness twice - once
    //as the distance falloff and again as radiance - and because it has to hold its own against a
    //sun at 4.5. See the note on shading_brightness in default.frag.
    lamp->brightness = 3.5f;
    lamp->f_casts_shadow = true;
    main_scene->AddObject(lamp);

    /*
        The field's own camera. Orthographic down -Z, because THIS app's world is the XY plane
        with Z up - the engine fixes no up axis and most other apps are the other way round,
        which is why the axis is passed to the renderer explicitly rather than assumed.

        Not the scene camera, tempting as that is: it is already an orthographic top-down camera
        over the same board. UpdateCameraShake moves it on a line clear, and that would drag the
        field's world mapping under the shadows for as long as the shake lasted.

        zoom is a half-extent in world units, and the map is square. 13 around the camera target
        reaches x [-7,19] and y [-3.5,22.5], which covers the well, the previews and the labels
        with room to spare. At 512 texels that is ~20 per world unit, so a 0.92-unit block is
        about 18 texels across - and since the jump flood runs log2(size)+2 passes over every
        texel of this map every frame, doubling the resolution quadruples the only part of this
        that costs anything. 512 is where that stopped being free and the edges still look right.
    */
    field_camera = new Camera();
    field_camera->name = "Field Camera";
    field_camera->SetupOrthographic(512,512,13.0f,0.1f,120.0f);
    field_camera->SetPosition(vec3(camera_target.x,camera_target.y,40.0f));
    field_camera->SetLookAt(vec3(camera_target.x,camera_target.y,0.0f));
    field_camera->CalculateLookatMatrix();

    if (!renderer->EnableFieldShadows(field_camera,vec3(0,0,1),512)){
        debug->Err("Failed to enable field shadows\n");
    }
}

void ApplicationTetris::SetupCamera(){
    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_ORTHOGRAPHIC);
    //zoom is the VERTICAL half-extent in world units; the horizontal follows from the aspect
    //ratio. 11.5 shows the 20-row well plus a margin above and below.
    camera->SetupOrthographic(renderer->width,renderer->height,11.5f,0.1f,120.0f);
    camera->SetPosition(vec3(camera_target.x,camera_target.y,40.0f));
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();
}

void ApplicationTetris::SetupInput(){
    InputController* input = main_scene->inputcontroller;

    /*
        EVERY MAPPING IN THIS BLOCK IS A WIN32 VIRTUAL-KEY CODE, so it is guarded rather than
        assumed. The Android port of this same file builds against a device with no keyboard to
        press them on, and VK_LEFT and friends do not exist there at all.

        Kept verbatim inside the guard rather than split into a platform file: this app's file is
        shared with that port more or less line for line, and re-syncing it should stay a plain
        copy plus this one #if. The gamepad and touch bindings below are deliberately OUTSIDE it -
        they are portable as written.
    */
#if defined(_WIN32)
    //Two mappings for most actions, because muscle memory differs: arrows are the classic
    //arcade layout and Z/X/C the guideline one. KeyState::f_isdown counts HELD MAPPINGS rather
    //than being a boolean, so an action stays down while either of its keys is.
    input->AddKeyMap(VK_LEFT,INPUT_TETRIS_LEFT);
    input->AddKeyMap(VK_RIGHT,INPUT_TETRIS_RIGHT);
    input->AddKeyMap(VK_DOWN,INPUT_TETRIS_SOFT_DROP);
    input->AddKeyMap(VK_UP,INPUT_TETRIS_ROTATE_CW);
    input->AddKeyMap('X',INPUT_TETRIS_ROTATE_CW);
    input->AddKeyMap('Z',INPUT_TETRIS_ROTATE_CCW);
    input->AddKeyMap(VK_CONTROL,INPUT_TETRIS_ROTATE_CCW);
    input->AddKeyMap(VK_SPACE,INPUT_TETRIS_HARD_DROP);
    input->AddKeyMap('C',INPUT_TETRIS_HOLD);
    input->AddKeyMap(VK_SHIFT,INPUT_TETRIS_HOLD);
    input->AddKeyMap('R',INPUT_TETRIS_RESTART);
    input->AddKeyMap(VK_F1,INPUT_TETRIS_TOGGLE_UI);
    input->AddKeyMap('M',INPUT_TETRIS_MUTE);
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::UpdatePhysics itself, so this is the whole feature.
    input->AddKeyMap('P',INPUT_PAUSE);
#endif //_WIN32

    /*
        GAMEPAD. Outside the _WIN32 guard above on purpose: GAMEPAD_KEY_* carry their own XInput
        bit values on platforms with no <xinput.h> (see core/InputController.h), so this block is
        portable as written and the port gets the same layout from the same lines.

        --- THE LAYOUT, AND WHY ------------------------------------------------------------------
        Left thumb moves, right thumb acts, index fingers hold. That split is the whole design:
        moving and rotating happen at the same time constantly in this game, so they must not
        share a digit.

            D-pad left/right    move            the DAS/ARR path, same as the arrow keys
            D-pad down          soft drop
            D-pad up            ROTATE CW       matching VK_UP on the keyboard, NOT hard drop
            A                   rotate CW       the button the thumb already rests on
            B / X               rotate CCW      two of them, mirroring Z and Ctrl
            Y                   hard drop       deliberately the far corner - see below
            LB / RB             hold            either shoulder, mirroring C and Shift
            Start               pause
            Back                restart

        D-PAD UP IS ROTATE, NOT HARD DROP, which is worth stating because most console Tetris puts
        hard drop there. This app's keyboard maps VK_UP to rotate, and a player who moves between
        the two would otherwise lose a piece to the difference - an irreversible move is the wrong
        place to be clever. For the same reason hard drop is the one action with a single binding,
        in the corner furthest from the thumb's resting position: every other action here is
        recoverable, and that one is not.

        Start is INPUT_PAUSE, the ENGINE's action - the same one 'P' is bound to - so this needs no
        handling anywhere in this app.
    */
    input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,     INPUT_TETRIS_LEFT);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_RIGHT,    INPUT_TETRIS_RIGHT);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_DOWN,     INPUT_TETRIS_SOFT_DROP);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_UP,       INPUT_TETRIS_ROTATE_CW);
    input->AddKeyMap(GAMEPAD_KEY_A,             INPUT_TETRIS_ROTATE_CW);
    input->AddKeyMap(GAMEPAD_KEY_B,             INPUT_TETRIS_ROTATE_CCW);
    input->AddKeyMap(GAMEPAD_KEY_X,             INPUT_TETRIS_ROTATE_CCW);
    input->AddKeyMap(GAMEPAD_KEY_Y,             INPUT_TETRIS_HARD_DROP);
    input->AddKeyMap(GAMEPAD_KEY_LEFT_SHOULDER, INPUT_TETRIS_HOLD);
    input->AddKeyMap(GAMEPAD_KEY_RIGHT_SHOULDER,INPUT_TETRIS_HOLD);
    input->AddKeyMap(GAMEPAD_KEY_START,         INPUT_PAUSE);
    input->AddKeyMap(GAMEPAD_KEY_BACK,          INPUT_TETRIS_RESTART);

    /*
        The left stick does what the D-pad does, for the thumb that reached for it instead. Bound
        to its own axis actions and thresholded in GatherInput - see INPUT_TETRIS_STICK_X in the
        header for why binding a stick straight onto INPUT_TETRIS_LEFT cannot work.

        7849 is XInput's own XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, spelled out rather than included
        because this block compiles on the port too. It is about 24%, which is larger than the 18%
        apps/breakout steers a paddle with, and deliberately so: a paddle wants the small movements
        that a dead zone eats, and a Tetris board has ten columns and no use for them at all.

        Stick UP is left unbound. It is the natural place for hard drop and that is exactly the
        problem - a thumb rolling from left to right across the top of the gate would drop a piece
        on the way past. See the note on D-pad up above; same reasoning, stronger here.
    */
    input->AddGamePadMap(0,INPUT_TETRIS_STICK_X,7849);
    input->AddGamePadMap(1,INPUT_TETRIS_STICK_Y,7849);

    /*
        Everything from here to the end of SetupInput is the ON-SCREEN TOUCH UI, which is
        Android-only - see USE_TOUCH_UI in core/Application.h, which is the single line that
        turns it on for a desktop build when you want to look at the layout.

        The BINDING is inside the guard and not just the drawing, on purpose: AddTouchButton is
        what makes a rect live, so buttons bound but not drawn would sit invisible in the corners
        of a desktop window quietly eating clicks.
    */
#if 0
    /*
        On-screen buttons - the third input family, and the whole test of whether the seam was put
        in the right place: this block is the ONLY app code that changes for them. GatherInput,
        DAS/ARR, the HUD, the MCP tools and the snapshot are all untouched, because everything
        downstream of a KeyState stopped caring where input came from.

        Driven by the mouse as pointer 0 on Windows (InputController::HandleMessage) and by real
        fingers on Android. Nothing here knows which.

        LAID OUT IN PIXELS, which is a known placeholder. The port sizes these in MILLIMETRES via
        GetDisplayDPI - 11 mm buttons, 2 mm gaps, 4 mm inset - because a fraction-of-the-screen
        button is a pinhead on a phone and a dinner plate on a tablet simultaneously. That needs
        backlog item 70, which does not exist on win32 yet; when it does, only these numbers
        change.
    */
    //BOUND here, POSITIONED in LayoutTouchButtons. The rect passed now is discarded: the window
    //is still 1280x800 at this point and becomes 1200x900 a few lines further down Init, so any
    //geometry computed here would be wrong by 80 pixels and the right-hand cluster would hang off
    //the edge. See Application::LayoutTouchButtons.
    InputController::TouchRect later = {};

    /*
        MOVE LEFT IS ON THE LEFT AND MOVE RIGHT IS ON THE RIGHT, one under each thumb, rather than
        both directions crammed under one of them. Holding a tablet in landscape, the direction you
        press should be the side you press - and DAS means a direction is HELD, so the two are
        never wanted by the same thumb at the same time anyway.

        The rotations split the same way and end up as the inner button of each cluster, which
        leaves each thumb with one direction and one rotation.
    */
    touch_left      = input->AddTouchButton(later,INPUT_TETRIS_LEFT,      "<");
    touch_softdrop  = input->AddTouchButton(later,INPUT_TETRIS_SOFT_DROP, "v");
    touch_ccw       = input->AddTouchButton(later,INPUT_TETRIS_ROTATE_CCW,"CCW");

    touch_harddrop  = input->AddTouchButton(later,INPUT_TETRIS_HARD_DROP, "DROP");
    touch_cw        = input->AddTouchButton(later,INPUT_TETRIS_ROTATE_CW, "CW");
    touch_right     = input->AddTouchButton(later,INPUT_TETRIS_RIGHT,     ">");

    /*
        Chrome, top-right. All three work WHILE PAUSED, which is the whole reason they are handled
        where they are rather than in the gameplay tick:

          - New game submits TETRIS_CMD_RESTART, and Scene::BeginPass drains the command queue
            before it decides whether to tick.
          - Pause is INPUT_PAUSE, the ENGINE's action - the same one 'P' is mapped to - which
            BeginPass services itself on every pass. There is no app code for it at all, which is
            the point: a pause handled in RunSimulationTick would be a one-way trip, because the
            tick that would read the release never runs while paused.
          - Mute is read in UpdateView, which runs on ticking and non-ticking passes alike.
    */
    touch_newgame   = input->AddTouchButton(later,INPUT_TETRIS_RESTART,   "NEW");
    touch_pause     = input->AddTouchButton(later,INPUT_PAUSE,            "II");
    touch_mute      = input->AddTouchButton(later,INPUT_TETRIS_MUTE,      "MUTE");

    /*
        The device's own media volume, under MUTE.

        Bound only when the platform actually HAS the control, so the win32 build (where
        GetMaxSystemVolume returns -1) gets no buttons rather than two dead ones. That check
        belongs here rather than in LayoutTouchButtons, because AddTouchButton is what allocates
        the keycode - a button bound and then never positioned would still be pressable at (0,0).

        It earns its place on a touch device specifically: the Android reference tablet idles with
        its music stream muted at zero, `media volume` does not exist before API 26, and VOLUME_UP
        key events only act on the ACTIVE stream, so from an idle app they do nothing. Without
        this the game is silent there and there is no way from inside it to find out why.
    */
    system_volume_max = GetMaxSystemVolume();
    if (system_volume_max > 0){
        touch_vol_down = input->AddTouchButton(later,INPUT_TETRIS_VOL_DOWN,"V-");
        touch_vol_up   = input->AddTouchButton(later,INPUT_TETRIS_VOL_UP,  "V+");
        RefreshSystemVolume();
    }
#endif //USE_TOUCH_UI
}

//Reads the device volume back. See system_volume in the header for why this is cached rather
//than called from the draw.
void ApplicationTetris::RefreshSystemVolume(){
    if (system_volume_max <= 0){
        return;
    }
    system_volume = GetSystemVolume();
}

/*
    The volume readout, under its two buttons.

    On the overlay rather than as a 3D TextMesh like SCORE/LINES/LEVEL, deliberately: those are
    positioned in WORLD space and so move relative to the screen as the aspect ratio changes.
    This label has to stay under two SCREEN-SPACE buttons, so it is drawn in screen space too,
    and it reads its position back out of the button rather than recomputing the layout.

    Shows the step and the top of the scale ("VOL 12/15") because the scale is per-device - 15 on
    the Android reference tablet, but nothing may assume that - so a bare number would not say
    how loud 12 actually is.

    Render thread, with `overlay` already Begin()'d at the window size - see the DrawOverlay
    contract in core/Application.h.
*/
void ApplicationTetris::DrawOverlay(){
    if ((touch_vol_up < 0) || (system_volume < 0) || !overlay || !overlay->IsReady()){
        return;
    }
    InputController* input = main_scene ? main_scene->inputcontroller : NULL;
    if (!input || (touch_vol_up >= input->GetNumTouchButtons())){
        return;
    }
    const InputController::TouchRect& r = input->GetTouchButtons()[touch_vol_up].rect;

    char text[32];
    snprintf(text,sizeof(text),"VOL %i/%i",system_volume,system_volume_max);

    const float size = r.h * 0.34f;
    //Right edge of the button row, so the readout and the buttons share one margin.
    const vec2 at = vec2(r.x + r.w,r.y + r.h + size * 1.25f);
    overlay->AddText(text,at,size,UIColor(200,215,240,200),UI_ALIGN_RIGHT);
}

/*
    Where the on-screen buttons actually go. Render thread, before the first frame and again on
    every resize - so this is the only place that knows the window size, and it cannot be run too
    early the way SetupInput can.

    LAID OUT IN PIXELS, which is a known placeholder. The Android port sizes these in MILLIMETRES
    via GetDisplayDPI - 11 mm buttons, 2 mm gaps, 4 mm inset - because a button measured as a
    fraction of the screen is a pinhead on a phone and a dinner plate on a tablet at the same time.
    That needs backlog item 70, which has no win32 implementation yet; when it does, only the three
    constants below change.
*/
void ApplicationTetris::LayoutTouchButtons(int w, int h){
    InputController* input = main_scene->inputcontroller;

    const float s     = 88.0f;      //a comfortable thumb target
    const float g     = 12.0f;      //gap
    const float m     = 28.0f;      //inset from the window edge
    const float small = 64.0f;      //chrome, smaller on purpose - see below

    const float left  = 0.0f;
    const float top   = 0.0f;
    const float right = (float)w;
    const float row   = (float)h - m - s;

    /*
        ANCHORED TO CORNERS, not laid out from one origin. The well and the HUD both live in the
        middle, so the two thumb clusters go in the bottom corners, where a hand holding a tablet
        in landscape actually is - and anchoring each cluster to its own edge is what keeps that
        true at every window size.
    */

    //Left thumb, built rightwards from the left edge.
    {
        float x = left + m;
        input->SetTouchButtonRect(touch_left,     {x,row,s,s});  x += s + g;
        input->SetTouchButtonRect(touch_softdrop, {x,row,s,s});  x += s + g;
        input->SetTouchButtonRect(touch_ccw,      {x,row,s,s});
    }

    /*
        Right thumb, MIRRORED: built leftwards from the right edge, so ">" is the outermost button
        on the right exactly as "<" is the outermost on the left.

        DROP SITS ABOVE ">" RATHER THAN BESIDE IT, in a second row. Hard drop is the one
        irreversible action on the pad - the piece locks where it lands and there is no taking it
        back - and while it was the third button in this row it was immediately next to CW, which
        is pressed constantly and in a hurry. Moving it to its own row costs a deliberate reach
        upwards, which is the right price for it, and it no longer shares an edge with anything
        that gets pressed by reflex.

        Above the OUTERMOST button specifically: that is where the thumb already rests, so the
        reach is short even though it is deliberate.
    */
    {
        float x = right - m - s;
        input->SetTouchButtonRect(touch_right,    {x,row,s,s});
        input->SetTouchButtonRect(touch_harddrop, {x,row - s - g,s,s});
        x -= s + g;
        input->SetTouchButtonRect(touch_cw,       {x,row,s,s});
    }

    /*
        Top-right corner: new game, pause, mute. SMALLER on purpose - these are not played with,
        and a fat target up there would be in the way of the board. Built leftwards from the right
        edge like the thumb cluster below it, so the two corners stay visually aligned.
    */
    {
        const float y = top + m;
        float x = right - m - small;
        input->SetTouchButtonRect(touch_mute,    {x,y,small,small});  x -= small + g;
        input->SetTouchButtonRect(touch_pause,   {x,y,small,small});  x -= small + g;
        input->SetTouchButtonRect(touch_newgame, {x,y,small,small});
    }

    /*
        Device volume, in a second row directly under MUTE.

        Right-aligned like the row above it so the two corners stay one block, and the readout
        (DrawOverlay) sits under these again. Skipped entirely when the platform has no volume
        control - the indices are -1 then and SetTouchButtonRect ignores those, but not binding
        them in the first place is what actually keeps them off the screen.
    */
    if (touch_vol_up >= 0){
        const float y = top + m + small + g;
        float x = right - m - small;
        input->SetTouchButtonRect(touch_vol_up,   {x,y,small,small});  x -= small + g;
        input->SetTouchButtonRect(touch_vol_down, {x,y,small,small});
    }
}


void ApplicationTetris::RegisterCommandHandlers(){
    //Restarting is intent from outside the simulation - the HUD button, an MCP call, later a
    //replay - so it goes on the command queue rather than being done on the caller's thread. The
    //handler runs on the physics thread at the top of a tick with physics_mutex held, which is
    //the only place it is safe to throw the board away.
    main_scene->RegisterCommandHandler(TETRIS_CMD_RESTART,
        [this](const SimCommand& cmd) -> objectid_t {
            //The seed travels IN the command, so a recorded restart deals the same pieces on
            //replay - the same reason ApplicationTank's vehicle reset carries its pose.
            uint32_t seed = (uint32_t)cmd.value[0];
            NewGame(seed ? seed : next_auto_seed++);
            return OBJECTID_INVALID;
        });
}

void ApplicationTetris::NewGame(uint32_t seed){
    //Before the reset, because Playfield::NewGame is about to set `score` back to zero and this
    //is the last moment the game that just ended still exists. `best_score` deliberately survives
    //it - it is the one number a restart must not throw away.
    SaveBestScore();

    current_seed = seed;
    game.NewGame(seed);
    //The walls go back to calm on their own next tick, but the state has to be forgotten here or
    //a game that ended in the red would start the next one in the red too.
    wall_state = -1;
    //Throw away everything the previous game left in the world.
    for (size_t i = 0; i < debris.size(); i++){
        if (debris[i].object){
            debris[i].object->Destroy();
        }
    }
    debris.clear();
    main_scene->DeleteDestroyedObjects();
    //The popups belong to the game that scored them. Emptying the queue here (rather than
    //letting the last game's "TETRIS +3200" float over an empty board) is the whole of it; the
    //slots themselves are hidden by the frame, which is the only thread allowed to touch them.
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        pending_clears.clear();
        popup_flush_tick = main_scene->GetPhysicsTick();
        snapshot.last_clear_lines = 0;
        snapshot.last_clear_points = 0;
    }
    f_collapse_animating = false;
    last_piece_type = -1;
    das_direction = 0;
    das_ticks_left = 0;
    arr_ticks_left = 0;
    shake_amount = 0.0f;
    shake_ticks = 0;
    SyncBoardView();
    SyncPieceView();
    SyncPreviewView();
    PublishSnapshot();
    debug->Ok("New game, seed %u\n",seed);
}

//--- The tick -------------------------------------------------------------------------------

//Chrome only. This used to sit inside the gameplay hook behind the pause predicate, which meant
//the UI toggle did nothing while the game was paused - exactly when you most want to open a panel.
//It is not gameplay by its own description, so it moved here and now works whether or not the
//simulation is running.
void ApplicationTetris::UpdateView(void){
    if (!main_scene){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    if (input->WasKeyReleased(INPUT_TETRIS_TOGGLE_UI)){
        f_show_engine_ui = !f_show_engine_ui;
        f_show_scene_window = f_show_engine_ui;
        f_show_inspector_window = f_show_engine_ui;
        f_show_engine_window = f_show_engine_ui;
    }

    if (input->WasKeyReleased(INPUT_TETRIS_MUTE)){
        f_sound_enabled = !f_sound_enabled;
    }

    /*
        Device volume, one step per press.

        On the PRESS edge, unlike the chrome buttons around it. The others are one-shot state
        flips where press-then-slide-off is a useful free cancel; this is a repeated adjustment
        where you look at the readout and press again, and a release-edge control makes that feel
        like the number is lagging your finger.

        Re-read after setting rather than assuming the write landed: the platform can refuse or
        clamp it (a fixed-volume device, a policy restriction), and a readout showing what we
        ASKED for rather than what happened would be worse than no readout at all.
    */
    if (system_volume_max > 0){
        int wanted = system_volume;
        if (input->WasKeyPressed(INPUT_TETRIS_VOL_DOWN)){ wanted--; }
        if (input->WasKeyPressed(INPUT_TETRIS_VOL_UP)){   wanted++; }
        if (wanted != system_volume){
            SetSystemVolume(wanted);
            RefreshSystemVolume();
        } else if (--volume_poll_countdown <= 0){
            //Once a second, so a change made outside the app (hardware keys, the system UI) is
            //reflected rather than sitting stale until the next press. In PASSES rather than
            //ticks on purpose - UpdateView runs on non-ticking passes too, and this is chrome,
            //not simulation, so it must keep refreshing while the game is paused.
            volume_poll_countdown = 60;
            RefreshSystemVolume();
        }
    }

    /*
        New game. HERE rather than in RunSimulationTick, and as a COMMAND rather than a direct
        NewGame() call, and both halves of that are for the same reason: so it works while paused.

        It used to sit in the gameplay tick, which does not run while the simulation is paused - so
        the one moment a player is most likely to reach for "new game" was the one moment it did
        nothing. UpdateView runs on every pass, and Scene::BeginPass drains the command queue
        BEFORE it decides whether to tick, so the restart lands either way.

        This is also exactly what the ImGui "New game" button already did, so there is now one path
        rather than two that could drift.
    */
    if (input->WasKeyReleased(INPUT_TETRIS_RESTART)){
        SimCommand cmd;
        cmd.type = TETRIS_CMD_RESTART;
        cmd.value[0] = 0.0f;   //0 means "pick a fresh seed"
        SubmitUICommand(cmd);
    }
}

/*
    The game. This function used to open with a hand-rolled copy of Scene::UpdatePhysics's pause
    predicate, because the old RunLogic ran on every pass of the physics loop rather than once per
    simulated tick, and unguarded gameplay would have kept playing through a pause and advanced by
    an unknown number of ticks under single-stepping. That guard is gone: the engine now makes that
    decision once per pass in Scene::BeginPass and only calls this hook on the passes that tick.
*/
void ApplicationTetris::RunSimulationTick(void){
    if (!main_scene){
        return;
    }

    InputController* input = main_scene->inputcontroller;

    //Restart moved to UpdateView - see the note there. It is chrome by behaviour even though it is
    //a game action by subject: it has to work while paused, and this hook does not run then.

    TetrisInput actions;
    GatherInput(actions);

    TetrisEvents events;
    game.Tick(actions,events);
    HandleEvents(events);

    /*
        The best, tracked live rather than at the end. A player who beats their record and then
        tops out still beat it, and a record that only counted finished games would be a record
        of how people die rather than of how well they played.

        Flushed on the two moments that end a game and, failing those, every ten seconds - a soft
        drop scores a point a cell, so `f_best_dirty` is set several times a second and writing on
        every change would be a file write per tick for a number nobody is reading.
    */
    if (game.score > game.best_score){
        game.best_score = game.score;
        f_best_dirty = true;
    }
    if (events.f_game_over || (main_scene->GetPhysicsTick() % 600) == 0){
        SaveBestScore();
    }

    SyncBoardView();
    SyncPieceView();
    SyncPreviewView();
    UpdateDebris();
    UpdateDangerState();
    UpdateCameraShake();
    PublishSnapshot();
}

void ApplicationTetris::GatherInput(TetrisInput& out){
    InputController* input = main_scene->inputcontroller;

    //Keys meant for another application must not drive the game, but a scripted hold is not OS
    //input and has to come through - otherwise every MCP-driven test does nothing, which is
    //exactly when the window is not in front. See InputController::IsInputLive.
    if (!input->IsInputLive()){
        das_direction = 0;
        das_ticks_left = 0;
        arr_ticks_left = 0;
        return;
    }

    /*
        Edge-triggered actions: one press, one action, no matter how long the key is held.

        The PRESS edge, not the release edge. These four read WasKeyReleased until 2026-09-13,
        for no better reason than that it was the only edge the engine had (backlog item 69).
        On a keyboard the difference is invisible, which is how it survived; it stops being
        invisible the moment a finger is involved, because a hard drop that lands when you LIFT
        reads as the game lagging. The UI toggles below keep the release edge deliberately -
        that is how a button behaves, and it lets a mis-press be taken back by sliding off.
    */
    out.f_rotate_cw  = input->WasKeyPressed(INPUT_TETRIS_ROTATE_CW);
    out.f_rotate_ccw = input->WasKeyPressed(INPUT_TETRIS_ROTATE_CCW);
    out.f_hard_drop  = input->WasKeyPressed(INPUT_TETRIS_HARD_DROP);
    out.f_hold       = input->WasKeyPressed(INPUT_TETRIS_HOLD);

    /*
        The left stick, folded into the same booleans the keyboard and the D-pad produce, here and
        nowhere else. Everything downstream - DAS/ARR, the snapshot, the MCP tools, a replay -
        stays unable to tell which device moved the piece, which is the only reason the stick needs
        no repeat logic of its own.
    */
    float stick_x = input->GetAxis(INPUT_TETRIS_STICK_X);
    float stick_y = input->GetAxis(INPUT_TETRIS_STICK_Y);

    //Level-triggered: soft drop is "gravity is fast while this is down". XInput reports Y positive
    //UP, so pulling the stick towards you is the negative half.
    out.f_soft_drop = input->IsKeyDown(INPUT_TETRIS_SOFT_DROP) || (stick_y < -TETRIS_STICK_THRESHOLD);

    /*
        DAS/ARR. A held left or right moves once immediately, then pauses for TETRIS_DAS_TICKS,
        then repeats every TETRIS_ARR_TICKS. Both are tick counts, so the feel of the controls is
        identical whether the simulation is running freely, single-stepped, or replayed - which
        would not be true of a millisecond timer, and is the single most important reason this
        translation lives on the physics thread rather than in the window message handler.
    */
    bool f_left = input->IsKeyDown(INPUT_TETRIS_LEFT) || (stick_x < -TETRIS_STICK_THRESHOLD);
    bool f_right = input->IsKeyDown(INPUT_TETRIS_RIGHT) || (stick_x > TETRIS_STICK_THRESHOLD);
    //Both down: the most recent one wins, which here means "keep doing what we were doing".
    int direction = 0;
    if (f_left && !f_right){
        direction = -1;
    }else if (f_right && !f_left){
        direction = +1;
    }else if (f_left && f_right){
        direction = das_direction;
    }

    if (direction == 0){
        das_direction = 0;
        das_ticks_left = 0;
        arr_ticks_left = 0;
        return;
    }
    if (direction != das_direction){
        //A fresh press: move at once, then wait out the DAS delay before repeating.
        das_direction = direction;
        das_ticks_left = TETRIS_DAS_TICKS;
        arr_ticks_left = 0;
        if (direction < 0){ out.f_move_left = true; }else{ out.f_move_right = true; }
        return;
    }
    if (das_ticks_left > 0){
        das_ticks_left--;
        return;
    }
    if (arr_ticks_left > 0){
        arr_ticks_left--;
        return;
    }
    arr_ticks_left = TETRIS_ARR_TICKS;
    if (direction < 0){ out.f_move_left = true; }else{ out.f_move_right = true; }
}

void ApplicationTetris::HandleEvents(const TetrisEvents& events){
    if (f_sound_enabled && soundsystem){
        //One sound per event, and each on its own handle: SoundSystem gives a handle exactly one
        //OpenAL source, so two events sharing a handle would cut each other off.
        if (events.f_moved){        soundsystem->Play("move",false,0.35f); }
        if (events.f_rotated){      soundsystem->Play("rotate",false,0.35f); }
        if (events.f_held){         soundsystem->Play("hold",false,0.5f); }
        if (events.f_locked){       soundsystem->Play("lock",false,0.6f); }
        if (events.lines_cleared){  soundsystem->Play("clear",false,0.9f); }
        if (events.f_game_over){    soundsystem->Play("gameover",false,1.0f); }
    }

    if (events.f_hard_dropped && events.hard_drop_cells > 0){
        //A slam should be felt. Scaled by how far it fell, capped so a full-height drop does not
        //throw the camera off the board.
        shake_amount = min(0.06f * events.hard_drop_cells,0.35f);
        shake_ticks = 10;
    }
    if (events.lines_cleared > 0){
        shake_amount = max(shake_amount,0.08f * events.lines_cleared);
        shake_ticks = max(shake_ticks,14);
        SpawnClearDebris();
    }

    /*
        Anything that scored gets announced, and that includes a clear of NO rows: a T-spin with
        nothing under it is worth 400, which beats a triple, and it is the one score in this game
        that happens with no visible consequence on the board at all. If it is not said out loud
        it is invisible.
    */
    if (events.score_awarded > 0 && (events.lines_cleared > 0 || events.spin != TETRIS_SPIN_NONE)){
        /*
            Hand the frame something to announce. This is the answer to "what was that worth" -
            a score that only ever appears as a bigger total teaches the player nothing, and
            without it there is no way to find out that a tetris pays eight times a single, that
            a combo pays at all, or that emptying the board is worth going for.

            The rules worked the number out (Playfield::AwardLineScore); this only carries it
            across the thread boundary, with the row it happened on so the popup starts there.
        */
        float row_sum = 0.0f;
        for (size_t i = 0; i < game.clearing_rows.size(); i++){
            row_sum += (float)game.clearing_rows[i];
        }
        TetrisClearEvent clear;
        clear.kind = TETRIS_ANNOUNCE_CLEAR;
        clear.lines = events.lines_cleared;
        clear.points = events.score_awarded;
        clear.combo = events.combo;
        clear.spin = events.spin;
        clear.f_back_to_back = events.f_back_to_back;
        clear.f_perfect_clear = events.f_perfect_clear;
        //Over the rows that went. A spin that cleared nothing has no such rows, so it is
        //announced over the piece that did it - which is where the player is looking.
        clear.world_y = game.clearing_rows.empty()
                      ? ((float)game.piece_y + 2.0f)
                      : ((row_sum / (float)game.clearing_rows.size()) + 1.0f);
        clear.tick = main_scene->GetPhysicsTick();
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex);
            snapshot.last_clear_lines = clear.lines;
            snapshot.last_clear_points = clear.points;
            snapshot.last_clear_spin = clear.spin;
            pending_clears.push_back(clear);
        }
    }

    /*
        The level up, which until now was an event nothing read.

        Its own announcement kind, high over the well rather than down where the clear is, for
        two reasons: it arrives on the SAME TICK as the clear that caused it, and what it says -
        that gravity just got faster - is the one thing in this game that changes how the next
        piece behaves. A player who is not told is simply surprised by it.
    */
    if (events.f_level_up){
        TetrisClearEvent up;
        up.kind = TETRIS_ANNOUNCE_LEVEL;
        up.level = game.level;
        up.world_y = 15.0f;
        up.tick = main_scene->GetPhysicsTick();
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        pending_clears.push_back(up);
    }
    if (events.lines_cleared > 0 || events.f_locked){
        //A new piece is coming, so the next slide must not be interpolated from the old one.
        last_piece_type = -1;
    }
    if (events.f_game_over){
        debug->Info("Game over: score %i, %i lines, level %i\n",game.score,game.lines,game.level);
    }
}

//--- The view -------------------------------------------------------------------------------

void ApplicationTetris::SyncBoardView(){
    /*
        The board array is the truth and these cubes are a view of it, rebuilt from scratch every
        tick. That is 200 cheap assignments and it removes a whole class of bug: there is no
        incremental update to get wrong, so the display can never disagree with the rules.

        The exception is the collapse animation, which drives the same objects through
        Scene::MoveObjectOverTicks. While that is running the array has not changed yet (the rows
        are removed when the animation finishes), so syncing would fight the motion.
    */
    if (f_collapse_animating){
        if (game.phase == TETRIS_PHASE_COLLAPSING){
            return;
        }
        //The animation is over; the loop below puts every cube back on its grid position.
        f_collapse_animating = false;
    }
    if (game.phase == TETRIS_PHASE_COLLAPSING && !f_collapse_animating){
        StartCollapseAnimation();
        return;
    }

    //Is this row one of the ones flashing? Small enough a linear scan beats anything cleverer.
    for (int y = 0; y < TETRIS_BOARD_H; y++){
        bool f_flashing = false;
        if (game.phase == TETRIS_PHASE_CLEARING){
            for (size_t i = 0; i < game.clearing_rows.size(); i++){
                if (game.clearing_rows[i] == y){
                    f_flashing = true;
                    break;
                }
            }
        }
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            Object* cell = cell_objects[y][x];
            if (!cell){
                continue;
            }
            /*
                POSITION is re-asserted here, not just visibility and colour, and that is not
                belt-and-braces - it is the fix for a real bug.

                The collapse animation drives these same cubes through MoveObjectOverTicks, and a
                motion outlives its own `ticks` by one tick: Scene::AdvanceObjectMotions checks
                `ticks_done >= ticks_total` at the TOP of the loop, so the call that lands the
                object exactly on its target comes AFTER the last interpolating one. If that final
                call happens after this function has already decided the animation is over, it
                writes the collapse target back over the grid position - and since nothing else
                ever sets a cell's position again, that cube stays parked one row low for the rest
                of the run, drawn on top of its neighbour. Dropping a piece into that region then
                looks like the piece loses blocks a tick after it lands.

                Re-asserting from the array every tick makes the view genuinely a function of the
                board, which is what the comment at the top of this function claims. Guarded by a
                compare so the usual case does not dirty 200 transform matrices a tick.
            */
            vec3 home = CellWorldPosition(x,y);
            if ((cell->GetPosition() - home).length() > 0.001f){
                cell->SetPosition(home);
            }
            int8_t type = game.board[y][x];
            if (type < 0){
                cell->Hide();
                continue;
            }
            cell->Show();
            cell->SetMaterialSlot(0,f_flashing ? material_flash : material_piece[type]);
        }
    }
}

void ApplicationTetris::StartCollapseAnimation(){
    /*
        The cleared rows vanish and everything above them slides down by however many rows were
        cleared beneath it. Scene::MoveObjectOverTicks does the whole thing in one call per cube:
        it interpolates over exactly N TICKS, so the animation length is simulation time and a
        single-stepped or replayed run shows the identical motion. This is the nicest API in the
        engine for a game and it is worth saying so.
    */
    f_collapse_animating = true;
    for (int y = 0; y < TETRIS_BOARD_H; y++){
        int drop = 0;
        bool f_cleared = false;
        for (size_t i = 0; i < game.clearing_rows.size(); i++){
            if (game.clearing_rows[i] == y){
                f_cleared = true;
            }else if (game.clearing_rows[i] < y){
                drop++;
            }
        }
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            Object* cell = cell_objects[y][x];
            if (!cell){
                continue;
            }
            if (f_cleared){
                //Its debris is already falling out of the well - the cube itself just goes.
                cell->Hide();
                continue;
            }
            if (game.board[y][x] < 0 || drop == 0){
                continue;
            }
            vec3 target = CellWorldPosition(x,y - drop);
            //The full phase length. This used to ask for one tick less, to dodge an engine bug
            //where a motion of N ticks occupied N+1 calls to Scene::AdvanceObjectMotions and the
            //extra one snapped the cube back onto its target after this phase had already moved it
            //home - see docs/engine_backlog.md item 32. A motion without physics now retires on
            //its last interpolating tick, so N means N and the animation fills the phase exactly.
            main_scene->MoveObjectOverTicks(cell,&target,NULL,TETRIS_COLLAPSE_TICKS);
        }
    }
}

void ApplicationTetris::SyncPieceView(){
    bool f_falling = (game.phase == TETRIS_PHASE_FALLING);
    if (!f_falling){
        for (int i = 0; i < 4; i++){
            if (piece_objects[i]){ piece_objects[i]->Hide(); }
            if (ghost_objects[i]){ ghost_objects[i]->Hide(); }
        }
        return;
    }

    const TetrominoCell* cells = GetTetrominoCells(game.piece_type,game.piece_rotation);
    int ghost_y = game.GetGhostY();
    //A piece that has just spawned, or just been swapped out by hold, must not be interpolated
    //from wherever the last one happened to be - it teleports.
    bool f_snap = (last_piece_type != game.piece_type);

    /*
        The lock delay, made visible.

        A piece that has come to rest still has half a second in which it can be slid and spun
        (TETRIS_LOCK_DELAY_TICKS), and until now the board gave no sign of it at all: a piece
        that looked settled might have most of that grace left or none of it, and the player had
        no way to tell which. Squashing the cubes as the delay runs out says "this is setting" in
        the one place the player is already looking.

        Scale rather than colour, because colour is how a piece says which piece it is - and that
        is the one thing on this board that must never be ambiguous.
    */
    float squash = 1.0f - TETRIS_LOCK_SQUASH * game.GetLockProgress();

    for (int i = 0; i < 4; i++){
        vec3 target = CellWorldPosition(game.piece_x + cells[i].x,game.piece_y + cells[i].y);
        Object* cube = piece_objects[i];
        if (cube){
            cube->Show();
            cube->SetMaterialSlot(0,material_piece[game.piece_type]);
            cube->SetScale(vec3(CELL_VISUAL_SCALE * squash));
            if (f_snap){
                cube->SetPosition(target);
            }else if ((target - piece_cell_targets[i]).length() > 0.01f){
                //Only when the target actually CHANGES: MoveObjectOverTicks replaces an
                //in-flight motion with the new one, so re-requesting the same move every tick
                //would restart it every tick and the cube would never arrive.
                main_scene->MoveObjectOverTicks(cube,&target,NULL,TETRIS_SLIDE_TICKS);
            }
        }
        piece_cell_targets[i] = target;

        Object* ghost = ghost_objects[i];
        if (ghost){
            //Hidden when it would sit under the piece itself - a ghost drawn inside the piece is
            //just z-fighting with extra steps.
            if (ghost_y >= game.piece_y){
                ghost->Hide();
            }else{
                ghost->Show();
                ghost->SetPosition(CellWorldPosition(game.piece_x + cells[i].x,ghost_y + cells[i].y));
            }
        }
    }
    last_piece_type = game.piece_type;
}

void ApplicationTetris::SyncPreviewView(){
    for (int i = 0; i < 4; i++){
        Object* cube = hold_objects[i];
        if (!cube){
            continue;
        }
        if (game.hold_type < 0){
            cube->Hide();
            continue;
        }
        cube->Show();
        //Dimmed to the ghost material while it cannot be used again this piece, so the rule is
        //visible instead of being something the player has to remember.
        cube->SetMaterialSlot(0,game.f_hold_used ? material_ghost : material_piece[game.hold_type]);
        cube->SetPosition(PreviewCellPosition(vec3(PREVIEW_X,HOLD_Y,0),game.hold_type,i));
    }

    for (int n = 0; n < TETRIS_NEXT_QUEUE_SHOWN; n++){
        int type = (n < (int)game.next_queue.size()) ? game.next_queue[n] : -1;
        vec3 anchor = vec3(PREVIEW_X,NEXT_Y - n * NEXT_SPACING_Y,0);
        for (int i = 0; i < 4; i++){
            Object* cube = next_objects[n][i];
            if (!cube){
                continue;
            }
            if (type < 0){
                cube->Hide();
                continue;
            }
            cube->Show();
            cube->SetMaterialSlot(0,material_piece[type]);
            cube->SetPosition(PreviewCellPosition(anchor,type,i));
        }
    }
}

//--- Physics garnish ------------------------------------------------------------------------

void ApplicationTetris::SpawnClearDebris(){
    /*
        One dynamic cube per cleared cell, thrown out of the well and to the left, into the tray.
        Purely cosmetic: the board is still a plain array, no board cube has a collider, and the
        tray sits in front of the well in z - so there is no path by which any of this can reach
        the rules. That constraint is the whole reason the physics is allowed to be here at all.

        Safe to create bodies here because RunSimulationTick is not inside the physics step - the rule
        (core/SimCommand.h, ApplicationShip.cpp:995) is that a body must never be created from
        inside a contact callback, where rp3d is mid-iteration over its own arrays.
    */
    if (!f_debris_enabled || !main_scene->physics_world){
        return;
    }
    uint64_t now = main_scene->GetPhysicsTick();
    for (size_t r = 0; r < game.clearing_rows.size(); r++){
        int y = game.clearing_rows[r];
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            int8_t type = game.board[y][x];
            if (type < 0){
                continue;
            }
            Object* chunk = MakeCube(block_mesh,main_scene,"Debris",
                                     vec3((float)x,(float)y,DEBRIS_SPAWN_Z),
                                     vec3(0.5f),material_piece[type]);
            if (!chunk){
                continue;
            }
            chunk->SetPickability(false);
            Physics* p = chunk->AddPhysics(main_scene->physics_world);
            if (p){
                p->AddBoxCollider(vec3(0.25f),vec3(),quat().identity(),1.0f);
                //A body starts STATIC with gravity off - dynamics is opt-in.
                p->SetStatic(false);
                p->SetGravityEnabled(true);
                p->SetBounciness(0.25f);
                p->SetFrictionCoefficient(0.5f);
                /*
                    AIMED, not shoved. The first version of this gave each chunk a leftward push
                    proportional to its column, on the reasoning that the ones with furthest to
                    go should travel fastest - and every chunk in the row landed within a unit of
                    every other, in a heap against the near end of the tray. The reasoning was
                    right and the arithmetic cancelled: making the speed proportional to the
                    distance makes the LANDING POINT very nearly constant.

                    So solve it instead. The flight is a plain ballistic arc, the shelf height is
                    known, and the only unknown is the time to reach it - which the quadratic
                    below gives exactly. Dividing the distance by that time lands a chunk on the
                    spot it was aimed at, so the row fans out across the tray instead of piling
                    up where it fell out of the well.

                    Every number here is a function of the cell's position and of nothing else.
                    That is not fussiness: the piece bag is seeded and every duration in this app
                    is a tick count, so a random draw here would be the ONE thing standing between
                    this game and a replay that matches - see §7.1 of docs/tetris_findings.md.
                    The gravity is asked of the world rather than written down for the same
                    reason - a number copied here would be a number to get out of step.
                */
                float lift = 4.5f + (x & 1) * 1.2f;
                float g = -main_scene->physics_world->GetGravity().y;
                if (g < 0.01f){
                    g = 9.81f;      //a world with no gravity has nothing to aim in; fall back
                }
                //Time to fall from the row to the shelf: 0.5*g*t^2 - lift*t - (y - shelf) = 0.
                float drop = (float)y - TRAY_SHELF_Y;
                float flight = (lift + sqrtf(lift * lift + 2.0f * g * drop)) / g;

                //Column 0 lands nearest the well, column 9 nearest the far wall, the rows of a
                //multi-row clear a little deeper into the tray than one another - so a tetris
                //arrives as four ranks rather than as forty chunks in one line.
                float span = (TRAY_X_MAX - TRAY_X_MIN) - 2.0f;
                float target_x = (TRAY_X_MAX - 1.0f) - span * ((float)x / (float)(TRAY_BOARD_SPREAD));
                float target_z = TRAY_Z_MIN + 1.2f + (float)(r % 4) * 0.7f;

                p->SetVelocity(vec3((target_x - (float)x) / flight,
                                    lift,
                                    (target_z - DEBRIS_SPAWN_Z) / flight));
                p->SetAngularVelocity(vec3(2.0f,2.0f,-4.0f - (float)x * 0.5f));
            }
            TetrisDebris entry;
            entry.object = chunk;
            entry.spawn_tick = now;
            debris.push_back(entry);
        }
    }
}

void ApplicationTetris::UpdateDebris(){
    /*
        What bounds the pile is a COUNT, not a clock - see TETRIS_DEBRIS_MAX.

        The old rule reaped every chunk TETRIS_DEBRIS_LIFETIME_TICKS after it was born, which made
        the physics a firework: it went off, it was pretty, and four seconds later there was no
        evidence any of it had happened. Trimming by size instead lets the tray fill up, so a
        clear lands on the last clear's chunks and a good game leaves something to look at. The
        cost is flat either way, because `debris` is in spawn order and the oldest is the front.
    */
    bool f_any_destroyed = false;

    //A chunk that missed the tray is gone the moment it is below the world, whatever the cap
    //says. Without this a bad bounce would hold a slot in the pile forever, out of sight.
    for (size_t i = 0; i < debris.size(); ){
        Object* object = debris[i].object;
        if (object && object->GetWorldPosition().y > TETRIS_DEBRIS_FLOOR_Y){
            i++;
            continue;
        }
        if (object){
            object->Destroy();
            f_any_destroyed = true;
        }
        debris.erase(debris.begin() + i);
    }

    while ((int)debris.size() > TETRIS_DEBRIS_MAX){
        if (debris.front().object){
            debris.front().object->Destroy();
            f_any_destroyed = true;
        }
        debris.erase(debris.begin());
    }

    if (f_any_destroyed){
        //Object::Destroy only MARKS. Without this the cubes stop rendering but their rigid bodies
        //stay in the physics world for the life of the run - only two of the eleven apps in this
        //repo call it, which is a trap rather than a feature. Safe here: RunSimulationTick holds
        //physics_mutex, so the render thread is not walking the object list.
        main_scene->DeleteDestroyedObjects();
    }
}

/*
    How close the stack is to the top, said by the walls.

    The board itself never told the player this. The well is 20 rows and the piece spawns into
    the top one, so the difference between "plenty of room" and "the next S piece ends the game"
    is four rows that look exactly like the four below them. Three states rather than a gradient:
    a warning has to be noticed, and a colour that creeps up on you is a colour you do not see.

    Guarded by `wall_state` so the usual case writes nothing - this runs every tick, and a
    material slot assignment that changes nothing still dirties the object.
*/
void ApplicationTetris::UpdateDangerState(){
    int height = game.GetStackHeight();
    int state = TETRIS_WALL_CALM;
    if (height >= TETRIS_DANGER_ROWS){
        state = TETRIS_WALL_DANGER;
    }else if (height >= TETRIS_WARN_ROWS){
        state = TETRIS_WALL_WARN;
    }
    if (state == wall_state){
        return;
    }
    wall_state = state;

    int material = material_frame;
    if (state == TETRIS_WALL_WARN){
        material = material_frame_warn;
    }else if (state == TETRIS_WALL_DANGER){
        material = material_frame_danger;
    }
    for (int i = 0; i < 2; i++){
        if (well_walls[i]){
            well_walls[i]->SetMaterialSlot(0,material);
        }
    }
}

void ApplicationTetris::UpdateCameraShake(){
    Camera* camera = main_scene->camera;
    if (!camera){
        return;
    }
    if (shake_ticks <= 0){
        shake_amount = 0.0f;
        camera->SetPosition(vec3(camera_target.x,camera_target.y,40.0f));
        return;
    }
    shake_ticks--;
    //A decaying square wave on alternating ticks - cheap, and at 60Hz it reads as a jolt rather
    //than as a wobble. Driven off the simulation tick so it replays identically.
    float decay = shake_amount * ((float)shake_ticks / 14.0f);
    float sign = (main_scene->GetPhysicsTick() & 1) ? 1.0f : -1.0f;
    camera->SetPosition(vec3(camera_target.x + decay * sign * 0.5f,
                             camera_target.y - decay,
                             40.0f));
}

//--- Telemetry ------------------------------------------------------------------------------

/*
    The best score, and the only thing in this app that outlives the process.

    A plain text file beside the executable. It is NOT resolved through ResolveAssetPath and that
    is deliberate rather than an oversight: an asset is something the app reads and ships with,
    resolved against a search path that may point at a shared folder two apps deep - and writing
    a save file into that would put one app's scores under another app's assets. See core/File.h,
    which is about loading and has no write side for exactly this reason.

    Both halves fail silently. A missing or unreadable file is a best of zero, which is the
    correct answer for a machine that has never run this before; a failed write costs a score and
    nothing else, and a dialog about it in the middle of a game would cost more than it saved.
*/
static std::string BestScorePath(){
    return GetExecutableDirectory() + "/tetris_best.txt";
}

void ApplicationTetris::LoadBestScore(){
    FILE* f = fopen(BestScorePath().c_str(),"rb");
    if (!f){
        return;
    }
    char buffer[32] = {};
    size_t read = fread(buffer,1,sizeof(buffer) - 1,f);
    fclose(f);
    buffer[read] = 0;
    int value = atoi(buffer);
    if (value > 0){
        game.best_score = value;
        debug->Info("Best score so far: %i\n",value);
    }
}

void ApplicationTetris::SaveBestScore(){
    if (!f_best_dirty){
        return;
    }
    f_best_dirty = false;
    FILE* f = fopen(BestScorePath().c_str(),"wb");
    if (!f){
        debug->Warn("Could not write the best score to %s\n",BestScorePath().c_str());
        return;
    }
    fprintf(f,"%i\n",game.best_score);
    fclose(f);
}

void ApplicationTetris::PublishSnapshot(){
    //Filled on the physics thread and read by MCP tool handlers, which hold no lock of their own
    //- see the comment on TetrisSnapshot. The mutex is this app's, not the engine's: taking
    //physics_mutex from an MCP thread would be the other option, and it would stall the
    //simulation for the length of every telemetry call.
    std::lock_guard<std::mutex> lock(snapshot_mutex);
    snapshot.tick = main_scene->GetPhysicsTick();
    snapshot.game_ticks = game.ticks_elapsed;
    snapshot.score = game.score;
    snapshot.lines = game.lines;
    snapshot.level = game.level;
    snapshot.phase = game.phase;
    snapshot.pieces_placed = game.pieces_placed;
    snapshot.piece_type = (game.phase == TETRIS_PHASE_FALLING) ? game.piece_type : -1;
    snapshot.piece_rotation = game.piece_rotation;
    snapshot.piece_x = game.piece_x;
    snapshot.piece_y = game.piece_y;
    snapshot.ghost_y = (game.phase == TETRIS_PHASE_FALLING) ? game.GetGhostY() : 0;
    snapshot.hold_type = game.hold_type;
    snapshot.f_hold_used = game.f_hold_used;
    snapshot.f_paused = main_scene->IsPhysicsPaused();
    snapshot.combo = game.combo;
    snapshot.f_back_to_back = game.f_back_to_back;
    snapshot.best_score = game.best_score;
    snapshot.stack_height = game.GetStackHeight();
    snapshot.lock_progress = game.GetLockProgress();
    snapshot.debris_count = (int)debris.size();
    snapshot.seed = current_seed;
    snapshot.next_types = game.next_queue;
    snapshot.rows = game.ToAsciiRows();
}

#ifdef USE_MCP
//Only BuildStateJson uses this, so it follows that inside the guard -- otherwise a USE_MCP=0
//build warns about an unused static function.
static const char* PhaseName(int phase){
    switch (phase){
        case TETRIS_PHASE_SPAWN:      return "spawn";
        case TETRIS_PHASE_FALLING:    return "falling";
        case TETRIS_PHASE_CLEARING:   return "clearing";
        case TETRIS_PHASE_COLLAPSING: return "collapsing";
        case TETRIS_PHASE_GAMEOVER:   return "gameover";
    }
    return "?";
}

//--- MCP ------------------------------------------------------------------------------------

//This app's own MCP tools. Present only when USE_MCP=1; see the block in engine.mk for why
//the core half of the same switch is a swapped translation unit rather than an #ifdef.
void ApplicationTetris::RegisterMCPTools(){
    //Registered from Init(). The server only starts accepting requests after Init() returns, so
    //registration can never race a client's tools/list.

    //One action name -> one mapped keycode, shared by tetris_input and its schema so the two
    //cannot drift apart.
    struct ActionMap{
        const char* name;
        uint32_t mapped;
    };
    static const ActionMap actions[] = {
        { "left",       INPUT_TETRIS_LEFT },
        { "right",      INPUT_TETRIS_RIGHT },
        { "soft_drop",  INPUT_TETRIS_SOFT_DROP },
        { "hard_drop",  INPUT_TETRIS_HARD_DROP },
        { "rotate_cw",  INPUT_TETRIS_ROTATE_CW },
        { "rotate_ccw", INPUT_TETRIS_ROTATE_CCW },
        { "hold",       INPUT_TETRIS_HOLD },
    };
    static const int num_actions = (int)(sizeof(actions)/sizeof(actions[0]));

    MCPServer::Get()->RegisterTool("tetris_state",
        "The whole game state: the board as 20 ASCII rows (top row first, '.' empty, an uppercase "
        "letter for a settled block of that piece, lowercase for the four cells of the piece the "
        "player is still steering), plus score, level, lines, the active piece, the ghost's "
        "landing row, the hold slot and the next queue. Read from a snapshot the physics thread "
        "publishes at the end of every tick, so it never disturbs the game it is measuring. Set "
        "include_screenshot to also get a PNG of the current frame.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the current frame, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tetris_input",
        "Press a control for a number of SIMULATION TICKS and block until it has played out, then "
        "return the resulting state. This is how a program plays the game: the hold emits ordinary "
        "input events, so the simulation cannot tell it from a person's finger. The game runs at "
        "60 ticks per second. Edge-triggered actions (rotate_cw, rotate_ccw, hard_drop, hold) fire "
        "once on RELEASE, so a 1-tick hold is one rotation; level-triggered ones (left, right, "
        "soft_drop) act for as long as they are held, with the same auto-repeat a human gets. "
        "While the simulation is paused the hold does not count down - use tetris_step to advance "
        "it.",
        json{
            {"type","object"},
            {"properties", {
                {"action", {{"type","string"},{"enum", json::array({"left","right","soft_drop","hard_drop","rotate_cw","rotate_ccw","hold"})}}},
                {"ticks", {{"type","number"},{"description","how many simulation ticks to hold it, default 2, capped at 600"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the resulting frame, default false"}}}
            }},
            {"required", json::array({"action"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string action = args.value("action","");
            uint32_t mapped = 0;
            for (int i = 0; i < num_actions; i++){
                if (action == actions[i].name){
                    mapped = actions[i].mapped;
                    break;
                }
            }
            if (!mapped){
                return json{ {"error","unknown action"} };
            }
            int ticks = (int)clamp(args.value("ticks",2.0f),1.0f,600.0f);
            uint64_t start_tick = main_scene->GetPhysicsTick();
            input->HoldKey(mapped,(uint32_t)ticks);

            //Wait for the hold to have played out AND for its edge to have been consumed by a
            //tick. The gameplay actions now fire on the PRESS edge, so the action itself happens
            //on the FIRST tick of the hold rather than the tick after the last one - but this
            //still waits for the whole hold, because `ticks` is also how long a held action (a
            //soft drop, a DAS move) is meant to run, and the caller asked for that. Bounded, and
            //it gives up rather than hanging if the simulation is paused with nothing stepping it.
            uint64_t target_tick = start_tick + (uint64_t)ticks + 2;
            for (int waited_ms = 0; waited_ms < 4000 && main_scene->GetPhysicsTick() < target_tick; waited_ms += 4){
                Sleep(4);
            }
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tetris_pause",
        "Pause or resume the simulation. While paused the window keeps redrawing but no tick runs: "
        "nothing falls, no input is consumed and the tick counter stops. Pair it with tetris_step "
        "to watch the game one tick at a time.",
        json{
            {"type","object"},
            {"properties", {
                {"paused", {{"type","boolean"},{"description","true to pause, false to resume"}}}
            }},
            {"required", json::array({"paused"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            main_scene->PausePhysics(args.value("paused",true));
            return BuildStateJson();
        });

    MCPServer::Get()->RegisterTool("tetris_step",
        "Advance the paused simulation by exactly num_ticks ticks and return the resulting state. "
        "Requires tetris_pause first. Every duration in this game is a tick count, so stepping is "
        "exact: 48 steps at level 1 is exactly one cell of gravity. sim_step is the same stepping "
        "without the board state.",
        json{
            {"type","object"},
            {"properties", {
                {"num_ticks", {{"type","number"},{"description","how many ticks to advance, default 1"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the resulting frame, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","not paused - call tetris_pause with paused=true first"} };
            }
            int num_ticks = max((int)args.value("num_ticks",1.0f),0);
            //Shared with the core sim_step tool - see Application::StepPhysicsAndWait.
            StepPhysicsAndWait(num_ticks);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tetris_restart",
        "Throw the current game away and start a new one. The piece order is a pure function of "
        "the seed, so the same seed always deals the same game - which is what makes a scripted "
        "run reproducible. Goes through the simulation command queue, so it lands at the top of a "
        "tick on the physics thread rather than in the middle of one.",
        json{
            {"type","object"},
            {"properties", {
                {"seed", {{"type","number"},{"description","piece-order seed; omit or 0 for a fresh one"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = TETRIS_CMD_RESTART;
            cmd.value[0] = args.value("seed",0.0f);
            //SubmitCommandAndWait, not SubmitUICommand: an MCP handler holds no lock, so it may
            //wait - and it has to, or it would report the state of the game it just replaced.
            SubmitCommandAndWait(cmd);
            return BuildStateJson();
        });
}

json ApplicationTetris::BuildStateJson(){
    TetrisSnapshot copy;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        copy = snapshot;
    }
    json next = json::array();
    for (size_t i = 0; i < copy.next_types.size() && i < TETRIS_NEXT_QUEUE_SHOWN; i++){
        next.push_back(GetTetrominoName(copy.next_types[i]));
    }
    json rows = json::array();
    for (size_t i = 0; i < copy.rows.size(); i++){
        rows.push_back(copy.rows[i]);
    }
    //Everything else comes from the snapshot, but "paused" is read live: the snapshot is only
    //written by a tick, and pausing is precisely the thing that stops ticks happening - so a
    //snapshotted value would report the state from BEFORE the pause and never update.
    bool f_paused = main_scene ? main_scene->IsPhysicsPaused() : copy.f_paused;
    return json{
        {"tick",copy.tick},
        {"game_ticks",copy.game_ticks},
        {"paused",f_paused},
        //Read live for the same reason as "paused": it is chrome, toggled in UpdateView on passes
        //that need not tick, so a snapshotted copy would lag or never move at all. It is here so
        //the mute button is TESTABLE - without it the only way to tell whether a press landed is
        //to listen, which no automated check can do.
        {"sound",f_sound_enabled},
        {"phase",PhaseName(copy.phase)},
        {"score",copy.score},
        {"lines",copy.lines},
        {"level",copy.level},
        //The run bonuses, so a scripted player can actually play FOR them - without these a bot
        //can see that the score went up and has no way to find out which of the three reasons it
        //was. `combo` is the multiplier, so -1 is "no run" and 0 is "one clear in, paying
        //nothing yet"; see Playfield::combo.
        {"combo",copy.combo},
        {"back_to_back",copy.f_back_to_back},
        {"last_clear_lines",copy.last_clear_lines},
        {"last_clear_points",copy.last_clear_points},
        {"last_clear_spin",Playfield::SpinName(copy.last_clear_spin)},
        {"best_score",copy.best_score},
        //How close to losing, and how much of the lock delay is left. Both are things a scripted
        //player has to be able to see: without the first it cannot know when to stop stacking,
        //and without the second it cannot know how long it still has to slide a piece.
        {"stack_height",copy.stack_height},
        {"lock_progress",copy.lock_progress},
        {"pieces_placed",copy.pieces_placed},
        {"seed",copy.seed},
        {"piece",copy.piece_type >= 0 ? json(GetTetrominoName(copy.piece_type)) : json(nullptr)},
        {"piece_rotation",copy.piece_rotation},
        {"piece_x",copy.piece_x},
        {"piece_y",copy.piece_y},
        {"ghost_y",copy.ghost_y},
        {"hold",copy.hold_type >= 0 ? json(GetTetrominoName(copy.hold_type)) : json(nullptr)},
        {"hold_used",copy.f_hold_used},
        {"next",next},
        {"debris_bodies",copy.debris_count},
        {"board",rows}
    };
}
#endif //USE_MCP

//--- HUD ------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationTetris::DrawImGuiUI(void){
    //Runs on the RENDER thread with physics_mutex held, so reading the simulation directly here
    //is safe - and is the reason this reads `game` rather than the snapshot the MCP tools use.
    if (f_show_engine_ui){
        RenderApplicationUI();
    }
    RenderTetrisHUD();
}
#endif //USE_IMGUI

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationTetris::RenderTetrisHUD(){
    //Anchored top-left and kept narrow, because the engine's debug panels dock into the same
    //corner when F1 is on and the two should not fight over it.
    ImGui::SetNextWindowPos(ImVec2(f_show_engine_ui ? 320.0f : 16.0f,16.0f),ImGuiCond_Always);
    //290 rather than 250: the scoring table below is the widest thing in here and a reference
    //that wraps its own last column is not much of a reference.
    ImGui::SetNextWindowSize(ImVec2(290,0),ImGuiCond_Always);
    ImGui::Begin("Tetris",NULL,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    ImGui::Text("SCORE  %i",game.score);
    ImGui::Text("BEST   %i",game.best_score);
    ImGui::Text("LEVEL  %i",game.level);
    ImGui::Text("LINES  %i",game.lines);
    //The same warning the walls carry, in words, for whoever is reading this panel instead of
    //the board - and because a number says how many rows are actually left.
    {
        int height = game.GetStackHeight();
        if (height >= TETRIS_DANGER_ROWS){
            ImGui::TextColored(ImVec4(1,0.25f,0.25f,1),"STACK  %i/%i  DANGER",height,TETRIS_BOARD_H);
        }else if (height >= TETRIS_WARN_ROWS){
            ImGui::TextColored(ImVec4(1,0.75f,0.2f,1),"STACK  %i/%i",height,TETRIS_BOARD_H);
        }else{
            ImGui::Text("STACK  %i/%i",height,TETRIS_BOARD_H);
        }
    }
    //The two bonuses that are about to expire, so they are worth seeing while they are live.
    if (game.combo > 0){
        ImGui::TextColored(ImVec4(1,0.6f,0.3f,1),"COMBO  x%i%s",game.combo,game.f_back_to_back ? "   B2B" : "");
    }else if (game.f_back_to_back){
        ImGui::TextColored(ImVec4(1,0.6f,0.3f,1),"B2B READY");
    }
    ImGui::Separator();
    ImGui::Text("piece  %s",game.phase == TETRIS_PHASE_FALLING ? GetTetrominoName(game.piece_type) : "-");
    ImGui::Text("tick   %llu",(unsigned long long)main_scene->GetPhysicsTick());
    ImGui::Text("gravity %i ticks/cell",Playfield::GravityTicksForLevel(game.level));

    /*
        The scoring table, printed from the same functions the rules score with rather than from a
        copy of the numbers. A player who has never been told that a tetris is worth eight times a
        single has no reason to build a well, and the popup over the board only ever answers "what
        was THAT worth" - this is the part that answers "what is anything worth".

        OPEN by default, and collapsible for anyone who has read it once. A reference nobody can
        see is the state this app was already in.
    */
    if (ImGui::CollapsingHeader("Scoring",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextDisabled("all line scores multiply by LEVEL");
        ImGui::Text("%-7s %5s %5s %5s","","plain","T-spin","perf");
        for (int n = 1; n <= 4; n++){
            //A T-spin triple is the widest a T can clear, so the fourth row's T-spin column is
            //the table saying "not possible" rather than a number - print it as such.
            char spin_cell[8] = "-";
            if (n <= 3){
                snprintf(spin_cell,sizeof(spin_cell),"%i",Playfield::LineClearBaseScore(n,false,TETRIS_SPIN_FULL));
            }
            ImGui::Text("%-7s %5d %5s %5d",
                        Playfield::LineClearName(n),
                        Playfield::LineClearBaseScore(n,false),
                        spin_cell,
                        Playfield::LineClearBaseScore(n,true));
        }
        ImGui::Text("t-spin  %5d  clearing nothing at all",
                    Playfield::LineClearBaseScore(0,false,TETRIS_SPIN_FULL));
        ImGui::Text("  mini  %5d  one front corner open",
                    Playfield::LineClearBaseScore(0,false,TETRIS_SPIN_MINI));
        ImGui::Text("combo   +%d per clear after the 1st",Playfield::ComboStepScore());
        ImGui::Text("b2b     +%d%% tetris/t-spin in a row",Playfield::BackToBackBonusPercent());
        ImGui::Text("drops   %d/cell soft, %d/cell hard",
                    Playfield::SoftDropCellScore(),Playfield::HardDropCellScore());
    }

    if (game.phase == TETRIS_PHASE_GAMEOVER){
        //The engine has no world-space text of any kind - one 13px ImGui font is the whole text
        //rendering story - so "GAME OVER" is a HUD line rather than something on the board.
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"GAME OVER");
    }
    if (main_scene->IsPhysicsPaused()){
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1,0.85f,0.2f,1),"PAUSED");
    }

    ImGui::Separator();
    if (ImGui::Button("New game")){
        //SubmitUICommand, never SubmitCommandAndWait: this runs with physics_mutex held, and the
        //physics thread needs that same lock to drain the queue - waiting here deadlocks.
        SimCommand cmd;
        cmd.type = TETRIS_CMD_RESTART;
        cmd.value[0] = 0.0f;   //0 means "pick a fresh seed"
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    if (ImGui::Button(main_scene->IsPhysicsPaused() ? "Resume" : "Pause")){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }
    ImGui::Checkbox("Sound",&f_sound_enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Debris",&f_debris_enabled);

    ImGui::Separator();
    ImGui::TextDisabled("arrows move/soft drop");
    ImGui::TextDisabled("Z/X or up rotate");
    ImGui::TextDisabled("space hard drop, C hold");
    ImGui::TextDisabled("R restart, P pause, M mute, F1 panels");
    ImGui::TextDisabled("pad d-pad/stick move, A/B/X rotate");
    ImGui::TextDisabled("pad Y drop, LB/RB hold, start pause");

    ImGui::End();
}
#endif //USE_IMGUI
