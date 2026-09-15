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
#include <stdlib.h>

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
            //One per tile type - anything short of that value-initialises to '\0' and prints
            //as a hole in the board. See the same table in ApplicationBomber::MapJson.
            static const char GLYPH[MAZE_TILE_COUNT] = {'.',',',':','~','#','h','w','D'};
            char c = GLYPH[maze.tile[z][x]];
            if (maze.decor[z][x] == MAZE_DECOR_BRIDGE){
                c = (maze.pass_axis[z][x] == MAZE_AXIS_X) ? '-' : '|';
            }
            if (maze.item[z][x] != MAZE_ITEM_NONE && maze.IsPassable(x,z)){
                static const char ITEM_GLYPH[MAZE_ITEM_COUNT] = {' ','+','S','K','c','d','x'};
                c = ITEM_GLYPH[maze.item[z][x]];
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

//--- treasure ----------------------------------------------------------------------------------

/*
    What a pickup is worth, and that taking one is what pays.

    Built on a cleared board rather than a generated one, because this is a statement about the five
    item types and not about where the generator puts them - a test that had to go looking for a
    crystal would be testing AddItems twice and TickItems not at all.
*/
static void TestScore(){
    printf("treasure pays, and the other two pickups do not\n");

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
    maze.player.tile_x = maze.player.from_x = 2;
    maze.player.tile_z = maze.player.from_z = 5;
    maze.player.step_ticks = 0;
    maze.player.f_alive = true;
    maze.score = 0;
    maze.health = MAZE_START_HEALTH;

    Check(MAZE_SCORE_COIN < MAZE_SCORE_DIAMOND && MAZE_SCORE_DIAMOND < MAZE_SCORE_CRYSTAL,
                                                      "coin < diamond < crystal");
    Check(MazeItemScore(MAZE_ITEM_HEALTH) == 0 && MazeItemScore(MAZE_ITEM_SHIELD) == 0,
                                                      "health and shield are worth no points");

    //A row of them to walk along, in the order the score ladder is written in.
    maze.item[5][3] = MAZE_ITEM_COIN;
    maze.item[5][4] = MAZE_ITEM_DIAMOND;
    maze.item[5][5] = MAZE_ITEM_CRYSTAL;
    maze.item[5][6] = MAZE_ITEM_SHIELD;

    StepOnce(maze,MAZE_DIR_EAST);
    Check(maze.score == MAZE_SCORE_COIN,              "a coin scored");
    StepOnce(maze,MAZE_DIR_EAST);
    Check(maze.score == MAZE_SCORE_COIN + MAZE_SCORE_DIAMOND,
                                                      "a diamond scored more");
    StepOnce(maze,MAZE_DIR_EAST);
    uint32_t before_shield = maze.score;
    Check(before_shield == MAZE_SCORE_COIN + MAZE_SCORE_DIAMOND + MAZE_SCORE_CRYSTAL,
                                                      "a crystal scored most");
    StepOnce(maze,MAZE_DIR_EAST);
    Check(maze.score == before_shield,                "the shield added nothing to the score");
    Check(maze.shield_ticks > 0,                      "but still granted its shield");
    Check(maze.items_taken == 4,                      "all four counted as pickups");

    //Dying is expensive enough already - see the note on `score`.
    maze.shield_ticks = 0;
    maze.invuln_ticks = 0;
    maze.health = 1;
    /*
        Killed by their own bomb, which is the DETERMINISTIC way to do it here.

        An enemy dropped onto the player's tile does not work: StepWalker commits to a neighbouring
        cell on the very next tick and `tile_x` is the DESTINATION, so it has walked off the tile
        before TickPlayerCondition ever looks. A blast sits still.
    */
    maze.f_bomb = true;
    maze.bomb_x = maze.player.tile_x;
    maze.bomb_z = maze.player.tile_z;
    maze.fuse_ticks = 1;
    RunTicks(maze,MAZE_RESPAWN_TICKS + 8);
    Check(maze.deaths > 0,                            "the player died");
    Check(maze.score == before_shield,                "and kept the score");

    //A new board is a new round.
    maze.NewGame(7);
    Check(maze.score == 0,                            "NewGame starts the score again");
}

/*
    What the generator buries, over enough boards that "exact rather than probable" is a claim with
    evidence behind it.

    The interesting half is the SHORT boards: MAZE_ITEM_ORDER is walked front to back, so a board
    with too few soft blocks loses the things at the back of it. That is deliberate and this is
    where it would be noticed changing.
*/
static void TestItemMix(){
    printf("what is buried is exact, not probable\n");

    int full_boards = 0;
    int bad_mix = 0;
    int total[MAZE_ITEM_COUNT] = {0};
    for (uint32_t seed = 1; seed <= 60; seed++){
        Maze maze;
        maze.NewGame(seed);
        int count[MAZE_ITEM_COUNT] = {0};
        int buried = 0;
        for (int z = 0; z < MAZE_H; z++){
            for (int x = 0; x < MAZE_W; x++){
                uint8_t it = maze.item[z][x];
                if (it != MAZE_ITEM_NONE){
                    count[it]++;
                    total[it]++;
                    buried++;
                }
            }
        }
        if (buried < MAZE_NUM_ITEMS){
            //A board too small to carry the whole list. Not a failure - see the note above.
            continue;
        }
        full_boards++;
        if (count[MAZE_ITEM_COIN] != 4 || count[MAZE_ITEM_HEALTH] != 1 ||
            count[MAZE_ITEM_SHIELD] != 1 || count[MAZE_ITEM_DIAMOND] != 1 ||
            count[MAZE_ITEM_CRYSTAL] != 1){
            bad_mix++;
        }
    }
    printf("  %i of 60 boards buried the whole list; %i coin, %i health, %i shield, %i diamond, "
           "%i crystal in all\n",full_boards,total[MAZE_ITEM_COIN],total[MAZE_ITEM_HEALTH],
           total[MAZE_ITEM_SHIELD],total[MAZE_ITEM_DIAMOND],total[MAZE_ITEM_CRYSTAL]);
    Check(full_boards > 50,                           "nearly every board has room for all eight");
    Check(bad_mix == 0,                               "and every one of those buried exactly the "
                                                      "list, not a roll of it");
}

//--- the exit ----------------------------------------------------------------------------------

/*
    Every board has a way out, and it is in a place you could walk up to.

    The three claims are separable and all three have failed in some form on some board while this
    was being written: the door has to EXIST, it has to be on the BORDER (a door in the middle of
    the field is a wall with a handle), and the cell INSIDE it has to be open or nothing can ever
    stand in front of it.
*/
static void TestExitPlaced(){
    printf("every board has an exit, on the border, reachable from inside\n");

    int missing = 0;
    int off_border = 0;
    int sealed = 0;
    int near_spawn = 0;
    int doors_per_board = 0;
    int wrong_count = 0;
    for (uint32_t seed = 1; seed <= 200; seed++){
        Maze maze;
        maze.NewGame(seed);
        int x = maze.door_x;
        int z = maze.door_z;
        doors_per_board = 0;
        for (int dz = 0; dz < MAZE_H; dz++){
            for (int dx = 0; dx < MAZE_W; dx++){
                if (maze.tile[dz][dx] == MAZE_TILE_DOOR){
                    doors_per_board++;
                }
            }
        }
        if (doors_per_board != 1){
            wrong_count++;
        }
        if (maze.tile[z][x] != MAZE_TILE_DOOR){
            missing++;
            continue;
        }
        bool f_border = (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1);
        if (!f_border){
            off_border++;
        }
        int ix = (x == 0) ? 1 : (x == MAZE_W - 1 ? MAZE_W - 2 : x);
        int iz = (z == 0) ? 1 : (z == MAZE_H - 1 ? MAZE_H - 2 : z);
        /*
            Open, or something a bomb can clear.

            PlaceDoor runs BEFORE AddSoftBlocks and only ever picks a cell whose inward neighbour is
            open floor - but a hedge may then be laid on that floor, which is fine and is arguably
            better: the exit takes a bomb to get to. What must never happen is a HARD wall there,
            and that is what this counts.
        */
        if (!maze.IsPassable(ix,iz) && !maze.IsSoft(ix,iz)){
            sealed++;
        }
        //Manhattan by hand: Maze::CellDistance is private, and spelling it out here is one
        //line rather than widening the class's surface for a test.
        int dist = abs(x - MAZE_SPAWN_X) + abs(z - MAZE_SPAWN_Z);
        if (dist < MAZE_DOOR_MIN_DIST){
            near_spawn++;
        }
    }
    printf("  over 200 seeds: %i missing, %i off the border, %i walled in, %i closer than "
           "MAZE_DOOR_MIN_DIST\n",missing,off_border,sealed,near_spawn);
    Check(missing == 0,                               "every board placed one");
    Check(wrong_count == 0,                           "exactly one door tile per board");
    Check(off_border == 0,                            "always in the border wall");
    Check(sealed == 0,                                "and never with a hard wall in front of it");
    //A PREFERENCE, not a rule - see MAZE_DOOR_MIN_DIST. Loud if it ever stops being rare.
    Check(near_spawn < 10,                            "and nearly always a walk away from the spawn");
}

/*
    The key opens it, and nothing else does.

    The last check is the one worth having: a door that could be bombed open would make the key
    decorative, and the reason it cannot is that MAZE_TILE_DOOR sits inside the IsBlock range - one
    line away from being wrong in either direction.
*/
static void TestKeyOpensDoor(){
    printf("the key opens the exit, and a bomb does not\n");

    Maze maze;
    maze.NewGame(3);
    int x = maze.door_x;
    int z = maze.door_z;

    Check(!maze.f_has_key,                            "a new board starts locked");
    Check(!maze.IsPassable(x,z),                      "and the door cannot be walked through");
    Check(maze.BlocksBlast(x,z),                      "a blast stops at it");
    Check(!maze.IsSoft(x,z),                          "it is not something a bomb can destroy");
    Check(!maze.IsChoppable(x,z),                     "and not something an enemy can cut");

    //Straight into the door, which is the strongest form of the claim.
    maze.f_bomb = true;
    maze.bomb_x = (x == 0) ? 1 : (x == MAZE_W - 1 ? MAZE_W - 2 : x);
    maze.bomb_z = (z == 0) ? 1 : (z == MAZE_H - 1 ? MAZE_H - 2 : z);
    maze.fuse_ticks = 1;
    RunTicks(maze,MAZE_BLAST_TICKS + 8);
    Check(maze.tile[z][x] == MAZE_TILE_DOOR,          "a bomb next to it leaves it standing");
    Check(!maze.IsPassable(x,z),                      "and still shut");

    maze.f_has_key = true;
    Check(maze.IsPassable(x,z),                       "the key makes it passable");
    Check(maze.BlocksBlast(x,z),                      "an OPEN door still stops a blast");

    //And through the real pickup path, on a cleared board so the walk is not the test.
    Maze m2;
    m2.NewGame(3);
    for (int cz = 1; cz < MAZE_H - 1; cz++){
        for (int cx = 1; cx < MAZE_W - 1; cx++){
            m2.tile[cz][cx] = MAZE_TILE_GRASS;
            m2.decor[cz][cx] = MAZE_DECOR_NONE;
            m2.item[cz][cx] = MAZE_ITEM_NONE;
            m2.pass_axis[cz][cx] = MAZE_AXIS_ANY;
        }
    }
    m2.num_enemies = 0;
    m2.player.tile_x = m2.player.from_x = 5;
    m2.player.tile_z = m2.player.from_z = 5;
    m2.player.step_ticks = 0;
    m2.item[5][6] = MAZE_ITEM_KEY;
    uint32_t score_before = m2.score;
    StepOnce(m2,MAZE_DIR_EAST);
    Check(m2.f_has_key,                               "walking onto the key picks it up");
    Check(m2.score == score_before,                   "and it is worth no points");
    Check(m2.IsPassable(m2.door_x,m2.door_z),         "which unlocked the door");
}

/*
    The key is always buried, and always somewhere a bomb can reach.

    The second half is the soft lock this was written to make impossible: a key under a block with
    four walls round it is a board that cannot be finished, and nothing else on the board would say
    so - you would simply run out of places to look.
*/
static void TestKeyIsDiggable(){
    printf("the key is always buried, and always diggable\n");

    int no_key = 0;
    int not_buried = 0;
    int sealed = 0;
    for (uint32_t seed = 1; seed <= 200; seed++){
        Maze maze;
        maze.NewGame(seed);
        int kx = -1;
        int kz = -1;
        int keys = 0;
        for (int z = 0; z < MAZE_H; z++){
            for (int x = 0; x < MAZE_W; x++){
                if (maze.item[z][x] == MAZE_ITEM_KEY){
                    keys++;
                    kx = x;
                    kz = z;
                }
            }
        }
        if (keys != 1){
            no_key++;
            continue;
        }
        if (!maze.IsSoft(kx,kz)){
            not_buried++;
        }
        bool f_open = false;
        for (int d = 0; d < MAZE_NUM_DIRS; d++){
            if (maze.IsPassable(kx + Maze::DirX(d),kz + Maze::DirZ(d))){
                f_open = true;
            }
        }
        if (!f_open){
            sealed++;
        }
    }
    printf("  over 200 seeds: %i without exactly one key, %i not under a block, %i with no open "
           "cell beside them\n",no_key,not_buried,sealed);
    Check(no_key == 0,                                "exactly one key on every board");
    Check(not_buried == 0,                            "always under a soft block");
    Check(sealed == 0,                                "and always with somewhere to bomb it from");
}

//--- lilly pads --------------------------------------------------------------------------------

/*
    A lilly goes on empty water and nowhere else.

    Three separate claims, and the third is the one that would break quietly: a pad on a BRIDGE cell
    would still be on water, so testing the tile alone would pass while the board looked wrong.
*/
static void TestLillies(){
    printf("lillies grow on empty water only\n");

    int lillies = 0;
    int on_dry_land = 0;
    int on_a_bridge = 0;
    int water_cells = 0;
    int boards_with_water = 0;
    int boards_with_lillies = 0;
    for (uint32_t seed = 1; seed <= 40; seed++){
        Maze maze;
        maze.NewGame(seed);
        int here = 0;
        int wet = 0;
        for (int z = 0; z < MAZE_H; z++){
            for (int x = 0; x < MAZE_W; x++){
                if (maze.tile[z][x] == MAZE_TILE_WATER){
                    wet++;
                    water_cells++;
                }
                if (maze.decor[z][x] != MAZE_DECOR_LILLY){
                    continue;
                }
                lillies++;
                here++;
                if (maze.tile[z][x] != MAZE_TILE_WATER){
                    on_dry_land++;
                }
                //A bridge cell cannot also be a lilly cell - they are the same byte - so what this
                //really checks is that the pad did not replace a span. IsPassable says it best:
                //water is only crossable where a bridge is, so a lilly cell must not be passable.
                if (maze.IsPassable(x,z)){
                    on_a_bridge++;
                }
            }
        }
        if (wet > 0){
            boards_with_water++;
        }
        if (here > 0){
            boards_with_lillies++;
        }
    }
    printf("  %i lillies over %i water cells on %i boards (%i of 40 boards had water)\n",
           lillies,water_cells,boards_with_lillies,boards_with_water);
    Check(lillies > 0,                                "some boards actually grew one");
    Check(on_dry_land == 0,                           "none of them on dry land");
    Check(on_a_bridge == 0,                           "and none on a cell you can walk across");
    //Roughly MAZE_LILLY_PCT of the water that has no span on it. Loose bounds on purpose: the point
    //is that the knob is connected, not that the RNG hits its mean over 40 boards.
    int pct = water_cells ? (lillies * 100) / water_cells : 0;
    Check(pct > 5 && pct < MAZE_LILLY_PCT + 10,       "and the density is in the right region");
}

//--- enemies that are not born in a box --------------------------------------------------------

/*
    No enemy starts somewhere it can do nothing, and none of them ends up stuck.

    TWO CLAIMS, and they need each other. The spawn filter alone is not enough: an enemy walled in
    on three sides with a hedge on the fourth passes it, and used to stand there for the whole round
    anyway, because a dead end made it reverse - and `back` is the opposite of `facing`, so it
    flipped between the same two directions for ever and never turned to look at the hedge.

    The soak is the honest test of both. Before this, 15 of 800 enemies over these seeds spawned
    with no exit at all and another 14 spawned next to a hedge they could never face.
*/
static void TestEnemiesCanAct(){
    printf("no enemy is born in a box, and none of them stays in one\n");

    int total = 0;
    int no_option = 0;
    int never_moved = 0;
    for (uint32_t seed = 1; seed <= 200; seed++){
        Maze maze;
        maze.NewGame(seed);
        int sx[MAZE_MAX_ENEMIES];
        int sz[MAZE_MAX_ENEMIES];
        int n = maze.num_enemies;
        for (int i = 0; i < n; i++){
            sx[i] = maze.enemy[i].tile_x;
            sz[i] = maze.enemy[i].tile_z;
            total++;
            bool f_can_act = false;
            for (int d = 0; d < MAZE_NUM_DIRS; d++){
                int nx = sx[i] + Maze::DirX(d);
                int nz = sz[i] + Maze::DirZ(d);
                if (maze.CanEnter(sx[i],sz[i],nx,nz) || maze.IsChoppable(nx,nz)){
                    f_can_act = true;
                }
            }
            if (!f_can_act){
                no_option++;
            }
        }

        //Nobody touching the controls: the enemies are the only thing moving, so anything still on
        //its spawn tile at the end never found anything to do.
        bool moved[MAZE_MAX_ENEMIES] = {false};
        MazeInput in;
        for (int t = 0; t < 2000; t++){
            maze.Tick(in);
            for (int i = 0; i < n; i++){
                if (maze.enemy[i].tile_x != sx[i] || maze.enemy[i].tile_z != sz[i]){
                    moved[i] = true;
                }
            }
        }
        for (int i = 0; i < n; i++){
            //A corpse is not stuck - it is dead, and the player's own bombs are not running here,
            //so this only happens to one that walked into a blast it started itself. It cannot.
            if (!moved[i] && maze.enemy[i].f_alive){
                never_moved++;
            }
        }
    }
    printf("  %i enemies over 200 seeds: %i spawned with no option, %i never moved in 2000 ticks\n",
           total,no_option,never_moved);
    Check(no_option == 0,                             "every enemy spawns able to walk or to cut");
    Check(never_moved == 0,                           "and every one of them got going");
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

//--- bridges span, rather than lying side by side -------------------------------------------------

/*
    The SHAPE of a crossing, over generated boards.

    This is here because the thing it guards against looked completely fine cell by cell. When a
    bridge was laid across its pond's run rather than along it, every water cell got its own
    one-tile plank and every single one of them was a legal, walkable, correctly-turned bridge - and
    the board read as three little bridges lying side by side instead of one crossing. No assertion
    about a cell could have caught that, because no cell was wrong. The span was.

    So this measures SPANS: a maximal run of bridge cells sharing one grain. Their length is the
    property the picture actually shows, and the two ends are where a crossing is or is not one.
*/
static void TestBridgesSpan(){
    printf("bridges span their pond and land on both banks, seeds 1..200\n");
    bool f_on_water = true;
    bool f_no_water_left_over = true;
    bool f_banks_are_land = true;
    int num_spans = 0;
    int total_cells = 0;
    int longest = 0;

    for (uint32_t seed = 1; seed <= 200; seed++){
        Maze maze;
        maze.NewGame(seed);
        for (int z = 0; z < MAZE_H; z++){
            for (int x = 0; x < MAZE_W; x++){
                if (maze.decor[z][x] != MAZE_DECOR_BRIDGE){
                    continue;
                }
                total_cells++;
                //A bridge is only ever over water, and it always states a grain - the walker and
                //the view have nothing else to go on.
                uint8_t axis = maze.pass_axis[z][x];
                if (maze.tile[z][x] != MAZE_TILE_WATER || axis == MAZE_AXIS_ANY){
                    f_on_water = false;
                    continue;
                }

                int dx = (axis == MAZE_AXIS_X) ? 1 : 0;
                int dz = 1 - dx;
                //Measure each span once, from its head: the cell behind a head is not another
                //bridge cell of the same grain.
                int bx = x - dx;
                int bz = z - dz;
                if (maze.InBounds(bx,bz) && maze.decor[bz][bx] == MAZE_DECOR_BRIDGE &&
                    maze.pass_axis[bz][bx] == axis){
                    continue;
                }

                int len = 1;
                while (maze.InBounds(x + dx * len,z + dz * len) &&
                       maze.decor[z + dz * len][x + dx * len] == MAZE_DECOR_BRIDGE &&
                       maze.pass_axis[z + dz * len][x + dx * len] == axis){
                    len++;
                }
                num_spans++;
                if (len > longest){
                    longest = len;
                }

                //The two ends. Water at either one is a span that stops short of its own pond -
                //the "crossing that does not cross" the generator is written to make impossible.
                int fx = x + dx * len;
                int fz = z + dz * len;
                if ((maze.InBounds(bx,bz) && maze.tile[bz][bx] == MAZE_TILE_WATER) ||
                    (maze.InBounds(fx,fz) && maze.tile[fz][fx] == MAZE_TILE_WATER)){
                    f_no_water_left_over = false;
                }
                /*
                    And both ends have to be ground you can end up standing on. A SOFT BLOCK COUNTS:
                    the generator scatters hedges and wood after the water is down, so a bank can
                    end up with one on it - which is a bridge you have to blow your way onto, not a
                    bridge that goes nowhere. A wall there would be the second thing.
                */
                if (!((maze.IsPassable(bx,bz) || maze.IsSoft(bx,bz)) &&
                      (maze.IsPassable(fx,fz) || maze.IsSoft(fx,fz)))){
                    f_banks_are_land = false;
                }
            }
        }
    }

    Check(num_spans > 0,          "generated boards have bridges at all");
    Check(f_on_water,             "every bridge cell is water and states a grain");
    Check(f_no_water_left_over,   "no span stops short of its own pond");
    Check(f_banks_are_land,       "every span ends on ground, or on a block over it");
    //The point of the whole exercise: a crossing is one long thing. If this drops back to 1 the
    //bridges are being laid across their ponds again and the board is a row of planks.
    Check(longest >= 3,           "and crossings get long - the longest here is a real span");
    printf("    %i spans over %i bridge cells, longest %i, mean %.2f\n",
           num_spans,total_cells,longest,num_spans ? (double)total_cells / num_spans : 0.0);
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
    TestScore();
    TestItemMix();
    TestLillies();
    TestExitPlaced();
    TestKeyOpensDoor();
    TestKeyIsDiggable();
    TestEnemiesCanAct();
    TestEnemyChopsHedge();
    TestPlayerCondition();
    TestEnemyDies();
    TestBridgesSpan();
    TestBridgeStillDirectional();
    TestDeterminism();
    TestSoak();

    printf("\n%s (%i failing)\n",failures ? "FAILURES" : "all checks passed",failures);
    return failures ? 1 : 0;
}
