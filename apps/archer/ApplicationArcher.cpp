#include "ApplicationArcher.h"
#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit - see the note at the top of it.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#include "Debug.h"
#include "Primitives.h"
#include "type_helpers.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include <math.h>
#include <string.h>
#include <stdio.h>

static Debugger* debug = new Debugger("ApplicationArcher",DEBUG_ALL);

/*
    How deep the world is in z.

    The play plane is z = 0 and Stage's coordinates ARE world coordinates, so nothing is ever
    converted. Depth exists only so that 3D assets read as 3D: a block with thickness catches the
    light on its near face and throws a shadow with a visible edge, where a flat quad reads as a
    sprite. The camera looks straight down -Z, so none of this depth is ever in the way.
*/
#define BLOCK_DEPTH                 3.00f
#define PROP_DEPTH                  0.80f
#define ARCHER_DEPTH                0.70f
#define ARROW_DEPTH                 0.06f

//A target counts as knocked over once it has tipped this far off vertical.
#define TARGET_KNOCKED_DEG          40.0f

ApplicationArcher::ApplicationArcher():Application(){
    debug->Info("ApplicationArcher constructed\n");
}

ApplicationArcher::~ApplicationArcher(){
}

//--- Setup --------------------------------------------------------------------------------------

void ApplicationArcher::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //Still constructed even though this app loads nothing from disk yet: the engine reaches for it
    //unguarded in places (the Scene panel's asset list, the object_spawn command handler). It is
    //also what the real meshes will arrive through once there are any.
    assetmanager = new AssetManager();

    //The archer, the blocks and every prop are all scaled unit boxes, so one mesh serves them
    //all - and the archer's collider then cannot disagree with its own mesh, because both come
    //from the size passed to MakePlanarBody.
    unit_mesh   = MakeBox(vec3(1,1,1));
    arrow_mesh  = MakeBox(vec3(ARROW_HALF_LEN * 2.0f,ARROW_DEPTH,ARROW_DEPTH));
    dot_mesh    = MakeSphere(0.075f,10,6);
    if (!unit_mesh || !arrow_mesh || !dot_mesh){
        debug->Fatal("Failed to build the primitive meshes\n");
    }
    //A reference for the app's own pointer, the same way AssetManager holds one for an asset's
    //mesh: Object::DeleteMesh frees a mesh when its last Object lets go, and these are handed to
    //many objects over the app's life.
    unit_mesh->num_references++;
    arrow_mesh->num_references++;
    dot_mesh->num_references++;

    main_scene = CreateNewScene("Archer");
    main_scene->physics_world = new PhysicsWorld();
    /*
        Gravity for the PROPS ONLY.

        The archer does not use this and neither does an arrow - both are integrated in Stage,
        against ARCHER_GRAVITY and ARROW_GRAVITY, which are four and two times earth's because a
        platformer jump under 9.81 hangs in the air and reads as floaty. The crates and the
        knocked-over targets are the only things this number touches, and they want to look
        physical rather than snappy, so it is nearer the real one - deliberately NOT matched to
        ARCHER_GRAVITY. Two different jobs, two different numbers, and neither is wrong.
    */
    main_scene->physics_world->SetGravity(vec3(0.0f,-18.0f,0.0f));
    main_scene->physics_world->SetDebugRendering(false);

    BuildMaterials();
    BuildBlocks();
    BuildProps();
    BuildArcher();
    BuildArrowViews();
    BuildAimArc();
    SetupLights();
    SetupCamera();
    SetupInput();
    RegisterCommandHandlers();
#ifdef USE_MCP
    RegisterMCPTools();
#endif

    //The rules denominate everything in ticks and were written against this rate - see ARCHER_TPS
    //in Stage.h, which is the one number they cannot look up for themselves.
    SetPhysicsTPS(ARCHER_TPS);

    main_window->Resize(1440,810);      //16:9; a side-scroller wants width far more than height

    //One tick so the first frame is not an empty level.
    main_scene->StepPhysics(1);
}

void ApplicationArcher::BuildMaterials(){
    /*
        A flat, readable palette rather than an attempt at a look.

        THE RULE IS THAT COLOUR MEANS A RULE. Ground you stand on, a ledge you will be able to
        grab, a platform you can drop through and a wall that will break are four different
        behaviours, and a prototype's whole job is to let someone see which is which before they
        touch it. When the real assets land this palette is what they have to keep legible.

        Metallic stays low everywhere on purpose: this app turns the skybox off, so a metallic
        surface gives up its diffuse term and gets no environment back in return - see the long
        note on `metallic` in core/Material.h, and breakout's tough bricks, which rendered black.
    */
    struct Simple{
        const char* name;
        vec4 colour;
        float emissive;
        int* out;
    };
    Simple table[] = {
        //Ground: neutral and dark, so everything standing on it reads first.
        { "ar_ground",      vec4(0.26f,0.28f,0.33f,1.0f), 0.04f, &material_ground },
        //Ledge: warm, because it is the one surface with a verb attached to it.
        { "ar_ledge",       vec4(0.78f,0.55f,0.24f,1.0f), 0.12f, &material_ledge },
        //One-way platform: translucent-looking pale blue. It behaves differently from below, and
        //looking lighter than everything solid is the cheapest way to say so.
        { "ar_platform",    vec4(0.55f,0.76f,0.92f,1.0f), 0.22f, &material_platform },
        //Breakable: cracked-brick red, the colour it will burst into.
        { "ar_breakable",   vec4(0.62f,0.28f,0.24f,1.0f), 0.10f, &material_breakable },
        { "ar_archer",      vec4(0.30f,0.72f,0.42f,1.0f), 0.18f, &material_archer },
        //Hanging and climbing. Distinct enough to read at a glance in a screenshot, close enough
        //in hue that it still reads as the same character rather than a different object.
        { "ar_archer_hang", vec4(0.95f,0.80f,0.25f,1.0f), 0.30f, &material_archer_hang },
        { "ar_crate",       vec4(0.68f,0.52f,0.30f,1.0f), 0.06f, &material_crate },
        { "ar_target",      vec4(0.90f,0.90f,0.88f,1.0f), 0.10f, &material_target },
        //A struck target goes green, so a hit is legible in a screenshot with no HUD at all -
        //which is exactly how this app gets checked over MCP.
        { "ar_target_hit",  vec4(0.30f,0.85f,0.40f,1.0f), 0.45f, &material_target_hit },
        { "ar_arrow",       vec4(0.95f,0.88f,0.55f,1.0f), 0.30f, &material_arrow },
        //Rubble: the breakable red, knocked back and darkened so a pile of chunks reads as debris
        //rather than as a wall that has fallen over intact.
        { "ar_debris",      vec4(0.46f,0.22f,0.19f,1.0f), 0.05f, &material_debris }
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++){
        Material m;
        m.name = table[i].name;
        m.glsl_material.color = table[i].colour;
        m.glsl_material.metallic = 0.12f;
        m.glsl_material.roughness = 0.62f;
        //emissive.w is what actually makes something glow; emissive.rgb alone is clamped to 1.
        //A little of it keeps a surface in shadow reading as its own colour rather than as a grey
        //lump, which matters in a level lit by one sun.
        m.glsl_material.emissive = vec4(table[i].colour.x,table[i].colour.y,table[i].colour.z,
                                        table[i].emissive);
        renderer->AddMaterial(m);
        *table[i].out = renderer->FindMaterialIndex(m.name);
    }

    {   //The aim arc's beads. UNLIT, which is the right tool and not a hack: these are a HUD
        //element that happens to live in the world, and a readout that dims when it passes into
        //shadow is a readout that lies. See `f_unlit` in core/Material.h.
        Material m;
        m.name = "ar_dot";
        m.glsl_material.color = vec4(1.0f,0.92f,0.45f,1.0f);
        m.glsl_material.f_unlit = 1;
        renderer->AddMaterial(m);
        material_dot = renderer->FindMaterialIndex(m.name);
    }
    {   //The last bead - where the arrow actually ends up. A different colour, because "where it
        //lands" is the one thing the player is really reading off the arc.
        Material m;
        m.name = "ar_dot_hot";
        m.glsl_material.color = vec4(1.0f,0.35f,0.25f,1.0f);
        m.glsl_material.f_unlit = 1;
        renderer->AddMaterial(m);
        material_dot_hot = renderer->FindMaterialIndex(m.name);
    }
}

/*
    One dynamic (or static) box body, pinned to the play plane.

    EVERY prop goes through here, which is the point: the pinning is not something a caller can
    forget. A flat game built on a 3D solver needs this on its first day - anything given a nudge
    out of plane by a spawn impulse or a glancing contact drifts along the axis nobody is watching,
    and in breakout a power-up drifted a unit out of plane and passed clean through the paddle,
    generating no contact at all. See the axis-lock note in core/physics/Physics.h; it is the same
    trap, and this function is this app's answer to it.
*/
Object* ApplicationArcher::MakePlanarBody(Mesh* mesh, const char* name, const vec3& position,
                                          const vec3& size, int material, uint32_t category,
                                          uint32_t collide_mask, float mass, bool f_static){
    Object* object = new Object();
    object->SetMesh(mesh);
    object->name = name;
    object->SetPosition(position);
    object->SetScale(vec3(size.x,size.y,size.z));
    //A generated mesh carries no material names, so there is nothing for
    //Renderer::UpdateObjectMaterials to resolve over this slot on the next frame.
    object->SetMaterialSlot(0,material);
    main_scene->AddObject(object);

    Physics* p = object->AddPhysics(main_scene->physics_world);
    if (!p){
        return object;
    }
    /*
        Category and mask before the collider, which is now safe in either order - Physics
        remembers them and every Add*Collider re-applies them. It did not used to be.

        THE MASK IS A PARAMETER RATHER THAN 0xFFFF, and that is not tidiness. "Collides with
        everything" is wrong for this scene in one specific and fatal way: it puts the archer's
        kinematic body in the solver's argument with the static level, which Stage has ALREADY
        resolved by hand. A kinematic body has infinite mass and wins that argument by definition,
        so the symptom is not the archer stopping - it is the archer grinding through the ground
        while every crate and target standing on it gets shoved by the correction. Measured, not
        predicted: with 0xFFFF here, three target boards were flung to x -26, -116 and +78 by an
        archer who had not fired a single arrow.
    */
    p->SetCollisionCategoryBits(category);
    p->SetCollideWithMaskBits(collide_mask);
    //AddBoxCollider takes HALF extents (it goes straight to rp3d's createBoxShape).
    p->AddBoxCollider(vec3(size.x * 0.5f,size.y * 0.5f,size.z * 0.5f),vec3(),quat().identity(),1.0f);
    /*
        SURFACE, WHICH rp3d's DEFAULTS GET WRONG FOR THIS GAME.

        Add*Collider leaves friction at 0.3 and bounciness at 0.5 - see the note above
        AddSphereCollider in core/physics/Physics.h. Half a unit of bounciness is a rubber ball,
        and it shows: a kicked crate hit its neighbour and came straight back past the archer who
        kicked it, ending up LEFT of where it started. 0.3 friction is ice, and a knocked-over
        target board slid for a second and a half after it landed.

        These act on `last_collider`, which is the one added on the line above - every body here
        has exactly one, so this is the right place and the only place.
    */
    p->SetBounciness(0.05f);
    p->SetFrictionCoefficient(0.65f);

    if (f_static){
        //A body from AddPhysics already starts STATIC; saying so is documentation as much as code.
        p->SetStatic(true);
        return object;
    }
    p->SetStatic(false);
    /*
        GRAVITY HAS TO BE TURNED ON. A body from AddPhysics starts STATIC with gravity OFF, which
        is exactly right for a wall and silently wrong for anything that is supposed to fall -
        SetStatic(false) makes it dynamic and does not touch the gravity flag.

        This one cost an afternoon and it is worth knowing what it looks like, because it does not
        look like missing gravity. Nothing floats gently upward; everything LOOKS fine until it is
        touched, and then it never stops. With no gravity there is no weight on the floor, so there
        is no normal force, so there is no friction - a crate given a shove slides forever, and a
        prop given any upward component at all leaves the level and keeps going. It reads exactly
        like an explosion in the solver, and three separate "the archer is flinging things across
        the map" theories were chased before anyone read `gravity: false` off object_get.
    */
    p->SetGravityEnabled(true);
    p->SetMass(mass);
    //The play plane, and rotation only about the axis facing the camera. Without the angular lock
    //a knocked-over target spins out of the plane it was cut from and shows its own edge.
    p->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
    p->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));
    return object;
}

