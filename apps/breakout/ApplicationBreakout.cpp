#include "ApplicationBreakout.h"
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

#include <math.h>
#include <string.h>
#include <stdio.h>

static Debugger* debug = new Debugger("ApplicationBreakout",DEBUG_ALL);

/*
    Where the arena sits in the world.

    The play plane is z = 0, x to the right, y up - so breakout/Field's coordinates ARE world
    coordinates and nothing is ever converted. The camera then makes this read as a paddle game by
    looking up the field from slightly below and in front, rather than straight on: the same trick
    Tetris plays with an orthographic camera, but with a little perspective kept on purpose so the
    bricks are solid objects with sides and shadows instead of coloured rectangles.
*/
#define ARENA_Z             0.0f
#define WALL_THICKNESS      1.0f
#define WALL_DEPTH          2.4f
#define BACK_Z              -1.40f
#define BRICK_VISUAL_W      1.86f
#define BRICK_VISUAL_H      0.86f
#define BRICK_DEPTH         1.00f
#define PADDLE_DEPTH        0.90f

//The text column, to the RIGHT of the arena. The engine's own debug panels dock to the LEFT
//(Application::RenderApplicationUI), so anything put over there is covered the moment F1 is on.
#define TEXT_X              24.4f
#define TEXT_SCALE          0.90f
/*
    The labels sit just in FRONT of the back panel rather than out in the play plane.

    Text here is geometry, so it casts a real shadow like everything else - and a label four units
    clear of the panel behind it throws a crisp, perfectly legible second copy of itself onto that
    panel, offset by the sun. It reads as a rendering fault, not as a shadow. Pressing the labels
    up against the panel collapses the offset to nothing.

    THAT IS NO LONGER WHY THIS VALUE IS SMALL. The alternative was to stop the labels casting at
    all, and there was no way to say it - Light::f_casts_shadow turns a whole LIGHT off, and an
    Object had no say in whether it appeared in a depth pass (docs/breakout_findings.md 5). There
    is now: Object::SetCastsShadow, and every label here calls it. So the labels are free to sit
    wherever they look best, and a banner that wants to float in front of the arena can.

    What still holds this value down is the other half: the panel's front face is at BACK_Z + 0.25
    (it is a 0.5-thick slab centred on BACK_Z), so this clears it by 0.15 - far enough that the
    glyphs, which are 0.1 deep themselves, are not fighting the panel for the same depth.
*/
#define TEXT_Z              (BACK_Z + 0.40f)

//The shield's quad. Wider than the arena on purpose: it passes THROUGH the side walls, and the
//soft intersection the shader does against the G-buffer is what makes that read as a field
//anchored into the structure rather than as a rectangle laid over it.
#define SHIELD_QUAD_W       25.0f
#define SHIELD_QUAD_H       2.40f

//Raw mouse counts to world units. A little under a thumb-width of mouse movement crosses the
//arena, which is the sensitivity an arcade spinner had.
#define MOUSE_TO_WORLD      0.035f
//...and a hard cap per tick. While the simulation is paused nothing consumes the accumulated
//delta, so it piles up and arrives as one jump on the tick after the resume. Clamping it is
//cheaper and more honest than pretending the pause did not happen.
#define MOUSE_MAX_PER_TICK  2.5f

ApplicationBreakout::ApplicationBreakout():Application(){
    debug->Info("Created new application.\n");
}

ApplicationBreakout::~ApplicationBreakout(){
}

//--- Small helpers ------------------------------------------------------------------------------

//Every box in this app is the same unit cube with a scale, a position and one material slot, so
//this is the one place that knows how to make one. The mesh is shared by pointer; SetMesh takes a
//reference, so it outlives any individual object.
static Object* MakeBoxObject(Mesh* mesh, Scene* scene, const char* name,
                             const vec3& position, const vec3& scale, int material_index){
    if (!mesh){
        return NULL;
    }
    Object* object = new Object();
    object->SetMesh(mesh);
    object->name = name;
    object->SetPosition(position);
    object->SetScale(scale);
    //A generated mesh carries no material names, so there is nothing for
    //Renderer::UpdateObjectMaterials to resolve over this slot on the next frame - the index is
    //simply the answer. See docs/engine_backlog.md item 14 for the invariant.
    object->SetMaterialSlot(0,material_index);
    scene->AddObject(object);
    return object;
}

static const char* PhaseName(int phase){
    switch (phase){
        case BREAKOUT_PHASE_READY:          return "ready";
        case BREAKOUT_PHASE_PLAYING:        return "playing";
        case BREAKOUT_PHASE_LOST_BALL:      return "lost_ball";
        case BREAKOUT_PHASE_LEVEL_CLEARED:  return "level_cleared";
        case BREAKOUT_PHASE_GAMEOVER:       return "gameover";
    }
    return "?";
}

static const char* PowerupName(int kind){
    switch (kind){
        case POWERUP_WIDE_PADDLE:   return "wide_paddle";
        case POWERUP_SLOW_BALL:     return "slow_ball";
        case POWERUP_SHIELD:        return "shield";
        case POWERUP_MULTIBALL:     return "multiball";
    }
    return "?";
}

//--- Setup --------------------------------------------------------------------------------------

void ApplicationBreakout::Init(void){
    //Deferred rather than MSAA: it is what fills the object-id buffer, and so what makes mouse
    //picking and the Inspector work - but more to the point here, it is what the custom shader
    //pass reads to know how far away the solid scene is. The shield's soft intersection against
    //the arena walls needs that, so PIPELINE_DEFERRED is load-bearing in this app rather than a
    //default that was copied.
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //Still constructed even though this app loads no assets from disk: the engine reaches for it
    //unguarded in places (the Scene panel's asset list, the object_spawn command handler).
    assetmanager = new AssetManager();

    unit_mesh = MakeBox(vec3(1,1,1));
    ball_mesh = MakeSphere(BREAKOUT_BALL_RADIUS,22,12);
    if (!unit_mesh || !ball_mesh){
        debug->Fatal("Failed to build the primitive meshes\n");
    }
    //Registered as assets so the asset holds a reference for the app's own pointer. A brick burst
    //can destroy a lot of objects at once, and without a holder that outlives them the count
    //could reach zero while the pointer is still about to be handed to the next object.
    assetmanager->AddNewAsset("bo_unit_box",unit_mesh);
    assetmanager->AddNewAsset("bo_ball_mesh",ball_mesh);

    main_scene = CreateNewScene("Breakout");
    main_scene->physics_world = new PhysicsWorld();
    //Gravity only has to pull debris and capsules down the screen. A little stronger than earth
    //so a dropped capsule arrives while the rally that earned it is still going.
    main_scene->physics_world->SetGravity(vec3(0,-16.0f,0));
    main_scene->physics_world->SetDebugRendering(false);

    {   //Without a light everything renders black. Aimed at the middle of the arena, with the
        //shadow ortho (viewport.zoom) wide enough to cover the arena AND the text column, or the
        //labels fall outside the shadow map and go dark.
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        sun->SetPosition(vec3(-12.0f,36.0f,26.0f));
        sun->SetLookAt(vec3(11.0f,13.0f,0.0f));
        sun->color = vec3(1.0f,0.95f,0.88f);
        sun->brightness = 4.2f;
        sun->viewport.zoom = 30.0f;     //half-extent in world units; the arena alone is 27 tall
        main_scene->AddObject(sun);
    }
    {   //A cool fill from the front so the faces pointing at the camera are not pure shadow.
        //A second directional light costs one entry in the light SSBO and nothing else.
        DirectionalLight* fill = new DirectionalLight();
        fill->name = "Fill";
        //From the right and in front, so it lifts the text column out of the shadow the right
        //wall casts across it - the sun comes from the left and that column is the one part of
        //the scene it never reaches.
        fill->SetPosition(vec3(34.0f,6.0f,24.0f));
        fill->SetLookAt(vec3(16.0f,16.0f,0.0f));
        fill->color = vec3(0.50f,0.66f,1.0f);
        fill->brightness = 1.3f;
        fill->f_casts_shadow = false;
        fill->viewport.zoom = 30.0f;
        main_scene->AddObject(fill);
    }

    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    /*
        Eight handles over the four files in data/sound. Deliberately more handles than files:
        SoundSystem gives each HANDLE exactly one OpenAL source and a source cannot overlap
        itself, so a ball breaking two bricks on one tick through one handle is ONE sound. The
        three brick handles below are round-robined for exactly that reason.
    */
    soundsystem->AppendFile("sound/bleep.wav","brick_a");
    soundsystem->AppendFile("sound/bleep.wav","brick_b");
    soundsystem->AppendFile("sound/bleep.wav","brick_c");
    soundsystem->AppendFile("sound/click.wav","paddle");
    soundsystem->AppendFile("sound/click.wav","wall");
    soundsystem->AppendFile("sound/floop.wav","shield");
    soundsystem->AppendFile("sound/floop.wav","powerup");
    soundsystem->AppendFile("sound/hax.wav","lost");

    BuildMaterials();
    BuildArena();
    BuildBricks();
    BuildBallsAndPaddle();
    BuildShield();
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

    //The rules denominate everything in ticks and were written against this rate - see
    //BREAKOUT_TPS in breakout/Field.h, which is the one number they cannot look up for
    //themselves. Sixty rather than the engine's fifty because a paddle is the most
    //latency-sensitive control in the repo.
    SetPhysicsTPS(BREAKOUT_TPS);

    //Contacts, for catching the power-up capsules. After the bodies exist, which is what the
    //ordering of this whole function is about.
    main_scene->physics_world->rp_world->setEventListener(this);

    NewGame(current_seed);

    main_window->Resize(1280,960);

    //One tick so the first frame is not an empty arena.
    main_scene->StepPhysics(1);
}

