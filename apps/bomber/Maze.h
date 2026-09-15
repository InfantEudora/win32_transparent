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
    simply nothing to stand on. A wall stops both. Keeping the two predicates separate (CanEnter /
    BlocksBlast) rather than having one `f_solid` flag is what makes that expressible at all.
*/
enum MazeTile : uint8_t {
    MAZE_TILE_GRASS = 0,    //walkable
    MAZE_TILE_BRICK,        //walkable
    MAZE_TILE_ROCK,         //walkable
    MAZE_TILE_WATER,        //impassable on its own; a bridge over it is not
    MAZE_TILE_WALL,         //impassable, and stops a blast
    MAZE_TILE_COUNT
};

/*
    What is standing on a cell, on top of its tile.

    Decoration, with one exception: a BRIDGE makes the water under it crossable. That is here rather
    than as a fifth tile type because the tile underneath is still water - it still does not block a
    blast, and a bomb that later removes the bridge leaves water behind rather than having to
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

/*
    Which way a cell may be crossed.

    A GENERIC PROPERTY OF THE CELL, not a property of bridges. A bridge is the only thing that sets
    it today, because a bridge is the only asset there is with a direction - you walk along the
    planks and the rope rails are in the way at the sides. But a fence gap, a doorway, a one-tile
    corridor of scenery or a conveyor would all want exactly this, and none of them should have to
    add a case to the walker.

    The rule is applied to BOTH ends of a step: you may not enter a cell across its grain, and you
    may not leave one across its grain either. Checking only the target would let you step sideways
    off the middle of a bridge into the water.
*/
enum MazePassAxis : uint8_t {
    MAZE_AXIS_ANY = 0,      //no restriction
    MAZE_AXIS_X,            //east-west only
    MAZE_AXIS_Z             //north-south only
};

/*
    How a region of the board is laid out.

    A field is a BLEND of these rather than one of them, which is the difference between this and a
    plain bomberman board: the classic pillar grid is fair but samey, a labyrinth is interesting but
    claustrophobic with bombs in it, and an open plaza is where a chase happens. Mixing them in one
    field means the same match has corridors to be cornered in and rooms to run in.

    Reported per cell in `zone`, so the MCP map can show which is which and a layout that plays
    badly can be looked at rather than guessed about.
*/
enum MazeStyle : uint8_t {
    MAZE_STYLE_BOMBER = 0,  //the classic: open floor with a pillar on every even/even cell
    MAZE_STYLE_LABYRINTH,   //carved corridors and dead ends, on the same odd-cell lattice
    MAZE_STYLE_OPEN,        //a plaza, with a few clumps of wall for cover
    MAZE_STYLE_COUNT
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

//Where the character starts. Kept as a pair of defines because the generator has to guarantee this
//cell and its two neighbours are open, and that guarantee should name the same thing the spawn does.
#define MAZE_SPAWN_X            1
#define MAZE_SPAWN_Z            1

//What the player is asking for this tick. A struct rather than two arguments because the next thing
//to arrive here is a second player, and then this is what gets duplicated rather than the signature.
struct MazeInput{
    int  direction = MAZE_DIR_NONE;   //held, not an edge: a walker keeps walking
    bool f_place_bomb = false;        //an EDGE: one press is one bomb
};

class Maze{
public:
    //--- the field ----------------------------------------------------------------------------
    uint8_t tile[MAZE_H][MAZE_W];
    uint8_t decor[MAZE_H][MAZE_W];
    uint8_t pass_axis[MAZE_H][MAZE_W];
    uint8_t zone[MAZE_H][MAZE_W];       //a MazeStyle, for the map dump and the panel

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
    int tile_x = MAZE_SPAWN_X;
    int tile_z = MAZE_SPAWN_Z;
    int from_x = MAZE_SPAWN_X;
    int from_z = MAZE_SPAWN_Z;
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

    //How many bombs have been detonated this game. The view seeds its noise off this so two blasts
    //in the same place do not look identical, which makes it part of the simulation rather than a
    //statistic.
    uint32_t blast_count = 0;

