#ifndef _HALLWAY_H_
#define _HALLWAY_H_

#include "Maze.h"

/*
    The corridor between two levels.

    --- WHY THIS IS NOT A Maze ---------------------------------------------------------------------
    It shares almost nothing with one. No bombs, no blast, no enemies, no items, no water, no zones,
    no soft blocks, no reachability - and nothing can follow you in or hurt you here, which is the
    whole point of it. Folding it into Maze would mean a mode flag with most of that class dead on
    one side of it, and every future change to a bomberman rule would have to ask whether it makes
    sense in a corridor.

    What the two DO share is the walker, and that is shared directly: `MazeWalker` is a standalone
    struct and `Maze::DirX`/`DirZ`/`DirOpposite` are static, so this file reuses both rather than
    copying them.

    --- THE TWO ARE NEVER SIMULATED AT THE SAME TIME ------------------------------------------------
    Which is the fact that makes all of this cheap. The app holds a phase; one of the two ticks. They
    overlap on SCREEN for as long as the near door is still open, and not a moment longer.

    --- THE SHAPE ----------------------------------------------------------------------------------
    HALL_W wide and `length` long, in its own local space: +z runs away from the board you came out
    of, +x is across. Both ENDS are doorways one cell wide, in the middle of the width:

        z = 0             the near doorway - the same world cell as the board's exit door
        z = 1..length-2   the corridor, HALL_W wide
        z = length-1      the far doorway - the way onto the next board

    There is no tile array. The shape is a function of `length`, so IsPassable computes it; an array
    would be a second place for the same fact to be written down.

    --- THE COMMIT POINT ---------------------------------------------------------------------------
    `f_sealed` goes true the tick the near door shuts, and that is the ONE moment the app is waiting
    for: before it, the old board is still visible through the open doorway and must not be touched;
    after it, the corridor is a closed box with a door at each end and the old board can be thrown
    away, the next one generated, and the whole corridor moved and turned to meet it without anybody
    being able to tell. See ApplicationBomber's transition.
*/

//Across. Three so the corridor is a space rather than a pipe, and so the doorways at either end
//read as doorways rather than as the corridor simply continuing.
#define HALL_W                  3
/*
    How long a corridor can be, in cells, INCLUDING the doorway at each end.

    So the walk between the doors is HALL_MIN_LEN-2 to HALL_MAX_LEN-2 cells. Long enough that the
    board behind you is gone before the far door opens, short enough that it is a breath rather than
    a journey - and the top of the range is what a score tally wants to fit in.
*/
#define HALL_MIN_LEN            4
#define HALL_MAX_LEN            8
//The same walk as on the board. A corridor that walked at a different speed would read as a
//different game rather than as the same character in a different room.
#define HALL_STEP_TICKS         MAZE_STEP_TICKS

/*
    The four directions in the order they go ROUND, which the MazeDir numbering deliberately is not -
    those are grouped by axis (+X, -X, -Z, +Z) so that DirOpposite is a flip of one bit.

    A corridor has to be turned to meet the next board, so it needs the cyclic order as well. This is
    that, and it is the only place in the project that needs it.
*/
static const int HALL_DIR_CYCLE[MAZE_NUM_DIRS] = {
    MAZE_DIR_EAST,      //+X
    MAZE_DIR_SOUTH,     //+Z
    MAZE_DIR_WEST,      //-X
    MAZE_DIR_NORTH      //-Z
};

struct Hallway{
    /*
        Lays out a corridor and puts the walker in its near doorway.

        `carry` is the walker as it left the board - health, score, the key and all - and it arrives
        here unchanged apart from being stood at the entrance. NOTHING IS RESET HERE: what a level
        boundary costs is decided when the NEXT board is laid out (Maze::NewGame takes the walker),
        so the corridor is a place the player passes through rather than a place things happen to
        them.

        `forward` is the world direction the corridor runs in, which is the direction the player was
        walking when they stepped into the exit.
    */
    void Begin(uint32_t seed, const MazeWalker& carry, int forward);
    //One tick. `input.f_place_bomb` is IGNORED - there are no bombs in here, and that is a rule.
    void Tick(const MazeInput& input);

    //--- the shape ------------------------------------------------------------------------------
    bool InBounds(int x, int z) const {
        return x >= 0 && x < HALL_W && z >= 0 && z < length;
    }
    //Floor, computed from `length` rather than stored - see the note at the top.
    bool IsPassable(int x, int z) const;
    //A step, which is IsPassable plus the two doors. No walker argument: nothing in here is carried
    //and nothing is asked of the body, unlike Maze::CanEnter and its key.
    bool CanEnter(int from_x, int from_z, int to_x, int to_z) const;
    void StepWalker(MazeWalker& walker, int dir, int step_ticks);

    //--- turning --------------------------------------------------------------------------------
    /*
        TWO FRAMES, and they come apart the moment the corridor is turned.

        `LocalToWorld` is GEOMETRY: where a cell is and which way the model faces, in the frame the
        corridor is currently built in. It follows `forward`.

        `InputToLocal` is the PLAYER'S HANDS, and it follows `control_forward`, which never changes.
        They were one function to begin with and that was a bug you could walk into: the corridor is
        rotated at the commit and the camera is rotated with it, so on screen nothing happens and the
        player is still walking away from the camera - but the keys are world directions, so a
        quarter turn meant the key that had been walking them forward now walked them into a wall.

        Local +z is MAZE_DIR_SOUTH by convention, so that DirX/DirZ can be used on a local direction
        with no second table.
    */
    int InputToLocal(int world_dir) const;
    int LocalToWorld(int local_dir) const;

    //--- state ----------------------------------------------------------------------------------
    int length = HALL_MIN_LEN;
    //World direction of local +z. Set by Begin, and changed by the app when the corridor is turned
    //to meet the next board - which is safe only while f_sealed, when nobody can see it happen.
    int forward = MAZE_DIR_SOUTH;
    /*
        The direction the corridor ran when the player walked into it, and it stays that way.

        This is the frame their HANDS are in. Turning the corridor turns `forward` and leaves this
        alone, so the key that was walking them up it still does - which is the only way a turn can
        be genuinely invisible rather than merely unseen.
    */
    int control_forward = MAZE_DIR_SOUTH;
    MazeWalker player;
    /*
        The near door starts OPEN because the player has just walked through it, and shuts the
        moment they are past it. `f_sealed` is the same event reported once - see the note at the
        top on why the app is waiting for exactly that tick.
    */
    bool f_near_door_open = true;
    bool f_sealed = false;
    //Opens when the player is standing in front of it, and stays open.
    bool f_far_door_open = false;
    //The player has stepped INTO the far doorway. The app takes it from here.
    bool f_finished = false;
    /*
        Bumped whenever something the view would have to redraw changes - a new corridor, a door
        moving. The same trick as Maze::field_version and for the same reason.
    */
    uint32_t version = 0;
};

#endif
