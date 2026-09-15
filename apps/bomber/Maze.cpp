#include "Maze.h"

//Manhattan distance between two cells. A local helper rather than <stdlib.h>'s abs, so this
//translation unit keeps depending on nothing at all - which is the point of the Maze/Application
//split and is worth more than four saved lines.
static int CellDistance(int ax, int az, int bx, int bz){
    int dx = ax > bx ? ax - bx : bx - ax;
    int dz = az > bz ? az - bz : bz - az;
    return dx + dz;
}

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

int Maze::DirOpposite(int dir){
    switch (dir){
        case MAZE_DIR_EAST:  return MAZE_DIR_WEST;
        case MAZE_DIR_WEST:  return MAZE_DIR_EAST;
        case MAZE_DIR_NORTH: return MAZE_DIR_SOUTH;
        case MAZE_DIR_SOUTH: return MAZE_DIR_NORTH;
        default:             return MAZE_DIR_NONE;
    }
}

//--- the walker ---------------------------------------------------------------------------------

float MazeWalker::X() const {
    if (step_ticks <= 0 || step_total <= 0){
        return (float)tile_x;
    }
    //step_ticks counts DOWN to arrival, so this is 0 at the start of a step and 1 at the end.
    float t = 1.0f - (float)step_ticks / (float)step_total;
    return (float)from_x + ((float)tile_x - (float)from_x) * t;
}

