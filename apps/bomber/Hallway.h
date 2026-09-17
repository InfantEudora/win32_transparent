#ifndef _HALLWAY_H_
#define _HALLWAY_H_

#include "Maze.h"

/*
    The corridor between two levels.

    Not a Maze: no bombs, no blast, no enemies, no items, no water, no zones, no soft blocks, and
    nothing can follow you in or hurt you here. Folding it into Maze would mean a mode flag with
    most of that class dead on one side of it. What the two share is the walker, and that is shared
    directly - MazeWalker is a standalone struct and Maze::DirX/DirZ/DirOpposite are static.

    The two are NEVER simulated at the same time, which is what makes all of this cheap: the app
    holds a phase and one of them ticks. They overlap on SCREEN only while the near door is open.

    The shape, in local space - +z runs away from the board you came out of, +x is across:

        z = 0             the near doorway - the same world cell as the board's exit door
        z = 1..length-2   the corridor, HALL_W wide
        z = length-1      the far doorway - the way onto the next board

    There is no tile array: the shape is a function of `length`, so IsPassable computes it rather
    than a second place existing for the same fact.

    `f_sealed` is the COMMIT POINT the app waits for. Before it, the old board is still visible
    through the open doorway and must not be touched; after it, the corridor is a closed box with a
    door at each end, so the board can be thrown away, the next one generated, and the corridor
    moved and turned to meet it without anybody being able to tell.
*/

//Across. Three so the corridor is a space rather than a pipe, and so the doorways at either end
//read as doorways rather than as the corridor simply continuing.
#define HALL_W                  3
//How long a corridor can be, in cells, INCLUDING the doorway at each end - so the walk between the
//doors is two less. Long enough that the board behind you is gone before the far door opens, short
//enough that it is a breath rather than a journey, and the top of the range is what a score tally
//wants to fit in.
#define HALL_MIN_LEN            4
#define HALL_MAX_LEN            8
//The same walk as on the board. A corridor that walked at a different speed would read as a
//different game rather than as the same character in a different room.
#define HALL_STEP_TICKS         MAZE_STEP_TICKS

//The four directions in the order they go ROUND. The MAZE_DIR_* numbering deliberately is not -
//those are grouped by axis so DirOpposite is a flip of one bit - and turning a corridor to meet
//the next board is the only thing in the project that needs the cyclic order.
static const int HALL_DIR_CYCLE[MAZE_NUM_DIRS] = {
    MAZE_DIR_EAST,      //+X
    MAZE_DIR_SOUTH,     //+Z
    MAZE_DIR_WEST,      //-X
    MAZE_DIR_NORTH      //-Z
};

struct Hallway{
    //Lays out a corridor and puts the walker in its near doorway. `carry` is the walker as it left
    //the board - health, score, the key and all - and it arrives unchanged apart from where it is
    //standing: NOTHING IS RESET HERE, because what a level boundary costs is decided when the next
    //board is laid out (Maze::NewGame takes the walker), so this is a place the player passes
    //through rather than one things happen to them. `forward` is the world direction the corridor
    //runs in, which is the way they were walking when they stepped into the exit.
    void Begin(uint32_t seed, const MazeWalker& carry, int forward);
    //One tick. `input.f_place_bomb` is IGNORED - there are no bombs in here, and that is a rule.
    void Tick(const MazeInput& input);

    //--- the shape ------------------------------------------------------------------------------
    bool InBounds(int x, int z) const {
        return x >= 0 && x < HALL_W && z >= 0 && z < length;
    }
    //Floor, computed from `length` rather than stored - see the note at the top.
    bool IsPassable(int x, int z) const;
    //A step, which is IsPassable plus the two doors. No walker argument: nothing is asked of the
    //body in here, unlike Maze::CanEnter and its key.
    bool CanEnter(int from_x, int from_z, int to_x, int to_z) const;
    void StepWalker(MazeWalker& walker, int dir, int step_ticks);

    //--- turning --------------------------------------------------------------------------------
    /*
        TWO FRAMES, and they come apart the moment the corridor is turned. LocalToWorld is GEOMETRY
        - where a cell is and which way a model faces - and follows `forward`. InputToLocal is the
        PLAYER'S HANDS and follows `control_forward`, which never changes.

        They were one function to begin with, and that was a bug you could walk into: the corridor
        is rotated at the commit and the camera with it, so on screen nothing happens - but the keys
        are world directions, so a quarter turn meant the key that had been walking them forward now
        walked them into a wall.

        Local +z is MAZE_DIR_SOUTH by convention, so DirX/DirZ work on a local direction with no
        second table.
    */
    int InputToLocal(int world_dir) const;
    int LocalToWorld(int local_dir) const;

    //--- state ----------------------------------------------------------------------------------
    int length = HALL_MIN_LEN;
    //World direction of local +z. Set by Begin, and changed by the app when the corridor is turned
    //to meet the next board - which is safe only while f_sealed, when nobody can see it happen.
    int forward = MAZE_DIR_SOUTH;
    //The frame the player's HANDS are in: the direction the corridor ran when they walked into it,
    //and it stays that way. Turning the corridor turns `forward` and leaves this alone, which is
    //the only way a turn can be genuinely invisible rather than merely unseen.
    int control_forward = MAZE_DIR_SOUTH;
    MazeWalker player;
    //The near door starts OPEN because the player has just walked through it, and shuts the moment
    //they are past it. `f_sealed` is that same event reported once - see the commit point above.
    bool f_near_door_open = true;
    bool f_sealed = false;
    //Set by the APP when the next board is laid out and standing there. The far door will not open
    //without it, and that is a real constraint rather than belt and braces: the old board takes a
    //moment to sink out of sight and the near door a moment to shut, and on a SHORT corridor the
    //player reaches the far end before either has finished and would stand waiting at a door that
    //could not let them through yet.
    bool f_next_ready = false;
    //Opens when the player is standing in front of it, and stays open.
    bool f_far_door_open = false;
    //The player has stepped INTO the far doorway. The app takes it from here.
    bool f_finished = false;
    //Bumped whenever something the view would have to redraw changes - a new corridor, a door
    //moving. The same trick as Maze::field_version, for the same reason.
    uint32_t version = 0;
};

#endif
