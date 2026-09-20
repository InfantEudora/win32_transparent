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
        { "ar_crate",       vec4(0.68f,0.52f,0.30f,1.0f), 0.06f, &material_crate },
        { "ar_target",      vec4(0.90f,0.90f,0.88f,1.0f), 0.10f, &material_target },
        //A struck target goes green, so a hit is legible in a screenshot with no HUD at all -
        //which is exactly how this app gets checked over MCP.
        { "ar_target_hit",  vec4(0.30f,0.85f,0.40f,1.0f), 0.45f, &material_target_hit },
        { "ar_arrow",       vec4(0.95f,0.88f,0.55f,1.0f), 0.30f, &material_arrow }
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
    input->AddKeyMap('K',INPUT_ARCHER_KNIFE);

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

    StageEvents events;
    stage.Tick(intent,events);

    HandleEvents(events);
    ResolveArrowsAgainstProps();
    DriveArcherBody();
    KickProps();
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
}

void ApplicationArcher::HandleEvents(const StageEvents& events){
    //Sound and particles hang off here once there are any; for now the log is the feedback, and
    //only for the things worth a line. A landing every time the archer walks down a step would
    //drown the log that the MCP runs are read out of.
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
    The archer's shove on the props.

    WHY THE ARCHER PUSHES PROPS HERE RATHER THAN THROUGH THE SOLVER. Stage resolves the archer
    against the LEVEL and knows nothing about props, so the archer walks clean through a crate
    rather than being stopped by it. A kinematic character controller normally gets its pushing
    for free precisely because it IS stopped by what it pushes, keeping the overlap shallow; with
    the overlap instead lasting as long as it takes to walk the prop's width, handing that to the
    solver means asking it to resolve a deep overlap against infinite mass every one of those
    ticks. Doing it here instead is bounded by construction: the push SETS a velocity rather than
    adding a force, so it cannot accumulate over those ticks.

    IN FAIRNESS TO THE SOLVER, it was blamed for a while for something that was not its fault -
    the props had no gravity and so no friction, and nothing that was ever touched came to rest.
    See the gravity note in MakePlanarBody. Now that they behave, solver-driven pushing is worth
    re-testing when the kick slice lands; this function is not the only possible answer, it is the
    controllable one.

    This is also the seed of the kick-and-break slice, which wants a deliberate kick on a key with
    a bigger number, aimed at the brick wall.
*/
void ApplicationArcher::KickProps(){
    //A prop resting against someone standing still stays put; a shove is something you do by
    //moving into it.
    float speed = (stage.vel.x < 0.0f) ? -stage.vel.x : stage.vel.x;
    float dir = (stage.vel.x > 0.0f) ? 1.0f : -1.0f;
    /*
        ON THE GROUND ONLY. The archer's jump arc passes clean through anything standing at head
        height - the target board on the ledge is at exactly the height of the apex - and punting
        it across the level while sailing over it is not a kick, it is a bug that looks like one.
        Measured: a board at x 31 ended up against the far wall at x 72 having been jumped through.
    */
    bool f_may_kick = stage.f_on_ground && (speed >= ARCHER_KICK_MIN_SPEED);
    //Scaled by how fast the archer is actually going, so walking nudges and running shoves.
    float push = ARCHER_KICK_SPEED * clamp(speed / ARCHER_RUN_SPEED,0.0f,1.0f);

    for (size_t i = 0; i < prop_views.size(); i++){
        PropView& view = prop_views[i];
        if (!view.object){
            continue;
        }
        Physics* p = view.object->GetPhysics();
        if (!p || p->IsStatic()){
            continue;       //a brick in a standing wall is static until the kick slice frees it
        }

        vec3 pp = view.object->GetWorldPosition();
        //Axis-aligned overlap of the archer's body box against the prop's. Approximate for a
        //toppled prop, which is fine - see the note on PropView::half_extents.
        bool f_inside = true;
        if (pp.x + view.half_extents.x < stage.pos.x - ARCHER_HALF_W){ f_inside = false; }
        if (pp.x - view.half_extents.x > stage.pos.x + ARCHER_HALF_W){ f_inside = false; }
        if (pp.y + view.half_extents.y < stage.pos.y - ARCHER_HALF_H){ f_inside = false; }
        if (pp.y - view.half_extents.y > stage.pos.y + ARCHER_HALF_H){ f_inside = false; }

        /*
            ONE KICK PER CONTACT, on the leading edge of the overlap.

            Kicking on every tick the overlap lasts is what a shove looks like if you write the
            obvious thing, and it is wrong twice over: the archer walks THROUGH a prop rather than
            being stopped by it, so the overlap lasts as long as it takes to walk the prop's width,
            and re-applying the lift every one of those ticks holds the prop in the air travelling
            alongside the archer instead of letting it fall away. Measured: crates and boards
            dribbled tens of units down the level that way.

            An edge means a prop takes one impulse and then falls out of the overlap on its own,
            which is what a kick is.
        */
        if (!f_inside){
            view.f_kick_contact = false;
            continue;
        }
        bool f_new_contact = !view.f_kick_contact;
        view.f_kick_contact = true;
        if (!f_new_contact || !f_may_kick){
            continue;
        }

        //Only ever pushed AWAY from the archer, so a prop the archer has already walked past is
        //not dragged back through them.
        float away = (pp.x >= stage.pos.x) ? 1.0f : -1.0f;
        if (away != dir){
            continue;
        }
        vec3 v = p->GetVelocity();
        //Set, not add. And only if this would speed it up - a crate already flying away from a
        //kick must not be slowed down to the walking pace of the archer chasing it.
        float want = push * dir;
        if ((dir > 0.0f && v.x < want) || (dir < 0.0f && v.x > want)){
            v.x = want;
        }
        if (v.y < ARCHER_KICK_LIFT){
            v.y = ARCHER_KICK_LIFT;
        }
        p->WakeUp();
        p->SetVelocity(vec3(v.x,v.y,0.0f));
    }
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
    ImGui::TextWrapped("A/D or arrows run.  S drops through a platform.  Space jumps - hold it for "
                       "height.  J draws the bow, release to loose.  Up/Down tilt the aim.  "
                       "R restarts, F1 shows the engine panels.");

    ImGui::End();
}
#endif