float MazeWalker::Z() const {
    if (step_ticks <= 0 || step_total <= 0){
        return (float)tile_z;
    }
    float t = 1.0f - (float)step_ticks / (float)step_total;
    return (float)from_z + ((float)tile_z - (float)from_z) * t;
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
    Water, and the bridges that span it.

    A POND IS A STRAIGHT RUN AND ITS BRIDGE RUNS DOWN THE LENGTH OF IT. That single choice is the
    whole difference between the two things this can look like, and it is worth stating because the
    other one seems just as reasonable on paper: lay the bridge ACROSS the run and every water cell
    gets its own one-tile plank, so a four-cell pond comes out as four little bridges side by side,
    each crossing nothing much. Lay it ALONG the run and they are one span - you step on at one
    bank, walk the length of the pond, and step off at the other, with the rope rails down the
    sides where they belong.

    So the run's axis IS the bridge's axis, and that one assignment is the whole mechanism. The
    view reads it to turn the model (see BOMBER_BRIDGE_YAW_X in ApplicationBomber) and the walker
    reads it to refuse a step over the rails, so the picture and the rule can never disagree.

    THE RUN IS GROWN BOTH WAYS FROM ITS SEED, not forwards from it, and that is not tidiness - it
    is what makes the spans actually happen. Growing one way meant a seed that landed just before a
    pillar produced a one-cell stub, and on a lattice of pillars stubs are most of what you get. A
    stub is a puddle with a plank on it. Growing out from the middle spends exactly the same rolls
    on a pond that is usually its full length.

    A BRIDGE IS ONLY LAID WHEN BOTH ITS ENDS REACH DRY LAND. A span that runs into a wall is a
    pier, and a pier is a crossing that does not cross - which is the rule the all-or-nothing note
    below has always stated, now applied to the ENDS as well as to the middle. It matters more than
    it used to: a cross-wise plank had two chances at a bank per water cell, a span has exactly two
    in total.
*/
void Maze::AddWater(){
    /*
        Whether this cell is a BANK of a bridge that is already down - that is, whether some
        neighbour is a bridge whose grain points at this cell, so that this is where you step off it.

        It exists only to be refused by can_flood below, and it earns its keep because the ponds are
        laid one after another out of the same bag of rolls: a later pond that floods an earlier
        bridge's landing turns a finished crossing into a pier, and it does it silently, several
        ponds after the bridge was checked and found good.
    */
    auto guards_bridge = [this](int x, int z){
        for (int d = 0; d < MAZE_NUM_DIRS; d++){
            int nx = x + DirX(d);
            int nz = z + DirZ(d);
            //DirAxis(d) is the axis of the step from here to that neighbour, so a bridge there with
            //that same grain is one whose planks run towards this cell.
            if (InBounds(nx,nz) && decor[nz][nx] == MAZE_DECOR_BRIDGE &&
                pass_axis[nz][nx] == DirAxis(d)){
                return true;
            }
        }
        return false;
    };
    /*
        Where water may go. Not out of bounds; not onto a pillar, because a pond that ate one would
        punch a hole in the lattice the zones are built on; not through somebody else's bridge,
        because cutting an earlier crossing in half leaves two piers and a gap in the middle; and
        not onto either of that bridge's banks - which together are the shape this function exists
        to avoid, from both ends.
    */
    auto can_flood = [this,&guards_bridge](int x, int z){
        return InBounds(x,z) && tile[z][x] != MAZE_TILE_WALL &&
               decor[z][x] != MAZE_DECOR_BRIDGE && !guards_bridge(x,z);
    };
    /*
        A bank: somewhere you can actually stand, whose own grain lets you step off the bridge onto
        it. The grain test is not hypothetical - a bank can be the last cell of ANOTHER bridge, and
        when the two agree on an axis the result is one longer span across two ponds, which is a
        better accident than a T-junction you cannot walk through.
    */
    auto is_bank = [this](int x, int z, uint8_t axis){
        return IsPassable(x,z) &&
               (pass_axis[z][x] == MAZE_AXIS_ANY || pass_axis[z][x] == axis);
    };

    for (int pond = 0; pond < MAZE_NUM_PONDS; pond++){
        //Along an axis, not a diagonal: a pond that steps diagonally cannot be bridged by a line of
        //bridge pieces, and the axis is what a bridge needs.
        int dir = RandomBelow(MAZE_NUM_DIRS);
        int span = MAZE_POND_MIN + RandomBelow(MAZE_POND_MAX - MAZE_POND_MIN + 1);
        int seed_x = 1 + RandomBelow(MAZE_W - 2);
        int seed_z = 1 + RandomBelow(MAZE_H - 2);
        int dx = DirX(dir);
        int dz = DirZ(dir);

        //Nothing to grow from. Skipped rather than nudged onto a free cell: the roll is what makes
        //a board this seed's board, and a generator that quietly moves its own dice is one nobody
        //can reason about afterwards.
        if (!can_flood(seed_x,seed_z)){
            continue;
        }

        //Out from the seed, backwards first and then forwards, up to `span` cells between them.
        //Only the AXIS of `d` matters here, since it grows both ways along it.
        auto reach = [&](int d, int& back, int& fwd){
            back = 0;
            while (back + 1 < span &&
                   can_flood(seed_x - DirX(d) * (back + 1),seed_z - DirZ(d) * (back + 1))){
                back++;
            }
            fwd = 0;
            while (back + fwd + 1 < span &&
                   can_flood(seed_x + DirX(d) * (fwd + 1),seed_z + DirZ(d) * (fwd + 1))){
                fwd++;
            }
        };

        int back = 0;
        int fwd = 0;
        reach(dir,back,fwd);
        /*
            IF THE ROLLED AXIS HAS NO ROOM, TAKE THE OTHER ONE. This is not fussiness, it is half
            the ponds: the pillar lattice puts a wall on every even x of every even row, so a run
            rolled east-west along an EVEN row is boxed in before it starts and comes out as a
            single cell of water in a corridor - which is not a pond, cannot be bridged (both of
            its banks are the pillars that boxed it in), and just blocks the corridor. The same
            seed one quarter-turn round has the whole row to grow into.

            The dice are not being re-rolled - the seed cell, the length and the bridge chance are
            all still the ones this pond was dealt, and a pond with room both ways keeps the axis
            it rolled. Only a pond that has nowhere to go is turned, and it is turned to a fixed
            axis rather than a random one so the board stays a function of the seed.
        */
        if (back + fwd + 1 < MAZE_POND_MIN){
            int other = (DirAxis(dir) == MAZE_AXIS_X) ? MAZE_DIR_SOUTH : MAZE_DIR_EAST;
            int other_back = 0;
            int other_fwd = 0;
            reach(other,other_back,other_fwd);
            if (other_back + other_fwd > back + fwd){
                dir = other;
                back = other_back;
                fwd = other_fwd;
                dx = DirX(dir);
                dz = DirZ(dir);
            }
        }

        int x = seed_x - dx * back;         //the first cell of the run, walking towards `dir`
        int z = seed_z - dz * back;
        int laid = back + fwd + 1;

        //Bridge the whole of it, some of the time. All or nothing - see the note above. Rolled
        //BEFORE the water goes down, because a pond that is going to be bridged is shaped
        //differently from one that is not - which is the next paragraph.
        uint8_t axis = DirAxis(dir);
        bool f_bridge = (RandomBelow(100) < MAZE_BRIDGE_PCT);
        if (f_bridge){
            /*
                A POND THAT IS ABOUT TO BE BRIDGED STOPS ONE CELL SHORT OF ANYTHING ITS BRIDGE
                CANNOT LAND ON, by giving up cells from whichever end has nowhere to go until it
                has two banks or nothing left.

                It is the difference between a rule and a lottery. A run grows until it meets a
                wall, so the cell that stopped it IS the wall, and simply refusing to bridge such a
                pond threw away most of them - measured over a thousand seeds, only 71% of boards
                had a crossing anywhere on them, against 96% before any of this. Retracting the
                pond by one instead leaves that cell dry, and a dry cell is exactly what a bridge
                needs at its end. The pond is one shorter and the board has a crossing on it.

                Giving up EVERYTHING is a real outcome and not a failure - a two-cell run wedged
                between two walls has no dry end to offer - and then it is simply a pond, laid at
                its full length, which is what the untouched x/z/laid below still describe.
            */
            int bx = x;
            int bz = z;
            int blen = laid;
            while (blen > 0 && !is_bank(bx - dx,bz - dz,axis)){
                bx += dx;                   //the cell we just gave up is the bank now
                bz += dz;
                blen--;
            }
            //bx + dx * blen does not move while the back end retracts, so this is still the far end.
            while (blen > 0 && !is_bank(bx + dx * blen,bz + dz * blen,axis)){
                blen--;
            }
            if (blen > 0){
                x = bx;
                z = bz;
                laid = blen;
            }else{
                f_bridge = false;
            }
        }

        for (int i = 0; i < laid; i++){
            tile[z + dz * i][x + dx * i] = MAZE_TILE_WATER;
            decor[z + dz * i][x + dx * i] = MAZE_DECOR_NONE;
            pass_axis[z + dz * i][x + dx * i] = MAZE_AXIS_ANY;
        }
        if (!f_bridge){
            continue;
        }
        for (int i = 0; i < laid; i++){
            decor[z + dz * i][x + dx * i] = MAZE_DECOR_BRIDGE;
            pass_axis[z + dz * i][x + dx * i] = axis;
        }
    }
}

/*
    Hedges and wooden walls, scattered over the open floor.

    AFTER THE REACHABILITY PASS, ON PURPOSE. A soft block is not a wall: everything behind one is
    somewhere you can get to as soon as you have dug, so counting it as unreachable would be wrong
    and - worse - the prune would turn the pocket behind it into real wall and make the lie true.
    Running the prune over the hard walls only and then scattering these on what is left is what
    makes `reachable_cells` mean "reachable once you have done the work".

    The spawn's elbow room is kept clear so the opening move is never "dig your way out", and
    bridges are skipped because a blocked bridge is a crossing that does not cross.
*/
void Maze::AddSoftBlocks(){
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (tile[z][x] != MAZE_TILE_GRASS){
                continue;
            }
            if (pass_axis[z][x] != MAZE_AXIS_ANY){
                continue;
            }
            if (CellDistance(x,z,MAZE_SPAWN_X,MAZE_SPAWN_Z) <= MAZE_SPAWN_CLEAR){
                continue;
            }
            if (RandomBelow(100) >= MAZE_SOFT_BLOCK_PCT){
                continue;
            }
            //More hedge than wood, because the hedge is the one an enemy can cut and the one that
            //therefore makes the board move on its own.
            tile[z][x] = (RandomBelow(100) < 60) ? MAZE_TILE_HEDGE : (uint8_t)MAZE_TILE_WOOD;
        }
    }
}

