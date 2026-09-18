#ifndef _BOMBER_MAZE_H_
#define _BOMBER_MAZE_H_

#include <stdint.h>

/*
    The rules of the bomberman field: a grid of tiles, walkers on it, a bomb, and a blast.

    No engine type appears in this header - the same split breakout/Field.h keeps. This side owns
    what is true, ApplicationBomber owns what it looks like, so the rules can be stepped, printed
    and reasoned about without a window.

    Everything is counted in TICKS and in TILES. The view multiplies tiles by its own cell size;
    the simulation never knows what that is, which is what lets the art be rescaled.

    Deterministic: NewGame lays the field out of this class's own stream and the enemies draw from
    that same stream as they wander, so a seed plus a sequence of inputs is exactly one game - the
    property a replay needs. See rng_state.

    Tick() is the whole simulation. Call it once per tick that runs, hand it the intent for that
    tick, and read the state back out.
*/

#define MAZE_W  16
#define MAZE_H  16

/*
    What is on a cell.

    Passability, blast-blocking and destructibility are three different questions - water stops a
    walker but not a flame, a hedge stops both and burns - so they are three predicates
    (IsPassable / BlocksBlast / IsSoft) rather than one f_solid flag.

    The block types are adjacent and last, which lets IsBlock be a range test.
*/
enum MazeTile : uint8_t {
    MAZE_TILE_GRASS = 0,    //walkable
    MAZE_TILE_BRICK,        //walkable
    MAZE_TILE_ROCK,         //walkable
    MAZE_TILE_WATER,        //impassable on its own; a bridge over it is not
    MAZE_TILE_WALL,         //impassable, stops a blast, and nothing can remove it
    MAZE_TILE_HEDGE,        //impassable, stops a blast, burns - and an enemy can cut through it
    MAZE_TILE_WOOD,         //impassable, stops a blast, burns
    //The exit. The one tile whose passability is not a property of the cell: a block until the key
    //is found, floor after, so IsPassable tests f_has_key before it tests IsBlock. It stays in the
    //IsBlock range for everything else that asks - no blast opens it, no enemy cuts it, nothing is
    //laid on it.
    MAZE_TILE_DOOR,
    MAZE_TILE_COUNT
};

/*
    What is standing on a cell, on top of its tile. Scenery, with one exception: a bridge makes the
    water under it crossable. That is decor rather than a tile type so the cell underneath stays
    water - it still does not block a blast, and blowing the bridge up leaves water behind.
*/
enum MazeDecor : uint8_t {
    MAZE_DECOR_NONE = 0,
    MAZE_DECOR_FLOWERS,         //flat, in the grass
    MAZE_DECOR_FLOWERS_TALL,    //a standing clump, tall enough to read from across the board
    MAZE_DECOR_PLANT,
    MAZE_DECOR_TURD,
    MAZE_DECOR_LILLY,           //only goes on water, and only water with nothing else on it
    MAZE_DECOR_BRIDGE,          //makes the water under it crossable; IsPassable asks for it by name
    MAZE_DECOR_COUNT
};

/*
    What is buried on a cell, waiting to be picked up.

    A separate array from `decor` because the two overlap in time: an item is laid under a soft
    block that may also be carrying a plant, and it has to outlive the block being blown up.

    An item is HIDDEN while its cell is not passable, which is the whole design - they are buried
    under soft blocks, so digging is how you find them and no "revealed" flag is needed.
*/
enum MazeItem : uint8_t {
    MAZE_ITEM_NONE = 0,
    MAZE_ITEM_HEALTH,       //one point of health back, up to the starting maximum
    MAZE_ITEM_SHIELD,       //MAZE_SHIELD_TICKS of not being hurt by anything
    //The way out: unlocks MAZE_TILE_DOOR and grants nothing else, which is why f_has_key is a flag
    //and not a counter. Always buried, first in MAZE_ITEM_ORDER, and never under a block with no
    //open cell beside it - a key nobody can reach is a board that cannot be finished.
    MAZE_ITEM_KEY,
    //The three treasures. They do nothing but score, which is the point of having three: a pickup
    //worth only points can be made rare purely because it is pretty. Ordered by what they are
    //worth, and MazeItemScore has to be kept in step with that order.
    MAZE_ITEM_COIN,
    MAZE_ITEM_DIAMOND,
    MAZE_ITEM_CRYSTAL,
    MAZE_ITEM_COUNT
};

