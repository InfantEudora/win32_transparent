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
    THE BLAST FRAGMENT SHADER, PER PLATFORM.

    TWO FILES, NOT ONE FILE WITH #ifdefs IN IT. bomber_explosion.frag is `#version 430 core`, reads
    its instance transform and the scene's lights from SSBOs in the FRAGMENT stage, samples a
    sampler3D filled by a compute shader, and carries a default initialiser on every uniform. Not
    one of those four survives on GLES - the device this port targets reports
    GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS = 0, and GLSL ES has no uniform initialisers at all. So
    the second file is a REDUCTION rather than a port, and a reduction is a different shader.

    WHAT THE TWO SHARE IS THE SHAPE, NOT THE SHADING: bomber_explosion_android.frag carries this
    file's TILE-mode timing arithmetic unchanged, so which tiles light and when is identical on
    both platforms and stays identical when a knob moves. Read the block at the top of it for what
    is lost and why that is the right thing to lose.

    THE VERTEX HALF IS NOT NAMED HERE. It comes from Application::shader_vert_name, which is
    already the one place an app's vertex stage is chosen, so the blast is drawn with whatever the
    rest of the app is drawn with rather than repeating the decision in a second place.
*/
#if defined(__ANDROID__)
    #define BOMBER_BLAST_FRAG   "shaders/bomber_explosion_android.frag"
    #define BOMBER_WATER_FRAG   "shaders/bomber_water_android.frag"
#else
    #define BOMBER_BLAST_FRAG   "shaders/bomber_explosion.frag"
    #define BOMBER_WATER_FRAG   "shaders/bomber_water.frag"
#endif

/*
    THE WATER FORK IS NOT THE SAME KIND OF THING AS THE BLAST FORK, and it is worth knowing
    which is which before either is edited.

    The blast pair is PERMANENT: one marches a noise volume through SSBO-fed instance data and
    the other cannot, because the hardware says so. The water pair is a DIALECT split only -
    bomber_water_android.frag is bomber_water.frag from the first #define down, character for
    character, with an ES preamble in front of it. If the desktop file ever moves to a version
    and a varying convention both platforms share, that second file simply goes away.
*/

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
    "tile_grass",   //MAZE_TILE_DOOR  - the ground under the archway
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
    //NULL, and that is the whole of what used to be a special case in RebuildField: the archway is
    //its own object, placed and animated separately, so the door cell simply has no block on it.
    NULL,           //MAZE_TILE_DOOR
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
    "pickup_key",
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
    false,              //key
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
    The player's rig, and the clips it MAY have.

    THE CLIP LIST IS PROBED, NOT ASSUMED - see BuildCharacter. The file carries `Character_Idle`
    today and nothing else, so the walk name below currently matches nothing; exporting a clip under
    that name is the whole of adding a walk, with no code change here. That is worth the one query
    it costs, because the alternative is either an error logged every launch for a clip that is
    deliberately absent, or a name that has to be added in two places later.

    The bone names in this rig are MIXAMO's (`mixamorig:Hips` and 26 more), not the enemy's four
    hand-named ones. Nothing here cares - Animation::LinkObjects binds by string against whatever
    the skeleton has - but it is what tells you at a glance which rig a clip came from.
*/
#define BOMBER_CHAR_SKIN    "character_armature"
#define BOMBER_ANIM_CHAR_IDLE  "Character_Idle"
#define BOMBER_ANIM_CHAR_WALK  "Character_Walking"
#define BOMBER_ANIM_CHAR_DEATH "Character_Death"
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

//--- the corridor between levels --------------------------------------------------------------------
/*
    Where the corridor puts you back onto the board is `Maze::entry_x/entry_z` and no longer a pair
    of defines here.

    IT MOVES NOW. The board's entry border is chosen from the direction the corridor runs, so that
    the corridor never has to be turned to meet a fixed cell - which is what let the camera hold one
    world direction through a whole transition. RebuildField leaves the brick off whichever cell it
    is and stands the archway there; the board still has one on level one, where nothing walked in
    through it, because MAZE_DIR_WEST is Maze::NewGame's default.
*/
//Ticks one cell takes to rise out of the floor, and how far behind the row in front it starts. The
//corridor unrolls away from you rather than appearing all at once.
#define BOMBER_HALL_RISE_TICKS  14
#define BOMBER_HALL_ROW_DELAY   4
//How far below the floor a cell starts. A whole cell, so it is clearly arriving from underneath.
#define BOMBER_HALL_RISE_DROP   1.0f
/*
    Ticks from the corridor sealing to the board being swapped.

    The Door_Opening clip is 0.83s - 50 ticks - and the commit must not happen until the near door
    has FINISHED shutting, because until then the old board is still visible through the gap. Sixty
    is that plus a moment.
*/
#define BOMBER_HALL_COMMIT_TICKS 60
/*
    The corridor's framing: the game framing with the pitch dropped and the distance closed in.

    THE YAW IS THE ONE THING THAT DOES NOT CHANGE, and that is the entire point. It used to: the
    camera went behind and above the player in the CORRIDOR'S frame, so a corridor running east
    swung the view a quarter turn - and the keys are world directions, so screen-up stopped being
    the forward key for the length of the walk. Pitch and distance can move freely, because neither
    of them is what a thumb reads.

    Coming down and in is still worth doing: the corridor is a small space and the board's framing
    would leave the player a speck in the middle of it. This is close enough to read a score tally
    in, which is what the corridor is eventually for.
*/
#define BOMBER_HALL_CAM_PITCH     50.0f
#define BOMBER_HALL_CAM_DISTANCE  7.0f

/*
    The old board sinking out of sight, which is the corridor's pop-in run backwards.

    It starts at the SEAL and has to be finished before the commit throws the objects away - so the
    spread plus the fall has to fit inside BOMBER_HALL_COMMIT_TICKS. The board's far corner is about
    22 units from any exit, so 22 * SPREAD + TICKS is the number to keep under 60.
*/
#define BOMBER_BOARD_SINK_TICKS  24
#define BOMBER_BOARD_SINK_DROP   7.0f
//Ticks of delay per world unit away from the exit. Nearest goes first, so the level collapses away
//behind the player rather than dropping all at once.
#define BOMBER_BOARD_SINK_SPREAD 0.9f

/*
    The NEW board rising into place, which is the sink run the other way.

    IT HAS LESS TIME THAN THE SINK DOES. The sink owns the whole BOMBER_HALL_COMMIT_TICKS window
    before the commit; the rise only has what is left of the corridor after it, and on a 4-long one
    that is little more than the far door's 50-tick swing. The board's far corner is about 22 units
    from the entry, so 22 * SPREAD + TICKS is the number to keep under 50 - and EndHallway finishes
    whatever is still in the air rather than trusting the arithmetic.
*/
#define BOMBER_BOARD_RISE_TICKS  20
#define BOMBER_BOARD_RISE_DROP   7.0f
#define BOMBER_BOARD_RISE_SPREAD 0.5f

/*
    The yaw that makes a walker LOOK in each direction. Lifted out of SyncView because the corridor
    needs it too - see the long note at its old home for why these four numbers are what they are.
*/
static const float BOMBER_WALKER_YAW[MAZE_NUM_DIRS] = {
     TYPE_PI * 0.5f,    //EAST  +X
    -TYPE_PI * 0.5f,    //WEST  -X
     TYPE_PI,           //NORTH -Z
     0.0f               //SOUTH +Z - the rest pose
};

//A world direction as a unit step in world space, and as the yaw that points local +z along it.
//Local +z is SOUTH, which is yaw 0 - so this is BOMBER_WALKER_YAW turned round.
static vec3 BomberDirVec(int dir){
    return vec3((float)Maze::DirX(dir),0.0f,(float)Maze::DirZ(dir));
}
static float BomberDirYaw(int dir){
    switch (dir){
        case MAZE_DIR_EAST:  return  TYPE_PI * 0.5f;
        case MAZE_DIR_WEST:  return -TYPE_PI * 0.5f;
        case MAZE_DIR_NORTH: return  TYPE_PI;
        default:             return  0.0f;      //SOUTH, and local +z is SOUTH
    }
}

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
    /*
        THE SCENE PAIR COMES FROM Application::shader_vert_name / shader_frag_name, not from
        literals, and that is not only about the _android suffix.

        WATCH THE SECOND ARGUMENT: it does not name the same shader in both trees. The desktop
        Renderer::Init builds its DEFERRED program from (vert,frag) - hence the "shaders/
        deferred.frag" that used to be written here - while the Android one builds its COLOUR
        program from them and has no separate deferred fragment shader at all. Two Inits with one
        signature and two meanings, so a literal that is right in one tree is quietly wrong in the
        other: it compiles, it links, and it draws the wrong thing or nothing.

        Taking both names from the members is what makes this line correct on both sides without
        being two lines. Each tree's Application supplies whatever its own Init wants, and an app
        never has to know which of the two contracts it is talking to.
    */
    if (!renderer->Init(shader_vert_name,shader_frag_name,PIPELINE_DEFERRED)){
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

    //Both names from the members, for the reason spelled out at the Renderer::Init call above -
    //this is the one place an app chooses its shader stages, and it has to be one place.
    //
    //shader_LIT_frag_name, not shader_frag_name: this is the SHADED stage, where Renderer::Init
    //above took the G-buffer one. Two names because this tree has two scene programs and the
    //port has one - the block on them in core/Application.h says which is which, and why using
    //the wrong one links cleanly and then draws a scene with no lighting in it.
    default_shader = new Shader(shader_vert_name,shader_lit_frag_name);
    /*
        The enemy is a SKINNED mesh, and a skinned mesh has nowhere to be drawn without this.

        Renderer::skinned_shader is NULL by default and an app that forgets it gets no warning -
        the enemy simply does not appear, which reads as a failed asset load rather than as a
        missing shader. Only the fragment half is shared with default_shader; the vertex half is
        the one that knows about bone matrices.
    */
    renderer->skinned_shader = new Shader(shader_skinned_vert_name,shader_lit_frag_name);

    main_window->Resize(1024,640);

    /*
        60 ticks per second, not the default 50.

        The blast clock is counted in ticks, so the tick rate IS the frame rate of the explosion. A
        fireball's first fifth of a second is where all its expansion happens, and at 50 Hz that is
        ten frames to say it in. It also makes MAZE_STEP_TICKS a round third of a second.
    */
    SetPhysicsTPS(60.0f);

    game_scene = CreateNewScene("Bomber Scene");
    main_scene = game_scene;
    assetmanager = new AssetManager();

    /*
        The game framing, placed through the same solve that maintains it - see BOMBER_CAM_PITCH.

        NOT A LITERAL POSITION any more, and that is the point: there is one description of where
        the game camera goes, so the framing Init starts at and the framing UpdateGameCamera eases
        back to after a transition cannot drift apart.
    */
    camera_target = vec3(0.0f,0.0f,0.0f);

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
    BuildCharacter();
    BuildTurds();
    BuildDoor();
    BuildHallway();
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

    /*
        AND NOW THE CAMERA, because the framing leans toward the player and there was no player to
        lean toward until the line above ran. Called before the board exists it aims at a default
        walker on cell (0,0) - the far corner - and the first framing anyone sees is three units off
        the middle of the board, easing back over the next second.

        Snapped rather than eased: there is no previous framing for the first one to come from.
    */
    UpdateGameCamera(true);

#ifdef USE_SOUND
    //After the field, so a device that takes a moment to open does not delay anything visible.
    LoadSounds();
#endif

    SetupInput();
    RegisterCommandHandlers();

#ifdef USE_MCP
    //Guarded because this app's own tools call into MCPServer, which USE_MCP=0 does not compile.
    //The CORE tools are switched a different way - see engine.mk.
    RegisterMCPTools();
#endif

    /*
        LAST, and main_scene is moved onto it directly rather than through RequestActiveScene.

        Both of those are about ordering. Everything above built and registered against the game
        scene while it was the active one, so it had to go first. And a direct write is correct
        HERE and nowhere else: Init runs on the frame thread before Application::Start creates the
        physics thread, so no other thread is reading main_scene yet. Once the game is running,
        RequestActiveScene is the only safe way to change it.
    */
    CreateTitleScene();
    main_scene = title_scene;

    /*
        AND THE BOARD'S ATLASES GO STRAIGHT BACK OFF THE DEVICE.

        Everything above still loads at startup - the GLB is read once and its textures uploaded
        with it, which is the simplest thing and costs a second at launch rather than a stall
        mid-game. What does not work is keeping them all RESIDENT: six material textures against
        the device's five units (NUM_MATERIAL_UNITS), and the title screen samples exactly one of
        the six.

        So the app finishes Init in the same state the loading screen leaves it in on the way back
        from a game - the menu's two on the GPU, the board's four decoded and parked in RAM. Which
        also makes the FIRST start go through exactly the same steps as every later one, instead
        of being the single transition whose uploads were already done and whose bar would jump.

        After CreateTitleScene, necessarily: its two materials are what CollectPageTextures tells
        the sets apart by, and they do not exist until it has run.
    */
    //Picks the splash for this window and parks the other variants, so the line below sees a
    //settled menu set rather than every variant at once. Safe here: main_scene is already the
    //title scene, which is the one thing EnsureSplashVariant insists on.
    EnsureSplashVariant();

    std::vector<Texture*> menu_textures;
    std::vector<Texture*> game_textures;
    CollectPageTextures(menu_textures,game_textures);
    for (Texture* tex:game_textures){
        tex->Unload();
    }

    debug->Ok("Bomber ready - %ix%i field, seed %u, %i objects, %i menu textures resident and "
              "%i game textures parked\n",
              MAZE_W,MAZE_H,current_seed,(int)field_objects.size(),
              (int)menu_textures.size(),(int)game_textures.size());
}

/*
    An orthographic camera and one unlit quad, and nothing else - no light, no physics world.

    THE QUAD IS BUILT 2x2 AND SIZED BY ITS SCALE, every frame, in PreRender.

    With an orthographic camera `zoom` is the half-HEIGHT in world units and the half-width is
    zoom*aspect (Camera::CalculateLookatMatrix), so at zoom 1 the viewport is x in [-aspect,aspect]
    by y in [-1,1] and a 2x2 quad covers a SQUARE one exactly. Everything else is the scale.

    Which is what lets the splash keep its own aspect - see GetTitleContentRect - instead of being
    stretched to whatever shape the window is. Renderer::DrawFrame already refreshes the camera's
    aspect from the live viewport every frame; PreRender does the same for the quad, so a resize
    needs no hook and there is no size cached anywhere to go stale.

    A SCALE RATHER THAN A REBUILT MESH: Mesh::SetMeshData uploads immediately, so rebuilding this
    on every resize would be a GPU upload per mouse-drag frame to say something a transform
    already says.
*/
void ApplicationBomber::CreateTitleScene(){
    title_scene = CreateNewScene("Title Screen");

    const float half_height = 1.0f;

    Camera* camera = title_scene->camera;
    camera->name = "Title Camera";
    //Down the -Z axis at the quad, which MakeQuad builds facing +Z. Engine forward is -Z.
    camera->SetPosition(vec3(0,0,1));
    camera->SetLookAt(vec3(0,0,0));
    camera->SetupOrthographic((float)renderer->GetViewportWidth(),(float)renderer->GetViewportHeight(),
                              half_height,0.01f,10.0f);

    /*
        f_unlit, which is what this flag is for: the texture goes to the screen as it is, with no
        light in the scene to light it and no shadow to darken it. The alternative - a light and a
        lit material - would tint a piece of artwork that is already exactly the colour it should
        be. See the long note on f_unlit in core/Material.h.
    */
    /*
        TWO materials on one quad, swapped by ApplyMenuPage - the front page keeps the painted
        splash, every sub-page gets the empty dungeon.

        Both are built here, at startup, rather than the background being loaded when a page is
        first opened. Loading a texture is a render-thread job and opening a menu is not, so a
        lazy load would either block the first frame of that page or need a request across the
        thread boundary; there are two of them and they are small.
    */
    /*
        THE SPLASH VARIANTS, WIDEST FIRST, and the order is what PickSplashVariant reads: it takes
        the first row the surface is at least as wide as, so the last row's 0 is the catch-all.

        THE CUT IS AT 1:1 and that is a real line rather than a tuned number - it is where a
        screen stops being landscape and starts being portrait, which is also where four buttons
        stop fitting side by side. Between 1:1 and very wide the landscape picture is cropped
        further and further at the sides, which is what BOMBER_FIT_HEIGHT is for and what the art
        is drawn to survive: everything that matters is in the middle. Past the picture's own
        2.33 the same mode stops cropping and starts banding instead, so an absurdly wide window
        gets the whole picture rather than a letterbox slot cut out of its middle.
    */
    const bomber_splash_variant variants[BOMBER_SPLASH_VARIANT_COUNT] = {
        {"images/splash_wide_noui.jpg",    "bomber_splash_wide",    1.0f,BOMBER_FIT_HEIGHT,false},
        {"images/splash_portrait_noui.jpg","bomber_splash_portrait",0.0f,BOMBER_FIT_HEIGHT,true},
    };
    for (int i = 0; i < BOMBER_SPLASH_VARIANT_COUNT; i++){
        splash_variant[i] = variants[i];
    }

    struct title_material{
        const char* name;
        const char* asset;
        int* out_index;
        //The Texture as well as the material, because the menu's set is unloaded by hand - see
        //the block on the variants in the header.
        Texture** out_texture;
    };
    title_material wanted[BOMBER_SPLASH_VARIANT_COUNT + 1];
    for (int i = 0; i < BOMBER_SPLASH_VARIANT_COUNT; i++){
        wanted[i] = {splash_variant[i].material_name,splash_variant[i].asset,
                     &splash_variant[i].material,&splash_variant[i].texture};
    }
    wanted[BOMBER_SPLASH_VARIANT_COUNT] = {"bomber_menu_back","images/menu_background.jpg",
                                           &title_material_menu,&title_texture_menu};

    for (int i = 0; i < (int)(sizeof(wanted) / sizeof(wanted[0])); i++){
        Material mat = {};
        mat.name = wanted[i].name;
        mat.glsl_material.color = vec4(1,1,1,1);
        mat.glsl_material.f_unlit = 1;
        Texture* tex = renderer->LoadTexture(wanted[i].asset);
        if (tex){
            mat.glsl_material.diffuse_texture = 0;
            mat.glsl_material.handle_diffuse = tex->texture_handle;
            mat.diff_texture = tex;
        }else{
            //Not fatal: a title screen that is a flat colour still works as one, and its buttons
            //still work - they are drawn by the overlay now, not painted into the picture.
            //Losing the artwork should not cost anyone the app.
            debug->Err("Title screen: could not load %s\n",wanted[i].asset);
        }
        renderer->AddMaterial(mat);
        *(wanted[i].out_index) = renderer->FindMaterialIndex(mat.name);
        //NULL when the load failed, which every user of these already has to survive.
        *(wanted[i].out_texture) = tex;
    }

    title_splash = new Object();
    title_splash->name = "Title Splash";
    //flip_v, because this samples a MATERIAL texture and those are authored V=0 at the top row -
    //without it the splash renders upside down. See the note on MakeQuad in core/Primitives.h.
    //
    //2x2 and unscaled here: the shape is entirely the scale PreRender sets, and it sets one
    //before the first frame is drawn, so there is no frame where this square is seen.
    title_splash->SetMesh(MakeQuad(2.0f * half_height,2.0f * half_height,true));
    //No material yet: EnsureSplashVariant picks one for the surface and parks the rest, which is
    //also what keeps only one of them resident. It runs from PreRender before the first frame.
    title_splash->SetMaterialSlot(0,title_material_menu);
    //Nothing on this screen is an object the player or the inspector should be picking.
    title_splash->SetPickability(false);
    title_scene->AddObject(title_splash);
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
                      "pickup_key","pickup_coin","pickup_diamond","pickup_crystal",
                      BOMBER_SHIELD_ASSET,
                      BOMBER_DOOR_ARCH_ASSET,BOMBER_DOOR_LEAF_ASSET,
                      BOMBER_BOMB_ASSET);
    /*
        NEITHER THE ENEMY NOR THE CHARACTER IS IN THAT LIST, and that is the whole difference between
        them and everything else on the board. GetAssetsFromGLTF builds one Object per node and hands
        out copies that SHARE its mesh, which is exactly right for four hundred tiles and exactly
        wrong for a skinned one: a pose lives in the bones, the bones are the object's children, and
        two actors sharing one set would be one actor drawn twice. BuildEnemies and BuildCharacter
        give each its own skeleton, its own bones and its own copies of the clips.

        THE CHARACTER WAS IN IT until it became skinned on 2026-09-17, and leaving it there after
        would not have failed - it would have loaded the skin a second time as a plain mesh, logged
        "Loading a skinned mesh as normal mesh", and quietly kept an unused copy of it in the
        AssetManager for the life of the process.
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
    The player, as a skinned skeleton. RENDER THREAD, ONCE, from Init.

    ONCE AND IN Init IS THE WHOLE POINT OF THIS FUNCTION EXISTING. The character used to be made in
    RebuildField out of the AssetManager, which was fine while it was a static mesh - but a skeleton
    uploads a mesh, and RebuildField runs on the PHYSICS thread. So the player joins the enemies and
    the turds under the same rule: built once here, and a restart only moves it. It is not in
    `field_objects` and is never destroyed, which also means `shield_worn` outlives a rebuild with
    it and the two pointers can never be left dangling by one.

    THE CLIPS ARE WHATEVER THE FILE HAS. GetAnimationNames is asked rather than a fixed list being
    passed, because the rig currently ships with an idle and no walk: naming a walk that does not
    exist would log an error on every launch for something deliberately absent, and leaving it out
    would mean editing this list the day one is exported. Neither is necessary - see the note at
    BOMBER_ANIM_CHAR_WALK.
*/
void ApplicationBomber::BuildCharacter(void){
    static const char* WANTED[] = {BOMBER_ANIM_CHAR_IDLE,BOMBER_ANIM_CHAR_WALK,
                                   BOMBER_ANIM_CHAR_DEATH};

    const char* clips[sizeof(WANTED)/sizeof(WANTED[0])];
    int num_clips = 0;
    std::vector<std::string> present = gltfloader.GetAnimationNames();
    for (size_t w = 0; w < sizeof(WANTED)/sizeof(WANTED[0]); w++){
        for (size_t i = 0; i < present.size(); i++){
            if (present[i] == WANTED[w]){
                clips[num_clips++] = WANTED[w];
                break;
            }
        }
    }
    if (num_clips == 0){
        debug->Err("No character clips in bomber_assets.glb - looked for %s and %s\n",
                   BOMBER_ANIM_CHAR_IDLE,BOMBER_ANIM_CHAR_WALK);
    }

    Skeleton* skeleton = BuildSkinnedActor(BOMBER_CHAR_SKIN,BOMBER_CHAR_ASSET,clips,num_clips,true);
    if (!skeleton){
        return;
    }
    skeleton->name = "Character";
    //Kept as an Object* from here on, exactly as the enemies are: everything this class does to it
    //afterwards - SetPosition, SetVisibility, TransitionToAnimation - is Object's, and the skeleton
    //half only matters while it is being built.
    character = skeleton;

    /*
        The shield goes on as a CHILD with no transform of its own.

        The artist placed `equipped_shield` on top of `character` in the .glb - both nodes sit at the
        same origin - so identity here is exactly where it was drawn.

        ON THE SKELETON'S ROOT, NOT ON A BONE, and that is a choice rather than a limitation. A bone
        would make the bubble bob and lean with the body; the root keeps it centred on the tile the
        player is standing on, which is what the thing actually protects. Attaching a held item -
        a weapon, a lamp - is the case that would want a bone instead.
    */
    shield_worn = assetmanager->GetObjectFromAsset(BOMBER_SHIELD_ASSET);
    if (shield_worn){
        shield_worn->name = "Shield";
        shield_worn->SetVisibility(false);
        skeleton->AttachChild(shield_worn);
    }

    /*
        DYING IS AN EVENT, NOT A STATE - the one clip here that does not loop, exactly as the enemy's
        is. A looping death is a body repeatedly getting back up to fall over again; a one-shot stops
        on its last frame, which is the pose a corpse should hold for the rest of MAZE_DEATH_TICKS.
    */
    Animation* death = skeleton->FindAnimation(BOMBER_ANIM_CHAR_DEATH);
    if (death){
        death->looped = false;
    }

    /*
        HOW FAST THE WALK HAS TO RUN TO STOP THE FEET SKATING, solved rather than dialled in.

        The clip is an IN-PLACE walk - every translation track in it is flat, so it says how the legs
        move and nothing about how far that carries you. The board says that: one tile per
        MAZE_STEP_TICKS. So the two are tied together here, by asserting the one thing that makes a
        walk read as walking - ONE CYCLE IS TWO FOOTFALLS, AND A FOOTFALL IS A TILE.

        Derived from the clip's own duration rather than written down as a number, so re-exporting a
        longer or shorter cycle, or changing how fast a walker crosses a tile, keeps the feet on the
        ground with nothing to remember. At 1.04 s over two 20-tick steps at 60 Hz it comes out about
        1.56; at rate 1.0 the character covers three tiles per cycle and visibly skates.
    */
    Animation* walk = skeleton->FindAnimation(BOMBER_ANIM_CHAR_WALK);
    if (walk && walk->duration > 0.0f){
        float seconds_for_two_tiles = 2.0f * (float)MAZE_STEP_TICKS * GetPhysicsTimestep();
        char_walk_rate = walk->duration / seconds_for_two_tiles;
    }

    //Something has to be playing or ApplyAnimation has nothing to pose and the rig stands in its
    //bind pose, which looks exactly like a clip that failed to load. Same reason as BuildEnemies.
    if (num_clips > 0){
        skeleton->SwitchToAnimation(clips[0]);
    }
    skeleton->SetVisibility(false);
    main_scene->AddObject(skeleton);
    debug->Ok("Built the character: %i bone(s), %i clip(s), walk at %.2fx\n",
              skeleton->num_bones,num_clips,char_walk_rate);
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

      - Scene::UpdateAnimations walks `Scene::objects`, which holds what was handed to
        Scene::AddObject. A child is DRAWN through its parent (Renderer::CullObjects recurses) but
        it is not in that list, so ApplyAnimation would never run on the leaf. The archway is in it.
      - Object::AddAnimation calls Animation::LinkObjects(this), which resolves each track against
        this object AND its children by name. The clip's one track is named `door`, so it binds to
        the leaf from up here.

    So the archway is the animated object and the leaf is what the animation moves, exactly as a
    Skeleton is the animated object and its bones are what move.
*/
Object* ApplicationBomber::MakeDoorway(const char* name){
    Object* arch = assetmanager->GetObjectFromAsset(BOMBER_DOOR_ARCH_ASSET);
    Object* leaf = assetmanager->GetObjectFromAsset(BOMBER_DOOR_LEAF_ASSET);
    if (!arch || !leaf){
        debug->Err("A doorway needs both %s and %s in bomber_assets.glb\n",
                   BOMBER_DOOR_ARCH_ASSET,BOMBER_DOOR_LEAF_ASSET);
        return NULL;
    }
    arch->name = name;
    //NOT a display name: LinkObjects matches the clip's track against it. See BOMBER_DOOR_LEAF_ASSET.
    leaf->name = BOMBER_DOOR_LEAF_ASSET;
    /*
        No transform on the leaf. The two nodes share an origin in the .glb, so identity is already
        the shut pose, and from here on the clip owns this transform completely - anything set on it
        would be overwritten on the first frame that plays.
    */
    arch->AttachChild(leaf);

    /*
        ITS OWN COPY OF THE CLIP, one per archway.

        AddAnimation binds a clip's tracks to the object it is added to and its children, so a clip
        shared between two archways would drive whichever linked it last and leave the other standing
        - the same rule BuildSkinnedActor keeps for the enemies. The track is named `door` and each
        arch has a child of that name, which is why the names may repeat.
    */
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
        arch->AddAnimation(opening);
        //The same unbound-track report BuildSkinnedActor does, and for the same reason: a track
        //that found nothing to drive looks exactly like a clip that is playing.
        for (ObjectAnimation* track:opening->object_animations){
            if (!track->target){
                debug->Warn("Clip %s drives nothing on [%s] - no object of that name under %s\n",
                            BOMBER_ANIM_DOOR,track->target_name.c_str(),arch->name.c_str());
            }
        }
        debug->Ok("Clip %-16s %i track(s), %.2fs\n",BOMBER_ANIM_DOOR,
                  (int)opening->object_animations.size(),opening->duration);
    }

    main_scene->AddObject(arch);
    return arch;
}

