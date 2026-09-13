#include "ApplicationPinball.h"

#include "Debug.h"
#include "Primitives.h"
#include "CubeMap.h"
#include "MCPServer.h"
#include "type_helpers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static Debugger* debug = new Debugger("ApplicationPinball",DEBUG_ALL);

/*
    --- THINGS THAT ARE GEOMETRY RATHER THAN LAYOUT ----------------------------------------------
    Table.h owns every coordinate a ball can reach. What is here instead are the numbers that only
    describe how a placeholder is DRAWN - how thick a plate is, how many segments a curve is
    sampled into - because none of them survives into stage 1 and none of them changes where
    anything is. Keeping them out of Table.h is what keeps Table.h reviewable.
*/
#define PIN_PLATE_THICKNESS     0.06f       //the apron and the backglass, both flat panels
#define PIN_INSERT_RISE         0.012f      //how far a lamp insert stands proud of the deck
#define PIN_ARC_SEGMENTS        30          //the orbit; see the chord-error note in TableBuilder.h
#define PIN_RAMP_SLAB           0.08f       //how thick a ramp floor looks
#define PIN_RAMP_RAIL_THICK     0.06f       //a moulded ramp's side wall; thin, and OUTSIDE the floor
#define PIN_RAMP_SKIRT          0.60f       //how far a climb's side wall hangs below the floor: to the deck, and then some

/*
    Portrait, 3:2, because the machine is (see Table.h's revision note): the cabinet is 6.9 x 10.4
    and from 26 degrees off vertical it fills a 2:3 frame edge to edge with the margin MakeShot
    leaves. The renderer's own width and height are not known until the window has resized, so
    the shots are solved against these rather than against renderer->width.
*/
#define PIN_WINDOW_WIDTH        800
#define PIN_WINDOW_HEIGHT       1200

//The camera eases toward the current shot by this fraction of the remaining distance per pass.
//A rate rather than a duration, so interrupting a move to go somewhere else costs nothing.
#define PIN_CAMERA_EASE         0.10f

//--- The ramps, as centrelines ------------------------------------------------------------------
/*
    Where the ball runs, not where the floor is: MakeRibbon centres its slab on the path and hangs
    the thickness BELOW it, so a point here is a place the ball can be. That is what makes these
    numbers checkable - "the crest is 0.82 above the deck" means the ball is 0.82 above the deck.

    BOTH RAMPS CLIMB ALONG A WALL AND COME HOME ALONG THE SAME WALL, the way the reference artwork
    draws them: up the inside, a U-turn at the top, down the outside over the lane below. The
    first build ran both habitrails back down the MIDDLE of the table as opaque slabs and they
    covered most of the deck; here the airborne part is four wires (see BuildRamps) and passes
    over things that are never more than 0.44 tall - the orbit return lane and the upper flipper
    on the left, the plunger chute and the drop bank on the right.

    Each begins with a short FLAT run at deck level: that is the mouth, the spinner hangs in it on
    the left, and it is what a scripted test fires at. The floor stops being part of the deck once
    its underside clears a ball (y > PIN_RAMP_AIRBORNE_Y); from there on the ball is in the air.

    Heights are what the plan tool checks: tools/pinball_plan.py samples these against everything
    they cross and reports headroom in balls. Move a point, run it.
*/
//Above this the ramp floor's underside clears a ball on the deck, so the ramp is airborne and
//its rails stop being deck walls. PIN_RAMP_SLAB + a ball + a whisker; the plan tool uses the same.
#define PIN_RAMP_AIRBORNE_Y     (PIN_RAMP_SLAB + PIN_BALL_DIAMETER + 0.02f)

static const vec3 kLeftRampPath[] = {
    vec3(PIN_RAMP_L_ENTRY_X, 0.00f, PIN_RAMP_L_ENTRY_Z),    //mouth, flat, spinner hanging in it
    vec3(-1.60f, 0.00f,  0.95f),    //the floor starts to climb
    vec3(-1.70f, 0.12f,  0.45f),
    vec3(-1.80f, 0.32f, -0.20f),    //the last point that is part of the deck; wires from here
    vec3(-1.84f, 0.55f, -0.95f),    //passes the left pop's skirt by 0.14
    vec3(-1.94f, 0.74f, -1.70f),    //over the upper flipper's tip, 1.3 balls above it
    vec3(-2.00f, 0.82f, -2.25f),    //crest
    vec3(-2.20f, 0.85f, -2.75f),    //U-turn toward the wall...
    vec3(-2.48f, 0.86f, -2.88f),
    vec3(-2.66f, 0.84f, -2.55f),    //...and back down-table, over the orbit return lane
    vec3(-2.68f, 0.78f, -1.80f),    //over the upper flipper's pivot
    vec3(-2.68f, 0.68f, -0.60f),
    vec3(-2.66f, 0.58f,  0.60f),
    vec3(-2.58f, 0.52f,  1.10f),
    vec3(-2.20f, 0.48f,  1.45f),    //turning inboard, up-table of the outlane guide
    vec3(-1.60f, 0.46f,  1.60f),    //ends 0.35 short of the divider; the ball drops into the inlane top
};

static const vec3 kRightRampPath[] = {
    vec3(PIN_RAMP_R_ENTRY_X, 0.00f, PIN_RAMP_R_ENTRY_Z),    //mouth, flat
    vec3( 1.10f, 0.00f,  0.95f),
    vec3( 1.20f, 0.12f,  0.45f),
    vec3( 1.32f, 0.32f, -0.20f),    //the last deck-level point; wires from here
    vec3( 1.44f, 0.55f, -0.95f),    //passes the right pop's skirt by 0.33
    vec3( 1.52f, 0.74f, -1.65f),
    vec3( 1.60f, 0.82f, -2.10f),    //crest, beside the right wormhole
    vec3( 1.85f, 0.86f, -2.50f),    //U-turn over the drop bank (0.30 tall; 0.48 of headroom)
    vec3( 2.20f, 0.87f, -2.55f),
    vec3( 2.55f, 0.85f, -2.20f),    //...and down the plunger chute, between divider and cabinet
    vec3( 2.60f, 0.80f, -1.50f),
    vec3( 2.60f, 0.77f, -0.30f),
    vec3( 2.60f, 0.76f,  0.80f),
    vec3( 2.45f, 0.74f,  1.25f),    //turning inboard OVER the chute divider (0.55 tall) at 0.70
    vec3( 2.05f, 0.68f,  1.50f),
    vec3( 1.55f, 0.56f,  1.62f),
    vec3( 1.02f, 0.48f,  1.62f),    //ends 0.35 short of the right divider
};

//--- The camera shots ---------------------------------------------------------------------------
/*
    Answer 2 to the design document's open questions: a fixed view like the original, framed to
    match the reference artwork, that MOVES somewhere when something happens there and comes back.

    PIN_SHOT_TABLE is the fixed one and the only one stage 0 starts in. The others exist to prove
    the mechanism and, more immediately, to be able to look closely at a corner of the layout
    without hand-flying a camera - which is most of what stage 0 is for.

    --- A SHOT IS SOLVED, NOT TYPED ----------------------------------------------------------------
    The first build hand-derived each shot from two points - the nearest thing in frame and the
    farthest - and wrote the resulting camera position down as a literal. The derivation was right
    and the literals went stale the moment the table changed length, which is exactly what this
    revision did to it. So the derivation is now code and runs at start-up against Table.h.

    Given the two points A (near, low) and C (far, high), a view axis `elevation` degrees off
    vertical, and a vertical fov: the ray from the camera to A lies half a fov BELOW the axis and
    the ray to C half a fov ABOVE it. Both rays' directions are therefore known, and the camera is
    simply where the line through A along one meets the line through C along the other. That
    fixes the camera in the vertical plane. Where it LOOKS is where the axis meets the deck, which
    on a long table is well down-table of the geometric centre - perspective makes the near end
    take far more of the frame than the far end, and aiming at the middle cuts the flippers off.
    It did, in the first build.

    Width is then the window's job: with the length fixing distance and fov, the aspect ratio
    decides how much width shows. MakeShot checks that the cabinet fits and backs the camera off
    along its own axis if it does not - moving straight back only ever shrinks what A and C
    subtend, so the length still fits.
*/
static PinShot MakeShot(const char* name, const vec3& near_point, const vec3& far_point,
                        float elevation_degrees, float fov_degrees,
                        float half_width_needed, float aspect){
    //Everything happens in the vertical plane x = PIN_CENTRE_X, as 2D (z, y).
    const float e = toradians(elevation_degrees);
    const float h = toradians(fov_degrees) * 0.5f * 0.94f;   //6% of margin inside the frame
    //The axis points up-table (-z) and down (-y). Rotating it toward the vertical by h gives the
    //steeper ray, to the near point; away from the vertical by h gives the flatter ray, to the far
    //point. In (z, y) a direction at angle t off vertical pointing down-and-up-table is
    //(-sin t, -cos t).
    const vec2 ray_near = vec2(-sinf(e - h),-cosf(e - h));
    const vec2 ray_far  = vec2(-sinf(e + h),-cosf(e + h));
    const vec2 A = vec2(near_point.z,near_point.y);
    const vec2 C = vec2(far_point.z,far_point.y);
    //Solve A - ray_near * s = C - ray_far * t for s, by Cramer's rule on the 2x2 system
    //    ray_near * s - ray_far * t = A - C
    const vec2 rhs = A - C;
    const float det = ray_near.x * (-ray_far.y) - (-ray_far.x) * ray_near.y;
    float s = 1.0f;
    if (fabsf(det) > 1e-6f){
        s = (rhs.x * (-ray_far.y) - (-ray_far.x) * rhs.y) / det;
    }
    vec2 P = A - ray_near * s;

    //Does the subject fit across? The horizontal half-fov follows from the vertical one and the
    //aspect. Measured on the deck halfway between the two points - the middle of what the shot
    //is OF, which for a detail shot is nowhere near the middle of the table.
    const vec2 axis = vec2(-sinf(e),-cosf(e));
    const float tan_half_h = tanf(toradians(fov_degrees) * 0.5f) * aspect;
    const vec2 M = vec2((near_point.z + far_point.z) * 0.5f,0.0f);
    const float along = (M - P).dot(axis);
    const float needed = half_width_needed / tan_half_h;
    if (needed > along){
        P = P - axis * (needed - along);
    }

    //Look where the axis meets the deck.
    const float to_deck = P.y / cosf(e);
    const vec2 T = P + axis * to_deck;

    PinShot shot;
    shot.name     = name;
    shot.position = vec3(PIN_CENTRE_X,P.y,P.x);
    shot.target   = vec3(PIN_CENTRE_X,0.15f,T.x);
    shot.fov      = fov_degrees;
    return shot;
}

//How fast the orbit camera answers the mouse. Divisors rather than multipliers to match the feel
//of apps/tank, which these are deliberately the same as - the point of a debug camera is that it
//behaves the way the other one in this repo already does.
#define PIN_ORBIT_DRAG_DEGREES  0.35f   //degrees of swing per raw mouse count
#define PIN_ORBIT_PAN_SCALE     0.010f  //world units of pivot travel per raw mouse count
#define PIN_ORBIT_ZOOM_SCALE    0.0016f //fraction of the current distance per wheel count
//What the camera lamp should actually deliver at the pivot, in the units the other lights here
//are tuned in. ApplyOrbit solves brightness = sqrt(this * distance) for it every time it moves.
#define PIN_ORBIT_LIGHT_LEVEL   1.30f
#define PIN_ORBIT_MIN_DISTANCE  1.20f
#define PIN_ORBIT_MAX_DISTANCE  60.0f

ApplicationPinball::ApplicationPinball():Application(){
    debug->Info("Created new application.\n");
}

ApplicationPinball::~ApplicationPinball(){
}

//--- Parts --------------------------------------------------------------------------------------

void ApplicationPinball::LoadParts(){
    /*
        LoadFile is FATAL on a file it cannot find, so the existence check has to come first: a
        table without parts.glb is a table built from primitives, not a crash.
    */
    std::string resolved;
    if (!ResolveAssetPath("meshes/parts.glb",resolved)){
        debug->Warn("meshes/parts.glb not found - building the table from primitives. "
                    "tools/pinball_parts_blender.py makes it.\n");
        return;
    }
    gltfloader.LoadGLTFFile("meshes/parts.glb");
    GetAllAssetsFromGLTF();
    f_parts_loaded = true;

    //Every part the builders will ask for, so a missing or misnamed node shows up here as one
    //line rather than as a primitive quietly standing in. Extents are in engine axes, which is
    //also the check that Blender's Z-up came through as our Y-up: a flipper bat is long in x,
    //the plunger rod is long in y (the app turns it onto z), the standup is wide in x.
    const char* expected[] = {
        "ball","flipper_bat","flipper_bat_upper","bumper_body","bumper_cap","post","rubber",
        "saucer_rim","target_drop","target_standup","spinner_vane","gate_flap",
        "plunger_tip","plunger_rod","plunger_knob",
    };
    for (int i = 0; i < (int)(sizeof(expected)/sizeof(expected[0])); i++){
        Mesh* mesh = assetmanager->GetMeshFromAsset(expected[i]);
        if (mesh){
            vec3 e = mesh->GetExtents();
            debug->Info("part %-18s extents %.3f x %.3f x %.3f\n",expected[i],e.x,e.y,e.z);
        }else{
            debug->Warn("part %-18s MISSING from parts.glb - its primitive stands in\n",expected[i]);
        }
    }
}

bool ApplicationPinball::HasPart(const char* part){
    return f_parts_loaded && assetmanager->GetAsset(part) != NULL;
}

Object* ApplicationPinball::NewPartObject(const char* part, const char* name, Mesh* fallback,
                                          const vec3& position, int material){
    Object* object = NULL;
    if (HasPart(part)){
        //A new Object sharing the asset's mesh - so three bumpers are one instanced draw call,
        //exactly as three Objects sharing one primitive mesh are.
        object = assetmanager->GetObjectFromAsset(part);
    }else if (fallback){
        object = new Object();
        object->SetMesh(fallback);
    }
    if (!object){
        debug->Err("NewPartObject(%s): no part and no fallback\n",name);
        return NULL;
    }
    object->name = name;
    object->SetPosition(position);
    //By INDEX, which lowers the object's resolve flag: the names the .glb brought along will not
    //come back next frame and overwrite this. See the materials note in core/Object.h.
    object->SetMaterialSlot(0,material);
    main_scene->AddObject(object);
    return object;
}

//--- Small builders -----------------------------------------------------------------------------

Object* ApplicationPinball::AddBox(const char* name, const vec3& centre, const vec3& size,
                                   int material, float yaw_degrees){
    if (!unit_box_mesh){
        return NULL;
    }
    Object* object = new Object();
    //Every box on the table is the SAME unit cube with a scale, so the renderer emits one
    //instanced draw call for all of them rather than one per wall. ApplicationBreakout makes the
    //same trade for the same reason.
    object->SetMesh(unit_box_mesh);
    object->name = name;
    object->SetPosition(centre);
    object->SetScale(size);
    if (yaw_degrees != 0.0f){
        object->SetRotation(quat(vec3(0,1,0),toradians(yaw_degrees)));
    }
    //A generated mesh carries no material names, so there is nothing for
    //Renderer::UpdateObjectMaterials to resolve over this slot - the index IS the answer.
    object->SetMaterialSlot(0,material);
    main_scene->AddObject(object);
    return object;
}

Object* ApplicationPinball::AddCylinderObject(const char* name, const vec3& centre, float radius,
                                              float height, int material, int segments){
    Mesh* mesh = MakeCylinder(radius,height,segments,true);
    if (!mesh){
        return NULL;
    }
    return AddMeshObject(name,mesh,centre,material);
}

Object* ApplicationPinball::AddMeshObject(const char* name, Mesh* mesh, const vec3& position,
                                          int material){
    if (!mesh){
        debug->Err("AddMeshObject(%s): no mesh\n",name);
        return NULL;
    }
    Object* object = new Object();
    object->SetMesh(mesh);
    object->name = name;
    object->SetPosition(position);
    object->SetMaterialSlot(0,material);
    main_scene->AddObject(object);
    return object;
}

Object* ApplicationPinball::AddWall(const char* name, const PinPath& path, float height,
                                    float thickness, int material, float bounciness){
    Mesh* mesh = MakeWallStrip(path,height,thickness,true);
    if (!mesh){
        debug->Err("AddWall(%s): the path made no mesh\n",name);
        return NULL;
    }
    //wall: points = the path, a = thickness, b = height. Every guide, divider and rail on the
    //table comes through here, which is what makes the plan complete.
    PinPlanEntry& entry = RecordPlan("wall",name);
    entry.points = path;
    entry.a = thickness;
    entry.b = height;
    //A swept mesh is already in world coordinates - the path was - so the Object sits at the
    //origin and the geometry carries the position. The collider chain reads the same path rather
    //than this object's transform, which is the point of building both from one source
    //(pinball_design.md 2.3): the wall the ball meets IS the wall it sees.
    Object* object = AddMeshObject(name,mesh,vec3(),material);
    AddSweptColliders(object,path,thickness * 0.5f,0.0f,height,bounciness,
                      bounciness >= PIN_RUBBER_BOUNCINESS ? PIN_RUBBER_FRICTION : PIN_RAIL_FRICTION);
    return object;
}

Object* ApplicationPinball::AddInsert(const char* name, float x, float z, float radius,
                                      int material, float y){
    Mesh* mesh = MakeCylinder(radius,PIN_INSERT_RISE * 2.0f,18,true);
    if (!mesh){
        return NULL;
    }
    //Half sunk into whatever it sits in, so only the rise shows and there is no coplanar face to
    //z-fight. `y` is 0 for a lamp in the deck; a saucer passes its rim's top face, because a hole
    //level with the rim around it is a hole buried INSIDE that rim and invisible - which is
    //exactly what all four saucers looked like first time round.
    return AddMeshObject(name,mesh,vec3(x,y,z),material);
}

