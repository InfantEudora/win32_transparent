#include "Playfield.h"
#include "Debug.h"
#include "type_helpers.h"

static Debugger* debug = new Debugger("Playfield",DEBUG_ALL);

/*
    Gravity, in ticks per cell, by level. These are the NES frame counts, which is why the app
    runs its simulation at 60 ticks/second (ApplicationTetris::Init) - at 60Hz the table means
    exactly what it meant on the original hardware, and level 19 really is the wall everybody
    remembers. Index 0 is unused; levels are 1-based.
*/
static const int gravity_table[] = {
    0, 48, 43, 38, 33, 28, 23, 18, 13, 8, 6,
    5, 5, 5, 4, 4, 4, 3, 3, 3, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 1
};
#define GRAVITY_TABLE_SIZE  ((int)(sizeof(gravity_table)/sizeof(gravity_table[0])))

//How much faster a held soft drop is than the level's own gravity. 20x is the guideline value,
//and it is a divisor rather than a fixed rate so soft drop never ends up SLOWER than gravity at
//high levels.
#define SOFT_DROP_DIVISOR   20

Playfield::Playfield(){
    NewGame(1);
}

int Playfield::GravityTicksForLevel(int level){
    if (level < 1){
        level = 1;
    }
    if (level >= GRAVITY_TABLE_SIZE){
        return gravity_table[GRAVITY_TABLE_SIZE - 1];
    }
    return gravity_table[level];
}

void Playfield::NewGame(uint32_t seed){
    for (int y = 0; y < TETRIS_BOARD_H; y++){
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            board[y][x] = -1;
        }
    }
    random.SetSeed(seed);
    bag.clear();
    next_queue.clear();
    clearing_rows.clear();
    hold_type = -1;
    f_hold_used = false;
    score = 0;
    lines = 0;
    level = 1;
    pieces_placed = 0;
    ticks_elapsed = 0;
    gravity_ticks = 0;
    lock_ticks = 0;
    lock_resets = 0;
    phase = TETRIS_PHASE_SPAWN;
    phase_ticks = TETRIS_SPAWN_DELAY_TICKS;
    //Fill the preview queue up front so the HUD has something to show on the very first frame.
    while ((int)next_queue.size() < TETRIS_NEXT_QUEUE_SHOWN + 1){
        if (bag.empty()){
            RefillBag();
        }
        next_queue.push_back(bag.back());
        bag.pop_back();
    }
}

//The "7-bag": each of the seven pieces exactly once per bag, in a shuffled order. The reason
//every modern Tetris uses it is that a pure uniform draw can withhold an I piece for a very long
//time, which feels like the game cheating rather than like bad luck.
void Playfield::RefillBag(){
    bag.clear();
    for (int i = 0; i < TETROMINO_COUNT; i++){
        bag.push_back(i);
    }
    //Fisher-Yates, drawing from this object's own generator only - see TetrisRandom.
    for (int i = (int)bag.size() - 1; i > 0; i--){
        int j = random.GetInt(0,i);
        int swap = bag[i];
        bag[i] = bag[j];
        bag[j] = swap;
    }
}

int Playfield::DrawNextPiece(){
    if (next_queue.empty()){
        if (bag.empty()){
            RefillBag();
        }
        next_queue.push_back(bag.back());
        bag.pop_back();
    }
    int type = next_queue.front();
    next_queue.erase(next_queue.begin());
    if (bag.empty()){
        RefillBag();
    }
    next_queue.push_back(bag.back());
    bag.pop_back();
    return type;
}

bool Playfield::IsPositionLegal(int type, int rotation, int x, int y) const{
    const TetrominoCell* cells = GetTetrominoCells(type,rotation);
    for (int i = 0; i < 4; i++){
        int cx = x + cells[i].x;
        int cy = y + cells[i].y;
        if (cx < 0 || cx >= TETRIS_BOARD_W || cy < 0){
            return false;
        }
        //Above the top of the well is legal: a tall piece may stick out while it is being
        //manoeuvred, and only a cell that has to SETTLE up there ends the game. The board array
        //has no rows up there, so there is nothing to test against either.
        if (cy >= TETRIS_BOARD_H){
            continue;
        }
        if (board[cy][cx] >= 0){
            return false;
        }
    }
    return true;
}

void Playfield::SpawnPiece(int type, TetrisEvents& events){
    piece_type = type;
    piece_rotation = 0;
    //Centred: the box is 3 or 4 wide in a 10-wide well, and the classic placement is the left of
    //the two middle columns for a 3-box (x=3) and column 3 for the 4-box I.
    piece_x = (TETRIS_BOARD_W - GetTetrominoBoxSize(type)) / 2;
    //Sit the piece's TOP row on the top row of the well, so it is fully visible the moment it
    //appears. The guideline spawns into two hidden rows above the well instead, which at level 1
    //means a full second where the player cannot see what they are steering.
    piece_y = (TETRIS_BOARD_H - 1) - GetTetrominoSpawnTopOffset(type);
    gravity_ticks = GravityTicksForLevel(level);
    lock_ticks = 0;
    lock_resets = 0;
    f_hold_used = false;

    if (!IsPositionLegal(piece_type,piece_rotation,piece_x,piece_y)){
        //Block out: the stack has reached the spawn area. This is the only way to lose.
        phase = TETRIS_PHASE_GAMEOVER;
        events.f_game_over = true;
        return;
    }
    phase = TETRIS_PHASE_FALLING;
}