void ApplicationBreakout::BuildMaterials(){
    /*
        A hue per brick ROW for the plain bricks, and a distinct look for each of the special
        types. That split is the readability rule this whole palette is built on: colour tells
        you WHERE a brick is, surface tells you WHAT it is. A player scanning the wall for the
        armoured bricks should not have to also remember which shade of blue row five was.
    */
    static const vec4 row_colours[BREAKOUT_ROWS] = {
        vec4(0.93f,0.24f,0.22f,1.0f),   //row 0, the bottom of the wall
        vec4(0.96f,0.47f,0.13f,1.0f),
        vec4(0.97f,0.76f,0.14f,1.0f),
        vec4(0.52f,0.85f,0.22f,1.0f),
        vec4(0.16f,0.82f,0.55f,1.0f),
        vec4(0.16f,0.72f,0.93f,1.0f),
        vec4(0.35f,0.45f,0.95f,1.0f),
        vec4(0.70f,0.35f,0.93f,1.0f)    //row 7, the top
    };
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        Material m;
        char name[32];
        snprintf(name,sizeof(name),"bo_brick_row%i",r);
        m.name = name;
        m.glsl_material.color = row_colours[r];
        m.glsl_material.metallic = 0.20f;
        m.glsl_material.roughness = 0.42f;
        //A little self-illumination so a brick in the shadow of the wall above it still reads as
        //its own colour rather than as a dark grey lump. emissive.w is what actually makes
        //something glow - emissive.rgb alone is clamped to 1 (see core/Material.h).
        m.glsl_material.emissive = vec4(row_colours[r].x,row_colours[r].y,row_colours[r].z,0.16f);
        renderer->AddMaterial(m);
        material_brick_row[r] = renderer->FindMaterialIndex(m.name);
    }

    {   //Tough: brushed steel. Reads as harder than a painted brick at a glance, which is the
        //whole job - the player has to know it will take two hits before they aim at it.
        Material m;
        m.name = "bo_brick_tough";
        m.glsl_material.color = vec4(0.78f,0.82f,0.88f,1.0f);
        //Metallic 0.9 looked right on paper and rendered almost BLACK: this app turns the skybox
        //off, so a nearly-metallic surface has no environment to be metallic against and the
        //diffuse term it gave up is not replaced by anything. Half-metal plus a little glow of
        //its own is what actually reads as steel here. The same trap is waiting for anything else
        //that sets a high metallic in a scene with no reflections.
        m.glsl_material.metallic = 0.45f;
        m.glsl_material.roughness = 0.30f;
        m.glsl_material.emissive = vec4(0.62f,0.70f,0.82f,0.30f);
        renderer->AddMaterial(m);
        material_brick_tough = renderer->FindMaterialIndex(m.name);
    }
    {   //Armoured: dark, hot-edged. Three hits.
        Material m;
        m.name = "bo_brick_armoured";
        m.glsl_material.color = vec4(0.30f,0.09f,0.11f,1.0f);
        m.glsl_material.metallic = 0.85f;
        m.glsl_material.roughness = 0.35f;
        m.glsl_material.emissive = vec4(1.0f,0.22f,0.10f,0.30f);
        renderer->AddMaterial(m);
        material_brick_armoured = renderer->FindMaterialIndex(m.name);
    }
    {   //Prize: gold and unmistakably lit from inside. It is the only brick worth going out of
        //your way for, so it is the only one that glows.
        Material m;
        m.name = "bo_brick_prize";
        m.glsl_material.color = vec4(1.0f,0.84f,0.28f,1.0f);
        m.glsl_material.metallic = 0.65f;
        m.glsl_material.roughness = 0.18f;
        m.glsl_material.emissive = vec4(1.0f,0.78f,0.22f,0.85f);
        renderer->AddMaterial(m);
        material_brick_prize = renderer->FindMaterialIndex(m.name);
    }
    {   //Solid: level geometry, not a target. Nearly black so it reads as a hole in the wall.
        Material m;
        m.name = "bo_brick_solid";
        m.glsl_material.color = vec4(0.16f,0.17f,0.21f,1.0f);
        //Same reason as the tough brick above, but this one WANTS to be dark - just not so dark
        //that it disappears into the back panel and reads as a hole rather than as a block.
        m.glsl_material.metallic = 0.55f;
        m.glsl_material.roughness = 0.20f;
        m.glsl_material.emissive = vec4(0.30f,0.34f,0.45f,0.16f);
        renderer->AddMaterial(m);
        material_brick_solid = renderer->FindMaterialIndex(m.name);
    }
    {   //The ball. Emissive hard, because it also carries a point light and the two have to agree
        //about where the brightest thing on the screen is.
        Material m;
        m.name = "bo_ball";
        m.glsl_material.color = vec4(1.0f,0.97f,0.88f,1.0f);
        m.glsl_material.metallic = 0.1f;
        m.glsl_material.roughness = 0.25f;
        m.glsl_material.emissive = vec4(1.0f,0.86f,0.55f,1.6f);
        renderer->AddMaterial(m);
        material_ball = renderer->FindMaterialIndex(m.name);
    }
    {
        Material m;
        m.name = "bo_paddle";
        m.glsl_material.color = vec4(0.80f,0.92f,1.0f,1.0f);
        m.glsl_material.metallic = 0.55f;
        m.glsl_material.roughness = 0.22f;
        m.glsl_material.emissive = vec4(0.35f,0.75f,1.0f,0.45f);
        renderer->AddMaterial(m);
        material_paddle = renderer->FindMaterialIndex(m.name);
    }
    {   //The widened paddle gets its own colour rather than just being longer, so the power-up
        //running out is something the player SEES rather than something they discover.
        Material m;
        m.name = "bo_paddle_wide";
        m.glsl_material.color = vec4(0.70f,1.0f,0.72f,1.0f);
        m.glsl_material.metallic = 0.55f;
        m.glsl_material.roughness = 0.22f;
        m.glsl_material.emissive = vec4(0.30f,1.0f,0.45f,0.70f);
        renderer->AddMaterial(m);
        material_paddle_wide = renderer->FindMaterialIndex(m.name);
    }
    {
        Material m;
        m.name = "bo_wall";
        m.glsl_material.color = vec4(0.26f,0.28f,0.34f,1.0f);
        m.glsl_material.metallic = 0.70f;
        m.glsl_material.roughness = 0.30f;
        renderer->AddMaterial(m);
        material_wall = renderer->FindMaterialIndex(m.name);
    }
    {   //The panel the whole arena casts its shadows onto. Dark, so lit bricks pop off it and the
        //shield has something to bloom against.
        Material m;
        m.name = "bo_back";
        m.glsl_material.color = vec4(0.045f,0.055f,0.085f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 0.92f;
        renderer->AddMaterial(m);
        material_back = renderer->FindMaterialIndex(m.name);
    }
    {
        Material m;
        m.name = "bo_text";
        m.glsl_material.color = vec4(0.86f,0.90f,0.98f,1.0f);
        m.glsl_material.metallic = 0.15f;
        m.glsl_material.roughness = 0.48f;
        m.glsl_material.emissive = vec4(0.80f,0.86f,0.98f,0.55f);
        renderer->AddMaterial(m);
        material_text = renderer->FindMaterialIndex(m.name);
    }
    {   //The banner. emissive.w has to stay near 1 here: at the 2-3 a flash uses, every channel
        //clips and the colour comes out white, and a banner that is merely bright is not worth
        //losing its colour for.
        Material m;
        m.name = "bo_text_hot";
        m.glsl_material.color = vec4(1.0f,0.55f,0.25f,1.0f);
        m.glsl_material.metallic = 0.0f;
        m.glsl_material.roughness = 0.40f;
        m.glsl_material.emissive = vec4(1.0f,0.42f,0.14f,1.05f);
        renderer->AddMaterial(m);
        material_text_hot = renderer->FindMaterialIndex(m.name);
    }

    static const vec4 capsule_colours[POWERUP_COUNT] = {
        vec4(0.35f,1.00f,0.45f,1.0f),   //wide paddle
        vec4(0.35f,0.72f,1.00f,1.0f),   //slow ball
        vec4(0.30f,0.95f,1.00f,1.0f),   //shield
        vec4(1.00f,0.35f,0.85f,1.0f)    //multiball
    };
    for (int i = 0; i < POWERUP_COUNT; i++){
        Material m;
        char name[32];
        snprintf(name,sizeof(name),"bo_capsule%i",i);
        m.name = name;
        m.glsl_material.color = capsule_colours[i];
        m.glsl_material.metallic = 0.35f;
        m.glsl_material.roughness = 0.25f;
        //Brightly emissive: a capsule has to be visible while it is falling past a wall of
        //brightly coloured bricks, and it is competing with them for attention.
        m.glsl_material.emissive = vec4(capsule_colours[i].x,capsule_colours[i].y,capsule_colours[i].z,1.1f);
        renderer->AddMaterial(m);
        material_capsule[i] = renderer->FindMaterialIndex(m.name);
    }
}

