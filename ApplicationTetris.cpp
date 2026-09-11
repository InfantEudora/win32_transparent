#include "ApplicationTetris.h"
#include "Debug.h"
#include "Primitives.h"
#include "type_helpers.h"
#include "MCPServer.h"

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
#define NEXT_Y              12.0f
#define NEXT_SPACING_Y      3.5f

//The text column, in those same world coordinates. Left-aligned a little inside the preview
//column so the captions line up with each other rather than with the pieces they label.
//TEXT_SCALE is in ems and one em is one world unit per line, so 0.8 makes a line of text a
//little shorter than a block is tall - see core/TextMesh.h.
#define TEXT_X              11.6f
#define TEXT_SCALE          0.8f

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
    if (!renderer->Init(PIPELINE_DEFERRED)){
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
    soundsystem->AppendFile("data/sound/click.wav","move");
    soundsystem->AppendFile("data/sound/click.wav","lock");
    soundsystem->AppendFile("data/sound/bleep.wav","rotate");
    soundsystem->AppendFile("data/sound/bleep.wav","hold");
    soundsystem->AppendFile("data/sound/floop.wav","clear");
    soundsystem->AppendFile("data/sound/hax.wav","gameover");

    BuildMaterials();
    BuildWell();
    BuildViewObjects();
    BuildTextLabels();
    SetupFieldShadows();
    SetupCamera();
    SetupInput();
    RegisterCommandHandlers();
    RegisterMCPTools();

    //60 ticks per second, not the engine's default 50. The classic gravity table in
    //Playfield.cpp is denominated in 60Hz frames, so at 60 ticks the table means exactly what it
    //meant on the hardware it came from, and every other duration in this app (DAS, lock delay,
    //the clear flash) is a count of these same ticks.
    SetPhysicsTPS(60.0f);

    NewGame(current_seed);

    main_window->Resize(1200,900);

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
    if (!LoadGlyphSetFromGLB(glyphs,"data/glyphs_unispace.glb",0.509167f,1.0f)){
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
        //The stats go below the next queue, which ends at NEXT_Y - 2*NEXT_SPACING_Y = 5.0.
        //Score gets two lines because it is the number people actually look at.
        { TETRIS_LABEL_SCORE,    vec3(TEXT_X, 3.0f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "SCORE\n0" },
        { TETRIS_LABEL_LINES,    vec3(TEXT_X, 1.0f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "LINES 0" },
        { TETRIS_LABEL_LEVEL,    vec3(TEXT_X,-0.1f,0.0f),         TEXT_SCALE, TEXT_ALIGN_LEFT,   0, "LEVEL 1" },
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
    bool f_gameover = false;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        score = snapshot.score;
        lines = snapshot.lines;
        level = snapshot.level;
        f_gameover = (snapshot.phase == TETRIS_PHASE_GAMEOVER);
    }

    char text[64];
    snprintf(text,sizeof(text),"SCORE\n%i",score);
    SetLabelText(TETRIS_LABEL_SCORE,text);
    snprintf(text,sizeof(text),"LINES %i",lines);
    SetLabelText(TETRIS_LABEL_LINES,text);
    snprintf(text,sizeof(text),"LEVEL %i",level);
    SetLabelText(TETRIS_LABEL_LEVEL,text);

    //Text or no text is the whole of the banner's state.
    SetLabelText(TETRIS_LABEL_GAMEOVER,f_gameover ? "GAME OVER\nPRESS R" : "");
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
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::UpdatePhysics itself, so this is the whole feature.
    input->AddKeyMap('P',INPUT_PAUSE);

    //Gamepad: the left stick's X axis steers, which is the one analog control a Tetris has. The
    //D-pad arrives through XInput as buttons rather than as an analog index, so it is not mapped
    //here - see docs/tetris_findings.md on what that costs.
    input->AddGamePadMap(0,INPUT_TETRIS_LEFT);
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
    current_seed = seed;
    game.NewGame(seed);
    //Throw away everything the previous game left in the world.
    for (size_t i = 0; i < debris.size(); i++){
        if (debris[i].object){
            debris[i].object->Destroy();
        }
    }
    debris.clear();
    renderer->DeleteDestroyedObjects();
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

    //Restart is a game action, so it stays here and is read whether or not a game is in progress.
    //It is deliberately not part of the TetrisInput the rules see.
    if (input->WasKeyReleased(INPUT_TETRIS_RESTART)){
        //Already on the physics thread inside the tick - so this is a direct call, not a command.
        //A command would be a round trip through the queue to arrive back here one tick later.
        NewGame(next_auto_seed++);
    }

    TetrisInput actions;
    GatherInput(actions);

    TetrisEvents events;
    game.Tick(actions,events);
    HandleEvents(events);

    SyncBoardView();
    SyncPieceView();
    SyncPreviewView();
    UpdateDebris();
    UpdateCameraShake();
    PublishSnapshot();
}

void ApplicationTetris::GatherInput(TetrisInput& out){
    InputController* input = main_scene->inputcontroller;

    //Most apps gate input on main_window->f_has_focus so keys meant for another application do
    //not drive the game. A scripted hold does not come from the OS, so it must come through that
    //gate anyway - otherwise every MCP-driven test does nothing, which is exactly when the window
    //is not in front. InputController::HasSyntheticHolds is the sanctioned way to ask.
    if (!main_window->f_has_focus && !input->HasSyntheticHolds()){
        das_direction = 0;
        das_ticks_left = 0;
        arr_ticks_left = 0;
        return;
    }

    //Edge-triggered actions: one press, one action, no matter how long the key is held.
    out.f_rotate_cw  = input->WasKeyReleased(INPUT_TETRIS_ROTATE_CW);
    out.f_rotate_ccw = input->WasKeyReleased(INPUT_TETRIS_ROTATE_CCW);
    out.f_hard_drop  = input->WasKeyReleased(INPUT_TETRIS_HARD_DROP);
    out.f_hold       = input->WasKeyReleased(INPUT_TETRIS_HOLD);

    //Level-triggered: soft drop is "gravity is fast while this is down".
    out.f_soft_drop = input->IsKeyDown(INPUT_TETRIS_SOFT_DROP);

    /*
        DAS/ARR. A held left or right moves once immediately, then pauses for TETRIS_DAS_TICKS,
        then repeats every TETRIS_ARR_TICKS. Both are tick counts, so the feel of the controls is
        identical whether the simulation is running freely, single-stepped, or replayed - which
        would not be true of a millisecond timer, and is the single most important reason this
        translation lives on the physics thread rather than in the window message handler.
    */
    bool f_left = input->IsKeyDown(INPUT_TETRIS_LEFT);
    bool f_right = input->IsKeyDown(INPUT_TETRIS_RIGHT);
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

    for (int i = 0; i < 4; i++){
        vec3 target = CellWorldPosition(game.piece_x + cells[i].x,game.piece_y + cells[i].y);
        Object* cube = piece_objects[i];
        if (cube){
            cube->Show();
            cube->SetMaterialSlot(0,material_piece[game.piece_type]);
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
        One dynamic cube per cleared cell, thrown forward out of the well. Purely cosmetic: the
        board is still a plain array and nothing here can affect the rules, which is the only
        way to have physics in a Tetris without ruining it.

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
                                     CellWorldPosition(x,y) + vec3(0,0,0.2f),
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
                p->SetBounciness(0.35f);
                p->SetFrictionCoefficient(0.4f);
                //Thrown out towards the camera and away from the centre of the row, so the row
                //bursts outwards instead of dropping straight down in a slab. Deterministic: it
                //is a function of the cell's position, not of a random draw.
                float sideways = ((float)x - (TETRIS_BOARD_W - 1) * 0.5f) * 0.8f;
                p->SetVelocity(vec3(sideways,2.5f,4.0f + (x & 1)));
                p->SetAngularVelocity(vec3(sideways,2.0f,-sideways));
            }
            TetrisDebris entry;
            entry.object = chunk;
            entry.reap_tick = now + TETRIS_DEBRIS_LIFETIME_TICKS;
            debris.push_back(entry);
        }
    }
}

void ApplicationTetris::UpdateDebris(){
    uint64_t now = main_scene->GetPhysicsTick();
    bool f_any_destroyed = false;
    for (size_t i = 0; i < debris.size(); ){
        Object* object = debris[i].object;
        //Also reaped once it has fallen well below the well, so a chunk that missed the floor
        //does not survive on the far side of the world just because its timer has not run out.
        bool f_expired = (now >= debris[i].reap_tick) || (object && object->GetWorldPosition().y < -12.0f);
        if (!f_expired){
            i++;
            continue;
        }
        if (object){
            object->Destroy();
            f_any_destroyed = true;
        }
        debris.erase(debris.begin() + i);
    }
    if (f_any_destroyed){
        //Object::Destroy only MARKS. Without this the cubes stop rendering but their rigid bodies
        //stay in the physics world for the life of the run - only two of the eleven apps in this
        //repo call it, which is a trap rather than a feature. Safe here: RunSimulationTick holds
        //physics_mutex, so the render thread is not walking the object list.
        renderer->DeleteDestroyedObjects();
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
    snapshot.debris_count = (int)debris.size();
    snapshot.seed = current_seed;
    snapshot.next_types = game.next_queue;
    snapshot.rows = game.ToAsciiRows();
}

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

            //Wait for the hold to have played out AND for the release edge to have been consumed
            //by a tick - an edge-triggered action is read with WasKeyReleased, which only becomes
            //true on the tick after the hold ends. Bounded, and it gives up rather than hanging
            //if the simulation is paused with nothing stepping it.
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
        {"phase",PhaseName(copy.phase)},
        {"score",copy.score},
        {"lines",copy.lines},
        {"level",copy.level},
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

//--- HUD ------------------------------------------------------------------------------------

void ApplicationTetris::DrawImGuiUI(void){
    //Runs on the RENDER thread with physics_mutex held, so reading the simulation directly here
    //is safe - and is the reason this reads `game` rather than the snapshot the MCP tools use.
    if (f_show_engine_ui){
        RenderApplicationUI();
    }
    RenderTetrisHUD();
}

void ApplicationTetris::RenderTetrisHUD(){
    //Anchored top-left and kept narrow, because the engine's debug panels dock into the same
    //corner when F1 is on and the two should not fight over it.
    ImGui::SetNextWindowPos(ImVec2(f_show_engine_ui ? 320.0f : 16.0f,16.0f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250,0),ImGuiCond_Always);
    ImGui::Begin("Tetris",NULL,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    ImGui::Text("SCORE  %i",game.score);
    ImGui::Text("LEVEL  %i",game.level);
    ImGui::Text("LINES  %i",game.lines);
    ImGui::Separator();
    ImGui::Text("piece  %s",game.phase == TETRIS_PHASE_FALLING ? GetTetrominoName(game.piece_type) : "-");
    ImGui::Text("tick   %llu",(unsigned long long)main_scene->GetPhysicsTick());
    ImGui::Text("gravity %i ticks/cell",Playfield::GravityTicksForLevel(game.level));

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
    ImGui::TextDisabled("R restart, P pause, F1 panels");

    ImGui::End();
}