void ApplicationBomber::BuildDoor(void){
    door_arch = MakeDoorway("Doorway");
    //Shut, and WHERE it stands is not decided here - RebuildField puts it on the cell Maze chose.
    SetDoorOpen(false,true);
}

/*
    Puts a door at one end of its clip THIS INSTANT and holds it there.

    The rate-0 case, and the one thing SwitchToAnimation cannot express: a corridor's near door has
    to START open, because the player has just walked through it, and playing the clip forwards to
    get there would be fifty ticks of it opening in front of somebody who is already inside.

    `time_index` is written directly, which is the only reach-in - it is the field Object's own state
    machine advances, and setting a rate of 0 is what stops it advancing again.
*/
void ApplicationBomber::ParkDoor(Object* arch, bool f_open){
    if (!arch){
        return;
    }
    Animation* clip = arch->FindAnimation(BOMBER_ANIM_DOOR);
    if (!clip){
        return;
    }
    arch->SwitchToAnimation(clip);
    clip->time_index = f_open ? clip->duration : 0.0f;
    //Posed now rather than on the next tick, so there is not one frame of the old pose.
    clip->ApplyInterval(clip->time_index);
    arch->SetAnimationRate(0.0f);
}

/*
    Every object a corridor can ever need, at its longest, built once and then only shown and moved.

    RENDER THREAD, from Init. Twenty-four floors and forty walls covers HALL_MAX_LEN at full width
    with the surplus hidden, which is the same bargain the turds and the enemies make: a pool costs
    a few hidden objects and buys never having to create one on the physics thread.
*/
void ApplicationBomber::BuildHallway(void){
    for (int z = 0; z < HALL_MAX_LEN; z++){
        for (int x = 0; x < HALL_W; x++){
            char name[48];
            snprintf(name,sizeof(name),"Hall floor (%i,%i)",x,z);
            Object* o = assetmanager->GetObjectFromAsset(BOMBER_TILE_ASSET[MAZE_TILE_BRICK]);
            hall_floor[z][x] = o;
            if (o){
                o->name = name;
                o->SetVisibility(false);
                //Same reasoning as the field's floor - see the SetCastsShadow note in
                //RebuildField. Nothing is under a floor.
                o->SetCastsShadow(false);
                main_scene->AddObject(o);
            }
        }
        for (int px = 0; px < HALL_W + 2; px++){
            char name[48];
            snprintf(name,sizeof(name),"Hall wall (%i,%i)",px - 1,z);
            Object* o = assetmanager->GetObjectFromAsset(BOMBER_BLOCK_ASSET[MAZE_TILE_WALL]);
            hall_wall[z][px] = o;
            if (o){
                o->name = name;
                o->SetVisibility(false);
                main_scene->AddObject(o);
            }
        }
    }
    hall_near_arch = MakeDoorway("Hall Near Door");
    hall_far_arch = MakeDoorway("Hall Far Door");
    if (hall_near_arch){
        hall_near_arch->SetVisibility(false);
        ParkDoor(hall_near_arch,false);
    }
    /*
        The far arch is the board's ENTRY and it is visible from the very first frame, standing shut
        in the border beside the spawn - on level one too, where nothing came through it. During a
        transition it is carried out to the corridor's far end and then brought back here, which is
        why it is this object and not a fourth one.
    */
    if (hall_far_arch){
        ParkDoor(hall_far_arch,false);
    }
    debug->Ok("Corridor pool: %i floor, %i wall, 2 doorways\n",
              HALL_MAX_LEN * HALL_W,HALL_MAX_LEN * (HALL_W + 2));
}

/*
    World position of a corridor cell, in the corridor's own frame.

    Fractional, so a walker mid-step lands between two cells. `hall_origin` is local (1,0) - the near
    doorway - and the two axes come straight out of Hallway::LocalToWorld, so moving or turning the
    corridor is two members and nothing else has to be told.
*/
vec3 ApplicationBomber::HallCellCentre(float x, float z) const {
    vec3 fwd = BomberDirVec(hall.LocalToWorld(MAZE_DIR_SOUTH));
    vec3 across = BomberDirVec(hall.LocalToWorld(MAZE_DIR_EAST));
    return hall_origin + across * ((x - 1.0f) * BOMBER_CELL_SIZE) + fwd * (z * BOMBER_CELL_SIZE);
}

/*
    The player has stepped into the open exit. PHYSICS THREAD, from RunSimulationTick.

    The corridor's local cell (1,0) is the exit cell ITSELF, so nothing moves at the moment the two
    swap over: the same body is standing in the same place, and which class is simulating it changes.
*/
void ApplicationBomber::BeginHallway(void){
    if (f_in_hallway){
        return;
    }
    /*
        NOTHING IS SAVED ABOUT THE CAMERA HERE, and that is worth a line because a saved pose used
        to be exactly what was restored on the way out. The game framing is SOLVED from the state of
        the game, so the way back to it is simply to stop being in a corridor - see SolveGameFraming.
    */
    /*
        THE LEVEL IS OVER, SO THE CLOCK PAYS OUT. Here and nowhere else: this is the tick the player
        stepped into the exit, and it is the last moment `maze.level_ticks` means anything - the
        commit lays out a new board and restarts it.

        Onto the WALKER, because the walker is what travels (see MazeWalker) - so the points are
        already on the body that `hall.Begin` picks up two lines down, with nothing to marshal. Both
        halves are kept for the corridor to show; see hall_level_ticks.
    */
    hall_level_ticks = maze.level_ticks;
    hall_time_bonus = maze.TimeBonus();
    maze.player.score += hall_time_bonus;
    if (hall_time_bonus > 0){
        debug->Ok("Level cleared in %u ticks - time bonus %u (par %i)\n",
                  hall_level_ticks,hall_time_bonus,MAZE_TIME_PAR_TICKS);
    }
    //Chosen NOW rather than at the commit, so the next board's seed is settled before anything can
    //depend on it - and so a recorded run lays out the same one.
    hall_next_seed = next_auto_seed++;
    //The way they were walking IS the way the corridor runs. It cannot be anything else: they got
    //here by stepping outward through the border.
    hall.Begin(hall_next_seed,maze.player,maze.player.facing);
    hall_origin = CellCentre(maze.door_x,maze.door_z);

    f_in_hallway = true;
    f_hall_committed = false;
    hall_commit_ticks = 0;
    hall_build_ticks = 0;

    /*
        The board's exit door steps aside for the corridor's near arch.

        Same asset in the same place, and the yaw is COPIED rather than recomputed so the hinge and
        the leaf are on the same side - a 180 degree difference would be invisible on the archway and
        very visible on the door hanging in it. The board's one is carried off to the next board at
        the commit, which is why this cannot simply be it.
    */
    if (door_arch){
        if (hall_near_arch){
            hall_near_arch->SetRotation(door_arch->GetRotation());
        }
        door_arch->SetVisibility(false);
    }
    if (hall_near_arch){
        hall_near_arch->SetVisibility(true);
        //OPEN, because the player has just walked through it. Fifty ticks of it opening in front of
        //somebody already standing inside is what ParkDoor exists to avoid.
        ParkDoor(hall_near_arch,true);
        f_hall_near_drawn_open = true;
    }
    if (hall_far_arch){
        ParkDoor(hall_far_arch,false);
        f_hall_far_drawn_open = false;
    }
#ifdef USE_SOUND
    /*
        The level itself moving, which is the one moment in this game when a mass of stone does.

        THE CLIP IS block_shift.wav AND THIS IS A JUDGEMENT CALL - it is the only one of the seven
        whose name does not name an event in this game. A destructible block breaking was the other
        candidate; the corridor rising out of the floor while the old board falls away won because
        it happens ONCE, and four hedges going up together with a 1.08 s tail apiece would be mud
        under an explosion that is already playing.
    */
    PlaySound("level_shift",0.8f);
#endif
    debug->Ok("Corridor: %i long, running %s, next board seed %u\n",
              hall.length,
              hall.forward == MAZE_DIR_EAST ? "east" :
              hall.forward == MAZE_DIR_WEST ? "west" :
              hall.forward == MAZE_DIR_NORTH ? "north" : "south",
              hall_next_seed);
    SyncHallwayView();
}

/*
    The near door has finished shutting. Swap the board, and move the corridor to meet the new one.

    THE ONE MOMENT ANY OF THIS IS SAFE. The corridor is a closed box by now - a door at each end,
    both shut, and this app draws no skybox - so there is no external reference by which a move or a
    turn could be seen. The player and the camera are carried along with it.
*/
void ApplicationBomber::CommitHallway(void){
    //Where the player is before any of it moves. The corridor's DIRECTION is not recorded,
    //because nothing below changes it any more - see the note further down.
    vec3 before = HallCellCentre(hall.player.X(),hall.player.Z());

    current_seed = hall_next_seed;
    /*
        The walker goes with them, and Maze::NewGame is where a level boundary's cost is decided -
        health and score kept, the key and the shield taken. One place, and not this one.
    */
    /*
        AND THE ENTRY BORDER COMES FROM THE CORRIDOR. It runs `forward`, so it arrives at the border
        whose outward normal is the opposite of that - walking north into a board means coming in
        through its south side. This one argument is what removed the rotation below.
    */
    maze.NewGame(current_seed,&hall.player,Maze::DirOpposite(hall.forward));
    RebuildField();

    /*
        Now slide the corridor up to it. A SLIDE AND NOTHING ELSE - `hall.forward` is not touched.

        THIS USED TO BE A TURN, and that turn is what the camera work was really about. The next
        board had one fixed entry cell that had to be walked into heading east, so a corridor running
        any other way was picked up and rotated here - and the camera had to be rotated with it to
        keep that invisible, which is precisely how the view came to point somewhere new every level
        with the keys still pointing where they always had. The board's entry border is chosen from
        the corridor's direction now (Maze::NewGame above), so the corridor already points at it and
        there is nothing left to rotate.

        The far doorway is local (1, length-1), so the origin - local (1,0) - is that cell walked
        back down the length of the corridor.
    */
    hall_origin = CellCentre(maze.entry_x,maze.entry_z)
                - BomberDirVec(hall.forward) * ((float)(hall.length - 1) * BOMBER_CELL_SIZE);

    vec3 after = HallCellCentre(hall.player.X(),hall.player.Z());

    /*
        And carry the camera with it, by exactly the same translation.

        A PURE TRANSLATION IS INVISIBLE TO ANY CAMERA THAT SHARES IT, which a rotation is not unless
        the camera is turned too - and that is the whole trade this change makes. The pivot is the
        only camera state that has to move, because everything else about the game framing is solved
        from it; moving it outright rather than letting the ease chase it is what stops the slide
        showing up as the camera lagging a few units behind the corridor.
    */
    camera_target += after - before;

    /*
        The board is standing there now, so the far door is allowed to open - see f_next_ready.

        AND IT STARTS RISING. It was laid out at full height a few lines up, and the camera no longer
        hides that: the framing holds one world direction all the way through a transition, so from
        over a one-brick corridor wall the swap is in shot. A board that falls away behind you and
        rises ahead of you is a better thing to be able to watch than a pop is a thing to hide.
    */
    hall.f_next_ready = true;
    board_sink_ticks = -1;
    board_rise_ticks = 0;
    //Out of the doorway they are about to come through, so it assembles away from them.
    board_rise_from = CellCentre(maze.entry_x,maze.entry_z);
    //Once, now, so the first frame after the commit has the board underground rather than standing.
    RiseBoard();
    f_hall_committed = true;
    debug->Ok("Corridor sealed: board %u laid out at the %s border, corridor slid to meet it\n",
              current_seed,
              maze.entry_dir == MAZE_DIR_EAST  ? "east"  :
              maze.entry_dir == MAZE_DIR_WEST  ? "west"  :
              maze.entry_dir == MAZE_DIR_NORTH ? "north" : "south");
}

/*
    The player has stepped into the far doorway, which is the border cell beside the spawn - so they
    are already standing on the next board and Maze has had them on its spawn since the commit.
*/
void ApplicationBomber::EndHallway(void){
    f_in_hallway = false;
    /*
        Put whatever is still in the air straight down on the ground.

        The rise has only the tail of the corridor to run in - see BOMBER_BOARD_RISE_TICKS - and on
        a short one that can run out. Finishing it outright is right rather than merely safe: the
        player is standing on this board now, and a board still arriving under their feet is worse
        than one that arrived a few ticks early.
    */
    if (board_rise_ticks >= 0){
        RiseBoard(true);
    }
    /*
        NOTHING SETS `facing` HERE. Maze::NewGame did, at the commit, from the entry border - which
        is the same direction the corridor ran, because that is where the border came from. It used
        to be forced to MAZE_DIR_EAST here because the corridor had been turned to run east by now;
        with the turn gone there is one place that decides it and this is not it.
    */

    if (door_arch){
        door_arch->SetVisibility(true);
    }
    if (hall_near_arch){
        hall_near_arch->SetVisibility(false);
    }
    for (int z = 0; z < HALL_MAX_LEN; z++){
        for (int x = 0; x < HALL_W; x++){
            if (hall_floor[z][x]){
                hall_floor[z][x]->SetVisibility(false);
            }
        }
        for (int px = 0; px < HALL_W + 2; px++){
            if (hall_wall[z][px]){
                hall_wall[z][px]->SetVisibility(false);
            }
        }
    }
    /*
        THE FAR ARCH STAYS. It is standing in the border beside the spawn, which is exactly where the
        corridor left it, and it is the door the player came in by - so it swings shut behind them
        rather than being taken away with the rest.
    */
    if (hall_far_arch){
        hall_far_arch->SetAnimationRate(-1.0f);
        f_hall_far_drawn_open = false;
    }
    SyncView();
}

/*
    The old board falling out of sight, a tick at a time.

    THE CORRIDOR'S POP-IN RUN BACKWARDS, and the same shape: every piece has its own clock, staggered
    by how far it is from the exit the player just left through, so the level collapses away behind
    them instead of dropping in one piece.

    Applied as a DELTA rather than an absolute Y, because nothing else is writing these positions
    while the corridor is the live one - SyncView returns early - so there is no base position to
    remember and no second copy of it to fall out of step.
*/
void ApplicationBomber::SinkBoard(void){
    if (board_sink_ticks < 0){
        return;
    }
    int was = board_sink_ticks;
    board_sink_ticks++;

    /*
        How far one piece has fallen at tick t, given how far it stands from the exit.

        Squared, so it starts gently and accelerates away - a linear drop reads like a lift and this
        reads like a floor giving way.
    */
    struct Fall{
        static float At(int t, float dist){
            float delay = dist * BOMBER_BOARD_SINK_SPREAD;
            float k = clamp(((float)t - delay) / (float)BOMBER_BOARD_SINK_TICKS,0.0f,1.0f);
            return -(k * k) * BOMBER_BOARD_SINK_DROP;
        }
    };

    auto sink = [&](Object* object){
        if (!object || !object->IsVisible()){
            return;
        }
        vec3 p = object->GetPosition();
        //Distance in PLAN, so a piece does not speed up as it falls - its own Y is the one thing
        //about it that is changing.
        float dx = p.x - board_sink_from.x;
        float dz = p.z - board_sink_from.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float d = Fall::At(board_sink_ticks,dist) - Fall::At(was,dist);
        if (d != 0.0f){
            object->SetPosition(p + vec3(0.0f,d,0.0f));
        }
    };

    for (Object* object:field_objects){
        sink(object);
    }
    for (Object* object:enemy_objects){
        sink(object);
    }
    for (Object* object:turd_objects){
        sink(object);
    }
    sink(bomb);
    //The board's own exit door goes with it. It is hidden by now - the corridor's near arch is
    //standing in its place - but RebuildField will move it to the next board, and leaving it sunk
    //would put it underground there.
}