void ApplicationBreakout::BuildArena(){
    //Walls and the back panel. The walls carry static box colliders, which is what gives the
    //debris and the capsules something to bounce off - the BALL never touches them, because it
    //resolves its own collisions against the same three planes in breakout/Field.cpp.
    const float mid_y = (BREAKOUT_DEATH_Y + BREAKOUT_FIELD_TOP) * 0.5f;
    const float span_y = BREAKOUT_FIELD_TOP - BREAKOUT_DEATH_Y + WALL_THICKNESS * 2.0f;
    const float span_x = BREAKOUT_FIELD_RIGHT - BREAKOUT_FIELD_LEFT + WALL_THICKNESS * 2.0f;

    struct ArenaPart{
        const char* name;
        vec3 position;
        vec3 scale;
    };
    const ArenaPart parts[] = {
        { "Wall Left",  vec3(BREAKOUT_FIELD_LEFT  - WALL_THICKNESS * 0.5f,mid_y,ARENA_Z),
                        vec3(WALL_THICKNESS,span_y,WALL_DEPTH) },
        { "Wall Right", vec3(BREAKOUT_FIELD_RIGHT + WALL_THICKNESS * 0.5f,mid_y,ARENA_Z),
                        vec3(WALL_THICKNESS,span_y,WALL_DEPTH) },
        { "Wall Top",   vec3((BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f,
                             BREAKOUT_FIELD_TOP + WALL_THICKNESS * 0.5f,ARENA_Z),
                        vec3(span_x,WALL_THICKNESS,WALL_DEPTH) }
    };
    for (int i = 0; i < (int)(sizeof(parts)/sizeof(parts[0])); i++){
        Object* part = MakeBoxObject(unit_mesh,main_scene,parts[i].name,
                                     parts[i].position,parts[i].scale,material_wall);
        if (!part){
            continue;
        }
        Physics* p = part->AddPhysics(main_scene->physics_world);
        if (p){
            //A body from AddPhysics starts STATIC with gravity off, which is exactly what a wall
            //wants, so there is nothing to opt into. Half extents are world units.
            p->AddBoxCollider(parts[i].scale * 0.5f,vec3(),quat().identity(),1.0f);
            p->SetBounciness(0.35f);
            p->SetFrictionCoefficient(0.6f);
            //Set AFTER the collider exists: Object::SetCollisionCategoryBits walks the body's
            //colliders, so calling it on a body that has none yet does nothing at all.
            part->SetCollisionCategoryBits(BREAKOUT_CAT_ARENA);
            part->SetCollideWithMaskBits(BREAKOUT_CAT_DEBRIS | BREAKOUT_CAT_CAPSULE);
        }
    }

    //A floor well below the death line, so debris and uncaught capsules have something to land on
    //and pile up against instead of falling forever. Out of shot; it exists for the solver, not
    //for the player.
    Object* floor = MakeBoxObject(unit_mesh,main_scene,"Arena Floor",
                                  vec3((BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f,-7.0f,ARENA_Z),
                                  vec3(span_x,1.0f,WALL_DEPTH * 2.0f),material_wall);
    if (floor){
        Physics* p = floor->AddPhysics(main_scene->physics_world);
        if (p){
            p->AddBoxCollider(vec3(span_x * 0.5f,0.5f,WALL_DEPTH),vec3(),quat().identity(),1.0f);
            p->SetBounciness(0.2f);
            p->SetFrictionCoefficient(0.9f);
            floor->SetCollisionCategoryBits(BREAKOUT_CAT_ARENA);
            floor->SetCollideWithMaskBits(BREAKOUT_CAT_DEBRIS | BREAKOUT_CAT_CAPSULE);
        }
    }

    //The back panel. No collider - debris is meant to tumble forward out of the arena, and a wall
    //behind it only ever produces chunks wedged in a corner.
    MakeBoxObject(unit_mesh,main_scene,"Back Panel",
                  vec3((BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f,mid_y,BACK_Z),
                  //Generously oversized: at this camera angle a panel merely big enough to catch
                  //the arena's shadows has its own corners in shot, and a visible edge on the
                  //backdrop makes the whole scene read as a diorama on a table.
                  vec3(span_x + 46.0f,span_y + 34.0f,0.5f),material_back);
}

void ApplicationBreakout::BuildBricks(){
    /*
        One object per CELL, made once and hidden while the cell is empty.

        88 hidden cubes cost nothing, and it removes a whole class of bug: the view is rebuilt
        from the rules array every tick, so there is no incremental update to get wrong and the
        display can never disagree with the game. It also means a brick's rigid body is created
        once, at a moment when nothing is iterating the physics world, rather than 88 times a
        level while a ball is in flight.
    */
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        for (int c = 0; c < BREAKOUT_COLS; c++){
            char name[32];
            snprintf(name,sizeof(name),"Brick %i,%i",c,r);

            //Subclassed rather than hung off a parallel array, which is how this engine wants an
            //app to carry its own data on an object (see the note at the top of core/Object.h).
            //The reverse lookup - click a brick in the viewport, find out which one it is - is
            //then a dynamic_cast rather than a search.
            Brick* brick = new Brick();
            brick->col = c;
            brick->row = r;
            brick->name = name;
            brick->SetMesh(unit_mesh);
            brick->SetPosition(vec3(game.BrickCenterX(c),game.BrickCenterY(r),ARENA_Z));
            brick->SetScale(vec3(BRICK_VISUAL_W,BRICK_VISUAL_H,BRICK_DEPTH));
            brick->SetMaterialSlot(0,material_brick_row[r]);
            main_scene->AddObject(brick);
            brick->Hide();

            Physics* p = brick->AddPhysics(main_scene->physics_world);
            if (p){
                //The collider is the FULL cell, not the visual (which is inset a little so
                //neighbouring bricks have a seam). Debris should bounce off a flush wall.
                p->AddBoxCollider(vec3(BREAKOUT_BRICK_W * 0.5f,BREAKOUT_BRICK_H * 0.5f,BRICK_DEPTH * 0.5f),
                                  vec3(),quat().identity(),1.0f);
                p->SetBounciness(0.45f);
                p->SetFrictionCoefficient(0.5f);
                //Debris only. A capsule that could land on a brick would sit there, asleep, until
                //its reap timer ran out - see the note on the category bits in the header.
                brick->SetCollisionCategoryBits(BREAKOUT_CAT_BRICK);
                brick->SetCollideWithMaskBits(BREAKOUT_CAT_DEBRIS);
                //Switched on and off by SyncBrickView as the cell fills and empties. A dead
                //brick's collider staying in the world would have debris bouncing off gaps.
                p->SetActive(false);
            }
            brick_objects[r][c] = brick;
        }
    }
}

void ApplicationBreakout::BuildBallsAndPaddle(){
    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        Object* ball = new Object();
        ball->name = "Ball";
        ball->SetMesh(ball_mesh);
        ball->SetMaterialSlot(0,material_ball);
        ball->SetPickability(false);
        main_scene->AddObject(ball);
        ball->Hide();
        ball_objects[i] = ball;
    }

    /*
        The paddle is a KINEMATIC rigid body, and that is the interesting part of it.

        It could have been a plain object - the ball resolves against it by hand, so nothing in
        the core loop needs a collider. But the capsules are real falling bodies that have to be
        CAUGHT, and the debris has to be shoved out of the way rather than passed through. A
        kinematic body driven by VELOCITY (not by teleporting it) is exactly the shape for that,
        and it is the same mechanism Scene::MoveObjectOverTicks uses for a physics-driven motion:
        each tick gets the velocity that carries the body to where the rules say it should be, so
        by the end of the tick it is exactly there, the solver saw a moving surface, and
        Object::UpdatePhysicsState syncs the visual from the body with no lag at all.
    */
    paddle_object = MakeBoxObject(unit_mesh,main_scene,"Paddle",
                                  vec3(game.paddle_x,BREAKOUT_PADDLE_Y,ARENA_Z),
                                  vec3(BREAKOUT_PADDLE_W,BREAKOUT_PADDLE_H,PADDLE_DEPTH),
                                  material_paddle);
    if (paddle_object){
        Physics* p = paddle_object->AddPhysics(main_scene->physics_world);
        if (p){
            p->AddBoxCollider(vec3(BREAKOUT_PADDLE_W * 0.5f,BREAKOUT_PADDLE_H * 0.5f,PADDLE_DEPTH * 0.5f),
                              vec3(),quat().identity(),4.0f);
            p->SetBodyType(rp3d::BodyType::KINEMATIC);
            p->SetBounciness(0.5f);
            p->SetFrictionCoefficient(0.4f);
            paddle_object->SetCollisionCategoryBits(BREAKOUT_CAT_PADDLE);
            paddle_object->SetCollideWithMaskBits(BREAKOUT_CAT_DEBRIS | BREAKOUT_CAT_CAPSULE);
        }
    }

    //A warm point light riding the ball. Two lines, and it is the single most striking thing in
    //the scene: the arena is dark, so the ball lights the bricks it is about to hit and its
    //shadow sweeps across the back panel. Shadowed through the occluder field below.
    ball_light = new PointLight();
    ball_light->name = "Ball Light";
    ball_light->SetPosition(vec3(11.0f,10.0f,3.0f));
    ball_light->color = vec3(1.0f,0.80f,0.48f);
    //Bright for a point light, because the shader applies a point light's brightness twice - once
    //as the distance falloff and again as radiance - and it has to hold its own against a sun
    //at 4.2. See the note on shading_brightness in shaders/default.frag.
    ball_light->brightness = 4.5f;
    ball_light->f_casts_shadow = true;
    main_scene->AddObject(ball_light);
}

void ApplicationBreakout::BuildShield(){
    /*
        Deliverable 2: the custom material pass.

        Four lines of wiring and then everything interesting happens in the .frag. The shader is
        registered with the renderer, the mesh is tagged with the index it came back with, and the
        uniform callback is where this tick's game state reaches the GPU.
    */
    shield_shader = new Shader("shaders/default.vert","shaders/breakout_shield.frag");
    //Reuse default.vert. Every custom shader in the repo does and there is a sharp reason:
    //CustomShaderPass calls Setmat4("mat_worldcam") on every registered custom shader every
    //frame, and Setmat4 on a missing uniform calls debug->Fatal -> exit(1). default.vert uses
    //that uniform for gl_Position, which keeps it live in the linked program for free.
    shield_shader->uniform_callback = std::bind(&ApplicationBreakout::SetShieldUniforms,this);
    shield_shader_index = renderer->AddCustomShader(shield_shader);

    shield_mesh = MakeQuad(SHIELD_QUAD_W,SHIELD_QUAD_H);
    if (!shield_mesh){
        debug->Fatal("Failed to build the shield quad\n");
    }
    assetmanager->AddNewAsset("bo_shield",shield_mesh);
    //mesh_mode is a property of the MESH, which is why this quad is not shared with anything.
    shield_mesh->mesh_mode = MESH_MODE_SHADER;
    shield_mesh->custom_shader_index = shield_shader_index;

    shield_object = new Object();
    shield_object->name = "Shield";
    shield_object->SetMesh(shield_mesh);
    //MakeQuad lies in XY facing +Z, which is already the plane this game plays in, so there is
    //nothing to rotate. Centred on the collision line the rules bounce a ball off.
    shield_object->SetPosition(vec3((BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f,
                                    BREAKOUT_SHIELD_Y,ARENA_Z));
    //No material: the shader computes its own colour and never touches the material buffer.
    shield_object->SetMaterialSlot(0,-1);
    //A MESH_MODE_SHADER mesh never reaches the object-id buffer, so it could not be picked
    //anyway - but nothing about a force field is meant to be clickable.
    shield_object->SetPickability(false);
    main_scene->AddObject(shield_object);
}

void ApplicationBreakout::BuildTextLabels(){
    /*
        Everything the game says in words is geometry in the scene, not ImGui. ImGui is the debug
        layer: a game should be able to ship without it, and these labels are lit and shadowed
        like a brick and appear in an include_ui:false screenshot, which the panels do not.
    */
    if (!LoadGlyphSetFromGLB(glyphs,"meshes/glyphs_unispace.glb",0.509167f,1.0f)){
        //Not fatal. Without glyphs the game still plays perfectly; it just says nothing.
        debug->Warn("No glyphs loaded - the game will play without labels\n");
        return;
    }

    struct LabelSetup{
        int         id;
        vec3        position;
        float       scale;
        int         align;
        bool        f_hot;
        const char* text;
    };
    const LabelSetup setup[] = {
        { BREAKOUT_LABEL_SCORE,  vec3(TEXT_X,23.6f,TEXT_Z), TEXT_SCALE, TEXT_ALIGN_LEFT,   false, "SCORE\n0" },
        { BREAKOUT_LABEL_LIVES,  vec3(TEXT_X,20.8f,TEXT_Z), TEXT_SCALE, TEXT_ALIGN_LEFT,   false, "BALLS 3" },
        { BREAKOUT_LABEL_LEVEL,  vec3(TEXT_X,19.2f,TEXT_Z), TEXT_SCALE, TEXT_ALIGN_LEFT,   false, "LEVEL 1" },
        { BREAKOUT_LABEL_SHIELD, vec3(TEXT_X,17.6f,TEXT_Z), TEXT_SCALE, TEXT_ALIGN_LEFT,   false, "SHIELD" },
        { BREAKOUT_LABEL_COMBO,  vec3(TEXT_X,15.0f,TEXT_Z), TEXT_SCALE, TEXT_ALIGN_LEFT,   true,  "" },
        //Centred over the arena, on the panel rather than out in the play plane - so the ball and
        //the paddle pass in FRONT of it, which is what a title card should do.
        { BREAKOUT_LABEL_BANNER, vec3(11.0f,11.5f,TEXT_Z), 1.45f,      TEXT_ALIGN_CENTER, true,  "PRESS SPACE" }
    };

    for (int i = 0; i < (int)(sizeof(setup)/sizeof(setup[0])); i++){
        const LabelSetup& ls = setup[i];
        BreakoutLabel& label = labels[ls.id];

        label.object = new Object();
        label.object->name = "Label";
        label.object->SetPosition(ls.position);
        label.object->SetMaterialSlot(0,ls.f_hot ? material_text_hot : material_text);
        //Text must not swallow a pick aimed at a brick behind it, and there is nothing useful to
        //inspect about a label.
        label.object->SetPickability(false);
        //Nor should it throw a shadow. A caption is extruded geometry, so held any distance clear
        //of the panel it drops a crisp second copy of itself onto it, which reads as a rendering
        //fault rather than as a shadow. TEXT_Z presses the labels almost flat against the panel
        //to keep that offset down to a few pixels; this is the part of that workaround the engine
        //can now do properly, and it is what frees a caption to sit anywhere it looks best.
        label.object->SetCastsShadow(false);
        label.scale = ls.scale;
        label.align = ls.align;

        //Before AddObject, so the object is never in the scene without a mesh to batch.
        SetLabelText(ls.id,ls.text);
        main_scene->AddObject(label.object);
    }
}

void ApplicationBreakout::SetLabelText(int label_id, const char* text){
    if (label_id < 0 || label_id >= BREAKOUT_LABEL_COUNT || !text){
        return;
    }
    BreakoutLabel& label = labels[label_id];
    if (!label.object){
        return;
    }
    //Most frames change nothing, and comparing a string is far cheaper than rebuilding a mesh.
    //This is what makes it safe to call unconditionally every single frame.
    if (strncmp(label.text,text,sizeof(label.text)) == 0){
        return;
    }
    snprintf(label.text,sizeof(label.text),"%s",text);

    TextLayout layout;
    layout.scale = label.scale;
    layout.align = label.align;
    //Slot 0, which is where this object's material was assigned. The glyph meshes carry matid 0
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
    The occluder field, which is what lets the ball's point light cast a shadow at all.

    A directional light is the one kind the engine can shadow with a depth map, because every ray
    is parallel and one viewpoint covers every receiver. A point light has no single viewpoint, and
    the textbook answer - six cube map faces per light per frame - buys omnidirectionality that a
    fixed camera cannot spend. So this light is shadowed by marching a top-down occluder field
    instead. A wall of extruded bricks on a flat panel is very nearly the prism field that
    technique describes exactly, so it is a good fit here.

    Worth knowing, and reported on: backlog item 39 says a column of the field is a single slab
    between the highest and lowest surface in it, so a BALL in front of a brick merges with that
    brick and light cannot pass between the two. In this app the ball sits at the same z as the
    bricks, so that case is never reached - which is luck, not design.
*/
void ApplicationBreakout::SetupFieldShadows(){
    /*
        The field's own camera, orthographic down -Z, because THIS app's world is the XY plane
        with z toward the viewer. The engine fixes no up axis, which is why the axis is passed
        explicitly rather than assumed.

        Not the scene camera, tempting as that is: UpdateCameraShake moves that one on a big
        clear, and that would drag the field's world mapping under the shadows for the length of
        the shake. Same reason the ship app's cloud camera is separate.

        zoom is a half-extent in world units and the map is square: 17 around (11,13) reaches
        x [-6,28] and y [-4,30], covering the arena, the shield and the text column.
    */
    field_camera = new Camera();
    field_camera->name = "Field Camera";
    field_camera->SetupOrthographic(512,512,17.0f,0.1f,140.0f);
    field_camera->SetPosition(vec3(11.0f,13.0f,50.0f));
    field_camera->SetLookAt(vec3(11.0f,13.0f,0.0f));
    field_camera->CalculateLookatMatrix();

    if (!renderer->EnableFieldShadows(field_camera,vec3(0,0,1),512)){
        debug->Err("Failed to enable field shadows\n");
    }
}

void ApplicationBreakout::SetupCamera(){
    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_PERSPECTIVE);
    /*
        Perspective, not orthographic, and tilted.

        Tetris looks straight down -Z with an orthographic camera and is right to: a board of
        cubes on a grid wants no foreshortening at all. A paddle game does not have that
        constraint and gains a lot from breaking it - from down here the bricks have visible
        sides, the walls have thickness, and the ball's light throws its shadow along the wall
        rather than flat behind it. The cost is that world units and screen pixels stop being
        proportional, which matters only for mouse picking, and nothing in this game picks.

        fov is VERTICAL and in degrees (fmat4::perspectivematrix takes toradians(fov)/2). 38
        degrees at 44 units from the target shows about 30 world units of height, which is the
        27-unit arena plus its walls with a little margin.
    */
    camera->SetupPerspective(renderer->width,renderer->height,38.0f,0.5f,220.0f);
    camera->SetPosition(vec3(camera_target.x,camera_target.y - 18.5f,40.0f));
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();
}

void ApplicationBreakout::SetupInput(){
    InputController* input = main_scene->inputcontroller;

    //Two mappings for each direction, because muscle memory differs and both cost nothing:
    //KeyState::f_isdown counts HELD MAPPINGS rather than being a boolean, so an action stays
    //down while either of its keys is.
    input->AddKeyMap(VK_LEFT,INPUT_BREAKOUT_LEFT);
    input->AddKeyMap('A',INPUT_BREAKOUT_LEFT);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_BREAKOUT_LEFT);
    input->AddKeyMap(VK_RIGHT,INPUT_BREAKOUT_RIGHT);
    input->AddKeyMap('D',INPUT_BREAKOUT_RIGHT);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_RIGHT,INPUT_BREAKOUT_RIGHT);

    input->AddKeyMap(VK_SPACE,INPUT_BREAKOUT_LAUNCH);
    input->AddKeyMap(VK_UP,INPUT_BREAKOUT_LAUNCH);
    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_BREAKOUT_LAUNCH);

    input->AddKeyMap('R',INPUT_BREAKOUT_RESTART);
    input->AddKeyMap(VK_F1,INPUT_BREAKOUT_TOGGLE_UI);
    input->AddKeyMap('M',INPUT_BREAKOUT_TOGGLE_MOUSE);
    input->AddKeyMap(VK_F5,INPUT_BREAKOUT_RELOAD_SHADER);
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature.
    input->AddKeyMap('P',INPUT_PAUSE);

    /*
        The analog steer. A paddle is the one classic control genuinely better analog than
        digital, so the left stick drives a SCALAR AXIS rather than a pair of fake key presses -
        which also means InputController::HoldAxis can steer it from a script exactly as a thumb
        does, for a duration counted in simulation ticks.

        The default dead zone of 50 out of 32767 is far too small for a real stick: at that
        threshold a worn thumbstick's rest position registers as a steady deflection and the
        paddle drifts into the wall on its own. 6000 is about 18%, which is the usual figure.
    */
    //Binds the left stick's X axis AND declares the action as a scalar axis, so GetAxis and a
    //scripted HoldAxis both work on it. That used to take a second AddKeyMap(0,...) call to force
    //a KeyState into existence, an idiom nothing wrote down and whose absence failed silently.
    input->AddGamePadMap(0,INPUT_BREAKOUT_STEER,6000);
}