/*
    The pickups, buried under soft blocks.

    UNDER blocks rather than lying on the floor, which is what makes blowing a hedge up worth doing
    for its own sake rather than only when it is in the way. Nothing marks them as hidden: an item
    on an impassable cell cannot be walked onto, so the tile above it IS the lid.

    Chosen by shuffling the list of soft blocks and taking the front of it, rather than by rolling
    until enough have been placed: a board that happens to have three soft blocks then places three
    items instead of spinning, and the count is exact rather than probable. The two kinds alternate
    so a field never buries four of the same thing.
*/
void Maze::AddItems(){
    int cell_x[MAZE_W * MAZE_H];
    int cell_z[MAZE_W * MAZE_H];
    int num_cells = 0;
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (IsSoft(x,z)){
                cell_x[num_cells] = x;
                cell_z[num_cells] = z;
                num_cells++;
            }
        }
    }

    int want = num_cells < MAZE_NUM_ITEMS ? num_cells : MAZE_NUM_ITEMS;
    for (int i = 0; i < want; i++){
        int j = i + RandomBelow(num_cells - i);
        int tx = cell_x[i]; cell_x[i] = cell_x[j]; cell_x[j] = tx;
        int tz = cell_z[i]; cell_z[i] = cell_z[j]; cell_z[j] = tz;
        //The cells are shuffled and the LIST IS NOT, so the mix on a full board is exact while
        //where each thing landed is not. See MAZE_ITEM_ORDER.
        item[cell_z[i]][cell_x[i]] = MAZE_ITEM_ORDER[i];
    }
}

