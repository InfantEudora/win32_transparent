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

//The camera eases toward the current shot by this fraction of the remaining distance per pass.
//A rate rather than a duration, so interrupting a move to go somewhere else costs nothing.
#define PIN_CAMERA_EASE         0.10f

//--- The ramps, as centrelines ------------------------------------------------------------------
/*
    Where the ball runs, not where the floor is: MakeRibbon centres its slab on the path and hangs
    the thickness BELOW it, so a point here is a place the ball can be. That is what makes these
    numbers checkable - "the crest is 0.85 above the deck" means the ball is 0.85 above the deck.

    Both ramps end short of the inlane they feed, at z = 3.70 and 0.32 up rather than the
    (x, 4.30) pinball_design.md 1.5 gives. A habitrail 0.46 wide centred at x = -1.70 spans -1.93
    to -1.47, which at z = 4.30 has it lying across the inlane/outlane divider AND through the
    slingshot, which stands 0.34 proud of the deck. Stopping up-table of the slingshot and a ball's
    height above it drops the ball into the mouth of the inlane instead, which is what the design
    describes and what a real habitrail does: it stops, and the ball falls the last little way.

    The right ramp's loop over the bumper nest sits at y = 0.86, not the 0.76 that reads naturally
    from the design's 0.70 crest. Even with the bumpers lowered to 0.63 overall (see
    PIN_BUMPER_HEIGHT), 0.76 put the loop's underside 0.05 BELOW the nearest bumper cap - the ramp
    floor and the cap occupying the same space. 0.86 clears it by 0.13, which is all a shape that
    no ball is meant to roll over needs.
*/
static const vec3 kLeftRampPath[] = {
    vec3(-2.05f, 0.00f,  2.00f),    //entry, at deck level, with the spinner in its mouth
    vec3(-2.16f, 0.20f,  1.10f),
    vec3(-2.27f, 0.52f,  0.10f),
    vec3(-2.30f, 0.85f, -1.00f),    //crest
    vec3(-2.00f, 0.86f, -1.55f),    //the habitrail turns right...
    vec3(-1.55f, 0.84f, -1.45f),
    vec3(-1.45f, 0.78f, -0.70f),    //...and back down-table
    vec3(-1.50f, 0.66f,  0.40f),
    vec3(-1.55f, 0.52f,  1.60f),
    vec3(-1.62f, 0.34f,  2.80f),
    vec3(-1.70f, 0.32f,  3.70f),    //drops into the mouth of the left inlane
};

static const vec3 kRightRampPath[] = {
    vec3( 1.45f, 0.00f,  2.20f),    //entry
    vec3( 1.55f, 0.18f,  1.30f),
    vec3( 1.64f, 0.44f,  0.40f),
    vec3( 1.70f, 0.70f, -0.60f),    //crest
    vec3( 1.35f, 0.76f, -1.45f),
    vec3( 0.55f, 0.86f, -2.00f),
    vec3(-0.20f, 0.86f, -2.35f),    //the loop, over the bumper nest
    vec3(-0.70f, 0.80f, -1.85f),
    vec3(-0.55f, 0.70f, -1.15f),
    vec3( 0.10f, 0.60f, -0.70f),
    vec3( 0.70f, 0.46f,  0.30f),
    vec3( 0.95f, 0.30f,  1.70f),
    vec3( 1.05f, 0.34f,  3.10f),
    vec3( 1.10f, 0.32f,  3.70f),    //drops into the mouth of the right inlane
};

