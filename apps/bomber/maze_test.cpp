/*
    A harness for Maze, with no engine anywhere near it.

    This is what the Maze/ApplicationBomber split is FOR: every rule the user asked about - a bomb
    clearing a hedge, an enemy cutting through one, a pickup appearing under it - is a statement
    about integers, and none of them needs a window, a mesh or a GPU to be checked. Running it here
    means a failure is a failing assertion with a board printed next to it rather than something
    that looked wrong in a screenshot.
*/
#include "Maze.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void Check(bool f_ok, const char* what){
    printf("  [%s] %s\n",f_ok ? "ok  " : "FAIL",what);
    if (!f_ok){
        failures++;
    }
}

static void PrintBoard(Maze& maze){
    for (int z = 0; z < MAZE_H; z++){
        char row[MAZE_W + 1];
        for (int x = 0; x < MAZE_W; x++){
            static const char GLYPH[MAZE_TILE_COUNT] = {'.',',',':','~','#','h','w'};
            char c = GLYPH[maze.tile[z][x]];
            if (maze.decor[z][x] == MAZE_DECOR_BRIDGE){
                c = (maze.pass_axis[z][x] == MAZE_AXIS_X) ? '-' : '|';
            }
            if (maze.item[z][x] != MAZE_ITEM_NONE && maze.IsPassable(x,z)){
                c = (maze.item[z][x] == MAZE_ITEM_HEALTH) ? '+' : '*';
            }
            for (int i = 0; i < maze.num_enemies; i++){
                if (maze.enemy[i].f_alive && maze.enemy[i].tile_x == x && maze.enemy[i].tile_z == z){
                    c = 'E';
                }
            }
            if (maze.player.f_alive && maze.player.tile_x == x && maze.player.tile_z == z){
                c = '@';
            }
            row[x] = c;
        }
        row[MAZE_W] = 0;
        printf("    %s\n",row);
    }
}

static int CountSoft(Maze& maze){
    int n = 0;
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            if (maze.IsSoft(x,z)){
                n++;
            }
        }
    }
    return n;
}

static void RunTicks(Maze& maze, int n, int dir = MAZE_DIR_NONE, bool f_bomb = false){
    for (int i = 0; i < n; i++){
        MazeInput in;
        in.direction = dir;
        in.f_place_bomb = f_bomb && (i == 0);
        maze.Tick(in);
    }
}

/*
    Hold a direction until the player commits to ONE step, then let go and let it land.

    Not the same as holding it for MAZE_STEP_TICKS: a held direction rolls straight into the next
    step on the tick the last one lands - deliberately, it is what makes walking smooth - so
    "MAZE_STEP_TICKS + a couple" actually walks TWO tiles. That cost a round of thinking the bridge
    rule had broken when what had moved was the test.

    Returns false if the step was refused, which is exactly what the directional tests assert on.
*/
static bool StepOnce(Maze& maze, int dir){
    int sx = maze.player.tile_x;
    int sz = maze.player.tile_z;
    bool f_moved = false;
    for (int i = 0; i < MAZE_STEP_TICKS * 2; i++){
        MazeInput in;
        in.direction = dir;
        maze.Tick(in);
        if (maze.player.tile_x != sx || maze.player.tile_z != sz){
            f_moved = true;
            break;
        }
    }
    //Nothing held, so it finishes the step it is on and does not begin another.
    while (maze.player.step_ticks > 0){
        RunTicks(maze,1);
    }
    return f_moved;
}

//--- generation ------------------------------------------------------------------------------------