void ApplicationPinball::AddPost(const char* name, float x, float z){
    char buffer[64];
    snprintf(buffer,sizeof(buffer),"%s_post",name);
    /*
        A post is a CAPSULE in stage 1, not a cylinder, and with its round caps deliberately buried
        below the deck and above the ball's band - core has no cylinder collider, and a capsule
        whose curved ends the ball can reach would deflect it vertically off a shape that looks
        vertical. The render mesh here is the cylinder that describes; the collider will be the
        capsule that behaves.
    */
    Object* post = NewPartObject("post",buffer,post_mesh,vec3(x,PIN_POST_HEIGHT * 0.5f,z),
                                 material_chrome);
    //The collider is the RUBBER's radius, on the post's body: the ring is what the ball meets.
    AddPostCollider(post,PIN_RUBBER_RADIUS,PIN_RUBBER_BOUNCINESS,PIN_RUBBER_FRICTION);

    snprintf(buffer,sizeof(buffer),"%s_rubber",name);
    NewPartObject("rubber",buffer,rubber_mesh,vec3(x,0.17f,z),material_rubber);

    //post: one point, a = the radius the ball meets, which is the RUBBER's and not the post's,
    //b = how tall it stands, for the ramps' headroom check.
    PinPlanEntry& entry = RecordPlan("post",name);
    entry.points.push_back(vec3(x,0.0f,z));
    entry.a = PIN_RUBBER_RADIUS;
    entry.b = PIN_POST_HEIGHT;
}

Flipper* ApplicationPinball::AddFlipper(const char* name, float x, float z, float length,
                                        float rest_degrees, float up_degrees, bool f_mirrored){
    /*
        The bat has its PIVOT AT THE OBJECT ORIGIN and lies along its own +X, for BOTH hands,
        because that is what the hinge joint needs and what the Blender flipper_bat is modelled to
        (pinball_design.md 3.2). The mirrored hand is the same bat turned through 180 - angle; the
        Flipper handles that, and the reasoning is in Mechanisms.cpp.
    */
    const char* part = (length > (PIN_FLIPPER_U_LENGTH + 0.01f)) ? "flipper_bat" : "flipper_bat_upper";
    Mesh* fallback = NULL;
    if (!HasPart(part)){
        PinPath bat;
        AppendXZ(bat,0.0f,0.0f,0.0f);
        AppendXZ(bat,length,0.0f,0.0f);
        //The placeholder is a plain slab; the taper a real bat has is the modelled part's job.
        fallback = MakeWallStrip(bat,PIN_FLIPPER_THICKNESS,PIN_FLIPPER_WIDTH,true);
        if (!fallback){
            return NULL;
        }
    }
    Flipper* flipper = new Flipper(name,assetmanager,part,fallback,material_orange,
                                   main_scene->physics_world,main_scene,
                                   vec3(x,0.0f,z),length,rest_degrees,up_degrees,f_mirrored);
    //flipper: one point at the pivot, a = length, b = rest angle in degrees, c = 1 if mirrored,
    //d = the bat's width. The plan tool sweeps the bat between rest and up itself.
    PinPlanEntry& entry = RecordPlan("flipper",name);
    entry.points.push_back(vec3(x,0.0f,z));
    entry.a = length;
    entry.b = rest_degrees;
    entry.c = f_mirrored ? 1.0f : 0.0f;
    entry.d = PIN_FLIPPER_WIDTH;
    return flipper;
}


void ApplicationPinball::Init(void){
    /*
        Deferred, which on this table is not a default that was copied. It is what fills the
        object-id buffer, and so what makes mouse picking and the Inspector work - and picking a
        feature out of the scene to read its transform is most of how the layout gets checked at
        this stage.
    */
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    assetmanager = new AssetManager();

    main_scene = CreateNewScene("Orbit Outpost");
    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetDebugRendering(false);

    //--- Shared meshes --------------------------------------------------------------------------
    unit_box_mesh    = MakeBox(vec3(1,1,1));
    post_mesh        = MakeCylinder(PIN_POST_RADIUS,PIN_POST_HEIGHT,14,true);
    rubber_mesh      = MakeCylinder(PIN_RUBBER_RADIUS,0.16f,14,true);
    bumper_body_mesh = MakeCylinder(PIN_BUMPER_RADIUS,PIN_BUMPER_HEIGHT,22,true);
    bumper_cap_mesh  = MakeCylinder(PIN_BUMPER_SKIRT_RADIUS,0.14f,26,true);
    saucer_mesh      = MakeCylinder(PIN_SAUCER_RADIUS,0.10f,22,true);
    ball_mesh        = MakeSphere(PIN_BALL_RADIUS,26,14);
    skybox_mesh      = MakeBox(vec3(2,2,2));
    if (!unit_box_mesh || !post_mesh || !bumper_body_mesh || !ball_mesh){
        debug->Fatal("Failed to build the primitive meshes\n");
    }
    //A reference for the app's own pointer, the way AssetManager holds one for an asset's mesh:
    //Object::DeleteMesh frees a mesh when its last holder lets go, and these are handed out to
    //many objects over the course of a build.
    unit_box_mesh->num_references++;
    post_mesh->num_references++;
    rubber_mesh->num_references++;
    bumper_body_mesh->num_references++;
    bumper_cap_mesh->num_references++;
    saucer_mesh->num_references++;
    ball_mesh->num_references++;

    BuildMaterials();
    BuildEnvironment();
    //After the materials - the .glb's own material names are deduplicated against the app's by
    //name, and the app's have to be there first for the app's to be the ones that win.
    LoadParts();
    BuildDeckAndCabinet();
    BuildLowerPlayfield();
    BuildLauncher();
    BuildUpperPlayfield();
    BuildScoringCluster();
    BuildRamps();
    BuildBackbox();
    BuildOrbitBackdrop();
    BuildFeatureLabels();
    BuildLights();
    SetupCamera();
    SetupInput();
    RegisterCommandHandlers();
    RegisterMCPTools();

    //240, and taken from the rules' own header rather than written here as a literal - see PIN_TPS
    //in Table.h for the tunnelling arithmetic that picks it.
    SetPhysicsTPS(PIN_TPS);
    ApplyTilt();

    //Portrait, 3:2 - see PIN_WINDOW_WIDTH. The shots were solved for this aspect in SetupCamera.
    main_window->Resize(PIN_WINDOW_WIDTH,PIN_WINDOW_HEIGHT);

    //One tick so the first frame is not an empty world. Nothing simulates yet; this is the same
    //courtesy every other app in the repo extends.
    main_scene->StepPhysics(1);
}

void ApplicationPinball::BuildMaterials(){
    /*
        The palette is taken from the reference artwork in assets/artwork: a deep navy deck, teal
        and cream plastics, and one hot orange that nothing else on the table is allowed to be, so
        that the things which matter - the flippers, the lit lamps, the flashers - are the only
        orange things on screen.

        METALLIC IS AFFORDABLE HERE, unlike in Breakout, and that is entirely because
        BuildEnvironment turns on a skybox and reflections. Metallic trades a surface's diffuse
        term for reflections of the environment; with no environment it trades it for nothing and
        renders near-black (see the long note in core/Material.h). Half this table is chrome, so
        the environment is load-bearing rather than decoration - if the HDR ever fails to load, the
        rails will go dark and THAT is why.
    */
    struct Entry{
        const char* name;
        vec4  color;
        float metallic;
        float roughness;
        vec4  emissive;
        int*  slot;
    };
    const Entry entries[] = {
        //The deck. Dark, glossy and barely metallic - a lacquered playfield is a mirror for the
        //lights above it, not for the room.
        { "pin_deck",      vec4(0.055f,0.105f,0.165f,1.0f), 0.12f, 0.22f, vec4(0.04f,0.10f,0.14f,0.10f), &material_deck },
        //Painted lane lines and arrows on the deck: cream, flat, and unaffected by anything.
        { "pin_deck_line", vec4(0.88f,0.86f,0.78f,1.0f),    0.00f, 0.65f, vec4(0.62f,0.60f,0.54f,0.22f), &material_deck_line },
        //The cabinet: matte. It was a quarter metallic and the walls mirrored the woolshop HDR
        //back at every camera that looked at them from the side - a bright blur of wool where a
        //dark box should be. A painted cabinet reflects nothing worth seeing.
        { "pin_cabinet",   vec4(0.085f,0.090f,0.105f,1.0f), 0.00f, 0.65f, vec4(0,0,0,1),                 &material_cabinet },
        //Chrome: posts, ball, wire rails. Roughness low enough to mirror, not so low that it is a
        //single hard highlight and otherwise black.
        { "pin_chrome",    vec4(0.92f,0.94f,0.97f,1.0f),    0.95f, 0.13f, vec4(0,0,0,1),                 &material_chrome },
        //Guide rails, which on a real machine are dulled steel rather than mirror.
        { "pin_rail",      vec4(0.78f,0.81f,0.85f,1.0f),    0.80f, 0.28f, vec4(0,0,0,1),                 &material_rail },
        //Ramp floors: the pale blue-white of the moulded plastic ramps every table has had since
        //about 1985. Not actually transparent - core has no blending pass to do it honestly, and a
        //fake would read worse than an opaque ramp does.
        { "pin_ramp",      vec4(0.72f,0.82f,0.88f,1.0f),    0.10f, 0.16f, vec4(0.40f,0.56f,0.66f,0.18f), &material_ramp },
        { "pin_orange",    vec4(0.91f,0.34f,0.12f,1.0f),    0.15f, 0.34f, vec4(0.91f,0.34f,0.12f,0.30f), &material_orange },
        { "pin_teal",      vec4(0.12f,0.48f,0.50f,1.0f),    0.15f, 0.38f, vec4(0.10f,0.42f,0.45f,0.20f), &material_teal },
        { "pin_cream",     vec4(0.91f,0.89f,0.81f,1.0f),    0.05f, 0.42f, vec4(0.55f,0.53f,0.47f,0.14f), &material_cream },
        { "pin_rubber",    vec4(0.055f,0.055f,0.065f,1.0f), 0.00f, 0.72f, vec4(0,0,0,1),                 &material_rubber },
        { "pin_ball",      vec4(0.95f,0.96f,0.98f,1.0f),    1.00f, 0.08f, vec4(0,0,0,1),                 &material_ball },
        //A lit lamp insert. emissive.w is what actually makes something glow: emissive.rgb alone
        //is clamped to 1 and so can never be brighter than a fully lit white surface.
        { "pin_lamp_lit",  vec4(1.00f,0.62f,0.18f,1.0f),    0.00f, 0.30f, vec4(1.00f,0.55f,0.12f,1.60f), &material_lamp_lit },
        { "pin_lamp_dark", vec4(0.26f,0.20f,0.13f,1.0f),    0.00f, 0.40f, vec4(0.20f,0.13f,0.06f,0.10f), &material_lamp_dark },
        { "pin_backglass", vec4(0.10f,0.22f,0.30f,1.0f),    0.05f, 0.40f, vec4(0.14f,0.34f,0.42f,0.45f), &material_backglass },
        //The feature labels. Unlit on purpose - they are annotation, not part of the machine, and
        //annotation that takes a shadow is annotation that can become unreadable.
        { "pin_label",     vec4(0.55f,0.88f,0.92f,1.0f),    0.00f, 1.00f, vec4(0.30f,0.62f,0.70f,0.80f), &material_label },
        //The orbit's backdrop card. f_unlit is set on it below - it is the one surface here that
        //must not react to anything, because a backdrop that takes the camera lamp stops being a
        //constant and starts competing with the thing in front of it.
        { "pin_backdrop",  vec4(0.105f,0.115f,0.135f,1.0f), 0.00f, 1.00f, vec4(0,0,0,1),                 &material_backdrop },
    };

    for (int i = 0; i < (int)(sizeof(entries)/sizeof(entries[0])); i++){
        Material m;
        m.name = entries[i].name;
        m.glsl_material.color     = entries[i].color;
        m.glsl_material.metallic  = entries[i].metallic;
        m.glsl_material.roughness = entries[i].roughness;
        m.glsl_material.emissive  = entries[i].emissive;
        renderer->AddMaterial(m);
        //Looked up by name rather than taken from AddMaterial's return value: that returns
        //materials.size()-1 even when the name already existed, so it is only correct for a
        //material that is genuinely new. See docs/tetris_findings.md.
        *entries[i].slot = renderer->FindMaterialIndex(m.name);
    }

    //Unlit has to be set after the fact: the table above is a list of the five fields every entry
    //shares, and one material wanting a sixth is not worth a column of zeroes on fourteen rows.
    //`f_unlit` means the albedo goes to the screen as it is - no lights, no shadow, no
    //reflections - which is what makes the backdrop a constant rather than a surface.
    renderer->materials[material_backdrop].glsl_material.f_unlit = 1;
}

void ApplicationPinball::BuildEnvironment(){
    /*
        The woolshop HDR, converted to a cubemap once at startup.

        It is doing two jobs and only one of them is the sky. The other is being the thing every
        metal surface on this table reflects, which is most of what sells a pinball at this scale -
        a chrome ball with nothing to reflect is a grey circle. An interior with structure in it
        beats an open sky for exactly that reason: the rails pick up straight edges and the ball
        picks up something that moves as it rolls.
    */
    CubeMap* cubemap = new CubeMap();
    cubemap->LoadFromEquirectangular("textures/witsand_woolshop_2k.hdr");
    renderer->SetSkyboxCubemap(cubemap);
    renderer->UploadCubeMap(cubemap);
    renderer->skybox_shader = new Shader("shaders/skybox.vert","shaders/skybox.frag");
    renderer->skybox_mesh = skybox_mesh;
    /*
        REFLECTIONS ON, SKYBOX OFF - and the two really are independent. Renderer::UploadCubeMap
        binds the cubemap to its own texture unit once and leaves it there; only DrawSkyBox is
        gated on f_render_skybox. So the rails and the ball go on reflecting a woolshop that is
        never drawn.

        Which is what this table wants. Answer 2 to the design's open questions asks for the
        artwork's framing, and in the artwork the machine fills the frame against black - a visible
        room behind it competes with the playfield for every bit of contrast the deck has. The HDR
        is here to be reflected, not to be seen.

        f_use_reflections is OFF by default in the renderer and half the materials above assume it
        is on. If the rails ever render dark, this line is the first place to look.
    */
    renderer->f_render_skybox = false;
    renderer->f_use_reflections = true;
}

void ApplicationPinball::BuildDeckAndCabinet(){
    const float deck_cx = (PIN_DECK_MIN_X + PIN_DECK_MAX_X) * 0.5f;
    const float deck_cz = (PIN_DECK_MIN_Z + PIN_DECK_MAX_Z) * 0.5f;
    const float deck_w  = PIN_DECK_MAX_X - PIN_DECK_MIN_X;
    const float deck_l  = PIN_DECK_MAX_Z - PIN_DECK_MIN_Z;

    //One box, top face at y = 0, everything else buried. One collider and one collider only - a
    //playfield is the easiest surface on the whole machine to get right.
    Object* deck = AddBox("deck",vec3(deck_cx,-PIN_DECK_THICKNESS * 0.5f,deck_cz),
                          vec3(deck_w,PIN_DECK_THICKNESS,deck_l),material_deck);
    AddBoxCollider(deck,vec3(deck_w,PIN_DECK_THICKNESS,deck_l),PIN_DECK_BOUNCINESS,PIN_DECK_FRICTION);

    /*
        The glass: a body with no mesh, a slab over the whole cabinet at PIN_GLASS_HEIGHT. Nothing
        should ever reach it - the deck is flat, the walls are vertical and the capsules keep
        their round ends out of the ball's band - but "should" is not a collider, and a ball that
        does get airborne has to come down inside the cabinet rather than over its wall.
    */
    glass = new Object();
    glass->name = "glass";
    glass->SetPosition(vec3(0.0f,PIN_GLASS_HEIGHT + PIN_WALL_THICKNESS * 0.5f,0.0f));
    glass->SetPickability(false);
    main_scene->AddObject(glass);
    AddBoxCollider(glass,vec3(deck_w + PIN_WALL_THICKNESS * 2.0f,PIN_WALL_THICKNESS,
                              deck_l + PIN_WALL_THICKNESS * 2.0f),PIN_DECK_BOUNCINESS,PIN_DECK_FRICTION);

    //The four cabinet walls, their INNER faces on the play area's bounds and their thickness
    //outward. 0.6 thick is not styling: it is twice the worst-case per-tick travel, which is
    //mitigation 1 of pinball_design.md 2.2 and the only reason a ball at 90 u/s stays inside.
    const float t  = PIN_WALL_THICKNESS;
    const float h  = PIN_CABINET_HEIGHT;
    const float cy = h * 0.5f;
    const float inner_x = PIN_DECK_MAX_X - 0.05f;      //2.85: the deck runs 0.05 under the wall
    const float inner_z = PIN_DECK_MAX_Z;

    //Drawn and collided as the same four boxes - the one place the design's 0.6-thick collider
    //rule applies as written, because there is nothing behind a cabinet wall to close off.
    {
        Object* wall;
        wall = AddBox("cabinet_left", vec3(-inner_x - t * 0.5f,cy,0.0f),
                      vec3(t,h,(inner_z + t) * 2.0f),material_cabinet);
        AddBoxCollider(wall,vec3(t,h,(inner_z + t) * 2.0f),PIN_RAIL_BOUNCINESS,PIN_RAIL_FRICTION);
        wall = AddBox("cabinet_right",vec3( inner_x + t * 0.5f,cy,0.0f),
                      vec3(t,h,(inner_z + t) * 2.0f),material_cabinet);
        AddBoxCollider(wall,vec3(t,h,(inner_z + t) * 2.0f),PIN_RAIL_BOUNCINESS,PIN_RAIL_FRICTION);
        wall = AddBox("cabinet_top",  vec3(0.0f,cy,-inner_z - t * 0.5f),
                      vec3(inner_x * 2.0f,h,t),material_cabinet);
        AddBoxCollider(wall,vec3(inner_x * 2.0f,h,t),PIN_RAIL_BOUNCINESS,PIN_RAIL_FRICTION);
    }
    /*
        The front wall is HALF the height of the other three, and that is a real cabinet's shape
        rather than a saving: the glass slopes down to meet it, the lockdown bar sits on it, and
        the plunger comes out through it.

        It also has to be, for a reason that only shows up once a camera is pointed at it. The
        outhole is 0.10 behind this wall, so at the full 1.30 there is no viewpoint down-table of
        the cabinet that can see the drain at all - the sight line would have to clear 1.30 of wall
        over 0.10 of run. The "lower" shot spent a revision framing a grey slab before this was
        obvious. 0.55 is still two ball diameters, so it contains everything it needs to.
    */
    {
        Object* front = AddBox("cabinet_front",vec3(0.0f,PIN_CABINET_FRONT_HEIGHT * 0.5f, inner_z + t * 0.5f),
                               vec3(inner_x * 2.0f,PIN_CABINET_FRONT_HEIGHT,t),material_cabinet);
        AddBoxCollider(front,vec3(inner_x * 2.0f,PIN_CABINET_FRONT_HEIGHT,t),PIN_RAIL_BOUNCINESS,PIN_RAIL_FRICTION);
    }

    /*
        The apron, in two plates with the drain mouth between them. On a real machine this is where
        the instruction cards go, and the reference artwork has exactly that - two cards and a
        SHOOT AGAIN plate in the middle. Here it is two boxes and a gap.
    */
    const float apron_cz = (PIN_APRON_MIN_Z + inner_z) * 0.5f;
    const float apron_l  = inner_z - PIN_APRON_MIN_Z;
    {
        float x0 = -inner_x;
        float x1 = PIN_APRON_GAP_MIN_X;
        AddBox("apron_left",vec3((x0 + x1) * 0.5f,PIN_PLATE_THICKNESS * 0.5f,apron_cz),
               vec3(x1 - x0,PIN_PLATE_THICKNESS,apron_l),material_cream);
    }
    {
        float x0 = PIN_APRON_GAP_MAX_X;
        float x1 = PIN_PLAY_MAX_X;
        AddBox("apron_right",vec3((x0 + x1) * 0.5f,PIN_PLATE_THICKNESS * 0.5f,apron_cz),
               vec3(x1 - x0,PIN_PLATE_THICKNESS,apron_l),material_cream);
    }
    //The outhole itself: a dark disc in the mouth, so the drain is somewhere the eye can find.
    AddInsert("drain",PIN_DRAIN_X,PIN_DRAIN_Z,0.22f,material_rubber);
}

