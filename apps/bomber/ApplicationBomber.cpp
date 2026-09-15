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

//Skeleton, for the enemies' skinned meshes. Only BuildEnemies needs the type - everything after it
//talks to them through Object - so it is included here rather than in the header.
#include "Skeleton.h"

#include <math.h>
#include <stdio.h>
#include <string.h>     //strcmp, for comparing the clip that is playing against the one wanted

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

    A CELL WITH A BLOCK ON IT GETS A FLOOR TOO: wall_brick is only 0.81 deep against a 1.0 cell
    and wall_hedge only 0.45, so without a floor under them you would see through the gap to the
    void. The floor under every block type is the GRASS tile, and that is not a shortcut - it is
    what lets a hedge burn away without anything having to swap the floor underneath it. See the
    floor-variety loop at the end of Maze::NewGame, which skips soft blocks to keep it true.
*/
static const char* BOMBER_TILE_ASSET[MAZE_TILE_COUNT] = {
    "tile_grass",   //MAZE_TILE_GRASS
    "tile_brick",   //MAZE_TILE_BRICK
    "tile_rock",    //MAZE_TILE_ROCK
    "tile_water",   //MAZE_TILE_WATER
    "tile_grass",   //MAZE_TILE_WALL  - the floor under the block
    "tile_grass",   //MAZE_TILE_HEDGE - and what is left standing when it burns
    "tile_grass",   //MAZE_TILE_WOOD  - the same
};
/*
    What STANDS on a cell, by the type of the tile it is standing on. NULL means the tile is floor
    and nothing stands there.

    A table rather than the `if (t == MAZE_TILE_WALL)` the field builder used to carry, because
    there are three block types now and a fourth would otherwise be a fourth branch.
*/
static const char* BOMBER_BLOCK_ASSET[MAZE_TILE_COUNT] = {
    NULL,           //MAZE_TILE_GRASS
    NULL,           //MAZE_TILE_BRICK
    NULL,           //MAZE_TILE_ROCK
    NULL,           //MAZE_TILE_WATER
    "wall_brick",   //MAZE_TILE_WALL
    "wall_hedge",   //MAZE_TILE_HEDGE
    "wall_wood",    //MAZE_TILE_WOOD
};
static const char* BOMBER_DECOR_ASSET[MAZE_DECOR_COUNT] = {
    NULL,               //MAZE_DECOR_NONE
    "grass_flowers",
    "flowers",
    "grass_plant",
    "turd",
    "lilly",            //MAZE_DECOR_LILLY - the only one that goes on water
    "bridge",
};
//Ordered by MazeItem, so the rows after the first two are coin, diamond, crystal - the score ladder
//in the order Maze.h writes it.
static const char* BOMBER_ITEM_ASSET[MAZE_ITEM_COUNT] = {
    NULL,               //MAZE_ITEM_NONE
    "pickup_health",
    "pickup_shield",
    "pickup_coin",
    "pickup_diamond",
    "pickup_crystal",
};
/*
    Which pickups turn on the spot.

    The two flat ones. A crystal is faceted enough to read as an object standing still and a health
    or shield pickup is a thing on the floor, but a coin seen edge-on is a line - it has to turn to
    say what it is. Indexed by MazeItem so adding a treasure is a row here rather than a condition
    somewhere in the field builder.
*/
static const bool BOMBER_ITEM_SPINS[MAZE_ITEM_COUNT] = {
    false,              //MAZE_ITEM_NONE
    false,              //health
    false,              //shield
    true,               //coin
    true,               //diamond
    false,              //crystal
};
/*
    Which way the bridge model's planks run at each yaw.

    The asset is very nearly square in plan (0.89 x 0.94), so which of the two is "along the planks"
    cannot be read off its bounding box - it has to be looked at once and then written down. These
    two are that, and flipping them is the whole fix if a re-export turns the model.
*/
#define BOMBER_BRIDGE_YAW_X     (TYPE_PI * 0.5f)    //planks run east-west
#define BOMBER_BRIDGE_YAW_Z     0.0f                //planks run north-south

/*
    Which way a THIN block model lies at yaw 0.

    wall_hedge measures 1.00 x 0.45 in plan and wall_wood 0.96 x 0.27 - they are panels, not cubes,
    and a panel turned at random reads as rubble rather than as a hedge. So a run of them takes its
    angle from its NEIGHBOURS, which is the same trick the bridges use one screen down: the long
    axis lies along whichever way the run goes, and a row lines up into one hedge. wall_brick is
    very nearly square (0.99 x 0.81) and keeps the random quarter-turn instead.
*/
#define BOMBER_PANEL_YAW_X      0.0f                //long axis east-west: the model's rest pose
#define BOMBER_PANEL_YAW_Z      (TYPE_PI * 0.5f)    //long axis north-south

#define BOMBER_BOMB_ASSET   "bomb"
#define BOMBER_CHAR_ASSET   "character"
#define BOMBER_ENEMY_ASSET  "enemy"
/*
    The enemy's skin and its two clips, by their names IN THE FILE.

    GLTFLoader::GetSkeleton is looked up by SKIN name, not by node name - GetSkeletonNames returns
    model.skins, which is a different list from the nodes everything else here is named by. So the
    enemy is "enemy" to GetMeshFromNode and "enermy_armature" to GetSkeleton. The spelling is the
    artist's and is deliberately copied rather than corrected: it is a lookup key into the asset,
    and a tidier constant that does not match the file finds nothing.

    The clips are named rather than taken from GetAnimationNames() so that a clip added to the file
    for the player does not get loaded onto the enemy's three bones - the same reason LoadAssets
    names its nodes instead of calling GetAllAssetsFromGLTF.
*/
#define BOMBER_ENEMY_SKIN   "enemy_armature"
/*
    The turd's rig and its one clip - flies buzzing round it.

    THIS IS THE MULTI-ROOT RIG. `turd_armature` has three joints, `Base`, `Flies` and a
    `neutral_bone` the Blender exporter adds by itself, and none of them is a child of another.
    Until GLTFLoader::GetSkeleton learned to load every root, it loaded only `Base` - which is not
    the animated one - so this is the asset that pays for that fix.

    A POOL, not one per cell: turds are scattered by the generator, and a board has two on average
    and eight at the most over two thousand seeds. Twelve is comfortably clear of that, and a cell
    past the end of the pool falls back to the static mesh rather than going missing.
*/
#define BOMBER_TURD_SKIN    "turd_armature"
#define BOMBER_ANIM_TURD    "Turd_Idle"
#define BOMBER_MAX_TURDS    12
#define BOMBER_ANIM_WALK    "Enemy_Walking"
#define BOMBER_ANIM_CHOP    "Enemy_Chopping"
//Plays ONCE and holds its last frame - see where it is loaded. The only non-looping clip here.
#define BOMBER_ANIM_DEATH   "Enemy_Death"

/*
    --- the exit door, and why it is two assets ---------------------------------------------------

    `wall_doorway` is the frame and `door` is the leaf that swings in it. They are separate nodes in
    the .glb, parented there, and both sit at the same origin with no offset between them.

    THAT SPLIT IS WHAT MAKES THE CLIP WORK AT ALL. An animation track writes an ABSOLUTE local
    transform onto its target - Animation::ApplyIntervalOnto does a plain SetPosition/SetRotation -
    so a clip on a single door object would overwrite the placement this app gives it and drag the
    door to wherever the cell is in Blender. With the leaf a CHILD, its local transform is relative
    to the archway, so the clip owns it outright and the archway keeps the cell.

    It also buys the hinge. The leaf's origin is not on its hinge edge, so `Door_Opening` swings it
    97 degrees AND slides it by (-0.33, 0, -0.31) to keep the hinge still - a rotation-only clip
    would spin the door about its middle. Both halves are needed and both are in the file.

    Note what is NOT needed: no armature, no skin, no bones. GLTFLoader::LoadAnimation builds one
    track per target NODE and does not care whether that node is a joint, and Object::AddAnimation
    links tracks by name against the object and its children. A rigid prop animates through exactly
    the same path a character does, minus the skin.
*/
#define BOMBER_DOOR_ARCH_ASSET  "wall_doorway"
/*
    The leaf's node name, and it is a LOOKUP KEY, not a label: Animation::LinkObjects binds the
    clip's one track by comparing it against object names, so the leaf object has to be called
    exactly this or the clip plays and nothing moves.
*/
#define BOMBER_DOOR_LEAF_ASSET  "door"
#define BOMBER_ANIM_DOOR        "Door_Opening"
/*
    Where it stands, until the rules place it.

    The middle of the north border, so it reads as the way OUT of the board rather than as a piece
    of furniture on it. RebuildField leaves the block off this cell - see the note there.
*/
#define BOMBER_DOOR_X           (MAZE_W / 2)
#define BOMBER_DOOR_Z           0

