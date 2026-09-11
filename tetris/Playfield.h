#ifndef _PLAYFIELD_H_
#define _PLAYFIELD_H_

#include <stdint.h>
#include <vector>
#include <string>
#include "Tetromino.h"

/*
    The rules of Tetris, and nothing else. No engine types appear in this header on purpose: the
    board is a plain array and every duration is a count of SIMULATION TICKS, so the whole game
    is a pure function of (previous state, this tick's input). That is what makes it replayable,
    and it is also what lets it be reasoned about without a window on the screen.

    The Application owns the other half: turning held keys into the one-shot actions below
    (DAS/ARR), and turning the board array into cubes.
*/

#define TETRIS_BOARD_W  10
#define TETRIS_BOARD_H  20

//Tuning, all in ticks. The app runs at 60 ticks/second (Application::SetPhysicsTPS), which is
//also the unit the classic gravity table below is denominated in, so these read as frames.
#define TETRIS_LOCK_DELAY_TICKS     30  //half a second on the floor before it sets
#define TETRIS_MAX_LOCK_RESETS      15  //...but only 15 moves may push that back, or a piece
                                        //could be slid along the floor forever
#define TETRIS_CLEAR_FLASH_TICKS    18  //completed rows flash white before they go
#define TETRIS_COLLAPSE_TICKS       10  //...then everything above them slides down
#define TETRIS_SPAWN_DELAY_TICKS    6   //"ARE": the pause between one piece locking and the next
#define TETRIS_NEXT_QUEUE_SHOWN     3   //how many upcoming pieces the HUD previews

//What phase the game is in. Everything that takes time is a phase with a tick counter rather
//than a flag plus a timestamp, so "what is the game doing" has exactly one answer.
enum TetrisPhase{
    TETRIS_PHASE_SPAWN = 0,     //waiting out the spawn delay, then the next piece enters
    TETRIS_PHASE_FALLING,       //the player controls the active piece
    TETRIS_PHASE_CLEARING,      //completed rows are flashing; the board is frozen
    TETRIS_PHASE_COLLAPSING,    //the rows above the cleared ones are sliding down
    TETRIS_PHASE_GAMEOVER
};

//One tick's worth of intent. Each field is an ACTION, not a key: the app has already resolved
//held keys, auto-repeat and edges into "move one cell left this tick". Keeping the translation
//out here means the rules never ask how long something has been held.
struct TetrisInput{
    bool f_move_left = false;
    bool f_move_right = false;
    bool f_soft_drop = false;   //the only level-triggered one: it means "gravity is fast now"
    bool f_hard_drop = false;
    bool f_rotate_cw = false;
    bool f_rotate_ccw = false;
    bool f_hold = false;
};

//What happened during a tick, for the app to hang sound and effects off. A consequence of the
//simulation, never a command into it - see core/SimCommand.h on that distinction.
struct TetrisEvents{
    bool f_moved = false;
    bool f_rotated = false;
    bool f_rotation_refused = false;
    bool f_locked = false;
    bool f_held = false;
    bool f_hard_dropped = false;
    int  hard_drop_cells = 0;
    int  lines_cleared = 0;         //set on the tick the clear STARTS (the flash), not the collapse
    bool f_level_up = false;
    bool f_game_over = false;
    bool f_collapsed = false;       //the tick the cleared rows actually vanish from the board
};

/*
    A tiny xorshift32, private to the piece bag.

    core/RRandom.h is the engine's "reproducable random" and would have been the obvious choice,
    but it cannot be used for this: its noise buffer is a process-wide static shared by every
    RRandom instance, its state member is left uninitialised by the constructor, and SetSeed
    writes a `seed` field that nothing ever reads. So two RRandom instances are one stream, that
    stream starts at an indeterminate offset, and it cannot be seeded. See docs/tetris_findings.md.
*/
struct TetrisRandom{
    uint32_t state = 0x12345678u;
    void SetSeed(uint32_t seed){
        state = seed ? seed : 0x12345678u;   //xorshift is stuck at zero forever
    }
    uint32_t Next(){
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int GetInt(int imin, int imax){          //inclusive both ends
        if (imax <= imin){
            return imin;
        }
        return imin + (int)(Next() % (uint32_t)(imax - imin + 1));
    }
};

class Playfield{
public:
    Playfield();

    //Throws the whole game away and starts a new one from `seed`. The same seed gives the same
    //piece order, every run, on any machine - the bag draws from this object's own generator and
    //from nothing else.
    void NewGame(uint32_t seed);

    //One simulation tick. The ONLY way the state ever changes. Must be called exactly once per
    //physics tick that actually runs (never while paused) - see ApplicationTetris::RunSimulationTick.
    void Tick(const TetrisInput& input, TetrisEvents& events);

    //--- Reading the state --------------------------------------------------------------------
    //Settled cells: the TetrominoType that filled it, or -1 for empty. board[y][x], y counting UP
    //from the floor, x from the left wall. The active piece is NOT in here - it is written into
    //the board only when it locks, so "can it move" never has to exclude the piece from itself.
    int8_t board[TETRIS_BOARD_H][TETRIS_BOARD_W] = {};

    //The active piece. Valid only while phase == TETRIS_PHASE_FALLING.
    int piece_type = TETROMINO_I;
    int piece_rotation = 0;
    int piece_x = 0;                //board column of the bounding box's left edge
    int piece_y = 0;                //board row of the bounding box's bottom edge

    int phase = TETRIS_PHASE_SPAWN;
    int phase_ticks = 0;            //ticks remaining in a timed phase

    int score = 0;
    int lines = 0;
    int level = 1;
    int pieces_placed = 0;
    uint64_t ticks_elapsed = 0;     //ticks this game has run, for telemetry

    std::vector<int> next_queue;    //front() is the piece that spawns next
    int hold_type = -1;             //-1 when nothing is held
    bool f_hold_used = false;       //hold is once per piece, or it is an infinite undo

    //Rows completed and waiting to be flashed away. Filled when a piece locks, consumed by the
    //collapse. Empty at every other time.
    std::vector<int> clearing_rows;

    //Where the active piece would land if dropped right now, for the ghost. Returns the piece's
    //bounding-box bottom row; only meaningful while falling.
    int GetGhostY() const;

    //Does the piece fit here? Public because the ghost, the spawn test and the app's telemetry
    //all ask the same question.
    bool IsPositionLegal(int type, int rotation, int x, int y) const;

    //Rows the level's gravity takes to fall one cell, at 60 ticks/second.
    static int GravityTicksForLevel(int level);

    //The board plus the active piece, as TETRIS_BOARD_H strings of TETRIS_BOARD_W characters,
    //top row first ('.' empty, the piece letter otherwise, lowercase for the active piece). For
    //the MCP tools - a picture of the board that survives a JSON round trip.
    std::vector<std::string> ToAsciiRows() const;

private:
    TetrisRandom random;
    std::vector<int> bag;           //the current 7-bag, drawn down to empty then refilled

    int gravity_ticks = 0;          //ticks until the piece falls one more cell
    int lock_ticks = 0;             //ticks the piece has been resting on something
    int lock_resets = 0;

    void RefillBag();
    int  DrawNextPiece();           //pops the queue and tops it back up
    void SpawnPiece(int type, TetrisEvents& events);
    bool TryMove(int dx, int dy);
    bool TryRotate(bool f_clockwise, TetrisEvents& events);
    void LockPiece(TetrisEvents& events);
    void CollapseClearedRows();
    void AwardLineScore(int num_lines, TetrisEvents& events);
    void UpdateFalling(const TetrisInput& input, TetrisEvents& events);
};

#endif
