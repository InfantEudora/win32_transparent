#include "Maze.h"

//--- the field's own random stream ---------------------------------------------------------------

uint32_t Maze::NextRandom(){
    //xorshift32. Never let the state reach zero, which is the one input it cannot recover from.
    uint32_t x = rng_state ? rng_state : 2463534242u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

int Maze::RandomBelow(int n){
    if (n <= 1){
        return 0;
    }
    //Modulo, with the bias accepted: the stream is 32 bits and n is never more than a few dozen, so
    //the skew is one part in a hundred million and nothing here is a lottery.
    return (int)(NextRandom() % (uint32_t)n);
}

int Maze::DirX(int dir){
    switch (dir){
        case MAZE_DIR_EAST:  return  1;
        case MAZE_DIR_WEST:  return -1;
        default:             return  0;
    }
}

int Maze::DirZ(int dir){
    switch (dir){
        case MAZE_DIR_NORTH: return -1;
        case MAZE_DIR_SOUTH: return  1;
        default:             return  0;
    }
}

uint8_t Maze::DirAxis(int dir){
    return (dir == MAZE_DIR_EAST || dir == MAZE_DIR_WEST) ? MAZE_AXIS_X : MAZE_AXIS_Z;
}

//--- generation -------------------------------------------------------------------------------------

/*
    The classic. Open floor with a pillar on every cell whose x and z are both even.

    That grid is what makes a bomberman board navigable rather than a random mess: the open cells
    form corridors one tile wide with no dead ends, guaranteed, without anything having to check.
*/
void Maze::FillBomber(int x0, int z0, int x1, int z1){
    for (int z = z0; z <= z1; z++){
        for (int x = x0; x <= x1; x++){
            bool f_pillar = ((x % 2) == 0 && (z % 2) == 0);
            tile[z][x] = f_pillar ? MAZE_TILE_WALL : (uint8_t)MAZE_TILE_GRASS;
            zone[z][x] = MAZE_STYLE_BOMBER;
        }
    }
}

/*
    A carved labyrinth: corridors one tile wide, with junctions and dead ends.

    Recursive backtracker (iterative, with an explicit stack) over the ODD cells only, knocking out
    the cell between each pair it joins. Working on the odd lattice is what makes this interlock with
    the bomber zones: their corridors are on odd coordinates too, so a doorway punched through the
    boundary at an odd coordinate always lands on a corridor on both sides rather than into the side
    of a wall.

    The stack is a fixed array rather than a std::vector because the maximum depth is the number of
    odd cells in the board, which is small and known, and this header is supposed to stay free of
    anything that allocates.
*/
void Maze::FillLabyrinth(int x0, int z0, int x1, int z1){
    for (int z = z0; z <= z1; z++){
        for (int x = x0; x <= x1; x++){
            tile[z][x] = MAZE_TILE_WALL;
            zone[z][x] = MAZE_STYLE_LABYRINTH;
        }
    }

    //First odd cell inside the rect. If the zone is too thin to hold one the carve is skipped and
    //the zone stays solid - which the reachability prune below will notice and nothing will break.
    int sx = (x0 % 2) ? x0 : x0 + 1;
    int sz = (z0 % 2) ? z0 : z0 + 1;
    if (sx > x1 || sz > z1){
        return;
    }

    //Two shorts packed per entry would be tidier; two arrays are clearer and the cost is nothing.
    int stack_x[MAZE_W * MAZE_H];
    int stack_z[MAZE_W * MAZE_H];
    int depth = 0;

    tile[sz][sx] = MAZE_TILE_GRASS;
    stack_x[depth] = sx;
    stack_z[depth] = sz;
    depth++;

    while (depth > 0){
        int cx = stack_x[depth - 1];
        int cz = stack_z[depth - 1];

        //Which two-cell steps from here land on an odd cell that is still solid.
        int cand[MAZE_NUM_DIRS];
        int num_cand = 0;
        for (int d = 0; d < MAZE_NUM_DIRS; d++){
            int nx = cx + DirX(d) * 2;
            int nz = cz + DirZ(d) * 2;
            if (nx < x0 || nx > x1 || nz < z0 || nz > z1){
                continue;
            }
            if (tile[nz][nx] != MAZE_TILE_WALL){
                continue;   //already carved
            }
            cand[num_cand++] = d;
        }

        if (num_cand == 0){
            depth--;        //dead end: back up
            continue;
        }

        int d = cand[RandomBelow(num_cand)];
        //Knock out the wall between, then the cell itself.
        tile[cz + DirZ(d)][cx + DirX(d)] = MAZE_TILE_GRASS;
        int nx = cx + DirX(d) * 2;
        int nz = cz + DirZ(d) * 2;
        tile[nz][nx] = MAZE_TILE_GRASS;
        stack_x[depth] = nx;
        stack_z[depth] = nz;
        depth++;
    }
}

/*
    A plaza: open ground with a few clumps of wall for cover.

    The clumps are short runs rather than single blocks, because one block in the middle of a room
    reads as a mistake and a line of three reads as a ruin. They are placed with a margin from the
    zone edge so they cannot seal a doorway from the inside.
*/
void Maze::FillOpen(int x0, int z0, int x1, int z1){
    for (int z = z0; z <= z1; z++){
        for (int x = x0; x <= x1; x++){
            tile[z][x] = MAZE_TILE_GRASS;
            zone[z][x] = MAZE_STYLE_OPEN;
        }
    }

    int area = (x1 - x0 + 1) * (z1 - z0 + 1);
    int clumps = 1 + area / 18;
    for (int i = 0; i < clumps; i++){
        int w = x1 - x0 - 1;
        int h = z1 - z0 - 1;
        if (w < 1 || h < 1){
            return;
        }
        int x = x0 + 1 + RandomBelow(w);
        int z = z0 + 1 + RandomBelow(h);
        int dir = RandomBelow(MAZE_NUM_DIRS);
        int run = 2 + RandomBelow(2);
        for (int step = 0; step < run; step++){
            if (x > x0 && x < x1 && z > z0 && z < z1){
                tile[z][x] = MAZE_TILE_WALL;
            }
            x += DirX(dir);
            z += DirZ(dir);
        }
    }
}

/*
    Holes through the two internal zone boundaries.

    Carved at ODD coordinates and three cells wide (the boundary column plus one either side) so
    that the opening lands on the odd corridor lattice both zones are built on. A doorway on an even
    coordinate can open into the side of a bomber pillar or a labyrinth wall and achieve nothing,
    which is exactly the failure that leaves a quarter of the board sealed off.

    Several per boundary, because one can still be swallowed by a labyrinth whose corridor happens
    to run parallel to the boundary just inside it.
*/
void Maze::CarveDoorways(int split_x, int split_z){
    //The vertical boundary: openings at odd z.
    for (int i = 0; i < 4; i++){
        int z = 1 + RandomBelow((MAZE_H - 2) / 2) * 2;      //odd, inside the border
        for (int x = split_x - 1; x <= split_x + 1; x++){
            if (x > 0 && x < MAZE_W - 1 && z > 0 && z < MAZE_H - 1){
                tile[z][x] = MAZE_TILE_GRASS;
            }
        }
    }
    //The horizontal boundary: openings at odd x.
    for (int i = 0; i < 4; i++){
        int x = 1 + RandomBelow((MAZE_W - 2) / 2) * 2;      //odd, inside the border
        for (int z = split_z - 1; z <= split_z + 1; z++){
            if (x > 0 && x < MAZE_W - 1 && z > 0 && z < MAZE_H - 1){
                tile[z][x] = MAZE_TILE_GRASS;
            }
        }
    }
}

/*
    Water, and bridges over it.

    THE RUN'S DIRECTION IS THE BRIDGE'S DIRECTION, which is the whole point of doing this in one
    place: a pond is grown as a straight run, so every bridge laid on it gets the run's axis and a
    row of them lines up into ONE bridge rather than three planks at three angles. The view reads
    that axis to turn the model, and the walker reads it to refuse a step across the rope rails.

    Bridges are laid on a CONTIGUOUS stretch rather than scattered, for the same reason: half a
    bridge across a three-tile pond is a crossing that does not cross.
*/
void Maze::AddWater(){
    for (int pond = 0; pond < 7; pond++){
        int x = 1 + RandomBelow(MAZE_W - 2);
        int z = 1 + RandomBelow(MAZE_H - 2);
        //Along an axis, not a diagonal: a pond that steps diagonally cannot be bridged by a line of
        //bridge pieces, and the axis is what a bridge needs.
        int dir = RandomBelow(MAZE_NUM_DIRS);
        int run = 2 + RandomBelow(3);

        //Lay the water first, remembering how far it actually got - it stops at a wall rather than
        //eating one, because a pond that ate a pillar would punch a hole in the lattice the zones
        //are built on.
        int laid = 0;
        int wx = x;
        int wz = z;
        for (int i = 0; i < run; i++){
            if (!InBounds(wx,wz) || tile[wz][wx] == MAZE_TILE_WALL){
                break;
            }
            tile[wz][wx] = MAZE_TILE_WATER;
            decor[wz][wx] = MAZE_DECOR_NONE;
            pass_axis[wz][wx] = MAZE_AXIS_ANY;
            laid++;
            wx += DirX(dir);
            wz += DirZ(dir);
        }

        //Bridge the whole of it, some of the time. All or nothing - see the note above.
        if (laid > 0 && RandomBelow(100) < 55){
            //ACROSS the run, not along it: you cross a stream, you do not walk down it. The bridge
            //planks therefore run perpendicular to the water's own direction.
            uint8_t axis = (DirAxis(dir) == MAZE_AXIS_X) ? MAZE_AXIS_Z : MAZE_AXIS_X;
            int bx = x;
            int bz = z;
            for (int i = 0; i < laid; i++){
                decor[bz][bx] = MAZE_DECOR_BRIDGE;
                pass_axis[bz][bx] = axis;
                bx += DirX(dir);
                bz += DirZ(dir);
            }
        }
    }
}

void Maze::AddDecor(){
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (tile[z][x] == MAZE_TILE_WALL || tile[z][x] == MAZE_TILE_WATER){
                continue;
            }
            if (decor[z][x] != MAZE_DECOR_NONE){
                continue;
            }
            int roll = RandomBelow(100);
            if (roll < 9){
                decor[z][x] = MAZE_DECOR_FLOWERS;
            }else if (roll < 15){
                decor[z][x] = MAZE_DECOR_PLANT;
            }else if (roll < 17){
                decor[z][x] = MAZE_DECOR_TURD;
            }
        }
    }
}