static void TestGeneration(){
    printf("generation, seeds 1..200\n");
    bool f_all_styles_every_seed = true;
    bool f_soft_everywhere = true;
    bool f_spawn_clear = true;
    bool f_items_buried = true;
    bool f_enemies_far = true;
    bool f_playable = true;
    bool f_full_enemies = true;
    int worst_reachable = 9999;
    uint32_t worst_seed = 0;

    for (uint32_t seed = 1; seed <= 200; seed++){
        Maze maze;
        maze.NewGame(seed);

        bool f_style[MAZE_STYLE_COUNT] = {false,false,false};
        for (int z = 1; z < MAZE_H - 1; z++){
            for (int x = 1; x < MAZE_W - 1; x++){
                f_style[maze.zone[z][x]] = true;
            }
        }
        if (!(f_style[0] && f_style[1] && f_style[2])){
            f_all_styles_every_seed = false;
        }

        if (CountSoft(maze) < 8){
            f_soft_everywhere = false;
        }
        if (maze.reachable_cells < MAZE_MIN_PLAYABLE){
            f_playable = false;
        }
        if (maze.reachable_cells < worst_reachable){
            worst_reachable = maze.reachable_cells;
            worst_seed = seed;
        }
        if (maze.num_enemies != MAZE_MAX_ENEMIES){
            f_full_enemies = false;
        }

        //The spawn and its elbow room have to be walkable, or the game opens by digging.
        if (!maze.IsPassable(MAZE_SPAWN_X,MAZE_SPAWN_Z) ||
            !maze.IsPassable(MAZE_SPAWN_X + 1,MAZE_SPAWN_Z) ||
            !maze.IsPassable(MAZE_SPAWN_X,MAZE_SPAWN_Z + 1)){
            f_spawn_clear = false;
        }

        //Every item must start under something, or it is not a reward for digging.
        for (int z = 0; z < MAZE_H; z++){
            for (int x = 0; x < MAZE_W; x++){
                if (maze.item[z][x] != MAZE_ITEM_NONE && !maze.IsSoft(x,z)){
                    f_items_buried = false;
                }
            }
        }

        for (int i = 0; i < maze.num_enemies; i++){
            int dx = maze.enemy[i].tile_x - MAZE_SPAWN_X;
            int dz = maze.enemy[i].tile_z - MAZE_SPAWN_Z;
            if (dx < 0) dx = -dx;
            if (dz < 0) dz = -dz;
            if (dx + dz < MAZE_ENEMY_MIN_DIST){
                f_enemies_far = false;
            }
            if (!maze.IsPassable(maze.enemy[i].tile_x,maze.enemy[i].tile_z)){
                f_enemies_far = false;
            }
        }
    }

    Check(f_all_styles_every_seed,"every seed blends all three zone styles");
    Check(f_playable,             "no seed produces a board under MAZE_MIN_PLAYABLE cells");
    Check(f_full_enemies,         "every seed has room for the full set of enemies");
    Check(f_soft_everywhere,      "every seed scatters a useful number of soft blocks");
    Check(f_spawn_clear,          "spawn and its two neighbours are always walkable");
    Check(f_items_buried,         "every pickup starts buried under a soft block");
    Check(f_enemies_far,          "every enemy starts on open ground, MAZE_ENEMY_MIN_DIST away");
    printf("  smallest board over 200 seeds: %i cells (seed %u), floor is %i\n",
           worst_reachable,worst_seed,MAZE_MIN_PLAYABLE);

    Maze maze;
    maze.NewGame(7);
    printf("  seed 7: %i reachable, %i soft blocks, %i enemies\n",
           maze.reachable_cells,CountSoft(maze),maze.num_enemies);
    PrintBoard(maze);
}

//--- the bomb clears soft blocks -------------------------------------------------------------------