//--- the pickups ----------------------------------------------------------------------------------
/*
    How long a taken pickup takes to shrink away, in ticks.

    Under a third of a second. Long enough to be seen and to say WHICH cell paid out, which is the
    only job it has - a pickup that vanished on the frame it was touched left the player wondering
    whether they had got it. Much longer and it is still shrinking while you walk off the tile,
    which reads as a second object rather than as the one you took.
*/
#define BOMBER_ITEM_SHRINK_TICKS    18
//Ticks for one full turn of a coin. Four seconds - slow enough to be scenery rather than a beacon.
#define BOMBER_ITEM_SPIN_TICKS      240
//The shield the player wears. Parented to the character - see the note on shield_worn.
#define BOMBER_SHIELD_ASSET         "equipped_shield"

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
    /*
        The enemy is a SKINNED mesh, and a skinned mesh has nowhere to be drawn without this.

        Renderer::skinned_shader is NULL by default and an app that forgets it gets no warning -
        the enemy simply does not appear, which reads as a failed asset load rather than as a
        missing shader. Only the fragment half is shared with default_shader; the vertex half is
        the one that knows about bone matrices.
    */
    renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");

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
    BuildEnemies();
    BuildTurds();
    BuildDoor();
    BuildBlastNoise();
    //BEFORE BuildExplosion, because custom shaders draw in registration order and the water is
    //opaque while the blast is not - see BuildWater.
    BuildWater();
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
                      "wall_brick","wall_hedge","wall_wood",
                      "grass_flowers","flowers","grass_plant","turd","bridge","lilly",
                      "pickup_health","pickup_shield",
                      "pickup_coin","pickup_diamond","pickup_crystal",BOMBER_SHIELD_ASSET,
                      BOMBER_DOOR_ARCH_ASSET,BOMBER_DOOR_LEAF_ASSET,
                      BOMBER_BOMB_ASSET,BOMBER_CHAR_ASSET);
    /*
        THE ENEMY IS NOT IN THAT LIST, and that is the whole difference between it and everything
        else on the board. GetAssetsFromGLTF builds one Object per node and hands out copies that
        SHARE its mesh, which is exactly right for four hundred tiles and exactly wrong for a
        skinned character: a pose lives in the bones, the bones are the object's children, and four
        enemies sharing one set would be one enemy drawn four times. BuildEnemies gives each its own
        skeleton, its own bones and its own copies of the clips.
    */

    //The ones that move, so their height does not have to be looked up every tick.
    character_y = gltfloader.GetNodePosition(BOMBER_CHAR_ASSET).y;
    bomb_y      = gltfloader.GetNodePosition(BOMBER_BOMB_ASSET).y;
    enemy_y     = gltfloader.GetNodePosition(BOMBER_ENEMY_ASSET).y;
    turd_y      = gltfloader.GetNodePosition(BOMBER_DECOR_ASSET[MAZE_DECOR_TURD]).y;
}

/*
    One skinned skeleton, with its clips loaded onto it.

    RENDER THREAD ONLY - GetMeshFromNode uploads a mesh. Everything that uses these calls it from
    Init and then only shows, hides and poses them; RebuildField runs on the PHYSICS thread and must
    never build one.

    EVERY ACTOR GETS ITS OWN COPY OF EVERY CLIP, which is why this returns one skeleton and is
    called in a loop rather than building a shared thing. Object::AddAnimation binds a clip to the
    bones of the skeleton it is added to, so a shared clip would drive whichever skeleton linked it
    last and leave the rest standing still.

    Every clip is loaded LOOPING. Callers that want a one-shot clear the flag afterwards - there is
    one such clip (the enemy's death) and making the exception the caller's business keeps this
    function from needing to know what any of them mean.
*/
Skeleton* ApplicationBomber::BuildSkinnedActor(const char* skin_name, const char* node_name,
                                               const char* const* clip_names, int num_clips,
                                               bool f_report){
    Skeleton* skeleton = gltfloader.GetSkeleton(skin_name,assetmanager);
    if (!skeleton){
        debug->Err("No skin called %s in bomber_assets.glb\n",skin_name);
        return NULL;
    }

    std::vector<Material> loaded_materials;
    Mesh* skinned_mesh = gltfloader.GetMeshFromNode(node_name,&loaded_materials,true);
    if (!skinned_mesh){
        debug->Err("No skinned mesh on node %s\n",node_name);
        return NULL;
    }
    skeleton->SetMesh(skinned_mesh);
    skeleton->TakeMaterialNames(loaded_materials);
    skeleton->PickMaterials(loaded_materials,renderer->materials);

    for (int i = 0; i < num_clips; i++){
        Animation* animation = gltfloader.LoadAnimation(clip_names[i]);
        if (!animation){
            debug->Err("No animation called %s in bomber_assets.glb\n",clip_names[i]);
            continue;
        }
        /*
            NONE OF THESE CLIPS MOVES THE OBJECT, which is why the extract flags are left alone.

            Where a thing is standing is a RULE here - Maze steps the enemies tile to tile and
            scatters the decor - so root motion pulled out of a hip track would fight the grid and
            drag the model off it. Both flags default to false; this is a note about why they are
            not touched rather than a line of code. See Animation::extract_horizontal_root_motion.
        */
        animation->looped = true;
        skeleton->AddAnimation(animation);

        /*
            Did every track in the clip find a bone to drive?

            Reported for the first actor of each kind, because A CLIP THAT DRIVES NOTHING LOOKS
            EXACTLY LIKE A CLIP THAT IS PLAYING: Object::ApplyAnimation skips tracks whose target is
            NULL, silently, so the clip is "playing", its time index advances, and nothing moves.

            An unbound track means that bone is not in the skeleton. That used to happen whenever a
            rig's joints were siblings rather than one chain - GetSkeleton walked down from
            joints[0] and took only what hung off it - and it is what the turd's Base/Flies/
            neutral_bone rig ran into. GetSkeleton loads every root now, so this should stay quiet;
            it is kept because it is the only thing that would say so if it ever stopped being true.
        */
        if (f_report){
            std::string unbound;
            for (ObjectAnimation* track:animation->object_animations){
                if (!track->target){
                    unbound += (unbound.empty() ? "" : ", ") + track->target_name;
                }
            }
            if (unbound.empty()){
                debug->Ok("Clip %-16s %i/%i track(s) bound, %.2fs\n",clip_names[i],
                          (int)animation->object_animations.size(),
                          (int)animation->object_animations.size(),animation->duration);
            }else{
                debug->Warn("Clip %s drives nothing on [%s] - those bones are not in the "
                            "skeleton %s\n",clip_names[i],unbound.c_str(),skin_name);
            }
        }
    }
    return skeleton;
}

