#ifndef _BOMBER_MAZE_H_
#define _BOMBER_MAZE_H_

#include <stdint.h>

/*
    The rules of the bomberman field: a grid of tiles, walkers on it, a bomb, and a blast.

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
    for that tick, and read the state back out.

    IT IS DETERMINISTIC, and that is a stronger statement than "it draws no randomness": NewGame
    lays the field out of this class's OWN stream, and the enemies draw from that same stream as
    they wander. Nothing else touches it and nothing touches it off a tick, so a given seed plus a
    given sequence of inputs is still exactly one game - which is the property a replay needs, and
    the reason the enemies do not reach for Application::rrand. See the note on rng_state.
*/

#define MAZE_W  16
#define MAZE_H  16

/*
    What is on a cell.

    PASSABILITY, BLAST-BLOCKING AND DESTRUCTIBILITY ARE THREE DIFFERENT QUESTIONS and this is the
    type that answers all three, differently. Water stops a walker and does not stop a flame - it is
    at ground level, there is simply nothing to stand on. A brick wall stops both and survives.
    A hedge stops both and burns. Keeping the predicates separate (IsPassable / BlocksBlast /
    IsSoft) rather than having one `f_solid` flag is what makes that expressible at all.

    THE BLOCK TYPES ARE ADJACENT AND LAST, which is what lets IsBlock be a range test rather
    than a list that something will eventually be left out of.
*/
enum MazeTile : uint8_t {
    MAZE_TILE_GRASS = 0,    //walkable
    MAZE_TILE_BRICK,        //walkable
    MAZE_TILE_ROCK,         //walkable
    MAZE_TILE_WATER,        //impassable on its own; a bridge over it is not
    MAZE_TILE_WALL,         //impassable, stops a blast, and nothing can remove it
    MAZE_TILE_HEDGE,        //impassable, stops a blast, burns - and an enemy can cut through it
    MAZE_TILE_WOOD,         //impassable, stops a blast, burns
    /*
        The exit, in the border wall. THE ONE TILE WHOSE PASSABILITY IS NOT A PROPERTY OF THE CELL:
        it is a block until the key is found and floor afterwards, so IsPassable tests `f_has_key`
        before it tests IsBlock rather than the two being folded together.

        It stays inside the IsBlock range, which is right for everything else that asks: a blast
        never opens it (BlocksBlast), an enemy cannot cut it (IsChoppable is hedge only), nothing
        is scattered on it and no soft block is laid over it.
    */
    MAZE_TILE_DOOR,
    MAZE_TILE_COUNT
};

/*
    What is standing on a cell, on top of its tile.

    Decoration, with one exception: a BRIDGE makes the water under it crossable. That is here rather
    than as a further tile type because the tile underneath is still water - it still does not block
    a blast, and a bomb that later removes the bridge leaves water behind rather than having to
    remember what was there.
*/
enum MazeDecor : uint8_t {
    MAZE_DECOR_NONE = 0,
    MAZE_DECOR_FLOWERS,         //flat, in the grass
    MAZE_DECOR_FLOWERS_TALL,    //a standing clump, tall enough to read from across the board
    MAZE_DECOR_PLANT,
    MAZE_DECOR_TURD,
    /*
        A lilly pad. THE ONLY DECOR THAT GOES ON WATER, and only on water with nothing else on it -
        a pad growing through a bridge is the one arrangement that would read as a mistake.

        It is scenery and nothing more: it does not make the cell crossable (that is
        MAZE_DECOR_BRIDGE's job and IsPassable asks for that by name), it does not block a blast,
        and walking is not affected by it, because there is no walking on water to affect.
    */
    MAZE_DECOR_LILLY,
    MAZE_DECOR_BRIDGE,
    MAZE_DECOR_COUNT
};