static void TestBlastClearsBlocks(){
    printf("a blast burns a soft block and stops at it\n");

    //A board built by hand, so the thing under test is the rule and not the generator's dice.
    Maze maze;
    maze.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            maze.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    maze.num_enemies = 0;
    maze.player.tile_x = maze.player.from_x = 5;
    maze.player.tile_z = maze.player.from_z = 5;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;

    //East: a hedge one tile away, with a pickup under it.
    maze.tile[5][6] = MAZE_TILE_HEDGE;
    maze.item[5][6] = MAZE_ITEM_HEALTH;
    //West: a hard wall one tile away.
    maze.tile[5][4] = MAZE_TILE_WALL;
    //North: wood two tiles away, so the arm has to pass one clear tile to reach it.
    maze.tile[3][5] = MAZE_TILE_WOOD;
    //South: nothing in MAZE_BLAST_RANGE, so the arm runs to its full length.

    uint32_t version_before = maze.field_version;
    Check(!maze.IsPassable(6,5),"a hedge is impassable before the bomb");
    Check(maze.BlocksBlast(6,5),"a hedge stops a flame before the bomb");

    RunTicks(maze,1,MAZE_DIR_NONE,true);            //drop
    RunTicks(maze,MAZE_FUSE_TICKS);                 //burn down and go off

    Check(maze.f_blast,"the bomb went off");
    Check(maze.arm[MAZE_DIR_EAST] == 1,
          "east arm reaches INTO the hedge and stops there (1 tile)");
    Check(maze.arm[MAZE_DIR_WEST] == 0,
          "west arm stops BEFORE the hard wall (0 tiles)");
    Check(maze.arm[MAZE_DIR_NORTH] == 2,
          "north arm crosses one clear tile and stops in the wood (2 tiles)");
    Check(maze.arm[MAZE_DIR_SOUTH] == MAZE_BLAST_RANGE,
          "south arm runs to its full range over open ground");

    Check(maze.tile[5][6] == MAZE_TILE_GRASS,"the hedge is gone");
    Check(maze.tile[3][5] == MAZE_TILE_GRASS,"the wooden wall is gone");
    Check(maze.tile[5][4] == MAZE_TILE_WALL, "the hard wall is still there");
    Check(maze.blocks_destroyed == 2,        "two blocks counted as destroyed");
    Check(maze.field_version > version_before,"field_version moved, so the view will refresh");
    Check(maze.IsPassable(6,5),              "what the hedge stood on is walkable now");
    Check(maze.item[5][6] == MAZE_ITEM_HEALTH,"the pickup under it survived the blast");
}

//--- the pickup ------------------------------------------------------------------------------------

static void TestPickup(){
    printf("a revealed pickup is taken by walking onto it\n");

    Maze maze;
    maze.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            maze.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    maze.num_enemies = 0;
    maze.player.tile_x = maze.player.from_x = 5;
    maze.player.tile_z = maze.player.from_z = 5;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;
    maze.health = 1;

    //One still buried, one out in the open.
    maze.tile[5][7] = MAZE_TILE_HEDGE;
    maze.item[5][7] = MAZE_ITEM_SHIELD;
    maze.item[5][6] = MAZE_ITEM_HEALTH;

    StepOnce(maze,MAZE_DIR_EAST);
    Check(maze.player.tile_x == 6,             "walked east one tile");
    Check(maze.item[5][6] == MAZE_ITEM_NONE,   "the loose pickup was taken");
    Check(maze.health == 2,                    "health went up by one");
    Check(maze.items_taken == 1,               "one pickup counted");

    //Walking into the hedge must not collect what is under it.
    Check(!StepOnce(maze,MAZE_DIR_EAST),       "the hedge blocked the next step");
    Check(maze.player.tile_x == 6,             "and the player stayed put");
    Check(maze.item[5][7] == MAZE_ITEM_SHIELD, "the buried pickup was NOT collected through it");
    Check(maze.shield_ticks == 0,              "and no shield was granted");
}

//--- the enemy cuts a hedge --------------------------------------------------------------------------

static void TestEnemyChopsHedge(){
    printf("an enemy cuts through a hedge and walks on\n");

    Maze maze;
    maze.NewGame(1);
    //A one-tile corridor running east, blocked by a hedge: the enemy has nowhere else to go, so
    //what is being tested is the chop and not the wander.
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            maze.tile[z][x] = MAZE_TILE_WALL;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    for (int x = 2; x <= 10; x++){
        maze.tile[8][x] = MAZE_TILE_GRASS;
    }
    maze.tile[8][6] = MAZE_TILE_HEDGE;

    maze.num_enemies = 1;
    maze.enemy[0] = MazeWalker();
    maze.enemy[0].tile_x = maze.enemy[0].from_x = 5;
    maze.enemy[0].tile_z = maze.enemy[0].from_z = 8;
    maze.enemy[0].step_total = MAZE_ENEMY_STEP_TICKS;
    maze.enemy[0].facing = MAZE_DIR_EAST;
    maze.enemy[0].f_alive = true;

    //Parked well out of the way, so nothing here is about the player.
    maze.player.tile_x = maze.player.from_x = 2;
    maze.player.tile_z = maze.player.from_z = 8;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;
    maze.health = MAZE_START_HEALTH;

    RunTicks(maze,1);
    Check(maze.enemy[0].chop_ticks > 0,"it starts cutting the hedge in front of it");
    Check(maze.tile[8][6] == MAZE_TILE_HEDGE,"the hedge is still there while it works");

    RunTicks(maze,MAZE_CHOP_TICKS);
    Check(maze.tile[8][6] == MAZE_TILE_GRASS,"the hedge is gone after MAZE_CHOP_TICKS");
    Check(maze.enemy[0].chop_ticks == 0,"and it has stopped cutting");

    //It should now be able to use the hole it made.
    RunTicks(maze,MAZE_ENEMY_STEP_TICKS * 2);
    Check(maze.enemy[0].tile_x >= 6,"it walked through the gap it cut");

    //A wooden wall is NOT its business.
    maze.tile[8][9] = MAZE_TILE_WOOD;
    int wood_before = maze.tile[8][9];
    RunTicks(maze,MAZE_CHOP_TICKS * 3);
    Check(maze.tile[8][9] == wood_before,"it never cuts a wooden wall, only a hedge");
}