bool Playfield::TryMove(int dx, int dy){
    if (!IsPositionLegal(piece_type,piece_rotation,piece_x + dx,piece_y + dy)){
        return false;
    }
    piece_x += dx;
    piece_y += dy;
    return true;
}

bool Playfield::TryRotate(bool f_clockwise, TetrisEvents& events){
    int from = piece_rotation & 3;
    int to = f_clockwise ? ((from + 1) & 3) : ((from + 3) & 3);
    int num_kicks = 0;
    const TetrominoCell* kicks = GetTetrominoKicks(piece_type,from,f_clockwise,num_kicks);
    for (int i = 0; i < num_kicks; i++){
        int test_x = piece_x + kicks[i].x;
        int test_y = piece_y + kicks[i].y;
        if (IsPositionLegal(piece_type,to,test_x,test_y)){
            piece_rotation = to;
            piece_x = test_x;
            piece_y = test_y;
            events.f_rotated = true;
            return true;
        }
    }
    //Every kick was blocked. A refused rotation is worth reporting: it is the difference between
    //"the controls are unresponsive" and "there was genuinely nowhere to turn".
    events.f_rotation_refused = true;
    return false;
}

int Playfield::GetGhostY() const{
    int y = piece_y;
    while (IsPositionLegal(piece_type,piece_rotation,piece_x,y - 1)){
        y--;
    }
    return y;
}

void Playfield::AwardLineScore(int num_lines, TetrisEvents& events){
    //Guideline scoring: a tetris is worth more than four singles by a wide margin, which is the
    //whole reason to build a well and wait.
    static const int line_score[5] = { 0, 100, 300, 500, 800 };
    if (num_lines < 0 || num_lines > 4){
        return;
    }
    score += line_score[num_lines] * level;
    lines += num_lines;
    int new_level = 1 + (lines / 10);
    if (new_level > level){
        level = new_level;
        events.f_level_up = true;
    }
}

void Playfield::LockPiece(TetrisEvents& events){
    const TetrominoCell* cells = GetTetrominoCells(piece_type,piece_rotation);
    for (int i = 0; i < 4; i++){
        int cx = piece_x + cells[i].x;
        int cy = piece_y + cells[i].y;
        if (cx < 0 || cx >= TETRIS_BOARD_W || cy < 0 || cy >= TETRIS_BOARD_H){
            //A cell settling above the well is a "lock out" - the same loss as a block out, just
            //arrived at from the other direction.
            phase = TETRIS_PHASE_GAMEOVER;
            events.f_game_over = true;
            events.f_locked = true;
            return;
        }
        board[cy][cx] = (int8_t)piece_type;
    }
    pieces_placed++;
    events.f_locked = true;

    clearing_rows.clear();
    for (int y = 0; y < TETRIS_BOARD_H; y++){
        bool f_complete = true;
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            if (board[y][x] < 0){
                f_complete = false;
                break;
            }
        }
        if (f_complete){
            clearing_rows.push_back(y);
        }
    }

    if (!clearing_rows.empty()){
        events.lines_cleared = (int)clearing_rows.size();
        AwardLineScore((int)clearing_rows.size(),events);
        phase = TETRIS_PHASE_CLEARING;
        phase_ticks = TETRIS_CLEAR_FLASH_TICKS;
        return;
    }
    phase = TETRIS_PHASE_SPAWN;
    phase_ticks = TETRIS_SPAWN_DELAY_TICKS;
}

void Playfield::CollapseClearedRows(){
    //Copy down, skipping the cleared rows. Walking from the floor up with a separate write cursor
    //handles any number of cleared rows, adjacent or not, in one pass.
    int write_y = 0;
    for (int read_y = 0; read_y < TETRIS_BOARD_H; read_y++){
        bool f_cleared = false;
        for (size_t i = 0; i < clearing_rows.size(); i++){
            if (clearing_rows[i] == read_y){
                f_cleared = true;
                break;
            }
        }
        if (f_cleared){
            continue;
        }
        if (write_y != read_y){
            for (int x = 0; x < TETRIS_BOARD_W; x++){
                board[write_y][x] = board[read_y][x];
            }
        }
        write_y++;
    }
    for (; write_y < TETRIS_BOARD_H; write_y++){
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            board[write_y][x] = -1;
        }
    }
    clearing_rows.clear();
}