void ApplicationArcher::BuildBlocks(){
    block_objects.clear();
    for (size_t i = 0; i < stage.blocks.size(); i++){
        const StageBlock& b = stage.blocks[i];
        int material = material_ground;
        switch (b.kind){
            case BLOCK_LEDGE:     material = material_ledge;     break;
            case BLOCK_PLATFORM:  material = material_platform;  break;
            case BLOCK_BREAKABLE: material = material_breakable; break;
            default: break;
        }

        char name[48];
        snprintf(name,sizeof(name),"block_%i",(int)i);
        vec3 size(b.hw * 2.0f,b.hh * 2.0f,BLOCK_DEPTH);

        /*
            A one-way platform gets NO rigid body, on purpose.

            rp3d has no one-way collider, and the rule is Stage's anyway - it already lets the
            archer through from below and holds them up from above. Giving it a real collider would
            make it solid to the crates and the debris too, which is the one thing a one-way
            platform must not be: a crate kicked off the ledge above would land on top of it and
            sit there, in mid-air, on a platform the player walks straight through.
        */
        bool f_collides = (b.kind != BLOCK_PLATFORM);
        Object* object = NULL;
        if (f_collides){
            object = MakePlanarBody(unit_mesh,name,vec3(b.x,b.y,0.0f),size,material,
                                    ARCHER_CAT_LEVEL,ARCHER_MASK_LEVEL,0.0f,true);
        }else{
            object = new Object();
            object->SetMesh(unit_mesh);
            object->name = name;
            object->SetPosition(vec3(b.x,b.y,0.0f));
            object->SetScale(size);
            object->SetMaterialSlot(0,material);
            main_scene->AddObject(object);
        }
        block_objects.push_back(object);
    }
    debug->Info("Built %i level blocks\n",(int)block_objects.size());
}

void ApplicationArcher::BuildProps(){
    prop_views.clear();
    for (size_t i = 0; i < stage.props.size(); i++){
        const StageProp& p = stage.props[i];
        char name[48];

        switch (p.kind){
            case PROP_CRATE: {
                snprintf(name,sizeof(name),"crate_%i",(int)i);
                //Light enough that a walking archer visibly shifts it, heavy enough that it does
                //not fly off like a beach ball. This is the first thing anyone will touch, so it
                //is the first number worth tuning.
                Object* o = MakePlanarBody(unit_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(p.w,p.h,PROP_DEPTH),material_crate,
                                           ARCHER_CAT_PROP,ARCHER_MASK_PROP,6.0f,false);
                PropView view;
                view.object = o;
                view.kind = p.kind;
                view.index = (int)i;
                view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
            } break;

            case PROP_TARGET: {
                snprintf(name,sizeof(name),"target_%i",(int)i);
                //A thin board standing on end - so it topples rather than slides, which is what
                //makes a hit readable from across the level with no HUD.
                Object* o = MakePlanarBody(unit_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(p.w,p.h,PROP_DEPTH),material_target,
                                           ARCHER_CAT_PROP,ARCHER_MASK_PROP,3.0f,false);
                PropView view;
                view.object = o;
                view.kind = p.kind;
                view.index = (int)i;
                view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
            } break;

            case PROP_BRICKWALL: {
                /*
                    STATIC BRICKS, for now.

                    Twenty-one dynamic boxes stacked into a wall is twenty-one bodies the solver
                    pays for every tick to hold something perfectly still - and rp3d would have to
                    be argued with to keep the stack from settling into a slouch on its own. The
                    kick-and-break slice flips the bricks it breaks to DYNAMIC at the moment of the
                    kick, which is both cheaper and more controllable than starting them loose.
                    They are built here so that slice has a wall to knock down.
                */
                float bw = p.w / (float)(p.cols > 0 ? p.cols : 1);
                float bh = p.h / (float)(p.rows > 0 ? p.rows : 1);
                for (int r = 0; r < p.rows; r++){
                    for (int c = 0; c < p.cols; c++){
                        snprintf(name,sizeof(name),"brick_%i_%i_%i",(int)i,r,c);
                        float bx = p.x - p.w * 0.5f + bw * ((float)c + 0.5f);
                        float by = p.y - p.h * 0.5f + bh * ((float)r + 0.5f);
                        Object* o = MakePlanarBody(unit_mesh,name,vec3(bx,by,0.0f),
                                                   vec3(bw * 0.97f,bh * 0.94f,PROP_DEPTH),
                                                   material_breakable,ARCHER_CAT_PROP,ARCHER_MASK_PROP,1.5f,true);
                        PropView view;
                        view.object = o;
                        view.kind = p.kind;
                        view.index = -1;    //a brick is not one of Stage's props in its own right
                        view.half_extents = vec3(bw * 0.485f,bh * 0.47f,PROP_DEPTH * 0.5f);
                        prop_views.push_back(view);
                    }
                }
            } break;

            case PROP_ROPE_ANCHOR: {
                BuildRope(p);
                //A marker, not a rope. The rope slice hangs a chain of ball-and-socket joints from
                //here; apps/tank/CraneCharacter.cpp already builds that shape (a pendulum with a
                //load on the end) and is the working example to copy.
                snprintf(name,sizeof(name),"rope_anchor_%i",(int)i);
                Object* o = MakePlanarBody(unit_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(1.20f,0.20f,PROP_DEPTH),material_ledge,
                                           ARCHER_CAT_LEVEL,ARCHER_MASK_LEVEL,0.0f,true);
                PropView view;
                view.object = o;
                view.kind = p.kind;
                view.index = (int)i;
                view.half_extents = vec3(0.60f,0.10f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
                rope_anchor_object = o;
            } break;

            default: break;
        }
    }
    debug->Info("Built %i prop bodies\n",(int)prop_views.size());
}

void ApplicationArcher::BuildArcher(){
    /*
        The hybrid body. See the long note at the top of ApplicationArcher.h - this is the half of
        it that exists at build time, and DriveArcherBody is the half that runs every tick.

        KINEMATIC: it is moved by having its velocity set, it ignores forces and gravity entirely,
        and it shoves dynamic bodies out of its way. It is NOT in the LEVEL category's way and does
        not need to be - Stage resolves the archer against the level by hand, and a second opinion
        from the solver on a question that already has an answer is how you get a character that
        grinds through walls.
    */
    /*
        THE SIZE HERE IS THE SIZE IN Stage.h, and it has to be: MakePlanarBody builds the collider
        from it, so anything else gives the archer a body whose shape disagrees with the box the
        rules are sweeping. The first version of this passed a unit cube and got exactly that - a
        1x1x1 collider around a 0.7x1.8 character, which overlapped things the archer was nowhere
        near.
    */
    archer_object = MakePlanarBody(unit_mesh,"archer",vec3(stage.pos.x,stage.pos.y,0.0f),
                                   vec3(ARCHER_HALF_W * 2.0f,ARCHER_HALF_H * 2.0f,ARCHER_DEPTH),
                                   material_archer,ARCHER_CAT_ARCHER,ARCHER_MASK_ARCHER,
                                   70.0f,false);
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (p){
        p->SetBodyType(rp3d::BodyType::KINEMATIC);
    }
}

void ApplicationArcher::BuildArrowViews(){
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        char name[32];
        snprintf(name,sizeof(name),"arrow_%i",i);
        Object* o = new Object();
        o->SetMesh(arrow_mesh);
        o->name = name;
        o->SetMaterialSlot(0,material_arrow);
        //An arrow is a marker, not a wall: it should not throw a shadow across the level it is
        //stuck in. See f_casts_shadow in core/Object.h, which exists for exactly this.
        o->SetVisibility(false);
        main_scene->AddObject(o);
        arrow_objects[i] = o;
    }
}

void ApplicationArcher::BuildAimArc(){
    for (int i = 0; i < AIM_ARC_POINTS; i++){
        char name[32];
        snprintf(name,sizeof(name),"arc_%i",i);
        Object* o = new Object();
        o->SetMesh(dot_mesh);
        o->name = name;
        o->SetMaterialSlot(0,material_dot);
        o->SetVisibility(false);
        o->SetPickability(false);
        main_scene->AddObject(o);
        arc_objects[i] = o;
    }
}

void ApplicationArcher::SetupLights(){
    {   //The sun. Its position is moved with the camera every tick (see UpdateCamera) because the
        //level is 84 units wide and one shadow ortho covering all of it would be useless - a
        //shadow map spread that thin has no edges left.
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        //Nearly overhead, leaning left and only a little toward the camera. A side view wants its
        //shadows ON THE GROUND beside things, where they read as contact - a sun raked in from the
        //front throws them backwards, behind the very objects casting them, and the level renders
        //looking flatly unlit for reasons that are nothing to do with the lighting being broken.
        sun->SetPosition(vec3(-9.0f,20.0f,10.0f));
        sun->SetLookAt(vec3(0.0f,2.0f,0.0f));
        sun->color = vec3(1.0f,0.96f,0.88f);
        sun->brightness = 4.0f;
        sun->viewport.zoom = 22.0f;     //half extent in world units, a little over one screenful
        main_scene->AddObject(sun);
        sun_light = sun;                //UpdateCamera drags it along every tick
    }
    {   //A cool fill from the front, so faces pointing at the camera are not pure shadow. A second
        //directional light costs one entry in the light SSBO and nothing else.
        DirectionalLight* fill = new DirectionalLight();
        fill->name = "Fill";
        fill->SetPosition(vec3(18.0f,8.0f,26.0f));
        fill->SetLookAt(vec3(0.0f,3.0f,0.0f));
        fill->color = vec3(0.48f,0.62f,1.0f);
        fill->brightness = 1.2f;
        fill->f_casts_shadow = false;
        fill->viewport.zoom = 30.0f;
        main_scene->AddObject(fill);
    }
}

