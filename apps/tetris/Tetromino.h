#ifndef _TETROMINO_H_
#define _TETROMINO_H_

#include <stdint.h>

/*
    The seven pieces and their rotation states, in the Super Rotation System (SRS) layout - the
    one every modern Tetris uses, and the one players' muscle memory expects. Nothing in here
    knows about the engine: it is a lookup table plus two accessors, so the rules can be reasoned
    about (and, later, tested) on their own.

    Coordinates are cell offsets from the BOTTOM-LEFT of the piece's bounding box, with y growing
    UP. That is deliberately the same direction the board array and the world both use, so a cell
    goes board -> world without a flip anywhere. The tables below are written out as ASCII art with
    row 0 at the TOP (which is how every SRS diagram is drawn) and flipped once, at load, by
    BuildTables.
*/

enum TetrominoType{
    TETROMINO_I = 0,
    TETROMINO_J,
    TETROMINO_L,
    TETROMINO_O,
    TETROMINO_S,
    TETROMINO_T,
    TETROMINO_Z,
    TETROMINO_COUNT
};

//One cell of a piece, relative to the bottom-left of its bounding box.
struct TetrominoCell{
    int8_t x = 0;
    int8_t y = 0;
};

//The four cells of `type` in rotation state `rotation` (0..3, 0 being spawn). Always four.
const TetrominoCell* GetTetrominoCells(int type, int rotation);

//Side of the piece's bounding box: 4 for I, 3 for everything else (O uses a 3-box with its cells
//parked in one corner, which is harmless because O's kick table is all zeroes and its four
//rotation states are identical).
int GetTetrominoBoxSize(int type);

//Highest occupied local y in the spawn rotation. The spawn code aligns THIS with the top row of
//the well, so a piece appears fully visible at the top rather than in a hidden buffer the player
//cannot see - see Playfield::SpawnPiece.
int GetTetrominoSpawnTopOffset(int type);

//SRS wall kicks: the offsets to try, in order, when rotating from `from_rotation` (0..3) in the
//given direction. The first one that leaves the piece in a legal position wins; if none does the
//rotation is refused. Returns a pointer to `count` offsets, the first of which is always (0,0)
//(the un-kicked rotation). I has its own table - that is what makes its wall kicks feel right.
const TetrominoCell* GetTetrominoKicks(int type, int from_rotation, bool f_clockwise, int& count);

//Human-readable name, for telemetry and the HUD. "I", "J", ...
const char* GetTetrominoName(int type);

#endif