void Playfield::UpdateFalling(const TetrisInput& input, TetrisEvents& events){
    //Hold first: swapping the piece invalidates everything else this tick would have done to it.
    if (input.f_hold && !f_hold_used){
        int incoming = (hold_type >= 0) ? hold_type : DrawNextPiece();
        hold_type = piece_type;
        SpawnPiece(incoming,events);
        //SpawnPiece clears f_hold_used (a fresh piece may hold again); this piece already has.
        f_hold_used = true;
        events.f_held = true;
        return;
    }

    if (input.f_move_left && TryMove(-1,0)){
        events.f_moved = true;
    }
    if (input.f_move_right && TryMove(+1,0)){
        events.f_moved = true;
    }
    if (input.f_rotate_cw){
        TryRotate(true,events);
    }
    if (input.f_rotate_ccw){
        TryRotate(false,events);
    }

    //Any successful move or rotation while the piece is resting on something pushes the lock
    //delay back - that is what makes last-moment slides and spins possible. Capped, or a piece
    //could be walked along the floor indefinitely.
    if ((events.f_moved || events.f_rotated) && lock_ticks > 0 && lock_resets < TETRIS_MAX_LOCK_RESETS){
        lock_ticks = 0;
        lock_resets++;
    }

    if (input.f_hard_drop){
        int landed_y = GetGhostY();
        int cells = piece_y - landed_y;
        piece_y = landed_y;
        score += cells * 2;                 //guideline: 2 points a cell for a hard drop
        events.f_hard_dropped = true;
        events.hard_drop_cells = cells;
        LockPiece(events);                  //a hard drop sets instantly, with no lock delay
        return;
    }

    //Gravity. A held soft drop just runs the same counter faster and pays a point a cell.
    int interval = GravityTicksForLevel(level);
    if (input.f_soft_drop){
        interval = max(1,interval / SOFT_DROP_DIVISOR);
    }
    if (gravity_ticks > interval){
        //A soft drop pressed mid-interval must take effect now, not after the slow interval that
        //was already counting down.
        gravity_ticks = interval;
    }
    gravity_ticks--;
    if (gravity_ticks <= 0){
        gravity_ticks = interval;
        if (TryMove(0,-1)){
            if (input.f_soft_drop){
                score += 1;
            }
            lock_ticks = 0;                 //it is falling freely again
        }
    }

    //Lock delay: the piece only sets after it has been unable to fall for a while, which is what
    //gives the player the moment at the bottom to slide it into place.
    if (!IsPositionLegal(piece_type,piece_rotation,piece_x,piece_y - 1)){
        lock_ticks++;
        if (lock_ticks >= TETRIS_LOCK_DELAY_TICKS){
            LockPiece(events);
        }
    }else{
        lock_ticks = 0;
    }
}

void Playfield::Tick(const TetrisInput& input, TetrisEvents& events){
    events = TetrisEvents();
    ticks_elapsed++;

    switch (phase){
        case TETRIS_PHASE_SPAWN:
            phase_ticks--;
            if (phase_ticks <= 0){
                SpawnPiece(DrawNextPiece(),events);
            }
            break;

        case TETRIS_PHASE_FALLING:
            UpdateFalling(input,events);
            break;

        case TETRIS_PHASE_CLEARING:
            phase_ticks--;
            if (phase_ticks <= 0){
                phase = TETRIS_PHASE_COLLAPSING;
                phase_ticks = TETRIS_COLLAPSE_TICKS;
            }
            break;

        case TETRIS_PHASE_COLLAPSING:
            phase_ticks--;
            if (phase_ticks <= 0){
                CollapseClearedRows();
                events.f_collapsed = true;
                phase = TETRIS_PHASE_SPAWN;
                phase_ticks = TETRIS_SPAWN_DELAY_TICKS;
            }
            break;

        case TETRIS_PHASE_GAMEOVER:
        default:
            //Nothing moves until something calls NewGame - which is a command from outside the
            //simulation (a key, the HUD button, an MCP tool), never something a tick decides.
            break;
    }
}

std::vector<std::string> Playfield::ToAsciiRows() const{
    //Top row first, because that is how the board is looked at.
    std::vector<std::string> rows;
    for (int y = TETRIS_BOARD_H - 1; y >= 0; y--){
        std::string row;
        for (int x = 0; x < TETRIS_BOARD_W; x++){
            row.push_back(board[y][x] >= 0 ? GetTetrominoName(board[y][x])[0] : '.');
        }
        rows.push_back(row);
    }
    if (phase == TETRIS_PHASE_FALLING){
        //The active piece goes in lowercase, so one glance separates what has settled from what
        //the player is still steering.
        const TetrominoCell* cells = GetTetrominoCells(piece_type,piece_rotation);
        char letter = (char)(GetTetrominoName(piece_type)[0] - 'A' + 'a');
        for (int i = 0; i < 4; i++){
            int cx = piece_x + cells[i].x;
            int cy = piece_y + cells[i].y;
            if (cx < 0 || cx >= TETRIS_BOARD_W || cy < 0 || cy >= TETRIS_BOARD_H){
                continue;
            }
            rows[TETRIS_BOARD_H - 1 - cy][cx] = letter;
        }
    }
    return rows;
}