void ApplicationArcher::SetupCamera(){
    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_PERSPECTIVE);
    /*
        Perspective, not orthographic, and square-on rather than tilted.

        Orthographic is the obvious choice for a side view and is the wrong one here: the whole
        premise is 3D assets in a 2D game, and with no foreshortening at all they render as
        cardboard cut-outs. A narrow perspective from a good way back keeps the read flat enough to
        judge a jump by - which is the thing a platformer camera must never compromise - while
        still giving blocks visible sides and letting the sun put a real edge on every shadow.

        fov is VERTICAL and in degrees. 38 at 26 units away shows about 17.9 units of height, which
        is the jump arc (3.2) with the level's tallest structures and enough sky to see an arrow
        at the top of its lob.
    */
    camera->SetupPerspective(renderer->width,renderer->height,38.0f,0.5f,240.0f);
    camera_target = vec3(stage.pos.x,stage.pos.y + 2.0f,0.0f);
    camera_ideal = camera_target;
    camera->SetPosition(vec3(camera_target.x,camera_target.y,CAMERA_DISTANCE));
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();
}

void ApplicationArcher::SetupInput(){
    InputController* input = main_scene->inputcontroller;

    //Two mappings per direction because muscle memory differs and both cost nothing:
    //KeyState::f_isdown counts HELD MAPPINGS rather than being a boolean, so an action stays down
    //while either of its keys is.
    input->AddKeyMap('A',INPUT_ARCHER_LEFT);
    input->AddKeyMap(VK_LEFT,INPUT_ARCHER_LEFT);
    input->AddKeyMap('D',INPUT_ARCHER_RIGHT);
    input->AddKeyMap(VK_RIGHT,INPUT_ARCHER_RIGHT);

    //Drop-through is on S alone. Down is the AIM, and one key meaning two things is how a control
    //scheme starts fighting itself - see the layout note in ApplicationArcher.h.
    input->AddKeyMap('S',INPUT_ARCHER_DOWN);

    input->AddKeyMap(VK_SPACE,INPUT_ARCHER_JUMP);
    input->AddKeyMap('J',INPUT_ARCHER_DRAW);
    input->AddKeyMap(VK_UP,INPUT_ARCHER_AIM_UP);
    input->AddKeyMap(VK_DOWN,INPUT_ARCHER_AIM_DOWN);
    input->AddKeyMap('E',INPUT_ARCHER_ACTION);
    //J, K, L in a row: bow, kick, knife. The knife has no rules behind it yet; the mapping is here
    //so the layout is decided once rather than argued about again when that slice lands.
    input->AddKeyMap('K',INPUT_ARCHER_KICK);
    input->AddKeyMap('L',INPUT_ARCHER_KNIFE);

    input->AddKeyMap('R',INPUT_ARCHER_RESTART);
    input->AddKeyMap(VK_F1,INPUT_ARCHER_TOGGLE_UI);
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature.
    input->AddKeyMap('P',INPUT_PAUSE);
}

void ApplicationArcher::RegisterCommandHandlers(){
    //Restarting throws the game away, which is only safe at the top of a tick on the physics
    //thread - which is exactly where a command handler runs.
    main_scene->RegisterCommandHandler(ARCHER_CMD_RESTART,
        [this](const SimCommand& cmd) -> objectid_t {
            (void)cmd;
            NewGame();
            return OBJECTID_INVALID;
        });

    /*
        Set the aim angle outright, in value[0], in degrees.

        A player tilts the aim over time with Up/Down and a script could do the same with HoldKey -
        but a script trying to land an arrow on a target wants to SOLVE for the angle, and
        bisecting on "hold up for 7 ticks" is a much worse instrument than setting 41.5 degrees and
        looking at where the arc says it lands. This is a measuring tool, and it is on the command
        queue rather than being a setter because the caller is on the wrong thread.
    */
    main_scene->RegisterCommandHandler(ARCHER_CMD_AIM,
        [this](const SimCommand& cmd) -> objectid_t {
            stage.aim_deg = clamp(cmd.value[0],BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
            return OBJECTID_INVALID;
        });

    /*
        Put the archer somewhere, in value[0]/value[1].

        A DEVELOPMENT TOOL and unapologetically so: the level is 84 units long with two gaps in it,
        and iterating on the ledge at x 44 should not require flying the whole approach by script
        every time - a run that spends thirty seconds getting there and then falls in a pit has
        measured nothing. It goes through the command queue like everything else, so it lands at a
        known point in a known tick and would be recorded by a replay rather than corrupting one.

        It clears the movement state as well as the position, which is the part worth stating: a
        teleport that keeps the old velocity, hang and climb-timer drops the archer into the new
        spot still hanging off a ledge that is now somewhere else entirely.
    */
    main_scene->RegisterCommandHandler(ARCHER_CMD_PLACE,
        [this](const SimCommand& cmd) -> objectid_t {
            stage.pos = v2(cmd.value[0],cmd.value[1]);
            stage.vel = v2(0.0f,0.0f);
            stage.mode = MODE_AIR;
            stage.hang_block = -1;
            stage.climb_ticks = 0;
            stage.grab_cooldown = 0;
            stage.f_on_ground = false;
            stage.coyote_ticks = 0;
            stage.buffer_ticks = 0;
            stage.bow_mode = BOW_IDLE;
            stage.draw_ticks = 0;
            return OBJECTID_INVALID;
        });
}

void ApplicationArcher::NewGame(){
    stage.Reset();
    //The level geometry is rebuilt from Stage every time, rather than being reset in place. It is
    //a few dozen boxes once per restart, and it means a change to BuildLevel cannot leave stale
    //geometry behind - which the prop bodies, with their accumulated velocities and tip-overs,
    //absolutely would.
    for (size_t i = 0; i < block_objects.size(); i++){
        if (block_objects[i]){
            block_objects[i]->Destroy();
        }
    }
    for (size_t i = 0; i < prop_views.size(); i++){
        if (prop_views[i].object){
            prop_views[i].object->Destroy();
        }
    }
    //And the rubble, or a restart leaves the last run's broken wall lying in the new one.
    for (size_t i = 0; i < debris.size(); i++){
        if (debris[i].object){
            debris[i].object->Destroy();
        }
    }
    debris.clear();
    //The rope's joints have to go before its bodies do, and both before BuildProps makes new ones.
    DestroyRope();
    main_scene->DeleteDestroyedObjects();
    BuildBlocks();
    BuildProps();

    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (p){
        p->SetBodyWorldPosition(vec3(stage.pos.x,stage.pos.y,0.0f));
        p->SetVelocity(vec3());
    }
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (arrow_objects[i]){
            arrow_objects[i]->SetVisibility(false);
        }
    }
    debug->Info("New game\n");
}

//--- Chrome -------------------------------------------------------------------------------------

void ApplicationArcher::UpdateView(void){
    if (!main_scene){
        return;
    }
    InputController* input = main_scene->inputcontroller;
    if (input->WasKeyReleased(INPUT_ARCHER_TOGGLE_UI)){
        f_show_engine_ui = !f_show_engine_ui;
        f_show_scene_window = f_show_engine_ui;
        f_show_inspector_window = f_show_engine_ui;
        f_show_engine_window = f_show_engine_ui;
    }
}

//--- The tick -----------------------------------------------------------------------------------