/*
    The NEW board rising into place. PHYSICS THREAD, from RunSimulationTick while the corridor is
    still the live one.

    SinkBoard's curve read backwards, and the same per-tick DELTA - which is safe for the same
    reason: SyncView returns early in the corridor, so nothing else is writing these positions
    between the commit and EndHallway. See board_rise_ticks for why the board is worth watching
    arrive at all.

    Only `field_objects`. The enemies and the turds of the board being built are placed by SyncView
    from the rules the moment it takes over, so an offset put on them here would be overwritten on
    the first tick out of the corridor and is simply a lie in the meantime.
*/
void ApplicationBomber::RiseBoard(bool f_finish){
    if (board_rise_ticks < 0){
        return;
    }
    int was = board_rise_ticks;
    board_rise_ticks++;

    /*
        How far below its place one piece still is at tick t, given how far it stands from the way in.

        Squared FROM THE FAR END rather than from the near one, so it decelerates into position
        instead of slamming down - which is the sink's accelerating fall reversed, and is what makes
        the two read as one move rather than as two effects that happen to share a distance.
    */
    struct Rise{
        static float At(int t, float dist){
            float delay = dist * BOMBER_BOARD_RISE_SPREAD;
            float k = clamp(((float)t - delay) / (float)BOMBER_BOARD_RISE_TICKS,0.0f,1.0f);
            float e = 1.0f - k;
            return -(e * e) * BOMBER_BOARD_RISE_DROP;
        }
    };

    for (Object* object:field_objects){
        if (!object || !object->IsVisible()){
            continue;
        }
        vec3 p = object->GetPosition();
        //Distance in PLAN, so a piece does not change speed as it rises.
        float dx = p.x - board_rise_from.x;
        float dz = p.z - board_rise_from.z;
        float dist = sqrtf(dx * dx + dz * dz);
        /*
            THE FIRST CALL COMES FROM NO OFFSET AT ALL, which is what puts the board underground:
            RebuildField has just left every piece standing at its proper height, so there is no
            previous value of this curve to step from. Every call after it steps from the last one
            the way the sink does.
        */
        float from = (was == 0) ? 0.0f : Rise::At(was,dist);
        float d = (f_finish ? 0.0f : Rise::At(board_rise_ticks,dist)) - from;
        if (d != 0.0f){
            object->SetPosition(p + vec3(0.0f,d,0.0f));
        }
    }
    if (f_finish){
        board_rise_ticks = -1;
    }
}

/*
    The corridor on screen: where its cells are, how far out of the floor they have risen, and its
    two doors. PHYSICS THREAD, from SyncView, every tick while the corridor is the live one.
*/
void ApplicationBomber::SyncHallwayView(void){
    hall_build_ticks++;

    //Hoisted: GetNodePosition is a name lookup, and this is per tick rather than per cell.
    float floor_y = gltfloader.GetNodePosition(BOMBER_TILE_ASSET[MAZE_TILE_BRICK]).y;
    float wall_y = gltfloader.GetNodePosition(BOMBER_BLOCK_ASSET[MAZE_TILE_WALL]).y;
    float yaw = BomberDirYaw(hall.forward);

    for (int z = 0; z < HALL_MAX_LEN; z++){
        /*
            THE ROW'S OWN CLOCK. Each row starts BOMBER_HALL_ROW_DELAY after the one in front, so the
            corridor unrolls away from the player rather than appearing all at once - which is the
            difference between a corridor arriving and a corridor having always been there.
        */
        int t = hall_build_ticks - z * BOMBER_HALL_ROW_DELAY;
        float rise = clamp((float)t / (float)BOMBER_HALL_RISE_TICKS,0.0f,1.0f);
        float drop = -(1.0f - rise) * BOMBER_HALL_RISE_DROP;
        bool f_row = (z < hall.length) && (t > 0);

        for (int x = 0; x < HALL_W; x++){
            Object* o = hall_floor[z][x];
            if (!o){
                continue;
            }
            bool f_show = f_row && hall.IsPassable(x,z);
            o->SetVisibility(f_show);
            if (f_show){
                o->SetPosition(HallCellCentre((float)x,(float)z) + vec3(0.0f,floor_y + drop,0.0f));
                o->SetRotation(quat(vec3(0,1,0),yaw));
            }
        }
        for (int px = 0; px < HALL_W + 2; px++){
            Object* o = hall_wall[z][px];
            if (!o){
                continue;
            }
            //px is local x + 1, so the two side walls fall out of IsPassable along with the caps at
            //either end of the corridor.
            bool f_show = f_row && !hall.IsPassable(px - 1,z);
            o->SetVisibility(f_show);
            if (f_show){
                o->SetPosition(HallCellCentre((float)(px - 1),(float)z)
                               + vec3(0.0f,wall_y + drop,0.0f));
                o->SetRotation(quat(vec3(0,1,0),yaw));
            }
        }
    }

    //--- the two doors --------------------------------------------------------------------------
    if (hall_near_arch){
        hall_near_arch->SetPosition(HallCellCentre(1.0f,0.0f));
        //Yaw is NOT set here: it was copied from the board's exit door at Begin so the leaf hangs
        //the same way round, and the corridor never turns while this one is still in shot.
        if (f_hall_near_drawn_open != hall.f_near_door_open){
            f_hall_near_drawn_open = hall.f_near_door_open;
#ifdef USE_SOUND
            //The near door SHUTTING is the one worth hearing - it is the moment the level behind
            //you stops existing, and it is the only thing that happens on that tick.
            PlaySound("door",0.75f);
#endif
            hall_near_arch->SetAnimationRate(hall.f_near_door_open ? 1.0f : -1.0f);
        }
    }
    if (hall_far_arch){
        hall_far_arch->SetPosition(HallCellCentre(1.0f,(float)(hall.length - 1)));
        hall_far_arch->SetRotation(quat(vec3(0,1,0),yaw));
        if (f_hall_far_drawn_open != hall.f_far_door_open){
            f_hall_far_drawn_open = hall.f_far_door_open;
#ifdef USE_SOUND
            PlaySound("door",0.8f);
#endif
            hall_far_arch->SetAnimationRate(hall.f_far_door_open ? 1.0f : -1.0f);
        }
    }

    //--- the player -------------------------------------------------------------------------------
    vec3 walker_pos = HallCellCentre(hall.player.X(),hall.player.Z());
    if (character){
        character->SetVisibility(true);
        character->SetPosition(walker_pos + vec3(0.0f,character_y,0.0f));
        //The walker's facing is LOCAL - the corridor turns it back into a world direction, which is
        //the whole reason Hallway keeps its own frame.
        int world_facing = hall.LocalToWorld(hall.player.facing);
        if (world_facing >= 0 && world_facing < MAZE_NUM_DIRS){
            character->SetRotation(quat(vec3(0,1,0),BOMBER_WALKER_YAW[world_facing]));
        }
    }
    //The shield follows the body as always - it is a child - so only whether it shows is decided.
    if (shield_worn){
        shield_worn->SetVisibility(hall.player.shield_ticks > 0);
    }

    /*
        THE CAMERA IS NOT THIS FUNCTION'S BUSINESS, and it used to be.

        It came down here, behind and above the player in the CORRIDOR'S frame - which turned the
        view a quarter or a half turn every level, because the corridor runs whichever way the exit
        faced. UpdateGameCamera solves the corridor's framing now (SolveGameFraming), from the same
        world yaw the board uses, so the only thing left of that block is the pitch and distance it
        chose - and those are two constants rather than a camera write buried in a view sync.
    */
    drawn_hall_version = hall.version;
}



/*
    Swings it, or swings it back.

    PHYSICS THREAD ONLY - from SyncView, which compares it against the player's key every tick, and
    from RebuildField and BuildDoor with `f_snap`.

    ONE CLIP, RUN BOTH WAYS. There is a single `Door_Opening` in the file and shutting is it at -1,
    which is the whole of Object::SetAnimationRate's reason for existing. Nothing rewinds, so
    losing the key halfway through the swing reverses from halfway through rather than snapping to
    the far end - which is also what a door hit by a second thought actually does.

    `f_snap` is the one case where that is wrong: a NEW BOARD's door was never open, so rewinding
    the clip first puts it shut on the first frame instead of swinging it shut over fifty ticks.
*/
void ApplicationBomber::SetDoorOpen(bool f_open, bool f_snap){
    if (!door_arch){
        return;
    }
    f_door_open = f_open;
    if (f_snap){
        //The only rewind in the door's life. After this the clip is only ever run one way or the
        //other, and the first tick takes it to an end and pauses it there, posing the leaf.
        door_arch->SwitchToAnimation(BOMBER_ANIM_DOOR);
    }
#ifdef USE_SOUND
    /*
        Only on the way OPEN, and only when it is actually swinging.

        `f_snap` is a door being PARKED at one end of its clip - a new board's door starting shut -
        and a parked door makes no noise, because nothing moved. Without that test every RebuildField
        would creak.
    */
    if (f_open && !f_snap){
        PlaySound("door",0.8f);
    }
#endif
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
    /*
        THE CHARACTER IS NOT THROWN AWAY HERE EITHER, for the same reason the enemies are not: it is
        a skinned skeleton built once on the render thread by BuildCharacter, and this is the physics
        one. It used to be destroyed and remade per field, back when it was a static mesh.

        Its shield goes on surviving with it, which is what the two pointers being left alone means.
    */
    if (bomb){
        bomb->Destroy();
        bomb = NULL;
    }
    main_scene->DeleteDestroyedObjects();

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
            Object* tile = AddCellObject(tile_asset,x,z,tile_y,0.0f,true);
            /*
                THE FLOOR CASTS NOTHING, AND THAT IS THE SINGLE CHEAPEST THING IN THIS FILE.

                A floor tile is the lowest surface in the world - there is nothing beneath it for
                its shadow to land on, and the shadows that matter (blocks, plants, characters
                onto the ground) are unaffected, because RECEIVING a shadow does not depend on
                this flag. So the picture is identical and the shadow pass stops re-submitting
                every tile on the board from the light's point of view.

                Measured on the MDT740: the field submits ~1.49M vertices a frame and the shadow
                pass re-submits all of them, of which tile_grass alone is 641,472 - 43% of the
                pass, for nothing. See Renderer's frame_vertices/shadow_vertices counters and the
                Timing tab's "log draw breakdown" button, which is where those numbers come from.
            */
            if (tile){
                tile->SetCastsShadow(false);
            }

            /*
                The block standing on it, if any.

                A thin panel takes its angle from its neighbours so a run reads as one hedge rather
                than as a pile - see BOMBER_PANEL_YAW_X. The test is on which ASSET it is and not on
                whether the tile is destructible, because being a panel is a fact about the MODEL:
                re-export wall_brick as a thin one and this is the line that should change.
            */
            const char* block_asset = BOMBER_BLOCK_ASSET[t];
            //The way IN. The corridor's far archway stands on this cell for the whole level, so
            //the border gets no brick here - see Maze::entry_x.
            if (x == maze.entry_x && z == maze.entry_z){
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

    //The bomb is the only mover this builds now - the character is built once in Init, and is not
    //in field_objects, so the destroy loop above cannot reach the thing the player is.
    /*
        The exit, on the cell Maze picked for it this time.

        Positioned here rather than in BuildDoor because the cell moves with the board, and only
        moving it is safe on this thread - the objects themselves were built once on the render
        thread, which is the same split the enemies and the turds keep.
    */
    if (door_arch){
        door_arch->SetPosition(CellCentre(maze.door_x,maze.door_z));
        /*
            A panel lying ALONG the wall it stands in - east-west on the north and south borders,
            north-south on the other two. The same two angles the hedges and wooden walls take; see
            BOMBER_PANEL_YAW_X.
        */
        bool f_side = (maze.door_x == 0 || maze.door_x == MAZE_W - 1);
        door_arch->SetRotation(quat(vec3(0,1,0),f_side ? BOMBER_PANEL_YAW_Z : BOMBER_PANEL_YAW_X));
        //A new board is a new lock, and it was never open - so snap rather than swing.
        SetDoorOpen(false,true);
    }
    /*
        And the way in, which is a cell of THIS board and so is placed here once rather than moved
        about. It is not the same cell every level any more - see the note where the entry defines
        used to be - so the archway takes its angle from which border it landed in, exactly as the
        exit door above does.

        Left where the corridor put it if one is in flight - SyncHallwayView owns it then, and moving
        it here would drag the far end of a corridor somebody is standing in.
    */
    if (hall_far_arch && !f_in_hallway){
        bool f_entry_side = (maze.entry_x == 0 || maze.entry_x == MAZE_W - 1);
        hall_far_arch->SetPosition(CellCentre(maze.entry_x,maze.entry_z));
        hall_far_arch->SetRotation(quat(vec3(0,1,0),
                                        f_entry_side ? BOMBER_PANEL_YAW_Z : BOMBER_PANEL_YAW_X));
        ParkDoor(hall_far_arch,false);
        f_hall_far_drawn_open = false;
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
#ifdef USE_SOUND
    /*
        And the sound watch, BEFORE that SyncView runs - NewGame has just put every counter back to
        zero, and a watch still holding the last board's totals would sit above them and stay silent
        until they caught up. See BomberSoundWatch.
    */
    ResetSoundWatch();
#endif
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
#if defined(__ANDROID__)
    /*
        NO NOISE VOLUME HERE, AND NOTHING STANDING IN FOR ONE.

        Three separate things in the desktop path do not exist on this device: the compute shader
        that fills the volume, the glBindImageTexture that gives it somewhere to write, and the
        glBindTextureUnit that hands the result to the fragment stage (DSA, absent at every GLES
        version up to 3.2). And the shader that would sample it does not run here either - see
        BOMBER_BLAST_FRAG at the top of this file.

        blast_noise STAYS NULL, which is the flag PushBlastUniforms already tests before binding
        it. That is deliberate: there is no second switch to keep in step with this one, so a
        later change that brings the volume back only has to touch this function.
    */
    debug->Info("Blast noise skipped: no compute shaders on this platform - the blast is drawn\n"
                "                    analytically instead, see bomber_explosion_android.frag\n");
#else
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
#endif //__ANDROID__
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
    bool f_tile_ok = tile_shader->Build(shader_vert_name,BOMBER_BLAST_FRAG);
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
    bool f_cross_ok = cross_shader->Build(shader_vert_name,BOMBER_BLAST_FRAG);
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

    /*
        EVERYTHING ABOVE THIS LINE IS SHARED, everything below it until the shared tail is the
        march's. The split is not cosmetic: bomber_explosion_android.frag declares only the names
        above, and a uniform a program does not declare is stripped by the GLSL compiler, so
        pushing the march's settings at it would cost a warning per name at startup and a
        glGetUniformLocation per name per frame for nothing.

        ONE GUARD RATHER THAN TWO FUNCTIONS. The shared half is the half that decides WHICH tiles
        burn and WHEN, and that must not be allowed to drift between platforms - keeping it in one
        unguarded block is what makes drifting impossible rather than merely unlikely.
    */
#if !defined(__ANDROID__)
    //Per-mode, and deliberately not knobs.
    shader->Setfloat("shell_thickness",
                     (mode == BLAST_MODE_CROSS) ? BOMBER_CROSS_SHELL : BOMBER_TILE_SHELL);
    shader->Setfloat("core_heat",
                     (mode == BLAST_MODE_CROSS) ? BOMBER_CROSS_CORE_HEAT : BOMBER_TILE_CORE_HEAT);
#endif

    {
        std::lock_guard<std::mutex> lock(knob_mutex);
        shader->Setfloat("blast_life",blast_life);
        shader->Setfloat("tile_delay",tile_delay);
        //The two length knobs, converted. Everything below them is already dimensionless.
        shader->Setfloat("blast_radius",blast_radius * to_object);
        shader->Setfloat("rise",rise * to_object);

#if !defined(__ANDROID__)
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
#endif //!__ANDROID__
        //Shared again: both shaders draw the box when asked to.
        shader->Setint("f_show_box",debug_view);
    }

#if !defined(__ANDROID__)
    /*
        THE NOISE VOLUME. Guarded for the entry point rather than for the pointer: glBindTextureUnit
        is direct state access and does not exist at any GLES version, so this line has to go even
        though blast_noise is always NULL here (see BuildBlastNoise) and the test would have covered
        it at runtime. A call that cannot LINK is not saved by a branch that never runs.
    */
    if (blast_noise){
        glBindTextureUnit(TEXUNIT_APP_RESERVED,blast_noise->texture_id);
    }
#endif

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
    //shader_vert_name, not a literal: the water rides the same vertex stage as everything else,
    //and naming it here again is how a port loses one shader and not the others.
    if (!water_shader->Build(shader_vert_name,BOMBER_WATER_FRAG)){
        debug->Err("%s did not compile - the water will not draw at all, F5 reloads it:\n%s\n",
                   BOMBER_WATER_FRAG,water_shader->compile_log.c_str());
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

    /*
        EVERY MAPPING IN THIS BLOCK IS A WIN32 VIRTUAL-KEY CODE, so it is guarded rather than
        assumed - the same treatment, and for the same reason, as the one in apps/tetris. The
        Android port of this file builds against a device with no keyboard to press them on, and
        VK_LEFT and friends do not exist there at all. The bare character literals are in here too:
        'W' is only a key code because Win32 happens to number the letter keys by their ASCII
        value, which is not a portable fact.

        THE D-PAD MAPPINGS THAT USED TO SIT BESIDE EACH ARROW HAVE MOVED BELOW, out of the guard.
        Grouping all three bindings of an action together read better, but it put portable lines
        inside a Windows-only block, and a port then loses the d-pad for no reason. Split the way
        tetris splits it, so the two files stay comparable.

        Arrows AND WASD because muscle memory differs and both cost nothing: KeyState::f_isdown
        counts HELD MAPPINGS rather than being a boolean, so an action stays down while any of its
        keys is.
    */
#if defined(_WIN32)
    input->AddKeyMap(VK_UP,INPUT_BOMBER_NORTH);
    input->AddKeyMap('W',INPUT_BOMBER_NORTH);
    input->AddKeyMap(VK_DOWN,INPUT_BOMBER_SOUTH);
    input->AddKeyMap('S',INPUT_BOMBER_SOUTH);
    input->AddKeyMap(VK_LEFT,INPUT_BOMBER_WEST);
    input->AddKeyMap('A',INPUT_BOMBER_WEST);
    input->AddKeyMap(VK_RIGHT,INPUT_BOMBER_EAST);
    input->AddKeyMap('D',INPUT_BOMBER_EAST);

    input->AddKeyMap(VK_SPACE,INPUT_BOMBER_DROP);
    input->AddKeyMap('B',INPUT_BOMBER_DROP);

    input->AddKeyMap('R',INPUT_BOMBER_RESTART);
    input->AddKeyMap(VK_F5,INPUT_BOMBER_RELOAD_SHADER);
    //The free orbit, which is a debugging affordance and is off by default now - see BOMBER_CAM_GAME.
    input->AddKeyMap('C',INPUT_BOMBER_CAMERA);

    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature - and pausing
    //mid-fireball is the single most useful thing you can do to a volumetric effect.
    input->AddKeyMap('P',INPUT_PAUSE);

    /*
        Escape, and the one line that makes it ours.

        Without clearing f_escape_closes_window the window closes on the key-down and nothing below
        ever runs - the key was always arriving (Raw Input forwards every VK), it just never got a
        chance to mean anything. See the note on the flag in core/Window.h.

        Inside the guard with the key it exists to serve: a platform with no Escape key has no
        window-closing behaviour to suppress either.
    */
    input->AddKeyMap(VK_ESCAPE,INPUT_BOMBER_BACK);
    if (main_window){
        main_window->f_escape_closes_window = false;
    }
    /*
        ENTER IS THE LOADING SCREEN'S TAP for anyone on a keyboard, and it is the only menu action
        with a key at all - everything else on these pages is a rectangle drawn into the artwork.

        Enter rather than Space, which reads as the more natural "any key": Space is already the
        bomb (see the DROP maps above), and one key on two actions is not a thing this input layer
        models. It maps a key to an action, and the second map would be the one that lost.
    */
    input->AddKeyMap(VK_RETURN,INPUT_BOMBER_MENU_TAP);
#endif //_WIN32

    /*
        GAMEPAD. Outside the guard on purpose: GAMEPAD_KEY_* carry their own XInput bit values on
        platforms with no <xinput.h> - see core/InputController.h - so these lines are portable as
        written and a port gets the same layout from the same source.
    */
    input->AddKeyMap(GAMEPAD_KEY_DPAD_UP,INPUT_BOMBER_NORTH);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_DOWN,INPUT_BOMBER_SOUTH);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_BOMBER_WEST);
    input->AddKeyMap(GAMEPAD_KEY_DPAD_RIGHT,INPUT_BOMBER_EAST);
    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_BOMBER_DROP);
    //START for the loading screen's tap, for the same reason Enter is: A is already the bomb.
    input->AddKeyMap(GAMEPAD_KEY_START,INPUT_BOMBER_MENU_TAP);

    /*
        The menu buttons, bound here and POSITIONED NOWHERE YET.

        AddTouchButton allocates each one its own synthetic keycode and that is the part that has
        to happen once; the rects change every time the page does. Passing a zero rect is what the
        engine asks for - see the note on AddTouchButton - and LayoutMenuButtons owns the geometry
        from here on. A zero rect also means nothing is live until a page is applied, which is the
        safe direction to fail in.

        The labels are for the engine's own debug drawing of these rects, which this app leaves
        off (f_draw_touch_buttons); they cost nothing and make an ImGui dump of the button list
        readable.
    */
    const InputController::TouchRect nowhere;
    menu_button[0] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_START,  "start");
    menu_button[1] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_LEVELS, "levels");
    menu_button[2] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_OPTIONS,"options");
    menu_button[3] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_SCORES, "scores");
    menu_button[4] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_BACK,   "back");
    //The loading screen's tap. Bound like the rest and given its rect by LayoutMenuButtons, which
    //is where it becomes the whole surface on that one page and nothing on every other.
    menu_button[5] = input->AddTouchButton(nowhere,INPUT_BOMBER_MENU_TAP,    "tap");

    /*
        The gameplay set - see the block on these in the header. Bound to the SAME actions the
        keyboard maps above, not to new ones: a touch button is another way to press a key, and
        everything downstream (the DAS in UpdateView, the BACK edge in the menu handler, the
        pause gate in Scene::BeginPass) goes on reading exactly what it read before.

        INPUT_PAUSE is the engine's own action rather than one of this app's, which is why
        pausing needs no code beyond this line and a rect.

        --- ANDROID ONLY, AND THE MENU'S BUTTONS ABOVE ARE NOT ------------------------------------
        A phone has nothing else to play with; a desktop has the arrow keys, Space and Escape
        already, so a D-pad over the board is seven rectangles of dead weight sitting on top of
        the game. That is what USE_TOUCH_UI says and this is the app half of it.

        THE MENU'S SIX ARE BOUND UNCONDITIONALLY, a few lines up, and the difference is not an
        oversight. Those are not a substitute for a keyboard - they ARE the interface, the only
        thing a mouse can aim at, and on the front page the only way into the game at all. The
        engine's flag is about on-screen GAME CONTROLS, which is a narrower thing than "rectangles
        on the screen", and conflating the two is how the menu lost its mouse.

        GATING THE BINDING, not just the drawing: an unbound button is index -1, which
        LayoutGameButtons and DrawGameButtons both already skip, so nothing is placed and nothing
        is live. A rect placed but not drawn would be an invisible button eating clicks in the
        corner of a desktop window, which is the failure the note on USE_TOUCH_UI warns about.
    */