/*
    What is buried on a cell, waiting to be picked up.

    A SEPARATE ARRAY FROM `decor` BECAUSE THE TWO OVERLAP IN TIME: an item is laid under a soft
    block at generation, the block may also be carrying a plant, and the item has to outlive the
    block being blown up. Packing both into one byte would mean losing one of them at exactly the
    moment the other appears.

    An item is HIDDEN while the cell it is on is not passable, which is the whole design: they are
    laid under hedges and wooden walls, so blowing blocks up is how you find them, and no separate
    "revealed" flag is needed - the tile answers it.
*/
enum MazeItem : uint8_t {
    MAZE_ITEM_NONE = 0,
    MAZE_ITEM_HEALTH,       //one point of health back, up to the starting maximum
    MAZE_ITEM_SHIELD,       //MAZE_SHIELD_TICKS of not being hurt by anything
    /*
        The way out. Worth no points and grants nothing that helps you survive - all it does is
        unlock MAZE_TILE_DOOR, which is why `f_has_key` is a flag on the game and not a counter.

        IT IS ALWAYS BURIED, first in MAZE_ITEM_ORDER, and AddItems will not lay it under a block
        with no open cell beside it - a key nobody can reach is a board that cannot be finished.
    */
    MAZE_ITEM_KEY,
    /*
        The three treasures. THEY DO NOTHING BUT SCORE, which is the point of having three of them:
        a pickup that changes how the game plays has to be balanced, and a pickup that is only worth
        points can be made rare purely because it is pretty.

        Ordered by what they are worth, and MazeItemScore below depends on that order only in the
        sense that it has to be kept in step with it - which is why it is a switch and not
        arithmetic on the enum.
    */
    MAZE_ITEM_COIN,
    MAZE_ITEM_DIAMOND,
    MAZE_ITEM_CRYSTAL,
    MAZE_ITEM_COUNT
};

//--- what treasure is worth ---------------------------------------------------------------------
/*
    Points per treasure, and the gaps are deliberately wide.

    5x between each step, so finding a crystal is not "a few more coins", it is the thing that
    happened this round. A flatter ladder would make the rare ones pointless to be pleased about,
    which is the only job they have.
*/
#define MAZE_SCORE_COIN         10
#define MAZE_SCORE_DIAMOND      50
#define MAZE_SCORE_CRYSTAL      250

//What one pickup adds to the score. 0 for the ones that pay in health or time instead.
static inline int MazeItemScore(uint8_t item){
    switch (item){
        case MAZE_ITEM_COIN:    return MAZE_SCORE_COIN;
        case MAZE_ITEM_DIAMOND: return MAZE_SCORE_DIAMOND;
        case MAZE_ITEM_CRYSTAL: return MAZE_SCORE_CRYSTAL;
        default:                return 0;
    }
}

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
/*
    How long the blast actually HURTS, which is much less than how long it is drawn.

    The fire and the smoke it leaves behind are one effect on screen and two different things to
    walk into: a blast that damaged for its whole visible life would make the tile it went off on
    unusable for nearly two seconds, and being killed by smoke reads as a bug rather than as a
    mistake. Three quarters of a second is about as long as the flame front is actually bright.
*/
#define MAZE_BLAST_HURT_TICKS   45

//Where the character starts. Kept as a pair of defines because the generator has to guarantee this
//cell and its two neighbours are open, and that guarantee should name the same thing the spawn does.
#define MAZE_SPAWN_X            1
#define MAZE_SPAWN_Z            1

/*
    How small a board is allowed to be before the layout is thrown away and rolled again.

    THE GENERATOR CAN PRODUCE A DUD. The zones are carved independently and joined by doorways that
    are placed, not routed, so a run of unlucky rolls can leave the spawn in a pocket with the rest
    of the board sealed off behind it - the reachability prune then turns all of that into wall and
    what is left is a corner of a field. Measured over seeds 1..12 the healthy ones land between
    116 and 151 cells; seed 12 produced FOURTEEN, with no room for a single enemy.

    A board that small is not a hard seed, it is a broken one, and it is not worth trying to prove
    the carve can never fail - re-rolling it is four lines and it cannot regress. The retries draw
    from the same stream, so the seed still determines the whole sequence and the field is still
    reproducible.
*/
#define MAZE_MIN_PLAYABLE       90
#define MAZE_LAYOUT_ATTEMPTS    8