void ApplicationPinball::BuildLowerPlayfield(){
    /*
        Everything below the ramp mouths, built once for the left and once mirrored - the mirror is
        PIN_MIRROR_X and not a second set of typed numbers, so the two halves cannot disagree. On
        the right the "cabinet" is the chute divider, which is exactly what PIN_MIRROR_X maps the
        left wall onto.

        Read it as four lanes side by side at z = 2.6, outside in:
            wall -2.85 | -2.35 outlane guide | outlane | -1.80 divider | inlane | -1.25 slingshot
        which is the arrangement every machine has had for fifty years. Each lane is 0.40 clear -
        1.5 balls - and tools/pinball_plan.py is what says so; the first draft's were measured
        centre to centre and one of them was 0.24.
    */
    for (int side = 0; side < 2; side++){
        const bool f_mirror = (side == 1);
        const char* tag = f_mirror ? "right" : "left";
        //One lambda-free helper: mirror an x, or do not.
        #define PIN_X(v) (f_mirror ? PIN_MIRROR_X(v) : (v))
        char name[64];

        //The inlane / outlane divider: straight, then bending inboard to end just outboard of
        //and up-table of the flipper pivot, so the inlane delivers the ball ONTO the bat. See
        //PIN_DIVIDER_END_X for why this bend is the whole inlane.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_MIN_Z);
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_BEND_Z);
            AppendXZ(p,PIN_X(PIN_DIVIDER_END_X),PIN_DIVIDER_END_Z);
            snprintf(name,sizeof(name),"divider_%s",tag);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        //The outlane's outer guide, FROM THE WALL: diagonally in to its knee and then straight
        //down. Starting at the wall is what closes the strip along the cabinet that the first
        //draft left open for the whole length of the table.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_PLAY_MIN_X),PIN_OUTLANE_TOP_Z);
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X),PIN_OUTLANE_KNEE_Z);
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X),PIN_OUTLANE_MAX_Z);
            snprintf(name,sizeof(name),"outlane_guide_%s",tag);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        //The slingshot: a closed triangle of rubber - kicking face, bottom, outer side - with a
        //plastic floating over it. The outer side is what bounds the inlane (PIN_SLING_L_AX).
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_SLING_L_AX),PIN_SLING_L_AZ);
            AppendXZ(p,PIN_X(PIN_SLING_L_BX),PIN_SLING_L_BZ);
            AppendXZ(p,PIN_X(PIN_SLING_L_CX),PIN_SLING_L_CZ);
            AppendXZ(p,PIN_X(PIN_SLING_L_AX),PIN_SLING_L_AZ);
            snprintf(name,sizeof(name),"sling_%s",tag);
            //Rubber: the liveliest thing on the table. The kick a real slingshot adds on top of
            //the bounce is stage 2's, with the switch that fires it.
            AddWall(name,p,PIN_SLING_HEIGHT,PIN_SLING_THICKNESS,material_rubber,PIN_RUBBER_BOUNCINESS);

            //The plastic that covers the mechanism. Reads as the orange wedge the artwork has
            //above each flipper; centred on the triangle, a shade larger than it.
            float cx = PIN_X((PIN_SLING_L_AX + PIN_SLING_L_BX + PIN_SLING_L_CX) / 3.0f);
            float cz = (PIN_SLING_L_AZ + PIN_SLING_L_BZ + PIN_SLING_L_CZ) / 3.0f;
            //The kicking face runs 0.45 across and 0.85 down-table: 62 degrees off the x axis.
            //A positive yaw about +Y takes +X toward -Z, so the left one leans by MINUS that.
            float yaw = f_mirror ? 62.0f : -62.0f;
            snprintf(name,sizeof(name),"sling_plastic_%s",tag);
            AddBox(name,vec3(cx,PIN_SLING_HEIGHT + 0.02f,cz),vec3(1.00f,0.04f,0.30f),
                   material_cream,yaw);
        }
        //The wall that funnels a drained ball from the outlane across to the outhole, under the
        //apron. Visible here because there is no glass and no apron art yet; it will be hidden.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X - 0.05f),PIN_OUTLANE_MAX_Z);
            AppendXZ(p,PIN_X(PIN_APRON_GAP_MIN_X - 0.23f),PIN_APRON_MIN_Z + 0.18f);
            snprintf(name,sizeof(name),"outhole_funnel_%s",tag);
            AddWall(name,p,0.26f,PIN_RAIL_VISUAL_THICK,material_rail);
        }

        //The switches, as lamp inserts. Dark until stage 4 has something to light them for.
        snprintf(name,sizeof(name),"insert_inlane_%s",tag);
        AddInsert(name,PIN_X(PIN_INLANE_L_X),PIN_INLANE_L_Z,0.13f,material_lamp_dark);
        snprintf(name,sizeof(name),"insert_save_%s",tag);
        AddInsert(name,PIN_X(PIN_SAVE_L_X),PIN_SAVE_L_Z,0.15f,material_lamp_dark);

        //The post beside each ramp mouth, turning a ball coming down the side of the table into
        //the mouth instead of letting it run down the wall. Not mirrored: the right one has the
        //chute divider for a wall and sits a little further in.
        snprintf(name,sizeof(name),"post_mouth_%s",tag);
        AddPost(name,f_mirror ? PIN_POST_MOUTH_R_X : PIN_POST_MOUTH_L_X,
                     f_mirror ? PIN_POST_MOUTH_R_Z : PIN_POST_MOUTH_L_Z);

        #undef PIN_X
    }

    //The three flippers, each a hinge with a motor, parked at rest until RunSimulationTick reads
    //a button. The upper one shares the left button.
    flipper_left  = AddFlipper("flipper_left", PIN_FLIPPER_L_X,PIN_FLIPPER_L_Z,PIN_FLIPPER_LENGTH,
                               PIN_FLIPPER_REST_DEG,PIN_FLIPPER_UP_DEG,false);
    flipper_right = AddFlipper("flipper_right",PIN_FLIPPER_R_X,PIN_FLIPPER_R_Z,PIN_FLIPPER_LENGTH,
                               PIN_FLIPPER_REST_DEG,PIN_FLIPPER_UP_DEG,true);
    flipper_upper = AddFlipper("flipper_upper",PIN_FLIPPER_U_X,PIN_FLIPPER_U_Z,PIN_FLIPPER_U_LENGTH,
                               PIN_FLIPPER_U_REST_DEG,PIN_FLIPPER_U_UP_DEG,false);
}


void ApplicationPinball::BuildLauncher(){
    //The chute divider: the long wall separating the launch lane from the play area, from the
    //plunger all the way up to where it hands the ball to the top orbit.
    {
        PinPath p;
        AppendXZ(p,PIN_CHUTE_DIVIDER_X,PIN_CHUTE_MAX_Z);
        AppendXZ(p,PIN_CHUTE_DIVIDER_X,PIN_CHUTE_MIN_Z);
        AddWall("chute_divider",p,PIN_CHUTE_DIVIDER_HEIGHT,PIN_CHUTE_DIVIDER_THICK,material_rail);
    }

    /*
        The plunger, as three pieces along the lane: the TIP is the mechanism - a slider joint
        with a spring return and a motorised pull-back (Mechanisms.h) - and the rod and the knob
        are scenery that RunSimulationTick moves along behind it. The rod passes THROUGH the
        cabinet wall and the knob sits outside it, which is the one part of a plunger that has to
        be modelled rather than implied.
    */
    const float ball_y = PIN_BALL_RADIUS;
    {
        //The modelled parts and the primitives both lie along +Y, MakeCylinder's axis of
        //revolution; the plunger's is +Z. Rotating +90 about X takes +Y to +Z, which is the whole
        //conversion, and it applies to either. The Plunger does it for the tip itself.
        const quat onto_z = quat(vec3(1,0,0),toradians(90.0f));
        Mesh* tip = HasPart("plunger_tip") ? NULL : MakeCylinder(0.11f,PIN_PLUNGER_TIP_LENGTH,18,true);
        plunger = new Plunger("plunger_tip",assetmanager,"plunger_tip",tip,material_chrome,
                              main_scene->physics_world,main_scene,
                              vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + PIN_PLUNGER_TIP_LENGTH * 0.5f),
                              PIN_PLUNGER_TRAVEL);

        Mesh* rod = HasPart("plunger_rod") ? NULL : MakeCylinder(0.05f,0.95f,12,true);
        plunger_rod = NewPartObject("plunger_rod","plunger_rod",rod,
                                    vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + 0.58f),material_chrome);
        if (plunger_rod) plunger_rod->SetRotation(onto_z);

        Mesh* knob = HasPart("plunger_knob") ? NULL : MakeSphere(0.14f,18,10);
        plunger_knob = NewPartObject("plunger_knob","plunger_knob",knob,
                                     vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + 1.30f),material_orange);
        if (plunger_knob) plunger_knob->SetRotation(onto_z);
    }

    //The three skill-shot rollovers up the lane, and the gate at the top of it.
    AddInsert("insert_skill_low", PIN_CHUTE_X,PIN_SKILL_Z_0,0.13f,material_lamp_dark);
    AddInsert("insert_skill_mid", PIN_CHUTE_X,PIN_SKILL_Z_1,0.13f,material_lamp_dark);
    AddInsert("insert_skill_high",PIN_CHUTE_X,PIN_SKILL_Z_2,0.13f,material_lamp_lit);

    //The one-way gate. A hinge with asymmetric limits in stage 1 - it opens into the orbit and
    //will not open back - so it is drawn where the flap hangs, leaning into the lane.
    if (HasPart("gate_flap")){
        Object* flap = NewPartObject("gate_flap","gate_flap",NULL,vec3(PIN_GATE_X,0.20f,PIN_GATE_Z),
                                     material_chrome);
        if (flap) flap->SetRotation(quat(vec3(0,1,0),toradians(-24.0f)));
    }else{
        AddBox("gate_flap",vec3(PIN_GATE_X,0.20f,PIN_GATE_Z),vec3(0.30f,0.30f,0.05f),
               material_chrome,-24.0f);
    }

    //The ball, resting against the plunger tip. The one thing on the table everything is scaled
    //to, and from stage 1 the one dynamic body that matters.
    BuildBall();
    //The gate flap, as the box it is drawn as. It swings open for a ball coming UP the lane, so
    //it is recorded as an obstacle the plan tool is told to ignore for reachability - see there.
    {
        PinPlanEntry& entry = RecordPlan("box","gate_orbit");
        entry.points.push_back(vec3(PIN_GATE_X,0.0f,PIN_GATE_Z));
        entry.a = 0.30f;
        entry.b = 0.05f;
        entry.c = 0.35f;
        entry.d = -24.0f;
    }
}

