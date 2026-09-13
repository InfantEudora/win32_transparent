#ifndef _PLAYFIELD_H_
#define _PLAYFIELD_H_

#include <stdint.h>
#include <vector>
#include <string>
#include "Tetromino.h"
#include "RRandom.h"

/*
    The rules of Tetris, and nothing else. The board is a plain array and every duration is a
    count of SIMULATION TICKS, so the whole game is a pure function of (previous state, this
    tick's input). That is what makes it replayable, and it is also what lets it be reasoned
    about without a window on the screen.

    Nothing here reaches the renderer, the scene or GL. `RRandom` is the one engine header it
    includes, and it can be: it holds a byte buffer and depends on nothing but the vector types.
    (It used to hold a `Texture`, and so pull in `glad.h` - which is why this file carried its own
    copy of the same xorshift32 that RRandom::Generate uses. See core/RRandom.h.)

    The Application owns the other half: turning held keys into the one-shot actions below
    (DAS/ARR), and turning the board array into cubes.
*/

#define TETRIS_BOARD_W  10
#define TETRIS_BOARD_H  20

/*
    Ticks per second this game is simulated at. The app hands it to Application::SetPhysicsTPS -
    see ApplicationTetris::Init - rather than picking its own number.

    IT LIVES HERE, WITH THE RULES, AND THE DIRECTION MATTERS. The obvious instinct is the other
    way round: let the rules ask the engine what rate they are running at. That is backwards,
    because the rules OWN the rate - the gravity table in Playfield.cpp is the NES frame counts,
    so 60 is not a preference the engine grants, it is what makes that table mean what it says.
    Every other duration here (DAS, lock delay, the clear flash) is a count of these same ticks.
    So the rules declare it and the app applies it: one direction, one source of truth, nothing
    to drift.

    The rules also cannot ask, which is the same fact seen from the other side: nothing in this
    header may reach an engine type, so there is no Scene here to call GetPhysicsTimestep() on.
    That is a feature. A rules layer that is a pure function of (previous state, this tick's
    input) is what makes the game replayable, and a rate it fetched at runtime would be one more
    input to record.

    And it should not be passed in per tick either, tempting as `Tick(dt, ...)` looks: the engine
    deliberately keeps the timestep CONSTANT for the life of a run (Application::GetPhysicsTimestep;
    physics_time_factor scales how OFTEN ticks run and never how long one is) precisely so a
    recorded run replays. Handing the rules a per-tick dt reopens that door for nothing.

    `breakout/Field.h` does the same thing with BREAKOUT_TPS. See engine backlog item 51, which was
    declined for these reasons.
*/
#define TETRIS_TPS      60.0f

//Tuning, all in ticks. The app runs at 60 ticks/second (Application::SetPhysicsTPS), which is
//also the unit the classic gravity table below is denominated in, so these read as frames.
#define TETRIS_LOCK_DELAY_TICKS     30  //half a second on the floor before it sets
#define TETRIS_MAX_LOCK_RESETS      15  //...but only 15 moves may push that back, or a piece
                                        //could be slid along the floor forever
#define TETRIS_CLEAR_FLASH_TICKS    18  //completed rows flash white before they go
#define TETRIS_COLLAPSE_TICKS       10  //...then everything above them slides down
#define TETRIS_SPAWN_DELAY_TICKS    6   //"ARE": the pause between one piece locking and the next
#define TETRIS_NEXT_QUEUE_SHOWN     3   //how many upcoming pieces the HUD previews

/*
    Whether the placement that just locked was a T-spin, and which kind.

    The test is the guideline one and it has three parts: the piece is a T, the last thing that
    happened to it was a ROTATION (not a slide and not gravity), and at least three of the four
    corners of its 3x3 box are blocked. That last condition is what makes a T-spin a fact about
    the STACK rather than about the keyboard - you cannot spin a T into open air.

    MINI is the same thing with only one of the two corners on the side the T points at. It is
    the difference between wedging a T into a proper slot and merely twisting one into a notch,
    and it scores a quarter as much, so the distinction is worth the table it costs.
*/
#define TETRIS_SPIN_NONE    0
#define TETRIS_SPIN_MINI    1
#define TETRIS_SPIN_FULL    2