//--- water and the bridges over it --------------------------------------------------------------
//How many ponds are ATTEMPTED. Attempted rather than laid: one whose seed lands on a pillar is
//skipped rather than moved, so a board usually ends up with a few less than this.
#define MAZE_NUM_PONDS          7
/*
    How long a pond's run TRIES to be, in cells, before the walls have their say.

    THIS IS THE LENGTH OF A BRIDGE, because a bridge spans the whole of the pond it is on - see
    AddWater. Five is the ceiling because a span longer than that is a corridor you cannot turn
    round in with a bomb behind you.

    The floor is three and it is not the shortest bridge you will see: a pond about to be bridged
    gives up a cell to any end that has no dry land to offer, so a three-cell run wedged between
    two walls ends up as a one-cell crossing. Measured over a thousand seeds this lands at a mean
    span of 3.1 cells with 11% of them single - a floor of two gave a mean of 2.7 with 18% single,
    and a ceiling of six bought 3.4 at the price of visibly less floor to play on.
*/
#define MAZE_POND_MIN           3
#define MAZE_POND_MAX           5
//Percentage of ponds that get a bridge. The rest stay water you have to walk around, which is what
//makes the ones you can cross worth noticing.
#define MAZE_BRIDGE_PCT         55

//--- soft blocks and what is under them ---------------------------------------------------------
//Percentage of open floor that gets a destructible block. A fifth is the classic feel: enough that
//the board changes shape as it is played, not so much that the opening move is always "dig out".
#define MAZE_SOFT_BLOCK_PCT     22
//Cells around the spawn kept clear of them, in manhattan distance. Two, so the first bomb always
//has somewhere to run to.
#define MAZE_SPAWN_CLEAR        2
/*
    How many pickups are buried, and EXACTLY WHAT THEY ARE.

    A fixed list handed out in order rather than a weighted roll, which is the same call AddItems
    already made about which cells to use: a board then has exactly one crystal rather than a 12%
    chance of two, and "how good was that board" is a question about where things were, not about
    whether the dice were kind.

    The list is walked front to back onto SHUFFLED cells, so the mix is exact while the placement is
    not. It also degrades the right way: a board with only three soft blocks left to bury things
    under keeps the coins and the health and loses the crystal, because the rare thing is at the
    back. Eight of them on a 16x16 board with ~34 soft blocks is roughly one dig in four.
*/
#define MAZE_NUM_ITEMS          9
static const uint8_t MAZE_ITEM_ORDER[MAZE_NUM_ITEMS] = {
    //FIRST, and that is the whole reason the list is walked in order: the key is the way out, so
    //it is the one thing a board too small to bury everything must still bury.
    MAZE_ITEM_KEY,
    MAZE_ITEM_COIN,    MAZE_ITEM_HEALTH,
    MAZE_ITEM_COIN,    MAZE_ITEM_DIAMOND,
    MAZE_ITEM_COIN,    MAZE_ITEM_SHIELD,
    MAZE_ITEM_COIN,    MAZE_ITEM_CRYSTAL,
};

/*
    How far from the spawn the exit has to be, in manhattan distance.

    Far enough that finding the key is a journey rather than a detour. It is a PREFERENCE and not a
    requirement: if no border cell that far out can be opened onto, PlaceDoor takes the best it can
    get, because a board with a door in an awkward place is still finishable and a board with no
    door at all is not.
*/
#define MAZE_DOOR_MIN_DIST      12

/*
    Percentage of empty water cells that get a lilly pad.

    A third, so a pond reads as a pond rather than as a lilly farm, and so two ponds on the same
    board do not look like copies of each other.
*/
#define MAZE_LILLY_PCT          33