    //How many cells the walker can actually get to, counted at generation time. Worth reporting:
    //it is the one number that says whether a seed produced a playable field or a pretty one with
    //most of itself walled off.
    int reachable_cells = 0;

    //--- the game -----------------------------------------------------------------------------
    //Lays out a fresh field and puts the character on its spawn. Draws from `seed` and nothing
    //else, so the same seed is the same field.
    void NewGame(uint32_t seed);

    //One tick of everything: the walker, the fuse, the blast clock. The only entry point that
    //changes anything.
    void Tick(const MazeInput& input);

    //--- queries ------------------------------------------------------------------------------
    bool InBounds(int x, int z) const {
        return x >= 0 && x < MAZE_W && z >= 0 && z < MAZE_H;
    }
    //Could anything stand here at all, ignoring which way it arrived? What the GENERATOR asks, and
    //what a reachability count is over. Movement asks CanEnter instead.
    bool IsPassable(int x, int z) const;
    /*
        May a walker step from one cell to an ADJACENT one?

        This and not IsPassable is what the walker asks, because a cell can be crossable one way and
        not the other - see MazePassAxis. Both ends are checked: leaving a bridge sideways is as
        wrong as entering one sideways.
    */
    bool CanEnter(int from_x, int from_z, int to_x, int to_z) const;
    //Does this cell stop a flame? Walls only - see the note on MazeTile.
    bool BlocksBlast(int x, int z) const;
    //Is this cell currently on fire? The view does not need it yet, but the moment anything can be
    //hurt by a blast this is the question it will ask, and it belongs on this side.
    bool IsBurning(int x, int z) const;

    /*
        Where to draw the character, in TILES, interpolated across the step in progress.

        Fractional on purpose and the only place this class admits to anything between tiles: a
        walker that teleported a whole tile every 20 ticks would be correct and would look terrible.
        Nothing in the rules reads these.
    */
    float CharX() const;
    float CharZ() const;

    //Unit vector of a direction, in tiles. Static because the blast walk needs it before there is
    //anything to ask.
    static int DirX(int dir);
    static int DirZ(int dir);
    //Which axis a direction runs along, as a MazePassAxis. Static for the same reason.
    static uint8_t DirAxis(int dir);

private:
    //Starts a step towards `dir` if it is allowed and we are not already stepping.
    void TryStep(int dir);
    //Turns the live bomb into a blast, walking each arm out to the first wall.
    void Explode();

    //--- generation ---------------------------------------------------------------------------
    //Each of these fills one rectangle of the interior, inclusive of its bounds.
    void FillBomber(int x0, int z0, int x1, int z1);
    void FillLabyrinth(int x0, int z0, int x1, int z1);
    void FillOpen(int x0, int z0, int x1, int z1);
    //Punches holes through a zone boundary so the pieces of the field are one field.
    void CarveDoorways(int split_x, int split_z);
    //Lays water in short runs and bridges some of them, ALIGNED with the run so a row of bridges
    //is one bridge. See the note at the definition.
    void AddWater();
    //Scatters flowers, plants and worse on dry open ground.
    void AddDecor();
    /*
        Flood fill from the spawn and turn anything the walker cannot get to into wall.

        Not tidiness: a blended field has zones that a doorway roll can leave sealed, and a sealed
        pocket of grass is worse than a wall there, because it looks like somewhere you are supposed
        to be able to reach. Water is left alone - it is impassable either way and reads as scenery.
        Returns the number of reachable cells.
    */
    int PruneUnreachable();

    /*
        The field's own random stream, seeded by NewGame.

        Deliberately NOT Application::rrand: that one is shared with the renderer and the debug UI,
        and there is an open backlog item about off-tick draws from those shifting it out from under
        the simulation. A generator that belongs to the rules and is seeded by the rules cannot have
        that problem. xorshift32 because it is four lines and the quality needed here is "scatters
        rocks convincingly".
    */
    uint32_t rng_state = 1;
    uint32_t NextRandom();
    int RandomBelow(int n);
};

#endif