void ApplicationPinball::BuildUpperPlayfield(){
    //The top orbit: one arc, sampled fine enough that the chord error is a twentieth of the ball's
    //radius. This single curve is the signature shot on the machine.
    {
        PinPath p;
        AppendArc(p,vec3(PIN_ORBIT_CX,0.0f,PIN_ORBIT_CZ),PIN_ORBIT_RADIUS,
                  PIN_ORBIT_START_DEG,PIN_ORBIT_END_DEG,PIN_ARC_SEGMENTS);
        //Dulled steel, not chrome: a 4.6-wide mirror across the top of the table reflected the
        //HDR as a bright striped band and read as a light fitting rather than a rail.
        AddWall("rail_orbit",p,PIN_ORBIT_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }
    //The orbit's outer guide, tangent to each side wall - see PIN_ORBIT_OUTER_X for why it is
    //two Beziers and not the cabinet.
    {
        PinPath p;
        const float xo = -PIN_ORBIT_OUTER_X;    //the right-hand side; the define is the left
        AppendBezier(p,vec3( xo,0.0f,PIN_ORBIT_OUTER_START_Z),vec3( xo,0.0f,PIN_ORBIT_OUTER_APEX_Z),
                       vec3(0.0f,0.0f,PIN_ORBIT_OUTER_APEX_Z),PIN_ARC_SEGMENTS / 2);
        AppendBezier(p,vec3(0.0f,0.0f,PIN_ORBIT_OUTER_APEX_Z),vec3(-xo,0.0f,PIN_ORBIT_OUTER_APEX_Z),
                       vec3(-xo,0.0f,PIN_ORBIT_OUTER_START_Z),PIN_ARC_SEGMENTS / 2);
        AddWall("rail_orbit_outer",p,PIN_ORBIT_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }

    /*
        The left orbit return: the lane a ball exiting the horseshoe runs down, on its way to the
        upper flipper. Its outer wall is the cabinet, so this is only the inner one - and it starts
        exactly where the orbit rail ends, so the ball is never handed off across a gap. Widths
        and why it stops where it does: PIN_RETURN_X.
    */
    {
        PinPath p;
        AppendXZ(p,PIN_RETURN_X + 0.02f,PIN_RETURN_MIN_Z);
        AppendXZ(p,PIN_RETURN_X,PIN_RETURN_MAX_Z);
        AddWall("rail_orbit_return",p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }

    //The FUEL lanes: three rollovers under the orbit, divided by four short walls a lane-pitch
    //apart. The outer two stay inside the horseshoe (see PIN_FUEL_Z).
    {
        for (int i = 0; i < 4; i++){
            PinPath p;
            const float x = PIN_FUEL_WALL_X_0 + PIN_FUEL_PITCH * (float)i;
            AppendXZ(p,x,PIN_FUEL_WALL_MIN_Z);
            AppendXZ(p,x,PIN_FUEL_WALL_MAX_Z);
            char name[48];
            snprintf(name,sizeof(name),"fuel_wall_%i",i);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        AddInsert("insert_fuel_f",PIN_FUEL_X_0,PIN_FUEL_Z,0.15f,material_lamp_lit);
        AddInsert("insert_fuel_u",PIN_FUEL_X_1,PIN_FUEL_Z,0.15f,material_lamp_dark);
        AddInsert("insert_fuel_e",PIN_FUEL_X_2,PIN_FUEL_Z,0.15f,material_lamp_dark);
    }

    //The three wormhole saucers. A rim and a dark hole; the subway underneath is pure code and
    //will never be seen (pinball_design.md 1.4).
    struct Saucer{ const char* name; float x; float z; };
    const Saucer saucers[] = {
        { "wormhole_left",   PIN_WORM_0_X,PIN_WORM_0_Z },
        { "wormhole_centre", PIN_WORM_1_X,PIN_WORM_1_Z },
        { "wormhole_right",  PIN_WORM_2_X,PIN_WORM_2_Z },
    };
    for (int i = 0; i < 3; i++){
        char name[48];
        snprintf(name,sizeof(name),"%s_rim",saucers[i].name);
        NewPartObject("saucer_rim",name,saucer_mesh,vec3(saucers[i].x,0.02f,saucers[i].z),
                      material_teal);

        //0.07 is the rim's own top face - it is a 0.10-tall disc centred at 0.02. Putting the hole
        //there leaves a 0.05 ring of rim showing around a dark centre, which is what a saucer is.
        snprintf(name,sizeof(name),"%s_hole",saucers[i].name);
        AddInsert(name,saucers[i].x,saucers[i].z,PIN_SAUCER_RADIUS - 0.05f,material_rubber,0.07f);

        //A saucer is a disc the ball has to be able to reach the MIDDLE of, so its "collision"
        //radius is zero: nothing about it blocks a ball. b is the rim, c the rim's height.
        PinPlanEntry& entry = RecordPlan("disc",saucers[i].name);
        entry.points.push_back(vec3(saucers[i].x,0.0f,saucers[i].z));
        entry.a = 0.0f;
        entry.b = PIN_SAUCER_RADIUS;
        entry.c = 0.07f;
    }
}


void ApplicationPinball::BuildScoringCluster(){
    //--- Pop bumpers ----------------------------------------------------------------------------
    struct Bumper{ const char* name; float x; float z; };
    const Bumper bumpers[] = {
        { "bumper_left", PIN_BUMPER_0_X,PIN_BUMPER_0_Z },
        { "bumper_right",PIN_BUMPER_1_X,PIN_BUMPER_1_Z },
        { "bumper_low",  PIN_BUMPER_2_X,PIN_BUMPER_2_Z },
    };
    for (int i = 0; i < 3; i++){
        char name[48];
        //The body is what the ball meets - PIN_BUMPER_RADIUS. In stage 1 it is a CAPSULE with its
        //round caps buried below the deck and above the ball's band, so that the ball only ever
        //touches the straight middle: core has no cylinder collider, and a sphere-capped one would
        //throw the ball upward off a shape that is visibly vertical.
        snprintf(name,sizeof(name),"%s_body",bumpers[i].name);
        Object* body = NewPartObject("bumper_body",name,bumper_body_mesh,
                                     vec3(bumpers[i].x,PIN_BUMPER_HEIGHT * 0.5f,bumpers[i].z),material_cream);
        //Static for now: a hard round thing the ball bounces off. The kick, and the switch that
        //fires it, are stage 2.
        AddPostCollider(body,PIN_BUMPER_RADIUS,PIN_PLASTIC_BOUNCINESS,PIN_RAIL_FRICTION);

        //The cap, which is the wider thing the player sees and the thing the art has to clear.
        //Skirt radius, not collider radius - the gap between the two IS the design.
        snprintf(name,sizeof(name),"%s_cap",bumpers[i].name);
        NewPartObject("bumper_cap",name,bumper_cap_mesh,
                      vec3(bumpers[i].x,PIN_BUMPER_HEIGHT + 0.04f,bumpers[i].z),material_orange);

        //The lit ring in the deck under it.
        snprintf(name,sizeof(name),"%s_insert",bumpers[i].name);
        AddInsert(name,bumpers[i].x,bumpers[i].z,PIN_BUMPER_SKIRT_RADIUS,material_lamp_dark);

        //disc: one point, a = the radius the ball meets, b = the radius the art needs, c = how
        //tall it stands. For a bumper those are the collider, the skirt and the cap's top.
        PinPlanEntry& entry = RecordPlan("disc",bumpers[i].name);
        entry.points.push_back(vec3(bumpers[i].x,0.0f,bumpers[i].z));
        entry.a = PIN_BUMPER_RADIUS;
        entry.b = PIN_BUMPER_SKIRT_RADIUS;
        entry.c = PIN_BUMPER_HEIGHT + 0.11f;
    }
    //No nest guides. The first draft had two angled walls hemming the pops in; on a table this
    //length they had to stand under the ramps' climbs, where a 0.44 wall meets a 0.5 ramp floor.
    //The ramp mouths, the saucer rims and the standups bound the nest instead.

    //--- The MISSION drop target bank ------------------------------------------------------------
    //Kinematic in stage 2, dropped with Scene::MoveObjectOverTicks - no joint needed, and it steps
    //and replays correctly, which a hand-animated transform would not. Backs onto the chute
    //divider, which is already there and already 0.55 tall, so it has no wall of its own.
    {
        const float z[3] = { PIN_DROP_Z_0,PIN_DROP_Z_1,PIN_DROP_Z_2 };
        const char* letters[3] = { "drop_m","drop_i","drop_s" };
        for (int i = 0; i < 3; i++){
            const vec3 centre = vec3(PIN_DROP_X,PIN_DROP_HEIGHT * 0.5f,z[i]);
            const vec3 size = vec3(PIN_DROP_DEPTH,PIN_DROP_HEIGHT,PIN_DROP_WIDTH);
            Object* target = HasPart("target_drop")
                           ? NewPartObject("target_drop",letters[i],NULL,centre,material_cream)
                           : AddBox(letters[i],centre,size,material_cream);
            AddBoxCollider(target,size,PIN_PLASTIC_BOUNCINESS,PIN_RAIL_FRICTION);
            //box: one point at the centre, a = size along x, b = size along z, c = height,
            //d = yaw in degrees. A target is a thing the ball has to be able to hit the FACE of.
            PinPlanEntry& entry = RecordPlan("box",letters[i]);
            entry.points.push_back(vec3(PIN_DROP_X,0.0f,z[i]));
            entry.a = PIN_DROP_DEPTH;
            entry.b = PIN_DROP_WIDTH;
            entry.c = PIN_DROP_HEIGHT;
        }
        AddPost("post_drop",PIN_POST_DROP_X,PIN_POST_DROP_Z);
    }

    //--- Standups ---------------------------------------------------------------------------------
    //Facing the player, just below the two upper pops. See PIN_STANDUP_0_X for why not on a wall.
    {
        const char* names[2] = { "standup_upper","standup_lower" };
        const float xs[2] = { PIN_STANDUP_0_X,PIN_STANDUP_1_X };
        const float zs[2] = { PIN_STANDUP_0_Z,PIN_STANDUP_1_Z };
        for (int i = 0; i < 2; i++){
            const vec3 centre = vec3(xs[i],PIN_STANDUP_HEIGHT * 0.5f,zs[i]);
            const vec3 size = vec3(PIN_STANDUP_WIDTH,PIN_STANDUP_HEIGHT,PIN_STANDUP_DEPTH);
            Object* target = HasPart("target_standup")
                           ? NewPartObject("target_standup",names[i],NULL,centre,material_teal)
                           : AddBox(names[i],centre,size,material_teal);
            AddBoxCollider(target,size,PIN_PLASTIC_BOUNCINESS,PIN_RAIL_FRICTION);
            PinPlanEntry& entry = RecordPlan("box",names[i]);
            entry.points.push_back(vec3(xs[i],0.0f,zs[i]));
            entry.a = PIN_STANDUP_WIDTH;
            entry.b = PIN_STANDUP_DEPTH;
            entry.c = PIN_STANDUP_HEIGHT;
        }
    }

    //--- The spinner ------------------------------------------------------------------------------
    //A free hinge, hanging in the mouth of the left ramp from the ramp's own rails, so the ball
    //passes under it on the way in. Counted by revolutions off the joint's own angle rather than
    //by contacts. The axle is drawn as a wire between the rails; the vane hangs from it.
    {
        const vec3 centre = vec3(PIN_SPINNER_X,PIN_RAMP_RAIL_HEIGHT - PIN_SPINNER_HEIGHT * 0.5f,
                                 PIN_SPINNER_Z);
        if (HasPart("spinner_vane")){
            NewPartObject("spinner_vane","spinner_vane",NULL,centre,material_chrome);
        }else{
            AddBox("spinner_vane",centre,vec3(PIN_SPINNER_WIDTH,PIN_SPINNER_HEIGHT,0.03f),
                   material_chrome);
        }
        PinPath axle;
        AppendXZ(axle,PIN_SPINNER_X - PIN_RAMP_WIDTH * 0.5f - 0.06f,PIN_SPINNER_Z,PIN_RAMP_RAIL_HEIGHT);
        AppendXZ(axle,PIN_SPINNER_X + PIN_RAMP_WIDTH * 0.5f + 0.06f,PIN_SPINNER_Z,PIN_RAMP_RAIL_HEIGHT);
        Mesh* wire = MakeTube(axle,0.02f,8);
        AddMeshObject("spinner_axle",wire,vec3(),material_chrome);
    }

    //--- The gravity well -------------------------------------------------------------------------
    {
        NewPartObject("saucer_rim","gravity_well_rim",saucer_mesh,vec3(PIN_WELL_X,0.02f,PIN_WELL_Z),
                      material_orange);
        AddInsert("gravity_well_hole",PIN_WELL_X,PIN_WELL_Z,PIN_SAUCER_RADIUS - 0.05f,
                  material_rubber,0.07f);
        PinPlanEntry& entry = RecordPlan("disc","gravity_well");
        entry.points.push_back(vec3(PIN_WELL_X,0.0f,PIN_WELL_Z));
        entry.a = 0.0f;
        entry.b = PIN_SAUCER_RADIUS;
        entry.c = 0.07f;
    }
}


void ApplicationPinball::BuildRamps(){
    /*
        A ramp is two things off ONE centreline.

        While its floor is still part of the deck it is a moulded RAMP: a ribbon with a thin rail
        standing outside each edge, so the full PIN_RAMP_WIDTH is clear for the ball - the first
        build stood the rails ON the floor's edges and left 0.18 between them, which is not a ramp
        a ball can enter. Once the floor is in the air it is a HABITRAIL: four wires the ball rides
        between - two under it, two beside it - and nothing else, so that seen from above it hides
        almost none of the deck below.

        That is the whole argument for TableBuilder: rails and wires cannot drift from the floor
        because they are not independently authored, and in stage 3 the collider chain reads the
        same paths.
    */
    struct Ramp{
        const char* name;
        const vec3* points;
        int count;
    };
    const Ramp ramps[] = {
        { "ramp_left", kLeftRampPath, (int)(sizeof(kLeftRampPath)/sizeof(kLeftRampPath[0])) },
        { "ramp_right",kRightRampPath,(int)(sizeof(kRightRampPath)/sizeof(kRightRampPath[0])) },
    };

    for (int r = 0; r < 2; r++){
        PinPath centre;
        for (int i = 0; i < ramps[r].count; i++){
            AppendPoint(centre,ramps[r].points[i]);
        }

        //ramp: points = the centreline the ball runs along, a = floor width, b = rail height,
        //c = how far the floor hangs below the centreline. The rails are NOT recorded as walls -
        //a rail 0.8 up in the air is not a wall on the deck - the plan tool derives the deck-level
        //footprint from the centreline's height itself, with the same PIN_RAMP_AIRBORNE_Y rule.
        PinPlanEntry& entry = RecordPlan("ramp",ramps[r].name);
        entry.points = centre;
        entry.a = PIN_RAMP_WIDTH;
        entry.b = PIN_RAMP_RAIL_HEIGHT;
        entry.c = PIN_RAMP_SLAB;

        //Split where the floor leaves the deck: the climb is every point a ball on the deck could
        //not pass under - the LAST such point is where the wires take over. The plan tool uses
        //the same rule for which stretch of a ramp is a pair of walls, so the two cannot disagree.
        const int n = (int)centre.size();
        int split = 0;
        while (split + 1 < n - 1 && centre[split + 1].y < PIN_RAMP_AIRBORNE_Y){
            split++;
        }
        PinPath climb(centre.begin(),centre.begin() + split + 1);
        PinPath air(centre.begin() + split,centre.end());

        char name[64];
        snprintf(name,sizeof(name),"%s_floor",ramps[r].name);
        Mesh* floor = MakeRibbon(climb,PIN_RAMP_WIDTH,PIN_RAMP_SLAB);
        Object* floor_object = AddMeshObject(name,floor,vec3(),material_ramp);
        /*
            The climb's floor collides, so a ball CAN go up a ramp - and off the crest onto the
            deck below, because the wires that carry it home are stage 3's (a capsule chain per
            wire, off the same paths). The same AddSweptColliders as every wall; the tilt is the
            path's own.

            IT IS SOLID TO THE DECK, not a slab. The space under a climbing floor is a wedge: open
            at the crest end where the underside is 0.47 up, a ball's height wide a little further
            down, and a ball that wanders in from up-table - which is where the upper flipper
            sends it - rolls forward until it jams. A real ramp sits on a housing that closes
            that space, so the collider hangs PIN_RAMP_SKIRT below the path and the dark skirt
            below is the housing the player sees. Buried in the deck at the mouth, a wall at the
            crest end.
        */
        AddSweptColliders(floor_object,climb,PIN_RAMP_WIDTH * 0.5f,PIN_RAMP_SKIRT,0.0f,
                          PIN_PLASTIC_BOUNCINESS,PIN_RAIL_FRICTION);
        snprintf(name,sizeof(name),"%s_housing",ramps[r].name);
        Mesh* housing = MakeSweptBox(climb,PIN_RAMP_WIDTH * 0.5f - 0.005f,PIN_RAMP_SKIRT,
                                     -PIN_RAMP_SLAB,true);
        AddMeshObject(name,housing,vec3(),material_cabinet);

        /*
            The rails stand OUTSIDE the floor's edges, so the floor's whole width is clear - and
            they reach DOWN TO THE DECK, not just down to the floor. A rail that only stood on the
            floor's edge rose with it, and a ball on the deck beside the climb met the rail's
            underside at exactly its own height and jammed under it; the first plunged ball ended
            its journey wedged there. A moulded ramp's side is solid to the deck, so the rail hangs
            PIN_RAMP_SKIRT below the path: buried in the deck at the mouth, a closed wall higher up.
        */
        const float rail_offset = (PIN_RAMP_WIDTH + PIN_RAMP_RAIL_THICK) * 0.5f;
        for (int side = 0; side < 2; side++){
            PinPath edge = OffsetPath(climb,side == 0 ? rail_offset : -rail_offset);
            snprintf(name,sizeof(name),"%s_rail_%c",ramps[r].name,side == 0 ? 'l' : 'r');
            Mesh* rail = MakeSweptBox(edge,PIN_RAMP_RAIL_THICK * 0.5f,PIN_RAMP_SKIRT,PIN_RAMP_RAIL_HEIGHT,true);
            Object* rail_object = AddMeshObject(name,rail,vec3(),material_ramp);
            AddSweptColliders(rail_object,edge,PIN_RAMP_RAIL_THICK * 0.5f,PIN_RAMP_SKIRT,PIN_RAMP_RAIL_HEIGHT,
                              PIN_PLASTIC_BOUNCINESS,PIN_RAIL_FRICTION);
        }

        /*
            The habitrail. Two wires the ball sits on, 0.22 apart so a 0.27 ball rests between them
            with its underside at the path; two more at its waist height to keep it in. The
            offsets are what a real wireform uses and they are also what stage 3's colliders will
            be, one capsule chain per wire.
        */
        struct Wire{ const char* suffix; float offset; float rise; };
        const Wire wires[4] = {
            { "wire_floor_l",  0.11f, 0.08f },
            { "wire_floor_r", -0.11f, 0.08f },
            { "wire_side_l",   0.23f, 0.30f },
            { "wire_side_r",  -0.23f, 0.30f },
        };
        if (air.size() >= 2){
            for (int w = 0; w < 4; w++){
                PinPath wire = OffsetPath(air,wires[w].offset,wires[w].rise);
                snprintf(name,sizeof(name),"%s_%s",ramps[r].name,wires[w].suffix);
                Mesh* tube = MakeTube(wire,PIN_HABITRAIL_RADIUS,10);
                AddMeshObject(name,tube,vec3(),material_chrome);
            }
        }

        //A lit arrow-sized insert at the mouth, because a ramp nobody can see the mouth of is a
        //ramp nobody shoots.
        snprintf(name,sizeof(name),"insert_%s",ramps[r].name);
        AddInsert(name,ramps[r].points[0].x,ramps[r].points[0].z + 0.45f,0.17f,material_lamp_lit);
    }
}


void ApplicationPinball::BuildBackbox(){
    /*
        Not playable, and not simulated - it is where the score, the rank and the mission text go in
        stage 5. Standing it up now costs two boxes and settles the one thing about it that matters
        to the camera: how much room above the table the framing has to leave.
    */
    AddBox("backbox",vec3(0.0f,PIN_BACKBOX_HEIGHT * 0.5f,PIN_BACKBOX_Z),
           vec3(PIN_DECK_MAX_X * 2.0f + PIN_WALL_THICKNESS * 2.0f - 0.10f,
                PIN_BACKBOX_HEIGHT,PIN_BACKBOX_DEPTH),material_cabinet,0.0f);
    //The glass itself, a hair proud of the housing and leaning back the way a real one does.
    Object* glass = AddBox("backglass",vec3(0.0f,PIN_BACKBOX_HEIGHT * 0.5f + 0.05f,
                                           PIN_BACKBOX_Z + PIN_BACKBOX_DEPTH * 0.5f + 0.05f),
                           vec3(6.10f,PIN_BACKBOX_HEIGHT - 0.70f,PIN_PLATE_THICKNESS),
                           material_backglass);
    if (glass){
        glass->SetRotation(quat(vec3(1,0,0),toradians(-8.0f)));
    }
}


void ApplicationPinball::BuildOrbitBackdrop(){
    /*
        A room around the machine, for the orbit camera to have something behind the table.

        Five slabs rather than one inside-out box. A box wound for viewing from outside is entirely
        back-faced from within it, so using one would mean flipping the winding with a negative
        scale - a trick that also flips the normals and interacts with the shadow pass. A slab is
        just a box seen from outside, which is the case everything already handles.

        Big enough that the orbit's maximum zoom-out still sits inside it, and offset down and
        up-table so the machine is not in the middle of an empty room - the floor sits just under
        the cabinet so a side-on view has a ground line to read heights against, which is most of
        what this is for.
    */
    const float w = 44.0f;      //across
    const float h = 26.0f;      //tall
    const float d = 54.0f;      //along the table
    const float t = 0.50f;      //slab thickness
    const float floor_y = -1.60f;
    const float cz = -1.0f;     //a little up-table, matching where the machine's mass is

    struct Slab{ const char* name; vec3 centre; vec3 size; };
    const Slab slabs[] = {
        { "backdrop_floor", vec3(0, floor_y - t * 0.5f, cz),          vec3(w,t,d) },
        { "backdrop_left",  vec3(-w * 0.5f, floor_y + h * 0.5f, cz),  vec3(t,h,d) },
        { "backdrop_right", vec3( w * 0.5f, floor_y + h * 0.5f, cz),  vec3(t,h,d) },
        { "backdrop_far",   vec3(0, floor_y + h * 0.5f, cz - d * 0.5f), vec3(w,h,t) },
        { "backdrop_near",  vec3(0, floor_y + h * 0.5f, cz + d * 0.5f), vec3(w,h,t) },
    };
    for (int i = 0; i < (int)(sizeof(slabs)/sizeof(slabs[0])); i++){
        Object* o = AddBox(slabs[i].name,slabs[i].centre,slabs[i].size,material_backdrop);
        if (!o){
            continue;
        }
        //Nothing about the card belongs in a depth pass: it would both receive and cast, and a
        //40-unit slab in the sun's shadow ortho is a good way to lose the resolution the table
        //needs. It is unlit anyway, so a shadow on it would not show.
        o->SetCastsShadow(false);
        o->SetPickability(false);
        o->SetVisibility(false);
        backdrop_objects.push_back(o);
    }
}

void ApplicationPinball::SetBackdropVisible(bool f_visible){
    for (size_t i = 0; i < backdrop_objects.size(); i++){
        backdrop_objects[i]->SetVisibility(f_visible);
    }
}

void ApplicationPinball::BuildFeatureLabels(){
    /*
        One flat label per feature, lying on the deck beside the thing it names.

        Worth the trouble because of what stage 0 is FOR. The deliverable is not a picture of a
        table, it is a layout somebody can check - and the somebody includes an agent driving this
        over MCP with no eyes on the monitor. An ImGui overlay would vanish from an
        `include_ui:false` screenshot; text as geometry does not, because it is just another lit
        object in the scene.
    */
    if (!LoadGlyphSetFromGLB(glyphs,"meshes/glyphs_unispace.glb",0.509167f,1.0f)){
        //Not fatal. The table renders perfectly well unlabelled; it is just harder to review.
        debug->Warn("No glyphs loaded - the table will render without feature labels\n");
        return;
    }

    for (int i = 0; i < PIN_FEATURE_COUNT; i++){
        const PinFeature& f = PIN_FEATURES[i];

        TextLayout layout;
        layout.scale = 0.12f;
        layout.align = TEXT_ALIGN_CENTER;
        layout.matid = 0;
        Mesh* mesh = BuildTextMesh(glyphs,f.name,layout);
        if (!mesh){
            continue;
        }
        Object* object = new Object();
        object->SetMesh(mesh);
        object->name = std::string("label_") + f.name;
        /*
            BuildTextMesh lays glyphs out in the XY plane facing +Z, like a billboard. Rotating -90
            about X takes +Z to +Y and the text's own up direction to -Z - so the label lies flat on
            the deck and reads from the player's end of the machine, which is the only orientation
            that is any use here.
        */
        object->SetRotation(quat(vec3(1,0,0),toradians(-90.0f)));
        //Offset down-table of the feature, clear of whatever it names, and a whisker above the
        //deck so it does not z-fight with it.
        object->SetPosition(vec3(f.x,f.y + 0.022f,f.z + 0.40f));
        object->SetMaterialSlot(0,material_label);
        //Annotation should not throw a shadow across the machine it is annotating.
        object->SetCastsShadow(false);
        main_scene->AddObject(object);
        feature_labels.push_back(object);
    }
    debug->Info("Built %zu feature labels\n",feature_labels.size());
}

void ApplicationPinball::BuildLights(){
    {
        /*
            The key light. Less "sun" than "the room the machine is standing in": high, a little to
            the left, and on the PLAYER'S side of the cabinet.

            That last part is not taste. It was up-table first, on the theory that a pinball machine
            is lit from its own backbox - and the backbox then threw a hard shadow four units deep
            straight down the top of the playfield, across the orbit and the FUEL lanes. The thing
            casting the shadow is part of the machine and does not move, so there is no camera angle
            that hides it. Lighting from the front puts that shadow behind the backbox where nobody
            can see it, and still rakes across the deck steeply enough that the ramps throw the long
            shadows that make them read as being above it.

            viewport.zoom is a HALF-EXTENT in world units for the shadow ortho. The table is 13.2
            long, so anything under 7 clips its own shadow map; 9.5 covers the machine and the
            backbox with margin.
        */
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        sun->SetPosition(vec3(-4.5f,15.0f,7.5f));
        sun->SetLookAt(vec3(PIN_CENTRE_X,0.0f,-1.0f));
        sun->color = vec3(1.0f,0.96f,0.90f);
        sun->brightness = 2.4f;
        sun->viewport.zoom = 9.5f;
        main_scene->AddObject(sun);
    }
    {
        //A cool fill from up-table, so the far faces of the bumpers, the ramp rails and the drop
        //bank are not solid shadow now that the key light comes from the front. No shadow of its
        //own - a second shadow-casting light doubles the depth passes and would put the backbox's
        //shadow straight back on the deck.
        DirectionalLight* fill = new DirectionalLight();
        fill->name = "Fill";
        fill->SetPosition(vec3(3.0f,8.0f,-11.0f));
        fill->SetLookAt(vec3(PIN_CENTRE_X,0.0f,-1.0f));
        fill->color = vec3(0.42f,0.60f,0.95f);
        fill->brightness = 0.8f;
        fill->f_casts_shadow = false;
        fill->viewport.zoom = 9.5f;
        main_scene->AddObject(fill);
    }

    /*
        General illumination, as four point lights down the length of the table.

        On a real machine the GI is a couple of dozen bulbs under the plastics; four is enough to
        keep the deck from falling off into black at the ends, which is all this needs to do. They
        are placed BETWEEN the feature clusters rather than on them, because a light sitting on top
        of a bumper flattens it.

        THESE NUMBERS LOOK ABSURDLY SMALL NEXT TO A DIRECTIONAL LIGHT'S, AND THEY ARE CORRECT.
        A point light's brightness is applied TWICE in shaders/default.frag: once as
        light_value = brightness / distance, and again as the radiance handed to
        CalcDirectionalPBRLight. The shader says so at line 471 and calls it long-standing
        behaviour that every existing light is tuned around, so it is not a bug to fix here - but
        it means the intensity actually landing on the deck goes as brightness SQUARED. These four
        started at 14 to 16, by analogy with the directional lights, and rendered a table so
        overexposed that the navy deck came out white and no feature on it was distinguishable
        from any other. 14 squared over 1.55 of distance is about 126.

        At 1.5 the same arithmetic gives about 1.5, which is a fill.
    */
    struct Lamp{ const char* name; float x; float z; vec3 color; float brightness; };
    const Lamp lamps[] = {
        { "gi_top",    0.10f,-5.60f, vec3(0.55f,0.80f,1.00f), 1.60f },
        { "gi_upper", -0.80f,-3.10f, vec3(1.00f,0.72f,0.40f), 1.80f },
        { "gi_middle", 0.20f, 0.20f, vec3(0.60f,0.90f,1.00f), 1.60f },
        { "gi_lower", -0.30f, 3.90f, vec3(1.00f,0.78f,0.50f), 1.80f },
    };
    {
        //The orbit's camera lamp. Created here with the rest of the lighting so there is one place
        //that knows what lights this scene has, but parked dark - ApplyOrbit moves it and gives it
        //a brightness, and SetShot puts it back to zero on the way out of the mode.
        orbit_light = new PointLight();
        orbit_light->name = "Orbit Camera Lamp";
        orbit_light->SetPosition(vec3(0,10.0f,0));
        orbit_light->color = vec3(1.0f,0.97f,0.92f);
        orbit_light->brightness = 0.0f;
        orbit_light->f_casts_shadow = false;
        main_scene->AddObject(orbit_light);
    }

    for (int i = 0; i < (int)(sizeof(lamps)/sizeof(lamps[0])); i++){
        PointLight* light = new PointLight();
        light->name = lamps[i].name;
        light->SetPosition(vec3(lamps[i].x,2.20f,lamps[i].z));
        light->color = lamps[i].color;
        light->brightness = lamps[i].brightness;
        //Shadows off for all four. Point-light shadows are six faces each, and the shape of this
        //table is already described by the sun's - these are here to raise the floor, not to draw.
        light->f_casts_shadow = false;
        main_scene->AddObject(light);
    }
}

void ApplicationPinball::SetupCamera(){
    /*
        The shots, solved from Table.h - see MakeShot. Each is two points and an elevation:

          table    the front wall's top edge to the top wall's top edge: the whole playfield,
                   the framing the machine is designed around. 26 degrees off vertical - nearer
                   the artwork's overhead look than the first build's 33, so that the table keeps
                   its 3:2 proportions on screen rather than foreshortening into a longer shape;
                   still enough perspective for the ramps to stand off the deck.
          upper    the bumper nest to the top wall.
          lower    the drain to the ramp mouths, steep enough to see over the front wall.
          machine  the front wall to the top of the backbox.

        The cabinet is 6.9 wide; every shot asks for 3.7 of half-width so it has a margin.
    */
    const float aspect = (float)PIN_WINDOW_WIDTH / (float)PIN_WINDOW_HEIGHT;
    const float half_w = PIN_DECK_MAX_X + PIN_WALL_THICKNESS + 0.25f;
    const vec3 front_top = vec3(PIN_CENTRE_X,PIN_CABINET_FRONT_HEIGHT,PIN_DECK_MAX_Z + PIN_WALL_THICKNESS);
    const vec3 back_top  = vec3(PIN_CENTRE_X,PIN_CABINET_HEIGHT,PIN_DECK_MIN_Z - PIN_WALL_THICKNESS);
    shots[PIN_SHOT_TABLE]   = MakeShot("table",  front_top,back_top,26.0f,40.0f,half_w,aspect);
    //The two detail shots only need the play area across, not the cabinet - asking for the full
    //width pushed them back until they were the table shot again.
    shots[PIN_SHOT_UPPER]   = MakeShot("upper",  vec3(PIN_CENTRE_X,0.0f,PIN_BUMPER_0_Z + 0.8f),back_top,
                                       30.0f,30.0f,2.7f,aspect);
    shots[PIN_SHOT_LOWER]   = MakeShot("lower",  front_top,vec3(PIN_CENTRE_X,0.4f,PIN_RAMP_L_ENTRY_Z - 0.6f),
                                       36.0f,30.0f,2.7f,aspect);
    shots[PIN_SHOT_MACHINE] = MakeShot("machine",front_top,
                                       vec3(PIN_CENTRE_X,PIN_BACKBOX_HEIGHT,PIN_BACKBOX_Z - PIN_BACKBOX_DEPTH),
                                       30.0f,42.0f,half_w,aspect);
    /*
        PIN_SHOT_ORBIT. The position and target are never used - UpdateCameraShot hands the camera
        to UpdateOrbitControls instead of easing toward them - but the entry has to exist so that
        shots[current_shot].name works for every shot, and the fov IS used, as the value the orbit
        starts at. See the mode note in ApplicationPinball.h.
    */
    shots[PIN_SHOT_ORBIT].name = "orbit";
    shots[PIN_SHOT_ORBIT].fov  = 39.0f;
    for (int i = 0; i < PIN_SHOT_COUNT; i++){
        debug->Info("shot %-8s camera (%.2f, %.2f, %.2f) looks at z %.2f, fov %.0f\n",shots[i].name,
                    shots[i].position.x,shots[i].position.y,shots[i].position.z,
                    shots[i].target.z,shots[i].fov);
    }

    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_PERSPECTIVE);
    //fov is VERTICAL and in degrees. znear is pulled in to 0.3 because the "lower" shot gets close
    //to the apron; zfar covers the backbox with room to spare.
    camera->SetupPerspective(renderer->width,renderer->height,shots[PIN_SHOT_TABLE].fov,0.3f,140.0f);

    //Snap to the opening shot rather than easing into it, so the first frame is already framed.
    current_shot   = PIN_SHOT_TABLE;
    shot_position  = shots[PIN_SHOT_TABLE].position;
    shot_target    = shots[PIN_SHOT_TABLE].target;
    shot_fov       = shots[PIN_SHOT_TABLE].fov;
    live_fov       = shot_fov;
    camera_target  = shot_target;
    camera->SetPosition(shot_position);
    camera->SetLookAt(shot_target);
    camera->CalculateLookatMatrix();
}


void ApplicationPinball::SetupInput(){
    InputController* input = main_scene->inputcontroller;
    input->AddKeyMap(VK_F1,INPUT_PINBALL_TOGGLE_UI);
    input->AddKeyMap('L', INPUT_PINBALL_TOGGLE_LABELS);
    //The three shots on the number row. Nothing else is bound yet - stage 1 is where the flippers,
    //the plunger and the nudge arrive, and binding them to nothing now would only mislead.
    input->AddKeyMap('1', INPUT_PINBALL_SHOT_TABLE);
    input->AddKeyMap('2', INPUT_PINBALL_SHOT_UPPER);
    input->AddKeyMap('3', INPUT_PINBALL_SHOT_LOWER);
    input->AddKeyMap('4', INPUT_PINBALL_SHOT_MACHINE);
    input->AddKeyMap('5', INPUT_PINBALL_SHOT_ORBIT);

    /*
        The player's controls. Z and / are 3D Pinball's own; the arrows and the shoulder buttons
        are for hands that do not know that. Space (or A) is the plunger: hold to pull back,
        release to launch. R (or Y) serves a fresh ball to the plunger, which is stage 1's "start
        again" until stage 4 has a game to start.
    */
    input->AddKeyMap('Z',                       INPUT_PINBALL_FLIP_LEFT);
    input->AddKeyMap(VK_LEFT,                   INPUT_PINBALL_FLIP_LEFT);
    input->AddKeyMap(GAMEPAD_KEY_LEFT_SHOULDER, INPUT_PINBALL_FLIP_LEFT);
    input->AddKeyMap(VK_OEM_2,                  INPUT_PINBALL_FLIP_RIGHT);   //the / key
    input->AddKeyMap(VK_RIGHT,                  INPUT_PINBALL_FLIP_RIGHT);
    input->AddKeyMap(GAMEPAD_KEY_RIGHT_SHOULDER,INPUT_PINBALL_FLIP_RIGHT);
    input->AddKeyMap(VK_SPACE,                  INPUT_PINBALL_PLUNGE);
    input->AddKeyMap(VK_RETURN,                 INPUT_PINBALL_PLUNGE);
    input->AddKeyMap(GAMEPAD_KEY_A,             INPUT_PINBALL_PLUNGE);
    input->AddKeyMap('R',                       INPUT_PINBALL_SERVE);
    input->AddKeyMap(GAMEPAD_KEY_Y,             INPUT_PINBALL_SERVE);
    //The orbit's own controls - middle mouse, shift and the wheel - need no mapping here:
    //InputController's constructor binds all four, because every app wants them.
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature.
    input->AddKeyMap('P', INPUT_PAUSE);
}

//--- Per pass -----------------------------------------------------------------------------------

void ApplicationPinball::ApplyTilt(){
    /*
        The machine is not tilted; the GRAVITY is. Rotating the whole cabinet about X by theta and
        rotating the gravity vector by -theta are the same simulation, because every part of the
        machine - deck, ramps, flippers - would rotate together. Doing it this way buys axis-aligned
        coordinates for the entire build, which is why not one wall on this table carries a
        rotation about anything but +Y.

            g = (0, -G cos t, +G sin t)
        with +Z down-table, so the +Z component is what pulls the ball toward the drain.
    */
    if (!main_scene || !main_scene->physics_world){
        return;
    }
    float t = toradians(tilt_degrees);
    main_scene->physics_world->SetGravity(vec3(0.0f,
                                               -PIN_GRAVITY * cosf(t),
                                                PIN_GRAVITY * sinf(t)));
}

void ApplicationPinball::SetShot(int shot){
    if (shot < 0 || shot >= PIN_SHOT_COUNT){
        return;
    }
    /*
        Entering the orbit takes its yaw, pitch and distance from WHERE THE CAMERA ALREADY IS, so
        that 1 then 5 starts orbiting the artwork framing rather than cutting to a default pose.
        That is the whole reason this is worth a special case: the useful gesture is "hold it right
        there and let me walk around it", and a mode that jumped somewhere else first would throw
        away the view that prompted it.
    */
    if (shot == PIN_SHOT_ORBIT && current_shot != PIN_SHOT_ORBIT){
        SeedOrbitFromCamera();
    }
    /*
        The instrument lighting goes on with the mode and off with it. Visibility and brightness are
        both render-side state written here on the physics thread and read by the render thread,
        which is the same unsynchronised arrangement every other render flag in this engine uses -
        the worst case is one frame drawn with the old value.
    */
    if (shot == PIN_SHOT_ORBIT){
        SetBackdropVisible(f_orbit_backdrop);
    }else{
        SetBackdropVisible(false);
        if (orbit_light){
            orbit_light->brightness = 0.0f;
        }
    }
    current_shot  = shot;
    shot_position = shots[shot].position;
    shot_target   = shots[shot].target;
    shot_fov      = shots[shot].fov;
}

void ApplicationPinball::SeedOrbitFromCamera(){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!camera){
        return;
    }
    //The inverse of ApplyOrbit below. camera_target is already the point the fixed shots look at,
    //so the pivot carries over for free and only the three spherical terms have to be recovered.
    vec3 offset = camera->GetPosition() - camera_target;
    float distance = offset.length();
    if (distance < 0.001f){
        //Degenerate: the camera is sitting on its own pivot and there is no direction to recover.
        //Leave the last orbit pose alone rather than inventing one from a zero vector.
        return;
    }
    orbit_distance = clamp(distance,PIN_ORBIT_MIN_DISTANCE,PIN_ORBIT_MAX_DISTANCE);
    orbit_pitch    = clamp(todegrees(asinf(clamp(offset.y / distance,-1.0f,1.0f))),
                           -ORBIT_PITCH_LIMIT,ORBIT_PITCH_LIMIT);
    //atan2(x,z) rather than the usual atan2(z,x): yaw is measured from +Z, the player's side,
    //because that is the direction a pinball machine is looked at from.
    orbit_yaw      = todegrees(atan2f(offset.x,offset.z));
    //The orbit does not ease its fov, so it has to inherit the one on screen or entering the mode
    //would snap the zoom.
    shot_fov = shots[PIN_SHOT_ORBIT].fov;
    live_fov = camera->viewport.fov;
}

void ApplicationPinball::ApplyOrbit(){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!camera){
        return;
    }
    orbit_pitch    = clamp(orbit_pitch,-ORBIT_PITCH_LIMIT,ORBIT_PITCH_LIMIT);
    orbit_distance = clamp(orbit_distance,PIN_ORBIT_MIN_DISTANCE,PIN_ORBIT_MAX_DISTANCE);

    float yaw = toradians(orbit_yaw);
    float pitch = toradians(orbit_pitch);
    float horizontal = cosf(pitch) * orbit_distance;
    vec3 offset = vec3(sinf(yaw) * horizontal,
                       sinf(pitch) * orbit_distance,
                       cosf(yaw) * horizontal);

    camera->SetupPerspective(renderer->width,renderer->height,live_fov,0.3f,140.0f);
    camera->SetPosition(camera_target + offset);
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();

    if (orbit_light){
        /*
            The lamp rides just behind and above the camera - not ON it, because a point light
            exactly at the eye flattens everything it lights (every surface is seen from its own
            highlight) and leaves no shadow terminator to read shape from. A little over the
            shoulder puts one back.

            brightness = sqrt(level * distance) because a point light's contribution here goes as
            brightness^2 / distance: solving for a constant delivered level keeps the table looking
            the same as the wheel goes in and out, instead of blowing out up close.
        */
        vec3 lamp = camera_target + offset * 1.04f + vec3(0,orbit_distance * 0.10f,0);
        orbit_light->SetPosition(lamp);
        orbit_light->brightness = f_orbit_light
                                ? sqrtf(PIN_ORBIT_LIGHT_LEVEL * orbit_distance)
                                : 0.0f;
    }
}

void ApplicationPinball::UpdateOrbitControls(){
    InputController* input = main_scene ? main_scene->inputcontroller : NULL;
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!input || !camera){
        return;
    }

    /*
        READ THE DELTAS EVERY PASS, WHETHER OR NOT THE DRAG IS ACTIVE, AND WITH GetDelta.

        Both halves of that are load-bearing and apps/tank's camera has the scar tissue to prove it
        (see the long note in ApplicationTank::UpdateView):

          - GetDelta, not GetValue. A relative axis keeps two numbers - this pass's movement, which
            Tick clears once it has been read, and a running total that is never reset. GetValue
            hands back the lifetime total, so the camera rotates by every mouse count since the
            process started, once per pass, for ever.
          - Unconditionally. Tick only clears the delta for maps that were actually READ this pass,
            so leaving these behind the button check lets movement pile up for the whole time the
            button is not held, and the first pass of a drag then applies all of it at once.
    */
    int dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);
    int wheel = input->GetDelta(INPUT_MOUSE_WHEEL);

    //Cursor-driven, so it stops when the window is not focused or the pointer is over a panel -
    //otherwise dragging an ImGui slider swings the camera at the same time.
    bool f_cursor_ours = main_window->f_has_focus && !ImGui::GetIO().WantCaptureMouse;
    if (!f_cursor_ours){
        ApplyOrbit();
        return;
    }

    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        if (input->IsKeyDown(INPUT_SHIFT)){
            /*
                Pan: slide the PIVOT, not the camera. Along the camera's own right and up vectors,
                so a drag moves the table the way the hand expects however the orbit is turned -
                and scaled by distance, so panning while zoomed in is fine work and panning while
                zoomed out covers ground.
            */
            vec3 right = -camera->GetLeft();
            vec3 up = camera->GetUp();
            float scale = PIN_ORBIT_PAN_SCALE * orbit_distance;
            camera_target += right * (-(float)dx * scale) + up * ((float)dy * scale);
        }else{
            orbit_yaw   -= (float)dx * PIN_ORBIT_DRAG_DEGREES;
            orbit_pitch += (float)dy * PIN_ORBIT_DRAG_DEGREES;
        }
    }

    //Zoom is proportional to the current distance, so one notch of wheel covers the same fraction
    //of the way in whether the camera is across the room or under the apron. A fixed step either
    //crawls from far away or overshoots the table from close up.
    if (wheel){
        orbit_distance *= (1.0f - (float)wheel * PIN_ORBIT_ZOOM_SCALE);
    }

    ApplyOrbit();
}