/*
    The enemies.

    Placed by the same shuffle-and-take as the items, and for the same reason. The distance rule is
    the whole design: an enemy that starts within a few tiles of the spawn is not a threat, it is an
    ambush, and the player has done nothing yet to deserve one.

    Bridges are excluded by the pass_axis test, which also takes care of water - a water cell is
    only passable when it has a bridge on it, and a bridge always sets an axis.
*/
void Maze::PlaceEnemies(){
    num_enemies = 0;

    int cell_x[MAZE_W * MAZE_H];
    int cell_z[MAZE_W * MAZE_H];
    int num_cells = 0;
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (!IsPassable(x,z) || pass_axis[z][x] != MAZE_AXIS_ANY){
                continue;
            }
            if (CellDistance(x,z,MAZE_SPAWN_X,MAZE_SPAWN_Z) < MAZE_ENEMY_MIN_DIST){
                continue;
            }
            cell_x[num_cells] = x;
            cell_z[num_cells] = z;
            num_cells++;
        }
    }

    int want = num_cells < MAZE_MAX_ENEMIES ? num_cells : MAZE_MAX_ENEMIES;
    for (int i = 0; i < want; i++){
        int j = i + RandomBelow(num_cells - i);
        int tx = cell_x[i]; cell_x[i] = cell_x[j]; cell_x[j] = tx;
        int tz = cell_z[i]; cell_z[i] = cell_z[j]; cell_z[j] = tz;

        MazeWalker& walker = enemy[num_enemies++];
        walker = MazeWalker();
        walker.tile_x = walker.from_x = cell_x[i];
        walker.tile_z = walker.from_z = cell_z[i];
        walker.step_total = MAZE_ENEMY_STEP_TICKS;
        walker.facing = RandomBelow(MAZE_NUM_DIRS);
        walker.f_alive = true;
    }
}