#if USE_TOUCH_UI
    touch_menu  = input->AddTouchButton(nowhere,INPUT_BOMBER_BACK, "MENU");
    touch_pause = input->AddTouchButton(nowhere,INPUT_PAUSE,       "II");
    touch_north = input->AddTouchButton(nowhere,INPUT_BOMBER_NORTH,"^");
    touch_south = input->AddTouchButton(nowhere,INPUT_BOMBER_SOUTH,"v");
    touch_west  = input->AddTouchButton(nowhere,INPUT_BOMBER_WEST, "<");
    touch_east  = input->AddTouchButton(nowhere,INPUT_BOMBER_EAST, ">");
    touch_bomb  = input->AddTouchButton(nowhere,INPUT_BOMBER_DROP, "BOMB");
#endif //USE_TOUCH_UI

    //The engine's own button rendering is a debugging aid and would draw five boxes over the
    //artwork. This app draws its own - see DrawMenu and DrawGameButtons.
    f_draw_touch_buttons = false;
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
            //The RULE, not the animation: SyncView brings the clip along on the next tick. The key
            //is the PLAYER's, which is what stops an enemy walking out through the same door.
            maze.player.f_has_key = (cmd.value[0] != 0.0f);
            maze.field_version++;
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
/*
    The menu's navigation, and Escape everywhere. PHYSICS THREAD, from UpdateView.

    FROM UpdateView RATHER THAN RunSimulationTick, WHICH IS A CORRECTION. It lived in the tick
    first, and pausing the game exposed why that is wrong: RunSimulationTick runs only on passes
    that TICK, so while the simulation was paused Escape did nothing - and because an unread edge
    is KEPT rather than dropped (backlog item 88), it then fired the moment the game was unpaused.
    Press pause, press Escape, nothing; unpause, and the menu opens by itself. Measured, not
    theorised.

    UpdateView runs on every pass including paused ones, for the reason stated where it is called
    in core/Application.cpp - a paused editor still needs a working camera - and a paused game
    still needs a working Escape. It is also exactly where the engine already handles the twin of
    this: Scene::BeginPass reads INPUT_PAUSE outside the tick for the same reason.

    That this sits outside the tick does NOT put it at odds with the tick-driven input direction.
    What that is about is SIMULATION state, which has to be reproducible from a recorded input
    stream. Which page a menu is on and which scene is active are application state: they are not
    simulated, not recorded, and not replayed. Gameplay stays in the tick; the menu is not
    gameplay.

    READ EVERY EDGE UNCONDITIONALLY, ACT ON THEM ONLY WHEN THE INPUT IS OURS. This is the rule the
    click-to-start handler that used to live here was built around, and it survives it because
    nothing about it was about clicking:

    A click NEXT TO the window used to start the game, and both halves of that are the engine
    working as designed. Raw input is registered RIDEV_INPUTSINK, so it arrives whether or not we
    are in front; SubmitSystemKey drops a key DOWN while unfocused but always honours the UP, so
    anything already held can still release - and losing focus additionally runs the release-all
    sweep, which raises a release edge for it. Fire on a release edge and an out-of-window click
    reaches you by either route.

    The reads stay outside the focus test on purpose. An edge nobody has read is KEPT across passes
    (backlog item 88, see InputController::Tick), so gating the read itself would park a stray
    release and spend it the instant focus came back - turning "starts too early" into "starts on
    the click that focused the window", which is worse. Consuming them here and discarding them is
    what actually throws them away. That matters MORE now than it did with one click: five parked
    edges would fire as a burst of navigation the moment the window came forward.

    RELEASE rather than press, so the click that gives the window focus does not fall through into
    whatever happens to be under it.
*/
void ApplicationBomber::UpdateMenu(InputController* input){
    bool f_start   = input->WasKeyReleased(INPUT_BOMBER_MENU_START);
    bool f_levels  = input->WasKeyReleased(INPUT_BOMBER_MENU_LEVELS);
    bool f_options = input->WasKeyReleased(INPUT_BOMBER_MENU_OPTIONS);
    bool f_scores  = input->WasKeyReleased(INPUT_BOMBER_MENU_SCORES);
    bool f_back    = input->WasKeyReleased(INPUT_BOMBER_MENU_BACK);
    bool f_tap     = input->WasKeyReleased(INPUT_BOMBER_MENU_TAP);
    //Escape means the same thing the BACK button does, so on the menu they are simply one edge.
    //Read separately as well, because in the GAME only Escape means it.
    bool f_escape  = input->WasKeyReleased(INPUT_BOMBER_BACK);
    f_back = f_back || f_escape;

    /*
        THE LOADING PAGE COMES FIRST, AND ABOVE THE INPUT GATE BELOW.

        It answers to nothing but the render thread: no input leaves it, because half of what
        either side of it needs is unloaded while it is up. Every edge read above is therefore
        dropped on the floor here, which is the intent.

        ABOVE the IsInputLive check and not below it, which is where this was first written and is
        a hang. Finishing a load is not an input event. With the check in front of it the page only
        advances while input is live, so a player who alt-tabs during a load - or anyone driving
        the app over MCP once the scripted hold has expired, which is how it was found - comes back
        to a full progress bar that never leaves.

        This is also the only place either transition ENDS, in both directions, which is what keeps
        "the textures are ready" and "the page changed" from ever being two decisions that could be
        made in the wrong order.
    */
    if (menu_page == BOMBER_PAGE_LOADING){
        if (!f_loading_complete.load()){
            //Still uploading. Every edge read above is dropped, including the tap - its rect is
            //live but nothing reads it yet, so an early tap is ignored rather than queued.
            return;
        }
        /*
            THE TAP GUARDS THE WAY IN ONLY.

            Going INTO the game it is worth stopping for: the board is about to start moving, and
            a player who looked away during the load should begin when they are looking. Coming
            BACK there is nothing to be ready for - the menu is where the app rests, so a screen
            saying READY in front of it is a door held shut for no reason. That is what made the
            loading window "appear again" on the way home; it now runs straight through.

            The tap IS an input, so unlike the completion above it goes through the same
            IsInputLive gate everything else on these pages does. That is not a formality: with
            RIDEV_INPUTSINK this app sees key-up events while unfocused, so a click landing NEXT
            to the window still raises this edge. Reading it ungated would let a click meant for
            another window dismiss the loading screen. See the note on IsInputLive in
            core/InputController.h.
        */
        const bool f_wait_for_tap = (loading_target.load() == BOMBER_LOADING_TO_GAME);
        if (!f_wait_for_tap || (input->IsInputLive() && f_tap)){
            const int finished = loading_target.exchange(BOMBER_LOADING_NONE);
            if (finished == BOMBER_LOADING_TO_GAME){
                RequestActiveScene(game_scene);
            }
            //MAIN either way. Going to the game it is what a later Escape lands on; coming back it
            //is the page itself. The quad stays hidden until its texture is resident, so setting
            //this before the scene switch cannot flash the splash - see ScaleTitleSplash.
            menu_page = BOMBER_PAGE_MAIN;
        }
        return;
    }

    //IsInputLive rather than HasFocus so a SCRIPTED click still works with the window in the
    //background, which is how this app is driven over MCP most of the time. A real click still
    //needs real focus; see the note on IsInputLive in core/InputController.h.
    if (!input->IsInputLive()){
        return;
    }

    /*
        In the game, Escape is the way back to the menu, and it works whether or not the
        simulation is running - see the note at the top about why this is not in the tick.

        The page is put back to the front one so the menu opens where it is expected to, not on
        whichever sub-page was last looked at before the game started.
    */
    if (!IsOnTitleScreen()){
        if (f_escape){
            /*
                Out through the loading screen, not straight to the menu: the board's atlases have
                to come off the device before the menu's two backgrounds can go back on. The page
                goes up now and the scene switch lands a pass or two later; StepLoading waits for
                the page rather than for the switch, so nothing is unloaded while the board is
                still being drawn.
            */
            f_loading_complete = false;
            loading_target = BOMBER_LOADING_TO_MENU;
            menu_page = BOMBER_PAGE_LOADING;
            RequestActiveScene(title_scene);
        }
        return;
    }

    if (menu_page != BOMBER_PAGE_MAIN){
        //Every sub-page is one window and one way out of it. Back also answers the case where a
        //page is opened and the window is resized under it, since nothing else can leave.
        if (f_back){
            menu_page = BOMBER_PAGE_MAIN;
        }
        return;
    }

    /*
        ESCAPE ON THE FRONT PAGE DOES NOTHING, and that is a deliberate stop rather than an
        omission.

        It quit the application here first, on the reasoning that Escape backs out one level and
        the level above the front page is the desktop. Testing it showed what that actually buys:
        Escape out of a game lands on this page, and a second press - which is an ordinary human
        habit, not a mistake - closes the whole app from under a player who was only backing out of
        a board. Losing a session to a doubled keypress is a bad trade for a shortcut that the
        title bar and Alt+F4 already provide.

        To make Escape quit from here after all, set main_window->f_should_quit = true. It has to
        be the flag rather than closing the window directly: this is the physics thread and the
        window belongs to the one pumping its messages.
    */

    if (f_start){
        /*
            Into the game THROUGH THE LOADING SCREEN, which is where the menu's two backgrounds
            come off the device and the board's four atlases go on. It used to switch the scene
            here and now that is the last thing the loading page does - see the block above, which
            is the one place either transition ends.

            The scene switch is still RequestActiveScene rather than a direct write when it comes:
            this runs on the physics thread and the switch is picked up at the top of the very next
            pass. See Application::ApplyPendingSceneSwitch.
        */
        f_loading_complete = false;
        loading_target = BOMBER_LOADING_TO_GAME;
        menu_page = BOMBER_PAGE_LOADING;
        return;
    }
    if (f_levels){
        menu_page = BOMBER_PAGE_LEVEL_SELECT;
    }else if (f_options){
        menu_page = BOMBER_PAGE_OPTIONS;
    }else if (f_scores){
        menu_page = BOMBER_PAGE_HIGH_SCORES;
    }
}