int Maze::PruneUnreachable(){
    bool seen[MAZE_H][MAZE_W];
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            seen[z][x] = false;
        }
    }

    //Breadth-first from the spawn over CanEnter, not IsPassable: a bridge you could only reach by
    //stepping onto it sideways is not reached, and counting it would overstate the field.
    int queue_x[MAZE_W * MAZE_H];
    int queue_z[MAZE_W * MAZE_H];
    int head = 0;
    int tail = 0;
    queue_x[tail] = MAZE_SPAWN_X;
    queue_z[tail] = MAZE_SPAWN_Z;
    tail++;
    seen[MAZE_SPAWN_Z][MAZE_SPAWN_X] = true;

    int count = 0;
    while (head < tail){
        int cx = queue_x[head];
        int cz = queue_z[head];
        head++;
        count++;
        for (int d = 0; d < MAZE_NUM_DIRS; d++){
            int nx = cx + DirX(d);
            int nz = cz + DirZ(d);
            if (!InBounds(nx,nz) || seen[nz][nx]){
                continue;
            }
            if (!CanEnter(cx,cz,nx,nz)){
                continue;
            }
            seen[nz][nx] = true;
            queue_x[tail] = nx;
            queue_z[tail] = nz;
            tail++;
        }
    }

    //Anything walkable that was not reached becomes wall. Water is left as it is: it was never
    //walkable, and a pond behind a wall reads as scenery rather than as a place you failed to get to.
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (seen[z][x]){
                continue;
            }
            if (tile[z][x] == MAZE_TILE_WALL || tile[z][x] == MAZE_TILE_WATER){
                continue;
            }
            tile[z][x] = MAZE_TILE_WALL;
            decor[z][x] = MAZE_DECOR_NONE;
            pass_axis[z][x] = MAZE_AXIS_ANY;
        }
    }
    return count;
}

