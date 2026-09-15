#ifndef _BOMBER_MAZE_H_
#define _BOMBER_MAZE_H_

#include <stdint.h>

/*
    The rules of the bomberman field: a grid of tiles, a walker on it, a bomb, and a blast.

    NO ENGINE TYPE APPEARS IN THIS HEADER, the same split breakout/Field.h keeps. This side owns
    what is true - which tiles exist, where the character is, whether it may step there, when the
    bomb goes off and how far the flame reaches - and ApplicationBomber owns what that LOOKS like.
    The payoff is that the rules can be stepped, printed and reasoned about without a window, and
    that nothing in here can accidentally depend on a mesh having loaded.

    EVERYTHING IS COUNTED IN TICKS AND IN TILES. No seconds, no world units: the view multiplies
    tiles by its own cell size and the simulation never knows what that is. Durations in ticks is
    the house rule (see Scene::GetPhysicsTick); tiles rather than metres is what lets the art be
    rescaled without touching a rule.

    Tick() is the whole simulation. Call it once per tick that actually runs, hand it the intent
    for that tick, and read the state back out. It draws no randomness except in NewGame, so a
    given seed plus a given sequence of inputs is a given game.
*/

#define MAZE_W  16
#define MAZE_H  16

/*
    What is on a cell.

    PASSABILITY AND BLAST-BLOCKING ARE DIFFERENT QUESTIONS and this is the type that answers both,
    differently. Water stops a walker and does not stop a flame - it is at ground level, there is
    simply nothing to stand on. A wall stops both. Keeping the two predicates separate (IsPassable
    / BlocksBlast) rather than having one `f_solid` flag is what makes that expressible at all.
*/
enum MazeTile : uint8_t {
    MAZE_TILE_GRASS = 0,    //walkable
    MAZE_TILE_BRICK,        //walkable
    MAZE_TILE_ROCK,         //walkable
    MAZE_TILE_WATER,        //impassable, but does not stop a blast
    MAZE_TILE_WALL,         //impassable, and stops a blast
    MAZE_TILE_COUNT
};

/*
    What is standing on a cell, on top of its tile.

    Decoration, with one exception: a BRIDGE makes the water under it walkable. That is here rather
    than as a fifth tile type because the tile underneath is still water - it still does not block
    a blast, and a bomb that later removes the bridge leaves water behind rather than having to
    remember what was there.
*/
enum MazeDecor : uint8_t {
    MAZE_DECOR_NONE = 0,
    MAZE_DECOR_FLOWERS,
    MAZE_DECOR_PLANT,
    MAZE_DECOR_TURD,
    MAZE_DECOR_BRIDGE,
    MAZE_DECOR_COUNT
};

//Directions, in the order everything else in this app uses them - and the same order the blast
//shader reads its arm_limit vec4 in, which is not a coincidence worth breaking.
#define MAZE_DIR_NONE   (-1)
#define MAZE_DIR_EAST   0   //+X
#define MAZE_DIR_WEST   1   //-X
#define MAZE_DIR_NORTH  2   //-Z
#define MAZE_DIR_SOUTH  3   //+Z
#define MAZE_NUM_DIRS   4

//Ticks to walk from one tile's centre to the next. At 60 TPS this is a third of a second a tile,
//which is a brisk but controllable bomberman walk.
#define MAZE_STEP_TICKS         20
//Ticks a placed bomb burns before it goes off. Two seconds: long enough to walk three tiles clear,
//which is what makes placing one a decision rather than a button press.
#define MAZE_FUSE_TICKS         120
//How many tiles the flame reaches from the bomb, before the walls have their say.
#define MAZE_BLAST_RANGE        2
//How long the blast stays lit. The view's fire outlives this a little - see ApplicationBomber.
#define MAZE_BLAST_TICKS        110

//What the player is asking for this tick. A struct rather than two arguments because the next
//thing to arrive here is a second player, and then this is what gets duplicated rather than the
//signature.
struct MazeInput{
    int  direction = MAZE_DIR_NONE;   //held, not an edge: a walker keeps walking
    bool f_place_bomb = false;        //an EDGE: one press is one bomb
};