//--- The camera shots ---------------------------------------------------------------------------
/*
    Answer 2 to the design document's open questions: a fixed view like the original, framed to
    match the reference artwork, that MOVES somewhere when something happens there and comes back.

    PIN_SHOT_TABLE is the fixed one and the only one stage 0 starts in. The other two exist to
    prove the mechanism and, more immediately, to be able to look closely at a corner of the layout
    without hand-flying a camera - which is most of what stage 0 is for.

    --- HOW THE TABLE SHOT WAS AIMED, BECAUSE GUESSING IT DOES NOT WORK --------------------------
    The first attempt was picked by eye and cut the flippers off the bottom of the frame, which is
    not a thing you can see coming from the numbers. The framing is really decided by two points,
    and once they are named it falls out. For the PLAYFIELD shot those are

        A = (0, 0.00, +6.90)    the drain end of the cabinet - the NEAREST thing in the shot
        C = (0, 1.30, -7.20)    the top rail at the far end  - the FARTHEST

    and from a camera at (0,16,11) they lie 18.4 degrees either side of the direction bisecting
    them, so a 39 degree vertical fov holds both. That bisector meets the deck at z = +0.72, and
    THAT is where the camera looks - not the middle of the playfield. Perspective makes the near
    end of a 13-unit table take far more of the frame than the far end, so aiming at the geometric
    centre puts the drain off the bottom edge every time. It did.

    The view axis comes out 33 degrees off vertical: the artwork's near-overhead look, with just
    enough perspective left that the ramps read as standing above the deck rather than painted on
    it.

    PIN_SHOT_MACHINE is the same arithmetic with B = (0,4.10,-7.55), the top of the backbox, in
    place of C - it backs off to (0,20,14) and looks at z = -0.83. It is worth having, because the
    machine does have a cabinet and a backglass, but it is not the default: with the backbox in
    frame the playfield only gets about half the height, and the playfield is the game.

    WIDTH IS THE WINDOW'S JOB, NOT THE CAMERA'S. Once the table's LENGTH has fixed the fov and the
    distance, the only thing left deciding how much width is visible is the aspect ratio - so a
    table that fills its frame needs a window about as narrow as a real cabinet is. See the Resize
    call at the end of Init.
*/
static const PinShot kShots[PIN_SHOT_COUNT] = {
    { "table",   vec3(PIN_CENTRE_X, 16.0f, 11.0f), vec3(PIN_CENTRE_X, 0.20f,  0.72f), 39.0f },
    //Both detail shots are aimed by the same two-point method as the table shot, over the stretch
    //each is meant to show. Picked by eye they were both wrong in the same way: too low, so the
    //cabinet's far wall stood up across the top of the frame and the shot was half wall.
    //  upper: z -2.00 (the bumper nest) to z -7.20 (the top rail) -> 13.1 degrees, fov 30
    //  lower: z +6.60 (the drain)       to z +1.80 (mid-table)    -> 12.3 degrees, fov 28,
    //         and steep enough to see over the cabinet's front wall - see BuildDeckAndCabinet.
    { "upper",   vec3(PIN_CENTRE_X,  9.0f,  2.0f), vec3(PIN_CENTRE_X, 0.15f, -4.79f), 30.0f },
    { "lower",   vec3(PIN_CENTRE_X, 10.0f,  8.0f), vec3(PIN_CENTRE_X, 0.15f,  4.31f), 28.0f },
    { "machine", vec3(PIN_CENTRE_X, 20.0f, 14.0f), vec3(PIN_CENTRE_X, 0.20f, -0.83f), 39.0f },
    /*
        PIN_SHOT_ORBIT. The position and target here are never used - UpdateCameraShot hands the
        camera to UpdateOrbitControls instead of easing toward them - but the entry has to exist so
        that kShots[current_shot].name works for every shot, and the fov IS used, as the value the
        orbit starts at. See the mode note in ApplicationPinball.h.
    */
    { "orbit",   vec3(),                                vec3(),                             39.0f },
};

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
                                    float thickness, int material){
    Mesh* mesh = MakeWallStrip(path,height,thickness,true);
    if (!mesh){
        debug->Err("AddWall(%s): the path made no mesh\n",name);
        return NULL;
    }
    //A swept mesh is already in world coordinates - the path was - so the Object sits at the
    //origin and the geometry carries the position. Stage 1's collider chain will read the same
    //path rather than this object's transform, which is the point of building both from one
    //source (pinball_design.md 2.3).
    return AddMeshObject(name,mesh,vec3(),material);
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
    Object* post = new Object();
    post->SetMesh(post_mesh);
    post->name = buffer;
    post->SetPosition(vec3(x,0.24f,z));
    post->SetMaterialSlot(0,material_chrome);
    main_scene->AddObject(post);

    snprintf(buffer,sizeof(buffer),"%s_rubber",name);
    Object* rubber = new Object();
    rubber->SetMesh(rubber_mesh);
    rubber->name = buffer;
    rubber->SetPosition(vec3(x,0.17f,z));
    rubber->SetMaterialSlot(0,material_rubber);
    main_scene->AddObject(rubber);
}