/*
    Lays out a field.

    The board is split into four rectangles by one vertical and one horizontal cut, and each gets a
    style. THE FOUR ARE NOT INDEPENDENT ROLLS: three of them are dealt one of each style and only the
    fourth is free, so every field has all three and the blend is guaranteed rather than likely. Four
    independent rolls give an all-bomber board one time in twenty, which is exactly the outcome worth
    ruling out.

    The cuts are jittered rather than through the middle, so the quarters are not obviously quarters.

    THE SPAWN IS CLEARED LAST and unconditionally, before the reachability pass. Whatever the dice
    did, the character starts on a tile it can stand on with two neighbours it can walk to -
    otherwise a seed in a hundred puts it in a pond and the game looks broken rather than unlucky.
*/
void Maze::NewGame(uint32_t seed){
    rng_state = seed ? seed : 1;

    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            tile[z][x] = MAZE_TILE_WALL;
            decor[z][x] = MAZE_DECOR_NONE;
            pass_axis[z][x] = MAZE_AXIS_ANY;
            zone[z][x] = MAZE_STYLE_BOMBER;
        }
    }

    //Jittered cuts, kept far enough from the border that no zone is thinner than three cells.
    int split_x = 5 + RandomBelow(MAZE_W - 10);
    int split_z = 5 + RandomBelow(MAZE_H - 10);

    //One of each style plus a free one, then shuffled so which quarter gets which is not fixed.
    uint8_t styles[4] = {MAZE_STYLE_BOMBER,MAZE_STYLE_LABYRINTH,MAZE_STYLE_OPEN,
                         (uint8_t)RandomBelow(MAZE_STYLE_COUNT)};
    for (int i = 3; i > 0; i--){
        int j = RandomBelow(i + 1);
        uint8_t t = styles[i];
        styles[i] = styles[j];
        styles[j] = t;
    }

    //The four rectangles, in the order the styles were dealt.
    const int rect[4][4] = {
        {1,          1,          split_x - 1, split_z - 1},
        {split_x,    1,          MAZE_W - 2,  split_z - 1},
        {1,          split_z,    split_x - 1, MAZE_H - 2 },
        {split_x,    split_z,    MAZE_W - 2,  MAZE_H - 2 }
    };
    for (int i = 0; i < 4; i++){
        int x0 = rect[i][0], z0 = rect[i][1], x1 = rect[i][2], z1 = rect[i][3];
        if (x0 > x1 || z0 > z1){
            continue;
        }
        switch (styles[i]){
            case MAZE_STYLE_LABYRINTH: FillLabyrinth(x0,z0,x1,z1); break;
            case MAZE_STYLE_OPEN:      FillOpen(x0,z0,x1,z1);      break;
            default:                   FillBomber(x0,z0,x1,z1);    break;
        }
    }

    //The border, put back after the zones have had their way with the interior.
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            if (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1){
                tile[z][x] = MAZE_TILE_WALL;
                decor[z][x] = MAZE_DECOR_NONE;
            }
        }
    }

    CarveDoorways(split_x,split_z);
    AddWater();

    //The spawn and the two cells it can step to, before the reachability pass so that pass starts
    //somewhere real.
    const int spawn[3][2] = {
        {MAZE_SPAWN_X,  MAZE_SPAWN_Z},
        {MAZE_SPAWN_X+1,MAZE_SPAWN_Z},
        {MAZE_SPAWN_X,  MAZE_SPAWN_Z+1}
    };
    for (int i = 0; i < 3; i++){
        tile[spawn[i][1]][spawn[i][0]] = MAZE_TILE_GRASS;
        decor[spawn[i][1]][spawn[i][0]] = MAZE_DECOR_NONE;
        pass_axis[spawn[i][1]][spawn[i][0]] = MAZE_AXIS_ANY;
    }

    reachable_cells = PruneUnreachable();
    //Decoration last, so nothing is scattered on a cell that is about to become a wall.
    AddDecor();

    //Floor variety, purely cosmetic: grass mostly, with brick and rock mixed in so the board reads
    //as ground rather than as a colour. Done here, after every rule has settled, precisely because
    //it changes nothing - grass, brick and rock are the same tile as far as the walker is concerned.
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (tile[z][x] != MAZE_TILE_GRASS){
                continue;
            }
            int roll = RandomBelow(100);
            if (roll >= 62 && roll < 84){
                tile[z][x] = MAZE_TILE_BRICK;
            }else if (roll >= 84){
                tile[z][x] = MAZE_TILE_ROCK;
            }
        }
    }

    tile_x = from_x = MAZE_SPAWN_X;
    tile_z = from_z = MAZE_SPAWN_Z;
    step_ticks = 0;
    facing = MAZE_DIR_SOUTH;

    f_bomb = false;
    fuse_ticks = 0;
    f_blast = false;
    blast_ticks = 0;
    blast_count = 0;
    for (int d = 0; d < MAZE_NUM_DIRS; d++){
        arm[d] = 0;
    }
}