void ApplicationPinball::UpdateCameraShot(){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!camera){
        return;
    }
    /*
        An exponential ease toward the target framing, not a keyframed move.

        It has no duration to tune and no overshoot, and - the property that actually matters here -
        redirecting it mid-flight is free, because there is no start keyframe to invalidate. Stage 4
        will be switching shots on gameplay events that can arrive in any order and interrupt each
        other, and a spline would need a whole scheduler to survive that.

        This runs in UpdateView rather than in a tick, which is deliberate: it must keep moving
        while the simulation is paused, or pausing the table to look at a corner of it would leave
        the camera stuck wherever it was.
    */
    //The orbit is a mode, not a framing: it drives the camera itself rather than being eased to.
    if (current_shot == PIN_SHOT_ORBIT){
        UpdateOrbitControls();
        return;
    }

    vec3 position = camera->GetPosition();
    position += (shot_position - position) * PIN_CAMERA_EASE;
    camera_target += (shot_target - camera_target) * PIN_CAMERA_EASE;
    live_fov += (shot_fov - live_fov) * PIN_CAMERA_EASE;

    camera->SetupPerspective(renderer->width,renderer->height,live_fov,0.3f,140.0f);
    camera->SetPosition(position);
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();
}

void ApplicationPinball::UpdateView(void){
    InputController* input = main_scene ? main_scene->inputcontroller : NULL;
    if (input){
        if (input->WasKeyReleased(INPUT_PINBALL_TOGGLE_UI)){
            f_show_engine_ui = !f_show_engine_ui;
        }
        if (input->WasKeyReleased(INPUT_PINBALL_TOGGLE_LABELS)){
            f_show_labels = !f_show_labels;
            for (size_t i = 0; i < feature_labels.size(); i++){
                feature_labels[i]->SetVisibility(f_show_labels);
            }
        }
        if (input->WasKeyReleased(INPUT_PINBALL_SHOT_TABLE)) SetShot(PIN_SHOT_TABLE);
        if (input->WasKeyReleased(INPUT_PINBALL_SHOT_UPPER)) SetShot(PIN_SHOT_UPPER);
        if (input->WasKeyReleased(INPUT_PINBALL_SHOT_LOWER)) SetShot(PIN_SHOT_LOWER);
        if (input->WasKeyReleased(INPUT_PINBALL_SHOT_MACHINE)) SetShot(PIN_SHOT_MACHINE);
        if (input->WasKeyReleased(INPUT_PINBALL_SHOT_ORBIT)) SetShot(PIN_SHOT_ORBIT);
    }

    //Requests posted by MCP handlers, which hold no lock and must not touch the scene themselves.
    int wanted = requested_shot.exchange(-1);
    if (wanted >= 0){
        SetShot(wanted);
    }
    if (f_tilt_dirty.exchange(false)){
        tilt_degrees = requested_tilt.load();
        ApplyTilt();
    }

    //An orbit placement posted by an MCP handler. Applied AFTER the shot request above, so a call
    //that switches to orbit and aims it in one go lands in that order.
    {
        PinOrbitRequest request;
        {
            std::lock_guard<std::mutex> lock(orbit_request_mutex);
            request = orbit_request;
            orbit_request.f_pending = false;
        }
        if (request.f_pending){
            //Every field is optional, so that "turn 30 degrees further round" is one argument
            //rather than a caller having to restate the whole pose.
            if (request.f_set_target)   camera_target  = request.target;
            if (request.f_set_yaw)      orbit_yaw      = request.yaw;
            if (request.f_set_pitch)    orbit_pitch    = request.pitch;
            if (request.f_set_distance) orbit_distance = request.distance;
            ApplyOrbit();
        }
    }

    UpdateCameraShot();
}