Object* ApplicationPinball::AddFlipper(const char* name, float x, float z, float length,
                                       float angle_degrees, bool f_mirrored){
    /*
        The bat is built with its PIVOT AT THE OBJECT ORIGIN and extending along its own +X (or -X
        when mirrored), because that is what stage 1's hinge joint needs and what the Blender
        flipper_bat has to match - pinball_design.md 3.2 calls it out as the modelling note most
        likely to cost a round trip. A bat whose origin is in its middle swings around its middle
        and looks broken, and the fault is invisible until it moves.
    */
    PinPath bat;
    float dir = f_mirrored ? -1.0f : 1.0f;
    AppendXZ(bat,0.0f,0.0f,0.0f);
    AppendXZ(bat,dir * length,0.0f,0.0f);
    //Slightly wider than PIN_FLIPPER_WIDTH so the placeholder reads as a bat rather than a wire;
    //the taper a real one has is the modelled mesh's job.
    Mesh* mesh = MakeWallStrip(bat,PIN_FLIPPER_THICKNESS,PIN_FLIPPER_WIDTH,true);
    if (!mesh){
        return NULL;
    }
    Object* object = AddMeshObject(name,mesh,vec3(x,0.06f,z),material_orange);
    if (object){
        //Positive angle points the tip UP-TABLE for both hands - see the convention note at
        //PIN_FLIPPER_REST_DEG. The mirror is in the bat's own direction, above, not in the sign.
        object->SetRotation(quat(vec3(0,1,0),toradians(angle_degrees)));
    }
    return object;
}

//A path offset sideways from another. `amount` is positive to the LEFT of travel, matching
//TableBuilder's own sense. Used to turn a ramp centreline into its two rails without writing the
//rails out by hand - which would be two more chances for the art to disagree with itself.
static PinPath OffsetPath(const PinPath& path, float amount){
    PinPath out;
    const vec3 up = vec3(0,1,0);
    for (size_t i = 0; i < path.size(); i++){
        //The heading at a point is the average of the segments either side of it, so the offset
        //curve stays parallel round a bend instead of stepping at every join.
        vec3 heading = vec3();
        if (i > 0){
            vec3 d = path[i] - path[i - 1];
            heading += vec3(d.x,0.0f,d.z).normalize();
        }
        if (i + 1 < path.size()){
            vec3 d = path[i + 1] - path[i];
            heading += vec3(d.x,0.0f,d.z).normalize();
        }
        if (heading.length() < 0.0001f){
            continue;
        }
        heading.normalize();
        out.push_back(path[i] + up.cross(heading) * amount);
    }
    return out;
}