void ApplicationBreakout::RegisterCommandHandlers(){
    //Restarting is intent from outside the simulation - the HUD button, an MCP call, later a
    //replay - so it goes on the command queue rather than being done on the caller's thread. The
    //handler runs on the physics thread at the top of a tick with physics_mutex held, which is
    //the only place it is safe to throw the game away.
    main_scene->RegisterCommandHandler(BREAKOUT_CMD_RESTART,
        [this](const SimCommand& cmd) -> objectid_t {
            //The seed travels IN the command, so a recorded restart builds the same levels and
            //drops the same prizes on replay.
            uint32_t seed = (uint32_t)cmd.value[0];
            NewGame(seed ? seed : next_auto_seed++);
            return OBJECTID_INVALID;
        });
}

void ApplicationBreakout::NewGame(uint32_t seed){
    current_seed = seed;
    game.NewGame(seed);

    //Throw away everything the previous game left in the world.
    for (size_t i = 0; i < debris.size(); i++){
        if (debris[i].object){
            debris[i].object->Destroy();
        }
    }
    debris.clear();
    for (size_t i = 0; i < capsules.size(); i++){
        if (capsules[i].object){
            capsules[i].object->Destroy();
        }
    }
    capsules.clear();
    staged_catches.clear();
    //Object::Destroy only MARKS; this is what actually frees them and takes their rigid bodies
    //out of the physics world. Safe here: we are on the physics thread with physics_mutex held,
    //so the render thread is not walking the object list.
    main_scene->DeleteDestroyedObjects();

    for (int i = 0; i < BREAKOUT_MAX_RIPPLES; i++){
        ripples[i].age = -1.0f;
    }
    next_ripple = 0;
    last_hit_tick = 0;
    last_hit_offset = 0.0f;
    last_hit_angle = 0.0f;
    last_hit_speed = 0.0f;
    shake_amount = 0.0f;
    shake_ticks = 0;
    mouse_paddle_delta = 0.0f;
    probe_ticks_left = 0;
    probe_escapes = 0;

    SyncBrickView();
    SyncBallView();
    SyncPaddleView();
    PublishSnapshot();
    debug->Ok("New game, seed %u\n",seed);
}

//--- The view pass ------------------------------------------------------------------------------