void ApplicationPinball::PreRender(void){
    //Nothing to rebuild yet - every label is static text baked once in Init. The hook is here
    //because stage 5's score display belongs in it: BuildTextMesh ends in glNamedBufferData, and
    //the physics thread may not touch GL.
}

//--- Debug UI -----------------------------------------------------------------------------------

void ApplicationPinball::DrawImGuiUI(void){
    //Render thread, with physics_mutex held - so reading the app's own state directly here is
    //safe, and nothing in here may WAIT on the physics thread. See SubmitUICommand in
    //core/Application.h for the deadlock this avoids.
    if (f_show_engine_ui){
        RenderApplicationUI();
    }

    ImGui::SetNextWindowPos(ImVec2(f_show_engine_ui ? 330.0f : 16.0f,16.0f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320,0),ImGuiCond_Always);
    ImGui::Begin("Orbit Outpost",NULL,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(1.0f,0.62f,0.18f,1.0f),"STAGE 1 - ball, flippers, plunger");
    ImGui::Text("%i features, %zu labels",PIN_FEATURE_COUNT,feature_labels.size());
    ImGui::Separator();

    //The tilt. No ball to feel it yet, but the design document asks for the control from day one
    //and the number is live - it really is the world's gravity.
    if (ImGui::SliderFloat("tilt (deg)",&tilt_degrees,0.0f,12.0f,"%.2f")){
        ApplyTilt();
    }
    {
        float t = toradians(tilt_degrees);
        ImGui::Text("gravity  (0, %.2f, %.2f)",-PIN_GRAVITY * cosf(t),PIN_GRAVITY * sinf(t));
    }
    ImGui::Text("tps      %.0f   tick %llu",physics_tps,(unsigned long long)main_scene->GetPhysicsTick());
    ImGui::Separator();

    /*
        Stage 1 readouts and tuning. Read directly: this runs with physics_mutex held, so the
        physics thread is between passes and nothing here is half-stepped. The tuning sliders
        write plain floats the mechanisms read on their next tick - a direct write rather than a
        command, which is allowed for the debug UI and right for tuning, which is not gameplay
        and has no business in a recording. The serve button IS gameplay and goes on the queue.
    */
    {
        Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
        if (p){
            vec3 pos = p->GetBodyWorldPosition();
            vec3 vel = p->GetVelocity();
            float speed = vel.length();
            ImGui::Text("ball     (%.2f, %.2f, %.2f)  %.1f u/s  %.2f r/tick",pos.x,pos.y,pos.z,speed,
                        speed * main_scene->GetPhysicsTimestep() / PIN_BALL_RADIUS);
        }
        ImGui::Text("flippers L %+5.1f  R %+5.1f  U %+5.1f deg",
                    flipper_left ? flipper_left->GetAngleDegrees() : 0.0f,
                    flipper_right ? flipper_right->GetAngleDegrees() : 0.0f,
                    flipper_upper ? flipper_upper->GetAngleDegrees() : 0.0f);
        ImGui::Text("plunger  %.2f / %.2f%s",plunger ? plunger->GetTravel() : 0.0f,
                    plunger ? plunger->travel : 0.0f,(plunger && plunger->IsPulling()) ? "  pulling" : "");
        ImGui::Text("drains %u  escapes %u  guard %u  clamps %u  max %.1f u/s",
                    (uint32_t)drains,(uint32_t)escapes,(uint32_t)guard_hits,(uint32_t)speed_clamps,
                    max_speed_seen);
        if (ImGui::Button("serve ball (R)")){
            //SubmitUICommand, never SubmitCommandAndWait: this runs with physics_mutex held.
            SimCommand cmd;
            cmd.type = PIN_CMD_SERVE;
            SubmitUICommand(cmd);
        }
        ImGui::SameLine();
        if (ImGui::Button("zero counters")){
            drains = 0; escapes = 0; guard_hits = 0; speed_clamps = 0; max_speed_seen = 0.0f;
        }
        if (ImGui::TreeNode("flipper tuning")){
            Flipper* list[3] = { flipper_left,flipper_right,flipper_upper };
            float speed  = flipper_left ? flipper_left->motor_speed : PIN_FLIPPER_MOTOR_SPEED;
            float torque = flipper_left ? flipper_left->motor_torque : PIN_FLIPPER_MOTOR_TORQUE;
            float rspeed = flipper_left ? flipper_left->return_speed : PIN_FLIPPER_RETURN_SPEED;
            float rtorq  = flipper_left ? flipper_left->return_torque : PIN_FLIPPER_RETURN_TORQUE;
            bool f_changed = false;
            f_changed |= ImGui::SliderFloat("motor speed (rad/s)",&speed,5.0f,80.0f);
            f_changed |= ImGui::SliderFloat("motor torque",&torque,50.0f,5000.0f);
            f_changed |= ImGui::SliderFloat("return speed (rad/s)",&rspeed,5.0f,60.0f);
            f_changed |= ImGui::SliderFloat("return torque",&rtorq,20.0f,1500.0f);
            if (f_changed){
                for (int i = 0; i < 3; i++){
                    if (!list[i]) continue;
                    list[i]->motor_speed = speed;
                    list[i]->motor_torque = torque;
                    list[i]->return_speed = rspeed;
                    list[i]->return_torque = rtorq;
                }
            }
            ImGui::TreePop();
        }
        if (plunger && ImGui::TreeNode("plunger tuning")){
            ImGui::SliderFloat("spring",&plunger->spring,100.0f,5000.0f);
            ImGui::SliderFloat("damping",&plunger->damping,0.0f,40.0f);
            ImGui::SliderFloat("pull speed (u/s)",&plunger->pull_speed,0.5f,10.0f);
            ImGui::TreePop();
        }
        if (p && ImGui::TreeNode("ball tuning")){
            float bounce = p->GetBounciness();
            float friction = p->GetFrictionCoefficient();
            float damping = p->GetLinearDamping();
            if (ImGui::SliderFloat("bounciness",&bounce,0.0f,1.0f))    p->SetBounciness(bounce);
            if (ImGui::SliderFloat("friction",&friction,0.0f,1.0f))    p->SetFrictionCoefficient(friction);
            if (ImGui::SliderFloat("linear damping",&damping,0.0f,1.0f)) p->SetLinearDamping(damping);
            ImGui::TreePop();
        }
    }
    ImGui::Separator();

    ImGui::Text("camera   %s",shots[current_shot].name);
    for (int i = 0; i < PIN_SHOT_COUNT; i++){
        //Four fixed shots on one row, the orbit on its own underneath - it is a different kind of
        //thing and a row of five buttons hides that.
        if (i && i != PIN_SHOT_ORBIT){
            ImGui::SameLine();
        }
        if (ImGui::Button(shots[i].name)){
            SetShot(i);
        }
    }
    if (current_shot == PIN_SHOT_ORBIT){
        //Typed in rather than only dragged, because a repeatable viewpoint is most of what a
        //debug camera is for - "the same angle as last time" has to be something you can enter.
        bool f_changed = false;
        f_changed |= ImGui::SliderFloat("yaw",&orbit_yaw,-180.0f,180.0f,"%.1f deg");
        f_changed |= ImGui::SliderFloat("pitch",&orbit_pitch,-ORBIT_PITCH_LIMIT,ORBIT_PITCH_LIMIT,"%.1f deg");
        f_changed |= ImGui::SliderFloat("distance",&orbit_distance,PIN_ORBIT_MIN_DISTANCE,PIN_ORBIT_MAX_DISTANCE,"%.2f");
        f_changed |= ImGui::SliderFloat("fov",&live_fov,12.0f,80.0f,"%.1f deg");
        ImGui::Text("pivot    %.2f, %.2f, %.2f",camera_target.x,camera_target.y,camera_target.z);
        if (ImGui::Button("pivot to table centre")){
            camera_target = vec3(PIN_CENTRE_X,0.0f,0.0f);
            f_changed = true;
        }
        //Both off gives the table exactly as the fixed shots light it, from wherever the orbit is
        //standing - which is the honest view, and unreadable from the side. That is the trade.
        if (ImGui::Checkbox("backdrop",&f_orbit_backdrop)){
            SetBackdropVisible(f_orbit_backdrop);
        }
        ImGui::SameLine();
        f_changed |= ImGui::Checkbox("camera lamp",&f_orbit_light);
        //Nothing is submitted anywhere: ApplyOrbit only writes the camera, which is not simulation
        //state, and this runs on the render thread with physics_mutex already held.
        if (f_changed){
            ApplyOrbit();
        }
        ImGui::TextDisabled("middle-drag orbit, +shift pan, wheel zoom");
    }
    ImGui::Separator();

    if (ImGui::Checkbox("feature labels",&f_show_labels)){
        for (size_t i = 0; i < feature_labels.size(); i++){
            feature_labels[i]->SetVisibility(f_show_labels);
        }
    }
    {
        //PhysicsWorld::SetDebugRendering already exists, and pinball_design.md 2.1 asks for the
        //toggle from day one: art and collider are different objects here, so a mismatch between
        //them is the bug this approach invites and it is invisible without this. There are no
        //colliders to show until stage 1 - the control is wired now so that there is never a
        //moment where they exist and cannot be seen.
        static bool f_show_colliders = false;
        if (ImGui::Checkbox("show colliders (stage 1)",&f_show_colliders)){
            main_scene->physics_world->SetDebugRendering(f_show_colliders);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Z / flippers   space plunger   R serve   P pause");
    ImGui::TextDisabled("F1 panels   L labels   1-5 camera");
    ImGui::End();
}

//--- Stage 1: colliders -------------------------------------------------------------------------

//The one static body every collider here goes on: the object's own, added if it has none, in the
//TABLE category, meeting only the ball. Statics never meet each other in rp3d anyway, so the mask
//costs nothing and says what is meant.
static Physics* StaticBodyFor(Object* object, PhysicsWorld* world){
    if (!object || !world){
        return NULL;
    }
    Physics* p = object->GetPhysics();
    if (!p){
        p = object->AddPhysics(world);      //STATIC by default, seeded from the object's transform
    }
    if (p){
        object->SetCollisionCategoryBits(PIN_CAT_TABLE);
        object->SetCollideWithMaskBits(PIN_CAT_BALL);
    }
    return p;
}

void ApplicationPinball::AddSweptColliders(Object* object, const PinPath& path, float half_width,
                                           float y_below, float y_above, float bounciness,
                                           float friction){
    Physics* p = StaticBodyFor(object,main_scene ? main_scene->physics_world : NULL);
    if (!p || path.size() < 2){
        return;
    }
    /*
        One box per segment, in the BODY's frame. Every object this is called on sits at the
        origin unrotated (a swept mesh carries its own world coordinates), so local is world; the
        origin is subtracted anyway so that a wall object placed elsewhere would still be right.

        Each box is extended by half a width at both ends, so consecutive boxes overlap round a
        bend and a ball cannot find a seam - the collider version of MakeSweptBox's mitre. The
        overlap also closes the small wedge a mitre leaves on the OUTSIDE of a bend, at the cost
        of a wedge the same size protruding on the inside, which is a ball's-width-irrelevant
        0.07 here and would be worth mitring properly only if a lane ever went that tight.
    */
    const vec3 origin = object->GetPosition();
    const vec3 up = vec3(0,1,0);
    for (size_t i = 0; i + 1 < path.size(); i++){
        vec3 a = path[i];
        vec3 b = path[i + 1];
        vec3 d = b - a;
        const float length = d.length();
        if (length < 0.0005f){
            continue;
        }
        const float flat = sqrtf(d.x * d.x + d.z * d.z);
        if (flat < 0.0005f){
            debug->Err("AddSweptColliders(%s): segment %zu is vertical\n",object->name.c_str(),i);
            continue;
        }
        //Local +X along the segment: yaw about +Y takes +X to the horizontal heading (+X toward
        //-Z for a positive yaw, hence the minus), then pitch about the local Z tilts it up the
        //climb. Multiplication applies the right-hand factor first.
        const float yaw   = atan2f(-d.z,d.x);
        const float pitch = atan2f(d.y,flat);
        const quat  orientation = quat(up,yaw) * quat(vec3(0,0,1),pitch);
        vec3 centre = (a + b) * 0.5f + up * ((y_above - y_below) * 0.5f) - origin;
        vec3 half   = vec3(length * 0.5f + half_width,(y_above + y_below) * 0.5f,half_width);
        p->AddBoxCollider(half,centre,orientation,1.0f);
        p->SetBounciness(bounciness);
        p->SetFrictionCoefficient(friction);
    }
}

void ApplicationPinball::AddBoxCollider(Object* object, const vec3& size, float bounciness,
                                        float friction){
    Physics* p = StaticBodyFor(object,main_scene ? main_scene->physics_world : NULL);
    if (!p){
        return;
    }
    //The object's own centre is the box's; AddBox drew it that way. Its render scale is not
    //consulted - the half extents are the size the caller drew, which is the point.
    p->AddBoxCollider(size * 0.5f,vec3(),quat().identity(),1.0f);
    p->SetBounciness(bounciness);
    p->SetFrictionCoefficient(friction);
}

void ApplicationPinball::AddPostCollider(Object* object, float radius, float bounciness,
                                         float friction){
    Physics* p = StaticBodyFor(object,main_scene ? main_scene->physics_world : NULL);
    if (!p){
        return;
    }
    /*
        core has no cylinder collider (pinball_design.md 0), so a post is a capsule whose straight
        part spans well past the ball's band both ways: centred at ball height, 0.60 tall, so it
        runs from 0.165 below the deck to 0.435 above it and the round caps are beyond both. The
        ball's centre never leaves 0.135 on a flat deck, so it only ever meets the cylinder - a
        cap would deflect it upward off a shape that is visibly vertical.
    */
    const float straight = 0.60f;
    const vec3 local = vec3(0.0f,PIN_BALL_RADIUS - object->GetPosition().y,0.0f);
    p->AddCapsuleCollider(radius,straight,local,quat().identity(),1.0f);
    p->SetBounciness(bounciness);
    p->SetFrictionCoefficient(friction);
}

//--- Stage 1: the ball --------------------------------------------------------------------------

void ApplicationPinball::BuildBall(){
    ball_object = NewPartObject("ball","ball",ball_mesh,
                                vec3(PIN_CHUTE_X,PIN_BALL_RADIUS,PIN_BALL_REST_Z),material_ball);
    if (!ball_object || !main_scene->physics_world){
        return;
    }
    Physics* p = ball_object->AddPhysics(main_scene->physics_world);
    if (!p){
        return;
    }
    p->AddSphereCollider(PIN_BALL_RADIUS,vec3(),quat().identity(),1.0f);
    p->SetBounciness(PIN_BALL_BOUNCINESS);
    p->SetFrictionCoefficient(PIN_BALL_FRICTION);
    p->SetStatic(false);
    //The only body on the table that feels gravity - the tilted vector ApplyTilt writes.
    p->SetGravityEnabled(true);
    p->SetMass(PIN_BALL_MASS);
    p->SetLinearDamping(PIN_BALL_LINEAR_DAMPING);
    p->SetAngularDamping(0.05f);
    if (p->body && p->body->rigidbody){
        //Sleeping disabled (pinball_design.md 2.4): a ball resting against the plunger would
        //otherwise go to sleep, and a sleeping body does not notice a tip arriving under it.
        p->body->rigidbody->setIsAllowedToSleep(false);
    }
    ball_object->SetCollisionCategoryBits(PIN_CAT_BALL);
    ball_object->SetCollideWithMaskBits(PIN_CAT_TABLE | PIN_CAT_FLIPPER | PIN_CAT_PLUNGER);
}

void ApplicationPinball::ServeBall(){
    Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    p->SetVelocity(vec3());
    p->SetAngularVelocity(vec3());
    p->SetBodyWorldPosition(vec3(PIN_CHUTE_X,PIN_BALL_RADIUS,PIN_BALL_REST_Z));
    p->SetBodyWorldOrientation(quat().identity());
    p->WakeUp();
}

//--- Stage 1: the tick --------------------------------------------------------------------------

void ApplicationPinball::TunnelGuard(){
    Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!p || !world || !p->body || !p->body->rigidbody){
        return;
    }
    /*
        rp3d 0.10 has no continuous collision detection, so a ball doing 0.33 units per tick
        against a 0.14 rail is on the far side of it before the solver knows it was near. This
        is the check the solver does not do: cast from where the ball is to where this tick will
        put it, one radius further, and if that crosses anything, put the ball against the
        surface and reflect its velocity by hand.

        Only above PIN_GUARD_MIN_TRAVEL per tick. Below it the solver's own contact - with its
        friction and its spin - is the better answer, and it catches everything at that speed.
        Above it the choice is between a hand-made reflection and a ball outside the cabinet.
    */
    const float dt = main_scene->GetPhysicsTimestep();
    const vec3 position = p->GetBodyWorldPosition();
    vec3 velocity = p->GetVelocity();
    const float speed = velocity.length();
    const float travel = speed * dt;
    if (travel < PIN_GUARD_MIN_TRAVEL){
        return;
    }
    const vec3 direction = velocity * (1.0f / speed);
    const vec3 to = position + direction * (travel + PIN_BALL_RADIUS);
    PhysicsWorld::RaycastHit hit = world->Raycast(position,to,p->body->rigidbody);
    if (!hit.hit){
        return;
    }
    const float distance = (hit.point - position).length();
    //The surface is further than this tick's travel plus half a radius: the ball ends the tick
    //with its centre clear of the surface's midplane, and the solver gets the contact. Leave it.
    if (distance > travel + PIN_BALL_RADIUS * 0.5f){
        return;
    }
    //Moving away from it already (a graze from behind, a surface the ray clipped edge-on)? Not
    //ours either.
    const float into = velocity.dot(hit.normal);
    if (into >= 0.0f){
        return;
    }
    //A radius short of the surface along the ray, and the normal component reflected with the
    //ball's own bounce. No friction and no spin: this is the emergency exit, not the physics.
    vec3 placed = hit.point - direction * (PIN_BALL_RADIUS + 0.005f);
    velocity = velocity - hit.normal * ((1.0f + PIN_BALL_BOUNCINESS) * into);
    p->SetBodyWorldPosition(placed);
    p->SetVelocity(velocity);
    guard_hits++;
    last_guard_point  = hit.point;
    last_guard_normal = hit.normal;
    last_guard_speed  = speed;
}

void ApplicationPinball::RunSimulationTick(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //--- The player ------------------------------------------------------------------------------
    //Held keys, read every tick - a flipper button is a level, not an edge. IsInputLive is the
    //engine's one predicate for "this input is ours to act on": the window in front, or a
    //scripted hold running (which is exactly when the window is not in front).
    const bool f_live = input->IsInputLive();
    const bool f_flip_left  = f_live && input->IsKeyDown(INPUT_PINBALL_FLIP_LEFT);
    const bool f_flip_right = f_live && input->IsKeyDown(INPUT_PINBALL_FLIP_RIGHT);
    const bool f_pull       = f_live && input->IsKeyDown(INPUT_PINBALL_PLUNGE);
    if (flipper_left)  flipper_left->SetFlip(f_flip_left);
    if (flipper_upper) flipper_upper->SetFlip(f_flip_left);
    if (flipper_right) flipper_right->SetFlip(f_flip_right);
    if (plunger)       plunger->SetPull(f_pull);
    if (f_live && input->WasKeyReleased(INPUT_PINBALL_SERVE)){
        ServeBall();
    }

    //--- The ball --------------------------------------------------------------------------------
    Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
    if (p){
        //Mitigation 3 first, so the guard sweeps the speed the ball will really move at.
        vec3 velocity = p->GetVelocity();
        const float speed = velocity.length();
        if (speed > PIN_MAX_BALL_SPEED){
            p->SetVelocity(velocity * (PIN_MAX_BALL_SPEED / speed));
            speed_clamps++;
        }
        if (speed > max_speed_seen){
            max_speed_seen = speed;
        }
        TunnelGuard();

        /*
            The drain and the escape. Both end in a re-serve; only one of them is a fault.

            The drain is a region of the deck rather than a trigger volume: the same test rp3d's
            trigger would make, done here on the tick where it belongs, and it costs a compare. It
            becomes a switch with the rest of them in stage 2. The ESCAPE is a ball found outside
            the cabinet or below the deck, which no honest simulation can produce - so it is
            counted, logged with where and how fast, and the count is the tunnelling detector this
            stage exists to drive to zero.
        */
        const vec3 position = p->GetBodyWorldPosition();
        const bool f_in_mouth = fabsf(position.x - PIN_DRAIN_X) < PIN_DRAIN_HALF_WIDTH;
        if (position.z > PIN_DRAIN_Z - 0.10f && f_in_mouth && position.y < 0.5f){
            drains++;
            ServeBall();
        }else if (position.x < PIN_PLAY_MIN_X - PIN_ESCAPE_MARGIN
               || position.x > PIN_DECK_MAX_X + PIN_ESCAPE_MARGIN
               || position.z < PIN_DECK_MIN_Z - PIN_ESCAPE_MARGIN
               || position.z > PIN_DECK_MAX_Z + PIN_ESCAPE_MARGIN + 0.5f
               || position.y < -0.5f || position.y > PIN_GLASS_HEIGHT + 1.0f){
            escapes++;
            debug->Warn("ball ESCAPED at (%.2f, %.2f, %.2f) doing %.1f u/s on tick %llu - re-served\n",
                        position.x,position.y,position.z,speed,
                        (unsigned long long)main_scene->GetPhysicsTick());
            ServeBall();
        }
    }

    //--- Scenery that follows a body -------------------------------------------------------------
    //The rod and the knob ride behind the plunger tip. Render-only objects, so setting their
    //position directly from the tick is fine.
    if (plunger){
        const vec3 tip = plunger->GetPosition();
        if (plunger_rod)  plunger_rod->SetPosition(vec3(tip.x,tip.y,tip.z + 0.53f));
        if (plunger_knob) plunger_knob->SetPosition(vec3(tip.x,tip.y,tip.z + 1.25f));
    }
}

//--- Stage 1: commands and telemetry ------------------------------------------------------------

void ApplicationPinball::RegisterCommandHandlers(){
    //Both run on the physics thread at the top of a tick with physics_mutex held - the only
    //place a teleport is safe (core/SimCommand.h), and the place a recording replays it to.
    main_scene->RegisterCommandHandler(PIN_CMD_PLACE_BALL,
        [this](const SimCommand& cmd) -> objectid_t {
            Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
            if (!p){
                return OBJECTID_INVALID;
            }
            p->SetAngularVelocity(vec3());
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                p->SetBodyWorldPosition(cmd.position);
            }
            p->SetVelocity((cmd.flags & SIM_CMD_FLAG_VELOCITY) ? cmd.velocity : vec3());
            p->WakeUp();
            return ball_object->GetID();
        });
    main_scene->RegisterCommandHandler(PIN_CMD_SERVE,
        [this](const SimCommand& cmd) -> objectid_t {
            ServeBall();
            return ball_object ? ball_object->GetID() : OBJECTID_INVALID;
        });
}