//--- Setup --------------------------------------------------------------------------------------

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
    post_mesh        = MakeCylinder(0.075f,0.48f,14,true);
    rubber_mesh      = MakeCylinder(0.105f,0.16f,14,true);
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
    RegisterMCPTools();

    //240, and taken from the rules' own header rather than written here as a literal - see PIN_TPS
    //in Table.h for the tunnelling arithmetic that picks it.
    SetPhysicsTPS(PIN_TPS);
    ApplyTilt();

    /*
        Portrait, and narrow - because a pinball cabinet is. See the width note in the kShots
        comment: the table's LENGTH has already fixed the fov and the camera distance, so the only
        control left over how much width is on screen is this aspect ratio. At 0.66 the shot shows
        about 11.3 units across against the cabinet's 6.9, which leaves roughly two units of margin
        either side - room for the score and mission readouts stage 5 wants, without the machine
        itself shrinking into the middle of a letterboxed frame.
    */
    main_window->Resize(760,1150);

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
        { "pin_cabinet",   vec4(0.085f,0.090f,0.105f,1.0f), 0.25f, 0.45f, vec4(0,0,0,1),                 &material_cabinet },
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

    //One box, top face at y = 0, everything else buried. In stage 1 this is one collider and one
    //collider only - a playfield is the easiest surface on the whole machine to get right.
    AddBox("deck",vec3(deck_cx,-PIN_DECK_THICKNESS * 0.5f,deck_cz),
           vec3(deck_w,PIN_DECK_THICKNESS,deck_l),material_deck);

    //The four cabinet walls, their INNER faces on the play area's bounds and their thickness
    //outward. 0.6 thick is not styling: it is twice the worst-case per-tick travel, which is
    //mitigation 1 of pinball_design.md 2.2 and the only reason a ball at 90 u/s stays inside.
    const float t  = PIN_WALL_THICKNESS;
    const float h  = PIN_CABINET_HEIGHT;
    const float cy = h * 0.5f;
    const float inner_x = 2.85f;
    const float inner_z = 6.60f;

    AddBox("cabinet_left", vec3(-inner_x - t * 0.5f,cy,0.0f),
           vec3(t,h,(inner_z + t) * 2.0f),material_cabinet);
    AddBox("cabinet_right",vec3( inner_x + t * 0.5f,cy,0.0f),
           vec3(t,h,(inner_z + t) * 2.0f),material_cabinet);
    AddBox("cabinet_top",  vec3(0.0f,cy,-inner_z - t * 0.5f),
           vec3(inner_x * 2.0f,h,t),material_cabinet);
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
    AddBox("cabinet_front",vec3(0.0f,0.275f, inner_z + t * 0.5f),
           vec3(inner_x * 2.0f,0.55f,t),material_cabinet);

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
        Everything below the bumper nest, built once for the left and once mirrored - the mirror is
        PIN_MIRROR_X and not a second set of typed numbers, so the two halves cannot disagree.

        Read it as four lanes side by side at z = 4.6, outside in:
            cabinet -2.85 | dead space | -2.30 outlane guide | outlane | -1.80 divider | inlane |
            -1.41 slingshot face | the flipper
        which is the arrangement every machine has had for fifty years, and which the published
        coordinates did not quite describe - see changes 2 to 5 in Table.h.
    */
    for (int side = 0; side < 2; side++){
        const bool f_mirror = (side == 1);
        const char* tag = f_mirror ? "right" : "left";
        //One lambda-free helper: mirror an x, or do not.
        #define PIN_X(v) (f_mirror ? PIN_MIRROR_X(v) : (v))
        char name[64];

        //The inlane / outlane divider.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_MIN_Z);
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_MAX_Z);
            snprintf(name,sizeof(name),"divider_%s",tag);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        //The outlane's outer guide. Angled in slightly at the top so a ball rolling down the side
        //is steered into the lane rather than meeting the end of a wall.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X - 0.18f),PIN_OUTLANE_MIN_Z);
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X),PIN_OUTLANE_MIN_Z + 0.55f);
            AppendXZ(p,PIN_X(PIN_OUTLANE_L_X),PIN_OUTLANE_MAX_Z);
            snprintf(name,sizeof(name),"outlane_guide_%s",tag);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        //The slingshot: a rubber face on the hypotenuse, with a plastic over it.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_SLING_L_AX),PIN_SLING_L_AZ);
            AppendXZ(p,PIN_X(PIN_SLING_L_BX),PIN_SLING_L_BZ);
            snprintf(name,sizeof(name),"sling_%s",tag);
            AddWall(name,p,0.34f,0.16f,material_rubber);

            //The plastic that covers the mechanism, floating just above the rubber. Reads as the
            //orange wedge the artwork has above each flipper.
            float mx = PIN_X((PIN_SLING_L_AX + PIN_SLING_L_BX) * 0.5f);
            float mz = (PIN_SLING_L_AZ + PIN_SLING_L_BZ) * 0.5f;
            //The face runs 0.60 across and 0.80 down-table, so it is 53 degrees off the x axis;
            //the mirrored one leans the other way.
            float yaw = f_mirror ? 53.13f : -53.13f;
            snprintf(name,sizeof(name),"sling_plastic_%s",tag);
            AddBox(name,vec3(mx + (f_mirror ? -0.14f : 0.14f),0.30f,mz - 0.08f),
                   vec3(0.92f,0.04f,0.34f),material_cream,yaw);
        }
        //The lip that carries the inlane onto the flipper, below where the divider stops.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_MAX_Z);
            AppendXZ(p,PIN_X(PIN_DIVIDER_L_X + 0.22f),PIN_DIVIDER_MAX_Z + 0.30f);
            snprintf(name,sizeof(name),"inlane_lip_%s",tag);
            AddWall(name,p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
        }
        //The wall that funnels a drained ball from the outlane across to the outhole, under the
        //apron. Visible here because there is no glass and no apron art yet; it will be hidden.
        {
            PinPath p;
            AppendXZ(p,PIN_X(PIN_SAVE_L_X - 0.24f),PIN_OUTLANE_MAX_Z);
            AppendXZ(p,PIN_X(-0.95f),PIN_APRON_MIN_Z + 0.18f);
            snprintf(name,sizeof(name),"outhole_funnel_%s",tag);
            AddWall(name,p,0.26f,PIN_RAIL_VISUAL_THICK,material_rail);
        }

        //The switches, as lamp inserts. Dark until stage 4 has something to light them for.
        snprintf(name,sizeof(name),"insert_inlane_%s",tag);
        AddInsert(name,PIN_X(PIN_INLANE_L_X),PIN_INLANE_L_Z,0.13f,material_lamp_dark);
        snprintf(name,sizeof(name),"insert_save_%s",tag);
        AddInsert(name,PIN_X(PIN_SAVE_L_X),PIN_SAVE_L_Z,0.15f,material_lamp_dark);

        //Two posts per side, where the lanes divide. These are where the rubbers that shape a
        //draining ball's last bounce live.
        snprintf(name,sizeof(name),"lane_%s_a",tag);
        AddPost(name,PIN_X(PIN_DIVIDER_L_X),PIN_DIVIDER_MIN_Z - 0.10f);
        snprintf(name,sizeof(name),"lane_%s_b",tag);
        AddPost(name,PIN_X(PIN_OUTLANE_L_X - 0.22f),PIN_OUTLANE_MIN_Z - 0.05f);

        #undef PIN_X
    }

    //The three flippers, parked at rest. Nothing moves them yet; stage 1 gives each a hinge joint
    //with a motor and these become the bodies it drives.
    AddFlipper("flipper_left", PIN_FLIPPER_L_X,PIN_FLIPPER_L_Z,PIN_FLIPPER_LENGTH,
               PIN_FLIPPER_REST_DEG,false);
    AddFlipper("flipper_right",PIN_FLIPPER_R_X,PIN_FLIPPER_R_Z,PIN_FLIPPER_LENGTH,
               PIN_FLIPPER_REST_DEG,true);
    AddFlipper("flipper_upper",PIN_FLIPPER_U_X,PIN_FLIPPER_U_Z,PIN_FLIPPER_U_LENGTH,
               PIN_FLIPPER_U_REST_DEG,false);
}