//Chrome only, and it runs on every pass of the physics loop including the ones that simulate
//nothing because the game is paused - which is exactly when you most want to be able to open a
//panel or reload a shader.
void ApplicationBreakout::UpdateView(void){
    if (!main_scene){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    if (input->WasKeyReleased(INPUT_BREAKOUT_TOGGLE_UI)){
        f_show_engine_ui = !f_show_engine_ui;
        f_show_scene_window = f_show_engine_ui;
        f_show_inspector_window = f_show_engine_ui;
        f_show_engine_window = f_show_engine_ui;
    }
    if (input->WasKeyReleased(INPUT_BREAKOUT_TOGGLE_MOUSE)){
        f_mouse_control = !f_mouse_control;
        debug->Info("Mouse control %s\n",f_mouse_control ? "on" : "off");
    }
    if (input->WasKeyReleased(INPUT_BREAKOUT_RELOAD_SHADER)){
        //NOT a GL call: it only raises a flag that PreRender acts on. UpdateView runs on the
        //physics thread, which may not touch the context at all.
        f_shader_reload_requested = true;
    }
}

//--- The tick -----------------------------------------------------------------------------------

void ApplicationBreakout::RunSimulationTick(void){
    if (!main_scene){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //Restart is a game action, so it is read whether or not a game is in progress. Already on the
    //physics thread inside the tick, so this is a direct call rather than a command - a command
    //would be a round trip through the queue to arrive back here one tick later.
    if (input->WasKeyReleased(INPUT_BREAKOUT_RESTART)){
        NewGame(next_auto_seed++);
        return;
    }

    BreakoutInput intent;
    GatherInput(intent);
    UpdateBallProbe();
    if (probe_ticks_left > 0){
        //Serve automatically while the probe is running. Without this the run stalls the first
        //time the ball is lost - the game sits in READY waiting for a launch that a measurement
        //harness is never going to press - and every remaining tick measures an empty arena.
        //Found by a probe that reported four broken bricks in 1500 ticks at 260 units/second.
        intent.f_launch = true;
    }

    BreakoutEvents events;
    game.Tick(intent,events);
    HandleEvents(events);

    UpdateCapsules();
    UpdateDebris();
    UpdateRipples();
    SyncBrickView();
    SyncBallView();
    SyncPaddleView();
    UpdateCameraShake();
    PublishSnapshot();
}

void ApplicationBreakout::GatherInput(BreakoutInput& out){
    InputController* input = main_scene->inputcontroller;

    //Act on input only when it is ours to act on: this window in front, or a scripted hold
    //running (which is not OS input, and happens precisely when the window is NOT in front).
    //One predicate owned by the engine - see InputController::IsInputLive.
    if (!input->IsInputLive()){
        //Drop whatever mouse movement piled up while we were not the active window, or alt-tabbing
        //back would fling the paddle across the arena.
        mouse_paddle_delta = 0.0f;
        input->GetDelta(INPUT_MOUSE_DELTA_X);
        return;
    }

    //--- the throttle -----------------------------------------------------------------------
    float axis = 0.0f;
    if (input->IsKeyDown(INPUT_BREAKOUT_LEFT)){     axis -= 1.0f; }
    if (input->IsKeyDown(INPUT_BREAKOUT_RIGHT)){    axis += 1.0f; }
    //One read for one axis, whoever is driving it. PollGamepad submits the stick as an ordinary
    //axis event now, so a thumb and a scripted HoldAxis land in the same KeyState and the last
    //one to move wins - where this used to have to read the two paths separately and ADD them,
    //which also meant a stick and a script could sum past full deflection.
    axis += input->GetAxis(INPUT_BREAKOUT_STEER);
    out.paddle_axis = clamp(axis,-1.0f,1.0f);

    //--- the mouse --------------------------------------------------------------------------
    //INPUT_MOUSE_DELTA_X is raw, unaccelerated and unclipped: it keeps reporting once the pointer
    //is against the edge of the screen, which is exactly what a paddle wants and is not what the
    //cursor position gives. Read once per tick; InputController::Tick clears it afterwards.
    int32_t raw_dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    if (f_mouse_control){
        mouse_paddle_delta = clamp((float)raw_dx * MOUSE_TO_WORLD,-MOUSE_MAX_PER_TICK,MOUSE_MAX_PER_TICK);
    }else{
        mouse_paddle_delta = 0.0f;
    }
    out.paddle_delta = mouse_paddle_delta;

    //--- the launch -------------------------------------------------------------------------
    //Edge-triggered: one press, one launch, however long the key is held.
    out.f_launch = input->WasKeyReleased(INPUT_BREAKOUT_LAUNCH);
}

void ApplicationBreakout::HandleEvents(const BreakoutEvents& events){
    if (f_sound_enabled && soundsystem){
        if (events.f_bounced_wall || events.f_bounced_ceiling){
            soundsystem->Play("wall",false,0.30f);
        }
        if (events.f_bounced_paddle){
            //Louder for a hit out at the edge of the paddle, which is the risky one and the one
            //worth noticing.
            soundsystem->Play("paddle",false,0.35f + 0.35f * fabsf(events.paddle_hit_offset));
        }
        if (events.f_shield_saved){
            soundsystem->Play("shield",false,0.95f);
        }
        if (events.f_prize_dropped){
            soundsystem->Play("powerup",false,0.6f);
        }
        if (events.f_life_lost){
            soundsystem->Play("lost",false,1.0f);
        }
        //One handle per brick broken this tick, round-robined over three. A source cannot overlap
        //itself, so two bricks on one handle would be one sound - and a multiball rally through a
        //checkerboard breaks bricks in pairs constantly.
        static const char* brick_handles[3] = { "brick_a","brick_b","brick_c" };
        int played = 0;
        for (size_t i = 0; i < events.brick_hits.size() && played < 3; i++){
            //Every hit, not only the fatal ones: a silent bounce off an armoured brick reads as
            //the ball having missed it.
            soundsystem->Play(brick_handles[played],false,
                              events.brick_hits[i].f_destroyed ? 0.60f : 0.35f);
            played++;
        }
    }

    for (size_t i = 0; i < events.brick_hits.size(); i++){
        const BreakoutBrickHit& hit = events.brick_hits[i];
        if (hit.f_destroyed){
            SpawnBrickDebris(hit);
        }
    }

    if (events.f_prize_dropped){
        SpawnCapsule(events.prize_x,events.prize_y);
    }

    if (events.f_bounced_paddle){
        //Written down on the tick it happens, because that is the only tick it exists on - a tool
        //that polls could never catch it. See BreakoutSnapshot::last_hit_tick.
        last_hit_tick = main_scene->GetPhysicsTick();
        last_hit_offset = events.paddle_hit_offset;
        //Taken from the EVENT, not from the ball. The rules capture it at the moment of the
        //bounce; reading the ball here would report whatever it hit next within the same tick.
        last_hit_angle = events.paddle_hit_angle;
        last_hit_speed = game.balls[0].speed;
    }

    if (events.f_shield_saved){
        //Claim a ripple slot for the shader. Round-robin rather than "find a free one": three
        //saves in quick succession should show three rings, and the oldest is the right one to
        //lose if there are more than three.
        ripples[next_ripple].x = events.shield_impact_x;
        ripples[next_ripple].age = 0.0f;
        next_ripple = (next_ripple + 1) % BREAKOUT_MAX_RIPPLES;
        shake_amount = max(shake_amount,0.16f);
        shake_ticks = max(shake_ticks,12);
    }

    if (events.f_level_cleared){
        shake_amount = max(shake_amount,0.30f);
        shake_ticks = max(shake_ticks,24);
        debug->Ok("Level %i cleared: score %i, %i saves\n",game.level,game.score,game.shield_saves);
    }
    if (events.f_life_lost){
        shake_amount = max(shake_amount,0.22f);
        shake_ticks = max(shake_ticks,18);
    }
    if (events.f_game_over){
        debug->Info("Game over: score %i, level %i, %i bricks, best combo %i\n",
                    game.score,game.level,game.bricks_broken,game.best_combo);
    }
}

//--- The physics half ---------------------------------------------------------------------------

void ApplicationBreakout::SpawnBrickDebris(const BreakoutBrickHit& hit){
    /*
        A destroyed brick bursts into four dynamic chunks that tumble down the arena and pile up
        against the bricks still standing - which they can do because those bricks carry real
        static colliders. Purely cosmetic: nothing here can touch the rules, which is the only
        way to have a solver in a paddle game without ruining the feel of it.

        Safe to create bodies here because RunSimulationTick is not inside the physics step. The
        rule (core/SimCommand.h, ApplicationShip.cpp:995) is that a body must never be created
        from inside a CONTACT CALLBACK, where rp3d is mid-iteration over its own arrays - see
        onContact below, which stages instead.
    */
    if (!f_debris_enabled || !main_scene->physics_world || !unit_mesh){
        return;
    }
    if ((int)debris.size() >= BREAKOUT_MAX_DEBRIS){
        return;
    }

    int row = (int)clamp((float)hit.row,0.0f,(float)(BREAKOUT_ROWS - 1));
    int material = material_brick_row[row];
    if (hit.type_before == BRICK_TOUGH){        material = material_brick_tough; }
    else if (hit.type_before == BRICK_ARMOURED){material = material_brick_armoured; }
    else if (hit.type_before == BRICK_PRIZE){   material = material_brick_prize; }

    uint64_t now = main_scene->GetPhysicsTick();
    for (int i = 0; i < BREAKOUT_DEBRIS_PER_BRICK; i++){
        //Two by two across the brick's own footprint, so the chunks start where the brick was
        //rather than all at its centre.
        float ox = ((i & 1) ? 0.42f : -0.42f);
        float oy = ((i & 2) ? 0.21f : -0.21f);
        Object* chunk = MakeBoxObject(unit_mesh,main_scene,"Debris",
                                      vec3(hit.x + ox,hit.y + oy,ARENA_Z + 0.1f),
                                      vec3(0.72f,0.38f,0.44f),material);
        if (!chunk){
            continue;
        }
        chunk->SetPickability(false);
        Physics* p = chunk->AddPhysics(main_scene->physics_world);
        if (p){
            p->AddBoxCollider(vec3(0.36f,0.19f,0.22f),vec3(),quat().identity(),1.0f);
            //A body starts STATIC with gravity off - dynamics is opt-in.
            p->SetStatic(false);
            p->SetGravityEnabled(true);
            p->SetBounciness(0.40f);
            p->SetFrictionCoefficient(0.5f);
            chunk->SetCollisionCategoryBits(BREAKOUT_CAT_DEBRIS);
            //Debris keeps colliding with everything: a shower that piles up against the bricks
            //still standing is the reason those bricks carry colliders at all.
            chunk->SetCollideWithMaskBits(BREAKOUT_CAT_ARENA | BREAKOUT_CAT_BRICK |
                                          BREAKOUT_CAT_DEBRIS | BREAKOUT_CAT_PADDLE);
            //Thrown out along the face the ball came in on, plus a spread from the chunk's own
            //offset. Deterministic: a function of the hit, not of a random draw, so two runs of
            //the same inputs produce the same shower.
            vec3 burst = vec3(hit.nx * -3.4f + ox * 5.0f,
                              hit.ny * -3.4f + oy * 5.0f + 2.2f,
                              3.0f + (i & 1) * 1.5f);
            p->SetVelocity(burst);
            p->SetAngularVelocity(vec3(oy * 26.0f,ox * 20.0f,-ox * 18.0f));
        }
        BreakoutDebris entry;
        entry.object = chunk;
        entry.reap_tick = now + BREAKOUT_DEBRIS_TICKS;
        debris.push_back(entry);
    }
}

/*
    A power-up capsule: a real falling rigid body that has to be caught.

    This is the piece of the game where the physics engine is doing GAMEPLAY work rather than
    garnish. The capsule's trajectory is the solver's - it can clip a brick on the way down and
    be deflected, it can bounce off a wall, and where it lands is genuinely not predictable from
    the tick that dropped it. Catching it is resolved through onContact against the paddle's
    kinematic body, which means the paddle's shape and speed matter to the catch exactly as they
    matter to the ball.
*/
void ApplicationBreakout::SpawnCapsule(float x, float y){
    if (!main_scene->physics_world || !unit_mesh){
        return;
    }
    //Drawn from the rules' own generator, on the physics thread inside the tick, so the same seed
    //and the same inputs drop the same power-ups. Application::rrand would NOT be safe here: it
    //is a single stream shared with anything the UI or an MCP thread draws from (backlog item
    //22's residual note), and an off-tick draw would shift it under the simulation.
    int kind = game.rng.GetInt(0,POWERUP_COUNT - 1);

    Object* capsule = MakeBoxObject(unit_mesh,main_scene,"Capsule",
                                    vec3(x,y,ARENA_Z + 0.2f),
                                    vec3(1.10f,0.44f,0.44f),material_capsule[kind]);
    if (!capsule){
        return;
    }
    capsule->SetPickability(false);
    Physics* p = capsule->AddPhysics(main_scene->physics_world);
    if (p){
        p->AddBoxCollider(vec3(0.55f,0.22f,0.22f),vec3(),quat().identity(),1.0f);
        p->SetStatic(false);
        p->SetGravityEnabled(true);
        p->SetBounciness(0.25f);
        p->SetFrictionCoefficient(0.35f);
        capsule->SetCollisionCategoryBits(BREAKOUT_CAT_CAPSULE);
        capsule->SetCollideWithMaskBits(BREAKOUT_CAT_ARENA | BREAKOUT_CAT_PADDLE);
        /*
            PINNED TO THE PLAY PLANE, and this is the fix for the bug that took longest to find.

            The capsule used to be given a little push toward the camera so it read as a falling
            object rather than a sliding decal. Over the second and a half it takes to fall, that
            0.8 u/s drifted it more than a unit out of z - past the far face of the paddle's
            collider - so it sailed through a paddle that was sitting directly underneath it and
            the catch never happened. Everything LOOKED right from the front, which is exactly the
            sort of bug a flat-on camera hides.

            Locking an axis outright is the honest fix for a 2D game inside a 3D solver: the body
            then cannot leave the plane no matter what shoves it.
        */
        p->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
        //Roll about z only, so the tumble stays in the plane the player is looking at.
        p->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));
        p->SetVelocity(vec3(0.0f,-1.2f,0.0f));
        p->SetAngularVelocity(vec3(0.0f,0.0f,1.6f));
    }

    BreakoutCapsule entry;
    entry.object = capsule;
    entry.kind = kind;
    entry.reap_tick = main_scene->GetPhysicsTick() + BREAKOUT_CAPSULE_TICKS;
    capsules.push_back(entry);
    debug->Info("Dropped a %s capsule at %.1f,%.1f\n",PowerupName(kind),x,y);
}