//Physics-thread state, so call this at a tick boundary (Scene::AtTickBoundary) or from the tick.
json ApplicationPinball::BuildTelemetryJson(){
    json result;
    result["tick"]   = (uint64_t)main_scene->GetPhysicsTick();
    result["paused"] = main_scene->IsPhysicsPaused();
    result["tilt_degrees"] = tilt_degrees;

    Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
    if (p){
        vec3 position = p->GetBodyWorldPosition();
        vec3 velocity = p->GetVelocity();
        float speed = velocity.length();
        result["ball"] = json{
            {"position",json::array({position.x,position.y,position.z})},
            {"velocity",json::array({velocity.x,velocity.y,velocity.z})},
            {"speed",speed},
            //In the two units that matter for tunnelling: how far it moves per tick, and that
            //as a fraction of its own radius.
            {"travel_per_tick",speed * main_scene->GetPhysicsTimestep()},
            {"travel_per_tick_radii",speed * main_scene->GetPhysicsTimestep() / PIN_BALL_RADIUS},
            {"in_chute",position.x > PIN_CHUTE_DIVIDER_X}
        };
    }
    json flippers = json::object();
    Flipper* list[3] = { flipper_left,flipper_right,flipper_upper };
    const char* names[3] = { "left","right","upper" };
    for (int i = 0; i < 3; i++){
        if (list[i]){
            flippers[names[i]] = json{
                {"angle_degrees",list[i]->GetAngleDegrees()},
                {"sweep_degrees",list[i]->GetSweepDegrees()},
                {"flipping",list[i]->IsFlipping()}
            };
        }
    }
    result["flippers"] = flippers;
    if (plunger){
        result["plunger"] = json{
            {"travel",plunger->GetTravel()},
            {"travel_max",plunger->travel},
            {"pulling",plunger->IsPulling()}
        };
    }
    result["counters"] = json{
        {"drains",(uint32_t)drains},
        {"escapes",(uint32_t)escapes},
        {"guard_hits",(uint32_t)guard_hits},
        {"speed_clamps",(uint32_t)speed_clamps},
        {"max_speed_seen",max_speed_seen},
        {"last_guard",json{
            {"point",json::array({last_guard_point.x,last_guard_point.y,last_guard_point.z})},
            {"normal",json::array({last_guard_normal.x,last_guard_normal.y,last_guard_normal.z})},
            {"speed",last_guard_speed}
        }}
    };
    return result;
}

//--- MCP ----------------------------------------------------------------------------------------

json ApplicationPinball::BuildLayoutJson(){
    /*
        The layout as data, so it can be reviewed without a screenshot.

        This is half of what makes stage 0 verifiable: a screenshot says whether the table LOOKS
        right, and this says whether the numbers ARE right - including the derived ones that are
        not written down anywhere, like the gap between the flipper tips, which is the measurement
        that turned out to matter most (change 1 in Table.h).
    */
    json features = json::array();
    for (int i = 0; i < PIN_FEATURE_COUNT; i++){
        const PinFeature& f = PIN_FEATURES[i];
        features.push_back(json{
            {"name",f.name},
            {"kind",PinFeatureKindName(f.kind)},
            {"x",f.x},{"y",f.y},{"z",f.z}
        });
    }

    /*
        The numbers that are consequences rather than inputs. Each one is a thing that was wrong in
        a draft of the layout and would be silently wrong again after any edit.

        CLEAR widths, not centre-to-centre: half of each bounding wall's thickness is taken off.
        The first build reported centre-to-centre and called a 0.24 inlane "1.43 balls"; the ball
        does not get to ignore the rails. tools/pinball_plan.py is the full check - it fattens
        every shape by the ball and floods the deck - and these are the handful worth having in
        the JSON without running it.
    */
    const float rest = toradians(PIN_FLIPPER_REST_DEG);
    const float tip_gap = (PIN_FLIPPER_R_X - PIN_FLIPPER_L_X)
                        - 2.0f * PIN_FLIPPER_LENGTH * cosf(rest);
    const float inlane  = (PIN_SLING_L_AX - PIN_DIVIDER_L_X)
                        - PIN_RAIL_VISUAL_THICK * 0.5f - PIN_SLING_THICKNESS * 0.5f;
    const float outlane = (PIN_DIVIDER_L_X - PIN_OUTLANE_L_X) - PIN_RAIL_VISUAL_THICK;
    const float orbit   = ((PIN_ORBIT_CZ - PIN_ORBIT_RADIUS) - PIN_ORBIT_OUTER_APEX_Z)
                        - PIN_RAIL_VISUAL_THICK;
    const float ret     = (PIN_RETURN_X - PIN_PLAY_MIN_X) - PIN_RAIL_VISUAL_THICK * 0.5f;
    const float chute   = (PIN_DECK_MAX_X - 0.05f - PIN_CHUTE_DIVIDER_X)
                        - PIN_CHUTE_DIVIDER_THICK * 0.5f;

    /*
        The plan: every shape the builders recorded (see PinPlanEntry in the header), in the frame
        the ball lives in. This is what tools/pinball_plan.py draws and floods. It is emitted in
        full every call because it is a few kilobytes and because a tool that has to ask for it
        separately is a tool that gets run against a stale copy.
    */
    json plan_json = json::array();
    for (size_t i = 0; i < plan.size(); i++){
        const PinPlanEntry& e = plan[i];
        json points = json::array();
        for (size_t k = 0; k < e.points.size(); k++){
            points.push_back(json::array({e.points[k].x,e.points[k].y,e.points[k].z}));
        }
        plan_json.push_back(json{
            {"kind",e.kind},{"name",e.name},{"points",points},
            {"a",e.a},{"b",e.b},{"c",e.c},{"d",e.d}
        });
    }
    vec3 ball_start = ball_object ? ball_object->GetPosition() : vec3(PIN_CHUTE_X,0.0f,PIN_PLUNGER_Z);

    return json{
        {"table","Orbit Outpost"},
        {"stage",0},
        {"units_per_metre",PIN_UNITS_PER_METRE},
        {"ball_diameter",PIN_BALL_DIAMETER},
        {"ball_radius",PIN_BALL_RADIUS},
        {"wall_thickness",PIN_WALL_THICKNESS},
        {"rail_height",PIN_RAIL_HEIGHT},
        {"play",{
            {"min_x",PIN_PLAY_MIN_X},{"max_x",PIN_PLAY_MAX_X},
            {"min_z",PIN_DECK_MIN_Z},{"max_z",PIN_DECK_MAX_Z},
            {"centre_x",PIN_CENTRE_X}
        }},
        {"ball_start",json::array({ball_start.x,ball_start.y,ball_start.z})},
        {"plan",plan_json},
        {"tilt_degrees",tilt_degrees},
        {"physics_tps",physics_tps},
        {"camera_shot",shots[current_shot].name},
        //Reported always, not only while orbiting, so a caller can read the pose back after aiming
        //it and get the same numbers it would need to reproduce the view later.
        {"orbit",{
            {"yaw",orbit_yaw},
            {"pitch",orbit_pitch},
            {"distance",orbit_distance},
            {"pivot",json::array({camera_target.x,camera_target.y,camera_target.z})},
            {"fov",live_fov}
        }},
        {"deck",{
            {"min_x",PIN_DECK_MIN_X},{"max_x",PIN_DECK_MAX_X},
            {"min_z",PIN_DECK_MIN_Z},{"max_z",PIN_DECK_MAX_Z}
        }},
        //Everything below is in BALL DIAMETERS as well as units, because "0.34" means nothing and
        //"1.27 balls" means everything when the question is whether something fits.
        {"clearances",{
            {"flipper_tip_gap",tip_gap},
            {"flipper_tip_gap_balls",tip_gap / PIN_BALL_DIAMETER},
            {"inlane_clear",inlane},
            {"inlane_clear_balls",inlane / PIN_BALL_DIAMETER},
            {"outlane_clear",outlane},
            {"outlane_clear_balls",outlane / PIN_BALL_DIAMETER},
            {"orbit_lane_at_apex_clear",orbit},
            {"orbit_lane_at_apex_balls",orbit / PIN_BALL_DIAMETER},
            {"orbit_return_clear",ret},
            {"orbit_return_balls",ret / PIN_BALL_DIAMETER},
            {"chute_clear",chute},
            {"chute_balls",chute / PIN_BALL_DIAMETER},
            {"ramp_mouth_clear",PIN_RAMP_WIDTH},
            {"ramp_mouth_balls",PIN_RAMP_WIDTH / PIN_BALL_DIAMETER},
            {"drain_mouth",PIN_APRON_GAP_MAX_X - PIN_APRON_GAP_MIN_X}
        }},
        {"features",features}
    };
}