//--- what treasure is worth ---------------------------------------------------------------------
//5x between each step, so finding a crystal is not "a few more coins" but the thing that happened
//this round. A flatter ladder makes the rare ones pointless to be pleased about.
#define MAZE_SCORE_COIN         10
#define MAZE_SCORE_DIAMOND      50
#define MAZE_SCORE_CRYSTAL      250

//What one pickup adds to the score. 0 for the ones that pay in health or time instead. A switch
//rather than arithmetic on the enum, so reordering MazeItem cannot silently change a value.
static inline int MazeItemScore(uint8_t item){
    switch (item){
        case MAZE_ITEM_COIN:    return MAZE_SCORE_COIN;
        case MAZE_ITEM_DIAMOND: return MAZE_SCORE_DIAMOND;
        case MAZE_ITEM_CRYSTAL: return MAZE_SCORE_CRYSTAL;
        default:                return 0;
    }
}

/*
    Which way a cell may be crossed - a generic property of the cell, not a property of bridges.
    A bridge is the only thing that sets it today, but a fence gap, a doorway or a conveyor would
    all want the same, and none of them should have to add a case to the walker.

    Applied to BOTH ends of a step: checking only the target would let you step sideways off the
    middle of a bridge into the water.
*/
enum MazePassAxis : uint8_t {
    MAZE_AXIS_ANY = 0,      //no restriction
    MAZE_AXIS_X,            //east-west only
    MAZE_AXIS_Z             //north-south only
};

/*
    How a region of the board is laid out. A field is a BLEND of these rather than one of them,
    which is the difference between this and a plain bomberman board: the pillar grid is fair but
    samey, a labyrinth is claustrophobic with bombs in it, and a plaza is where a chase happens.
    One field wants all three. Reported per cell in `zone`, so a bad layout can be looked at.
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
//How long the blast actually HURTS, which is much less than how long it is drawn. Fire and the
//smoke after it are one effect on screen and two things to walk into: damaging for the whole
//visible life makes the tile unusable for two seconds, and dying to smoke reads as a bug.
#define MAZE_BLAST_HURT_TICKS   45

/*
    How far along its border the way IN sits, counted from the low corner.

    The spawn is NOT a fixed cell any more - it is the cell just inside whichever border the player
    walked in through, so `entry_dir` decides both. One offset rather than a pair of coordinates,
    because the four cases are the same corner mirrored and writing them out four times is four
    chances to mirror one of them wrong. See Maze::SetEntry, which is where they are derived.

    Offset 1 rather than the middle of the border on purpose: it keeps the spawn in a corner, which
    is where it has always been and is what MAZE_DOOR_MIN_DIST is tuned against.
*/
#define MAZE_ENTRY_OFFSET       1

/*
    How small a board may be before the layout is thrown away and rolled again.

    The generator can produce a dud: zones are carved independently and joined by doorways that are
    placed rather than routed, so an unlucky run leaves the spawn in a pocket and the reachability
    prune turns the rest into wall. Healthy seeds land between 116 and 151 cells; seed 12 produced
    FOURTEEN. Re-rolling is four lines and cannot regress, and the retries draw from the same
    stream, so the seed still determines the whole field.
*/
#define MAZE_MIN_PLAYABLE       90
#define MAZE_LAYOUT_ATTEMPTS    8