void ApplicationPinball::BuildLauncher(){
    //The chute divider: the long wall separating the launch lane from the play area, from the
    //plunger all the way up to where it hands the ball to the top orbit.
    {
        PinPath p;
        AppendXZ(p,PIN_CHUTE_DIVIDER_X,PIN_CHUTE_MAX_Z);
        AppendXZ(p,PIN_CHUTE_DIVIDER_X,PIN_CHUTE_MIN_Z);
        AddWall("chute_divider",p,0.55f,0.18f,material_rail);
    }

    /*
        The plunger, as three pieces along the lane. A slider joint with a spring return in stage 1;
        scenery now, but placed so the geometry it will need is already right - in particular the
        rod passes THROUGH the cabinet wall and the knob sits outside it, which is the one part of
        a plunger that has to be modelled rather than implied.
    */
    const float ball_y = PIN_BALL_RADIUS;
    {
        Mesh* tip = MakeCylinder(0.11f,0.10f,18,true);
        Object* o = AddMeshObject("plunger_tip",tip,vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + 0.05f),
                                  material_chrome);
        //MakeCylinder's axis of revolution is +Y; the plunger's is +Z. Rotating +90 about X takes
        //+Y to +Z, which is the whole conversion.
        if (o) o->SetRotation(quat(vec3(1,0,0),toradians(90.0f)));
    }
    {
        Mesh* rod = MakeCylinder(0.05f,0.95f,12,true);
        Object* o = AddMeshObject("plunger_rod",rod,vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + 0.58f),
                                  material_chrome);
        if (o) o->SetRotation(quat(vec3(1,0,0),toradians(90.0f)));
    }
    {
        Mesh* knob = MakeSphere(0.14f,18,10);
        AddMeshObject("plunger_knob",knob,vec3(PIN_PLUNGER_X,ball_y,PIN_PLUNGER_Z + 1.30f),
                      material_orange);
    }

    //The three skill-shot rollovers up the lane, and the gate at the top of it.
    AddInsert("insert_skill_low", PIN_CHUTE_X,PIN_SKILL_Z_0,0.13f,material_lamp_dark);
    AddInsert("insert_skill_mid", PIN_CHUTE_X,PIN_SKILL_Z_1,0.13f,material_lamp_dark);
    AddInsert("insert_skill_high",PIN_CHUTE_X,PIN_SKILL_Z_2,0.13f,material_lamp_lit);

    //The one-way gate. A hinge with asymmetric limits in stage 1 - it opens into the orbit and
    //will not open back - so it is drawn where the flap hangs, leaning into the lane.
    AddBox("gate_flap",vec3(PIN_GATE_X,0.20f,PIN_GATE_Z),vec3(0.30f,0.30f,0.05f),
           material_chrome,-24.0f);

    //The ball, parked where the plunger will serve it. Scenery until stage 1 gives it a body; it is
    //here now because a table with no ball on it is missing the one object everything is scaled to.
    ball_object = AddMeshObject("ball",ball_mesh,vec3(PIN_CHUTE_X,ball_y,PIN_PLUNGER_Z - 0.30f),
                                material_ball);
}