void ApplicationArcher::RunSimulationTick(void){
    if (!main_scene){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //Restart is read whether or not anything else is happening. Already on the physics thread
    //inside the tick, so this is a direct call rather than a command - a command would be a round
    //trip through the queue to arrive back here one tick later.
    if (input->WasKeyReleased(INPUT_ARCHER_RESTART)){
        NewGame();
        return;
    }

    ArcherInput intent;
    GatherInput(intent);

    //The props as boxes and the rope as points, BEFORE the tick - the archer is about to be
    //resolved against the one and offered the other.
    RefreshObstacles();
    RefreshRopePoints();
    //And while the solver is the one moving the archer, its answer is the truth: read it back
    //before the rules run on it.
    if (stage.mode == MODE_ROPE){
        SyncArcherFromRope();
    }

    StageEvents events;
    stage.Tick(intent,events);

    HandleEvents(events);
    //The rope handoff, in both directions. Immediately after the tick that decided it, so the
    //joint exists (or is gone) before anything else this tick reads the body.
    if (events.f_grabbed_rope){
        AttachArcherToRope(events.grabbed_rope_id);
    }
    if (events.f_released_rope){
        DetachArcherFromRope(events.f_rope_jump);
    }
    if (stage.mode == MODE_ROPE){
        PumpRope(intent.move_axis);
    }
    ApplyPushes(events);
    ApplyKicks(events);
    BreakBlocks(events);
    UpdateDebris();
    ResolveArrowsAgainstProps();
    DriveArcherBody();
    SyncArcherView();
    SyncArrowViews();
    SyncAimArc();
    UpdateTargets();
    ReapFallenProps();
    UpdateCamera();
    PublishSnapshot();
}

void ApplicationArcher::GatherInput(ArcherInput& out){
    InputController* input = main_scene->inputcontroller;

    /*
        THE EDGES ARE READ UNCONDITIONALLY, and only then is the result thrown away if the input is
        not ours to act on.

        WasKeyPressed/WasKeyReleased are one-shot flags that a read consumes. Returning early
        without reading them does not discard them - it DEFERS them, so a key released while the
        window was in the background fires on the tick focus comes back, which for the draw key
        means an arrow loosing itself several seconds after the player let go. Raw Input reports
        key-up unfocused (RIDEV_INPUTSINK), so this is not a hypothetical.
    */
    bool f_jump_pressed  = input->WasKeyPressed(INPUT_ARCHER_JUMP);
    bool f_draw_released = input->WasKeyReleased(INPUT_ARCHER_DRAW);
    bool f_action        = input->WasKeyPressed(INPUT_ARCHER_ACTION);
    bool f_kick          = input->WasKeyPressed(INPUT_ARCHER_KICK);

    //Act on input only when it is ours to act on: this window in front, or a scripted hold running
    //(which is not OS input, and happens precisely when the window is NOT in front). One predicate
    //owned by the engine - see InputController::IsInputLive.
    if (!input->IsInputLive()){
        out = ArcherInput();
        return;
    }

    float move = 0.0f;
    if (input->IsKeyDown(INPUT_ARCHER_LEFT)){   move -= 1.0f; }
    if (input->IsKeyDown(INPUT_ARCHER_RIGHT)){  move += 1.0f; }
    out.move_axis = clamp(move,-1.0f,1.0f);

    float aim = 0.0f;
    if (input->IsKeyDown(INPUT_ARCHER_AIM_UP)){   aim += 1.0f; }
    if (input->IsKeyDown(INPUT_ARCHER_AIM_DOWN)){ aim -= 1.0f; }
    out.aim_axis = clamp(aim,-1.0f,1.0f);

    out.f_jump_down     = input->IsKeyDown(INPUT_ARCHER_JUMP);
    out.f_jump_pressed  = f_jump_pressed;
    out.f_draw_down     = input->IsKeyDown(INPUT_ARCHER_DRAW);
    out.f_draw_released = f_draw_released;
    out.f_down_held     = input->IsKeyDown(INPUT_ARCHER_DOWN);
    out.f_action_pressed = f_action;
    out.f_kick_pressed  = f_kick;
}

void ApplicationArcher::HandleEvents(const StageEvents& events){
    //Sound and particles hang off here once there are any; for now the log is the feedback, and
    //only for the things worth a line. A landing every time the archer walks down a step would
    //drown the log that the MCP runs are read out of.
    if (events.f_grabbed_ledge){
        debug->Info("Caught a ledge at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_climbed){
        debug->Info("Climbed up onto the ledge, standing at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_kick_connected){
        debug->Info("Kick connected: %i props, %i blocks broken\n",
                    (int)events.kicks.size(),(int)events.broken_blocks.size());
    }
    if (events.f_shot){
        debug->Info("Shot at %.0f deg, power %.2f\n",stage.aim_deg,events.shot_power);
    }
    for (size_t i = 0; i < events.arrow_hits.size(); i++){
        const StageEvents::ArrowHit& h = events.arrow_hits[i];
        int kind = (h.block >= 0 && h.block < (int)stage.blocks.size()) ? stage.blocks[h.block].kind : -1;
        //A hit on the cracked wall is the one the kick-and-break slice will care about, so it is
        //worth naming now rather than being one more anonymous thud.
        debug->Info("Arrow %i hit block %i (%s) at (%.2f,%.2f) doing %.1f\n",
                    h.arrow,h.block,(kind == BLOCK_BREAKABLE) ? "breakable" : "solid",
                    h.point.x,h.point.y,h.speed);
    }
}

/*
    The other half of the arrow hit test: the half that knows about rigid bodies.

    Stage has already swept every arrow against the LEVEL and stuck the ones that hit it. What it
    cannot see is the props, because they are rp3d's and Stage names no engine type. So for every
    arrow still flying, the segment it covered this tick is handed to the solver as a raycast, and
    a hit is turned into a shove plus a stuck arrow.

    Order matters: this runs AFTER stage.Tick, so `prev_pos -> pos` is exactly the ground the arrow
    covered on the tick that just ran, and an arrow the rules already stuck in a wall is skipped
    before we ever ask.
*/
void ApplicationArcher::ResolveArrowsAgainstProps(){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world){
        return;
    }
    rp3d::RigidBody* exclude = archer_object ? archer_object->GetRigidBody() : NULL;

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        Arrow& a = stage.arrows[i];
        if (!a.f_live || a.f_stuck){
            continue;
        }
        vec3 from(a.prev_pos.x,a.prev_pos.y,0.0f);
        vec3 to(a.pos.x,a.pos.y,0.0f);
        if (from.x == to.x && from.y == to.y){
            continue;       //a degenerate segment is not a query rp3d can answer
        }

        PhysicsWorld::RaycastHit hit = world->Raycast(from,to,exclude);
        if (!hit.hit || !hit.body){
            continue;
        }
        //Object::AddPhysics stamps every body with its owning Object, so this cast is valid for
        //anything in this world.
        Object* struck = (Object*)hit.body->getUserData();
        if (!struck){
            continue;
        }

        PropView* view = NULL;
        for (size_t v = 0; v < prop_views.size(); v++){
            if (prop_views[v].object == struck){
                view = &prop_views[v];
                break;
            }
        }
        if (!view){
            //A level block. Stage owns those and has already had its say, so there is nothing to
            //do here - and doing something would stick the arrow twice, in two different places.
            continue;
        }

        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y);
        Physics* p = struck->GetPhysics();
        if (p && !p->IsStatic() && speed > 0.001f){
            /*
                A force applied for exactly one tick at the point of impact.

                Scaled by the struck body's own mass so that the velocity it picks up is
                ARROW_SPEED_TRANSFER of the arrow's speed WHATEVER the target weighs - which is
                what makes this one number tunable instead of needing a different one per prop.
                Applied at the hit POINT rather than at the centre, which is the whole reason a
                target board topples rather than sliding away flat: off-centre force is torque, and
                the solver works that out for free.

                rp3d clears external forces at the end of every step, so this is genuinely an
                impulse and not something that keeps pushing.
            */
            vec3 dir(a.vel.x / speed,a.vel.y / speed,0.0f);
            float dt = GetPhysicsTimestep();
            float force = (speed * arrow_speed_transfer * p->GetMass()) / dt;
            p->WakeUp();
            p->AddWorldForceAt(dir * force,hit.point);
        }

        //Stuck where it struck, in Stage, which keeps the arrow's position the rules' business
        //even though this answer came from the solver.
        stage.StickArrow(i,v2(hit.point.x,hit.point.y));

        if (view->kind == PROP_TARGET && !view->f_knocked){
            debug->Info("Arrow %i struck target at (%.2f,%.2f) doing %.1f\n",
                        i,hit.point.x,hit.point.y,speed);
        }
    }
}

/*
    The archer's kinematic body, driven to where Stage says the archer is.

    By VELOCITY, not by position, and that is the entire trick - see the note at the top of
    ApplicationArcher.h. A kinematic body is stopped by nothing, so integrating this velocity for
    one timestep lands it exactly on Stage's answer; but on the way there it has a real velocity
    for the solver to resolve crate contacts against, which a setTransform teleport does not.
    That is what makes kicking a crate cost no kicking code.
*/
void ApplicationArcher::DriveArcherBody(){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    //ON THE ROPE THE SOLVER IS DRIVING. Writing a velocity here as well would be the second
    //integrator this whole arrangement exists to avoid.
    if (stage.mode == MODE_ROPE){
        return;
    }
    vec3 now = p->GetBodyWorldPosition();
    vec3 want(stage.pos.x,stage.pos.y,0.0f);
    vec3 delta = want - now;

    /*
        A TELEPORT IS NOT A KICK.

        Falling off the level puts the archer back at the start, which as a velocity is several
        hundred units a second - enough to scatter every crate it passes on the way, one tick after
        the player stopped being anywhere near them. Anything that big is not movement, so it is
        applied as a position instead and the solver is never told about it.
    */
    float far_enough = ARCHER_RUN_SPEED * GetPhysicsTimestep() * 4.0f;
    if (delta.x * delta.x + delta.y * delta.y > far_enough * far_enough){
        p->SetBodyWorldPosition(want);
        p->SetVelocity(vec3());
        return;
    }
    p->SetVelocity(delta * (1.0f / GetPhysicsTimestep()));
}

/*
    Hand Stage every live prop as a plain box, before the tick.

    This is the whole of how "a crate blocks you" is wired, and the reason it is six numbers rather
    than a pointer: Stage names no engine type, so it cannot be given a body, a collider or an
    Object. It is given where the thing is and how big it is, plus an id it never interprets and
    hands straight back on a push event.

    REBUILT EVERY TICK, never kept in sync. These are rigid bodies - a crate shoved last tick is
    somewhere else now - and a stale box is an invisible wall standing where a crate used to be.
    Clearing and refilling a vector of a couple of dozen PODs costs nothing next to being wrong.

    A KNOCKED-OVER prop is deliberately left out. Its axis-aligned box stops describing it the
    moment it topples, and a board lying on the floor should be stepped over rather than walked
    into - leaving it out gets both right for free.
*/
/*
    The boot landing on a prop.

    Far harder than ApplyPushes, and that difference is the whole reason the two are separate
    events rather than one with a magnitude on it: a shove is ARCHER_PUSH_SPEED and moves a crate
    at walking pace, a kick is KICK_SPEED with KICK_LIFT under it and sends it. Set rather than
    added, like the shove, so it stays bounded.

    A BRICK IN A WALL IS THE INTERESTING CASE. The wall is built from static bodies - twenty-one
    dynamic boxes holding each other up is a lot of solver time spent keeping something perfectly
    still, and rp3d would have to be argued with to stop the stack slouching. So the bricks stand
    static until something frees them, and a kick frees THE WHOLE WALL at once, not just the bricks
    the boot touched. Freeing only those leaves the rest hanging in the air over the hole, which
    looks like a bug and is one; freeing all of them lets the wall come down, which is the thing
    the player asked for. The ones near the boot get the impulse, the rest simply lose their
    footing - which is what a wall collapsing IS.
*/
void ApplicationArcher::ApplyKicks(const StageEvents& events){
    for (size_t k = 0; k < events.kicks.size(); k++){
        const StageEvents::StageKick& kick = events.kicks[k];
        if (kick.id < 0 || kick.id >= (int)prop_views.size()){
            continue;
        }
        PropView& hit = prop_views[kick.id];
        if (!hit.object){
            continue;
        }

        //Which bodies this kick is about: a lone crate or target is itself, a brick is its whole
        //wall. `index` is the Stage prop it came from, which for every brick of one wall is the
        //same number - that is what makes the wall identifiable at all.
        bool f_wall = (hit.kind == PROP_BRICKWALL);
        for (size_t i = 0; i < prop_views.size(); i++){
            PropView& view = prop_views[i];
            if (!view.object || view.f_lost){
                continue;
            }
            if (f_wall){
                if (view.kind != PROP_BRICKWALL || view.index != hit.index){
                    continue;
                }
            }else if (i != (size_t)kick.id){
                continue;
            }

            Physics* p = view.object->GetPhysics();
            if (!p){
                continue;
            }
            if (p->IsStatic()){
                //Freed. Gravity has to be turned on explicitly - SetStatic(false) does not do it,
                //and a brick without it hangs in the air looking like a broken solver. See the
                //gravity note in MakePlanarBody.
                p->SetStatic(false);
                p->SetGravityEnabled(true);
                p->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
                p->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));
            }
            //A brick off a wall is rubble from here on: it stops being something the archer can
            //walk into, or the pile becomes the wall all over again.
            if (view.kind == PROP_BRICKWALL){
                view.f_broken = true;
            }

            //Impulse falls off with distance from the boot, so a wall bursts outward from where it
            //was struck rather than every brick leaving at the same speed in the same direction.
            vec3 pp = view.object->GetWorldPosition();
            float dx = pp.x - kick.x;
            float dy = pp.y - kick.y;
            float dist = sqrtf(dx * dx + dy * dy);
            //A gentle falloff on purpose: the bricks the boot actually touched are thrown, and
            //the rest of the wall still gets enough of a shove to come apart rather than settling
            //back into the same shape one row lower.
            float falloff = 1.0f / (1.0f + dist * dist * 0.30f);

            vec3 v = p->GetVelocity();
            float want = kick.dir * KICK_SPEED * falloff;
            if ((kick.dir > 0.0f && v.x < want) || (kick.dir < 0.0f && v.x > want)){
                v.x = want;
            }
            float lift = KICK_LIFT * falloff;
            if (v.y < lift){
                v.y = lift;
            }
            p->WakeUp();
            p->SetVelocity(vec3(v.x,v.y,0.0f));
        }
    }
}