//--- damage, shield and respawn -----------------------------------------------------------------------

static void TestPlayerCondition(){
    printf("the flame hurts, a shield stops it, and death puts you back on the spawn\n");

    Maze maze;
    maze.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            maze.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    maze.num_enemies = 0;
    maze.player.tile_x = maze.player.from_x = 5;
    maze.player.tile_z = maze.player.from_z = 5;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;
    maze.health = MAZE_START_HEALTH;

    //Stand on your own bomb. One flame must cost exactly one point, not one per tick.
    RunTicks(maze,1,MAZE_DIR_NONE,true);
    RunTicks(maze,MAZE_FUSE_TICKS + 10);
    Check(maze.health == MAZE_START_HEALTH - 1,"standing in the flame costs exactly one health");
    Check(maze.invuln_ticks > 0,               "and opens the mercy window");

    //Wait the blast out, take a shield, then sit in another one.
    RunTicks(maze,MAZE_BLAST_TICKS + MAZE_HIT_INVULN_TICKS + 5);
    maze.item[5][5] = MAZE_ITEM_SHIELD;
    RunTicks(maze,1);
    //One short of the full value on the tick it is taken: TickItems grants it and
    //TickPlayerCondition spends a tick of it in the same Tick. An accounting detail, not a bug -
    //but it is the kind of thing a test should state rather than paper over with a range.
    Check(maze.shield_ticks == MAZE_SHIELD_TICKS - 1,"the shield pickup was taken");

    int health_before = maze.health;
    RunTicks(maze,1,MAZE_DIR_NONE,true);
    RunTicks(maze,MAZE_FUSE_TICKS + 10);
    Check(maze.health == health_before,"a shield takes the whole blast for you");

    //Smoke does not kill. Let the flame pass, drop the shield, and stand in what is left.
    Maze late;
    late.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            late.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            late.decor[z][x] = MAZE_DECOR_NONE;
            late.item[z][x] = MAZE_ITEM_NONE;
            late.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    late.num_enemies = 0;
    late.player.tile_x = late.player.from_x = 5;
    late.player.tile_z = late.player.from_z = 5;
    late.player.f_alive = true;
    late.health = MAZE_START_HEALTH;
    RunTicks(late,1,MAZE_DIR_NONE,true);
    RunTicks(late,MAZE_FUSE_TICKS);
    RunTicks(late,MAZE_BLAST_HURT_TICKS + 5);
    int after_flame = late.health;
    late.invuln_ticks = 0;
    RunTicks(late,20);
    Check(late.f_blast,"the blast is still drawn this late");
    Check(late.health == after_flame,"but the smoke after it does not hurt");

    //Three flames kill, and death is followed by a respawn on the spawn, on the SAME field.
    Maze die;
    die.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            die.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            die.decor[z][x] = MAZE_DECOR_NONE;
            die.item[z][x] = MAZE_ITEM_NONE;
            die.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    die.num_enemies = 0;
    die.player.tile_x = die.player.from_x = 9;
    die.player.tile_z = die.player.from_z = 9;
    die.player.f_alive = true;
    die.health = MAZE_START_HEALTH;
    for (int hit = 0; hit < MAZE_START_HEALTH; hit++){
        RunTicks(die,1,MAZE_DIR_NONE,true);
        RunTicks(die,MAZE_FUSE_TICKS + 5);
        RunTicks(die,MAZE_BLAST_TICKS + MAZE_HIT_INVULN_TICKS + 5);
    }
    Check(die.deaths == 1,"three flames killed the player once");
    RunTicks(die,MAZE_RESPAWN_TICKS + 2);
    Check(die.player.f_alive,"and the player came back");
    Check(die.player.tile_x == MAZE_SPAWN_X && die.player.tile_z == MAZE_SPAWN_Z,
          "on the spawn");
    Check(die.health == MAZE_START_HEALTH,"with full health");
}