/*
    One skeleton per enemy the board can ever hold, built once and then only shown, hidden and posed.

    MAZE_MAX_ENEMIES of them regardless of how many a given field places, because that is the most it
    can ever need and a spare costs one hidden object.
*/
void ApplicationBomber::BuildEnemies(void){
    static const char* CLIPS[] = {BOMBER_ANIM_WALK,BOMBER_ANIM_CHOP,BOMBER_ANIM_DEATH};

    for (int i = 0; i < MAZE_MAX_ENEMIES; i++){
        Skeleton* skeleton = BuildSkinnedActor(BOMBER_ENEMY_SKIN,BOMBER_ENEMY_ASSET,
                                               CLIPS,(int)(sizeof(CLIPS)/sizeof(CLIPS[0])),
                                               i == 0);
        if (!skeleton){
            return;
        }
        char name[32];
        snprintf(name,sizeof(name),"Enemy %i",i);
        skeleton->name = name;

        /*
            DYING IS AN EVENT, NOT A STATE - the one clip here that does not loop.

            A looping death is a body repeatedly getting back up to fall over again. A non-looping
            clip stops on its last frame instead, which is the pose a corpse should hold for the
            rest of MAZE_DEATH_TICKS.
        */
        Animation* death = skeleton->FindAnimation(BOMBER_ANIM_DEATH);
        if (death){
            death->looped = false;
        }

        //Something has to be playing or ApplyAnimation has nothing to pose, and the skeleton would
        //stand in its bind pose looking like the animation failed to load.
        skeleton->SwitchToAnimation(BOMBER_ANIM_WALK);
        skeleton->SetVisibility(false);

        main_scene->AddObject(skeleton);
        enemy_objects.push_back(skeleton);
    }

    Skeleton* first = enemy_objects.empty() ? NULL : dynamic_cast<Skeleton*>(enemy_objects[0]);
    debug->Ok("Built %i enemy skeletons, %i bone(s) each\n",(int)enemy_objects.size(),
              first ? first->num_bones : 0);
}

/*
    A pool of animated turds, buzzing.

    Decoration, and the only reason it is a pool rather than one per cell is that the generator
    scatters them: two on an average board, eight at the most over two thousand seeds, so
    BOMBER_MAX_TURDS covers it with room over. RebuildField hands them out to the cells that want
    one and hides the rest; a cell past the end of the pool falls back to the static mesh.

    This is the asset that multi-root rigs were fixed for - see BOMBER_TURD_SKIN.
*/
void ApplicationBomber::BuildTurds(void){
    static const char* CLIPS[] = {BOMBER_ANIM_TURD};

    for (int i = 0; i < BOMBER_MAX_TURDS; i++){
        Skeleton* skeleton = BuildSkinnedActor(BOMBER_TURD_SKIN,BOMBER_DECOR_ASSET[MAZE_DECOR_TURD],
                                               CLIPS,1,i == 0);
        if (!skeleton){
            return;
        }
        char name[32];
        snprintf(name,sizeof(name),"Turd %i",i);
        skeleton->name = name;

        /*
            They all start at a DIFFERENT point in the clip.

            Four turds buzzing in lockstep reads as one animation played four times, which is worse
            than not animating them at all. The offset comes from the index rather than a random
            draw so it survives a restart unchanged and cannot touch the simulation's stream.
        */
        skeleton->SwitchToAnimation(BOMBER_ANIM_TURD);
        //AFTER SwitchToAnimation, which rewinds the clip it starts. Each skeleton owns its own copy
        //of the clip - BuildSkinnedActor loads one per actor - so this offsets only this one.
        Animation* idle = skeleton->FindAnimation(BOMBER_ANIM_TURD);
        if (idle){
            idle->time_index = idle->duration * ((float)i / (float)BOMBER_MAX_TURDS);
        }
        skeleton->SetVisibility(false);

        main_scene->AddObject(skeleton);
        turd_objects.push_back(skeleton);
    }

    Skeleton* first = turd_objects.empty() ? NULL : dynamic_cast<Skeleton*>(turd_objects[0]);
    debug->Ok("Built %i turd skeletons, %i bone(s) each\n",(int)turd_objects.size(),
              first ? first->num_bones : 0);
}

/*
    The exit: an archway on its cell with a door leaf swinging under it.

    RENDER THREAD, ONCE. Not part of RebuildField and not in `field_objects`, so a restart lays out
    a new board around a door that keeps standing - and keeps whatever state it was left in, which
    is what you want while looking at the animation.

    --- THE CLIP GOES ON THE ARCHWAY, NOT ON THE LEAF ---------------------------------------------
    Which looks backwards, since the leaf is the thing that moves. Two reasons, and they are the
    same two that make a Skeleton work the way it does:

      - Scene::UpdateAnimations walks `renderer->objects`, which holds what was handed to
        Scene::AddObject. A child is DRAWN through its parent (Renderer::CullObjects recurses) but
        it is not in that list, so ApplyAnimation would never run on the leaf. The archway is in it.
      - Object::AddAnimation calls Animation::LinkObjects(this), which resolves each track against
        this object AND its children by name. The clip's one track is named `door`, so it binds to
        the leaf from up here.

    So the archway is the animated object and the leaf is what the animation moves, exactly as a
    Skeleton is the animated object and its bones are what move.
*/
void ApplicationBomber::BuildDoor(void){
    door_arch = assetmanager->GetObjectFromAsset(BOMBER_DOOR_ARCH_ASSET);
    Object* leaf = assetmanager->GetObjectFromAsset(BOMBER_DOOR_LEAF_ASSET);
    if (!door_arch || !leaf){
        debug->Err("The door needs both %s and %s in bomber_assets.glb\n",
                   BOMBER_DOOR_ARCH_ASSET,BOMBER_DOOR_LEAF_ASSET);
        door_arch = NULL;
        return;
    }
    door_arch->name = "Doorway";
    //NOT a display name: LinkObjects matches the clip's track against it. See BOMBER_DOOR_LEAF_ASSET.
    leaf->name = BOMBER_DOOR_LEAF_ASSET;
    /*
        No transform on the leaf. The two nodes share an origin in the .glb, so identity is already
        the shut pose, and from here on the clip owns this transform completely - anything set on it
        would be overwritten on the first frame that plays.
    */
    door_arch->AttachChild(leaf);

    //A panel lying east-west, like the hedges and the wooden walls - it stands IN the north border,
    //so its long axis runs along that wall. See BOMBER_PANEL_YAW_X.
    door_arch->SetPosition(CellCentre(BOMBER_DOOR_X,BOMBER_DOOR_Z));
    door_arch->SetRotation(quat(vec3(0,1,0),BOMBER_PANEL_YAW_X));

    Animation* opening = gltfloader.LoadAnimation(BOMBER_ANIM_DOOR);
    if (!opening){
        debug->Err("No animation called %s in bomber_assets.glb\n",BOMBER_ANIM_DOOR);
    }else{
        /*
            A DOOR IS AN EVENT, like the enemy's death and unlike everything else animated here: it
            swings once and stays where it got to. Object::ApplyAnimation drops a non-looping clip
            into ANIMATION_STATE_PAUSED on its last frame, which is precisely "open and staying
            open" - a looped door would slam and re-open forever.
        */
        opening->looped = false;
        door_arch->AddAnimation(opening);
        //The same unbound-track report BuildSkinnedActor does, and for the same reason: a track
        //that found nothing to drive looks exactly like a clip that is playing.
        for (ObjectAnimation* track:opening->object_animations){
            if (!track->target){
                debug->Warn("Clip %s drives nothing on [%s] - no object of that name under %s\n",
                            BOMBER_ANIM_DOOR,track->target_name.c_str(),door_arch->name.c_str());
            }
        }
        debug->Ok("Clip %-16s %i track(s), %.2fs\n",BOMBER_ANIM_DOOR,
                  (int)opening->object_animations.size(),opening->duration);
    }

    main_scene->AddObject(door_arch);
    /*
        Start the clip, then ask for it shut.

        SwitchToAnimation is the only rewind in the door's whole life: after this the clip is never
        restarted, only run one way or the other. The first tick takes it to 0 and pauses it there,
        posing the leaf on the way - so the shut door at startup is the same state a shut door ends
        in, rather than a second arrangement that has to be kept in step with the first.
    */
    door_arch->SwitchToAnimation(BOMBER_ANIM_DOOR);
    SetDoorOpen(false);
    debug->Ok("Door at tile (%i,%i)\n",BOMBER_DOOR_X,BOMBER_DOOR_Z);
}