//--- water and the bridges over it --------------------------------------------------------------
//How many ponds are ATTEMPTED - one whose seed lands on a pillar is skipped rather than moved, so
//a board usually ends up with a few less than this.
#define MAZE_NUM_PONDS          7
/*
    How long a pond's run tries to be, in cells - and so how long a BRIDGE is, since a bridge spans
    the whole pond it is on (see AddWater). Five is the ceiling because a longer span is a corridor
    you cannot turn round in with a bomb behind you.

    The floor of three is not the shortest bridge you will see: a pond gives up a cell to any end
    with no dry land to offer. Over a thousand seeds that lands at a mean span of 3.1 cells with
    11% of them single - a floor of two gave 2.7 with 18% single, a ceiling of six bought 3.4 at
    the price of visibly less floor to play on.
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
    How many pickups are buried, and exactly what they are.

    A fixed list handed out in order rather than a weighted roll, so a board has exactly one crystal
    rather than a 12% chance of two, and "how good was that board" is a question about where things
    were. It is walked front to back onto SHUFFLED cells - the mix is exact, the placement is not -
    and it degrades the right way: a board with only three soft blocks left keeps the coins and the
    health and loses the crystal, because the rare thing is at the back.
*/
#define MAZE_NUM_ITEMS          9
static const uint8_t MAZE_ITEM_ORDER[MAZE_NUM_ITEMS] = {
    //First, and that is the whole reason the list is walked in order: a board too small to bury
    //everything must still bury the way out.
    MAZE_ITEM_KEY,
    MAZE_ITEM_COIN,    MAZE_ITEM_HEALTH,
    MAZE_ITEM_COIN,    MAZE_ITEM_DIAMOND,
    MAZE_ITEM_COIN,    MAZE_ITEM_SHIELD,
    MAZE_ITEM_COIN,    MAZE_ITEM_CRYSTAL,
};

//How far from the spawn the exit has to be, in manhattan distance, so finding the key is a journey
//rather than a detour. A PREFERENCE, not a requirement: PlaceDoor takes the best it can get, since
//a door in an awkward place is still finishable and no door at all is not.
#define MAZE_DOOR_MIN_DIST      12

//Percentage of empty water cells that get a lilly pad. A third, so a pond reads as a pond rather
//than a lilly farm, and two ponds do not look like copies of each other.
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

/*
    What finishing a board QUICKLY is worth.

    `level_ticks` runs from the moment a board is laid out until the player steps into the exit, and
    every MAZE_TIME_BONUS_TICKS_PER_POINT it comes in under par is one point. Par is 6000 ticks - a
    hundred seconds at the 60 Hz this app ticks at - so a perfect run is worth 200, which is between
    a diamond (50) and a crystal (250). That is the scale it is meant to sit on: worth hurrying for,
    never worth more than actually playing the board.

    TICKS, NOT SECONDS, all the way through - the rules have no idea what rate they are ticked at,
    and a bonus expressed in seconds would silently change value if SetPhysicsTPS ever did.

    A DEATH COSTS TIME AND NOTHING ELSE. `level_ticks` keeps running through the corpse and the
    respawn, which is the whole reason dying is worth avoiding on a board you have already solved.
*/
#define MAZE_TIME_PAR_TICKS             6000
#define MAZE_TIME_BONUS_TICKS_PER_POINT 30

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
//How long a dead walker stays on the board before it stops being drawn. Long enough for the
//Enemy_Death clip (0.83s, so 50 ticks at 60 TPS) to play out and be seen, plus a moment holding
//the last frame - a body that vanished the instant the flame touched it read as never having
//been there.
#define MAZE_DEATH_TICKS        75

//What the player is asking for this tick. A struct rather than two arguments because the next thing
//to arrive here is a second player, and then this is what gets duplicated rather than the signature.
struct MazeInput{
    int  direction = MAZE_DIR_NONE;   //held, not an edge: a walker keeps walking
    bool f_place_bomb = false;        //an EDGE: one press is one bomb
};