//--- queries -----------------------------------------------------------------------------------------

bool Maze::IsPassable(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    if (tile[z][x] == MAZE_TILE_WALL){
        return false;
    }
    if (tile[z][x] == MAZE_TILE_WATER){
        //The one thing that makes water crossable. See the note on MAZE_DECOR_BRIDGE.
        return decor[z][x] == MAZE_DECOR_BRIDGE;
    }
    return true;
}

bool Maze::CanEnter(int from_x, int from_z, int to_x, int to_z) const {
    if (!IsPassable(to_x,to_z)){
        return false;
    }
    //Which axis this step runs along. Derived from the cells rather than taking a direction, so a
    //caller cannot pass a direction that disagrees with the cells it named.
    uint8_t axis = (from_x != to_x) ? MAZE_AXIS_X : MAZE_AXIS_Z;

    //The target's grain, and then the cell being LEFT. Both, because a bridge you could step off
    //sideways is not a bridge - see the note on MazePassAxis.
    if (pass_axis[to_z][to_x] != MAZE_AXIS_ANY && pass_axis[to_z][to_x] != axis){
        return false;
    }
    if (InBounds(from_x,from_z) &&
        pass_axis[from_z][from_x] != MAZE_AXIS_ANY && pass_axis[from_z][from_x] != axis){
        return false;
    }
    return true;
}