//--- an enemy dies in a blast -------------------------------------------------------------------------

static void TestEnemyDies(){
    printf("an enemy caught by a blast dies\n");

    Maze maze;
    maze.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            maze.tile[z][x] = MAZE_TILE_WALL;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    //A pocket the enemy cannot leave, one tile from where the bomb goes off.
    maze.tile[8][5] = MAZE_TILE_GRASS;
    maze.tile[8][6] = MAZE_TILE_GRASS;

    maze.num_enemies = 1;
    maze.enemy[0] = MazeWalker();
    maze.enemy[0].tile_x = maze.enemy[0].from_x = 6;
    maze.enemy[0].tile_z = maze.enemy[0].from_z = 8;
    maze.enemy[0].step_total = MAZE_ENEMY_STEP_TICKS;
    maze.enemy[0].f_alive = true;

    maze.player.tile_x = maze.player.from_x = 5;
    maze.player.tile_z = maze.player.from_z = 8;
    maze.player.f_alive = true;
    maze.health = MAZE_START_HEALTH;
    maze.shield_ticks = MAZE_SHIELD_TICKS;      //this test is about the enemy, not the player

    RunTicks(maze,1,MAZE_DIR_NONE,true);
    RunTicks(maze,MAZE_FUSE_TICKS + 3);
    Check(!maze.enemy[0].f_alive,"the enemy in the blast is dead");
    Check(maze.enemies_killed == 1,"and was counted");
}

//--- determinism --------------------------------------------------------------------------------------

static void TestDeterminism(){
    printf("the same seed and the same inputs are the same game\n");

    //The enemies draw from the maze's own stream every tick, so this is the property that says that
    //is still safe: two runs of the same seed, stepped the same way, must agree tick for tick.
    Maze a;
    Maze b;
    a.NewGame(23);
    b.NewGame(23);

    static const int DIRS[8] = {MAZE_DIR_EAST,MAZE_DIR_EAST,MAZE_DIR_SOUTH,MAZE_DIR_NONE,
                                MAZE_DIR_WEST,MAZE_DIR_NORTH,MAZE_DIR_SOUTH,MAZE_DIR_EAST};
    bool f_same = true;
    for (int i = 0; i < 900; i++){
        MazeInput in;
        in.direction = DIRS[(i / 13) % 8];
        in.f_place_bomb = (i % 211) == 7;
        a.Tick(in);
        b.Tick(in);
        if (a.player.tile_x != b.player.tile_x || a.player.tile_z != b.player.tile_z ||
            a.field_version != b.field_version || a.health != b.health ||
            a.blocks_destroyed != b.blocks_destroyed){
            f_same = false;
            break;
        }
        for (int e = 0; e < a.num_enemies; e++){
            if (a.enemy[e].tile_x != b.enemy[e].tile_x ||
                a.enemy[e].tile_z != b.enemy[e].tile_z ||
                a.enemy[e].chop_ticks != b.enemy[e].chop_ticks){
                f_same = false;
            }
        }
    }
    Check(f_same,"900 ticks of two identical games stayed identical");
    printf("  after 900 ticks: version %u, %u blocks destroyed, %u enemies killed, %i deaths\n",
           a.field_version,a.blocks_destroyed,a.enemies_killed,a.deaths);
}

//--- bridges still work -------------------------------------------------------------------------------