/*
    Swings it, or swings it back.

    PHYSICS THREAD ONLY - from the BOMBER_CMD_DOOR handler, and from BuildDoor before anything is
    running.

    ONE CLIP, RUN BOTH WAYS. There is a single `Door_Opening` in the file and shutting is it at -1,
    which is the whole of Object::SetAnimationRate's reason for existing. Nothing rewinds, so
    pressing shut halfway through the swing reverses from halfway through rather than snapping to
    the far end - which is also what a door hit by a second thought actually does.
*/
void ApplicationBomber::SetDoorOpen(bool f_open){
    if (!door_arch){
        return;
    }
    f_door_open = f_open;
    door_arch->SetAnimationRate(f_open ? 1.0f : -1.0f);
}

float ApplicationBomber::CellYaw(int cx, int cz) const {
    uint32_t h = (uint32_t)(cx * 73856093) ^ (uint32_t)(cz * 19349663);
    return (float)(h & 3) * (TYPE_PI * 0.5f);
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
    object->SetRotation(quat(vec3(0,1,0),f_random_yaw ? CellYaw(cx,cz) : yaw));

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
    /*
        THE ENEMIES ARE NOT THROWN AWAY HERE. They are skinned skeletons built once on the render
        thread by BuildEnemies, and rebuilding them would mean uploading a mesh from the physics
        thread - see the note there. A restart only changes how many of them a field uses, which
        SyncView expresses by hiding the spare ones.
    */
    //The per-cell index points into what was just thrown away, so it is cleared with it. Missing
    //this would leave RefreshCells calling SetVisibility on freed objects the first time a block
    //burned on the new field, which is a crash one restart later than the mistake.
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            cell_block[z][x] = NULL;
            cell_item[z][x] = NULL;
        }
    }
    if (character){
        //The worn shield is its CHILD, so ~Object takes it - see the note on shield_worn. Dropping
        //the pointer here is the whole of this side of its lifetime.
        character->Destroy();
        character = NULL;
        shield_worn = NULL;
    }
    if (bomb){
        bomb->Destroy();
        bomb = NULL;
    }
    renderer->DeleteDestroyedObjects();

    //How many of the pooled turds this field has used. Handed out in board order - see the decor
    //block below - and the unused tail is hidden once the walk is done.
    int num_turds_placed = 0;

    //The pickups' view state, which belongs to the field being built and not to the one that just
    //went. spin_items holds objects from `field_objects`, which the loop above has already emptied.
    spin_items.clear();
    num_item_shrinking = 0;
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            cell_item_shrink[z][x] = 0;
        }
    }

    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            uint8_t t = maze.tile[z][x] < MAZE_TILE_COUNT ? maze.tile[z][x] : 0;
            //The floor. Every cell gets one, blocks included - see the note on BOMBER_TILE_ASSET.
            const char* tile_asset = BOMBER_TILE_ASSET[t];
            float tile_y = gltfloader.GetNodePosition(tile_asset).y;
            //Floor tiles are turned at random too. They are square and the texture is not, so four
            //orientations is four times as much board for nothing.
            AddCellObject(tile_asset,x,z,tile_y,0.0f,true);

            /*
                The block standing on it, if any.

                A thin panel takes its angle from its neighbours so a run reads as one hedge rather
                than as a pile - see BOMBER_PANEL_YAW_X. The test is on which ASSET it is and not on
                whether the tile is destructible, because being a panel is a fact about the MODEL:
                re-export wall_brick as a thin one and this is the line that should change.
            */
            const char* block_asset = BOMBER_BLOCK_ASSET[t];
            /*
                Except where the door stands: the archway IS the wall on that cell, and a brick
                block in the same place would stand inside it.

                A special case in the VIEW, and the only one on this board - which is exactly why
                it is temporary. When the exit becomes a rule it will be a tile type in Maze like
                every other block, BOMBER_BLOCK_ASSET will have a row for it, and this goes away.
            */
            if (x == BOMBER_DOOR_X && z == BOMBER_DOOR_Z){
                block_asset = NULL;
            }
            if (block_asset){
                bool f_panel = (t != MAZE_TILE_WALL);
                float yaw = BOMBER_PANEL_YAW_X;
                if (f_panel){
                    bool f_ew = (x > 0 && maze.tile[z][x - 1] == t) ||
                                (x < MAZE_W - 1 && maze.tile[z][x + 1] == t);
                    bool f_ns = (z > 0 && maze.tile[z - 1][x] == t) ||
                                (z < MAZE_H - 1 && maze.tile[z + 1][x] == t);
                    //A lone block has neither and falls through to the rest pose, which is as good
                    //an answer as any and is at least the same one every time.
                    yaw = (f_ns && !f_ew) ? BOMBER_PANEL_YAW_Z : BOMBER_PANEL_YAW_X;
                }
                cell_block[z][x] = AddCellObject(block_asset,x,z,
                                                 gltfloader.GetNodePosition(block_asset).y,
                                                 yaw,!f_panel);
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
                /*
                    A TURD TAKES ONE OF THE POOLED, ANIMATED SKELETONS instead of a copy of the
                    static mesh - it is the one piece of decoration that moves.

                    Positioned here rather than added: these are built once on the render thread and
                    this runs on the physics thread, so all it may do is place one and show it.
                    Which turd lands on which cell changes every restart, which is fine - nothing
                    reads the index - and a board with more turds than the pool falls back to the
                    static mesh rather than leaving a cell empty.
                */
                if (d == MAZE_DECOR_TURD && num_turds_placed < (int)turd_objects.size()){
                    Object* object = turd_objects[num_turds_placed++];
                    object->SetPosition(CellCentre(x,z) + vec3(0.0f,turd_y,0.0f));
                    object->SetRotation(quat(vec3(0,1,0),CellYaw(x,z)));
                    object->SetVisibility(true);
                }else{
                    bool f_yaw = (d != MAZE_DECOR_BRIDGE);
                    float yaw = (maze.pass_axis[z][x] == MAZE_AXIS_X) ? BOMBER_BRIDGE_YAW_X
                                                                      : BOMBER_BRIDGE_YAW_Z;
                    AddCellObject(BOMBER_DECOR_ASSET[d],x,z,
                                  gltfloader.GetNodePosition(BOMBER_DECOR_ASSET[d]).y,yaw,f_yaw);
                }
            }

            /*
                The pickup, built now and hidden under whatever is on top of it.

                Built up front rather than spawned when the block above it burns, because spawning
                would mean a Scene::AddObject from the middle of a tick - the one thing that call
                warns about - to save four objects on a board of four hundred.
            */
            uint8_t it = maze.item[z][x];
            if (it != MAZE_ITEM_NONE && it < MAZE_ITEM_COUNT && BOMBER_ITEM_ASSET[it]){
                cell_item[z][x] = AddCellObject(BOMBER_ITEM_ASSET[it],x,z,
                                                gltfloader.GetNodePosition(BOMBER_ITEM_ASSET[it]).y,
                                                0.0f,false);
                if (cell_item[z][x]){
                    /*
                        HIDDEN UNTIL RefreshCells SAYS OTHERWISE, and that is not tidiness.

                        RefreshCells starts a shrink when it finds a pickup that is VISIBLE and that
                        the rules no longer have. Every item here is buried, so leaving them at the
                        Object default of visible would make the first refresh - which runs at the
                        bottom of this function - read them all as just taken and shrink the whole
                        board's worth away.
                    */
                    cell_item[z][x]->SetVisibility(false);
                    if (BOMBER_ITEM_SPINS[it]){
                        spin_items.push_back(cell_item[z][x]);
                    }
                }
            }
        }
    }

    //The two that move. Not in field_objects: they outlive a rebuild conceptually, and keeping
    //them separate is what stops the loop above destroying the thing the player is.
    character = assetmanager->GetObjectFromAsset(BOMBER_CHAR_ASSET);
    if (character){
        character->name = "Character";
        /*
            The shield goes on as a CHILD with no transform of its own.

            The artist placed `equipped_shield` on top of `character` in the .glb - both nodes sit
            at the same origin - so identity here is exactly where it was drawn. Attaching rather
            than positioning it every tick means it follows and turns with the player for free, and
            it is freed with them: see the note on shield_worn.
        */
        shield_worn = assetmanager->GetObjectFromAsset(BOMBER_SHIELD_ASSET);
        if (shield_worn){
            shield_worn->name = "Shield";
            shield_worn->SetVisibility(false);
            character->AttachChild(shield_worn);
        }
        main_scene->AddObject(character);
    }
    bomb = assetmanager->GetObjectFromAsset(BOMBER_BOMB_ASSET);
    if (bomb){
        bomb->name = "Bomb";
        bomb->SetVisibility(false);
        main_scene->AddObject(bomb);
    }
    //The turds this field did not need. Hidden rather than moved away, so a pool entry never ends
    //up standing on a cell that no longer has one.
    for (int i = num_turds_placed; i < (int)turd_objects.size(); i++){
        turd_objects[i]->SetVisibility(false);
    }

    //Nothing has been drawn yet, so whatever the maze's version is, this view is not at it. The
    //wrap when field_version is 0 does not matter: the comparison is for difference, not order.
    drawn_field_version = maze.field_version - 1;
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
    /*
        BOTH MODES DRAW AT REDUCED RESOLUTION, and they have to, not just for the frame rate:
        blast_pixel_scale is an art setting as much as a cost one, and a comparison between a
        pixelated cross and a smooth per-tile blast would be a comparison between two looks rather
        than between two ways of arranging one - which is the one thing this pair of shaders
        exists to avoid. See Shader::f_lowres, and PreRender for where the scale is pushed.
    */
    tile_shader->f_lowres = true;
    tile_shader_index = renderer->AddCustomShader(tile_shader);

    cross_shader = new Shader();
    cross_shader->f_fatal_on_error = false;
    bool f_cross_ok = cross_shader->Build("shaders/default.vert","shaders/bomber_explosion.frag");
    cross_shader->uniform_callback = std::bind(&ApplicationBomber::SetCrossUniforms,this);
    cross_shader->f_lowres = true;
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
        shader->Setfloat("scatter_cutoff",scatter_cutoff);
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
    //The water rides along with F5 too. The function's name is a little narrow for what it now
    //does, but reloading exactly one of the app's shaders from the key that means "reload my
    //shaders" would be the more surprising of the two.
    if (water_shader){
        f_ok = water_shader->Reload() && f_ok;
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

/*
    The water tiles. The header's block on BuildWater says why the SHARED mesh is what gets tagged,
    and why this has to run before BuildExplosion.
*/
void ApplicationBomber::BuildWater(void){
    water_shader = new Shader();
    //Soft, like the blast's: a shader that will not compile is something to read the log of and
    //fix with F5, not something to relaunch the app over.
    water_shader->f_fatal_on_error = false;
    if (!water_shader->Build("shaders/default.vert","shaders/bomber_water.frag")){
        debug->Err("shaders/bomber_water.frag did not compile - the water will not draw at all, "
                   "F5 reloads it:\n%s\n",water_shader->compile_log.c_str());
    }
    water_shader->uniform_callback = std::bind(&ApplicationBomber::SetWaterUniforms,this);
    /*
        A water tile is SOLID FLOOR that happens to compute its own colour, so it belongs in the
        G-buffer like any other tile - see Shader::f_writes_gbuffer.

        Without this the blast's raymarch, which clamps itself to the G-buffer's depth, marches
        straight through the water and draws over the pond instead of stopping at it. It is not
        theoretical: Maze::BlocksBlast lets flame cross water on purpose, so a bomb beside a pond
        is an ordinary move. Built both ways with a volume parked across the pond, the flag was
        worth 15% of the pond's pixels and changed nothing anywhere else in the frame.
    */
    water_shader->f_writes_gbuffer = true;
    water_shader_index = renderer->AddCustomShader(water_shader);

    /*
        The one line that reaches every water tile. Named through BOMBER_TILE_ASSET rather than by
        the literal "tile_water", so the shader follows the table if the asset is ever renamed -
        that table is already the single place saying which mesh a tile type draws with.
    */
    Mesh* mesh = assetmanager->GetMeshFromAsset(BOMBER_TILE_ASSET[MAZE_TILE_WATER]);
    if (!mesh){
        debug->Err("No mesh for %s - the water tiles will draw with the default shader\n",
                   BOMBER_TILE_ASSET[MAZE_TILE_WATER]);
        return;
    }
    mesh->mesh_mode = MESH_MODE_SHADER;
    mesh->custom_shader_index = water_shader_index;
}

void ApplicationBomber::SetWaterUniforms(void){
    if (!water_shader){
        return;
    }
    //Outside the lock: the clock is published by SyncView as a plain float and is read here the
    //same unlocked way blast_age_view is, for the reason stated on it.
    water_shader->Setfloat("water_time",water_time_view);

    std::lock_guard<std::mutex> lock(knob_mutex);
    water_shader->Setfloat("water_scale",water_scale);
    water_shader->Setfloat("water_speed",water_speed);
    water_shader->Setfloat("water_depth",water_depth);
    water_shader->Setfloat("caustic_width",caustic_width);
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
                maze.bomb_x = maze.player.tile_x;
                maze.bomb_z = maze.player.tile_z;
            }
            maze.fuse_ticks = 1;
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(BOMBER_CMD_DOOR,
        [this](const SimCommand& cmd) -> objectid_t {
            SetDoorOpen(cmd.value[0] != 0.0f);
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(BOMBER_CMD_GIVE,
        [this](const SimCommand& cmd) -> objectid_t {
            int id = (int)cmd.value[0];
            if (id <= MAZE_ITEM_NONE || id >= MAZE_ITEM_COUNT || !maze.player.f_alive){
                return OBJECTID_INVALID;
            }
            //Laid on the tile, not granted: TickItems picks it up on this same tick, through the
            //same switch a dug-up one goes through.
            maze.item[maze.player.tile_z][maze.player.tile_x] = (uint8_t)id;
            maze.field_version++;
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

    if (maze.player.facing >= 0 && maze.player.facing < MAZE_NUM_DIRS &&
        input->IsKeyDown(ACTION[maze.player.facing])){
        return maze.player.facing;
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
    Brings the view back in step with the board.

    Called from SyncView on the ticks where Maze::field_version has moved - a block destroyed, an
    item taken. NOTHING IS CREATED AND NOTHING IS DESTROYED HERE; see the note on cell_block. One
    walk of 256 cells on the few ticks where something actually changed is cheaper than any
    bookkeeping that would avoid it, and it reads the rules rather than a record of them, so it
    cannot drift out of step with what the game will actually let you do.
*/
void ApplicationBomber::RefreshCells(void){
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            uint8_t t = maze.tile[z][x] < MAZE_TILE_COUNT ? maze.tile[z][x] : 0;
            if (cell_block[z][x]){
                //Still standing only while the tile still says something stands here. A hedge that
                //burned is GRASS now, and BOMBER_BLOCK_ASSET[GRASS] is NULL.
                cell_block[z][x]->SetVisibility(BOMBER_BLOCK_ASSET[t] != NULL);
            }
            if (cell_item[z][x]){
                //Visible once it can be walked onto. The tile IS the lid - see the note on MazeItem.
                bool f_want = maze.item[z][x] != MAZE_ITEM_NONE && maze.IsPassable(x,z);
                if (f_want){
                    cell_item[z][x]->SetVisibility(true);
                }else if (cell_item[z][x]->IsVisible() && cell_item_shrink[z][x] <= 0){
                    /*
                        Taken. It SHRINKS AWAY rather than blinking out, over the next
                        BOMBER_ITEM_SHRINK_TICKS.

                        Started here because this is the one place that sees the change: the rules
                        cleared the cell and bumped field_version, and comparing what is drawn
                        against what they now say is what this function is for. The `<= 0` guard
                        matters - a second field_version bump while a shrink is running (another
                        block destroyed a tick later) would otherwise restart it.
                    */
                    cell_item_shrink[z][x] = BOMBER_ITEM_SHRINK_TICKS;
                    num_item_shrinking++;
                }
            }
        }
    }
    drawn_field_version = maze.field_version;
}

/*
    The two pickup tweens: treasure turning, and a taken pickup shrinking away.

    PHYSICS THREAD, from SyncView, every tick. Both are derived from state that already exists -
    the tick counter and a per-cell countdown - so neither is anything the simulation has to
    remember and neither can drift.
*/
void ApplicationBomber::TickPickupView(void){
    /*
        The spin, off the PHYSICS TICK rather than a wall clock.

        Same reason the blast and the water are: it freezes under sim_pause, it advances one frame
        per sim_step, and a screenshot of a board is reproducible. A wall clock here would put the
        coins on a different clock from the fire burning next to them.
    */
    if (!spin_items.empty() && main_scene){
        float phase = (float)(main_scene->GetPhysicsTick() % BOMBER_ITEM_SPIN_TICKS)
                      / (float)BOMBER_ITEM_SPIN_TICKS;
        float turn = phase * TYPE_PI * 2.0f;
        for (int i = 0; i < (int)spin_items.size(); i++){
            /*
                A different starting angle each, from the INDEX rather than a random draw - the same
                call BuildTurds makes and for the same reason. Four coins turning in lockstep reads
                as one animation played four times, and a draw from the shared stream would shift it
                out from under the simulation.
            */
            float offset = (float)i / (float)spin_items.size() * TYPE_PI * 2.0f;
            spin_items[i]->SetRotation(quat(vec3(0,1,0),turn + offset));
        }
    }

    //One comparison on the overwhelming majority of ticks - see the note on num_item_shrinking.
    if (num_item_shrinking <= 0){
        return;
    }
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            if (cell_item_shrink[z][x] <= 0 || !cell_item[z][x]){
                continue;
            }
            cell_item_shrink[z][x]--;
            if (cell_item_shrink[z][x] <= 0){
                //Hidden AND put back to full size: the object is reused for the life of the field,
                //and a pickup left at scale 0 would be invisible the next time one was revealed.
                cell_item[z][x]->SetVisibility(false);
                cell_item[z][x]->SetScale(vec3(1.0f,1.0f,1.0f));
                num_item_shrinking--;
            }else{
                float f = (float)cell_item_shrink[z][x] / (float)BOMBER_ITEM_SHRINK_TICKS;
                cell_item[z][x]->SetScale(vec3(f,f,f));
            }
        }
    }
}

/*
    Puts the view where the rules say it is. Physics thread, end of the tick.

    Everything here is a WRITE DERIVED FROM Maze and never the other way round. That one-way rule
    is what lets the whole view be thrown away and rebuilt (RebuildField) without the game noticing,
    and it is why the character's world position is computed from CharX/CharZ every tick rather
    than being integrated here.
*/
void ApplicationBomber::SyncView(void){
    //--- what changed on the board ---------------------------------------------------------------
    //First, so everything placed below stands on a field that already agrees with the rules. One
    //integer comparison on the ticks where nothing was destroyed and nothing was picked up.
    if (drawn_field_version != maze.field_version){
        RefreshCells();
    }
    //Straight after it, so a pickup taken THIS tick starts shrinking on the frame it was taken
    //rather than on the next one.
    TickPickupView();

    //--- the walkers -------------------------------------------------------------------------------
    /*
        Face the way they walk.

        TWO CONVENTIONS MEET HERE AND THEY POINT OPPOSITE WAYS, which is the whole reason this
        table needs a comment rather than being four obvious numbers:

          - the ENGINE's forward axis is -Z. Setting yaw 0 and asking object_get for
            world_forward returns (0,0,-1), so yaw t gives forward (-sin t, 0, -cos t).
          - the MODEL faces +Z, which is BLENDER'S forward and exactly what an export should
            produce - it is not an art fault and there is no export setting to change it. Point
            the camera due south of the character at yaw 0 and you are looking at its face.

        So the yaw that makes a walker LOOK in direction D is the one that puts the engine's
        forward at -D, which is what these four are.

        Worth measuring rather than reasoning about: two rounds of getting this wrong were really
        the camera being dragged between the camera_set and the screenshot, which silently reframes
        the test. object_get's world_forward is the honest instrument - it needs no picture - and a
        screenshot is only needed once, to settle which way the ART faces relative to that.
    */
    static const float YAW[MAZE_NUM_DIRS] = {
         TYPE_PI * 0.5f,    //EAST  +X
        -TYPE_PI * 0.5f,    //WEST  -X
         TYPE_PI,           //NORTH -Z
         0.0f               //SOUTH +Z - the rest pose
    };

    /*
        ONE PLACEMENT FOR THE PLAYER AND THE ENEMIES, because they are the same MazeWalker and the
        two models are exported facing the same way. Two copies of this would be two chances for one
        of them to be silently turned around, and the enemy has no animation to give that away.

        X()/Z() are in TILES and fractional across a step; the cell size lives on this side.

        A DEAD WALKER IS STILL DRAWN WHILE IT IS FALLING OVER. `death_ticks` is the window Maze
        gives it, and it is the whole reason a death animation has anywhere to play: as far as every
        rule is concerned the walker is already gone, and as far as the view is concerned it is
        still on its tile, holding the last frame of the clip until the window runs out.
    */
    vec3 base = CellCentre(0,0);
    auto PlaceWalker = [&](Object* object, const MazeWalker& walker, float y){
        if (!object){
            return;
        }
        bool f_draw = walker.f_alive || walker.death_ticks > 0;
        object->SetVisibility(f_draw);
        if (!f_draw){
            return;
        }
        object->SetPosition(vec3(base.x + walker.X() * BOMBER_CELL_SIZE,y,
                                 base.z + walker.Z() * BOMBER_CELL_SIZE));
        if (walker.facing >= 0 && walker.facing < MAZE_NUM_DIRS){
            object->SetRotation(quat(vec3(0,1,0),YAW[walker.facing]));
        }
    };

    PlaceWalker(character,maze.player,character_y);
    /*
        The worn shield, which is the only feedback that a shield is running other than a number in
        a panel.

        A CHILD, so this flag is all there is to it - it is already in the right place and facing
        the right way, and a dead player draws no shield because nothing below an invisible object
        is drawn either.
    */
    if (shield_worn){
        shield_worn->SetVisibility(maze.shield_ticks > 0);
    }
    for (int i = 0; i < (int)enemy_objects.size(); i++){
        //An object past the end of this field's enemies gets a default MazeWalker, whose f_alive is
        //false - which is why that default is false rather than true.
        const MazeWalker& walker = i < maze.num_enemies ? maze.enemy[i] : MazeWalker();
        PlaceWalker(enemy_objects[i],walker,enemy_y);

        /*
            Which clip that enemy is playing, derived from the rules rather than remembered.

            Dead, cutting or walking - in that order, because being dead outranks whatever it was
            in the middle of. Reading the rules every tick and comparing against what is actually
            playing means nothing has to be kept in step: a restart, a kill, a hedge finished early
            all land on the right clip without anybody telling the view about them.

            TransitionToAnimation now BLENDS, since the state machine moved from PlayerCharacter
            into Object - a plain Skeleton used to freeze on ANIMATION_STATE_TRANSITION_START
            because nothing advanced it. Death is the exception and uses SwitchToAnimation: blending
            INTO a death softens the one moment that should read as sudden, and blending OUT of it
            would be a corpse standing back up. It is also the only non-looping clip here, so it is
            the one that must start from frame 0 rather than wherever a crossfade left it.
        */
        if (!enemy_objects[i]){
            continue;
        }
        Object* object = enemy_objects[i];
        if (!walker.f_alive){
            if (walker.death_ticks > 0 &&
                strcmp(object->CurrentAnimationName(),BOMBER_ANIM_DEATH) != 0){
                object->SwitchToAnimation(BOMBER_ANIM_DEATH);
            }
            continue;
        }
        //One comparison, because CurrentAnimationName is what the object is playing OR BECOMING -
        //it names the destination from the moment a blend starts. This used to need a second test
        //against the transition target, since "current" then meant the clip being left and a blend
        //in flight looked like the wrong clip playing.
        const char* clip = walker.chop_ticks > 0 ? BOMBER_ANIM_CHOP : BOMBER_ANIM_WALK;
        if (strcmp(object->CurrentAnimationName(),clip) != 0){
            object->TransitionToAnimation(clip);
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

    //The water's clock. Ticks rather than seconds, so it stops when the simulation does - see
    //water_time_view. Taken from the scene rather than counted here, because the scene's tick is
    //already the number every other duration in this app is measured in.
    water_time_view = (float)main_scene->GetPhysicsTick();
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
    /*
        HERE AND NOT IN PushBlastUniforms, which is the obvious place and is wrong: that runs
        inside the custom-shader pass with the program bound and the target already chosen, and
        SetCustomShaderScale may reallocate the target it is drawing into. PreRender is the hook
        for exactly this - render thread, GL context, before the frame starts.

        Unconditional because the call is a compare and a return when the scale has not moved;
        there is nothing to guard.
    */
    int scale = 1;
    {
        //Same lock every other knob is read under - see PushBlastUniforms.
        std::lock_guard<std::mutex> lock(knob_mutex);
        scale = blast_pixel_scale;
    }
    renderer->SetCustomShaderScale(scale);
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
    knobs.push_back({"scatter_cutoff",&scatter_cutoff,NULL,0.0f,0.05f,
        "skip the light march where it could not be seen. 0 = never skip, the old picture"});
    knobs.push_back({"blast_pixel_scale",NULL,&blast_pixel_scale,1.0f,8.0f,
        "window pixels per blast pixel: 2 is quarter the marches and 2x2 blocks, 1 is off"});
    knobs.push_back({"blast_light_brightness",&blast_light_brightness,NULL,0.0f,60.0f,
        "peak brightness of the point light the fire throws on the field"});
    knobs.push_back({"blast_light_radius",&blast_light_radius,NULL,0.05f,5.0f,
        "source size of that light, for the penumbra estimate"});
    //--- the water. Four, and no colour: see the block on water_scale in the header.
    knobs.push_back({"water_scale",&water_scale,NULL,1.0f,8.0f,
        "voronoi cells across one tile of water. Under 1.5 reads as blobs, over 8 as noise"});
    knobs.push_back({"water_speed",&water_speed,NULL,0.0f,8.0f,
        "how fast the caustic net breathes, radians per 100 ticks. 0 freezes it"});
    knobs.push_back({"water_depth",&water_depth,NULL,0.0f,1.0f,
        "how much darker a cell's middle is than the tile's own colour. 0 is a flat tile"});
    knobs.push_back({"caustic_width",&caustic_width,NULL,0.02f,1.0f,
        "width of the bright bridges, in cell units - so it means the same at any water_scale"});

    knobs.push_back({"debug_view",NULL,&debug_view,0.0f,3.0f,
        "0 off, 1 the marched interval, 2 the G-buffer the shader is handed, 3 noise fetches per pixel"});
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
    static const char GLYPH[MAZE_TILE_COUNT] = {'.',',',':','~','#','h','w'};
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
            //A pickup shows only once it can actually be reached, by the same rule the view draws
            //it with. A map that showed buried ones would be a map of a different game.
            if (maze.item[z][x] != MAZE_ITEM_NONE && maze.IsPassable(x,z)){
                c = (maze.item[z][x] == MAZE_ITEM_HEALTH) ? '+' : '*';
            }
            //Whatever is STANDING on a cell wins over what the cell is made of, and the player wins
            //over everything - so these go last, in this order.
            if (maze.f_bomb && x == maze.bomb_x && z == maze.bomb_z){
                c = 'o';
            }
            for (int i = 0; i < maze.num_enemies; i++){
                if (maze.enemy[i].f_alive &&
                    x == maze.enemy[i].tile_x && z == maze.enemy[i].tile_z){
                    c = 'E';
                }
            }
            if (maze.player.f_alive && x == maze.player.tile_x && z == maze.player.tile_z){
                c = '@';
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
                           "| bridge north-south  # wall  h hedge  w wood  "
                           "+ health  * shield  @ player  E enemy  o bomb";
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
        {"tile",json::array({maze.player.tile_x,maze.player.tile_z})},
        {"stepping",maze.player.step_ticks > 0},
        {"facing",maze.player.facing},
        {"x",maze.player.X()},
        {"z",maze.player.Z()},
        {"alive",maze.player.f_alive},
        {"health",maze.health},
        {"shield_ticks",maze.shield_ticks},
        {"invuln_ticks",maze.invuln_ticks},
        {"deaths",maze.deaths},
        //Treasure only. See MazeItemScore - health and shields are worth nothing here.
        {"score",maze.score},
        //Whether the shield is actually being WORN, which is the half a tick count cannot answer:
        //it is a child of the character, so this is also a test that the character exists.
        {"wearing_shield",shield_worn ? shield_worn->IsVisible() : false}
    };
    //One entry per enemy this field placed, alive or not - the index is stable, so a test can
    //follow one of them. Their TILES are what an assertion wants: "did it cut the hedge" is a
    //question about two integers and a tile type, not about a picture.
    json enemies = json::array();
    for (int i = 0; i < maze.num_enemies; i++){
        //`clip` is the VIEW's answer next to the rules' - which clip is actually playing, against
        //the chop_ticks that should have chosen it. Reported because an animation is otherwise only
        //checkable by eye, and "is it playing the right one" and "is it playing at all" are two
        //different failures that look identical in a screenshot.
        Object* object = i < (int)enemy_objects.size() ? enemy_objects[i] : NULL;
        enemies.push_back(json{
            {"tile",json::array({maze.enemy[i].tile_x,maze.enemy[i].tile_z})},
            {"alive",maze.enemy[i].f_alive},
            {"facing",maze.enemy[i].facing},
            {"chopping",maze.enemy[i].chop_ticks > 0},
            {"chop_ticks",maze.enemy[i].chop_ticks},
            {"clip",object ? object->CurrentAnimationName() : "no object"},
            //What it is fading OUT of, or "None". Non-empty exactly while a crossfade is running,
            //so it is both the blend's source and the answer to "is this one blending right now" -
            //which `clip` alone can no longer tell you, now that it names the destination from the
            //moment the blend starts.
            {"blending_from",object ? object->PreviousAnimationName() : "None"},
            {"clips_loaded",object ? (int)object->animations.size() : 0}
        });
    }
    result["enemies"] = enemies;
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
    /*
        What is left of the board, counted rather than left to be diffed out of the map.

        `version` is the one the view tracks, so a caller watching for "did anything change" reads
        the same number the renderer does instead of comparing two grids of text.
    */
    int soft_left = 0;
    int items_left = 0;
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            if (maze.IsSoft(x,z)){
                soft_left++;
            }
            if (maze.item[z][x] != MAZE_ITEM_NONE){
                items_left++;
            }
        }
    }
    result["field"] = json{
        {"version",maze.field_version},
        {"soft_blocks_left",soft_left},
        {"items_buried_or_loose",items_left},
        {"items_taken",maze.items_taken},
        {"blocks_destroyed",maze.blocks_destroyed},
        {"blocks_cut",maze.blocks_cut},
        {"enemies_killed",maze.enemies_killed},
        //Pickups part-way through shrinking away. VIEW state, reported because it is the only
        //evidence that the shrink ran at all - it is over in BOMBER_ITEM_SHRINK_TICKS and a
        //screenshot has to be taken inside that window to see it.
        {"items_shrinking",num_item_shrinking}
    };
    /*
        The door, as the ANIMATION sees it.

        `clip` and `state` are here rather than just `open` because the interesting failure is not
        a door that will not open - it is a clip that reports itself playing while the leaf stands
        still, which is what an unbound track looks like from the outside. `leaf_moved` is the one
        that cannot lie: it is the leaf's own local transform, which only the clip ever writes.
    */
    if (door_arch){
        json door = json{
            {"tile",json::array({BOMBER_DOOR_X,BOMBER_DOOR_Z})},
            {"open",f_door_open},
            {"clip",std::string(door_arch->CurrentAnimationName())},
            {"state",door_arch->animation_state},
            //1 opening, -1 shutting, and PAUSED with either means it has got there.
            {"rate",door_arch->animation_rate}
        };
        Animation* opening = door_arch->FindAnimation(BOMBER_ANIM_DOOR);
        if (opening){
            door["time_index"] = opening->time_index;
            door["duration"] = opening->duration;
            door["tracks"] = (int)opening->object_animations.size();
        }
        Object* leaf = door_arch->GetNumChildren() > 0 ? door_arch->GetChild(0) : NULL;
        if (leaf){
            vec3 p = leaf->GetPosition();
            quat r = leaf->GetRotation();
            door["leaf_moved"] = json::array({p.x,p.y,p.z});
            door["leaf_turned"] = json::array({r.x,r.y,r.z,r.w});
        }
        result["door"] = door;
    }
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

    /*
        WHAT THE BLAST COSTS, because this app is a bench for it and a bench that cannot report a
        number is a picture.

        Both are rolling averages over the last 60 frames (PerfTimer::max_deltas), in
        microseconds. `render_loop_us` is the whole render thread including the buffer swap, so it
        is the one that turns into FPS and the one that answers "is this fast enough"; `renderer_us`
        is DrawFrame alone.

        READ THEM WITH VSYNC IN MIND, which Application::Init switches on: while the frame fits in
        a refresh interval, render_loop_us IS the refresh interval and says nothing about the
        effect at all. To compare two settings of the volume, make the frame miss vsync first -
        fill the screen with the blast and put num_view_steps up - and only then read this. A
        measurement taken at a comfortable 60 FPS will report that every setting costs the same,
        which is true and useless.
    */
    result["perf"] = {
        {"render_loop_us",tmr_render_loop ? tmr_render_loop->avg : 0.0},
        {"renderer_us",(renderer && renderer->tmr_frame) ? renderer->tmr_frame->avg : 0.0},
        {"vsync",renderer ? renderer->GetVSync() : false}
    };

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

    MCPServer::Get()->RegisterTool("bomber_door",
        "Swing the exit door open, or put it back shut. The door is an ARCHWAY plus a LEAF, and "
        "the leaf is moved by the Door_Opening clip out of the .glb - this is the app's only "
        "animated prop that is not a character, so it is also the test of whether an unskinned "
        "object can be animated at all. Opening takes about 50 ticks; to look at it a frame at a "
        "time, sim_pause, call this, then sim_step. bomber_state's `door` block reports the clip, "
        "its time index and - the one thing that cannot lie - the leaf's own local transform.",
        json{
            {"type","object"},
            {"properties", {
                {"open", {{"type","boolean"},{"description","true opens it, false snaps it shut (there is only one clip, so shutting does not animate)"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame afterwards"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }}
        },
        [this](const json& args) -> json {
            SimCommand cmd;
            cmd.type = BOMBER_CMD_DOOR;
            cmd.value[0] = args.value("open",true) ? 1.0f : 0.0f;
            //An MCP handler holds no lock, so it may wait - and it has to, or it would report the
            //door's state from before the command ran.
            SubmitCommandAndWait(cmd);
            return MaybeAttachScreenshot(StateJson(),
                                         args.value("include_screenshot",false),
                                         args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("bomber_give",
        "Lay a pickup on the tile the player is standing on; the next tick collects it. `item` is "
        "health, shield, coin, diamond or crystal. FOR TESTING what a pickup does without playing "
        "a board until one turns up - it goes through Maze::TickItems exactly as a dug-up one "
        "does, so the score, the shield and the health cap are all the real rules. What it does "
        "NOT do is make one appear on screen: the field's pickup objects are built per cell when "
        "the board is laid out, and a cell that never had one has no object to show. Use it for "
        "the rules and the worn shield; watch `field.items_shrinking` for the shrink.",
        json{
            {"type","object"},
            {"properties", {
                {"item", {{"type","string"},{"description","health, shield, coin, diamond or crystal"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame afterwards"}}},
                {"include_ui", {{"type","boolean"},{"description","draw the ImGui panels in that screenshot (default true)"}}}
            }},
            {"required",json::array({"item"})}
        },
        [this](const json& args) -> json {
            std::string want = args.value("item",std::string());
            int id = MAZE_ITEM_NONE;
            if (want == "health"){ id = MAZE_ITEM_HEALTH; }
            else if (want == "shield"){ id = MAZE_ITEM_SHIELD; }
            else if (want == "coin"){ id = MAZE_ITEM_COIN; }
            else if (want == "diamond"){ id = MAZE_ITEM_DIAMOND; }
            else if (want == "crystal"){ id = MAZE_ITEM_CRYSTAL; }
            else {
                return json{ {"error","item must be health, shield, coin, diamond or crystal"} };
            }
            SimCommand cmd;
            cmd.type = BOMBER_CMD_GIVE;
            cmd.value[0] = (float)id;
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
        "box; 3 reads out what each pixel COST, as noise fetches, which is how to measure the "
        "effect on a machine whose frame timer is pinned to vsync. REMEMBER TO SET IT BACK TO 0 - "
        "nothing resets a knob but relaunching, and a debug view left on looks like a rendering "
        "bug to whoever finds it next. `draw_tiles` and `draw_cross` are not knobs but are "
        "accepted here too: they switch the two renderers on and off, and both on draws the same "
        "blast twice.",
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
    ImGui::Text("Character  tile (%2i,%2i)%s",maze.player.tile_x,maze.player.tile_z,
                maze.player.step_ticks > 0 ? "  walking" : "");
    //Everything here is in TICKS rather than seconds, which is the house rule and is also what the
    //MCP tools report - a panel that said 1.4s while bomber_state said 84 would be two units to
    //hold in your head for no gain.
    ImGui::Text("Score %u",maze.score);
    if (maze.player.f_alive){
        ImGui::Text("Health %i/%i",maze.health,MAZE_START_HEALTH);
        if (maze.shield_ticks > 0){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"SHIELD %i",maze.shield_ticks);
        }else if (maze.invuln_ticks > 0){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f,0.8f,0.3f,1.0f),"hit %i",maze.invuln_ticks);
        }
    }else{
        ImGui::TextColored(ImVec4(1.0f,0.4f,0.3f,1.0f),"Dead - back on the spawn in %i ticks",
                           MAZE_RESPAWN_TICKS - maze.dead_ticks);
    }
    //Enough to tell whether the enemies are doing anything without having to find them on the board.
    int num_alive = 0;
    int num_cutting = 0;
    for (int i = 0; i < maze.num_enemies; i++){
        if (maze.enemy[i].f_alive){
            num_alive++;
            if (maze.enemy[i].chop_ticks > 0){
                num_cutting++;
            }
        }
    }
    ImGui::Text("Enemies %i/%i alive, %i cutting   %u killed",
                num_alive,maze.num_enemies,num_cutting,maze.enemies_killed);
    ImGui::Text("Blocks  %u bombed, %u cut by enemies   pickups taken %i",
                maze.blocks_destroyed,maze.blocks_cut,maze.items_taken);

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

    //The exit. Its own row because it is the only thing on this panel that is not about the blast
    //or the board, and because it is the fastest way to look at the clip a frame at a time: pause,
    //press it, then step.
    if (ImGui::Button(f_door_open ? "Shut the door" : "Open the door")){
        //SubmitUICommand, never SubmitCommandAndWait - render thread with the mutex held. See the
        //note on the Bomb now button.
        SimCommand cmd;
        cmd.type = BOMBER_CMD_DOOR;
        cmd.value[0] = f_door_open ? 0.0f : 1.0f;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("tile (%i,%i)  %s",BOMBER_DOOR_X,BOMBER_DOOR_Z,
                        door_arch ? door_arch->CurrentAnimationName() : "no door");

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