void ApplicationBomber::RunSimulationTick(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //Nothing of the board runs over a title card. The menu itself is driven from UpdateView, which
    //unlike this runs on paused passes too - see the note above UpdateMenu.
    if (IsOnTitleScreen()){
        return;
    }

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

    /*
        ONE OR THE OTHER, NEVER BOTH - see the note at the top of Hallway.h. That is what makes the
        corridor cost a branch rather than a second simulation, and it is why none of the board's
        rules have to know it exists.
    */
    if (f_in_hallway){
        hall.Tick(in);
        /*
            The commit is when the near door has FINISHED shutting, not when it starts: until it is
            shut the old board is still visible through the gap. Hallway reports the tick the player
            stepped off the threshold and this waits out the clip.
        */
        if (hall.f_sealed && !f_hall_committed){
            if (hall_commit_ticks == 0){
                //The tick the corridor sealed. Start the board falling away now rather than at the
                //commit - by the time RebuildField throws it out it has to be out of sight already,
                //or the throw is the pop this exists to avoid.
                board_sink_ticks = 0;
                board_sink_from = CellCentre(maze.door_x,maze.door_z);
            }
            hall_commit_ticks++;
            SinkBoard();
            if (hall_commit_ticks >= BOMBER_HALL_COMMIT_TICKS){
                CommitHallway();
            }
        }
        //After the commit the new board is climbing out of the floor behind the corridor's far
        //door. The sink above and this are never both running: one ends where the other begins.
        if (f_hall_committed){
            RiseBoard();
        }
        if (hall.f_finished && f_hall_committed){
            EndHallway();
        }
    }else{
        maze.Tick(in);
        /*
            Standing on the open exit is what starts a corridor. `step_ticks == 0` because `tile_x`
            is the DESTINATION of the step in progress - without it the corridor would begin while
            the player was still half a cell short of the doorway.
        */
        if (maze.player.f_alive && maze.player.step_ticks == 0 &&
            maze.player.tile_x == maze.door_x && maze.player.tile_z == maze.door_z){
            BeginHallway();
        }
    }

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
    //BEFORE the corridor branch, because it has to answer for whichever of the two owns the walker.
    PublishHUD();
#ifdef USE_SOUND
    //Likewise before it: the counters it watches belong to the board, and the corridor simply
    //leaves them all where they were.
    UpdateSound();
#endif
    /*
        In the corridor, the board is not being ticked and nothing on it can have moved - so it is
        left exactly as it was and only the corridor is brought up to date. After the commit there is
        no board left to update anyway.
    */
    if (f_in_hallway){
        SyncHallwayView();
        return;
    }
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
    //Lifted to file scope as BOMBER_WALKER_YAW, because the corridor points a walker with it too.
    const float* YAW = BOMBER_WALKER_YAW;

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
        Which clip the player is playing, derived from the rules exactly as the enemies' is one
        screen down - read every tick, compared against what is actually playing, and nothing kept
        in step by hand.

        WALK IF THERE IS ONE, otherwise the idle carries both states. That is not a placeholder for
        a missing feature, it is what having one clip means: the rig ships with `Character_Idle` and
        no walk, so today the player idles whether moving or not, and the moment a walk is exported
        under BOMBER_ANIM_CHAR_WALK this line starts choosing between them with nothing else to
        change. FindAnimation answering NULL is the whole test.

        NO DEATH CLIP EITHER, so a dead player holds the idle for MAZE_DEATH_TICKS rather than
        falling over the way an enemy does - see game_todo.md for what the rig still wants.
    */
    if (character){
        const MazeWalker& player = maze.player;
        if (!player.f_alive){
            /*
                DEATH IS SWITCHED TO, NOT BLENDED INTO, and it is the same call the enemies make for
                the same two reasons: blending into a death softens the one moment that should read
                as sudden, and blending OUT of it would be a corpse standing back up.

                SwitchToAnimation also REWINDS (it sets time_index to 0), which this clip needs and
                the looping two do not - a second death would otherwise open on the last frame of
                the first one, already flat on the floor.
            */
            if (player.death_ticks > 0 &&
                strcmp(character->CurrentAnimationName(),BOMBER_ANIM_CHAR_DEATH) != 0){
                character->SwitchToAnimation(BOMBER_ANIM_CHAR_DEATH);
                character->SetAnimationRate(1.0f);
            }
        }else{
            const char* clip = player.step_ticks > 0 ? BOMBER_ANIM_CHAR_WALK
                                                     : BOMBER_ANIM_CHAR_IDLE;
            //One comparison: CurrentAnimationName is what it is playing OR BECOMING, so a blend in
            //flight already reads as its destination.
            if (strcmp(character->CurrentAnimationName(),clip) != 0){
                character->TransitionToAnimation(clip);
                /*
                    THE RATE IS PER-OBJECT, NOT PER-CLIP, so it has to be set on every switch rather
                    than once at build time - leaving it alone would play the idle and the death at
                    the walk's 1.56x, which on a death is a body that falls over too fast to read.
                */
                character->SetAnimationRate(clip == BOMBER_ANIM_CHAR_WALK ? char_walk_rate : 1.0f);
            }
        }
    }
    /*
        The worn shield, which is the only feedback that a shield is running other than a number in
        a panel.

        A CHILD, so this flag is all there is to it - it is already in the right place and facing
        the right way, and a dead player draws no shield because nothing below an invisible object
        is drawn either.
    */
    if (shield_worn){
        shield_worn->SetVisibility(maze.player.shield_ticks > 0);
    }
    /*
        The door, derived from the rules rather than remembered - the same shape as the enemies'
        clip selection one screen down.

        Compared rather than set every tick because SetAnimationRate would otherwise wake the clip
        out of the PAUSED state it settles into at each end, every tick, for ever.
    */
    if (door_arch && f_door_open != maze.player.f_has_key){
        SetDoorOpen(maze.player.f_has_key);
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

#ifdef USE_SOUND
//--- sound --------------------------------------------------------------------------------------

/*
    The seven wavs, under names that say what they MEAN.

    RENDER THREAD, from Init. A name is registered against a FILE and several names may share one
    (core/SoundSystem.h), so naming by meaning costs nothing and leaves the call sites readable -
    "enemy_die" rather than "death.wav" at the point where an enemy dies.

    GAINS ARE AT THE CALL SITES, not here, because the same buffer is played at different volumes
    for different reasons: `death` is the player at full and nothing else, `pickup` is quieter for a
    coin than for the key. One number per file would have to be the loudest of those.
*/
void ApplicationBomber::LoadSounds(void){
    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    if (!soundsystem->f_initialised){
        //Not fatal, and deliberately not silent: a machine with no audio device is a perfectly good
        //place to develop the rest of the game, but a sound layer that failed to start and said
        //nothing looks exactly like one whose events never fire.
        debug->Warn("Sound: no device - bomber will run silent\n");
        return;
    }
    soundsystem->AppendFile("sound/explosion.wav",    "blast");
    soundsystem->AppendFile("sound/chop.wav",         "chop");
    soundsystem->AppendFile("sound/pickup.wav",       "pickup");
    soundsystem->AppendFile("sound/health.wav",       "health");
    soundsystem->AppendFile("sound/death.wav",        "death");
    soundsystem->AppendFile("sound/door_opening.wav", "door");
    soundsystem->AppendFile("sound/block_shift.wav",  "level_shift");
    debug->Ok("Sound ready - 7 clips\n");
}

void ApplicationBomber::PlaySound(const char* name, float gain){
    if (!f_sound_enabled || !soundsystem || !soundsystem->f_initialised){
        return;
    }
    //Fire and forget: the handle is dropped, the voice is recycled when the clip ends. Nothing in
    //this game holds a sound open - see SOUND_KEEP in core/SoundSystem.h for what would.
    soundsystem->Play(name,false,gain);
}

void ApplicationBomber::ResetSoundWatch(void){
    sound_watch.blast_count = maze.blast_count;
    sound_watch.blocks_cut = maze.blocks_cut;
    sound_watch.items_taken = maze.items_taken;
    sound_watch.deaths = maze.deaths;
    sound_watch.health = maze.player.health;
    sound_watch.chopping = 0;
}

/*
    What changed since last tick, as noise. PHYSICS THREAD, from the top of SyncView.

    ONE THREAD EVER CALLS Play. SoundSystem has no lock of its own, and every call site in this app
    is downstream of RunSimulationTick - so the ImGui panel's sound checkbox sets a bool and never
    plays anything itself. That is the same arrangement apps/breakout keeps.

    Each test is `>` rather than `!=` on purpose: a counter that has gone BACKWARDS means the board
    was laid out again underneath the watch, and the right response to that is silence rather than a
    noise. ResetSoundWatch should have made it impossible, and this is what happens if it did not.
*/
void ApplicationBomber::UpdateSound(void){
    /*
        THE BOARD IS NOT TICKING IN THE CORRIDOR, so none of these counters can move there and the
        whole function is a handful of comparisons that find nothing. The corridor's own noises -
        its doors, and the level shifting - are played where they happen, because they are the app's
        events and not the rules'.
    */
    if (maze.blast_count > sound_watch.blast_count){
        //The loudest thing in the game, and the one the player asked for.
        PlaySound("blast",0.85f);
    }
    /*
        A hedge falling to an enemy's shears. THE START OF THE CUT, not the end: MAZE_CHOP_TICKS is
        90 ticks and the clip is 0.65 s, so playing it when the hedge finally goes would be a sound
        that arrives a second and a half after the thing it belongs to.

        Counted rather than edge-detected per enemy, because what is wanted is "another one started"
        and the enemies are interchangeable. One starting on the same tick another finishes is
        missed, which at four enemies on a sixteen-wide board is not a case worth a bitmask.
    */
    int chopping = 0;
    for (int i = 0; i < maze.num_enemies; i++){
        if (maze.enemy[i].f_alive && maze.enemy[i].chop_ticks > 0){
            chopping++;
        }
    }
    if (chopping > sound_watch.chopping){
        PlaySound("chop",0.55f);
    }
    /*
        A pickup, and WHICH ONE is answered by what moved rather than by asking Maze.

        `items_taken` says something was collected; the health clip is for the one that gave a life
        back. A health pickup taken at full health raises nothing (Maze caps rather than banks it),
        so it correctly falls through to the ordinary pickup noise - which is the right sound for
        something that was wasted.
    */
    if (maze.items_taken > sound_watch.items_taken){
        if (maze.player.health > sound_watch.health){
            PlaySound("health",0.75f);
        }else{
            PlaySound("pickup",0.70f);
        }
    }
    /*
        The player dying. THE PLAYER ONLY.

        An enemy death would be the same clip, and it is deliberately not played: an enemy can only
        die to a blast, so it always lands on the same tick as `blast` - a 1.87 s explosion - and up
        to four of them at once. It would be four voices spent on something nothing can hear. See
        the sound gaps in game_todo.md, where "an enemy dying" is listed as wanting a clip of its
        own rather than this one.
    */
    if (maze.deaths > sound_watch.deaths){
        PlaySound("death",1.0f);
    }

    sound_watch.blast_count = maze.blast_count;
    sound_watch.blocks_cut = maze.blocks_cut;
    sound_watch.items_taken = maze.items_taken;
    sound_watch.deaths = maze.deaths;
    sound_watch.health = maze.player.health;
    sound_watch.chopping = chopping;
}
#endif

//--- the HUD ------------------------------------------------------------------------------------

/*
    The palette, in one place so the HUD reads as one thing.

    Every colour here is also a STATE, which is why the lost-life and no-key colours exist at all:
    an icon that disappears when you lose the thing it stands for tells you nothing about what you
    have lost, and on a key it does not even tell you there was a key to find. So nothing is ever
    removed from this HUD - it is drawn dim instead.
*/
#define BOMBER_HUD_LIFE        UIColor(236, 72, 76,255)
#define BOMBER_HUD_LIFE_LOST   UIColor(236, 72, 76, 70)
#define BOMBER_HUD_KEY         UIColor(255,198, 62,255)
#define BOMBER_HUD_KEY_NONE    UIColor(210,215,230, 55)
#define BOMBER_HUD_SHIELD      UIColor(104,198,255,255)
#define BOMBER_HUD_SHIELD_BACK UIColor(104,198,255, 55)
#define BOMBER_HUD_TEXT        UIColor(236,242,255,235)
#define BOMBER_HUD_TEXT_DIM    UIColor(200,212,236,150)
#define BOMBER_HUD_PANEL       UIColor(  8, 12, 20,120)
//One full breath of the loading screen's "TAP TO START", in TICKS - 60 at this app's 60 tps, so
//one second. Ticks because that is the unit every duration here is in; see DrawMenu.
#define BOMBER_TAP_PULSE_TICKS 60

/*
    Publishes what the HUD draws. PHYSICS THREAD, from the top of SyncView.

    BEFORE THE CORRIDOR BRANCH ON PURPOSE - this is the one place that knows which of `maze` and
    `hall` currently owns the walker, and answering it here is what keeps the render thread from
    having to know the corridor exists at all. See BomberHUD.
*/
void ApplicationBomber::PublishHUD(void){
    const MazeWalker& walker = f_in_hallway ? hall.player : maze.player;
    hud.health       = walker.health;
    hud.shield_ticks = walker.shield_ticks;
    hud.invuln_ticks = walker.invuln_ticks;
    hud.f_has_key    = walker.f_has_key;
    hud.f_alive      = walker.f_alive;
    hud.score        = walker.score;
    /*
        In the corridor the clock and the bonus are the ones BANKED at BeginHallway, not the live
        board's - `maze` has been laid out again by the commit and its own clock restarted. That is
        the whole reason hall_level_ticks exists.
    */
    hud.f_tally      = f_in_hallway;
    hud.level_ticks  = f_in_hallway ? hall_level_ticks : maze.level_ticks;
    hud.time_bonus   = f_in_hallway ? hall_time_bonus : 0;
}

/*
    A key, out of four rounded rectangles: a ring, a stem, and two teeth.

    BECAUSE THERE IS NO SPRITE TO DRAW. UIOverlay binds ONE texture for the whole batch - the R8
    font atlas - and every quad's UV points into it, so an arbitrary image cannot be drawn at all
    today (see core/UIOverlay.h and shared_assets/shaders/ui_overlay.frag). What the overlay is
    very good at instead is analytic rounded boxes at any size with exact corners, so an icon that
    is a few of those costs one draw call like everything else and stays sharp at any HUD scale.

    A capital 'K' would have been one call. It would also have read as a letter rather than as a
    thing you are carrying, which on the one HUD element that answers "can I leave yet" is worth
    four quads.
*/
void ApplicationBomber::DrawKeyIcon(vec2 centre, float h, uint32_t color){
    if (!overlay){
        return;
    }
    //The bow - a circle, which is a rounded rect whose radius is half its side.
    float ring = h * 0.42f;
    vec2 ring_c = vec2(centre.x - h * 0.28f,centre.y);
    overlay->AddRectOutline(vec2(ring_c.x - ring,ring_c.y - ring),
                            vec2(ring_c.x + ring,ring_c.y + ring),
                            ring,h * 0.15f,color);
    /*
        The shank, running right from the bow.

        It STARTS AT THE RING'S EDGE, not at its centre - a shank that begins inside draws a stroke
        across the hole and the whole thing stops reading as a key and starts reading as a lollipop.
        0.9 rather than 1.0 so the two overlap by a hair and no seam shows between them.
    */
    float shank = h * 0.11f;
    overlay->AddRect(vec2(ring_c.x + ring * 0.9f,centre.y - shank),
                     vec2(centre.x + h * 0.66f, centre.y + shank),
                     shank,color);
    //Two teeth on the underside of the far end. Different lengths, because a key with matching
    //teeth reads as a comb.
    for (int i = 0; i < 2; i++){
        float x = centre.x + h * (0.34f + 0.20f * (float)i);
        float drop = h * (i == 0 ? 0.34f : 0.24f);
        overlay->AddRect(vec2(x - shank,centre.y),
                         vec2(x + shank,centre.y + drop),
                         shank,color);
    }
}


/*
    THE FRONT PAGE'S FOUR BUTTONS, drawn by the overlay from the theme's `button` role.

    THEY USED TO BE PAINTED INTO THE SPLASH and this table used to be their pixel positions in it,
    measured off the artwork with tools/ui_extract_probe.py. That is gone with the artwork: the
    picture a painted button lives in cannot be cropped, because cropping slides the paint out
    from under the rectangle that answers for it, and cropping is exactly what the full-bleed
    variants do. images/splash.jpg is still in the tree as the artist's reference for what these
    should look like; nothing loads it.

    So the rects are CHOSEN now rather than measured, which is what lets the same four buttons sit
    in a row under a landscape picture and in a column down a portrait one.
*/
static const char* const bomber_main_button_label[4] = {
    "START","LEVELS","OPTIONS","SCORES"
};

/*
    The row and the column, as fractions of the visible artwork.

    Two sets rather than one scaled set, because a button that is comfortable in a row of four is
    not the same shape as one in a column of four - the row is constrained by width and the column
    by height, and a single set of numbers would have to lose one of those arguments.
*/
#define BOMBER_BTN_ROW_W       0.20f    //each button, of the content width
#define BOMBER_BTN_ROW_H       0.095f   //of the content height
#define BOMBER_BTN_ROW_GAP     0.025f   //between them, of the content width
#define BOMBER_BTN_ROW_BOTTOM  0.055f   //from the bottom of the content, of its height

#define BOMBER_BTN_COL_W       0.62f    //of the content width
#define BOMBER_BTN_COL_H       0.075f   //of the content height
#define BOMBER_BTN_COL_GAP     0.028f   //of the content height
#define BOMBER_BTN_COL_BOTTOM  0.055f

void ApplicationBomber::LayoutMainButtons(int w, int h, vec2* out_min, vec2* out_max) const{
    vec2 cmin,cmax;
    GetTitleContentRect(w,h,&cmin,&cmax);
    const float cw = cmax.x - cmin.x;
    const float ch = cmax.y - cmin.y;

    const bool f_stack = (active_splash >= 0) && splash_variant[active_splash].f_stack_buttons;

    if (f_stack){
        const float bw = BOMBER_BTN_COL_W * cw;
        const float bh = BOMBER_BTN_COL_H * ch;
        const float gap = BOMBER_BTN_COL_GAP * ch;
        const float total = 4.0f * bh + 3.0f * gap;
        const float x = cmin.x + (cw - bw) * 0.5f;
        //Measured UP from the bottom, not down from the top of the stack, so the lowest button
        //keeps its margin whatever the stack's height works out to.
        const float y0 = cmax.y - BOMBER_BTN_COL_BOTTOM * ch - total;
        for (int i = 0; i < 4; i++){
            const float y = y0 + (float)i * (bh + gap);
            out_min[i] = vec2(x,y);
            out_max[i] = vec2(x + bw,y + bh);
        }
        return;
    }

    const float bw = BOMBER_BTN_ROW_W * cw;
    const float bh = BOMBER_BTN_ROW_H * ch;
    const float gap = BOMBER_BTN_ROW_GAP * cw;
    const float total = 4.0f * bw + 3.0f * gap;
    const float x0 = cmin.x + (cw - total) * 0.5f;
    const float y = cmax.y - BOMBER_BTN_ROW_BOTTOM * ch - bh;
    for (int i = 0; i < 4; i++){
        const float x = x0 + (float)i * (bw + gap);
        out_min[i] = vec2(x,y);
        out_max[i] = vec2(x + bw,y + bh);
    }
}

int ApplicationBomber::PickSplashVariant(int w, int h) const{
    if (h <= 0){
        return (BOMBER_SPLASH_VARIANT_COUNT > 0) ? BOMBER_SPLASH_VARIANT_COUNT - 1 : -1;
    }
    const float aspect = (float)w / (float)h;
    //The HIGHEST qualifying entry, not the first that happens to match: the table is widest-first
    //so this is the earliest row, but saying "highest" is what makes the rule survive a reorder.
    int best = -1;
    for (int i = 0; i < BOMBER_SPLASH_VARIANT_COUNT; i++){
        if (aspect < splash_variant[i].min_aspect){
            continue;
        }
        if ((best < 0) || (splash_variant[i].min_aspect > splash_variant[best].min_aspect)){
            best = i;
        }
    }
    //Only reachable if no row has min_aspect 0, which would be a table with no catch-all.
    return (best >= 0) ? best : BOMBER_SPLASH_VARIANT_COUNT - 1;
}

void ApplicationBomber::EnsureSplashVariant(void){
    if (!title_splash || !main_window){
        return;
    }
    /*
        ONLY ON THE TITLE SCREEN, AND ONLY BETWEEN LOADS.

        A resize during play would otherwise UPLOAD a splash while the board's four atlases are
        resident - six textures against five units, which is the whole thing the loading screen
        exists to prevent, arrived at through the back door. While the game is up the menu's
        textures are meant to be gone and this has nothing to do; BuildLoadingSteps re-picks the
        variant on the way back, so a window reshaped mid-game is still handled, just later.
    */
    if (!IsOnTitleScreen() || (menu_page_applied == BOMBER_PAGE_LOADING)
        || (loading_target.load() != BOMBER_LOADING_NONE)){
        return;
    }

    const int want = PickSplashVariant(main_window->width,main_window->height);
    if ((want < 0) || (want == active_splash)){
        //Still re-assert the material: ApplyMenuPage only runs on a page CHANGE, and the first
        //frame of all has no page change to hang this on.
        if ((want >= 0) && (menu_page_applied == BOMBER_PAGE_MAIN)
            && (splash_variant[want].material >= 0)
            && (title_splash->GetMaterialSlot(0) != splash_variant[want].material)){
            title_splash->SetMaterialSlot(0,splash_variant[want].material);
        }
        return;
    }

    //Drop before raise, for the reason BuildLoadingSteps spells out: the two must never both be
    //resident, because on the device there is no room for both.
    for (int i = 0; i < BOMBER_SPLASH_VARIANT_COUNT; i++){
        if ((i != want) && splash_variant[i].texture){
            splash_variant[i].texture->Unload();
        }
    }
    active_splash = want;
    if (splash_variant[want].texture && !splash_variant[want].texture->IsResident()){
        splash_variant[want].texture->ReUploadTexture();
    }
    if ((menu_page_applied == BOMBER_PAGE_MAIN) && (splash_variant[want].material >= 0)){
        title_splash->SetMaterialSlot(0,splash_variant[want].material);
    }
    //The arrangement is a property of the variant - a row becomes a column here - so the rects
    //have to be rebuilt with it rather than waiting for the next resize.
    LayoutMenuButtons(main_window->width,main_window->height,menu_page_applied);

    debug->Info("Splash variant: %s (%.3f aspect, %s)\n",
                splash_variant[want].asset,
                (float)main_window->width / (float)(main_window->height ? main_window->height : 1),
                splash_variant[want].f_stack_buttons ? "buttons stacked" : "buttons in a row");
}

/*
    Sizes the splash quad to the content rect. RENDER THREAD, from PreRender, every frame.

    UNCONDITIONAL, like SetCustomShaderScale beside it: the work is two divides and a compare, and
    a cached surface size here would be a second copy of the one Application::DrawFrame already
    keeps for the touch rects - two caches that have to be invalidated by the same event, which is
    how a resize ends up moving the buttons and not the art.

    Writing to a scene object from the render thread is the same liberty ApplyMenuPage already
    takes with this object, and it is safe for the same reason: the title scene has no physics
    world, so nothing on the physics thread is reading this transform.
*/
void ApplicationBomber::ScaleTitleSplash(void){
    if (!title_splash || !renderer){
        return;
    }
    const int vw = renderer->GetViewportWidth();
    const int vh = renderer->GetViewportHeight();
    if ((vw <= 0) || (vh <= 0)){
        return;
    }

    //The DRAW rect, not the visible one: under COVER the quad is meant to hang off the edges,
    //and scaling it to the clipped rect would squeeze the whole picture into the window - which
    //is a stretch, and exactly what the crop exists to avoid.
    vec2 cmin,cmax;
    GetSplashDrawRect(vw,vh,&cmin,&cmax);

    /*
        Pixels to the camera's world units. The viewport is x in [-aspect,aspect] by y in [-1,1]
        (see CreateTitleScene), so a rect that is `f` of the viewport's width is f*aspect in half-
        width, and one that is `g` of its height is g in half-height. The 2x2 quad's half extents
        are 1, so those fractions ARE the scale.
    */
    const float aspect = (float)vw / (float)vh;
    const float sx = aspect * ((cmax.x - cmin.x) / (float)vw);
    const float sy = (cmax.y - cmin.y) / (float)vh;
    title_splash->SetScale(vec3(sx,sy,1.0f));

    /*
        THE QUAD IS VISIBLE EXACTLY WHEN ITS TEXTURE IS THERE, asked every frame rather than set
        when the page changes.

        Which is what makes the loading screen black without a case for it anywhere: that page's
        first steps unload both backgrounds, the quad's material stops being resident, and it
        stops being drawn. Hiding it from ApplyMenuPage instead would need the same knowledge in
        two places and would get the ORDER wrong in the other direction - the page becomes MAIN
        before the splash has finished uploading, and an unloaded material draws in its flat
        colour, which for this one is white. A full-screen white flash on the way back from every
        game is not a subtle bug, but it is an easy one to write.
    */
    const Material* mat = renderer->GetMaterial(title_splash->GetMaterialSlot(0));
    title_splash->SetVisibility((mat != NULL) && (mat->diff_texture != NULL)
                                && mat->diff_texture->IsResident());
}

/*
    Splits every material texture the renderer holds into the menu's and the game's.

    THE MENU'S ARE NAMED AND THE GAME'S ARE WHATEVER IS LEFT, rather than both being lists.
    A second list would be a list to forget: the board's atlases arrive with the GLB, so adding a
    fifth one to the art would silently leave it loaded across the menu - which on the device is
    exactly one unit too many, and shows up as some other surface losing its texture rather than
    as anything to do with the new one.

    THREE OUTCOMES, NOT TWO, and the third is easy to miss. A splash variant that is NOT the
    active one belongs to neither set: the menu does not want it (only one shape of screen is
    being drawn) and the game must not upload it (that is the unit this whole arrangement is
    saving). It is parked in RAM and stays there until the window changes shape - see
    EnsureSplashVariant - so it is skipped here rather than falling through into `game`, which is
    where it would land as "whatever is left".

    Distinct pointers only. Materials share textures here (the four tile atlases cover eleven
    materials), and unloading the same texture twice is harmless but uploading it twice is a
    wasted upload and a wasted step on the bar.
*/
void ApplicationBomber::CollectPageTextures(std::vector<Texture*>& menu,
                                            std::vector<Texture*>& game) const{
    menu.clear();
    game.clear();
    if ((active_splash >= 0) && splash_variant[active_splash].texture){
        menu.push_back(splash_variant[active_splash].texture);
    }
    if (title_texture_menu){
        menu.push_back(title_texture_menu);
    }

    const int count = renderer ? renderer->GetNumMaterials() : 0;
    for (int i = 0; i < count; i++){
        Material* mat = renderer->GetMaterial(i);
        if (!mat){
            continue;
        }
        Texture* both[2] = {mat->diff_texture,mat->norm_texture};
        for (int t = 0; t < 2; t++){
            Texture* tex = both[t];
            if (!tex){
                continue;
            }
            //Any variant, active or not - see the third outcome above. The active one is already
            //in `menu` and the rest belong in neither list.
            bool f_variant = false;
            for (int v = 0; v < BOMBER_SPLASH_VARIANT_COUNT; v++){
                if (splash_variant[v].texture == tex){
                    f_variant = true;
                    break;
                }
            }
            if (f_variant){
                continue;
            }
            bool f_seen = (tex == title_texture_menu);
            for (Texture* g:game){
                if (g == tex){
                    f_seen = true;
                    break;
                }
            }
            if (!f_seen){
                game.push_back(tex);
            }
        }
    }
}

/*
    DROP FIRST, THEN BRING UP, and that order is the whole reason this is a list rather than two
    loops run back to back.

    The set being dropped and the set being raised cannot both be resident - that is the situation
    the loading screen exists for - so doing it the other way round asks the device for six units
    when it has five, and the last upload lands nowhere. Unloading first means the peak is never
    higher than either set alone.
*/
void ApplicationBomber::BuildLoadingSteps(bomber_loading_target target){
    loading_steps.clear();
    loading_step = 0;

    /*
        RE-PICK THE VARIANT BEFORE COLLECTING, on the way back to the menu.

        EnsureSplashVariant deliberately does nothing while the game is up, so a window reshaped
        across 1:1 mid-game leaves active_splash pointing at the wrong picture. Choosing here, on
        the index only, means the upload step below raises the RIGHT one - where fixing it
        afterwards would upload one splash, then immediately unload it and upload the other.

        Index only: no GL. Both variants are unloaded at this point, which is what makes moving
        the index free.
    */
    if ((target == BOMBER_LOADING_TO_MENU) && main_window){
        const int want = PickSplashVariant(main_window->width,main_window->height);
        if (want >= 0){
            active_splash = want;
        }
    }

    std::vector<Texture*> menu;
    std::vector<Texture*> game;
    CollectPageTextures(menu,game);

    const std::vector<Texture*>& drop = (target == BOMBER_LOADING_TO_GAME) ? menu : game;
    const std::vector<Texture*>& raise = (target == BOMBER_LOADING_TO_GAME) ? game : menu;

    for (Texture* tex:drop){
        bomber_load_step step;
        step.texture = tex;
        step.f_upload = false;
        loading_steps.push_back(step);
    }
    for (Texture* tex:raise){
        bomber_load_step step;
        step.texture = tex;
        step.f_upload = true;
        loading_steps.push_back(step);
    }

    debug->Info("Loading screen: %i to unload, %i to upload, going to the %s\n",
                (int)drop.size(),(int)raise.size(),
                (target == BOMBER_LOADING_TO_GAME) ? "game" : "menu");
}

float ApplicationBomber::GetLoadingProgress(void) const{
    if (loading_steps.empty()){
        //Not "nothing done" but "nothing to do" - a full bar, so a transition with no work does
        //not flash an empty one on its way past.
        return 1.0f;
    }
    float p = (float)loading_step / (float)loading_steps.size();
    return (p < 0.0f) ? 0.0f : ((p > 1.0f) ? 1.0f : p);
}

void ApplicationBomber::StepLoading(void){
    if (loading_target.load() == BOMBER_LOADING_NONE){
        //The physics thread has taken the result and moved on. Dropping the list here rather than
        //when it completes is what makes GetLoadingProgress keep reading 1.0 for the frames
        //between the last step and the page actually changing.
        loading_steps.clear();
        loading_step = 0;
        return;
    }
    /*
        NOT UNTIL THE LOADING PAGE IS THE THING ON SCREEN.

        Coming back from the game the physics thread asks for the page and the scene switch in the
        same breath, and the switch is applied at the top of a later pass - so for a frame or two
        the GAME is still what is being drawn. Unloading its atlases during those frames would
        flatten the whole board to its base colours in full view. menu_page_applied is the render
        thread's own record of what it last drew, which makes it the right thing to gate on.
    */
    if (menu_page_applied != BOMBER_PAGE_LOADING){
        return;
    }
    if (f_loading_complete.load()){
        return;
    }
    if (loading_steps.empty()){
        BuildLoadingSteps((bomber_loading_target)loading_target.load());
    }

    if (loading_step < (int)loading_steps.size()){
        const bomber_load_step& step = loading_steps.at(loading_step);
        if (step.texture){
            if (step.f_upload){
                //Create2D + UploadTexture against the pixels that never left RAM - no decode, no
                //file read. See Texture::Unload for why they are still there.
                step.texture->ReUploadTexture();
            }else{
                step.texture->Unload();
            }
        }
        loading_step++;
    }
    if (loading_step >= (int)loading_steps.size()){
        f_loading_complete = true;
    }
}

/*
    See the header. Which background, and which fit, comes off the QUAD.

    THE SPLASH IS NOT THE ONLY THING THIS SHAPES. The sub-pages put the dungeon on the same quad,
    and it is a different picture with a different shape and a different answer - 1195x896 against
    the wide splash's 1568x672. Shaping it to the splash's numbers, which is what reading
    active_splash unconditionally did, stretched the dungeon to 2.33 and cropped most of it away
    on every sub-page.
*/
bool ApplicationBomber::CurrentBackground(Texture** out_texture, bomber_splash_fit* out_fit) const{
    if (out_texture){
        *out_texture = NULL;
    }
    if (out_fit){
        //The dungeon's mode, and the safe one generally: showing all of a picture cannot hide
        //anything, where a wrong HEIGHT would silently crop.
        *out_fit = BOMBER_FIT_CONTAIN;
    }
    if (!title_splash || !renderer){
        return false;
    }
    const Material* mat = renderer->GetMaterial(title_splash->GetMaterialSlot(0));
    if (!mat || !mat->diff_texture){
        return false;
    }
    Texture* tex = mat->diff_texture;
    if (out_texture){
        *out_texture = tex;
    }
    //A splash variant carries its own mode; anything else on this quad is the dungeon, which
    //CONTAINs. One loop rather than a flag on the material, because the material is the
    //renderer's and this is the app's opinion about it.
    for (int i = 0; i < BOMBER_SPLASH_VARIANT_COUNT; i++){
        if ((splash_variant[i].texture == tex) && out_fit){
            *out_fit = splash_variant[i].fit;
            break;
        }
    }
    return true;
}

/*
    See the header. Both modes scale the picture UNIFORMLY and differ only in which axis picks
    the scale - CONTAIN takes the smaller of the two, HEIGHT takes the height's and ignores the
    other. Two lines, so there is nothing for them to drift apart on.
*/
void ApplicationBomber::GetSplashDrawRect(int w, int h, vec2* out_min, vec2* out_max) const{
    const float fw = (float)w;
    const float fh = (float)h;

    //With nothing on the quad, or a texture that failed to load, the picture IS the surface: no
    //bands, no crop, and whatever flat colour the material has fills it.
    float aw = fw;
    float ah = fh;
    Texture* tex = NULL;
    bomber_splash_fit fit = BOMBER_FIT_CONTAIN;
    if (CurrentBackground(&tex,&fit) && tex && (tex->width > 0) && (tex->height > 0)){
        aw = (float)tex->width;
        ah = (float)tex->height;
    }
    if ((aw <= 0.0f) || (ah <= 0.0f)){
        aw = fw;
        ah = fh;
    }

    const float sx = fw / aw;
    const float sy = fh / ah;
    const float scale = (fit == BOMBER_FIT_CONTAIN) ? ((sx < sy) ? sx : sy) : sy;
    const float cw = aw * scale;
    const float ch = ah * scale;

    //Centred, so bands are even on both sides and a crop takes the same from each. Halves rather
    //than all of it at one edge because a picture pinned to a corner reads as a broken layout,
    //and because the middle of this artwork is where everything worth seeing is.
    const float cx = (fw - cw) * 0.5f;
    const float cy = (fh - ch) * 0.5f;
    if (out_min){
        *out_min = vec2(cx,cy);
    }
    if (out_max){
        *out_max = vec2(cx + cw,cy + ch);
    }
}

/*
    The VISIBLE artwork: the draw rect clipped to the surface.

    Under CONTAIN the two are the same rect and this costs nothing. Under COVER the draw rect
    hangs off the edges and this is the window - which is the answer a layout wants, because a
    button placed against the overhang would be placed off-screen.
*/
void ApplicationBomber::GetTitleContentRect(int w, int h, vec2* out_min, vec2* out_max) const{
    vec2 dmin,dmax;
    GetSplashDrawRect(w,h,&dmin,&dmax);

    const float x0 = (dmin.x > 0.0f) ? dmin.x : 0.0f;
    const float y0 = (dmin.y > 0.0f) ? dmin.y : 0.0f;
    const float x1 = (dmax.x < (float)w) ? dmax.x : (float)w;
    const float y1 = (dmax.y < (float)h) ? dmax.y : (float)h;
    if (out_min){
        *out_min = vec2(x0,y0);
    }
    if (out_max){
        *out_max = vec2(x1,y1);
    }
}

//The sub-pages' window, as fractions of the CONTENT RECT rather than of the surface - so the
//panel sits on the background art the same way at every window shape, instead of drifting across
//it as the bands grow. One panel, centred, with room under it for the one button that leaves.
#define BOMBER_MENU_PANEL_X 0.22f
#define BOMBER_MENU_PANEL_Y 0.20f
#define BOMBER_MENU_PANEL_W 0.56f
#define BOMBER_MENU_PANEL_H 0.52f
#define BOMBER_MENU_BACK_W  0.16f
#define BOMBER_MENU_BACK_H  0.085f

void ApplicationBomber::LayoutMenuButtons(int w, int h, bomber_menu_page page){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;
    /*
        EVERY rect below is placed in the CONTENT RECT, not in the window. On the front page that
        is not a preference: those four buttons are painted into the splash, and the splash is now
        letterboxed, so a rect measured against the window would sit in the black band at any shape
        but the art's own. The sub-pages follow the same rect so that the panel, the button that
        leaves it and the background behind them all move together.
    */
    vec2 cmin,cmax;
    GetTitleContentRect(w,h,&cmin,&cmax);
    const float fw = cmax.x - cmin.x;
    const float fh = cmax.y - cmin.y;

    /*
        EVERY button is written every time, and the ones this page does not show are written EMPTY.

        Not an optimisation to skip them: a button left at its old rect is still hit-tested, so the
        four front-page buttons would still be live behind the options window, and clicking where
        OPTIONS used to be would re-open it from inside itself. Nothing on screen would explain it.
        Writing all five from one table is what makes that unrepresentable.
    */
    InputController::TouchRect rect[BOMBER_MENU_BUTTON_COUNT];

    if (page == BOMBER_PAGE_MAIN){
        //Through the same function DrawMenu reads these rects back for, so the artwork and the
        //hit test cannot disagree about where a button is - see LayoutMainButtons.
        vec2 bmin[4];
        vec2 bmax[4];
        LayoutMainButtons(w,h,bmin,bmax);
        for (int i = 0; i < 4; i++){
            rect[i].x = bmin[i].x;
            rect[i].y = bmin[i].y;
            rect[i].w = bmax[i].x - bmin[i].x;
            rect[i].h = bmax[i].y - bmin[i].y;
        }
    }else if (page == BOMBER_PAGE_LOADING){
        /*
            TAP ANYWHERE, and anywhere means the WINDOW rather than the content rect - the one
            rect on these pages that deliberately ignores the letterbox.

            Everything else here is placed against the artwork because it has to line up with
            something painted into it. This lines up with nothing: it is the whole screen saying
            "go on then", and a player who taps in a black band and gets nothing has been told the
            app is still busy. The bands are not out of bounds, they are just empty.

            Live on this page ONLY, and only meaningful once the work is done - UpdateMenu checks
            f_loading_complete before it reads the edge. A rect that is live while the bar is
            still moving simply cannot do anything, which is a better shape than one that appears
            part way through and leaves the player unsure whether the first tap counted.
        */
        rect[5].x = 0.0f;
        rect[5].y = 0.0f;
        rect[5].w = (float)w;
        rect[5].h = (float)h;
    }else if (page != BOMBER_PAGE_NONE){
        //Back sits centred just inside the bottom of the window it leaves.
        rect[4].w = BOMBER_MENU_BACK_W * fw;
        rect[4].h = BOMBER_MENU_BACK_H * fh;
        rect[4].x = cmin.x + (fw - rect[4].w) * 0.5f;
        rect[4].y = cmin.y + (BOMBER_MENU_PANEL_Y + BOMBER_MENU_PANEL_H) * fh - rect[4].h * 1.6f;
    }

    for (int i = 0; i < BOMBER_MENU_BUTTON_COUNT; i++){
        if (menu_button[i] >= 0){
            input->SetTouchButtonRect(menu_button[i],rect[i]);
        }
    }
}

void ApplicationBomber::LayoutTouchButtons(int w, int h){
    //The page the RENDER thread last applied, not the live one - this is the same surface those
    //rects were computed for, and taking the live page here would put a resize and a page change
    //into two different orders depending on which thread won.
    LayoutMenuButtons(w,h,menu_page_applied);
    //BOMBER_PAGE_NONE IS the game - see ApplyMenuPage, which is the only thing that sets it.
    LayoutGameButtons(w,h,menu_page_applied == BOMBER_PAGE_NONE);
}

/*
    See the header. Anchored to the two bottom corners and the top-left, never laid out from one
    origin: the board fills the middle at every window size, and anchoring each cluster to its own
    edge is what keeps the thumbs off it whatever that size is.
*/
void ApplicationBomber::LayoutGameButtons(int w, int h, bool f_playing){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    //Nothing was bound, so there is nothing to place - see the USE_TOUCH_UI block in SetupInput.
    //An early return rather than seven ignored SetTouchButtonRect calls, because the geometry
    //below is a page of millimetre arithmetic that would be computed and thrown away every resize.
    if (touch_bomb < 0){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    const InputController::TouchRect nowhere;
    if (!f_playing){
        //Every one of them, every time. See the header on why an unplaced rect is worse than an
        //invisible one.
        const int all[] = {touch_menu,touch_pause,touch_north,touch_south,
                           touch_west,touch_east,touch_bomb};
        for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++){
            if (all[i] >= 0){
                input->SetTouchButtonRect(all[i],nowhere);
            }
        }
        return;
    }

    /*
        SIZED IN MILLIMETRES, AND ANCHORED TO THE SAFE AREA RATHER THAN TO THE WINDOW.

        Both halves were learned on hardware and neither is optional:

          - A thumb is about 10 mm wherever it is, so a touch target has to be a REAL size. The
            two Android devices this runs on are 160 and 440 dpi, which makes one 11 mm button
            69 px on the first and 190 px on the second. Any fixed pixel count is right on at
            most one of them - 88 px looked right at 160 dpi and is a 5 mm target at 440.
          - The surface spans system furniture that eats touches. The same phone in landscape
            reports 130 px of unsafe strip down its right edge, most of one button's width, and
            a button out there is drawn, looks live and does nothing.

        See Application::GetDisplayDPI and Application::GetSafeArea for both numbers.
    */
    const float mm    = GetDisplayDPI() / 25.4f;
    const float s     = 11.0f * mm;     //a comfortable thumb target
    const float g     =  2.0f * mm;     //gap
    const float m     =  4.0f * mm;     //inset from the safe edge
    const float small =  8.0f * mm;     //chrome, smaller on purpose - see the header

    float sx,sy,sw,sh;
    GetSafeArea(sx,sy,sw,sh);
    /*
        Clamped to the surface THIS call was made for. GetSafeArea reads the window, and on the
        frame a resize is being applied the window and the size handed in here can disagree - for
        exactly as long as it takes a button to be off the edge and noticed.
    */
    if (sx + sw > (float)w){ sw = (float)w - sx; }
    if (sy + sh > (float)h){ sh = (float)h - sy; }

    const float left  = sx;
    const float right = sx + sw;
    const float lower = sy + sh - m - s;            //bottom row of each thumb cluster
    const float upper = lower - g - s;              //the one stacked above it

    /*
        LEFT THUMB: WEST in the corner with NORTH directly above it. RIGHT THUMB mirrors it -
        EAST in the corner with SOUTH above - and BOMB sits INBOARD of EAST on the bottom row.

        The pairing is deliberate and it is not a d-pad. Each thumb owns one horizontal direction
        and one vertical, so no direction needs a hand to cross the screen, and the two that share
        a thumb are at right angles rather than opposite - which is what stops a slipped press
        turning the player back the way they came.
    */
    input->SetTouchButtonRect(touch_west, {left + m,lower,s,s});
    input->SetTouchButtonRect(touch_north,{left + m,upper,s,s});

    input->SetTouchButtonRect(touch_east, {right - m - s,lower,s,s});
    input->SetTouchButtonRect(touch_south,{right - m - s,upper,s,s});
    input->SetTouchButtonRect(touch_bomb, {right - m - s - g - s,lower,s,s});

    /*
        THE CHROME COLUMN, under the lives row on the left.

        Placed off the HUD's own unit rather than off `m`, because what it has to clear is the HUD
        and not the window: DrawOverlay sizes the pips and the shield bar in multiples of
        BOMBER_HUD_UNIT x height, so anything measured another way drifts across them at some
        window size and eventually sits on one.

        The shield bar's slot is reserved even though the bar is only drawn sometimes - a button
        that moves down when the player picks up a shield is worse than one placed slightly low.
    */
    const float u = (float)h * BOMBER_HUD_UNIT;
    const float hud_margin = u * BOMBER_HUD_MARGIN;
    const float pip = u * 0.62f;
    float y = hud_margin + pip * 2.0f + u * 0.85f + u * 0.42f + g;
    //Off the top of the safe area if the HUD's own arithmetic would have put it higher - a cutout
    //across the top is exactly as good at eating a touch as the nav bar is.
    if (y < sy + m){
        y = sy + m;
    }

    input->SetTouchButtonRect(touch_menu, {left + m,y,small,small});
    y += small + g;
    input->SetTouchButtonRect(touch_pause,{left + m,y,small,small});
}

void ApplicationBomber::ApplyMenuPage(bomber_menu_page page){
    if (page == menu_page_applied){
        return;
    }
    menu_page_applied = page;

    //The background only means anything while the title scene is up; BOMBER_PAGE_NONE is the game
    //scene, which has its own everything.
    if ((page != BOMBER_PAGE_NONE) && title_splash){
        //The front page gets whichever splash variant this screen's shape asked for; every other
        //page gets the dungeon. See EnsureSplashVariant for who sets active_splash.
        int material = title_material_menu;
        if ((page == BOMBER_PAGE_MAIN) && (active_splash >= 0)){
            material = splash_variant[active_splash].material;
        }
        if (material >= 0){
            title_splash->SetMaterialSlot(0,material);
        }
    }
    if (main_window){
        LayoutMenuButtons(main_window->width,main_window->height,page);
        //Together with the menu's, from the one place the page actually changes, so the two sets
        //can never both be live - or both be dead - for a frame.
        LayoutGameButtons(main_window->width,main_window->height,page == BOMBER_PAGE_NONE);
    }
}

/*
    Brings up the UI theme. RENDER THREAD, from PreRender, once.

    THE ATLAS GOES THROUGH THE RENDERER, not through a second image path of the overlay's own. That
    reuses the engine's decode, its mipmapping, and the unpack-alignment fix - the one that stops a
    texture whose width times channels is not a multiple of 4 shearing diagonally, which this very
    sheet would have hit at 507 px of content.
*/
void ApplicationBomber::LoadUITheme(void){
    //One attempt. Retrying every frame would spray the log and hide the first, real message.
    f_theme_tried = true;

    ui_atlas = renderer->LoadTexture("ui/uisheet.png");
    if (!ui_atlas){
        debug->Err("UI theme: no ui/uisheet.png - menu falls back to debug colours\n");
        return;
    }
    if (!ui_sheet.LoadSheet("ui/uisheet.json")){
        return;
    }
    //Catches the one failure that is otherwise silent: the JSON and the PNG exported from
    //different packs, so every sprite loads but samples the wrong pixels.
    if (!ui_sheet.ValidateAgainstAtlas(ui_atlas->width,ui_atlas->height)){
        return;
    }
    if (!ui_sheet.LoadTheme("ui/theme.json")){
        return;
    }

    overlay->SetThemeTexture(ui_atlas->texture_id,ui_atlas->width,ui_atlas->height);
    f_theme_ready = true;
    debug->Ok("UI theme ready: %ix%i atlas, %i sprites, %i elements\n",
              ui_atlas->width,ui_atlas->height,
              ui_sheet.GetNumSprites(),ui_sheet.GetNumElements());
}

bool ApplicationBomber::DrawThemed(const char* role, vec2 min, vec2 max, uint32_t color){
    if (!f_theme_ready || !overlay){
        return false;
    }
    const ui_element* el = ui_sheet.FindElement(role);
    if (!el){
        return false;
    }
    const ui_sprite* sp = ui_sheet.FindSprite(el->sprite.c_str());
    if (!sp){
        return false;
    }
    ui_nine_inset inset;
    inset.left   = el->slice_left;
    inset.top    = el->slice_top;
    inset.right  = el->slice_right;
    inset.bottom = el->slice_bottom;
    overlay->AddNineSliceSprite(min,max,sp->x,sp->y,sp->w,sp->h,inset,color);
    return true;
}

/*
    One themed button, DRAWN FROM THE RECT THE HIT TEST USES.

    Read back out of the InputController rather than recomputed from the same constants, which is
    the rule this menu already lived by for its one BACK button and now matters four times more:
    two expressions meant to agree are two expressions that can stop agreeing, and a button drawn
    a few pixels off the rectangle that responds is the single most common way a menu feels broken.

    THE LABEL IS FITTED TO THE BUTTON, not the other way round. Sized off the rect's height so it
    scales with the screen, then shrunk if it does not fit the width - which is what keeps
    "OPTIONS" inside a button sized for "START" in the portrait column, and which is cheaper than
    asking the artist for a wider sprite. An empty rect draws nothing, so a page that does not
    show this button costs one compare.
*/
void ApplicationBomber::DrawMenuButton(int button, const char* label){
    if ((button < 0) || (button >= BOMBER_MENU_BUTTON_COUNT) || (menu_button[button] < 0)){
        return;
    }
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    const std::vector<InputController::TouchButton>& buttons =
        main_scene->inputcontroller->GetTouchButtons();
    if (menu_button[button] >= (int)buttons.size()){
        return;
    }
    const InputController::TouchRect& r = buttons[menu_button[button]].rect;
    if ((r.w <= 0.0f) || (r.h <= 0.0f)){
        return;
    }
    const vec2 bmin = vec2(r.x,r.y);
    const vec2 bmax = vec2(r.x + r.w,r.y + r.h);

    /*
        Held is drawn as a TINT rather than a second sprite, which is what the themed path buys:
        the texel is multiplied by this colour, so a darker one reads as pressed with no extra
        artwork to author or pack. The fallback path uses it as its own alpha.

        The three-slice itself lives in theme.json - see the note on "button" there for why a
        button is sliced on one axis only.
    */
    const bool f_held = buttons[menu_button[button]].f_down;
    const uint32_t tint = f_held ? UIColor(170,170,170,255) : UIColor(255,255,255,255);
    if (!DrawThemed("button",bmin,bmax,tint)){
        ui_nine_inset binset;
        binset.left = binset.right = 20.0f;
        binset.top = 0.0f;
        binset.bottom = 0.0f;
        overlay->AddNineSliceDebug(bmin,bmax,binset,f_held ? 255 : 210);
    }

    if (!label || !*label){
        return;
    }
    float ts = r.h * 0.42f;
    const float room = r.w * 0.80f;
    const float need = overlay->MeasureText(label,ts).x;
    if ((need > room) && (need > 0.0f)){
        ts *= room / need;
    }
    //0.36 of the em below the centre is where this font's baseline puts a line of capitals in the
    //middle of a box. Measured against the old BACK button, which is the same expression written
    //in terms of the page's title size.
    overlay->AddText(label,vec2((bmin.x + bmax.x) * 0.5f,(bmin.y + bmax.y) * 0.5f + ts * 0.36f),
                     ts,BOMBER_HUD_TEXT,UI_ALIGN_CENTER);
}

/*
    The menu. RENDER THREAD, from DrawOverlay. See the header.
*/
void ApplicationBomber::DrawMenu(void){
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    //The letterboxed art, not the window - the same rect LayoutMenuButtons places the BACK button
    //in, so the panel and the button that leaves it cannot drift apart. See GetTitleContentRect.
    vec2 cmin,cmax;
    GetTitleContentRect(main_window->width,main_window->height,&cmin,&cmax);
    const float w = cmax.x - cmin.x;
    const float h = cmax.y - cmin.y;
    //Off the CONTENT height, so the type scales with the artwork rather than with the bands.
    const float text_size = h * 0.045f;

    //BOMBER_PAGE_NONE is the game. Its HUD is drawn elsewhere and none of this applies.
    if (menu_page_applied == BOMBER_PAGE_NONE){
        return;
    }

    /*
        THE FRONT PAGE IS ITS FOUR BUTTONS AND NOTHING ELSE - no panel, because the artwork is
        the page.

        It used to draw nothing at all: the buttons were painted into splash.jpg and these rects
        were laid invisibly on top of the paint. The full-bleed variants have no buttons in them
        and cannot have any, because they are CROPPED to the screen and paint would slide out from
        under its rectangle. So they are the theme's buttons now, three-sliced from the same
        sprite BACK uses, in a row or a column depending on the shape of the screen.
    */
    if (menu_page_applied == BOMBER_PAGE_MAIN){
        for (int i = 0; i < 4; i++){
            DrawMenuButton(i,bomber_main_button_label[i]);
        }
        return;
    }

    const char* title = "";
    switch (menu_page_applied){
        case BOMBER_PAGE_LEVEL_SELECT: title = "LEVEL SELECT"; break;
        case BOMBER_PAGE_OPTIONS:      title = "OPTIONS";      break;
        case BOMBER_PAGE_HIGH_SCORES:  title = "HIGH SCORES";  break;
        /*
            Two words for one page, because a full bar under the word LOADING says the opposite of
            what the screen means at that moment.

            READY only on the way INTO the game, which is the only direction that waits. Coming
            back the page leaves the same frame it finishes, so READY would be a word that
            appears for one frame on the way to somewhere else.
        */
        case BOMBER_PAGE_LOADING:
            title = (f_loading_complete.load()
                     && (loading_target.load() == BOMBER_LOADING_TO_GAME)) ? "READY" : "LOADING";
            break;
        default: break;
    }

    vec2 pmin = vec2(cmin.x + BOMBER_MENU_PANEL_X * w,cmin.y + BOMBER_MENU_PANEL_Y * h);
    vec2 pmax = vec2(pmin.x + BOMBER_MENU_PANEL_W * w,pmin.y + BOMBER_MENU_PANEL_H * h);
    //The artwork if the theme came up, the role colours if it did not. Same geometry either way -
    //AddNineSliceSprite and AddNineSliceDebug both cut with UINineSliceRegions - so the fallback is
    //a faithful stand-in rather than a different layout.
    if (!DrawThemed("panel",pmin,pmax)){
        ui_nine_inset inset;
        inset.left = inset.top = inset.right = inset.bottom = 30.0f;
        overlay->AddNineSliceDebug(pmin,pmax,inset,235);
    }

    overlay->AddText(title,vec2((pmin.x + pmax.x) * 0.5f,pmin.y + text_size * 2.0f),
                     text_size,BOMBER_HUD_TEXT,UI_ALIGN_CENTER);

    /*
        The back button, drawn from the SAME rect the hit-test uses.

        Read back out of the InputController rather than recomputed from the same constants: two
        expressions that are meant to agree are two expressions that can stop agreeing, and a
        button drawn a few pixels off the rectangle that responds is the single most common way a
        menu feels broken. This way there is one number.
    */
    /*
        THE BAR, and it is the whole of this page.

        Drawn from the OVERLAY's own primitives rather than from the theme, because a progress bar
        is the one piece of furniture here with no sprite in uisheet.json - and it does not need
        one: AddRect's rounded ends are analytic, so a capsule of any width is exact at any size.
        Which matters more than usual here, since the fill's width is the thing that changes.

        The fill is drawn over the track rather than the track being redrawn around it, so the
        two cannot disagree about where the ends are.
    */
    if (menu_page_applied == BOMBER_PAGE_LOADING){
        const float bar_w = (pmax.x - pmin.x) * 0.62f;
        const float bar_h = text_size * 0.55f;
        const float bar_x = (pmin.x + pmax.x) * 0.5f - bar_w * 0.5f;
        const float bar_y = (pmin.y + pmax.y) * 0.5f;
        const float radius = bar_h * 0.5f;

        overlay->AddRect(vec2(bar_x,bar_y),vec2(bar_x + bar_w,bar_y + bar_h),
                         radius,BOMBER_HUD_TEXT_DIM);
        const float filled = bar_w * GetLoadingProgress();
        //A zero-width fill would be dropped by AddQuad anyway, but the FLOOR is what stops a
        //just-started bar drawing as a dot: below one capsule's width there is nothing a rounded
        //rect can honestly show.
        if (filled > bar_h){
            overlay->AddRect(vec2(bar_x,bar_y),vec2(bar_x + filled,bar_y + bar_h),
                             radius,BOMBER_HUD_KEY);
        }

        /*
            TAP TO START, and it PULSES because a line of static text on a finished bar does not
            read as something waiting for you - it reads as a caption.

            IN TICKS, off Scene::GetPhysicsTick, which is the clock the rest of this codebase
            counts durations in and is an atomic, so reading it from the render thread is honest
            rather than merely lucky. A count of rendered FRAMES would have been the easy thing
            here and is the wrong unit twice over: it changes speed with the frame rate, and it
            would be the only duration in the app not expressed the way every other one is.
        */
        if (f_loading_complete.load() && (loading_target.load() == BOMBER_LOADING_TO_GAME)){
            const uint64_t tick = main_scene ? main_scene->GetPhysicsTick() : 0;
            const float phase = (float)(tick % BOMBER_TAP_PULSE_TICKS)
                              / (float)BOMBER_TAP_PULSE_TICKS;
            //Cosine rather than a sawtooth: the ends of a triangle wave are a visible corner, and
            //this sits still on screen long enough for that to be the thing you notice about it.
            const float wave = 0.5f - 0.5f * cosf(phase * 6.2831853f);
            const uint8_t alpha = (uint8_t)(110.0f + 145.0f * wave);
            overlay->AddText("TAP TO START",
                             vec2((pmin.x + pmax.x) * 0.5f,bar_y + bar_h + text_size * 1.8f),
                             text_size * 0.7f,UIColor(236,242,255,alpha),UI_ALIGN_CENTER);
        }
        return;
    }

    //The one way out of a sub-page, through the same helper the front page's four go through.
    DrawMenuButton(4,"BACK");
}

/*
    The HUD. RENDER THREAD, from Application::DrawFrame between the scene and the ImGui panels.

    WHAT IS DELIBERATELY NOT HERE: the score. What a board was worth is revealed on the walk OUT of
    it - see BomberHUD::f_tally - because a number ticking up in the corner while you play turns a
    bomberman board into a score attack. The clock is shown, because that one is a thing the player
    can still act on.

    Everything is sized off one unit (BOMBER_HUD_UNIT x window height), so this survives a resize
    without a layout pass. Reads `hud` and nothing else - see PublishHUD for why.
*/
void ApplicationBomber::DrawOverlay(void){
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    //No lives, bombs or timer over a title card. This is the one gate that reads main_scene from
    //the RENDER thread, so at a switch it can be one frame stale - a single frame of HUD over the
    //splash, or none over the first frame of play. See Application::ApplyPendingSceneSwitch.
    /*
        The menu is applied HERE, from the one place that already reads main_scene on this thread,
        so the background and the button rects change together with the page and never separately.
        BOMBER_PAGE_NONE while the game is up is what retires the menu's buttons - they share the
        window's one InputController with the game, so a rect left behind would be live under the
        board. See ApplyMenuPage.
    */
    ApplyMenuPage(IsOnTitleScreen() ? menu_page : BOMBER_PAGE_NONE);

    if (IsOnTitleScreen()){
        DrawMenu();
        return;
    }
    const float u = (float)main_window->height * BOMBER_HUD_UNIT;
    const float margin = u * BOMBER_HUD_MARGIN;
    const float w = (float)main_window->width;

    //--- lives ----------------------------------------------------------------------------------
    /*
        One pip per point of MAZE_START_HEALTH, lost ones drawn dim rather than dropped, so the row
        is the same width all game and says how much is gone as well as how much is left.

        Pulsed while the mercy window runs (MAZE_HIT_INVULN_TICKS), which is the only feedback
        anywhere that the player is briefly un-hittable - the ImGui panel had it and the game did
        not. Square wave off the tick counter rather than a sine: it has to read as blinking at a
        glance, and at 60 Hz a 6-tick half period is about as slow as that stays urgent.
    */
    const float pip = u * 0.62f;
    const float pip_step = pip * 2.6f;
    bool f_blink = (hud.invuln_ticks > 0) && (((hud.invuln_ticks / 6) & 1) != 0);
    for (int i = 0; i < MAZE_START_HEALTH; i++){
        vec2 c = vec2(margin + pip + (float)i * pip_step,margin + pip);
        bool f_have = (i < hud.health);
        uint32_t color = f_have ? BOMBER_HUD_LIFE : BOMBER_HUD_LIFE_LOST;
        if (f_have && f_blink){
            color = BOMBER_HUD_LIFE_LOST;
        }
        overlay->AddRect(vec2(c.x - pip,c.y - pip),vec2(c.x + pip,c.y + pip),pip,color);
        //An outline on every slot, lit or not, so the row keeps its shape when the pips go dim.
        overlay->AddRectOutline(vec2(c.x - pip,c.y - pip),vec2(c.x + pip,c.y + pip),
                                pip,u * 0.09f,BOMBER_HUD_LIFE_LOST);
    }

    //--- the key --------------------------------------------------------------------------------
    //On the same row, past the pips. Dim until it is found, which is what says one exists.
    vec2 key_c = vec2(margin + pip + (float)MAZE_START_HEALTH * pip_step + u * 0.9f,margin + pip);
    DrawKeyIcon(key_c,u * 1.25f,hud.f_has_key ? BOMBER_HUD_KEY : BOMBER_HUD_KEY_NONE);

    //--- the shield -----------------------------------------------------------------------------
    /*
        A draining capsule, and the ONE element that is absent when it does not apply - a shield is
        an event rather than a slot, and a permanently empty bar would read as something broken.
    */
    if (hud.shield_ticks > 0){
        float bar_h = u * 0.42f;
        float bar_w = u * 6.0f;
        vec2 bar_min = vec2(margin,margin + pip * 2.0f + u * 0.85f);
        vec2 bar_max = vec2(bar_min.x + bar_w,bar_min.y + bar_h);
        overlay->AddRect(bar_min,bar_max,bar_h * 0.5f,BOMBER_HUD_SHIELD_BACK);
        float left = clamp((float)hud.shield_ticks / (float)MAZE_SHIELD_TICKS,0.0f,1.0f);
        //Never narrower than the cap radius, or the last few ticks draw as an inside-out sliver.
        float fill = max(bar_w * left,bar_h);
        overlay->AddRect(bar_min,vec2(bar_min.x + fill,bar_max.y),bar_h * 0.5f,BOMBER_HUD_SHIELD);
    }

    //--- the clock ------------------------------------------------------------------------------
    /*
        Top right. TICKS ARE THE UNIT EVERYWHERE ELSE in this app (see CLAUDE.md), and this is the
        one place that converts - because a player reads a clock, not a tick count.

        Through Application::GetPhysicsTimestep, which is 1/physics_tps and therefore the rate this
        app ACTUALLY set in Init (60, not the engine's default 50). A hardcoded divisor would have
        been wrong by a fifth from the day it was written, and would have looked perfectly plausible.
    */
    float seconds = (float)hud.level_ticks * GetPhysicsTimestep();
    int mins = (int)(seconds / 60.0f);
    float secs = seconds - (float)mins * 60.0f;

    char text[64];
    const float text_size = u * 1.05f;
    snprintf(text,sizeof(text),"%i:%04.1f",mins,secs);
    //AddText takes the BASELINE, so the first line sits one text height down from the margin.
    vec2 clock_at = vec2(w - margin,margin + text_size);
    overlay->AddText(text,clock_at,text_size,BOMBER_HUD_TEXT,UI_ALIGN_RIGHT);

    /*
        THAT THE GAME IS PAUSED, which was the half of pausing that was missing.

        'P' and VK_PAUSE have always been mapped, and Scene::BeginPass has always acted on them -
        so the key worked and looked like it did not, because a paused board and a board where
        nothing happens to be moving are the same picture. Saying so is the whole fix.

        Read straight off the scene rather than published through `hud`: it is one atomic bool
        (Scene::f_paused), the render thread may read it directly, and routing it through the
        publish step would make it a tick stale - which on the one indicator whose entire job is
        to say "nothing is advancing" would be a strange thing to be late about.
    */
    if (main_scene && main_scene->IsPhysicsPaused()){
        overlay->AddText("PAUSED",vec2(w * 0.5f,margin + text_size * 1.6f),
                         text_size * 1.4f,BOMBER_HUD_TEXT,UI_ALIGN_CENTER);
    }

    //--- the tally ------------------------------------------------------------------------------
    /*
        The corridor, and the only place the score is ever shown.

        Drawn under the clock rather than in the middle of the screen: the corridor's framing already
        has the player in the middle of frame, and this is a receipt rather than an interstitial.
    */
    if (hud.f_tally){
        const float line = text_size * 1.25f;
        vec2 at = vec2(w - margin,clock_at.y + line);
        snprintf(text,sizeof(text),"TIME BONUS  +%u",hud.time_bonus);
        overlay->AddText(text,at,text_size * 0.8f,
                         hud.time_bonus > 0 ? BOMBER_HUD_KEY : BOMBER_HUD_TEXT_DIM,UI_ALIGN_RIGHT);
        at.y += line;
        snprintf(text,sizeof(text),"SCORE  %u",hud.score);
        overlay->AddText(text,at,text_size * 1.1f,BOMBER_HUD_TEXT,UI_ALIGN_RIGHT);
    }

    //Last, so the thumbs are over the HUD rather than under it.
    DrawGameButtons();
}

/*
    See the header.

    DRAWN FROM THE RECTS INPUT IS ACTUALLY USING, read back out of the InputController, rather than
    recomputed from the same constants. A button drawn a few pixels off the rectangle that responds
    is the single most common way a touch UI feels broken, and the only way to be sure they agree
    is for there to be one of them - the same reasoning as the menu's BACK button.

    An empty rect draws nothing, which is what makes the title screen need no case here: off the
    board LayoutGameButtons has already written all seven empty.
*/
void ApplicationBomber::DrawGameButtons(void){
    if (!overlay || !overlay->IsReady() || !main_scene || !main_scene->inputcontroller){
        return;
    }
    const std::vector<InputController::TouchButton>& buttons =
        main_scene->inputcontroller->GetTouchButtons();

    const int ids[] = {touch_menu,touch_pause,touch_north,touch_south,
                       touch_west,touch_east,touch_bomb};
    const char* labels[] = {"MENU","II","^","v","<",">","BOMB"};
    /*
        One type size per label rather than one rule for all seven: "BOMB" is four glyphs in a box
        built for a single arrow, and a size that suits the arrow overflows the word. A strlen test
        would get the same answer for "MENU" and "BOMB" and the wrong one for "II", which wants to
        be large because it is a symbol rather than a word.
    */
    const float scale[] = {0.30f,0.52f,0.58f,0.58f,0.58f,0.58f,0.26f};

    for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++){
        if ((ids[i] < 0) || (ids[i] >= (int)buttons.size())){
            continue;
        }
        const InputController::TouchRect& r = buttons[ids[i]].rect;
        if ((r.w <= 0.0f) || (r.h <= 0.0f)){
            continue;
        }
        vec2 bmin = vec2(r.x,r.y);
        vec2 bmax = vec2(r.x + r.w,r.y + r.h);

        //Tint for held, exactly as the BACK button does it - the themed texel is multiplied by
        //this, so a darker one reads as pressed with no second sprite to author.
        bool f_held = buttons[ids[i]].f_down;
        uint32_t tint = f_held ? UIColor(170,170,170,255) : UIColor(255,255,255,235);
        if (!DrawThemed("button",bmin,bmax,tint)){
            ui_nine_inset binset;
            binset.left = binset.right = 20.0f;
            binset.top = binset.bottom = 0.0f;
            overlay->AddNineSliceDebug(bmin,bmax,binset,f_held ? 255 : 190);
        }
        const float size = r.h * scale[i];
        overlay->AddText(labels[i],vec2((bmin.x + bmax.x) * 0.5f,
                                        (bmin.y + bmax.y) * 0.5f + size * 0.35f),
                         size,BOMBER_HUD_TEXT,UI_ALIGN_CENTER);
    }
}

//--- the camera -------------------------------------------------------------------------------

/*
    The offset from the pivot to the camera, from the three spherical terms.

    YAW 0 PUTS THE CAMERA DUE SOUTH OF THE PIVOT (+Z) LOOKING NORTH, which is what makes yaw 0 the
    framing where screen-up is MAZE_DIR_NORTH - the forward key. Every other yaw is measured from
    there, and the game framing simply never leaves it.
*/
static vec3 BomberCameraOffset(float yaw_degrees, float pitch_degrees, float distance){
    float yaw = toradians(yaw_degrees);
    float pitch = toradians(pitch_degrees);
    //Horizontal reach at this pitch. The vertical term is the rest of the distance.
    float flat = cosf(pitch) * distance;
    return vec3(sinf(yaw) * flat, sinf(pitch) * distance, cosf(yaw) * flat);
}

//`to` - `from` brought into -180..180, so easing a yaw back to the game framing takes the short way
//round. Without it a free orbit parked just past due north unwinds nearly all the way about.
static float BomberShortestAngle(float from, float to){
    float d = to - from;
    while (d > 180.0f){
        d -= 360.0f;
    }
    while (d < -180.0f){
        d += 360.0f;
    }
    return d;
}

/*
    Where the player is, whoever is simulating them. PHYSICS THREAD.

    One call rather than a branch at each site, because the camera is the only thing that has to ask
    without caring which of Maze and Hallway currently owns the walker.
*/
vec3 ApplicationBomber::PlayerWorldPos(void) const {
    if (f_in_hallway){
        return HallCellCentre(hall.player.X(),hall.player.Z());
    }
    vec3 base = CellCentre(0,0);
    return vec3(base.x + maze.player.X() * BOMBER_CELL_SIZE,0.0f,
                base.z + maze.player.Z() * BOMBER_CELL_SIZE);
}

/*
    Where the game camera wants to be right now.

    SOLVED FROM THE STATE OF THE GAME, not remembered - which is what makes the whole transition a
    matter of two constants differing rather than of a saved pose being restored. The board's
    framing and the corridor's are the same framing with a different pitch, distance and pivot; the
    yaw is common to both and never moves.
*/
void ApplicationBomber::SolveGameFraming(float& yaw, float& pitch, float& distance,
                                         vec3& pivot) const {
    //THE ONE LINE THAT MAKES THE CONTROLS READ. See BomberCameraOffset.
    yaw = 0.0f;

    if (f_in_hallway){
        pitch = BOMBER_HALL_CAM_PITCH;
        distance = BOMBER_HALL_CAM_DISTANCE;
        //Straight onto the player: there is no board left to keep in frame, and the corridor is
        //narrow enough that anything else would put them against an edge.
        pivot = PlayerWorldPos();
        return;
    }

    pitch = BOMBER_CAM_PITCH;
    distance = BOMBER_CAM_DISTANCE;
    /*
        On the board the pivot LEANS toward the player rather than following them.

        The board is the thing being played and it wants to stay in frame, so the camera is centred
        on it and only leans - a fraction of the way out, and never further than the clamp. A true
        follow would put the player in the middle of the screen and the board half off it, which on
        a game whose whole subject is a fixed 16x16 grid is worse than not tracking at all.

        Taken from the two corners rather than from vec3() so that this still means "the middle of
        the board" if CellCentre's origin ever moves.
    */
    vec3 centre = (CellCentre(0,0) + CellCentre(MAZE_W - 1,MAZE_H - 1)) * 0.5f;
    vec3 lean = (PlayerWorldPos() - centre) * BOMBER_CAM_FOLLOW;
    lean.y = 0.0f;
    float reach = lean.length();
    if (reach > BOMBER_CAM_FOLLOW_MAX){
        lean = lean * (BOMBER_CAM_FOLLOW_MAX / reach);
    }
    pivot = centre + lean;
}

/*
    Places the camera. PHYSICS THREAD, from UpdateView, every pass INCLUDING PAUSED ONES.

    That last part is deliberate and is the same call ApplicationPinball::UpdateCameraShot makes:
    the framing has to keep easing while the simulation is stopped, or pausing the game to look at
    something leaves the camera stranded half way through a move.

    An exponential ease rather than a keyframed move - no duration to tune, no overshoot, and
    redirecting it mid-flight costs nothing, which matters because the corridor can begin and end at
    any point in one.
*/
void ApplicationBomber::UpdateGameCamera(bool f_snap){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!camera || camera_mode != BOMBER_CAM_GAME){
        return;
    }
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 0.0f;
    vec3 pivot;
    SolveGameFraming(yaw,pitch,distance,pivot);

    float t = f_snap ? 1.0f : BOMBER_CAM_EASE;
    cam_yaw      += BomberShortestAngle(cam_yaw,yaw) * t;
    cam_pitch    += (pitch - cam_pitch) * t;
    cam_distance += (distance - cam_distance) * t;
    camera_target += (pivot - camera_target) * t;

    camera->SetPosition(camera_target + BomberCameraOffset(cam_yaw,cam_pitch,cam_distance));
    camera->SetLookAt(camera_target);
}