void ApplicationPinball::RegisterMCPTools(){
    //Registered from Init(). The server only starts accepting requests after Init() returns, so
    //registration can never race a client's tools/list.

    MCPServer::Get()->RegisterTool("pinball_layout",
        "The whole table as numbers: every feature with its name, kind and (x,y,z), the deck's "
        "extents, and the DERIVED clearances that decide whether the layout actually works - the "
        "gap between the flipper tips, the inlane and outlane widths, the orbit lane at its apex - "
        "each given in world units and in ball diameters. Stage 0 has no simulation, so this is "
        "static data and reading it disturbs nothing. Set include_screenshot for a PNG of the "
        "current framing alongside it.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the current frame, default false"}}},
                {"include_ui", {{"type","boolean"},{"description","include the ImGui panels in that PNG, default false - the table is the subject here"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BuildLayoutJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",false));
        });

    MCPServer::Get()->RegisterTool("pinball_camera",
        "Move the camera to one of the table's named shots and return the layout once it has "
        "arrived. 'table' is the fixed framing the machine is designed around - the playfield filling "
        "the frame the way the reference artwork does. 'upper' looks at the orbit, the wormholes "
        "and the FUEL lanes; 'lower' drops to flipper height; 'machine' backs off far enough to "
        "take in the cabinet and the backbox too. The move is an ease rather "
        "than a cut, and it keeps running while the simulation is paused, so this is the way to "
        "inspect a corner of the table without hand-flying anything.\n\n"
        "'orbit' is the free debug camera and the only one that is a MODE rather than a framing: "
        "it can be placed anywhere with the yaw, pitch, distance and pivot arguments, which is how "
        "to look at the table from the SIDE - the one thing the fixed near-overhead shots cannot "
        "show, and the only way to judge whether a ramp clears what it passes over. Yaw is degrees "
        "about the vertical measured from the player's end, so 0 looks up-table, +90 is from the "
        "right-hand rail, 180 is from behind the backbox; pitch is elevation, +90 straight down, 0 "
        "level with the deck. Giving any of them switches to the orbit; every one is optional, so a "
        "single argument nudges one axis and leaves the rest. A person drives the same camera with "
        "middle-drag, shift+middle-drag and the wheel. The pose comes back in the result under "
        "'orbit'.",
        json{
            {"type","object"},
            {"properties", {
                {"shot", {{"type","string"},{"enum", json::array({"table","upper","lower","machine","orbit"})}}},
                {"yaw", {{"type","number"},{"description","orbit only: degrees about the vertical, 0 from the player's end, +90 from the right rail"}}},
                {"pitch", {{"type","number"},{"description","orbit only: elevation in degrees, +88 straight down, 0 level with the deck"}}},
                {"distance", {{"type","number"},{"description","orbit only: how far the camera sits from the pivot, 1.2 to 60"}}},
                {"pivot", {{"type","array"},{"description","orbit only: the point to orbit around, [x,y,z]. Defaults to the table's middle"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG once the move has settled, default true"}}},
                {"include_ui", {{"type","boolean"},{"description","include the ImGui panels in that PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            //Any orbit argument implies the orbit, so that aiming it is one call rather than two.
            bool f_orbit_args = args.contains("yaw") || args.contains("pitch")
                             || args.contains("distance") || args.contains("pivot");
            std::string wanted = args.value("shot",f_orbit_args ? "orbit" : "table");
            int index = -1;
            for (int i = 0; i < PIN_SHOT_COUNT; i++){
                if (wanted == shots[i].name){
                    index = i;
                    break;
                }
            }
            if (index < 0){
                return json{ {"error","unknown shot"} };
            }
            if (f_orbit_args && index != PIN_SHOT_ORBIT){
                return json{ {"error","yaw/pitch/distance/pivot only apply to the orbit shot"} };
            }
            if (index == PIN_SHOT_ORBIT){
                PinOrbitRequest request;
                request.f_pending = true;
                if (args.contains("yaw")){
                    request.f_set_yaw = true;
                    request.yaw = args.value("yaw",0.0f);
                }
                if (args.contains("pitch")){
                    request.f_set_pitch = true;
                    request.pitch = args.value("pitch",55.0f);
                }
                if (args.contains("distance")){
                    request.f_set_distance = true;
                    request.distance = args.value("distance",18.0f);
                }
                if (args.contains("pivot")){
                    const json& p = args["pivot"];
                    if (!p.is_array() || p.size() != 3 || !p[0].is_number()
                        || !p[1].is_number() || !p[2].is_number()){
                        return json{ {"error","pivot must be [x,y,z]"} };
                    }
                    request.f_set_target = true;
                    request.target = vec3(p[0].get<float>(),p[1].get<float>(),p[2].get<float>());
                }
                {
                    std::lock_guard<std::mutex> lock(orbit_request_mutex);
                    orbit_request = request;
                }
                requested_shot.store(index);
                /*
                    The orbit does not ease - it is placed - so there is nothing to wait for except
                    the physics thread noticing. Waiting on the pending flag rather than on the
                    camera's position, because with no arguments at all there may be no movement to
                    detect and a position test would spin for its whole timeout.
                */
                for (int waited_ms = 0; waited_ms < 2000; waited_ms += 4){
                    std::lock_guard<std::mutex> lock(orbit_request_mutex);
                    if (!orbit_request.f_pending){
                        break;
                    }
                    Sleep(4);
                }
                return MaybeAttachScreenshot(BuildLayoutJson(),
                                             args.value("include_screenshot",true),
                                             args.value("include_ui",false));
            }
            //An MCP handler holds no lock and must not touch the scene: post the request and let
            //the next UpdateView apply it.
            requested_shot.store(index);
            /*
                Then wait for the ease to actually get there, because a screenshot taken now would
                be of the OLD framing and would look like the tool had not worked. Bounded, and
                measured against the camera rather than against a fixed sleep - UpdateView runs off
                the physics loop's pacing, which is not something to guess at.
            */
            for (int waited_ms = 0; waited_ms < 3000; waited_ms += 8){
                if ((main_scene->camera->GetPosition() - shots[index].position).length() < 0.02f){
                    break;
                }
                Sleep(8);
            }
            return MaybeAttachScreenshot(BuildLayoutJson(),
                                         args.value("include_screenshot",true),
                                         args.value("include_ui",false));
        });

    MCPServer::Get()->RegisterTool("pinball_tilt",
        "Read or set the table's tilt angle in degrees. The cabinet is never rotated - the gravity "
        "vector is tilted instead, which is the same simulation and is what keeps every wall on the "
        "machine axis-aligned. This is the single most important feel parameter on the table (it "
        "sets how fast the ball comes down), so it is settable from the very first stage even "
        "though there is nothing on the table yet to feel it. Omit `degrees` to just read it back.",
        json{
            {"type","object"},
            {"properties", {
                {"degrees", {{"type","number"},{"description","0 to 12; a real machine sits at about 6.5"}}}
            }}
        },
        [this](const json& args) -> json {
            if (args.contains("degrees")){
                float wanted = clamp(args.value("degrees",PIN_TILT_DEGREES),0.0f,12.0f);
                requested_tilt.store(wanted);
                f_tilt_dirty.store(true);
                //Let the physics thread pick it up, so the value reported back is the applied one.
                for (int waited_ms = 0; waited_ms < 1000 && f_tilt_dirty.load(); waited_ms += 4){
                    Sleep(4);
                }
            }
            float t = toradians(tilt_degrees);
            return json{
                {"tilt_degrees",tilt_degrees},
                {"gravity",json::array({0.0f,-PIN_GRAVITY * cosf(t),PIN_GRAVITY * sinf(t)})}
            };
        });

    MCPServer::Get()->RegisterTool("pinball_labels",
        "Show or hide the feature name labels painted on the deck. They are world-space geometry "
        "rather than an overlay, so they survive an include_ui:false screenshot - which is what "
        "makes a screenshot of this table reviewable by something with no eyes on the monitor. Turn "
        "them off for a clean look at the machine itself.",
        json{
            {"type","object"},
            {"properties", {
                {"visible", {{"type","boolean"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default true"}}}
            }},
            {"required", json::array({"visible"})}
        },
        [this](const json& args) -> json {
            bool visible = args.value("visible",true);
            /*
                Visibility is a render flag and nothing reads it from the simulation, so this is one
                of the few things an MCP handler can safely write directly. It is NOT a precedent:
                anything that changes what a tick would compute has to go through the command queue,
                and from stage 1 onwards that is almost everything.
            */
            f_show_labels = visible;
            for (size_t i = 0; i < feature_labels.size(); i++){
                feature_labels[i]->SetVisibility(visible);
            }
            return MaybeAttachScreenshot(json{ {"labels_visible",visible} },
                                         args.value("include_screenshot",true),
                                         false);
        });

    //--- Stage 1 -------------------------------------------------------------------------------
    /*
        Four tools the design asks for (pinball_design.md 4, stage 1) and one it did not know it
        needed. The rule they share: an MCP handler holds no lock and never touches the
        simulation. Input goes in as a scripted hold, exactly as a finger would; a teleport goes
        in as a SimCommand; state comes out at a tick boundary. pinball_run is the fifth - it
        steps a PAUSED simulation and returns the ball's path, which turns "does the ball get
        round the orbit" from a question about a screenshot into a list of numbers.
    */
    MCPServer::Get()->RegisterTool("pinball_telemetry",
        "The state of the machine at a tick boundary: the ball's position, velocity and speed "
        "(also as travel per tick, in radii - above 1 is where tunnelling lives), each flipper's "
        "angle from rest and whether it is being driven, the plunger's pull, and the counters: "
        "drains, escapes (a ball found outside the cabinet - the tunnelling detector, should be "
        "0), tunnel-guard interventions and speed clamps, and the fastest the ball has gone.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            json result;
            if (main_scene && main_scene->AtTickBoundary([&]{ result = BuildTelemetryJson(); })){
                return result;
            }
            return json{ {"error","no scene"} };
        });

    MCPServer::Get()->RegisterTool("pinball_flipper",
        "Hold a flipper button for a number of SIMULATION ticks - a scripted press, indistinguishable "
        "to the simulation from a finger. 'left' also drives the upper flipper, as the left button "
        "does on the machine. 240 ticks is a second; a flipper reaches its up stop in about 8. Pass "
        "ticks 0 to release early. While the simulation is running this blocks until the hold has "
        "played out; while it is PAUSED it returns at once and the hold counts down as pinball_run "
        "or sim_step advance the ticks - which is how to script a shot deterministically: pause, "
        "pinball_place_ball, pinball_flipper, pinball_run.",
        json{
            {"type","object"},
            {"properties", {
                {"side", {{"type","string"},{"enum", json::array({"left","right","both"})}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold, default 30, capped at 2400"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG afterwards, default false"}}}
            }},
            {"required", json::array({"side"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string side = args.value("side","left");
            int ticks = (int)clamp(args.value("ticks",30.0f),0.0f,2400.0f);
            uint64_t start = main_scene->GetPhysicsTick();
            if (side == "left" || side == "both")  input->HoldKey(INPUT_PINBALL_FLIP_LEFT,(uint32_t)ticks);
            if (side == "right" || side == "both") input->HoldKey(INPUT_PINBALL_FLIP_RIGHT,(uint32_t)ticks);
            if (ticks > 0 && !main_scene->IsPhysicsPaused()){
                uint64_t target = start + (uint64_t)ticks + 2;
                for (int waited = 0; waited < 12000 && main_scene->GetPhysicsTick() < target; waited += 4){
                    Sleep(4);
                }
            }
            json result;
            main_scene->AtTickBoundary([&]{ result = BuildTelemetryJson(); });
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),false);
        });

    MCPServer::Get()->RegisterTool("pinball_plunger",
        "Pull the plunger back for a number of SIMULATION ticks and let go. The pull runs at "
        "about 3 units per second against a 0.9 travel, so 72 ticks is a full pull and anything "
        "shorter is a softer launch - that is the skill shot. Same scripted-hold semantics as "
        "pinball_flipper: blocks while running, returns at once while paused.",
        json{
            {"type","object"},
            {"properties", {
                {"ticks", {{"type","number"},{"description","ticks to hold the plunger back, default 72 (a full pull)"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG afterwards, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            int ticks = (int)clamp(args.value("ticks",72.0f),0.0f,2400.0f);
            uint64_t start = main_scene->GetPhysicsTick();
            input->HoldKey(INPUT_PINBALL_PLUNGE,(uint32_t)ticks);
            if (ticks > 0 && !main_scene->IsPhysicsPaused()){
                //The pull, plus a quarter of a second for the launch to have happened.
                uint64_t target = start + (uint64_t)ticks + 60;
                for (int waited = 0; waited < 12000 && main_scene->GetPhysicsTick() < target; waited += 4){
                    Sleep(4);
                }
            }
            json result;
            main_scene->AtTickBoundary([&]{ result = BuildTelemetryJson(); });
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),false);
        });

    MCPServer::Get()->RegisterTool("pinball_place_ball",
        "Put the ball anywhere with any velocity, or back on the plunger with none. Goes through "
        "the simulation command queue, so it lands at the top of a tick on the physics thread and "
        "is recorded like any other command. The test the design asks for - fire the ball at every "
        "wall at 90 u/s from both sides and check it never ends up outside the cabinet - is this "
        "tool, pinball_run and the escapes counter.",
        json{
            {"type","object"},
            {"properties", {
                {"position", {{"type","array"},{"description","[x,y,z]; y is normally 0.135, the ball's radius. Omit to serve to the plunger"}}},
                {"velocity", {{"type","array"},{"description","[vx,vy,vz] in units per second; omit for a ball at rest"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            if (args.contains("position")){
                const json& p = args["position"];
                if (!p.is_array() || p.size() != 3){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.type = PIN_CMD_PLACE_BALL;
                cmd.flags |= SIM_CMD_FLAG_POSITION;
                cmd.position = vec3(p[0].get<float>(),p[1].get<float>(),p[2].get<float>());
                if (args.contains("velocity")){
                    const json& v = args["velocity"];
                    if (!v.is_array() || v.size() != 3){
                        return json{ {"error","velocity must be [vx,vy,vz]"} };
                    }
                    cmd.flags |= SIM_CMD_FLAG_VELOCITY;
                    cmd.velocity = vec3(v[0].get<float>(),v[1].get<float>(),v[2].get<float>());
                }
            }else{
                cmd.type = PIN_CMD_SERVE;
            }
            //SubmitCommandAndWait, not SubmitUICommand: an MCP handler holds no lock, so it may
            //wait - and it has to, or it would report the ball where it was. Commands drain while
            //paused too, so this does not hang against a paused table.
            SubmitCommandAndWait(cmd);
            json result;
            main_scene->AtTickBoundary([&]{ result = BuildTelemetryJson(); });
            return result;
        });

    MCPServer::Get()->RegisterTool("pinball_run",
        "Advance a PAUSED simulation by exactly `ticks` ticks and return the ball's path: its "
        "position and speed every `sample` ticks, plus what happened on the way (drains, escapes, "
        "guard interventions, the fastest speed). Requires sim_pause first - a running table cannot "
        "be stepped. Scripted holds (pinball_flipper, pinball_plunger) count down inside these "
        "ticks, so pause, place, hold, run is a reproducible shot. Capped at 4800 ticks (20 s).",
        json{
            {"type","object"},
            {"properties", {
                {"ticks", {{"type","number"},{"description","ticks to advance, default 240"}}},
                {"sample", {{"type","number"},{"description","record the ball every this many ticks, default 12"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG at the end, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","the simulation is running; sim_pause first"} };
            }
            int ticks  = (int)clamp(args.value("ticks",240.0f),1.0f,4800.0f);
            int sample = (int)clamp(args.value("sample",12.0f),1.0f,(float)ticks);
            uint32_t drains_before = drains, escapes_before = escapes, guard_before = guard_hits;
            float max_speed = 0.0f;
            json path = json::array();
            auto record = [&](){
                Physics* p = ball_object ? ball_object->GetPhysics() : NULL;
                if (!p){
                    return;
                }
                vec3 pos = p->GetBodyWorldPosition();
                float speed = p->GetVelocity().length();
                if (speed > max_speed) max_speed = speed;
                path.push_back(json::array({(uint64_t)main_scene->GetPhysicsTick(),
                                            pos.x,pos.y,pos.z,speed}));
            };
            main_scene->AtTickBoundary(record);
            int done = 0;
            while (done < ticks){
                int step = min(sample,ticks - done);
                uint64_t advanced = StepPhysicsAndWait(step);
                done += (int)advanced;
                main_scene->AtTickBoundary(record);
                if ((int)advanced < step){
                    break;      //timed out; report what ran rather than pretend
                }
            }
            json result;
            main_scene->AtTickBoundary([&]{ result = BuildTelemetryJson(); });
            result["ticks_advanced"] = done;
            result["path"] = path;
            result["path_columns"] = json::array({"tick","x","y","z","speed"});
            result["events"] = json{
                {"drains",(uint32_t)drains - drains_before},
                {"escapes",(uint32_t)escapes - escapes_before},
                {"guard_hits",(uint32_t)guard_hits - guard_before},
                {"max_speed",max_speed}
            };
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),false);
        });
}