/*
    Physics thread, from INSIDE reactphysics3d's own step.

    Nothing is created, destroyed or moved here. rp3d is part-way through iterating its own arrays,
    and the engine's own apps learned that the hard way (ApplicationShip.cpp:995). All this does is
    write down which capsule touched the paddle; RunSimulationTick acts on it afterwards.

    ContactStart only. rp3d reports contacts BEFORE it solves them and then re-reports the same one
    as ContactStay for every tick the bodies stay touching, so an unfiltered handler would catch
    the same capsule three or four times.
*/
void ApplicationBreakout::onContact(const rp3d::CollisionCallback::CallbackData& data){
    for (uint32_t i = 0; i < data.getNbContactPairs(); i++){
        rp3d::CollisionCallback::ContactPair pair = data.getContactPair(i);
        if (pair.getEventType() != rp3d::CollisionCallback::ContactPair::EventType::ContactStart){
            continue;
        }
        Object* a = (Object*)pair.getBody1()->getUserData();
        Object* b = (Object*)pair.getBody2()->getUserData();
        if (!a || !b){
            continue;
        }
        //Object::AddPhysics stamps every body with its owning Object, so this cast is always
        //valid for anything in this world.
        Object* other = NULL;
        if (a == paddle_object){        other = b; }
        else if (b == paddle_object){   other = a; }
        if (!other){
            continue;
        }
        //Ids rather than pointers: the object may be destroyed between here and the drain.
        staged_catches.push_back(other->GetID());
    }
}

void ApplicationBreakout::UpdateCapsules(){
    uint64_t now = main_scene->GetPhysicsTick();
    bool f_any_destroyed = false;

    //Drain what onContact saw. A capsule may be reported more than once in a tick (two colliders,
    //two contact manifolds), so a caught one is removed from the list on the first match and the
    //rest of the ids simply find nothing.
    for (size_t s = 0; s < staged_catches.size(); s++){
        for (size_t i = 0; i < capsules.size(); i++){
            if (!capsules[i].object || capsules[i].object->GetID() != staged_catches[s]){
                continue;
            }
            game.ApplyPowerup(capsules[i].kind);
            if (f_sound_enabled && soundsystem){
                soundsystem->Play("powerup",false,1.0f);
            }
            //Catching one is worth points in its own right, so a risky catch is a real decision
            //rather than a free bonus.
            game.score += 250;
            capsules[i].object->Destroy();
            f_any_destroyed = true;
            capsules.erase(capsules.begin() + i);
            break;
        }
    }
    staged_catches.clear();

    for (size_t i = 0; i < capsules.size(); ){
        Object* object = capsules[i].object;
        //Also reaped once it has fallen well below the arena, so a capsule that missed the paddle
        //does not sit on the out-of-shot floor for the rest of the run.
        bool f_expired = (now >= capsules[i].reap_tick) ||
                         (object && object->GetWorldPosition().y < -4.0f);
        if (!f_expired){
            i++;
            continue;
        }
        if (object){
            object->Destroy();
            f_any_destroyed = true;
        }
        capsules.erase(capsules.begin() + i);
    }

    if (f_any_destroyed){
        main_scene->DeleteDestroyedObjects();
    }
}

void ApplicationBreakout::UpdateDebris(){
    uint64_t now = main_scene->GetPhysicsTick();
    bool f_any_destroyed = false;
    for (size_t i = 0; i < debris.size(); ){
        Object* object = debris[i].object;
        bool f_expired = (now >= debris[i].reap_tick) ||
                         (object && object->GetWorldPosition().y < -9.0f);
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
        //Object::Destroy only MARKS. Without this the chunks stop rendering but their rigid
        //bodies stay in the physics world for the life of the run - and this game makes a lot of
        //them. Safe here: RunSimulationTick holds physics_mutex, so the render thread is not
        //walking the object list. Open backlog item 23.
        main_scene->DeleteDestroyedObjects();
    }
}

//--- The view -----------------------------------------------------------------------------------

void ApplicationBreakout::SyncBrickView(){
    /*
        The brick array is the truth and these cubes are a view of it, rebuilt from scratch every
        tick. That is 88 cheap comparisons and it removes a whole class of bug: there is no
        incremental update to get wrong, so the display can never disagree with the rules.
    */
    for (int r = 0; r < BREAKOUT_ROWS; r++){
        for (int c = 0; c < BREAKOUT_COLS; c++){
            Brick* brick = brick_objects[r][c];
            if (!brick){
                continue;
            }
            int type = game.GetBrick(c,r);
            Physics* p = brick->GetPhysics();
            if (type == BRICK_EMPTY){
                if (brick->IsVisible()){
                    brick->Hide();
                    //Take its collider out of the world with it, or debris bounces off a gap.
                    if (p){ p->SetActive(false); }
                }
                continue;
            }
            int material = material_brick_row[r];
            if (type == BRICK_TOUGH){           material = material_brick_tough; }
            else if (type == BRICK_ARMOURED){   material = material_brick_armoured; }
            else if (type == BRICK_PRIZE){      material = material_brick_prize; }
            else if (type == BRICK_SOLID){      material = material_brick_solid; }

            if (!brick->IsVisible()){
                brick->Show();
                if (p){ p->SetActive(true); }
            }
            brick->SetMaterialSlot(0,material);
        }
    }
}

void ApplicationBreakout::SyncBallView(){
    //The ball with the lowest y, which is the one about to need the shield - that is where the
    //shader's bloom belongs and where the light is most useful.
    float lowest_y = 1e9f;
    vec3 lowest_position = shield_focus;
    bool f_any = false;

    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        Object* object = ball_objects[i];
        if (!object){
            continue;
        }
        const BreakoutBall& ball = game.balls[i];
        if (!ball.f_alive){
            object->Hide();
            continue;
        }
        vec3 position = vec3(ball.x,ball.y,ARENA_Z);
        object->Show();
        //Set directly rather than through Scene::MoveObjectOverTicks: the ball's position IS the
        //simulation, and interpolating a view toward it would be showing the player something
        //other than where the collisions are being resolved.
        object->SetPosition(position);
        f_any = true;
        if (ball.y < lowest_y){
            lowest_y = ball.y;
            lowest_position = position;
        }
    }

    if (f_any){
        shield_focus = lowest_position;
        if (ball_light){
            //Pulled forward off the play plane so it lights the FACES of the bricks rather than
            //grazing them, and so the occluder field has something to cast from.
            ball_light->SetPosition(lowest_position + vec3(0,0,2.6f));
        }
    }
}

void ApplicationBreakout::SyncPaddleView(){
    if (!paddle_object){
        return;
    }
    float half = game.PaddleHalfWidth();
    vec3 scale = paddle_object->GetScale();
    float want_w = half * 2.0f;

    if (fabsf(scale.x - want_w) > 0.001f){
        Physics* p = paddle_object->GetPhysics();
        if (p){
            //Physics::ScaleColliders takes the RATIO (new/old) per axis and rescales the box's
            //half extents with it, then recomputes centre of mass and inertia - rp3d's own shape
            //setters leave the mass properties stale. Called only on a real change, which is
            //twice per power-up rather than once a tick.
            p->ScaleColliders(vec3(want_w / max(scale.x,0.001f),1.0f,1.0f));
        }
        paddle_object->SetScale(vec3(want_w,BREAKOUT_PADDLE_H,PADDLE_DEPTH));
        paddle_object->SetMaterialSlot(0,(game.powerup_wide_ticks > 0) ? material_paddle_wide : material_paddle);
    }

    Physics* p = paddle_object->GetPhysics();
    if (!p){
        paddle_object->SetPosition(vec3(game.paddle_x,BREAKOUT_PADDLE_Y,ARENA_Z));
        return;
    }
    /*
        Driven by VELOCITY, not teleported.

        The tick order is RunSimulationTick, then Scene::UpdatePhysics (which steps the solver and
        then syncs every object from its body). So a velocity that covers exactly the gap between
        where the body is and where the rules say it should be lands the body precisely on target
        by the end of this same tick, with no visual lag - and the solver saw a surface moving at
        a real speed, which is what lets the paddle shove a capsule rather than teleport through
        it. Teleporting a kinematic body gives the solver zero relative velocity and a falling
        capsule simply passes through at any decent paddle speed.
    */
    vec3 body_position = p->GetBodyWorldPosition();
    vec3 target = vec3(game.paddle_x,BREAKOUT_PADDLE_Y,ARENA_Z);
    vec3 needed = (target - body_position) / main_scene->GetPhysicsTimestep();
    p->SetVelocity(needed);
    p->SetAngularVelocity(vec3());
}

void ApplicationBreakout::UpdateRipples(){
    for (int i = 0; i < BREAKOUT_MAX_RIPPLES; i++){
        if (ripples[i].age < 0.0f){
            continue;
        }
        ripples[i].age += 1.0f;
        //45 ticks is the shader's own RIPPLE_LIFE; past that the ring contributes nothing and the
        //slot is better free for the next save.
        if (ripples[i].age > 45.0f){
            ripples[i].age = -1.0f;
        }
    }
    //Published for the shader, all of it in TICKS so the effect pauses and single-steps with the
    //game rather than running on a wall clock the simulation cannot see.
    shield_time_ticks = (float)(main_scene->GetPhysicsTick() & 0xFFFFF);
    shield_charge_view = game.shield_charge;
    shield_flare_view = (float)game.shield_flare_ticks / (float)BREAKOUT_SHIELD_FLARE_TICKS;
}

void ApplicationBreakout::UpdateCameraShake(){
    Camera* camera = main_scene->camera;
    if (!camera){
        return;
    }
    vec3 home = vec3(camera_target.x,camera_target.y - 18.5f,40.0f);
    if (shake_ticks <= 0){
        shake_amount = 0.0f;
        camera->SetPosition(home);
        camera->SetLookAt(camera_target);
        return;
    }
    shake_ticks--;
    //A decaying square wave on alternating ticks - cheap, and at 60Hz it reads as a jolt rather
    //than a wobble. Driven off the simulation tick so it replays identically.
    float decay = shake_amount * ((float)shake_ticks / 24.0f);
    float sign = (main_scene->GetPhysicsTick() & 1) ? 1.0f : -1.0f;
    camera->SetPosition(home + vec3(decay * sign * 0.8f,decay * 0.5f,0.0f));
    camera->SetLookAt(camera_target);
}

/*
    The tunnelling probe.

    This exists because "a fast small ball tunnels through a thin brick" is the kind of claim that
    should be measured rather than assumed, and because the sweep it measures is the thing the
    whole "what is the ball?" decision rests on. It parks the paddle under the wall, fires the ball
    at a chosen speed, and counts ESCAPES - ticks on which a ball ended up outside the arena, which
    is what a missed collision looks like from the outside. Driven from MCP by breakout_ball_probe.
*/
void ApplicationBreakout::UpdateBallProbe(){
    if (probe_ticks_left <= 0){
        return;
    }
    probe_ticks_left--;

    //Hold the speed at the probe's value against the rules' own ramp, which would otherwise change
    //the variable being measured half way through the run. Through Field::speed_override rather
    //than by writing the velocities directly: the rules re-assert a ball's speed every tick
    //(ConditionVelocity), so anything set from out here would be gone before it was used.
    game.speed_override = probe_speed;
    if (probe_ticks_left <= 0){
        game.speed_override = 0.0f;
    }

    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        BreakoutBall& ball = game.balls[i];
        if (!ball.f_alive || ball.f_stuck){
            continue;
        }
        //Outside the arena in x, or above the ceiling: the sweep let it through something.
        if ((ball.x < BREAKOUT_FIELD_LEFT - 1.0f) ||
            (ball.x > BREAKOUT_FIELD_RIGHT + 1.0f) ||
            (ball.y > BREAKOUT_FIELD_TOP + 1.0f)){
            probe_escapes++;
            //Put it back so one escape does not become a thousand as it flies off.
            ball.x = clamp(ball.x,BREAKOUT_FIELD_LEFT + 1.0f,BREAKOUT_FIELD_RIGHT - 1.0f);
            ball.y = clamp(ball.y,BREAKOUT_DEATH_Y + 2.0f,BREAKOUT_FIELD_TOP - 1.0f);
        }
    }

    //Keep a ball in play for the whole run: this is a collision test, not a game.
    if (game.LiveBalls() == 0 && game.phase != BREAKOUT_PHASE_GAMEOVER){
        game.lives = max(game.lives,2);
    }
}