//How high the stack has to get before the app starts warning about it. Here with the rules
//rather than in the view because it is measured in board rows, and the board is the rules'.
#define TETRIS_WARN_ROWS    15
#define TETRIS_DANGER_ROWS  18

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

    //What the clear was WORTH, alongside what it was. The app announces this over the board, and
    //that is the whole reason these are events rather than something the view recomputes: the
    //bonuses below compose (a back-to-back tetris that is also a combo and also a perfect clear
    //is one number arrived at four ways), and a second implementation of that arithmetic in the
    //view would be a second implementation to get wrong.
    int  score_awarded = 0;         //points this clear scored, every bonus included
    int  combo = 0;                 //how many clears deep the run is; 0 on the first of a run
    bool f_back_to_back = false;    //this clear EXTENDED a back-to-back chain and was paid for it
    bool f_perfect_clear = false;   //it also left the board completely empty
    //TETRIS_SPIN_*. Set on the tick a piece locks, and set even when the spin cleared NOTHING -
    //a T-spin with no lines still scores, so it is still an event the app has to announce.
    int  spin = TETRIS_SPIN_NONE;
};

/*
    Bytes of noise the piece bag draws from. The bag is a Fisher-Yates shuffle of seven, so it
    spends six GetInt calls - 24 bytes - per seven pieces, and this covers roughly 19,000 pieces
    before the stream wraps and starts repeating (see core/RRandom.h). A long game is a couple of
    thousand, so the margin is deliberate and 64 KB is nothing.
*/
#define TETRIS_RANDOM_BYTES 65536

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

    /*
        The two pieces of state that make a good run worth more than the same clears scattered
        about. Both are part of the RULES rather than of the presentation, because both change
        the score - see AwardLineScore.

        `combo` counts the unbroken run of placements that cleared something: -1 between runs, 0
        on the first clear of a run, 1 on the second. That off-by-one is deliberate and it is why
        the field is not simply a count - the value IS the multiplier the bonus wants, so the
        first clear of a run pays no combo and nothing has to subtract one.

        `f_back_to_back` remembers that the last clear was a "difficult" one - a tetris, or any
        T-spin that cleared something. Both have to be set up several pieces in advance, which is
        what the bonus is paying for, and a difficult clear that follows another scores half
        again. It is what stops the best-scoring strategy being to farm singles forever.
    */
    int combo = -1;
    bool f_back_to_back = false;

    //The best score this session has seen, and whether the game in progress has passed it. Kept
    //with the rules because the rules are what produce a score - the app loads and saves it, and
    //NewGame deliberately does not touch it, which is the whole point of a best.
    int best_score = 0;
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

    /*
        How far through its lock delay the active piece is, 0 while it is falling freely and 1 on
        the tick it sets. The app draws this, and drawing it is not decoration: the lock delay is
        half a second of grace that the player is given no sign of, so a piece that looks settled
        can still be slid and a piece that looks the same is already gone. Exposed as a fraction
        rather than as the tick count so the view never has to know what the delay is.
    */
    float GetLockProgress() const;

    //Rows the stack reaches, counting from the floor: 0 for an empty board, TETRIS_BOARD_H when
    //the top row is occupied. The settled board only - the active piece is not in it.
    int GetStackHeight() const;

    //Does the piece fit here? Public because the ghost, the spawn test and the app's telemetry
    //all ask the same question.
    bool IsPositionLegal(int type, int rotation, int x, int y) const;

    //Rows the level's gravity takes to fall one cell, at 60 ticks/second.
    static int GravityTicksForLevel(int level);

    /*
        The scoring table, and the name a clear goes by. Both static and public because THE HUD
        PRINTS THEM, and that is the point: a player who cannot see that a tetris is worth eight
        times a single has no reason to build a well, and a game that never says what a combo is
        worth has a scoring system the player can only guess at. A second copy of these numbers
        in the UI would be a second copy to get wrong, so there is one table and the view reads
        it - the same direction the rest of this header takes (see TETRIS_TPS).

        `base` is before the level multiplier, the combo and the back-to-back bonus; the full
        arithmetic is in AwardLineScore and is the only thing that touches `score`.

        `num_lines` of 0 is legal and scores nothing UNLESS `spin` says otherwise - a T-spin that
        clears no rows is still worth 400, which is most of a triple, and is the reason a good
        player sets a T-slot up several pieces in advance.
    */
    static int LineClearBaseScore(int num_lines, bool f_perfect_clear, int spin = TETRIS_SPIN_NONE);
    static const char* LineClearName(int num_lines);
    //"", "T-SPIN MINI" or "T-SPIN", for whatever wants to say what happened.
    static const char* SpinName(int spin);

    //Points per cell for the two drops, so the HUD can print those too rather than repeat them.
    static int SoftDropCellScore(){ return 1; }
    static int HardDropCellScore(){ return 2; }

    //Flat points a combo pays per step, before the level multiplier, and how much a back-to-back
    //chain adds to the base as a percentage.
    static int ComboStepScore(){ return 50; }
    static int BackToBackBonusPercent(){ return 50; }

    //The board plus the active piece, as TETRIS_BOARD_H strings of TETRIS_BOARD_W characters,
    //top row first ('.' empty, the piece letter otherwise, lowercase for the active piece). For
    //the MCP tools - a picture of the board that survives a JSON round trip.
    std::vector<std::string> ToAsciiRows() const;