/*
    Something that occupies a tile and moves between tiles. The player is one; so is each enemy.

    One struct rather than two sets of fields, because they differ in what decides their direction
    and in nothing else: they share the step, the facing, the interpolation and every rule about
    which cells may be entered. Any rule written against one and not the other would be a bug.

    Movement is TILE TO TILE, not free. `tile_x/tile_z` is where it is, or - mid-step - where it is
    GOING; `from_x/from_z` is where that step started and `step_ticks` counts down to arrival. So
    "which tile is this on" is always a pair of integers, and every rule that matters - can I go
    there, where does my bomb land, did the flame catch me - is a lookup rather than an overlap
    test. The smooth position the view draws is derived from them.
*/
struct MazeWalker{
    int tile_x = 0;
    int tile_z = 0;
    int from_x = 0;
    int from_z = 0;
    int step_ticks = 0;             //0 = standing on tile_x/tile_z
    //How long the step in progress was given, so X()/Z() can interpolate a fraction of it. Carried
    //per walker rather than read from a define, because the player and an enemy move at different
    //speeds - a walker that knows its own step length can be handed any speed at all.
    int step_total = MAZE_STEP_TICKS;
    int facing = MAZE_DIR_SOUTH;
    bool f_alive = false;
    /*
        Ticks left of being dead, counting DOWN. One counter, and whoever does the killing says how
        long it runs: f_alive false with death_ticks > 0 is gone as far as every rule is concerned -
        cannot be hit, blocks nothing, chops nothing - and still on the board as far as the view is
        concerned, which is what gives a death animation somewhere to play.

        TickEnemies sets MAZE_DEATH_TICKS and TickPlayerCondition sets MAZE_RESPAWN_TICKS; at zero
        an enemy stops being drawn and the player gets up on the spawn. There used to be a second
        counter on the Maze for the respawn, and the player's body vanished on the frame it died
        while the wait ran invisibly.
    */
    int death_ticks = 0;
    //How much more this body can take. On the walker because it is a fact about a BODY, which is
    //what makes it survive a level boundary for nothing. Defaults to one - a single hit kills -
    //and the player is set to MAZE_START_HEALTH. Enemies still die outright in TickEnemies, so it
    //is unused for them today; it is here so a tougher enemy is `health = 2` and not a mechanism.
    int health = 1;
    int shield_ticks = 0;       //MAZE_SHIELD_TICKS of taking no damage at all
    int invuln_ticks = 0;       //the mercy window after a hit, so one flame is one hit
    //Points, belonging to whoever is carrying them: TickItems already has the walker in hand when
    //it grants one, and a second player gets a second score without a design. Survives a death,
    //not a new board.
    uint32_t score = 0;
    /*
        Is this walker carrying the exit key? On the walker and not on the game, and that is a rule
        rather than tidiness: while it was a Maze member, IsPassable answered the door for whoever
        asked - and TickEnemies asks, through CanEnter - so taking the key opened the exit for the
        enemies too and 35 of 60 boards ended with one standing in it. A key is something a BODY
        carries, so the door asks the body.

        It survives death. Dying already costs a life and the walk back; also costing the key would
        mean re-digging a block whose location you have no way of remembering.
    */
    bool f_has_key = false;
    //Ticks left of the swing this walker is in the middle of. Enemies only - the player has no
    //melee - but it lives here because it is a reason to be STANDING STILL, which is a property of
    //a walker: nonzero means no step is begun and no direction chosen until it runs out.
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

    //Bumped every time a cell's CONTENTS change - a block destroyed, an item taken. The view's
    //reason for existing: ApplicationBomber builds one object per cell once and then only looks at
    //the board again when this moves, instead of rebuilding the field on the physics thread or
    //comparing 256 cells every tick. NewGame bumps it, so a view that has never looked is stale.
    uint32_t field_version = 0;