/*
    A block the kick destroyed.

    Stage has already cleared its f_alive, so as far as the rules are concerned it is gone - the
    archer walks through where it stood and arrows fly through it. What is left is the half the
    rules cannot reach: a static collider still standing in the physics world, which crates and
    debris would pile against forever, and nothing on screen to say it broke.
*/
void ApplicationArcher::BreakBlocks(const StageEvents& events){
    for (size_t i = 0; i < events.broken_blocks.size(); i++){
        int index = events.broken_blocks[i];
        if (index < 0 || index >= (int)block_objects.size() || !block_objects[index]){
            continue;
        }
        Object* object = block_objects[index];
        vec3 centre = object->GetWorldPosition();
        vec3 size = object->GetScale();

        Physics* p = object->GetPhysics();
        if (p){
            //Deactivated rather than destroyed: the block objects are a vector indexed in step
            //with Stage::blocks, and deleting one out of the middle of that would put every index
            //after it out of alignment with the rules. It stops colliding, which is what matters.
            p->SetActive(false);
        }
        object->SetVisibility(false);

        const StageBlock& block = stage.blocks[index];
        //Away from the archer, because a wall you kicked should fall away from you.
        float dir = (stage.pos.x <= block.x) ? 1.0f : -1.0f;
        SpawnDebris(centre,vec3(size.x * 0.5f,size.y * 0.5f,size.z * 0.5f),
                    vec3(dir,0.35f,0.0f),material_breakable);
        debug->Info("Broke block %i at (%.2f,%.2f)\n",index,centre.x,centre.y);
    }
}

/*
    Burst one block into chunks.

    Deliberately irregular - the chunks are different sizes and leave at different speeds, because
    a block that shatters into identical cubes all travelling the same way reads as a formation
    rather than as rubble.

    THE RANDOMNESS IS LOCAL AND SEEDED FROM THE TICK, not rand() and not the engine's RRandom.
    rand() is not reproducible across runs, which would make a recorded session diverge the moment
    a wall came down. RRandom would be reproducible but is a SHARED stream - the UI and the MCP
    thread draw from the same one off-tick, so the number this gets depends on what else happened
    to ask for a number first, which is an open problem in its own right. A local xorshift seeded
    from the tick and the chunk index is reproducible, costs nothing, and cannot be perturbed by
    anything outside this function.
*/
void ApplicationArcher::SpawnDebris(const vec3& centre, const vec3& half_extents,
                                    const vec3& impulse_dir, int material){
    uint32_t seed = (uint32_t)(main_scene->GetPhysicsTick() * 2654435761u) ^ 0x9E3779B9u;
    for (int i = 0; i < ARCHER_DEBRIS_PER_BLOCK; i++){
        if ((int)debris.size() >= ARCHER_MAX_DEBRIS){
            return;     //the cap is the point; see the note on it
        }
        //xorshift32, inline so the stream belongs to this burst and to nothing else.
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fx = (float)(seed % 1000) / 1000.0f - 0.5f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fy = (float)(seed % 1000) / 1000.0f - 0.5f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float scale = 0.30f + (float)(seed % 1000) / 1000.0f * 0.35f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fspeed = (float)(seed % 1000) / 1000.0f;

        vec3 size(half_extents.x * 2.0f * scale,half_extents.y * 2.0f * scale * 0.6f,
                  half_extents.z * 2.0f * scale);
        vec3 at(centre.x + fx * half_extents.x * 1.2f,
                centre.y + fy * half_extents.y * 1.4f,
                0.0f);

        char name[48];
        snprintf(name,sizeof(name),"debris_%i_%i",(int)debris.size(),i);
        Object* chunk = MakePlanarBody(unit_mesh,name,at,size,material,
                                       ARCHER_CAT_DEBRIS,ARCHER_MASK_DEBRIS,1.2f,false);
        if (!chunk){
            continue;
        }
        Physics* p = chunk->GetPhysics();
        if (p){
            float speed = 3.0f + fspeed * 5.0f;
            p->SetVelocity(vec3(impulse_dir.x * speed,
                                impulse_dir.y * speed + 2.0f + fy * 3.0f,0.0f));
            p->SetAngularVelocity(vec3(0.0f,0.0f,fx * 12.0f));
        }
        DebrisView view;
        view.object = chunk;
        view.reap_tick = main_scene->GetPhysicsTick() + ARCHER_DEBRIS_TICKS;
        debris.push_back(view);
    }
}

void ApplicationArcher::UpdateDebris(){
    uint64_t now = main_scene->GetPhysicsTick();
    bool f_any_destroyed = false;
    for (size_t i = 0; i < debris.size(); ){
        Object* object = debris[i].object;
        bool f_expired = (now >= debris[i].reap_tick) ||
                         (object && object->GetWorldPosition().y < -40.0f);
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
        //Object::Destroy only MARKS. Without this the chunks stop rendering but their rigid bodies
        //stay in the physics world for the life of the run - and a player demolishing a wall makes
        //a lot of them. Safe here: RunSimulationTick holds physics_mutex, so the render thread is
        //not walking the object list. Same reasoning as breakout's UpdateDebris.
        main_scene->DeleteDestroyedObjects();
    }
}

/*
    The rope: a chain of light links hanging from the anchor, joined end to end.

    Built once, from the anchor prop's declared length. Each link is joined to the one above by a
    ball-and-socket at the point where they meet, and the top one to a STATIC anchor body - which is
    what makes the whole thing hang rather than fall.

    THE LINKS ARE NOT ALLOWED TO SLEEP. rp3d puts a body that has been still for a moment to sleep,
    and a hanging rope is still by definition - so without this the rope goes to sleep on the first
    frame and the archer swings into a bar of iron. apps/tank/CraneCharacter.cpp does the same for
    the same reason, and it is the single easiest thing to leave out.
*/
void ApplicationArcher::BuildRope(const StageProp& anchor){
    DestroyRope();
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world){
        return;
    }

    float seg_len = anchor.h / (float)ROPE_SEGMENTS;
    rp3d::RigidBody* previous = NULL;

    //A static body at the anchor point for the top link to hang from. Invisible - the visible bar
    //is the prop itself, built by the caller.
    Object* fixed = MakePlanarBody(unit_mesh,"rope_fixed",vec3(anchor.x,anchor.y,0.0f),
                                   vec3(0.12f,0.12f,0.12f),material_ledge,
                                   ARCHER_CAT_ROPE,ARCHER_MASK_ROPE,0.0f,true);
    if (fixed){
        fixed->SetVisibility(false);
        previous = fixed->GetRigidBody();
        rope_segments.push_back(fixed);     //index 0 is the fixed point, not a handhold
    }

    for (int i = 0; i < ROPE_SEGMENTS; i++){
        char name[32];
        snprintf(name,sizeof(name),"rope_%i",i);
        float cy = anchor.y - seg_len * ((float)i + 0.5f);
        Object* link = MakePlanarBody(unit_mesh,name,vec3(anchor.x,cy,0.0f),
                                      vec3(ROPE_SEGMENT_THICK,seg_len * 0.92f,ROPE_SEGMENT_THICK),
                                      material_ledge,ARCHER_CAT_ROPE,ARCHER_MASK_ROPE,
                                      ROPE_SEGMENT_MASS,false);
        if (!link){
            continue;
        }
        Physics* lp = link->GetPhysics();
        if (lp && lp->body && lp->body->rigidbody){
            //See the note above: a sleeping rope is a rigid rope.
            lp->body->rigidbody->setIsAllowedToSleep(false);
            //A little damping, or the rope keeps swinging for a minute after it is let go and
            //reads as being in space rather than on a windy cliff.
            lp->SetLinearDamping(0.12f);
            lp->SetAngularDamping(0.20f);
        }
        if (previous && link->GetRigidBody()){
            vec3 pivot(anchor.x,anchor.y - seg_len * (float)i,0.0f);
            rp3d::BallAndSocketJointInfo info(previous,link->GetRigidBody(),
                                              (rp3d::Vector3&)pivot);
            info.isCollisionEnabled = false;
            rp3d::BallAndSocketJoint* joint =
                dynamic_cast<rp3d::BallAndSocketJoint*>(world->rp_world->createJoint(info));
            if (joint){
                rope_joints.push_back(joint);
            }
        }
        previous = link->GetRigidBody();
        rope_segments.push_back(link);
    }
    debug->Info("Built a rope of %i links from (%.2f,%.2f)\n",
                (int)rope_segments.size() - 1,anchor.x,anchor.y);
}

void ApplicationArcher::DestroyRope(){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (world){
        //The archer's own joint first - it refers to a link that is about to stop existing.
        if (rope_joint){
            world->rp_world->destroyJoint(rope_joint);
            rope_joint = NULL;
        }
        for (size_t i = 0; i < rope_joints.size(); i++){
            world->rp_world->destroyJoint(rope_joints[i]);
        }
    }
    rope_joints.clear();
    for (size_t i = 0; i < rope_segments.size(); i++){
        if (rope_segments[i]){
            rope_segments[i]->Destroy();
        }
    }
    rope_segments.clear();
}

/*
    Which links the archer may catch, handed to Stage as plain numbers before the tick.

    The top ROPE_FIRST_GRABBABLE links are left out on purpose - see the note on that constant. The
    id is the index into rope_segments, which is exactly what comes back on the grab event.
*/
void ApplicationArcher::RefreshRopePoints(){
    stage.ClearRopePoints();
    for (size_t i = ROPE_FIRST_GRABBABLE; i < rope_segments.size(); i++){
        if (!rope_segments[i]){
            continue;
        }
        vec3 p = rope_segments[i]->GetWorldPosition();
        stage.AddRopePoint(p.x,p.y,(int)i);
    }
}

