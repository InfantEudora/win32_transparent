#include "Tetromino.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Tetromino",DEBUG_ALL);

/*
    The shapes, written the way SRS diagrams are drawn: row 0 at the TOP, one string per row, one
    character per column, 'X' for an occupied cell. Kept as art rather than as a hand-written list
    of 112 coordinate pairs because the art can be checked against a reference picture by eye, and
    a transposed coordinate pair cannot.
*/
struct TetrominoShapeArt{
    int box = 3;
    const char* rows[4][4] = {};    //[rotation][row], row 0 = top. Unused entries are NULL.
};

static const TetrominoShapeArt shape_art[TETROMINO_COUNT] = {
    //I - the only 4x4 piece, and the only one with its own kick table.
    { 4, {
        { "....","XXXX","....","...." },
        { "..X.","..X.","..X.","..X." },
        { "....","....","XXXX","...." },
        { ".X..",".X..",".X..",".X.." },
    }},
    //J
    { 3, {
        { "X..","XXX","..." },
        { ".XX",".X.",".X." },
        { "...","XXX","..X" },
        { ".X.",".X.","XX." },
    }},
    //L
    { 3, {
        { "..X","XXX","..." },
        { ".X.",".X.",".XX" },
        { "...","XXX","X.." },
        { "XX.",".X.",".X." },
    }},
    //O - identical in all four states, so rotating it is a no-op by construction rather than by
    //a special case in the rotation code.
    { 3, {
        { ".XX",".XX","..." },
        { ".XX",".XX","..." },
        { ".XX",".XX","..." },
        { ".XX",".XX","..." },
    }},
    //S
    { 3, {
        { ".XX","XX.","..." },
        { ".X.",".XX","..X" },
        { "...",".XX","XX." },
        { "X..","XX.",".X." },
    }},
    //T
    { 3, {
        { ".X.","XXX","..." },
        { ".X.",".XX",".X." },
        { "...","XXX",".X." },
        { ".X.","XX.",".X." },
    }},
    //Z
    { 3, {
        { "XX.",".XX","..." },
        { "..X",".XX",".X." },
        { "...","XX.",".XX" },
        { ".X.","XX.","X.." },
    }},
};

static const char* shape_names[TETROMINO_COUNT] = { "I","J","L","O","S","T","Z" };

/*
    SRS kick tables. Indexed [from_rotation][direction], direction 0 = clockwise, 1 =
    counter-clockwise. Five offsets each; the first is always (0,0), so "no kick needed" costs the
    same code path as a kick. Signs are in this file's convention: +x right, +y UP. Published
    tables are usually written with +y up as well, so these are transcribed as-is.
*/
#define NUM_KICKS 5

static const TetrominoCell kicks_jlstz[4][2][NUM_KICKS] = {
    //from 0
    { { {0,0},{-1,0},{-1,+1},{0,-2},{-1,-2} },      //0 -> 1 (CW)
      { {0,0},{+1,0},{+1,+1},{0,-2},{+1,-2} } },    //0 -> 3 (CCW)
    //from 1
    { { {0,0},{+1,0},{+1,-1},{0,+2},{+1,+2} },      //1 -> 2 (CW)
      { {0,0},{+1,0},{+1,-1},{0,+2},{+1,+2} } },    //1 -> 0 (CCW)
    //from 2
    { { {0,0},{+1,0},{+1,+1},{0,-2},{+1,-2} },      //2 -> 3 (CW)
      { {0,0},{-1,0},{-1,+1},{0,-2},{-1,-2} } },    //2 -> 1 (CCW)
    //from 3
    { { {0,0},{-1,0},{-1,-1},{0,+2},{-1,+2} },      //3 -> 0 (CW)
      { {0,0},{-1,0},{-1,-1},{0,+2},{-1,+2} } },    //3 -> 2 (CCW)
};

static const TetrominoCell kicks_i[4][2][NUM_KICKS] = {
    //from 0
    { { {0,0},{-2,0},{+1,0},{-2,-1},{+1,+2} },      //0 -> 1
      { {0,0},{-1,0},{+2,0},{-1,+2},{+2,-1} } },    //0 -> 3
    //from 1
    { { {0,0},{-1,0},{+2,0},{-1,+2},{+2,-1} },      //1 -> 2
      { {0,0},{+2,0},{-1,0},{+2,+1},{-1,-2} } },    //1 -> 0
    //from 2
    { { {0,0},{+2,0},{-1,0},{+2,+1},{-1,-2} },      //2 -> 3
      { {0,0},{+1,0},{-2,0},{+1,-2},{-2,+1} } },    //2 -> 1
    //from 3
    { { {0,0},{+1,0},{-2,0},{+1,-2},{-2,+1} },      //3 -> 0
      { {0,0},{-2,0},{+1,0},{-2,-1},{+1,+2} } },    //3 -> 2
};