bool Maze::BlocksBlast(int x, int z) const {
    //Out of bounds counts as blocking, so an arm cannot walk off the board. Water deliberately does
    //NOT block: it is at ground level and there is nothing there to stop a flame.
    if (!InBounds(x,z)){
        return true;
    }
    return tile[z][x] == MAZE_TILE_WALL;
}

bool Maze::IsBurning(int x, int z) const {
    if (!f_blast){
        return false;
    }
    if (x == blast_x && z == blast_z){
        return true;
    }
    for (int d = 0; d < MAZE_NUM_DIRS; d++){
        for (int step = 1; step <= arm[d]; step++){
            if (x == blast_x + DirX(d) * step && z == blast_z + DirZ(d) * step){
                return true;
            }
        }
    }
    return false;
}

float Maze::CharX() const {
    if (step_ticks <= 0){
        return (float)tile_x;
    }
    //step_ticks counts DOWN to arrival, so this is 0 at the start of a step and 1 at the end.
    float t = 1.0f - (float)step_ticks / (float)MAZE_STEP_TICKS;
    return (float)from_x + ((float)tile_x - (float)from_x) * t;
}

float Maze::CharZ() const {
    if (step_ticks <= 0){
        return (float)tile_z;
    }
    float t = 1.0f - (float)step_ticks / (float)MAZE_STEP_TICKS;
    return (float)from_z + ((float)tile_z - (float)from_z) * t;
}