class Maze{
public:
    //--- the field ----------------------------------------------------------------------------
    uint8_t tile[MAZE_H][MAZE_W];
    uint8_t decor[MAZE_H][MAZE_W];

    //--- the walker ---------------------------------------------------------------------------
    /*
        The character moves TILE TO TILE, not freely.

        `tile_x/tile_z` is where it is, or - while a step is in progress - where it is GOING;
        `from_x/from_z` is where that step started and `step_ticks` counts down to arrival. So the
        authoritative answer to "which tile is the character on" is always a pair of integers, and
        the smooth position the view draws is an interpolation derived from them (CharX/CharZ).

        Free movement with a collision radius is the other way to do this and is what the original
        does. Grid stepping was chosen because it makes every rule that matters - can I go there,
        which tile does my bomb land on, did the flame catch me - a lookup instead of an overlap
        test, and none of those become easier by being approximate.
    */
    int tile_x = 1;
    int tile_z = 1;
    int from_x = 1;
    int from_z = 1;
    int step_ticks = 0;             //0 = standing on tile_x/tile_z
    int facing = MAZE_DIR_SOUTH;

    //--- the bomb -----------------------------------------------------------------------------
    //One at a time for now. `fuse_ticks` counts down; at zero it becomes the blast below.
    bool f_bomb = false;
    int  bomb_x = 0;
    int  bomb_z = 0;
    int  fuse_ticks = 0;

    //--- the blast ----------------------------------------------------------------------------
    //One at a time, for the same reason. `arm[]` is how far the flame actually reached in each
    //MAZE_DIR_*, in tiles, decided once when it went off.
    bool f_blast = false;
    int  blast_x = 0;
    int  blast_z = 0;
    int  blast_ticks = 0;           //counts UP from 0, so it can drive an age curve directly
    int  arm[MAZE_NUM_DIRS] = {0,0,0,0};

    //How many bombs have been detonated this game. The view seeds its noise off this so two
    //blasts in the same place do not look identical, which makes it part of the simulation
    //rather than a statistic.
    uint32_t blast_count = 0;

    //--- the game -----------------------------------------------------------------------------
    //Lays out a fresh field and puts the character on its spawn. Draws from `seed` and nothing
    //else, so the same seed is the same maze.
    void NewGame(uint32_t seed);

    //One tick of everything: the walker, the fuse, the blast clock. The only entry point that
    //changes anything.
    void Tick(const MazeInput& input);

    //--- queries ------------------------------------------------------------------------------
    bool InBounds(int x, int z) const {
        return x >= 0 && x < MAZE_W && z >= 0 && z < MAZE_H;
    }
    //Can a walker stand here? Water is out unless something has been laid over it.
    bool IsPassable(int x, int z) const;
    //Does this cell stop a flame? Walls only - see the note on MazeTile.
    bool BlocksBlast(int x, int z) const;
    //Is this cell currently on fire? The view does not need it yet, but the moment anything can
    //be hurt by a blast this is the question it will ask, and it belongs on this side.
    bool IsBurning(int x, int z) const;

    /*
        Where to draw the character, in TILES, interpolated across the step in progress.

        Fractional on purpose and the only place this class admits to anything between tiles: a
        walker that teleported a whole tile every 20 ticks would be correct and would look
        terrible. Nothing in the rules reads these.
    */
    float CharX() const;
    float CharZ() const;

    //Unit vector of a direction, in tiles. Static because the blast walk needs it before there is
    //anything to ask.
    static int DirX(int dir);
    static int DirZ(int dir);

private:
    //Starts a step towards `dir` if the target is passable and we are not already stepping.
    void TryStep(int dir);
    //Turns the live bomb into a blast, walking each arm out to the first wall.
    void Explode();

    /*
        The field's own random stream, seeded by NewGame.

        Deliberately NOT Application::rrand: that one is shared with the renderer and the debug UI,
        and there is an open backlog item about off-tick draws from those shifting it out from
        under the simulation. A generator that belongs to the rules and is seeded by the rules
        cannot have that problem. xorshift32 because it is four lines and the quality needed here
        is "scatters rocks convincingly".
    */
    uint32_t rng_state = 1;
    uint32_t NextRandom();
    int RandomBelow(int n);
};

#endif