/*
    Reads the three spherical terms back OUT of wherever the camera currently is.

    So that leaving the free orbit eases back to the game framing FROM THE VIEW YOU WERE JUST
    LOOKING AT rather than cutting to it. The exact inverse of the placement above, and the same
    trick as ApplicationPinball::SeedOrbitFromCamera with the two modes swapped.
*/
void ApplicationBomber::SeedCameraFromView(void){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (!camera){
        return;
    }
    vec3 offset = camera->GetPosition() - camera_target;
    float distance = offset.length();
    if (distance < 0.001f){
        //Degenerate: the camera is sitting on its own pivot and there is no direction to recover.
        //Leave the last framing alone rather than inventing one out of a zero vector.
        return;
    }
    cam_distance = distance;
    cam_pitch = todegrees(asinf(clamp(offset.y / distance,-1.0f,1.0f)));
    //atan2(x,z), NOT the usual (z,x): yaw here is measured from +Z, because that is where the game
    //framing sits. Getting this pair round the wrong way is a quarter turn that only shows up on
    //the frame the mode changes.
    cam_yaw = todegrees(atan2f(offset.x,offset.z));
}

void ApplicationBomber::SetCameraMode(int mode){
    if (mode != BOMBER_CAM_GAME && mode != BOMBER_CAM_FREE){
        return;
    }
    if (mode == camera_mode){
        return;
    }
    //Entering GAME takes its framing from where the camera already is, so the ease has somewhere to
    //come FROM. Leaving it needs nothing: the orbit works off camera->GetPosition and the pivot,
    //both of which the game camera has been keeping honest all along.
    if (mode == BOMBER_CAM_GAME){
        SeedCameraFromView();
    }
    camera_mode = mode;
    debug->Ok("Camera: %s\n",mode == BOMBER_CAM_GAME ? "game" : "free (middle-drag orbits)");
}