    //--- the walkers --------------------------------------------------------------------------
    MazeWalker player;
    //Fixed array rather than a vector: the count is small and known, this header is meant to stay
    //free of anything that allocates, and a dead enemy is a walker with f_alive false rather than
    //a hole in a list.
    MazeWalker enemy[MAZE_MAX_ENEMIES];
    int num_enemies = 0;

    //--- what the BOARD counts, and no more -----------------------------------------------------
    //Statistics, and nothing that belongs to a body - health, the shield and the score live on
    //MazeWalker, which is what makes them survive a level boundary. What a new level does to each
    //of these has not been decided yet; keeping them here is what stops it being decided by
    //accident. `deaths` and `items_taken` are the player's, the rest are the board's.
    int  deaths = 0;
    int  items_taken = 0;
    //Where the exit is. The door is a CELL, so it is in `tile` like everything else; these two are
    //only so nothing has to search the border. WHO may walk through is MazeWalker::f_has_key,
    //asked by CanEnter, so that an enemy never can.
    int  door_x = 0;
    int  door_z = 0;
    /*
        Where the way IN is, and where the player starts because of it.

        `entry_dir` is the outward direction of the border `entry_x/entry_z` sits in; the spawn is
        the cell one step inward from it. All three are derived by SetEntry from NewGame's argument
        and are then fixed for the life of the board.

        THE ENTRY CELL IS STILL BORDER WALL as far as the rules are concerned. Nobody walks through
        it: the corridor hands the player over already standing on the spawn. It is recorded because
        the VIEW has to leave the brick off that one cell and stand the archway there, and because
        PlaceDoor must not put the exit in it.
    */
    /*
        Ticks this board has been played for. Reset by NewGame, run by Tick, and NOT run while the
        player is in the corridor - because Maze is not ticked there, which is exactly right: the
        walk between levels is not part of either level's time.

        A rule, not telemetry: TimeBonus is paid out of it.
    */
    uint32_t level_ticks = 0;
    int  entry_dir = MAZE_DIR_WEST;
    int  entry_x = 0;
    int  entry_z = MAZE_ENTRY_OFFSET;
    int  spawn_x = MAZE_ENTRY_OFFSET;
    int  spawn_z = MAZE_ENTRY_OFFSET;
    //Points. Treasure only - health and shields pay in other currencies and are worth 0 here.
    //Survives a death, because dying already costs a life and the respawn. Does NOT survive
    //NewGame: a seed lays out a board and a board is a round.
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
    //Soft blocks removed, counted separately by WHO removed them. With one counter, a bomb taking
    //one block while the board lost four was invisible - the other three were enemies cutting
    //hedges, which is the only number that says they do anything when you are not watching.
    uint32_t blocks_destroyed = 0;      //by a blast
    uint32_t blocks_cut = 0;            //by an enemy's shears
    uint32_t enemies_killed = 0;

    //How many cells the walker can actually get to, counted at generation time OVER THE HARD WALLS
    //ONLY - soft blocks are laid afterwards, and a pocket behind one is somewhere you can get to
    //once you have dug. The one number that says whether a seed produced a playable field or a
    //pretty one with most of itself walled off.
    int reachable_cells = 0;

    //--- the game -----------------------------------------------------------------------------
    /*
        Lays out a fresh field and puts the character on its spawn. Draws from `seed` and nothing
        else, so the same seed is the same field.

        `carry` is the walker as it arrived from the level before, or NULL for a fresh run. This is
        the third of the three reset policies - the other two being the blunt one a few lines into
        the definition and the respawn's - and it is a LEVEL boundary: you keep what you earned and
        you lose what belonged to the board.
    */
    /*
        `entry_dir` is the OUTWARD direction of the border the player walks in through, and it
        decides where the spawn is - see MAZE_ENTRY_OFFSET and SetEntry. The default is the west
        border, which is the corner every board used before the entry could move.

        IT IS THE OPPOSITE OF THE WAY THEY ARE WALKING. A corridor running north arrives at this
        board's SOUTH border, so the caller passes MAZE_DIR_SOUTH. ApplicationBomber does exactly
        that from the direction its corridor runs, which is how the whole transition became a
        straight line with nothing to rotate.
    */
    void NewGame(uint32_t seed, const MazeWalker* carry = NULL, int entry_dir_in = MAZE_DIR_WEST);