//--- the player ---------------------------------------------------------------------------------
#define MAZE_START_HEALTH       3
//Mercy window after being hit, so one flame is one hit rather than one per tick.
#define MAZE_HIT_INVULN_TICKS   60
//What a shield pickup is worth. Ten seconds - long enough to plan a run through a burning field.
#define MAZE_SHIELD_TICKS       600
//How long the player lies there before reappearing on the spawn. The FIELD IS NOT REGENERATED:
//dying mid-experiment and losing the board you were looking at is worse than dying.
#define MAZE_RESPAWN_TICKS      120

//--- the enemy ----------------------------------------------------------------------------------
#define MAZE_MAX_ENEMIES        4
//Slower than the player's MAZE_STEP_TICKS, so walking away from one always works and the threat is
//being cornered rather than being outrun.
#define MAZE_ENEMY_STEP_TICKS   30
//Ticks an enemy spends cutting through one hedge. A second and a half: long enough to see it
//happening and to get there first, short enough that a hedge is cover rather than a wall.
#define MAZE_CHOP_TICKS         90
//Minimum manhattan distance from the spawn an enemy may start at, so the game does not open with
//one already touching you.
#define MAZE_ENEMY_MIN_DIST     7
/*
    How long a dead walker stays on the board before it stops being drawn.

    Long enough for the Enemy_Death clip (0.83s, so 50 ticks at 60 TPS) to play out and be seen,
    plus a moment holding the last frame. A body that vanished the instant the flame touched it
    read as the enemy having never been there - which is the wrong feedback for the one thing the
    player is actually trying to do.
*/
#define MAZE_DEATH_TICKS        75

//What the player is asking for this tick. A struct rather than two arguments because the next thing
//to arrive here is a second player, and then this is what gets duplicated rather than the signature.
struct MazeInput{
    int  direction = MAZE_DIR_NONE;   //held, not an edge: a walker keeps walking
    bool f_place_bomb = false;        //an EDGE: one press is one bomb
};

/*
    Something that occupies a tile and moves between tiles. The player is one; so is each enemy.

    A STRUCT RATHER THAN SIX FIELDS WITH A PREFIX, because the player and an enemy differ in what
    decides their direction and in nothing else at all - they share the step, the facing, the
    interpolation and every rule about which cells may be entered. Any rule written against one of
    them and not the other would be a bug, and the shape of the code is what stops that.

    Movement is TILE TO TILE, not free. `tile_x/tile_z` is where it is, or - while a step is in
    progress - where it is GOING; `from_x/from_z` is where that step started and `step_ticks` counts
    down to arrival. So the authoritative answer to "which tile is this on" is always a pair of
    integers, and the smooth position the view draws is an interpolation derived from them.

    Free movement with a collision radius is the other way to do this and is what the original does.
    Grid stepping was chosen because it makes every rule that matters - can I go there, which tile
    does my bomb land on, did the flame catch me - a lookup instead of an overlap test, and none of
    those become easier by being approximate.
*/
struct MazeWalker{
    int tile_x = 0;
    int tile_z = 0;
    int from_x = 0;
    int from_z = 0;
    int step_ticks = 0;             //0 = standing on tile_x/tile_z
    /*
        How long the step in progress was given, so the interpolation can be a fraction of it.

        Carried per walker rather than read from a define, because the player and an enemy move at
        different speeds and X()/Z() would otherwise have to be told which of the two it is holding.
        A walker that knows its own step length is one that can be handed any speed at all.
    */
    int step_total = MAZE_STEP_TICKS;
    int facing = MAZE_DIR_SOUTH;
    bool f_alive = false;
    /*
        Ticks left of this walker's death, counting DOWN from MAZE_DEATH_TICKS.

        A walker that has just died is `f_alive == false` with `death_ticks > 0`: gone as far as
        every rule is concerned - it cannot be hit again, it blocks nothing, it chops nothing - and
        still on the board as far as the view is concerned, which is what gives a death animation
        somewhere to play. At zero it stops being drawn at all.

        The duration is here rather than in the view for the usual reason: it is a duration, it is
        counted in ticks, and a paused simulation should freeze a death half-finished like it
        freezes everything else.
    */
    int death_ticks = 0;
    /*
        Ticks left of the swing this walker is in the middle of.

        Enemies only - the player has no melee - but it lives here rather than in a parallel array
        because it is a reason to be STANDING STILL, and standing still is a property of a walker.
        Nonzero means busy: no step is begun and no direction is chosen until it runs out.
    */
    int chop_ticks = 0;