/*
    THE HANDOFF. This is the one moment in the app where the archer stops being the rules' and
    starts being the solver's, and every line of it is undoing an assumption made elsewhere:

      - the body has been KINEMATIC, moved by DriveArcherBody to wherever Stage said. It becomes
        DYNAMIC, and DriveArcherBody stands down for as long as MODE_ROPE lasts.
      - it has collided with NOTHING, because Stage was resolving the world by hand. Now nothing is,
        so it needs a real collision mask or the swing goes through the floor.
      - it arrives with the velocity Stage had, which is what makes catching a rope at a run throw
        you further than catching it standing still. That continuity is the whole feel of the
        mechanic and it is one line.
*/
void ApplicationArcher::AttachArcherToRope(int segment){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!world || !p || segment < 0 || segment >= (int)rope_segments.size()){
        return;
    }
    Object* link = rope_segments[segment];
    if (!link || !link->GetRigidBody()){
        return;
    }

    p->SetBodyWorldPosition(vec3(stage.pos.x,stage.pos.y,0.0f));
    p->SetBodyType(rp3d::BodyType::DYNAMIC);
    p->SetGravityEnabled(true);
    p->SetCollideWithMaskBits(ARCHER_MASK_ON_ROPE);
    p->SetVelocity(vec3(stage.vel.x,stage.vel.y,0.0f));
    p->WakeUp();

    //Joined at the LINK, so the archer hangs below it and the arc is the rope's rather than a
    //rigid offset from it.
    vec3 pivot = link->GetWorldPosition();
    rp3d::BallAndSocketJointInfo info(link->GetRigidBody(),archer_object->GetRigidBody(),
                                      (rp3d::Vector3&)pivot);
    info.isCollisionEnabled = false;
    rope_joint = dynamic_cast<rp3d::BallAndSocketJoint*>(world->rp_world->createJoint(info));
    debug->Info("Grabbed rope link %i at (%.2f,%.2f)\n",segment,pivot.x,pivot.y);
}

/*
    And back again. The velocity the solver built up is READ OUT and handed to Stage, which is the
    payoff of the whole mechanic - let go at the bottom of the arc and you keep the speed, let go at
    the top and you do not.
*/
void ApplicationArcher::DetachArcherFromRope(bool f_jump){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (world && rope_joint){
        world->rp_world->destroyJoint(rope_joint);
    }
    rope_joint = NULL;
    if (!p){
        return;
    }

    vec3 v = p->GetVelocity();
    vec3 at = p->GetBodyWorldPosition();
    //Letting go with JUMP adds height; letting go with action keeps only what the swing gave.
    float vy = v.y + (f_jump ? ROPE_JUMP_BOOST : 0.0f);

    p->SetBodyType(rp3d::BodyType::KINEMATIC);
    p->SetCollideWithMaskBits(ARCHER_MASK_ARCHER);
    p->SetVelocity(vec3());

    stage.pos = v2(at.x,at.y);
    stage.vel = v2(v.x,vy);
    debug->Info("Let go of the rope at (%.2f,%.2f) doing (%.2f,%.2f)%s\n",
                at.x,at.y,v.x,vy,f_jump ? " with a jump" : "");
}

//While swinging, the archer's position IS the body's. Read it back so the rules, the camera and
//every telemetry reader agree with what is on screen.
void ApplicationArcher::SyncArcherFromRope(){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    vec3 at = p->GetBodyWorldPosition();
    vec3 v = p->GetVelocity();
    stage.pos = v2(at.x,at.y);
    stage.vel = v2(v.x,v.y);
}

/*
    Pumping the swing.

    A force rather than a velocity, and that is the point: a rope you can steer by setting your
    speed is a rope with no timing in it. A force has to be applied in the right phase of the arc to
    build anything, which is the entire skill of a rope swing and costs one line to express.
*/
void ApplicationArcher::PumpRope(float move_axis){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    if (move_axis < 0.01f && move_axis > -0.01f){
        return;     //not leaning either way
    }
    p->WakeUp();
    p->AddWorldForceAt(vec3(move_axis * ROPE_PUMP_FORCE,0.0f,0.0f),
                       p->GetBodyWorldPosition());
}

void ApplicationArcher::RefreshObstacles(){
    stage.ClearObstacles();
    for (size_t i = 0; i < prop_views.size(); i++){
        const PropView& view = prop_views[i];
        //Rubble and toppled boards are stepped over, not walked into. See PropView::f_broken.
        if (!view.object || view.f_lost || view.f_knocked || view.f_broken){
            continue;
        }
        Physics* p = view.object->GetPhysics();
        //Static props - a brick in a standing wall - block without being shoved. That is what
        //makes the brick wall a wall until the kick slice frees the bricks it breaks.
        bool f_pushable = (p && !p->IsStatic());
        vec3 pp = view.object->GetWorldPosition();
        stage.AddObstacle(pp.x,pp.y,view.half_extents.x,view.half_extents.y,(int)i,f_pushable);
    }
}

/*
    Shove whatever the archer leaned on.

    Stage decides WHETHER something is being pushed, in which direction and how fast, because all
    three fall out of the sweep it already does; this decides what that means for a rigid body. The
    velocity is SET rather than added, so it is bounded by construction however many ticks the lean
    lasts - and the archer is capped to the same speed by the rules, so the two move together
    instead of the archer grinding through a crate it is outrunning.

    No upward component. This is a shove along the floor, not a kick; the crate should slide rather
    than hop. A kick is a separate verb with a key of its own, and belongs to the kick slice.
*/
void ApplicationArcher::ApplyPushes(const StageEvents& events){
    for (size_t i = 0; i < events.pushes.size(); i++){
        const StageEvents::StagePush& push = events.pushes[i];
        if (push.id < 0 || push.id >= (int)prop_views.size()){
            continue;
        }
        PropView& view = prop_views[push.id];
        Physics* p = view.object ? view.object->GetPhysics() : NULL;
        if (!p || p->IsStatic()){
            continue;
        }
        vec3 v = p->GetVelocity();
        float want = push.dir * push.speed;
        //Only if it would speed the prop up - a crate already sliding away faster than the archer
        //walks must not be slowed to their pace by the hand still resting on it.
        if ((push.dir > 0.0f && v.x < want) || (push.dir < 0.0f && v.x > want)){
            v.x = want;
        }
        p->WakeUp();
        p->SetVelocity(vec3(v.x,v.y,0.0f));
    }
}

void ApplicationArcher::SyncArcherView(){
    if (!archer_object){
        return;
    }
    bool f_on_wall = (stage.mode == MODE_HANG || stage.mode == MODE_CLIMB);
    archer_object->SetMaterialSlot(0,f_on_wall ? material_archer_hang : material_archer);
}

void ApplicationArcher::SyncArrowViews(){
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        Object* o = arrow_objects[i];
        if (!o){
            continue;
        }
        const Arrow& a = stage.arrows[i];
        o->SetVisibility(a.f_live);
        if (!a.f_live){
            continue;
        }
        o->SetPosition(vec3(a.pos.x,a.pos.y,0.0f));
        //The mesh runs along +X, so one rotation about Z aims it. A stuck arrow keeps the angle it
        //arrived at, which is why Stage stops updating `angle` once it sticks.
        o->SetRotation(quat(vec3(0.0f,0.0f,1.0f),a.angle));
    }
}

/*
    Cut the aim arc short at the first PROP it would hit.

    Stage::PredictArc already stops the arc at level geometry, and cannot do more than that: props
    are rigid bodies and the rules name no engine type. So without this the preview keeps the
    promise it makes about walls and breaks it about the one thing the player is usually aiming AT
    - the arc sails straight through a target board and lands somewhere behind it, while the arrow
    itself stops dead in the board, because ResolveArrowsAgainstProps asks the solver and the
    preview did not.

    So the preview asks too, over exactly the same segments, and the two halves of "what will this
    arrow hit" are now asked in the same two places for the preview as for the flight. Costs one
    raycast per bead, and only while the bow is drawn.

    Returns the new point count, with the last point moved to the point of impact.
*/
int ApplicationArcher::TruncateArcAgainstProps(v2* points, int count){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world || !points || count < 1){
        return count;
    }
    rp3d::RigidBody* exclude = archer_object ? archer_object->GetRigidBody() : NULL;

    v2 from = stage.MuzzlePosition();
    for (int i = 0; i < count; i++){
        v2 to = points[i];
        if (from.x == to.x && from.y == to.y){
            from = to;
            continue;
        }
        PhysicsWorld::RaycastHit hit = world->Raycast(vec3(from.x,from.y,0.0f),
                                                      vec3(to.x,to.y,0.0f),exclude);
        if (hit.hit){
            points[i] = v2(hit.point.x,hit.point.y);
            return i + 1;
        }
        from = to;
    }
    return count;
}

void ApplicationArcher::SyncAimArc(){
    bool f_drawing = (stage.bow_mode == BOW_DRAWING);
    if (!f_drawing){
        for (int i = 0; i < AIM_ARC_POINTS; i++){
            if (arc_objects[i]){
                arc_objects[i]->SetVisibility(false);
            }
        }
        return;
    }

    v2 points[AIM_ARC_POINTS];
    int n = stage.PredictArc(points,AIM_ARC_POINTS);
    n = TruncateArcAgainstProps(points,n);
    for (int i = 0; i < AIM_ARC_POINTS; i++){
        Object* o = arc_objects[i];
        if (!o){
            continue;
        }
        o->SetVisibility(i < n);
        if (i >= n){
            continue;
        }
        o->SetPosition(vec3(points[i].x,points[i].y,0.0f));
        //The last bead is where it stops - either where it hits, or the end of the preview. That
        //is the one the player is really reading, so it gets its own colour.
        o->SetMaterialSlot(0,(i == n - 1) ? material_dot_hot : material_dot);
        //Beads shrink along the arc, so which end is which is readable without following it.
        float t = 1.0f - (0.55f * (float)i / (float)AIM_ARC_POINTS);
        o->SetScale(vec3(t,t,t));
    }
}

void ApplicationArcher::UpdateTargets(){
    for (size_t i = 0; i < prop_views.size(); i++){
        PropView& view = prop_views[i];
        if (view.kind != PROP_TARGET || !view.object || view.f_knocked){
            continue;
        }
        //How far off vertical the board has tipped. GetWorldUp is the object's own up in world
        //space, so the dot against world up is the cosine of the tilt - no Euler angles, and no
        //question of which order they would have been in.
        vec3 up = view.object->GetWorldUp();
        float cos_tilt = clamp(up.y,-1.0f,1.0f);
        float tilt_deg = todegrees(acosf(cos_tilt));
        if (tilt_deg >= TARGET_KNOCKED_DEG){
            view.f_knocked = true;
            view.object->SetMaterialSlot(0,material_target_hit);
            debug->Info("Target %i knocked over (%.0f degrees)\n",view.index,tilt_deg);
        }
    }
}

