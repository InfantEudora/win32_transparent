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
    //Modulo, with the bias accepted: the stream is 32 bits and n is never more than a few dozen,
    //so the skew is one part in a hundred million and nothing here is a lottery.
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

//--- the field ------------------------------------------------------------------------------------

/*
    Lays out a field.

    The bones are the classic's: a wall border, and an indestructible pillar on every cell whose
    x and z are both even. That grid is what makes a bomberman maze navigable rather than a random
    mess - the open cells form corridors one tile wide with no dead ends, guaranteed, without
    anything having to check. Everything after it is decoration on that skeleton:

      - the floor gets a type per cell, mostly grass with brick and rock mixed in, so the board
        has some texture to it and the three terrain meshes all get used;
      - a few ponds of water, which a walker cannot cross but a blast can;
      - a bridge over some of the water, because "the tile decides whether you may stand there"
        is a rule worth being able to see working in both directions;
      - grass, plants and the occasional turd on open ground.

    THE SPAWN IS CLEARED LAST and unconditionally. Whatever the dice did, the character starts on
    a tile it can stand on with two neighbours it can walk to - otherwise a seed in a hundred puts
    it in a pond and the game looks broken rather than unlucky.
*/
void Maze::NewGame(uint32_t seed){
    rng_state = seed ? seed : 1;

    for (int z = 0; z < MAZE_H; z++){
        for (int x = 0; x < MAZE_W; x++){
            decor[z][x] = MAZE_DECOR_NONE;

            //The border, and the pillar grid inside it.
            if (x == 0 || z == 0 || x == MAZE_W - 1 || z == MAZE_H - 1){
                tile[z][x] = MAZE_TILE_WALL;
                continue;
            }
            if ((x % 2) == 0 && (z % 2) == 0){
                tile[z][x] = MAZE_TILE_WALL;
                continue;
            }

            //Open ground. Grass most of the time so the board reads as a lawn with features in
            //it rather than as three-colour noise.
            int roll = RandomBelow(100);
            if (roll < 62){
                tile[z][x] = MAZE_TILE_GRASS;
            }else if (roll < 84){
                tile[z][x] = MAZE_TILE_BRICK;
            }else{
                tile[z][x] = MAZE_TILE_ROCK;
            }
        }
    }

    /*
        Ponds.

        Grown as short runs rather than scattered per cell, because single water tiles read as a
        mistake and a run of three reads as a stream. They are laid over open ground only - a pond
        that ate a pillar would punch a hole in the grid the layout above guarantees.
    */
    for (int pond = 0; pond < 7; pond++){
        int x = 1 + RandomBelow(MAZE_W - 2);
        int z = 1 + RandomBelow(MAZE_H - 2);
        int dir = RandomBelow(MAZE_NUM_DIRS);
        int run = 2 + RandomBelow(3);
        for (int i = 0; i < run; i++){
            if (InBounds(x,z) && tile[z][x] != MAZE_TILE_WALL){
                tile[z][x] = MAZE_TILE_WATER;
                //A bridge over roughly a third of it, so some water is crossable and some is not
                //and the difference is visible from across the board.
                if (RandomBelow(3) == 0){
                    decor[z][x] = MAZE_DECOR_BRIDGE;
                }
            }
            x += DirX(dir);
            z += DirZ(dir);
        }
    }

    //Scatter. Only on dry open ground, and never where a bridge already is.
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

    //The spawn, and the two cells it can step to. Cleared after everything else so nothing can
    //have been dropped on them - see the note above.
    const int spawn[3][2] = {{1,1},{2,1},{1,2}};
    for (int i = 0; i < 3; i++){
        int x = spawn[i][0];
        int z = spawn[i][1];
        tile[z][x] = MAZE_TILE_GRASS;
        decor[z][x] = MAZE_DECOR_NONE;
    }

    tile_x = from_x = 1;
    tile_z = from_z = 1;
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

bool Maze::BlocksBlast(int x, int z) const {
    //Out of bounds counts as blocking, so an arm cannot walk off the board. Water deliberately
    //does NOT block: it is at ground level and there is nothing there to stop a flame.
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

/*
    Begin a step, if we can.

    A step is ATOMIC: once begun it always completes, and no input is read until it has. That is
    what makes "which tile is the character on" answerable at every tick, and it is why the bomb
    can be placed on a tile rather than at a position. The cost is that the controls are slightly
    stiff, which is the usual trade and the right one at this stage.

    Facing is set even when the step is refused, so walking into a wall still turns the character
    to look at it - without that, pushing against a wall looks like the input was dropped.
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
    if (!IsPassable(nx,nz)){
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
    drawn through anything and the view needs no clipping of its own - the arm lengths it is
    handed are already the truth. This is also the one place the difference between the two
    predicates matters: the walk asks BlocksBlast, so it runs straight over water.
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
    //Checked AFTER the decrement, so a held direction rolls straight into the next step on the
    //tick the last one lands rather than costing an idle tick between every tile. Without this a
    //walk across the board is visibly stuttery and it looks like a frame-rate problem.
    if (step_ticks <= 0 && input.direction != MAZE_DIR_NONE){
        TryStep(input.direction);
    }

    //--- the bomb ---------------------------------------------------------------------------
    /*
        Placed on the tile the character is HEADING FOR while it is mid-step, which is `tile_x`
        either way - see the note on the walker's fields. That is the tile it will be standing on
        a moment later, and it is the one the player means.

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