//--- The custom shader's uniforms ----------------------------------------------------------------

/*
    Render thread, with the shield program already bound, once per frame.

    Every value here is a plain member the tick wrote. Nothing is locked: the render thread already
    holds physics_mutex for the whole of DrawFrame, so the physics thread cannot be half way
    through writing one of these while this runs.
*/
void ApplicationBreakout::SetShieldUniforms(){
    if (!shield_shader){
        return;
    }
    shield_shader->Setfloat("shield_charge",shield_charge_view);
    shield_shader->Setfloat("shield_flare",clamp(shield_flare_view,0.0f,1.0f));
    shield_shader->Setfloat("shield_time",shield_time_ticks);
    //The quad's rectangle, as two vec3s. core/Shader.h has no Setvec2 and no Setvec4, so a
    //two-component value travels in three components and a four-component one has to be split.
    shield_shader->Setvec3("shield_origin",
                           vec3((BREAKOUT_FIELD_LEFT + BREAKOUT_FIELD_RIGHT) * 0.5f - SHIELD_QUAD_W * 0.5f,
                                BREAKOUT_SHIELD_Y - SHIELD_QUAD_H * 0.5f,0.0f));
    shield_shader->Setvec3("shield_size",vec3(SHIELD_QUAD_W,SHIELD_QUAD_H,0.0f));
    shield_shader->Setvec3("shield_focus",shield_focus);
    //Three ripples, three uniforms. There is no array setter either, so this is the shape the
    //effect had to be designed around rather than a shape it was designed in.
    shield_shader->Setvec3("ripple0",vec3(ripples[0].x,ripples[0].age,0.0f));
    shield_shader->Setvec3("ripple1",vec3(ripples[1].x,ripples[1].age,0.0f));
    shield_shader->Setvec3("ripple2",vec3(ripples[2].x,ripples[2].age,0.0f));
}

/*
    Recompiles the shield shader from disk, on the render thread.

    This was a hand-rolled build-and-swap that never called ReleaseFile, so it recompiled the bytes
    LoadFile had cached at start-up and produced an identical program every time - the F5 key and
    the HUD button have been doing nothing since the cache was added. Shader::Reload() releases the
    whole source set first, which is the half that was missing, and rebuilds in place so the
    custom-shader index and the uniform callback both survive. See core/Shader.h and backlog 61.
*/
void ApplicationBreakout::ReloadShieldShader(){
    if (!shield_shader){
        return;
    }
    if (!shield_shader->Reload()){
        debug->Err("Shield shader not reloaded, the previous one is still drawing\n");
    }
}

//--- Render thread ------------------------------------------------------------------------------

/*
    Frame thread, once per frame, before the scene is drawn.

    This is where text gets built, because building it ends in glNamedBufferData and the physics
    thread may not touch GL. What crosses the thread boundary is the SNAPSHOT - plain numbers,
    published under snapshot_mutex at the end of every tick - and the strings are formatted here,
    on this side of it. Nothing has to be published as text and the simulation never waits.
*/
void ApplicationBreakout::PreRender(void){
    //A shader reload is GL work, so the key that asks for one only raises a flag on the physics
    //thread and it is serviced here.
    if (f_shader_reload_requested){
        f_shader_reload_requested = false;
        ReloadShieldShader();
    }

    if (!glyphs.IsValid()){
        return;
    }

    BreakoutSnapshot copy;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        copy = snapshot;
    }

    char text[80];
    snprintf(text,sizeof(text),"SCORE\n%i",copy.score);
    SetLabelText(BREAKOUT_LABEL_SCORE,text);
    snprintf(text,sizeof(text),"BALLS %i",max(copy.lives,0));
    SetLabelText(BREAKOUT_LABEL_LIVES,text);
    snprintf(text,sizeof(text),"LEVEL %i",copy.level);
    SetLabelText(BREAKOUT_LABEL_LEVEL,text);

    {   //A bar rather than a number: the shield is a gauge, and the shield itself is already
        //showing its colour. Ten cells of monospaced text is the cheapest honest bar there is.
        char bar[16] = {};
        int filled = (int)(clamp(copy.shield_charge,0.0f,1.0f) * 10.0f + 0.5f);
        for (int i = 0; i < 10; i++){
            bar[i] = (i < filled) ? '=' : '-';
        }
        snprintf(text,sizeof(text),"SHIELD\n%s",bar);
        SetLabelText(BREAKOUT_LABEL_SHIELD,text);
    }

    //Only while it means something. A combo of 1 is not a combo.
    if (copy.combo >= 2){
        snprintf(text,sizeof(text),"COMBO x%i",copy.combo);
        SetLabelText(BREAKOUT_LABEL_COMBO,text);
    }else{
        SetLabelText(BREAKOUT_LABEL_COMBO,"");
    }

    //Text or no text is the whole of the banner's state - BuildTextMesh returns NULL for a string
    //with no ink, which is a documented outcome rather than a failure.
    switch (copy.phase){
        case BREAKOUT_PHASE_READY:
            SetLabelText(BREAKOUT_LABEL_BANNER,"PRESS SPACE");
            break;
        case BREAKOUT_PHASE_LEVEL_CLEARED:
            snprintf(text,sizeof(text),"LEVEL %i\nCLEARED",copy.level);
            SetLabelText(BREAKOUT_LABEL_BANNER,text);
            break;
        case BREAKOUT_PHASE_GAMEOVER:
            SetLabelText(BREAKOUT_LABEL_BANNER,"GAME OVER\nPRESS R");
            break;
        default:
            SetLabelText(BREAKOUT_LABEL_BANNER,"");
            break;
    }
}

//--- Telemetry ------------------------------------------------------------------------------------

void ApplicationBreakout::PublishSnapshot(){
    //Filled on the physics thread and read by MCP tool handlers, which hold no lock of their own.
    //The mutex is this app's, not the engine's: taking physics_mutex from an MCP thread is the
    //other option (Scene::AtTickBoundary) and it would stall the simulation for the length of
    //every telemetry call - which matters when the point is to PLAY the game through the tools.
    std::lock_guard<std::mutex> lock(snapshot_mutex);
    snapshot.tick = main_scene->GetPhysicsTick();
    snapshot.game_ticks = game.ticks_elapsed;
    snapshot.phase = game.phase;
    snapshot.score = game.score;
    snapshot.lives = game.lives;
    snapshot.level = game.level;
    snapshot.combo = game.combo;
    snapshot.best_combo = game.best_combo;
    snapshot.bricks_left = game.BricksRemaining();
    snapshot.bricks_broken = game.bricks_broken;
    snapshot.shield_saves = game.shield_saves;
    snapshot.powerups_caught = game.powerups_caught;
    snapshot.shield_charge = game.shield_charge;
    snapshot.paddle_x = game.paddle_x;
    snapshot.paddle_vx = game.paddle_vx;
    snapshot.paddle_half_w = game.PaddleHalfWidth();
    snapshot.powerup_wide_ticks = game.powerup_wide_ticks;
    snapshot.powerup_slow_ticks = game.powerup_slow_ticks;
    snapshot.debris_bodies = (int)debris.size();
    snapshot.capsules = (int)capsules.size();
    snapshot.seed = current_seed;
    snapshot.probe_ticks_left = probe_ticks_left;
    snapshot.probe_escapes = probe_escapes;
    snapshot.probe_speed = probe_speed;
    snapshot.probe_bricks = game.bricks_broken - probe_bricks_at_start;
    snapshot.probe_overruns = game.resolution_overruns - probe_overruns_at_start;
    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        snapshot.balls[i] = game.balls[i];
    }
    snapshot.rows = game.ToAsciiRows();
    snapshot.last_hit_tick = last_hit_tick;
    snapshot.last_hit_offset = last_hit_offset;
    snapshot.last_hit_angle = last_hit_angle;
    snapshot.last_hit_speed = last_hit_speed;
    snapshot.capsule_views.clear();
    for (size_t i = 0; i < capsules.size(); i++){
        if (!capsules[i].object){
            continue;
        }
        //Read off the rigid body's own transform - these really are the solver's to place, which
        //is the point of them.
        vec3 position = capsules[i].object->GetWorldPosition();
        BreakoutSnapshot::CapsuleView view;
        view.x = position.x;
        view.y = position.y;
        view.kind = capsules[i].kind;
        snapshot.capsule_views.push_back(view);
    }
}

json ApplicationBreakout::BuildStateJson(){
    BreakoutSnapshot copy;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        copy = snapshot;
    }

    json balls = json::array();
    for (int i = 0; i < BREAKOUT_MAX_BALLS; i++){
        if (!copy.balls[i].f_alive){
            continue;
        }
        balls.push_back(json{
            {"x",copy.balls[i].x},
            {"y",copy.balls[i].y},
            {"vx",copy.balls[i].vx},
            {"vy",copy.balls[i].vy},
            {"speed",copy.balls[i].speed},
            {"stuck",copy.balls[i].f_stuck}
        });
    }
    json rows = json::array();
    for (size_t i = 0; i < copy.rows.size(); i++){
        rows.push_back(copy.rows[i]);
    }
    json falling = json::array();
    for (size_t i = 0; i < copy.capsule_views.size(); i++){
        falling.push_back(json{
            {"x",copy.capsule_views[i].x},
            {"y",copy.capsule_views[i].y},
            {"kind",PowerupName(copy.capsule_views[i].kind)}
        });
    }

    //Everything else comes from the snapshot, but "paused" is read live: the snapshot is only
    //written by a tick, and pausing is precisely what stops ticks happening - so a snapshotted
    //value would report the state from BEFORE the pause and never update.
    bool f_paused = main_scene ? main_scene->IsPhysicsPaused() : copy.f_paused;

    return json{
        {"tick",copy.tick},
        {"game_ticks",copy.game_ticks},
        {"paused",f_paused},
        {"phase",PhaseName(copy.phase)},
        {"score",copy.score},
        {"balls_left",copy.lives},
        {"level",copy.level},
        {"combo",copy.combo},
        {"best_combo",copy.best_combo},
        {"bricks_left",copy.bricks_left},
        {"bricks_broken",copy.bricks_broken},
        {"shield_charge",copy.shield_charge},
        {"shield_saves",copy.shield_saves},
        {"powerups_caught",copy.powerups_caught},
        {"powerup_wide_ticks",copy.powerup_wide_ticks},
        {"powerup_slow_ticks",copy.powerup_slow_ticks},
        {"paddle_x",copy.paddle_x},
        {"paddle_vx",copy.paddle_vx},
        {"paddle_half_width",copy.paddle_half_w},
        {"seed",copy.seed},
        {"balls",balls},
        {"debris_bodies",copy.debris_bodies},
        {"capsules_falling",copy.capsules},
        {"capsules",falling},
        {"last_paddle_hit",json{
            {"tick",copy.last_hit_tick},
            {"offset",copy.last_hit_offset},
            {"angle_deg",copy.last_hit_angle},
            {"speed",copy.last_hit_speed}
        }},
        {"probe",json{
            {"ticks_left",copy.probe_ticks_left},
            {"escapes",copy.probe_escapes},
            {"speed",copy.probe_speed},
            {"bricks_broken",copy.probe_bricks},
            {"resolution_overruns",copy.probe_overruns}
        }},
        {"wall",rows}
    };
}