/*
    Props that have left the level.

    A crate kicked into one of the gaps is gone, and gameplay-wise that is fine - what is not fine
    is that nothing stops it. There is no floor under the gaps, so it falls forever: a live rigid
    body accelerating toward infinity, reported to every telemetry reader as a target at y -658
    doing 156 units a second. That is not a physics fault, it is a missing reaper, and it is the
    kind of thing that looks like a physics fault for an hour.

    Deactivated rather than destroyed. Object::Destroy only MARKS, and the actual delete walks the
    scene's object list - which is not a thing to start doing from inside a tick. Deactivating
    takes the body out of the simulation immediately and safely, and the next restart rebuilds
    every prop from Stage anyway.
*/
void ApplicationArcher::ReapFallenProps(){
    for (size_t i = 0; i < prop_views.size(); i++){
        PropView& view = prop_views[i];
        if (view.f_lost || !view.object){
            continue;
        }
        //Well below the lowest thing anything is meant to stand on, so a prop resting at the
        //bottom of a pit - if one is ever added - would not be swept up by this.
        if (view.object->GetWorldPosition().y > -40.0f){
            continue;
        }
        Physics* p = view.object->GetPhysics();
        if (p){
            p->SetVelocity(vec3());
            p->SetActive(false);
        }
        view.object->SetVisibility(false);
        view.f_lost = true;
        debug->Info("Prop %s fell out of the level and was retired\n",view.object->name.c_str());
    }
}

void ApplicationArcher::UpdateCamera(){
    /*
        A trailing camera with lead.

        Welding the camera to the archer makes a fast platformer unreadable - the world slides
        under a character who never moves, and the eye has nothing to track. So the camera aims at
        a point AHEAD of the archer in the direction they are travelling and eases toward it, which
        both shows more of where they are going and lets the character move within the frame.

        The lead follows the VELOCITY, not the facing: turning round while drawing a bow should not
        swing the camera across the level.
    */
    float lead = 0.0f;
    if (stage.vel.x > 0.5f || stage.vel.x < -0.5f){
        lead = (stage.vel.x / ARCHER_RUN_SPEED) * CAMERA_LEAD;
    }
    camera_ideal = vec3(stage.pos.x + lead,stage.pos.y + 2.0f,0.0f);

    camera_target.x += (camera_ideal.x - camera_target.x) * CAMERA_SMOOTH;
    camera_target.y += (camera_ideal.y - camera_target.y) * CAMERA_SMOOTH;
    camera_target.z = 0.0f;

    Camera* camera = main_scene->camera;
    if (camera){
        camera->SetPosition(vec3(camera_target.x,camera_target.y + CAMERA_HEIGHT,CAMERA_DISTANCE));
        camera->SetLookAt(camera_target);
        camera->CalculateLookatMatrix();
    }

    //Drag the sun along with the view. The level is 84 units wide and the shadow ortho is 22, so a
    //sun fixed at the origin would leave everything past the first screen unshadowed - and the
    //fault would look like broken shadows rather than like a light pointed somewhere else.
    //Held as a pointer rather than looked up by name: this runs every tick, and Scene::FindObject
    //is a walk of the whole tree comparing strings.
    if (sun_light){
        sun_light->SetPosition(vec3(camera_target.x - 9.0f,camera_target.y + 20.0f,10.0f));
        sun_light->SetLookAt(vec3(camera_target.x,camera_target.y,0.0f));
    }
}

void ApplicationArcher::PublishSnapshot(){
    ArcherSnapshot s;
    s.tick = main_scene->GetPhysicsTick();
    s.stage_ticks = stage.ticks;
    s.x = stage.pos.x;
    s.y = stage.pos.y;
    s.vx = stage.vel.x;
    s.vy = stage.vel.y;
    s.facing = stage.facing;
    s.mode = stage.mode;
    s.f_on_ground = stage.f_on_ground;
    s.coyote_ticks = stage.coyote_ticks;
    s.bow_mode = stage.bow_mode;
    s.draw_ticks = stage.draw_ticks;
    s.draw_power = stage.DrawPower();
    s.aim_deg = stage.aim_deg;
    s.live_arrows = stage.NumLiveArrows();
    s.arrows_shot = stage.arrows_shot;
    s.arrows_hit_blocks = stage.arrows_hit_blocks;
    s.f_paused = main_scene->IsPhysicsPaused();

    for (size_t i = 0; i < prop_views.size(); i++){
        const PropView& view = prop_views[i];
        if (view.kind != PROP_TARGET || !view.object || view.f_lost){
            continue;
        }
        ArcherSnapshot::TargetView t;
        vec3 p = view.object->GetWorldPosition();
        t.x = p.x;
        t.y = p.y;
        vec3 up = view.object->GetWorldUp();
        t.tilt_deg = todegrees(acosf(clamp(up.y,-1.0f,1.0f)));
        t.f_knocked = view.f_knocked;
        s.targets.push_back(t);
    }

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        const Arrow& a = stage.arrows[i];
        if (!a.f_live){
            continue;
        }
        ArcherSnapshot::ArrowView av;
        av.x = a.pos.x;
        av.y = a.pos.y;
        av.vx = a.vel.x;
        av.vy = a.vel.y;
        av.f_stuck = a.f_stuck;
        s.arrows.push_back(av);
    }

    /*
        Where a shot loosed right now would end up.

        The single most useful number for a program trying to hit something: with it, a script can
        solve for the angle by bisection - set an aim, read the landing point, adjust - instead of
        loosing an arrow and waiting to see. Costs one run of the same integrator the arrow uses.
    */
    v2 arc[AIM_ARC_POINTS];
    int n = stage.PredictArc(arc,AIM_ARC_POINTS);
    //The same truncation the on-screen arc gets, so a script reading this number and a player
    //reading the beads are told the same thing.
    n = TruncateArcAgainstProps(arc,n);
    if (n > 0){
        s.predicted_x = arc[n - 1].x;
        s.predicted_y = arc[n - 1].y;
        s.f_predicted = true;
    }

    std::lock_guard<std::mutex> lock(snapshot_mutex);
    snapshot = s;
}

//--- MCP ----------------------------------------------------------------------------------------
#ifdef USE_MCP

void ApplicationArcher::WaitTicks(int ticks){
    if (ticks <= 0){
        return;
    }
    uint64_t target = main_scene->GetPhysicsTick() + (uint64_t)ticks;
    for (int waited = 0; waited < 8000 && main_scene->GetPhysicsTick() < target; waited += 4){
        Sleep(4);
    }
}

json ApplicationArcher::BuildStateJson(){
    ArcherSnapshot s;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        s = snapshot;
    }

    static const char* mode_names[] = { "ground","air","hang","climb","rope" };

    json targets = json::array();
    for (size_t i = 0; i < s.targets.size(); i++){
        targets.push_back(json{
            {"x",s.targets[i].x},
            {"y",s.targets[i].y},
            {"tilt_deg",s.targets[i].tilt_deg},
            {"knocked",s.targets[i].f_knocked}
        });
    }
    json arrows = json::array();
    for (size_t i = 0; i < s.arrows.size(); i++){
        arrows.push_back(json{
            {"x",s.arrows[i].x},
            {"y",s.arrows[i].y},
            {"vx",s.arrows[i].vx},
            {"vy",s.arrows[i].vy},
            {"stuck",s.arrows[i].f_stuck}
        });
    }

    json result = json{
        {"tick",s.tick},
        {"stage_ticks",s.stage_ticks},
        {"archer",json{
            {"x",s.x},{"y",s.y},{"vx",s.vx},{"vy",s.vy},
            {"facing",(s.facing > 0.0f) ? "right" : "left"},
            {"mode",mode_names[(s.mode >= 0 && s.mode <= MODE_ROPE) ? s.mode : 0]},
            {"on_ground",s.f_on_ground},
            {"coyote_ticks",s.coyote_ticks}
        }},
        {"bow",json{
            {"drawing",s.bow_mode == BOW_DRAWING},
            {"draw_ticks",s.draw_ticks},
            {"draw_ticks_full",BOW_DRAW_TICKS},
            {"draw_power",s.draw_power},
            {"aim_deg",s.aim_deg},
            {"predicted_landing",s.f_predicted ? json{{"x",s.predicted_x},{"y",s.predicted_y}}
                                               : json(nullptr)}
        }},
        {"arrows_shot",s.arrows_shot},
        {"arrows_in_blocks",s.arrows_hit_blocks},
        {"live_arrows",arrows},
        {"targets",targets},
        {"paused",s.f_paused}
    };
    return result;
}

