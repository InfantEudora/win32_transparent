#include "Hallway.h"

/*
    The rules of the corridor between two levels. See Hallway.h for the shape and for why this is
    not a Maze.

    Everything here is integers and ticks, like Maze, and for the same reason: `make rules` builds
    both against nothing at all and checks them in about a second.
*/

//--- the shape --------------------------------------------------------------------------------------

bool Hallway::IsPassable(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    //Both ends are a doorway one cell wide. Computed rather than stored - see the note at the top
    //of the header on why there is no tile array.
    if (z == 0 || z == length - 1){
        return x == 1;
    }
    return true;
}

bool Hallway::CanEnter(int from_x, int from_z, int to_x, int to_z) const {
    /*
        No `from` test at all, unlike Maze::CanEnter.

        That one has to check the cell being LEFT as well, because a bridge you could step off
        sideways is not a bridge. Nothing in here has a grain, so the question is only about where
        you are going. The arguments are kept so the two read the same way.
    */
    (void)from_x;
    (void)from_z;
    if (!IsPassable(to_x,to_z)){
        return false;
    }
    //The doors. The near one shuts behind you and does not open again - there is nothing to go back
    //to by then - and the far one is shut until you are standing in front of it.
    if (to_z == 0 && !f_near_door_open){
        return false;
    }
    if (to_z == length - 1 && !f_far_door_open){
        return false;
    }
    return true;
}

void Hallway::StepWalker(MazeWalker& walker, int dir, int step_ticks){
    if (dir < 0 || dir >= MAZE_NUM_DIRS){
        return;
    }
    /*
        Facing is set before the step is tested, exactly as Maze::StepWalker does it: walking into a
        wall should still turn you to face it. `dir` is LOCAL here, so the view has to put it back
        through LocalToWorld before it can point a model with it.
    */
    walker.facing = dir;
    if (walker.step_ticks > 0){
        return;
    }
    int nx = walker.tile_x + Maze::DirX(dir);
    int nz = walker.tile_z + Maze::DirZ(dir);
    if (!CanEnter(walker.tile_x,walker.tile_z,nx,nz)){
        return;
    }
    walker.from_x = walker.tile_x;
    walker.from_z = walker.tile_z;
    walker.tile_x = nx;
    walker.tile_z = nz;
    walker.step_ticks = step_ticks;
    walker.step_total = step_ticks;
}

//--- turning ----------------------------------------------------------------------------------------

/*
    Where a direction sits in the cyclic order. -1 for anything that is not a direction, which the
    two callers below turn into "no direction" rather than into an index of -1.
*/
static int CycleIndex(int dir){
    for (int i = 0; i < MAZE_NUM_DIRS; i++){
        if (HALL_DIR_CYCLE[i] == dir){
            return i;
        }
    }
    return -1;
}

/*
    How far the corridor is turned, in quarter turns.

    Local +z is MAZE_DIR_SOUTH by convention (which is what lets DirX/DirZ be used on a local
    direction), and SOUTH is index 1 in the cycle - so a corridor whose `forward` is at index f is
    turned by f - 1.
*/
static int TurnOf(int forward){
    int f = CycleIndex(forward);
    return (f < 0) ? 0 : (f - 1);
}

int Hallway::LocalToWorld(int local_dir) const {
    int i = CycleIndex(local_dir);
    if (i < 0){
        return MAZE_DIR_NONE;
    }
    return HALL_DIR_CYCLE[((i + TurnOf(forward)) % MAZE_NUM_DIRS + MAZE_NUM_DIRS) % MAZE_NUM_DIRS];
}

/*
    A key press, in the corridor's cells.

    Through `control_forward` and NOT `forward`, so that turning the corridor does not turn the
    controls - see the note at the declaration. This is deliberately not the inverse of LocalToWorld
    once the two frames have come apart.
*/
int Hallway::InputToLocal(int world_dir) const {
    int i = CycleIndex(world_dir);
    if (i < 0){
        return MAZE_DIR_NONE;
    }
    return HALL_DIR_CYCLE[((i - TurnOf(control_forward)) % MAZE_NUM_DIRS + MAZE_NUM_DIRS)
                          % MAZE_NUM_DIRS];
}