    //One tick of everything: the walkers, the fuse, the blast clock, and who got hurt. The only
    //entry point that changes anything.
    void Tick(const MazeInput& input);

    /*
        What this board's time is worth, in points. See MAZE_TIME_PAR_TICKS.

        A QUERY AND NOT A PAYMENT - it does not touch the score, and calling it twice gives the same
        answer. Whoever decides the level is over is the one that adds it, which is ApplicationBomber
        at the moment the player steps into the exit. Keeping it that way means the rule can be
        tested without running a level boundary.
    */
    uint32_t TimeBonus() const;

    //--- queries ------------------------------------------------------------------------------
    bool InBounds(int x, int z) const {
        return x >= 0 && x < MAZE_W && z >= 0 && z < MAZE_H;
    }
    //Could anything stand here at all, ignoring which way it arrived? What the GENERATOR asks, and
    //what a reachability count is over. Movement asks CanEnter instead.
    bool IsPassable(int x, int z) const;
    //May a walker step from one cell to an ADJACENT one? This and not IsPassable is what the walker
    //asks, because a cell can be crossable one way and not the other - see MazePassAxis. Both ends
    //are checked: leaving a bridge sideways is as wrong as entering one sideways.
    bool CanEnter(const MazeWalker& walker, int from_x, int from_z, int to_x, int to_z) const;
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
    //One attempt at a terrain layout: zones, border, doorways, water, spawn, prune. Split out of
    //NewGame so it can be RETRIED - see MAZE_MIN_PLAYABLE - and returns how many cells the walker
    //can reach, which is what decides whether the attempt was any good. Everything that needs the
    //terrain settled (soft blocks, items, enemies, decor) stays in NewGame and runs once.
    int LayOutTerrain();
    //Derives entry_x/entry_z and spawn_x/spawn_z from `entry_dir`. Called first thing in NewGame,
    //because the terrain layout, the reachability flood and every distance rule below want the
    //spawn before they run.
    void SetEntry(int dir);
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
    //Puts the exit in the border wall, on a cell that can be reached from inside. AFTER the terrain
    //has settled and BEFORE the soft blocks, so nothing is laid on top of it. A soft block CAN land
    //on the cell in front of it, which is fine: that is a bomb, not a lock.
    void PlaceDoor();
    //Is there anywhere next to this cell a walker could stand? What makes a buried thing DIGGABLE:
    //a blast reaches INTO a soft block from the cell beside it, so one open neighbour is enough.
    bool HasOpenNeighbour(int x, int z) const;
    //Scatters flowers, plants and worse on dry open ground.
    void AddDecor();
    //Flood fill from the spawn and turn anything the walker cannot get to into wall. Not tidiness:
    //a doorway roll can leave a zone sealed, and a sealed pocket of grass is worse than a wall,
    //because it looks like somewhere you are supposed to be able to reach. Water is left alone.
    //Returns the number of reachable cells.
    int PruneUnreachable();

    //The field's own random stream, seeded by NewGame. Deliberately NOT Application::rrand: that
    //one is shared with the renderer and the debug UI, and off-tick draws from those shift it out
    //from under the simulation (open backlog item). xorshift32 because it is four lines and the
    //quality needed here is "scatters rocks convincingly".
    uint32_t rng_state = 1;
    uint32_t NextRandom();
    int RandomBelow(int n);
};

#endif