    //Where to draw it, in TILES, interpolated across the step in progress. Fractional on purpose
    //and the only place this admits to anything between tiles: a walker that teleported a whole
    //tile every 20 ticks would be correct and would look terrible. No rule reads these.
    float X() const;
    float Z() const;
};

class Maze{
public:
    //--- the field ----------------------------------------------------------------------------
    uint8_t tile[MAZE_H][MAZE_W];
    uint8_t decor[MAZE_H][MAZE_W];
    uint8_t item[MAZE_H][MAZE_W];
    uint8_t pass_axis[MAZE_H][MAZE_W];
    uint8_t zone[MAZE_H][MAZE_W];       //a MazeStyle, for the map dump and the panel

    /*
        Bumped every time a cell's CONTENTS change - a block destroyed, an item taken.

        The view's reason for existing: ApplicationBomber builds one object per cell once and then
        only has to look at the board again when this number moves, instead of either rebuilding
        the field (hundreds of objects, on the physics thread) or comparing 256 cells every tick.
        NewGame bumps it too, so a view that has never looked is always out of date.
    */
    uint32_t field_version = 0;

    //--- the walkers --------------------------------------------------------------------------
    MazeWalker player;
    //Fixed array rather than a vector: the count is small and known, this header is meant to stay
    //free of anything that allocates, and a dead enemy is a walker with f_alive false rather than
    //a hole in a list.
    MazeWalker enemy[MAZE_MAX_ENEMIES];
    int num_enemies = 0;

    //--- the player's condition -----------------------------------------------------------------
    int  health = MAZE_START_HEALTH;
    int  shield_ticks = 0;          //>0: nothing can hurt you, and you can see it
    int  invuln_ticks = 0;          //the mercy window after a hit; not a shield, just not twice
    int  dead_ticks = 0;            //counts UP while lying there, to MAZE_RESPAWN_TICKS
    int  deaths = 0;
    int  items_taken = 0;
    /*
        The exit, and whether it is unlocked.

        The door is a CELL, so it is in `tile` like everything else; these two are only where it is,
        so nothing has to search the border for it. `f_has_key` is what opens it - one flag, because
        one key opens one door and a count would imply otherwise.
    */
    int  door_x = 0;
    int  door_z = 0;
    bool f_has_key = false;
    /*
        Points. Treasure only - health and shields pay in other currencies and are worth 0 here.

        Survives a death, because dying already costs a life and the respawn, and taking the score
        as well would mean a board is only worth playing from full health. It does NOT survive
        NewGame: a seed lays out a board and a board is a round.
    */
    uint32_t score = 0;

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
    /*
        Soft blocks removed, counted separately by WHO removed them.

        Two numbers rather than one, because they answer different questions and the first run of
        this in the live game is exactly why: a bomb took one block while the board lost four, and
        with a single counter that difference was invisible - the other three were enemies cutting
        hedges, which is the whole feature. `blocks_destroyed` is the difference between a bomb that
        was placed well and one that was merely placed; `blocks_cut` is the only number that says
        the enemies are doing anything at all when you are not watching them.
    */
    uint32_t blocks_destroyed = 0;      //by a blast
    uint32_t blocks_cut = 0;            //by an enemy's shears
    uint32_t enemies_killed = 0;

    //How many cells the walker can actually get to, counted at generation time OVER THE HARD WALLS
    //ONLY - soft blocks are laid afterwards, and a pocket behind one is somewhere you can get to
    //once you have dug. Worth reporting: it is the one number that says whether a seed produced a
    //playable field or a pretty one with most of itself walled off.
    int reachable_cells = 0;

    //--- the game -----------------------------------------------------------------------------
    //Lays out a fresh field and puts the character on its spawn. Draws from `seed` and nothing
    //else, so the same seed is the same field.
    void NewGame(uint32_t seed);