static void TestBridgeStillDirectional(){
    printf("the directional bridge rule still holds\n");

    Maze maze;
    maze.NewGame(1);
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
            maze.tile[z][x] = f_border ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            maze.decor[z][x] = MAZE_DECOR_NONE;
            maze.item[z][x] = MAZE_ITEM_NONE;
            maze.pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    maze.num_enemies = 0;
    //A bridge over water at (6,8), crossable east-west only.
    maze.tile[8][6] = MAZE_TILE_WATER;
    maze.decor[8][6] = MAZE_DECOR_BRIDGE;
    maze.pass_axis[8][6] = MAZE_AXIS_X;

    maze.player.tile_x = maze.player.from_x = 5;
    maze.player.tile_z = maze.player.from_z = 8;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;

    Check(StepOnce(maze,MAZE_DIR_EAST) && maze.player.tile_x == 6,
          "walked east onto the bridge");
    Check(!StepOnce(maze,MAZE_DIR_NORTH) && maze.player.tile_z == 8,
          "cannot step off it north - that is the rope side");
    Check(!StepOnce(maze,MAZE_DIR_SOUTH) && maze.player.tile_z == 8,
          "cannot step off it south either");
    Check(StepOnce(maze,MAZE_DIR_EAST) && maze.player.tile_x == 7,
          "but east, along the planks, is fine");
}

/*
    A long game on GENERATED boards, with nobody steering well.

    The hand-built tests above prove each rule in isolation; this one asks whether the rules ever
    actually meet on a real field. It is the check that would have caught an enemy that can cut a
    hedge in a corridor built for it but never finds one on a board the generator made - which is
    the kind of thing that looks fine in a unit test and is invisible in a screenshot.
*/
static void TestSoak(){
    printf("a long game on generated boards\n");

    uint32_t total_blocks = 0;
    uint32_t total_cut = 0;
    uint32_t total_kills = 0;
    int total_items = 0;
    int total_chops_seen = 0;
    int total_deaths = 0;

    for (uint32_t seed = 1; seed <= 6; seed++){
        Maze maze;
        maze.NewGame(seed);
        uint32_t rng = seed * 2654435761u + 1;
        int dir = MAZE_DIR_EAST;
        bool f_was_chopping[MAZE_MAX_ENEMIES] = {false,false,false,false};

        for (int i = 0; i < 12000; i++){
            //A cheap wander for the player, and a bomb now and then. Its own generator, so the
            //driver cannot shift the maze's stream out from under the thing being tested.
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if ((rng & 63) == 0){
                dir = (int)((rng >> 8) % MAZE_NUM_DIRS);
            }
            MazeInput in;
            in.direction = dir;
            in.f_place_bomb = ((rng >> 16) & 255) == 3;
            maze.Tick(in);

            for (int e = 0; e < maze.num_enemies; e++){
                bool f_now = maze.enemy[e].chop_ticks > 0;
                if (f_now && !f_was_chopping[e]){
                    total_chops_seen++;
                }
                f_was_chopping[e] = f_now;
            }
        }

        total_blocks += maze.blocks_destroyed;
        total_cut += maze.blocks_cut;
        total_kills += maze.enemies_killed;
        total_items += maze.items_taken;
        total_deaths += maze.deaths;
    }

    printf("  over 6 seeds x 12000 ticks: %u blocks bombed, %u hedges cut through "
           "(%i cuts started), %u enemies killed, %i pickups taken, %i deaths\n",
           total_blocks,total_cut,total_chops_seen,total_kills,total_items,total_deaths);
    Check(total_blocks > 0,     "bombs clear soft blocks on generated boards");
    Check(total_chops_seen > 0, "enemies find hedges to cut on generated boards");
    Check(total_cut > 0,        "and finish the cut, counted apart from bombed blocks");
    Check(total_items > 0,      "buried pickups get uncovered and collected");
}

int main(){
    TestGeneration();
    TestBlastClearsBlocks();
    TestPickup();
    TestEnemyChopsHedge();
    TestPlayerCondition();
    TestEnemyDies();
    TestBridgeStillDirectional();
    TestDeterminism();
    TestSoak();

    printf("\n%s (%i failing)\n",failures ? "FAILURES" : "all checks passed",failures);
    return failures ? 1 : 0;
}