void ApplicationArcher::RegisterMCPTools(){
    //Registered from Init(). The server only starts accepting requests after Init() returns, so
    //registration can never race a client's tools/list.

    MCPServer::Get()->RegisterTool("archer_state",
        "The whole game state: where the archer is and what they are doing, the bow's draw and aim, "
        "every live arrow, every target and whether it has been knocked over, and - the useful one - "
        "where an arrow loosed RIGHT NOW would land, under 'bow.predicted_landing'. That last field "
        "is computed with the same integrator the arrow flies on, so a script can solve for an aim "
        "angle by bisection instead of shooting and looking. Read from a snapshot the physics thread "
        "publishes at the end of every tick, so it never disturbs the game it is measuring. The "
        "level runs from x -12 to 72 with the ground surface at y 0; the game runs at 60 ticks a "
        "second and every duration is a tick count.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the current frame, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_run",
        "Hold left or right for a number of SIMULATION TICKS and block until it has played out. "
        "This is how a program plays: the hold emits ordinary input events, so the simulation "
        "cannot tell it from a key. The archer accelerates over about 6 ticks and tops out at 9 "
        "units a second, so a short hold nudges and a long one sprints. While the simulation is "
        "paused the hold does not count down - use sim_step.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"description","'left' or 'right'"}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold, default 30, capped at 600"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string dir = args.value("direction",std::string("right"));
            int ticks = (int)clamp(args.value("ticks",30.0f),0.0f,600.0f);
            uint32_t action = (dir == "left") ? INPUT_ARCHER_LEFT : INPUT_ARCHER_RIGHT;
            input->HoldKey(action,(uint32_t)ticks);
            WaitTicks(ticks + 2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_jump",
        "Jump, holding the key for a number of ticks. THE HOLD LENGTH IS THE JUMP HEIGHT: releasing "
        "early cuts the climb, so 3 ticks is a hop and 25 ticks is the full 3.2-unit jump. Combine "
        "with archer_run to jump while moving - a running jump clears about 6.5 units of gap, which "
        "is what the two gaps in this level are built around.",
        json{
            {"type","object"},
            {"properties", {
                {"hold_ticks", {{"type","number"},{"description","ticks to hold jump, default 25 (a full jump), capped at 120"}}},
                {"run", {{"type","string"},{"description","optionally run 'left' or 'right' for the whole jump"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            int hold = (int)clamp(args.value("hold_ticks",25.0f),1.0f,120.0f);
            std::string run = args.value("run",std::string(""));
            //The run is held for the whole flight, not just the launch: air control is 0.6 of
            //ground control, so letting go mid-jump lands noticeably shorter.
            int flight = hold + 50;
            if (run == "left"){
                input->HoldKey(INPUT_ARCHER_LEFT,(uint32_t)flight);
            }else if (run == "right"){
                input->HoldKey(INPUT_ARCHER_RIGHT,(uint32_t)flight);
            }
            input->HoldKey(INPUT_ARCHER_JUMP,(uint32_t)hold);
            WaitTicks(flight + 2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_aim",
        "Point the bow at an angle, in degrees, RELATIVE TO THE WAY THE ARCHER IS FACING: 0 is "
        "straight ahead, positive is up, and the range is -85 to +85. Facing left mirrors it, so "
        "the same angle means the same shot in both directions. Takes effect at the top of the next "
        "tick. Read 'bow.predicted_landing' back from archer_state to see where that angle puts an "
        "arrow before spending one.",
        json{
            {"type","object"},
            {"properties", {
                {"degrees", {{"type","number"},{"description","-85 .. +85, relative to facing; + is up"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"degrees"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_AIM;
            cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
            main_scene->SubmitCommand(cmd);
            WaitTicks(2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_draw",
        "Start drawing the bow and RETURN IMMEDIATELY, without waiting for the loose. The draw runs "
        "for hold_ticks and then looses on its own, so this is how to catch the game mid-draw - "
        "with the aim arc on screen - for a screenshot. Every other tool here blocks until its "
        "action has played out, which is right for measuring and useless for posing.",
        json{
            {"type","object"},
            {"properties", {
                {"hold_ticks", {{"type","number"},{"description","ticks to hold the draw before it looses, default 240 (4 seconds), capped at 600"}}},
                {"degrees", {{"type","number"},{"description","optional: set the aim angle first, -85 .. +85"}}},
                {"settle_ticks", {{"type","number"},{"description","ticks to wait before returning, so the arc is drawn, default 8"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input || !main_scene){
                return json{ {"error","no input controller"} };
            }
            if (args.contains("degrees")){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_AIM;
                cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
                main_scene->SubmitCommand(cmd);
                WaitTicks(2);
            }
            int hold = (int)clamp(args.value("hold_ticks",240.0f),1.0f,600.0f);
            int settle = (int)clamp(args.value("settle_ticks",8.0f),0.0f,120.0f);
            input->HoldKey(INPUT_ARCHER_DRAW,(uint32_t)hold);
            //Just long enough for the draw to start and SyncAimArc to place the beads - NOT for
            //the hold to finish, which is the whole point of this tool.
            WaitTicks(settle);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_shoot",
        "Draw the bow for a number of ticks and loose. A full draw is 36 ticks and gives an arrow "
        "46 units a second; a 1-tick tap gives the minimum 23.5. Optionally sets the aim first, so "
        "one call is one complete, measurable shot. Returns once the arrow is away - poll "
        "archer_state, or pass wait_ticks, to see where it ended up. Arrows fly under their own "
        "gravity (24, against the archer's 42) and are swept against the level, so a fast arrow "
        "cannot pass through a thin wall.",
        json{
            {"type","object"},
            {"properties", {
                {"draw_ticks", {{"type","number"},{"description","ticks to hold the draw, default 36 (full), capped at 120"}}},
                {"degrees", {{"type","number"},{"description","optional: set the aim angle first, -85 .. +85"}}},
                {"wait_ticks", {{"type","number"},{"description","extra ticks to let the arrow fly before reporting, default 60"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input || !main_scene){
                return json{ {"error","no input controller"} };
            }
            if (args.contains("degrees")){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_AIM;
                cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
                main_scene->SubmitCommand(cmd);
                WaitTicks(2);
            }
            int draw = (int)clamp(args.value("draw_ticks",(float)BOW_DRAW_TICKS),1.0f,120.0f);
            int fly  = (int)clamp(args.value("wait_ticks",60.0f),0.0f,900.0f);
            input->HoldKey(INPUT_ARCHER_DRAW,(uint32_t)draw);
            //The release is read with WasKeyReleased, which only becomes true on the tick AFTER
            //the hold ends - so the wait has to clear the hold plus that edge before the arrow
            //even exists. Since backlog item 84 these are stepped ticks too, so this works under
            //sim_step as well as free-running.
            WaitTicks(draw + 3 + fly);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_hold",
        "Hold any one control down for a number of SIMULATION TICKS and block until it has played "
        "out. The general form of archer_run and archer_jump, and the way to reach the controls "
        "that have no tool of their own: 'down' drops through a one-way platform and lets go of a "
        "ledge, 'action' and 'knife' are wired but not yet used. Several of these can be layered by "
        "calling with wait false and then holding the next one. Actions: left, right, down, jump, "
        "draw, action, knife.",
        json{
            {"type","object"},
            {"properties", {
                {"action", {{"type","string"},{"description","left, right, down, jump, draw, kick, action or knife"}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold it, default 20, capped at 600"}}},
                {"wait", {{"type","boolean"},{"description","block until the hold has finished, default true; false returns at once so another hold can be layered on top"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"action"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string name = args.value("action",std::string(""));
            uint32_t action = 0;
            if (name == "left"){        action = INPUT_ARCHER_LEFT;   }
            else if (name == "right"){  action = INPUT_ARCHER_RIGHT;  }
            else if (name == "down"){   action = INPUT_ARCHER_DOWN;   }
            else if (name == "jump"){   action = INPUT_ARCHER_JUMP;   }
            else if (name == "draw"){   action = INPUT_ARCHER_DRAW;   }
            else if (name == "kick"){   action = INPUT_ARCHER_KICK;   }
            else if (name == "action"){ action = INPUT_ARCHER_ACTION; }
            else if (name == "knife"){  action = INPUT_ARCHER_KNIFE;  }
            else{
                return json{ {"error","unknown action '" + name + "'; expected left, right, down, jump, draw, kick, action or knife"} };
            }
            int ticks = (int)clamp(args.value("ticks",20.0f),0.0f,600.0f);
            input->HoldKey(action,(uint32_t)ticks);
            if (args.value("wait",true)){
                //Plus two, so the RELEASE edge has been read by a tick as well as the hold - an
                //action read with WasKeyReleased is not delivered until then.
                WaitTicks(ticks + 2);
            }
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_place",
        "Put the archer at (x, y) and clear their movement state. A DEVELOPMENT TOOL: the level "
        "runs from x -12 to 72 with two gaps in it, and iterating on one part of it should not mean "
        "flying the whole approach by script every time. Useful landmarks: the ground surface is "
        "y 0, the start is (-6, 0.9), the grabbable-only ledge stands at x 44..48 with its lip at "
        "4.2 (jump from x 43.1 to catch it), the cracked wall is at x 57 and the brick wall at "
        "x 49.5. y is the archer's CENTRE, so standing on the ground is y 0.9. DO NOT PLACE INSIDE "
        "SOLID GEOMETRY: the archer is ejected out of it on the next tick, and out of a tall block "
        "that means upward onto its roof - which looks like the placement having worked and then "
        "the archer walking over things it should have been stopped by. x 44 is inside the ledge; "
        "43.1 is beside it.",
        json{
            {"type","object"},
            {"properties", {
                {"x", {{"type","number"},{"description","world x"}}},
                {"y", {{"type","number"},{"description","world y of the archer's centre; 0.9 stands on the ground"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"x"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_PLACE;
            cmd.value[0] = args.value("x",0.0f);
            cmd.value[1] = args.value("y",0.9f);
            main_scene->SubmitCommand(cmd);
            WaitTicks(3);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_restart",
        "Rebuild the level and put the archer back at the start. Everything the props have "
        "accumulated - kicked crates, toppled targets, embedded arrows - is thrown away and rebuilt "
        "from archer/Stage's BuildLevel, so this is the way to get a clean measurement.",
        json{ {"type","object"}, {"properties",json::object()} },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_RESTART;
            main_scene->SubmitCommand(cmd);
            WaitTicks(3);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });
}
#endif

//--- The debug panel ----------------------------------------------------------------------------
#ifdef USE_IMGUI

void ApplicationArcher::DrawImGuiUI(void){
    Application::DrawImGuiUI();

    //Runs on the RENDER thread with physics_mutex held, so the live Stage can be read directly.
    //It must never wait on the physics thread - see the threading note in ApplicationArcher.h.
    ImGui::Begin("Archer");

    ImGui::Text("%s",stage.DebugLine().c_str());
    ImGui::Separator();

    ImGui::Text("mode      %s%s",
                (stage.mode == MODE_GROUND) ? "ground" :
                (stage.mode == MODE_AIR) ? "air" :
                (stage.mode == MODE_HANG) ? "hang" :
                (stage.mode == MODE_CLIMB) ? "climb" : "rope",
                stage.f_on_ground ? "" : "  (airborne)");
    ImGui::Text("coyote    %i    buffer %i",stage.coyote_ticks,stage.buffer_ticks);
    ImGui::Text("velocity  %.2f, %.2f",stage.vel.x,stage.vel.y);

    ImGui::Separator();
    ImGui::Text("aim       %.0f deg %s",stage.aim_deg,(stage.facing > 0.0f) ? "right" : "left");
    ImGui::ProgressBar((float)stage.draw_ticks / (float)BOW_DRAW_TICKS,ImVec2(-1,0),"draw");
    ImGui::Text("arrows    %i live, %i shot, %i in walls",
                stage.NumLiveArrows(),stage.arrows_shot,stage.arrows_hit_blocks);

    ImGui::Separator();
    //The one number worth a slider: how much of an arrow's speed its target takes. Everything else
    //in the feel lives in Stage.h, where it belongs and where the rules test can check it - this
    //one is the app's, because it is about rigid bodies the rules never see.
    ImGui::SliderFloat("arrow punch",&arrow_speed_transfer,0.0f,0.25f,"%.3f");

    int knocked = 0;
    for (size_t i = 0; i < prop_views.size(); i++){
        if (prop_views[i].kind == PROP_TARGET && prop_views[i].f_knocked){
            knocked++;
        }
    }
    ImGui::Text("targets   %i knocked over",knocked);

    if (ImGui::Button("Restart")){
        //From the render thread, so it goes on the queue rather than being called here.
        SimCommand cmd;
        cmd.type = ARCHER_CMD_RESTART;
        SubmitUICommand(cmd);
    }

    ImGui::Separator();
    ImGui::TextWrapped("A/D or arrows run.  Space jumps - hold it for height.  J draws the bow, "
                       "release to loose; Up/Down tilt the aim.  E catches the rope over the second gap - lean "
                       "into the swing to build it, then let go with E to keep the speed or with "
                       "Space to add height.  K kicks: it punts a crate far "
                       "harder than walking into one does, and brings down the brick wall or the "
                       "cracked wall.  Jump at a ledge too high to land on and you CATCH it: Space "
                       "then climbs up, S lets go, and holding away from it refuses the grab.  "
                       "S also drops through a platform.  R restarts, F1 shows the engine panels.");

    ImGui::End();
}
#endif