/*
    Physics thread, every pass - including the ones that simulate nothing. Camera and the reload key
    only: neither is simulation, and nothing here may write something a tick will read.

    TWO CAMERAS LIVE HERE, and which one runs is `camera_mode`:

      - BOMBER_CAM_GAME is the game's own framing, solved by UpdateGameCamera. Nothing a human does
        moves it.
      - BOMBER_CAM_FREE is the orbit below, which is a DEBUGGING AFFORDANCE and used to be the only
        camera there was. <C> switches, and so does the panel and `bomber_camera`.

    THE ORBIT IS ApplicationShip's, not the shorter one apps/testfx uses, and the differences are
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

    //The menu and Escape, BEFORE the title-screen return below, and from here rather than from the
    //tick so they keep working while the simulation is paused. See the note above UpdateMenu.
    UpdateMenu(input);

    //The title camera is static and deliberately so. Everything below solves the GAME camera, and
    //main_scene->camera is the title one right now - it would ease the splash out of frame.
    if (IsOnTitleScreen()){
        return;
    }
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

    /*
        A mode switch asked for over MCP. BEFORE the lock, because the lock is about a HUMAN at the
        keyboard and this is not one - an agent has to be able to put the camera where it wants it
        without first giving the keyboard back.
    */
    int wanted_mode = requested_camera_mode.exchange(-1);
    if (wanted_mode >= 0){
        SetCameraMode(wanted_mode);
    }

    if (f_lock_human_input){
        /*
            Everything below this line is a human moving the camera or asking for a shader reload,
            and under the lock neither happens.

            THE GAME CAMERA IS OFF TOO, which is new and is the whole reason the lock still works.
            It is solved every pass, so leaving it running would overwrite `camera_set` on the very
            next one and an agent's framing would never survive to the screenshot. Under the lock
            the camera belongs to MCP, exactly as it did when the orbit was the only camera.
        */
        return;
    }

    if (input->WasKeyReleased(INPUT_BOMBER_RELOAD_SHADER)){
        //NOT a GL call: it only raises a flag that PreRender acts on. UpdateView runs on the
        //physics thread, which may not touch the context at all.
        f_shader_reload_requested = true;
    }
    if (input->WasKeyReleased(INPUT_BOMBER_CAMERA)){
        SetCameraMode(camera_mode == BOMBER_CAM_GAME ? BOMBER_CAM_FREE : BOMBER_CAM_GAME);
    }

    if (camera_mode == BOMBER_CAM_GAME){
        //One call, and it is the whole camera. The orbit below is not merely unused in this mode -
        //it must not run, or the two would fight over the same position every pass.
        UpdateGameCamera();
        return;
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
    //The UI theme, once, as soon as there is an overlay to hand it to - see LoadUITheme.
    if (!f_theme_tried && overlay && overlay->IsReady()){
        LoadUITheme();
    }
    //BEFORE ScaleTitleSplash, which reads the active variant's size to shape the quad - one frame
    //of the previous variant's aspect would be a visible stretch on the frame a window crosses 1:1.
    EnsureSplashVariant();
    ScaleTitleSplash();
    //The loading screen's one texture for this frame, if one is up. See StepLoading - it is a
    //return on every frame that is not loading, which is nearly all of them.
    StepLoading();
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

//--- MCP ---------------------------------------------------------------------------------------------

/*
    The section starts HERE, not below at RegisterMCPTools. MapJson and StateJson return `json`,
    a type that only exists when core/MCPServer.h has been included, so they are as much part of
    the MCP surface as the tool registrations that call them - and left outside the guard they
    stop a USE_MCP=0 build from compiling at all.
*/
#ifdef USE_MCP
json ApplicationBomber::MapJson(void){
    json out;
    /*
        ONE ENTRY PER TILE TYPE, and the compiler will not tell you if there are fewer.

        A table this shape value-initialises anything the initialiser list does not reach, so the
        day MAZE_TILE_DOOR was added this quietly started writing '\0' into the middle of a JSON
        string. Spelled out with the enum name against each row so the next one added is obvious.
    */
    static const char GLYPH[MAZE_TILE_COUNT] = {
        '.',    //MAZE_TILE_GRASS
        ',',    //MAZE_TILE_BRICK
        ':',    //MAZE_TILE_ROCK
        '~',    //MAZE_TILE_WATER
        '#',    //MAZE_TILE_WALL
        'h',    //MAZE_TILE_HEDGE
        'w',    //MAZE_TILE_WOOD
        'D',    //MAZE_TILE_DOOR
    };
    //Same shape, same warning. A pickup shows only where it can be reached - see below.
    static const char ITEM_GLYPH[MAZE_ITEM_COUNT] = {
        ' ',    //MAZE_ITEM_NONE
        '+',    //health
        'S',    //shield
        'K',    //key
        'c',    //coin
        'd',    //diamond
        'x',    //crystal
    };
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
                c = ITEM_GLYPH[maze.item[z][x] < MAZE_ITEM_COUNT ? maze.item[z][x] : 0];
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
                           "| bridge north-south  # wall  h hedge  w wood  D exit door  "
                           "+ health  S shield  K key  c coin  d diamond  x crystal  "
                           "@ player  E enemy  o bomb";
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
        {"health",maze.player.health},
        {"shield_ticks",maze.player.shield_ticks},
        {"invuln_ticks",maze.player.invuln_ticks},
        {"deaths",maze.deaths},
        //Counts DOWN while lying there; the body is drawn for all of it.
        {"death_ticks",maze.player.death_ticks},
        //Treasure only. See MazeItemScore - health and shields are worth nothing here.
        {"score",maze.player.score},
        //The exit key. The one pickup that is a win condition rather than a reward, and the one
        //thing on the walker that a death does not take.
        {"has_key",maze.player.f_has_key},
        //Whether the shield is actually being WORN, which is the half a tick count cannot answer:
        //it is a child of the character, so this is also a test that the character exists.
        {"wearing_shield",shield_worn ? shield_worn->IsVisible() : false},
        /*
            The clip the player is on, which the enemies have reported all along and the player could
            not until it was skinned.

            CurrentAnimationName is what it is playing OR BECOMING - it names the destination from
            the moment a blend starts - so this is also what the selection in SyncView compares
            against, and a caller reading it is seeing exactly what that code sees.
        */
        {"clip",character ? character->CurrentAnimationName() : "none"},
        //What the walk is slowed to so the feet keep up with the tiles - see char_walk_rate.
        {"walk_rate",char_walk_rate}
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
        //The cell inside the door - the only one a walker can step onto it from.
        int door_in_x = (maze.door_x == 0) ? 1
                      : (maze.door_x == MAZE_W - 1 ? MAZE_W - 2 : maze.door_x);
        int door_in_z = (maze.door_z == 0) ? 1
                      : (maze.door_z == MAZE_H - 1 ? MAZE_H - 2 : maze.door_z);
        json door = json{
            {"tile",json::array({maze.door_x,maze.door_z})},
            //`unlocked` is the RULE - whether the PLAYER may walk through. `open` is the view
            //following it, and the two differ for the fifty ticks the leaf takes to swing.
            {"unlocked",maze.player.f_has_key},
            /*
                What the walker will actually be told, asked of the rules rather than restated.

                Through CanEnter and from the cell INSIDE the door, because that is the only step
                anyone ever takes onto it - and because Maze::IsPassable now calls a door shut for
                everybody, which is its job. `enemy_passable` is the same question for a body with
                no key, and it is here because it was FALSE for a while and nothing said so: an
                enemy could walk into the unlocked exit and stand in it.
            */
            {"passable",maze.CanEnter(maze.player,door_in_x,door_in_z,maze.door_x,maze.door_z)},
            {"enemy_passable",maze.CanEnter(MazeWalker(),door_in_x,door_in_z,
                                            maze.door_x,maze.door_z)},
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
    /*
        The way IN, which is a different cell on every board now.

        Worth reporting rather than derivable: the entry border is chosen from the direction the
        LAST corridor ran, so nothing a caller can see on this board says where it is. `dir` is that
        border's outward normal, which is also the opposite of the way the player walked in.
    */
    /*
        The clock, and what it is currently worth.

        `bonus` is what the board would pay if the player reached the exit RIGHT NOW - a query, not
        a promise (Maze::TimeBonus), so it falls as the level runs and is 0 past par.
    */
    result["time"] = json{
        {"level_ticks",maze.level_ticks},
        {"par_ticks",MAZE_TIME_PAR_TICKS},
        {"bonus_now",maze.TimeBonus()}
    };
    result["entry"] = json{
        {"tile",json::array({maze.entry_x,maze.entry_z})},
        {"dir",maze.entry_dir},
        {"spawn",json::array({maze.spawn_x,maze.spawn_z})}
    };
    /*
        The corridor, when there is one. Which of the two is live is the first thing any caller has
        to know, because `character` and `field` describe the BOARD and the board is not where the
        player is standing while this block exists.
    */
    result["in_hallway"] = f_in_hallway;
    if (f_in_hallway){
        result["hallway"] = json{
            {"length",hall.length},
            {"tile",json::array({hall.player.tile_x,hall.player.tile_z})},
            /*
                ONE FRAME, and it is the one to press.

                `forward` is where the corridor points AND the direction whose key walks the player
                up it, because a corridor is never turned any more. It used to be turned at the
                commit to meet a fixed entry cell, so there was a second frame (`control_forward`)
                that a caller had to read instead - reading `forward` and pressing it walked you into
                the side wall, which is exactly what the first version of the test for this did. The
                board's entry border follows the corridor now and the second frame is gone.
            */
            {"forward",hall.forward},
            //What the board just left paid out. Frozen at BeginHallway - the live `time` block
            //above is the NEW board's clock, which the commit has already restarted.
            {"level_ticks",hall_level_ticks},
            {"time_bonus",hall_time_bonus},
            {"near_door_open",hall.f_near_door_open},
            //`sealed` is the RULE (the player stepped off the threshold) and `committed` is the app
            //having acted on it once the near door finished shutting - they are 60 ticks apart.
            {"sealed",hall.f_sealed},
            {"committed",f_hall_committed},
            {"far_door_open",hall.f_far_door_open},
            {"finished",hall.f_finished},
            //The walker travels, so these are the numbers that have to come out the other end.
            {"health",hall.player.health},
            {"score",hall.player.score},
            {"has_key",hall.player.f_has_key}
        };
    }
    /*
        The camera, because in GAME mode it is a fact about the game rather than about the viewer.

        `mode` is what decides whether `camera_set` sticks: in "game" the framing is solved every
        pass and overwrites anything written from outside, in "free" the orbit leaves it alone. An
        agent that wants a fixed framing switches to "free" first, or turns the input lock on - see
        bomber_lock_input, which freezes the game camera for exactly this reason.
    */
    result["camera"] = json{
        {"mode",camera_mode == BOMBER_CAM_GAME ? "game" : "free"},
        {"yaw",cam_yaw},
        {"pitch",cam_pitch},
        {"distance",cam_distance},
        {"target",json::array({camera_target.x,camera_target.y,camera_target.z})}
    };
#ifdef USE_SOUND
    /*
        The sound layer, as a number an agent can actually check.

        `playing` is how many voices are audible RIGHT NOW (core/SoundSystem.h), which is the only
        way to confirm from outside the process that an event made a noise - and the only way to
        see this app starving itself of the 16 it has. Paired with sim_pause and sim_step it is an
        exact test: step the tick that should make a sound, then read this.
    */
    result["sound"] = json{
        {"enabled",f_sound_enabled},
        {"device",soundsystem ? soundsystem->f_initialised : false},
        {"playing",soundsystem ? soundsystem->GetNumPlaying() : 0}
    };
#endif
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

//--- MCP: the tools themselves ------------------------------------------------------------------------

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
        "`action` is north/south/east/west/bomb while playing, or start/levels/options/scores/back/tap "
        "on the title screen - the menu's buttons drive ordinary actions, so they are reachable "
        "here without a pointer. A direction wants enough ticks to cross a tile (20 at the default "
        "walk speed); `bomb` and every menu action are edges, so one tick is enough. The call "
        "returns as soon as the hold is queued - step or wait for it to play out, then read "
        "bomber_state.",
        json{
            {"type","object"},
            {"properties", {
                {"action", {{"type","string"},{"description","north, south, east, west, bomb, or a menu action: start, levels, options, scores, back, tap (the loading screen's TAP TO START)"}}},
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
            //The menu, so the pages can be walked from a script. UpdateMenu reads these as RELEASE
            //edges, and a hold ends in a release, so one tick is a whole button press.
            else if (action == "start"){ mapped = INPUT_BOMBER_MENU_START; }
            else if (action == "levels"){ mapped = INPUT_BOMBER_MENU_LEVELS; }
            else if (action == "options"){ mapped = INPUT_BOMBER_MENU_OPTIONS; }
            else if (action == "scores"){ mapped = INPUT_BOMBER_MENU_SCORES; }
            //The universal back (what Escape is bound to), not the on-screen button's own action -
            //this one is handled in the game as well as in the menu, so one scripted action
            //exercises all three levels it backs out of.
            else if (action == "back"){ mapped = INPUT_BOMBER_BACK; }
            //The loading screen's "TAP TO START". Reachable here for the same reason every other
            //menu action is: it is an ordinary action, so it needs no pointer to deliver.
            else if (action == "tap"){ mapped = INPUT_BOMBER_MENU_TAP; }
            else {
                return json{ {"error","action must be north, south, east, west, bomb, start, levels, options, scores, back or tap"} };
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
        "Give the player the exit key, or take it back - which is what opens and shuts the door, "
        "because the door is the PLAYER'S key and the animation follows it - an enemy carries no "
        "key and can never use the exit. The door is an ARCHWAY "
        "plus a LEAF moved by the Door_Opening clip out of the .glb, the app's only animated prop "
        "that is not a character. Opening takes about 50 ticks and the RULE changes immediately, so "
        "`door.unlocked` and `door.open` differ while it swings; `door.passable` is what the walker "
        "will actually be allowed to do. To look at it a frame at a time: sim_pause, call this, "
        "then sim_step.",
        json{
            {"type","object"},
            {"properties", {
                {"open", {{"type","boolean"},{"description","true gives the key and opens it, false takes the key back and shuts it"}}},
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
        "health, shield, key, coin, diamond or crystal. FOR TESTING what a pickup does without playing "
        "a board until one turns up - it goes through Maze::TickItems exactly as a dug-up one "
        "does, so the score, the shield and the health cap are all the real rules. What it does "
        "NOT do is make one appear on screen: the field's pickup objects are built per cell when "
        "the board is laid out, and a cell that never had one has no object to show. Use it for "
        "the rules and the worn shield; watch `field.items_shrinking` for the shrink.",
        json{
            {"type","object"},
            {"properties", {
                {"item", {{"type","string"},{"description","health, shield, key, coin, diamond or crystal"}}},
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
            else if (want == "key"){ id = MAZE_ITEM_KEY; }
            else if (want == "coin"){ id = MAZE_ITEM_COIN; }
            else if (want == "diamond"){ id = MAZE_ITEM_DIAMOND; }
            else if (want == "crystal"){ id = MAZE_ITEM_CRYSTAL; }
            else {
                return json{ {"error","item must be health, shield, key, coin, diamond or crystal"} };
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

    MCPServer::Get()->RegisterTool("bomber_camera",
        "Switch between the two cameras. `game` is the one the game plays with: a fixed world yaw "
        "so that screen-up is always the forward key, a near-overhead pitch so a blast reads as the "
        "cross it is, and a pivot that leans toward the player without letting the board out of "
        "frame. It comes down and in by itself while the player is in the corridor between levels, "
        "keeping the same yaw the whole way - which is why walking through one no longer turns the "
        "controls round. `free` is the middle-mouse orbit, which is a debugging view. "
        "WHICH ONE IS ON DECIDES WHETHER camera_set STICKS: the game camera is solved every pass "
        "and overwrites anything written from outside on the next one, so switch to `free` (or turn "
        "bomber_lock_input on, which freezes the game camera) before placing the camera by hand. "
        "<C> does the same thing from the keyboard. Returns the framing either way.",
        json{
            {"type","object"},
            {"properties", {
                {"mode", {{"type","string"},{"enum",json::array({"game","free"})},
                          {"description","game for the played framing, free for the orbit"}}}
            }},
            {"required",json::array({"mode"})}
        },
        [this](const json& args) -> json {
            std::string mode = args.value("mode",std::string("game"));
            if (mode != "game" && mode != "free"){
                return json{ {"error","mode must be \"game\" or \"free\""} };
            }
            /*
                POSTED, NOT APPLIED. This runs on an MCP thread holding no lock; SetCameraMode reads
                and writes the camera and the framing, both of which belong to the physics thread.
                UpdateView picks it up at the top of the next pass - the same arrangement
                ApplicationPinball::requested_shot uses, and for the same reason.
            */
            requested_camera_mode = (mode == "game") ? BOMBER_CAM_GAME : BOMBER_CAM_FREE;
            return json{
                {"requested",mode},
                //What it is RIGHT NOW, which is still the old one until the next pass runs.
                {"mode",camera_mode == BOMBER_CAM_GAME ? "game" : "free"},
                {"note","applied on the next physics pass; read bomber_state.camera to confirm"}
            };
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

    ImGui::TextDisabled("arrows/WASD walk   space drops a bomb   R is a new field   C frees the camera");
    /*
        The camera mode, as a radio pair rather than a checkbox: "free camera [x]" reads as a
        property of the camera, and this is a choice between two of them.

        SetCameraMode is a physics-thread call and this is the render thread - but DrawImGuiUI runs
        with physics_mutex held (see the threading note at the top of the header), so the two cannot
        be inside each other. The same footing every other control on this panel stands on.
    */
    ImGui::Text("Camera");
    ImGui::SameLine();
    if (ImGui::RadioButton("game",camera_mode == BOMBER_CAM_GAME)){
        SetCameraMode(BOMBER_CAM_GAME);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("free orbit",camera_mode == BOMBER_CAM_FREE)){
        SetCameraMode(BOMBER_CAM_FREE);
    }
    if (camera_mode == BOMBER_CAM_GAME){
        ImGui::SameLine();
        ImGui::TextDisabled("yaw %.0f  pitch %.0f  %.1fm",cam_yaw,cam_pitch,cam_distance);
    }
    ImGui::Text("Field seed %u   %i cells reachable",current_seed,maze.reachable_cells);
    //Visible in the panel as well as over MCP, because a lock that is on and forgotten looks
    //exactly like a keyboard that has stopped working.
    ImGui::Checkbox("lock out keyboard/mouse (MCP only)",&f_lock_human_input);
#ifdef USE_SOUND
    /*
        A mute, and nothing more - UpdateSound's diff keeps running either way.

        Setting a bool is all this does on purpose: DrawImGuiUI is the RENDER thread and every
        Play in this app is on the physics one, which is what keeps SoundSystem's lack of a lock
        safe. A panel button that played a test sound would quietly break that.
    */
    ImGui::Checkbox("sound",&f_sound_enabled);
    if (soundsystem && !soundsystem->f_initialised){
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.7f,0.2f,1.0f),"no audio device");
    }
#endif
    if (f_lock_human_input){
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.7f,0.2f,1.0f),"LOCKED");
    }
    ImGui::Text("Character  tile (%2i,%2i)%s",maze.player.tile_x,maze.player.tile_z,
                maze.player.step_ticks > 0 ? "  walking" : "");
    //Everything here is in TICKS rather than seconds, which is the house rule and is also what the
    //MCP tools report - a panel that said 1.4s while bomber_state said 84 would be two units to
    //hold in your head for no gain.
    ImGui::Text("Score %u%s",maze.player.score,maze.player.f_has_key ? "   KEY" : "");
    if (maze.player.f_alive){
        ImGui::Text("Health %i/%i",maze.player.health,MAZE_START_HEALTH);
        if (maze.player.shield_ticks > 0){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"SHIELD %i",maze.player.shield_ticks);
        }else if (maze.player.invuln_ticks > 0){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f,0.8f,0.3f,1.0f),"hit %i",maze.player.invuln_ticks);
        }
    }else{
        //A countdown now, not a count up - one clock for being dead. See MazeWalker::death_ticks.
        ImGui::TextColored(ImVec4(1.0f,0.4f,0.3f,1.0f),"Dead - back on the spawn in %i ticks",
                           maze.player.death_ticks);
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
    if (ImGui::Button(maze.player.f_has_key ? "Take the key back" : "Give the exit key")){
        //SubmitUICommand, never SubmitCommandAndWait - render thread with the mutex held. See the
        //note on the Bomb now button.
        SimCommand cmd;
        cmd.type = BOMBER_CMD_DOOR;
        cmd.value[0] = f_door_open ? 0.0f : 1.0f;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("exit (%i,%i)  %s",maze.door_x,maze.door_z,
                        maze.player.f_has_key ? "UNLOCKED" : "locked");

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