//O never kicks: its four states are the same cells, so the un-kicked rotation always succeeds.
static const TetrominoCell kicks_o[4][2][NUM_KICKS] = {
    { { {0,0},{0,0},{0,0},{0,0},{0,0} }, { {0,0},{0,0},{0,0},{0,0},{0,0} } },
    { { {0,0},{0,0},{0,0},{0,0},{0,0} }, { {0,0},{0,0},{0,0},{0,0},{0,0} } },
    { { {0,0},{0,0},{0,0},{0,0},{0,0} }, { {0,0},{0,0},{0,0},{0,0},{0,0} } },
    { { {0,0},{0,0},{0,0},{0,0},{0,0} }, { {0,0},{0,0},{0,0},{0,0},{0,0} } },
};

//The art above, flipped into bottom-left-origin cell offsets. Built once, before anything can
//read it - see the static TableBuilder below.
static TetrominoCell shape_cells[TETROMINO_COUNT][4][4];
static int shape_spawn_top[TETROMINO_COUNT] = {};

static void BuildTables(){
    for (int type = 0; type < TETROMINO_COUNT; type++){
        const TetrominoShapeArt& art = shape_art[type];
        for (int rotation = 0; rotation < 4; rotation++){
            int found = 0;
            for (int row = 0; row < art.box; row++){
                const char* text = art.rows[rotation][row];
                if (!text){
                    continue;
                }
                for (int col = 0; col < art.box && text[col]; col++){
                    if (text[col] != 'X'){
                        continue;
                    }
                    if (found >= 4){
                        //A shape with more than four cells is a typo in the art above, and one
                        //that would otherwise show up as a piece that silently loses a block.
                        debug->Err("Tetromino %s rotation %i has more than 4 cells\n",shape_names[type],rotation);
                        break;
                    }
                    shape_cells[type][rotation][found].x = (int8_t)col;
                    //Row 0 is the TOP of the art, and y grows up - this is the one flip.
                    shape_cells[type][rotation][found].y = (int8_t)(art.box - 1 - row);
                    found++;
                }
            }
            if (found != 4){
                debug->Err("Tetromino %s rotation %i has %i cells, expected 4\n",shape_names[type],rotation,found);
            }
        }
        int top = 0;
        for (int i = 0; i < 4; i++){
            if (shape_cells[type][0][i].y > top){
                top = shape_cells[type][0][i].y;
            }
        }
        shape_spawn_top[type] = top;
    }
}

//A file-scope object whose constructor fills the tables. Runs during static initialisation, so
//every accessor below can assume the tables are there without a per-call "are we built yet"
//check on the hot path - and without a function-local static, whose thread-safe-init guard would
//be paid on every lookup from the physics thread.
struct TetrominoTableBuilder{
    TetrominoTableBuilder(){ BuildTables(); }
};
static TetrominoTableBuilder table_builder;

const TetrominoCell* GetTetrominoCells(int type, int rotation){
    if (type < 0 || type >= TETROMINO_COUNT){
        type = TETROMINO_I;
    }
    return shape_cells[type][rotation & 3];
}

int GetTetrominoBoxSize(int type){
    if (type < 0 || type >= TETROMINO_COUNT){
        return 3;
    }
    return shape_art[type].box;
}

int GetTetrominoSpawnTopOffset(int type){
    if (type < 0 || type >= TETROMINO_COUNT){
        return 0;
    }
    return shape_spawn_top[type];
}

const TetrominoCell* GetTetrominoKicks(int type, int from_rotation, bool f_clockwise, int& count){
    count = NUM_KICKS;
    int direction = f_clockwise ? 0 : 1;
    from_rotation &= 3;
    if (type == TETROMINO_I){
        return kicks_i[from_rotation][direction];
    }
    if (type == TETROMINO_O){
        return kicks_o[from_rotation][direction];
    }
    return kicks_jlstz[from_rotation][direction];
}

const char* GetTetrominoName(int type){
    if (type < 0 || type >= TETROMINO_COUNT){
        return "?";
    }
    return shape_names[type];
}