//--- the walker ---------------------------------------------------------------------------------------

/*
    Begin a step, if we can.

    A step is ATOMIC: once begun it always completes, and no input is read until it has. That is what
    makes "which tile is the character on" answerable at every tick, and it is why the bomb can be
    placed on a tile rather than at a position. The cost is that the controls are slightly stiff,
    which is the usual trade and the right one at this stage.

    Facing is set even when the step is refused, so walking into a wall still turns the character to
    look at it - without that, pushing against a wall looks like the input was dropped.
*/
void Maze::TryStep(int dir){
    if (dir < 0 || dir >= MAZE_NUM_DIRS){
        return;
    }
    facing = dir;
    if (step_ticks > 0){
        return;     //already on the way somewhere
    }
    int nx = tile_x + DirX(dir);
    int nz = tile_z + DirZ(dir);
    if (!CanEnter(tile_x,tile_z,nx,nz)){
        return;
    }
    from_x = tile_x;
    from_z = tile_z;
    tile_x = nx;
    tile_z = nz;
    step_ticks = MAZE_STEP_TICKS;
}

/*
    The bomb becomes the blast.

    Each arm is walked out one tile at a time and stops at the first wall, so the flame is never
    drawn through anything and the view needs no clipping of its own - the arm lengths it is handed
    are already the truth. This is also the one place the difference between the two predicates
    matters: the walk asks BlocksBlast, so it runs straight over water and over bridges.
*/
void Maze::Explode(){
    f_bomb = false;
    f_blast = true;
    blast_x = bomb_x;
    blast_z = bomb_z;
    blast_ticks = 0;
    blast_count++;

    for (int d = 0; d < MAZE_NUM_DIRS; d++){
        int reach = 0;
        for (int step = 1; step <= MAZE_BLAST_RANGE; step++){
            if (BlocksBlast(blast_x + DirX(d) * step,blast_z + DirZ(d) * step)){
                break;
            }
            reach = step;
        }
        arm[d] = reach;
    }
}

void Maze::Tick(const MazeInput& input){
    //--- the walker -------------------------------------------------------------------------
    if (step_ticks > 0){
        step_ticks--;
    }
    //Checked AFTER the decrement, so a held direction rolls straight into the next step on the tick
    //the last one lands rather than costing an idle tick between every tile. Without this a walk
    //across the board is visibly stuttery and it looks like a frame-rate problem.
    if (step_ticks <= 0 && input.direction != MAZE_DIR_NONE){
        TryStep(input.direction);
    }

    //--- the bomb ---------------------------------------------------------------------------
    /*
        Placed on the tile the character is HEADING FOR while it is mid-step, which is `tile_x`
        either way - see the note on the walker's fields. That is the tile it will be standing on a
        moment later, and it is the one the player means.

        Refused while a bomb is already burning or a blast is still lit: one at a time for now.
    */
    if (input.f_place_bomb && !f_bomb && !f_blast){
        f_bomb = true;
        bomb_x = tile_x;
        bomb_z = tile_z;
        fuse_ticks = MAZE_FUSE_TICKS;
    }

    if (f_bomb){
        fuse_ticks--;
        if (fuse_ticks <= 0){
            Explode();
        }
    }

    //--- the blast --------------------------------------------------------------------------
    if (f_blast){
        blast_ticks++;
        if (blast_ticks > MAZE_BLAST_TICKS){
            f_blast = false;
        }
    }
}