//--- MCP ------------------------------------------------------------------------------------------

#ifdef USE_MCP
//This app's own MCP tools. Present only when USE_MCP=1; see the block in engine.mk for why
//the core half of the same switch is a swapped translation unit rather than an #ifdef.
void ApplicationBreakout::RegisterMCPTools(){
    //Registered from Init(). The server only starts accepting requests after Init() returns, so
    //registration can never race a client's tools/list.

    MCPServer::Get()->RegisterTool("breakout_state",
        "The whole game state: the brick wall as 8 ASCII rows (top row first, '.' empty, 'o' a "
        "one-hit brick, 'O' two-hit, '@' three-hit, '*' a prize brick that drops a power-up, '#' "
        "indestructible), plus score, balls left, level, combo, the shield's charge, every live "
        "ball's position and velocity, and how many debris bodies and falling capsules exist. "
        "Read from a snapshot the physics thread publishes at the end of every tick, so it never "
        "disturbs the game it is measuring. The arena spans x 0..22 and y 0..27; the paddle sits "
        "at y 2.6 and the shield surface at y 1.1.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the current frame, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("breakout_steer",
        "Hold the paddle's ANALOG steering axis at `value` (-1 full left, +1 full right) for a "
        "number of SIMULATION TICKS, and block until it has played out. This is how a program "
        "plays: the hold emits ordinary input events, so the simulation cannot tell it from a "
        "thumb on a stick. The game runs at 60 ticks per second and the paddle accelerates, so a "
        "short hold nudges and a long one sweeps. Pass ticks 0 to release. While the simulation is "
        "paused the hold does not count down - use breakout_step.",
        json{
            {"type","object"},
            {"properties", {
                {"value", {{"type","number"},{"description","-1..+1; negative is left"}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold it, default 6, capped at 600"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the resulting frame, default false"}}}
            }},
            {"required", json::array({"value"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            float value = clamp(args.value("value",0.0f),-1.0f,1.0f);
            int ticks = (int)clamp(args.value("ticks",6.0f),0.0f,600.0f);
            uint64_t start_tick = main_scene->GetPhysicsTick();
            input->HoldAxis(INPUT_BREAKOUT_STEER,value,(uint32_t)ticks);
            if (ticks > 0){
                uint64_t target = start_tick + (uint64_t)ticks + 1;
                for (int waited = 0; waited < 4000 && main_scene->GetPhysicsTick() < target; waited += 4){
                    Sleep(4);
                }
            }
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("breakout_launch",
        "Launch the ball that is sitting on the paddle. Does nothing unless the phase is 'ready'. "
        "The ball leaves at an angle set by where it is resting on the paddle, so steer first and "
        "the launch is already aimed.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            uint64_t start_tick = main_scene->GetPhysicsTick();
            input->HoldKey(INPUT_BREAKOUT_LAUNCH,2);
            //Wait for the hold AND for the release edge to be consumed by a tick: the launch is
            //read with WasKeyReleased, which only becomes true on the tick after the hold ends.
            //Since backlog item 84 those ticks are STEPPED ticks as well as free-running ones -
            //scripted holds advance from inside the tick, so this works under sim_step too.
            uint64_t target = start_tick + 4;
            for (int waited = 0; waited < 4000 && main_scene->GetPhysicsTick() < target; waited += 4){
                Sleep(4);
            }
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("breakout_step",
        "Advance the PAUSED simulation by exactly num_ticks ticks and return the resulting state. "
        "Requires sim_pause first. Every duration in this game is a tick count, so stepping is "
        "exact - and since the ball's motion is integrated by the game rather than by the solver, "
        "one step is one deterministic sweep. Check ticks_advanced, not num_ticks.",
        json{
            {"type","object"},
            {"properties", {
                {"num_ticks", {{"type","number"},{"description","ticks to advance, default 1"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","not paused - call sim_pause with paused=true first"} };
            }
            int num_ticks = max((int)args.value("num_ticks",1.0f),0);
            uint64_t advanced = StepPhysicsAndWait(num_ticks);
            json result = BuildStateJson();
            result["ticks_advanced"] = advanced;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("breakout_restart",
        "Throw the current game away and start a new one. The level shapes are fixed but the prize "
        "bricks and the power-up kinds are a pure function of the seed, so the same seed always "
        "deals the same game. Goes through the simulation command queue, so it lands at the top of "
        "a tick on the physics thread rather than in the middle of one.",
        json{
            {"type","object"},
            {"properties", {
                {"seed", {{"type","number"},{"description","level seed; omit or 0 for a fresh one"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = BREAKOUT_CMD_RESTART;
            cmd.value[0] = args.value("seed",0.0f);
            //SubmitCommandAndWait, not SubmitUICommand: an MCP handler holds no lock, so it may
            //wait - and it has to, or it would report the state of the game it just replaced.
            SubmitCommandAndWait(cmd);
            return BuildStateJson();
        });

    MCPServer::Get()->RegisterTool("breakout_reload_shader",
        "Recompile shaders/breakout_shield.frag from disk and swap it in, without restarting the "
        "app. The same thing the F5 key and the HUD button do - exposed here because iterating on "
        "a shader is the one job where a rebuild-and-relaunch cycle costs more than the edit, and "
        "a caller who cannot reach the keyboard was otherwise stuck with the slow loop. The "
        "compile happens on the render thread in PreRender; a shader that fails to compile leaves "
        "the previous program drawing and logs why. The core `shader_reload` tool does the same "
        "thing for any shader in any app and returns the GLSL log with it.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            f_shader_reload_requested = true;
            //Long enough for a frame to have happened at any sane rate, so the reply means the
            //reload really did run rather than that it was merely asked for.
            Sleep(120);
            json result = BuildStateJson();
            result["reload_pending"] = (bool)f_shader_reload_requested;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("breakout_ball_probe",
        "Run the collision probe: pin every live ball's speed at `speed` world units per second "
        "for `ticks` simulation ticks and count ESCAPES - ticks on which a ball ended up outside "
        "the arena, which is what a missed collision looks like from outside. This is the "
        "measurement behind the decision to sweep the ball by hand rather than hand it to "
        "reactphysics3d; see docs/breakout_findings.md section 8. Read the result back with "
        "breakout_state, or wait for this call to return, which it does when the run is over. The "
        "`probe` field reports three things: `escapes` (a ball found outside the arena - a missed "
        "collision), `bricks_broken` (that the sweep is still HITTING things, not merely staying "
        "inside the box), and `resolution_overruns` (ticks on which the ball hit the six-impact "
        "per-tick cap and the rest of its travel was dropped - not a tunnel, but the ball moving "
        "less far than its velocity says). A ball is served automatically while a run is going, so "
        "losing one does not stall the measurement. Bricks are 2.0 x 1.0 world units and the "
        "ball's radius is 0.4, so a ball above 60 units/second moves more than a brick's height "
        "in one tick.",
        json{
            {"type","object"},
            {"properties", {
                {"speed", {{"type","number"},{"description","world units per second, 1..900"}}},
                {"ticks", {{"type","number"},{"description","how long to run it, default 600, capped at 20000"}}}
            }},
            {"required", json::array({"speed"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            float speed = clamp(args.value("speed",30.0f),1.0f,900.0f);
            int ticks = (int)clamp(args.value("ticks",600.0f),1.0f,20000.0f);
            //Written from this thread and read by the physics thread. Plain floats/ints rather
            //than a command, because the probe is a MEASUREMENT HARNESS rather than part of the
            //simulation - it deliberately does not belong in a replay.
            probe_escapes = 0;
            probe_speed = speed;
            //Baselined before the run is armed, so the counters report this run alone.
            probe_bricks_at_start = game.bricks_broken;
            probe_overruns_at_start = game.resolution_overruns;
            probe_ticks_left = ticks;
            //Bounded wait: a paused simulation would otherwise hang this forever.
            for (int waited = 0; waited < 60000 && probe_ticks_left > 0; waited += 8){
                Sleep(8);
            }
            json result = BuildStateJson();
            result["probe_finished"] = (probe_ticks_left <= 0);
            return result;
        });
}
#endif //USE_MCP

//--- HUD --------------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationBreakout::DrawImGuiUI(void){
    //Runs on the RENDER thread with physics_mutex held, so reading the simulation directly here is
    //safe - which is why this reads `game` rather than the snapshot the MCP tools use.
    if (f_show_engine_ui){
        RenderApplicationUI();
    }
    RenderBreakoutHUD();
}
#endif //USE_IMGUI

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationBreakout::RenderBreakoutHUD(){
    //Anchored top-left and kept narrow, because the engine's debug panels dock into the same
    //corner when F1 is on and the two should not fight over it.
    ImGui::SetNextWindowPos(ImVec2(f_show_engine_ui ? 330.0f : 16.0f,16.0f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280,0),ImGuiCond_Always);
    ImGui::Begin("Breakout",NULL,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    ImGui::Text("SCORE  %i",game.score);
    ImGui::Text("BALLS  %i    LEVEL %i",game.lives,game.level);
    ImGui::Text("BRICKS %i    COMBO x%i",game.BricksRemaining(),game.combo);
    ImGui::ProgressBar(clamp(game.shield_charge,0.0f,1.0f),ImVec2(-1,0),"shield");
    ImGui::Separator();
    ImGui::Text("phase   %s",PhaseName(game.phase));
    ImGui::Text("tick    %llu",(unsigned long long)main_scene->GetPhysicsTick());
    ImGui::Text("paddle  %.2f  (%.1f u/s)",game.paddle_x,game.paddle_vx);
    {
        const BreakoutBall& ball = game.balls[0];
        ImGui::Text("ball    %.2f,%.2f  %.1f u/s",ball.x,ball.y,ball.speed);
    }
    ImGui::Text("bodies  %i debris, %i capsules",(int)debris.size(),(int)capsules.size());
    if (probe_ticks_left > 0){
        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1),"PROBE %i ticks, %i escapes at %.0f u/s",
                           (int)probe_ticks_left,(int)probe_escapes,probe_speed);
    }

    ImGui::Separator();
    if (ImGui::Button("New game")){
        //SubmitUICommand, never SubmitCommandAndWait: this runs with physics_mutex held, and the
        //physics thread needs that same lock to drain the queue - waiting here deadlocks.
        SimCommand cmd;
        cmd.type = BREAKOUT_CMD_RESTART;
        cmd.value[0] = 0.0f;    //0 means "pick a fresh seed"
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    if (ImGui::Button(main_scene->IsPhysicsPaused() ? "Resume" : "Pause")){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload shader")){
        //Raises the flag PreRender acts on, for the same reason the key does: this is the render
        //thread, but it is inside the frame rather than before it, and rebuilding a program under
        //the pass that is about to use it is not worth the saving.
        f_shader_reload_requested = true;
    }
    ImGui::Checkbox("Sound",&f_sound_enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Debris",&f_debris_enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Mouse",&f_mouse_control);

    ImGui::Separator();
    ImGui::TextDisabled("mouse or A/D or stick to steer");
    ImGui::TextDisabled("space launches, P pauses");
    ImGui::TextDisabled("R restarts, M mouse, F5 shader");
    ImGui::TextDisabled("F1 engine panels");

    ImGui::End();
}
#endif //USE_IMGUI