private:
    //The bag's own stream. Seeded per game by NewGame; drawn from nowhere but RefillBag, on
    //the simulation thread, which is what keeps the piece order reproducible.
    RRandom random;
    std::vector<int> bag;           //the current 7-bag, drawn down to empty then refilled

    int gravity_ticks = 0;          //ticks until the piece falls one more cell
    int lock_ticks = 0;             //ticks the piece has been resting on something
    int lock_resets = 0;

    /*
        The two facts a T-spin test needs besides the board, and neither can be recovered after
        the fact - which is why they are recorded as they happen rather than worked out at lock
        time.

        `f_rotated_last` is the guideline's "the last movement was a rotation". Any successful
        translation clears it, gravity included: a T that was spun into place and then fell one
        more row was not spun into the slot it ended up in. A hard drop of ZERO cells does not
        clear it, because that is the piece already resting where it was spun.

        `f_last_kick_was_final` says the rotation had to use the LAST entry of its SRS kick table
        - the big two-row one. A rotation that needed it went somewhere no smaller kick could
        reach, which is a real spin whatever the corners say. It is recorded as a bool rather than
        as the index because the index means nothing without the table's length, and the length
        lives in Tetromino.cpp where it belongs.
    */
    bool f_rotated_last = false;
    bool f_last_kick_was_final = false;

    void RefillBag();
    int  DrawNextPiece();           //pops the queue and tops it back up
    void SpawnPiece(int type, TetrisEvents& events);
    bool TryMove(int dx, int dy);
    bool TryRotate(bool f_clockwise, TetrisEvents& events);
    void LockPiece(TetrisEvents& events);
    void CollapseClearedRows();
    void AwardLineScore(int num_lines, bool f_perfect_clear, int spin, TetrisEvents& events);
    //Would the board be completely empty once `clearing_rows` are taken out? Asked while those
    //rows are still in place, because that is when the score is awarded.
    bool WouldBePerfectClear() const;
    //TETRIS_SPIN_* for the piece as it stands. Asked at the moment of locking and never after:
    //it depends on the piece's position and on what the last input did, both of which the next
    //spawn throws away.
    int DetectSpin() const;
    void UpdateFalling(const TetrisInput& input, TetrisEvents& events);
};

#endif