//--- the corridor itself ----------------------------------------------------------------------------

void Hallway::Begin(uint32_t seed, const MazeWalker& carry, int forward_dir){
    /*
        Length straight out of the seed rather than from a random stream.

        Two reasons, and the second is the one that matters: it is one line, and Application::rrand
        is a single stream shared with the UI and MCP, so an off-tick draw from it would shift the
        simulation's sequence out from under itself (see RRandom.h). Maze carries its own generator
        for exactly that reason; a corridor needs one number and can do without one entirely.
    */
    length = HALL_MIN_LEN + (int)(seed % (uint32_t)(HALL_MAX_LEN - HALL_MIN_LEN + 1));
    forward = (forward_dir >= 0 && forward_dir < MAZE_NUM_DIRS) ? forward_dir : MAZE_DIR_SOUTH;
    //The two frames start together and only the geometry one ever moves.
    control_forward = forward;

    /*
        THE WALKER ARRIVES UNCHANGED apart from where it is standing.

        Health, score and the key all come through, because the corridor is somewhere the player
        passes through rather than something that happens to them. What a level boundary costs is
        decided one screen later, when Maze::NewGame lays out the next board and is handed this same
        walker - one place, one policy, and it is not this one.
    */
    player = carry;
    player.tile_x = player.from_x = 1;
    player.tile_z = player.from_z = 0;
    player.step_ticks = 0;
    player.step_total = HALL_STEP_TICKS;
    //LOCAL forward: the player is facing up the corridor, whichever way that points in the world.
    player.facing = MAZE_DIR_SOUTH;
    player.f_alive = true;
    player.death_ticks = 0;
    player.chop_ticks = 0;
    //Nothing in here can hurt anybody, so a mercy window carried in from a hit on the last board
    //would only be spent standing still. Cleared so it is not silently burnt.
    player.invuln_ticks = 0;

    f_near_door_open = true;
    f_sealed = false;
    f_far_door_open = false;
    f_finished = false;
    version++;
}

void Hallway::Tick(const MazeInput& input){
    if (f_finished){
        return;
    }

    if (player.step_ticks > 0){
        player.step_ticks--;
    }
    /*
        Checked AFTER the decrement so a held direction rolls into the next step, which is the same
        rule Maze::Tick keeps and for the same reason - without it a walk is visibly stuttery and
        reads as a frame-rate problem.

        `input.f_place_bomb` is not looked at anywhere in this function. THERE ARE NO BOMBS IN HERE,
        and that is a rule rather than an omission: the corridor is the one place in the game where
        nothing can hurt you.
    */
    if (player.step_ticks <= 0 && input.direction != MAZE_DIR_NONE){
        StepWalker(player,InputToLocal(input.direction),HALL_STEP_TICKS);
    }

    /*
        The near door shuts the moment the player is off the threshold, and THAT TICK IS THE COMMIT
        POINT - see the note at the top of the header. Until it happens the old board is still
        visible through the open doorway; after it, the corridor is a closed box.
    */
    if (f_near_door_open && player.tile_z >= 1){
        f_near_door_open = false;
        f_sealed = true;
        version++;
    }

    /*
        The far door opens when the player is standing in front of it, and only once the corridor is
        sealed - so the next board is always laid out before there is any way to reach it. With any
        legal `length` the seal comes first anyway; the test is here to say that it must.
    */
    if (!f_far_door_open && f_sealed && player.tile_z >= length - 2){
        f_far_door_open = true;
        version++;
    }

    //Into the far doorway, which is as far as this class goes. The app takes the walker from here.
    if (player.tile_z >= length - 1){
        f_finished = true;
    }
}