void ApplicationPinball::BuildUpperPlayfield(){
    //The top orbit: one arc, sampled fine enough that the chord error is a twentieth of the ball's
    //radius. This single curve is the signature shot on the machine.
    {
        PinPath p;
        AppendArc(p,vec3(PIN_ORBIT_CX,0.0f,PIN_ORBIT_CZ),PIN_ORBIT_RADIUS,
                  PIN_ORBIT_START_DEG,PIN_ORBIT_END_DEG,PIN_ARC_SEGMENTS);
        AddWall("rail_orbit",p,0.46f,PIN_RAIL_VISUAL_THICK,material_chrome);
    }

    /*
        The left orbit return: the lane a ball exiting the horseshoe runs down, on its way to the
        upper flipper. Its outer wall is the cabinet, so this is only the inner one - and it starts
        exactly where the orbit rail ends, so the ball is never handed off across a gap.
    */
    {
        PinPath p;
        AppendXZ(p,-2.32f,-5.35f);
        AppendXZ(p,-2.30f,-4.30f);
        AppendXZ(p,-2.45f,-3.20f);
        AppendXZ(p,-2.50f,-2.30f);
        AddWall("rail_orbit_return",p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }

    /*
        The FUEL lanes: three rollovers under the orbit, divided by four short walls. The outer two
        have to stay inside the horseshoe (see changes 6 and 7 in Table.h), which is what fixes
        their x - 2.10 out is as far as they go before the orbit rail is in the way.
    */
    {
        const float lane_x[4] = { -2.10f, -0.90f, 0.30f, 1.50f };
        for (int i = 0; i < 4; i++){
            PinPath p;
            AppendXZ(p,lane_x[i],PIN_FUEL_WALL_MIN_Z);
            AppendXZ(p,lane_x[i],PIN_FUEL_WALL_MAX_Z);
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
        Object* rim = new Object();
        rim->SetMesh(saucer_mesh);
        rim->name = name;
        rim->SetPosition(vec3(saucers[i].x,0.02f,saucers[i].z));
        rim->SetMaterialSlot(0,material_teal);
        main_scene->AddObject(rim);

        //0.07 is the rim's own top face - it is a 0.10-tall disc centred at 0.02. Putting the hole
        //there leaves a 0.05 ring of rim showing around a dark centre, which is what a saucer is.
        snprintf(name,sizeof(name),"%s_hole",saucers[i].name);
        AddInsert(name,saucers[i].x,saucers[i].z,PIN_SAUCER_RADIUS - 0.05f,material_rubber,0.07f);
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
        Object* body = new Object();
        body->SetMesh(bumper_body_mesh);
        body->name = name;
        body->SetPosition(vec3(bumpers[i].x,PIN_BUMPER_HEIGHT * 0.5f,bumpers[i].z));
        body->SetMaterialSlot(0,material_cream);
        main_scene->AddObject(body);

        //The cap, which is the wider thing the player sees and the thing the ramp above has to
        //clear. Skirt radius, not collider radius - the gap between the two IS the design.
        snprintf(name,sizeof(name),"%s_cap",bumpers[i].name);
        Object* cap = new Object();
        cap->SetMesh(bumper_cap_mesh);
        cap->name = name;
        cap->SetPosition(vec3(bumpers[i].x,PIN_BUMPER_HEIGHT + 0.04f,bumpers[i].z));
        cap->SetMaterialSlot(0,material_orange);
        main_scene->AddObject(cap);

        //The lit ring in the deck under it.
        snprintf(name,sizeof(name),"%s_insert",bumpers[i].name);
        AddInsert(name,bumpers[i].x,bumpers[i].z,PIN_BUMPER_SKIRT_RADIUS,material_lamp_dark);
    }

    //The two walls that keep a ball rattling around inside the nest instead of falling straight
    //through it. Without these a pop bumper cluster scores once and spits the ball out.
    {
        PinPath p;
        AppendXZ(p,-2.05f,-1.35f);
        AppendXZ(p,-1.95f,-2.20f);
        AppendXZ(p,-1.90f,-3.20f);
        AppendXZ(p,-1.45f,-3.85f);
        AddWall("nest_guide_left",p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }
    {
        PinPath p;
        AppendXZ(p, 0.85f,-1.35f);
        AppendXZ(p, 0.78f,-2.20f);
        AppendXZ(p, 0.72f,-3.20f);
        AppendXZ(p, 0.30f,-3.85f);
        AddWall("nest_guide_right",p,PIN_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_rail);
    }

    //--- The MISSION drop target bank ------------------------------------------------------------
    //Kinematic in stage 2, dropped with Scene::MoveObjectOverTicks - no joint needed, and it steps
    //and replays correctly, which a hand-animated transform would not.
    {
        const float z[3] = { PIN_DROP_Z_0,PIN_DROP_Z_1,PIN_DROP_Z_2 };
        const char* letters[3] = { "drop_m","drop_i","drop_s" };
        for (int i = 0; i < 3; i++){
            AddBox(letters[i],vec3(PIN_DROP_X,PIN_DROP_HEIGHT * 0.5f,z[i]),
                   vec3(PIN_DROP_DEPTH,PIN_DROP_HEIGHT,PIN_DROP_WIDTH),material_cream);
        }
        //No wall of its own any more. Now that the bank is on the right it backs straight onto
        //the chute divider, which is already there and already 0.55 tall.
    }

    //--- Standups ---------------------------------------------------------------------------------
    AddBox("standup_upper",vec3(PIN_STANDUP_0_X,0.16f,PIN_STANDUP_0_Z),
           vec3(0.10f,0.32f,0.40f),material_teal);
    AddBox("standup_lower",vec3(PIN_STANDUP_1_X,0.16f,PIN_STANDUP_1_Z),
           vec3(0.10f,0.32f,0.40f),material_teal);

    //--- The spinner ------------------------------------------------------------------------------
    //A free hinge, hanging in the mouth of the left ramp so the ball passes under it on the way in.
    //Counted by revolutions off the joint's own angle rather than by contacts.
    AddBox("spinner_vane",vec3(PIN_SPINNER_X,0.22f,PIN_SPINNER_Z),
           vec3(PIN_SPINNER_WIDTH,PIN_SPINNER_HEIGHT,0.03f),material_chrome);
    AddPost("spinner_a",PIN_SPINNER_X - PIN_SPINNER_WIDTH * 0.5f - 0.09f,PIN_SPINNER_Z);
    AddPost("spinner_b",PIN_SPINNER_X + PIN_SPINNER_WIDTH * 0.5f + 0.09f,PIN_SPINNER_Z);

    //--- The gravity well -------------------------------------------------------------------------
    {
        Object* rim = new Object();
        rim->SetMesh(saucer_mesh);
        rim->name = "gravity_well_rim";
        rim->SetPosition(vec3(PIN_WELL_X,0.02f,PIN_WELL_Z));
        rim->SetMaterialSlot(0,material_orange);
        main_scene->AddObject(rim);
        AddInsert("gravity_well_hole",PIN_WELL_X,PIN_WELL_Z,PIN_SAUCER_RADIUS - 0.05f,
                  material_rubber,0.07f);
    }

    /*
        The eight loose posts, placed where a ball needs deflecting rather than evenly spaced.

        All eight moved once. The first set was put down by eye and five of them turned out to be
        standing inside a ramp - a post is only 0.22 across but it is 0.48 tall, which is taller
        than either ramp is off the deck for most of its length. These are the positions
        tools/pinball_clearance.py passes; run it again before moving any of them.
    */
    AddPost("post_well_a",PIN_WELL_X - 0.55f,PIN_WELL_Z - 0.40f);
    AddPost("post_well_b",PIN_WELL_X + 0.55f,PIN_WELL_Z - 0.40f);
    AddPost("post_drop_a",PIN_DROP_X - 0.25f,PIN_DROP_Z_0 + 0.30f);
    AddPost("post_drop_b",PIN_DROP_X - 0.25f,PIN_DROP_Z_2 - 0.30f);
    AddPost("post_ramp_l",-2.62f, 2.90f);
    AddPost("post_ramp_r", 1.55f, 2.60f);
    AddPost("post_mid_l", -1.00f, 2.00f);
    AddPost("post_mid_r",  0.00f, 2.00f);
}

void ApplicationPinball::BuildRamps(){
    /*
        A ramp is three swept solids off ONE centreline: the floor, and a rail down each side at a
        fixed offset. That is the whole argument for TableBuilder - the rails cannot drift from the
        floor because they are not independently authored, and in stage 1 the collider chain reads
        the same three paths.
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

        char name[64];
        snprintf(name,sizeof(name),"%s_floor",ramps[r].name);
        Mesh* floor = MakeRibbon(centre,PIN_RAMP_WIDTH,PIN_RAMP_SLAB);
        AddMeshObject(name,floor,vec3(),material_ramp);

        //The rails sit on the floor's edges, half a rail's thickness inboard so their outer faces
        //line up with the floor's rather than hanging over it.
        const float rail_offset = (PIN_RAMP_WIDTH - PIN_RAIL_VISUAL_THICK) * 0.5f;
        for (int side = 0; side < 2; side++){
            PinPath edge = OffsetPath(centre,side == 0 ? rail_offset : -rail_offset);
            snprintf(name,sizeof(name),"%s_rail_%c",ramps[r].name,side == 0 ? 'l' : 'r');
            AddWall(name,edge,PIN_RAMP_RAIL_HEIGHT,PIN_RAIL_VISUAL_THICK,material_chrome);
        }

        //A lit arrow-sized insert at the entry, because a ramp nobody can see the mouth of is a
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
    const float back_z = -7.55f;
    AddBox("backbox",vec3(0.0f,2.05f,back_z),vec3(6.90f,4.10f,0.55f),material_cabinet,0.0f);
    //The glass itself, a hair proud of the housing and leaning back the way a real one does.
    Object* glass = AddBox("backglass",vec3(0.0f,2.10f,back_z + 0.32f),
                           vec3(6.10f,3.40f,PIN_PLATE_THICKNESS),material_backglass);
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
    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_PERSPECTIVE);
    //fov is VERTICAL and in degrees. znear is pulled in to 0.3 because the "lower" shot gets close
    //to the apron; zfar covers the backbox with room to spare.
    camera->SetupPerspective(renderer->width,renderer->height,kShots[PIN_SHOT_TABLE].fov,0.3f,140.0f);

    //Snap to the opening shot rather than easing into it, so the first frame is already framed.
    current_shot   = PIN_SHOT_TABLE;
    shot_position  = kShots[PIN_SHOT_TABLE].position;
    shot_target    = kShots[PIN_SHOT_TABLE].target;
    shot_fov       = kShots[PIN_SHOT_TABLE].fov;
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
    shot_position = kShots[shot].position;
    shot_target   = kShots[shot].target;
    shot_fov      = kShots[shot].fov;
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
    live_fov = shot_fov = kShots[PIN_SHOT_ORBIT].fov;
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

    ImGui::TextColored(ImVec4(1.0f,0.62f,0.18f,1.0f),"STAGE 0 - static layout");
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

    ImGui::Text("camera   %s",kShots[current_shot].name);
    for (int i = 0; i < PIN_SHOT_COUNT; i++){
        //Four fixed shots on one row, the orbit on its own underneath - it is a different kind of
        //thing and a row of five buttons hides that.
        if (i && i != PIN_SHOT_ORBIT){
            ImGui::SameLine();
        }
        if (ImGui::Button(kShots[i].name)){
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
    ImGui::TextDisabled("F1 panels   L labels   1-4 camera");
    ImGui::End();
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

    //The numbers that are consequences rather than inputs. Each one is a thing that was wrong in
    //the first draft of the layout and would be silently wrong again after any edit.
    const float rest = toradians(PIN_FLIPPER_REST_DEG);
    const float tip_gap = (PIN_FLIPPER_R_X - PIN_FLIPPER_L_X)
                        - 2.0f * PIN_FLIPPER_LENGTH * cosf(rest);
    //The inlane's width at the rollover: from the divider to wherever the slingshot face is at
    //that z.
    const float sling_t = (PIN_INLANE_L_Z - PIN_SLING_L_AZ) / (PIN_SLING_L_BZ - PIN_SLING_L_AZ);
    const float sling_x = PIN_SLING_L_AX + sling_t * (PIN_SLING_L_BX - PIN_SLING_L_AX);

    return json{
        {"table","Orbit Outpost"},
        {"stage",0},
        {"units_per_metre",PIN_UNITS_PER_METRE},
        {"ball_diameter",PIN_BALL_DIAMETER},
        {"tilt_degrees",tilt_degrees},
        {"physics_tps",physics_tps},
        {"camera_shot",kShots[current_shot].name},
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
            {"inlane_width",sling_x - PIN_DIVIDER_L_X},
            {"inlane_width_balls",(sling_x - PIN_DIVIDER_L_X) / PIN_BALL_DIAMETER},
            {"outlane_width",PIN_DIVIDER_L_X - PIN_OUTLANE_L_X},
            {"outlane_width_balls",(PIN_DIVIDER_L_X - PIN_OUTLANE_L_X) / PIN_BALL_DIAMETER},
            {"orbit_lane_at_apex",(PIN_ORBIT_CZ - PIN_ORBIT_RADIUS) - PIN_DECK_MIN_Z},
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
                if (wanted == kShots[i].name){
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
                if ((main_scene->camera->GetPosition() - kShots[index].position).length() < 0.02f){
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
}