    //One tick of everything: the walkers, the fuse, the blast clock, and who got hurt. The only
    //entry point that changes anything.
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
    //Does this cell stop a flame? Any of the three block types - see the note on MazeTile.
    bool BlocksBlast(int x, int z) const;
    //Is there a block standing here at all, of any kind?
    bool IsBlock(int x, int z) const;
    //Is the block here one a blast can remove? Hedge and wood.
    bool IsSoft(int x, int z) const;
    //Is it one an ENEMY can cut through? Hedge only: they have shears, not an axe.
    bool IsChoppable(int x, int z) const;
    //Is this cell currently on fire?
    bool IsBurning(int x, int z) const;
    //Is it on fire and still HOT - the part of a blast that actually takes a point of health? See
    //MAZE_BLAST_HURT_TICKS for why this is not the same question.
    bool IsDangerous(int x, int z) const;

    //Unit vector of a direction, in tiles. Static because the blast walk needs it before there is
    //anything to ask.
    static int DirX(int dir);
    static int DirZ(int dir);
    //Which axis a direction runs along, as a MazePassAxis. Static for the same reason.
    static uint8_t DirAxis(int dir);
    //The direction that undoes `dir`. Static for the same reason; used by the enemy's wander.
    static int DirOpposite(int dir);

private:
    //Starts a step towards `dir` if it is allowed and the walker is not already busy. Shared by
    //the player and the enemies, which is the point of MazeWalker existing.
    void StepWalker(MazeWalker& walker, int dir, int step_ticks);
    //Turns the live bomb into a blast, walking each arm out and burning what it can.
    void Explode();
    //Removes the block on a cell, whatever kind it is, and bumps field_version.
    void ClearBlock(int x, int z);
    //One tick of every enemy: finish a step, finish a chop, or decide what to do next.
    void TickEnemies();
    //Picks up whatever the player is standing on.
    void TickItems();
    //Works out whether the player got hurt this tick, and runs the respawn clock.
    void TickPlayerCondition();

    //--- generation ---------------------------------------------------------------------------
    /*
        One attempt at a terrain layout: zones, border, doorways, water, spawn, prune.

        Split out of NewGame so it can be RETRIED - see MAZE_MIN_PLAYABLE. Returns how many cells
        the walker can reach, which is the number that decides whether this attempt was any good.
        Everything that depends on the terrain being settled - soft blocks, items, enemies, decor -
        stays in NewGame and runs once, on the attempt that was kept.
    */
    int LayOutTerrain();
    //Each of these fills one rectangle of the interior, inclusive of its bounds.
    void FillBomber(int x0, int z0, int x1, int z1);
    void FillLabyrinth(int x0, int z0, int x1, int z1);
    void FillOpen(int x0, int z0, int x1, int z1);
    //Punches holes through a zone boundary so the pieces of the field are one field.
    void CarveDoorways(int split_x, int split_z);
    //Lays water in short straight runs and bridges some of them ALONG the run, so a crossing is one
    //long span rather than a row of parallel planks. See the note at the definition.
    void AddWater();
    //Scatters hedges and wooden walls over the open floor. AFTER the reachability pass - see there.
    void AddSoftBlocks();
    //Buries the pickups under soft blocks.
    void AddItems();
    //Puts the enemies down, far enough from the spawn to be a threat rather than an ambush.
    void PlaceEnemies();
    /*
        Puts the exit in the border wall, on a cell that can be reached from inside.

        AFTER the terrain has settled and BEFORE the soft blocks, so nothing is laid on top of it -
        the door is a block, and AddSoftBlocks only builds on open floor. A soft block CAN land on
        the cell in front of it, which is fine: that is a bomb, not a lock.
    */
    void PlaceDoor();
    //Is there anywhere next to this cell a walker could stand? What makes a buried thing DIGGABLE:
    //a blast reaches INTO a soft block from the cell beside it, so one open neighbour is enough.
    bool HasOpenNeighbour(int x, int z) const;
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