void Maze::AddDecor(){
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (IsBlock(x,z)){
                continue;
            }
            /*
                Water gets lillies and nothing else.

                The `decor == NONE` test is what keeps a pad off a bridge: a bridge IS this cell's
                decor, so a spanned cell is never empty water. Everything below this point is for
                dry land, which is why this returns rather than falling through - a plant growing
                out of a pond would be the same bug the other way round.
            */
            if (tile[z][x] == MAZE_TILE_WATER){
                if (decor[z][x] == MAZE_DECOR_NONE && RandomBelow(100) < MAZE_LILLY_PCT){
                    decor[z][x] = MAZE_DECOR_LILLY;
                }
                continue;
            }
            if (decor[z][x] != MAZE_DECOR_NONE){
                continue;
            }
            int roll = RandomBelow(100);
            if (roll < 7){
                decor[z][x] = MAZE_DECOR_FLOWERS;
            }else if (roll < 12){
                decor[z][x] = MAZE_DECOR_FLOWERS_TALL;
            }else if (roll < 18){
                decor[z][x] = MAZE_DECOR_PLANT;
            }else if (roll < 20){
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

    //Anything walkable that was not reached becomes wall.
    for (int z = 1; z < MAZE_H - 1; z++){
        for (int x = 1; x < MAZE_W - 1; x++){
            if (seen[z][x]){
                continue;
            }
            if (tile[z][x] == MAZE_TILE_WATER){
                /*
                    THE POND STAYS, ITS BRIDGE GOES. The water was never walkable, so a pond behind
                    a wall reads as scenery rather than as a place you failed to get to - but a
                    BRIDGE is a promise that you can cross, and one nobody can reach is a crossing
                    to nowhere with a wall at each end.

                    This is cheap now and it was not before: the banks are walled off by this very
                    loop, several cells after the generator checked them and found them good, so
                    AddWater cannot see it coming. It also shows far more than it used to, because a
                    span runs the whole length of its pond - what used to be one stranded plank is
                    now four of them in a sealed pocket.
                */
                decor[z][x] = MAZE_DECOR_NONE;
                pass_axis[z][x] = MAZE_AXIS_ANY;
                continue;
            }
            if (IsBlock(x,z)){
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

    THE ORDER OF THE LAST FIVE STEPS IS LOAD-BEARING and is the one thing here worth reading twice:
    prune, then soft blocks, then items under those blocks, then enemies on what is still open, then
    decoration on what is left. Each step depends on the one before having settled - see AddSoftBlocks
    for why the prune cannot come after it.
*/
int Maze::LayOutTerrain(){
    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            tile[z][x] = MAZE_TILE_WALL;
            decor[z][x] = MAZE_DECOR_NONE;
            item[z][x] = MAZE_ITEM_NONE;
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

    return PruneUnreachable();
}

void Maze::NewGame(uint32_t seed){
    rng_state = seed ? seed : 1;

    /*
        Roll a layout, and roll again if it came out as a corner of a field rather than a field.

        The stream is NOT reseeded between attempts, so the second layout is a different one and the
        whole sequence is still decided by `seed`. The last attempt is kept whatever it scored: a
        small board is worse than a big one but still better than no board, and a loop that could
        fail to terminate is worse than either.
    */
    for (int attempt = 0; attempt < MAZE_LAYOUT_ATTEMPTS; attempt++){
        reachable_cells = LayOutTerrain();
        if (reachable_cells >= MAZE_MIN_PLAYABLE){
            break;
        }
    }

    AddSoftBlocks();
    AddItems();
    PlaceEnemies();
    //Decoration last, so nothing is scattered on a cell that is about to become a wall or a hedge.
    AddDecor();

    /*
        Floor variety, purely cosmetic: grass mostly, with brick and rock mixed in so the board reads
        as ground rather than as a colour. Done here, after every rule has settled, precisely because
        it changes nothing - grass, brick and rock are the same tile as far as the walker is concerned.

        IT DELIBERATELY SKIPS THE SOFT BLOCKS, which is what lets the view treat a cell's floor as
        fixed for the life of the field: a hedge turns into GRASS when it burns, and the floor that
        was already under it is the grass tile, so nothing has to be swapped at the moment of the
        explosion. Give a hedge a rock floor here and that stops being true.
    */
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

    player = MazeWalker();
    player.tile_x = player.from_x = MAZE_SPAWN_X;
    player.tile_z = player.from_z = MAZE_SPAWN_Z;
    player.step_total = MAZE_STEP_TICKS;
    player.facing = MAZE_DIR_SOUTH;
    player.f_alive = true;

    health = MAZE_START_HEALTH;
    //A board is a round, so the score starts again with it. Dying does NOT reset it - see the note
    //on `score`.
    score = 0;
    shield_ticks = 0;
    invuln_ticks = 0;
    dead_ticks = 0;
    deaths = 0;
    items_taken = 0;

    f_bomb = false;
    fuse_ticks = 0;
    f_blast = false;
    blast_ticks = 0;
    blast_count = 0;
    blocks_destroyed = 0;
    blocks_cut = 0;
    enemies_killed = 0;
    for (int d = 0; d < MAZE_NUM_DIRS; d++){
        arm[d] = 0;
    }

    //A view that has never looked at this field is out of date by definition, so the counter moves
    //even though nothing was "changed" - see the note on field_version.
    field_version++;
}

//--- queries -----------------------------------------------------------------------------------------

bool Maze::IsBlock(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    //A range test, which the enum's ordering exists to allow - see the note on MazeTile.
    return tile[z][x] >= MAZE_TILE_WALL && tile[z][x] < MAZE_TILE_COUNT;
}

bool Maze::IsSoft(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    return tile[z][x] == MAZE_TILE_HEDGE || tile[z][x] == MAZE_TILE_WOOD;
}

bool Maze::IsChoppable(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    //Hedge only. An enemy carries shears, not an axe, and the difference is what makes the two soft
    //block types worth having as two things rather than one with two models.
    return tile[z][x] == MAZE_TILE_HEDGE;
}

bool Maze::IsPassable(int x, int z) const {
    if (!InBounds(x,z)){
        return false;
    }
    if (IsBlock(x,z)){
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
    return IsBlock(x,z);
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

bool Maze::IsDangerous(int x, int z) const {
    //The flame, not the smoke it leaves behind - see MAZE_BLAST_HURT_TICKS.
    if (!f_blast || blast_ticks > MAZE_BLAST_HURT_TICKS){
        return false;
    }
    return IsBurning(x,z);
}

//--- the walkers --------------------------------------------------------------------------------------

/*
    Begin a step, if we can.

    A step is ATOMIC: once begun it always completes, and no input is read until it has. That is what
    makes "which tile is the character on" answerable at every tick, and it is why the bomb can be
    placed on a tile rather than at a position. The cost is that the controls are slightly stiff,
    which is the usual trade and the right one at this stage.

    Facing is set even when the step is refused, so walking into a wall still turns the character to
    look at it - without that, pushing against a wall looks like the input was dropped. It is also
    what lets an enemy decide to cut a hedge by turning towards it first.
*/
void Maze::StepWalker(MazeWalker& walker, int dir, int step_ticks){
    if (dir < 0 || dir >= MAZE_NUM_DIRS){
        return;
    }
    walker.facing = dir;
    if (walker.step_ticks > 0 || walker.chop_ticks > 0){
        return;     //already on the way somewhere, or busy with the shears
    }
    int nx = walker.tile_x + DirX(dir);
    int nz = walker.tile_z + DirZ(dir);
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

void Maze::ClearBlock(int x, int z){
    if (!IsBlock(x,z)){
        return;
    }
    /*
        Straight to grass, and the DECOR AND ITEM ARE LEFT ALONE.

        Decor, because a block cell never had any - AddDecor skips them - so there is nothing to
        clear and pretending otherwise would hide a bug if that ever changed. The item, because it
        is the whole reason to knock the block down: it becomes reachable the instant the tile
        stops being impassable, with nothing having to reveal it.
    */
    tile[z][x] = MAZE_TILE_GRASS;
    field_version++;
}

/*
    The bomb becomes the blast.

    Each arm is walked out one tile at a time, so the flame is never drawn through anything and the
    view needs no clipping of its own - the arm lengths it is handed are already the truth. This is
    also the one place all three predicates matter at once:

      - the walk asks BlocksBlast, so it runs straight over water and over bridges;
      - a SOFT block is reached, burnt away, and stops the arm THERE - the flame gets to the hedge,
        which is what destroys it, and does not continue past where the hedge was;
      - a hard wall stops the arm BEFORE it, because the flame never gets to that cell at all.

    The one-tile difference between those last two is the whole rule, and getting it the other way
    round is what makes a bomberman blast either unable to clear blocks or able to see through them.
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
            int nx = blast_x + DirX(d) * step;
            int nz = blast_z + DirZ(d) * step;
            if (!InBounds(nx,nz)){
                break;
            }
            if (IsSoft(nx,nz)){
                ClearBlock(nx,nz);
                blocks_destroyed++;
                reach = step;
                break;
            }
            if (BlocksBlast(nx,nz)){
                break;
            }
            reach = step;
        }
        arm[d] = reach;
    }
}

/*
    One tick of every enemy.

    The whole of the AI, and deliberately small: an enemy finishes what it is doing, dies if it is
    standing in fire, cuts a hedge if one is in front of it, and otherwise wanders with a bias
    towards carrying straight on. There is no pathfinding and no awareness of the player, because an
    enemy that hunts you is a different game and this one has no animation to sell it yet.

    The wander rule is the only part with any subtlety: reversing is allowed only when there is
    nothing else, which is what keeps one from jittering back and forth in a corridor and is what
    makes it eventually cover ground.
*/
void Maze::TickEnemies(){
    for (int i = 0; i < num_enemies; i++){
        MazeWalker& walker = enemy[i];
        if (!walker.f_alive){
            //Dead, but possibly still falling over. Nothing else about a corpse is simulated.
            if (walker.death_ticks > 0){
                walker.death_ticks--;
            }
            continue;
        }

        //Caught by the flame. Checked here rather than in Explode so that walking INTO a blast that
        //is already burning is just as fatal as being under the bomb - one rule instead of two.
        if (IsDangerous(walker.tile_x,walker.tile_z)){
            walker.f_alive = false;
            walker.death_ticks = MAZE_DEATH_TICKS;
            //A death interrupts whatever it was doing. Without this a corpse keeps a chop pending
            //and the hedge it was cutting falls over some time after its killer walked away.
            walker.chop_ticks = 0;
            walker.step_ticks = 0;
            enemies_killed++;
            continue;
        }

        if (walker.step_ticks > 0){
            walker.step_ticks--;
            continue;
        }

        if (walker.chop_ticks > 0){
            walker.chop_ticks--;
            if (walker.chop_ticks <= 0){
                //Checked again on the way out: the player may have blown the same hedge up while
                //this one was busy with it, and cutting a hole in a cell that is already open would
                //bump field_version for nothing.
                int ax = walker.tile_x + DirX(walker.facing);
                int az = walker.tile_z + DirZ(walker.facing);
                if (IsChoppable(ax,az)){
                    ClearBlock(ax,az);
                    blocks_cut++;
                }
            }
            continue;
        }

        //A hedge straight ahead is worth stopping for.
        int ax = walker.tile_x + DirX(walker.facing);
        int az = walker.tile_z + DirZ(walker.facing);
        if (IsChoppable(ax,az)){
            walker.chop_ticks = MAZE_CHOP_TICKS;
            continue;
        }

        int back = DirOpposite(walker.facing);
        int cand[MAZE_NUM_DIRS];
        int num_cand = 0;
        bool f_straight_on = false;
        for (int d = 0; d < MAZE_NUM_DIRS; d++){
            int nx = walker.tile_x + DirX(d);
            int nz = walker.tile_z + DirZ(d);
            if (!CanEnter(walker.tile_x,walker.tile_z,nx,nz)){
                continue;
            }
            if (d == walker.facing){
                f_straight_on = true;
            }
            if (d != back){
                cand[num_cand++] = d;
            }
        }

        int dir;
        if (num_cand == 0){
            dir = back;                             //a dead end: the only way out is back
        }else if (f_straight_on && RandomBelow(100) < 70){
            dir = walker.facing;                    //keep going, mostly
        }else{
            dir = cand[RandomBelow(num_cand)];
        }
        StepWalker(walker,dir,MAZE_ENEMY_STEP_TICKS);
    }
}

/*
    Pick up whatever the player is standing on.

    ON ARRIVAL, not on departure: the test is step_ticks == 0, so an item is taken when the walker
    is actually on the cell rather than when it commits to walking there. Mid-step `tile_x` is
    already the destination, and collecting then would let an item be taken from a tile away.
*/
void Maze::TickItems(){
    if (!player.f_alive || player.step_ticks > 0){
        return;
    }
    int x = player.tile_x;
    int z = player.tile_z;
    if (!InBounds(x,z) || item[z][x] == MAZE_ITEM_NONE){
        return;
    }
    //Cannot happen while the player is standing here - the lid would have to be on top of them -
    //but it states what "hidden" actually means rather than leaving it implied by the tile.
    if (!IsPassable(x,z)){
        return;
    }

    switch (item[z][x]){
        case MAZE_ITEM_HEALTH:
            //Capped rather than banked. A health pickup found at full health is wasted, which is
            //what makes taking a hit cost something beyond the number.
            if (health < MAZE_START_HEALTH){
                health++;
            }
            break;
        case MAZE_ITEM_SHIELD:
            //Refreshed rather than added, so two shields in a row are not twenty seconds.
            shield_ticks = MAZE_SHIELD_TICKS;
            break;
        default:
            /*
                The treasures, which are the whole of the default case and are meant to be.

                MazeItemScore returns 0 for anything that is not one, so a pickup added later that
                forgets to say what it is worth adds nothing rather than adding garbage - and a
                treasure added later needs no case here at all, only a line in that switch.
            */
            score += (uint32_t)MazeItemScore(item[z][x]);
            break;
    }
    item[z][x] = MAZE_ITEM_NONE;
    items_taken++;
    field_version++;
}

/*
    Who hurt the player this tick, and the respawn clock.

    THE FIELD IS NOT REGENERATED ON DEATH. Losing the board you were in the middle of looking at is
    worse than dying, and this app is a bench as much as it is a game - so the player lies there for
    MAZE_RESPAWN_TICKS and gets up on the spawn with full health, on the same field, with whatever
    has already been blown up still blown up.
*/
void Maze::TickPlayerCondition(){
    if (shield_ticks > 0){
        shield_ticks--;
    }
    if (invuln_ticks > 0){
        invuln_ticks--;
    }

    if (!player.f_alive){
        dead_ticks++;
        if (dead_ticks >= MAZE_RESPAWN_TICKS){
            player = MazeWalker();
            player.tile_x = player.from_x = MAZE_SPAWN_X;
            player.tile_z = player.from_z = MAZE_SPAWN_Z;
            player.step_total = MAZE_STEP_TICKS;
            player.facing = MAZE_DIR_SOUTH;
            player.f_alive = true;
            health = MAZE_START_HEALTH;
            dead_ticks = 0;
            //Long enough to walk off the spawn if the spawn is what killed you.
            invuln_ticks = MAZE_HIT_INVULN_TICKS;
        }
        return;
    }

    bool f_hit = IsDangerous(player.tile_x,player.tile_z);
    for (int i = 0; i < num_enemies && !f_hit; i++){
        //Same TILE, not an overlap test - the whole reason the walkers are on a grid. Mid-step both
        //are drawn between cells, so this fires a little before they visibly touch, which is the
        //right way round: being killed by something that had not reached you yet reads as a bug,
        //and so does walking through one unharmed.
        if (enemy[i].f_alive &&
            enemy[i].tile_x == player.tile_x && enemy[i].tile_z == player.tile_z){
            f_hit = true;
        }
    }

    if (f_hit && shield_ticks <= 0 && invuln_ticks <= 0){
        health--;
        invuln_ticks = MAZE_HIT_INVULN_TICKS;
        if (health <= 0){
            health = 0;
            player.f_alive = false;
            dead_ticks = 0;
            deaths++;
        }
    }
}

void Maze::Tick(const MazeInput& input){
    //--- the player -------------------------------------------------------------------------
    if (player.f_alive){
        if (player.step_ticks > 0){
            player.step_ticks--;
        }
        //Checked AFTER the decrement, so a held direction rolls straight into the next step on the
        //tick the last one lands rather than costing an idle tick between every tile. Without this a
        //walk across the board is visibly stuttery and it looks like a frame-rate problem.
        if (player.step_ticks <= 0 && input.direction != MAZE_DIR_NONE){
            StepWalker(player,input.direction,MAZE_STEP_TICKS);
        }

        /*
            Placed on the tile the character is HEADING FOR while it is mid-step, which is `tile_x`
            either way - see the note on MazeWalker. That is the tile it will be standing on a
            moment later, and it is the one the player means.

            Refused while a bomb is already burning or a blast is still lit: one at a time for now.
        */
        if (input.f_place_bomb && !f_bomb && !f_blast){
            f_bomb = true;
            bomb_x = player.tile_x;
            bomb_z = player.tile_z;
            fuse_ticks = MAZE_FUSE_TICKS;
        }
    }

    //--- the bomb ---------------------------------------------------------------------------
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

    //--- everyone else ----------------------------------------------------------------------
    //After the blast clock, so an enemy that was standing where a bomb just went off dies on the
    //same tick the flame appears rather than on the next one.
    TickEnemies();
    TickItems();
    TickPlayerCondition();
}
