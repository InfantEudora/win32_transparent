/*
    The output stage every matrix kernel shares (llm_matvec_*.comp for one token,
    llm_matmat_*.comp for a batch): what happens to the finished sums of a subgroup's ROWS rows.
    The including kernel defines ROWS (even) and calls WriteRows from one lane.

    Five modes, chosen per dispatch by out_mode, each folding what used to be a separate
    dispatch into the one that produces the numbers:

      OUT_WRITE       y = W x
      OUT_ACCUMULATE  y += W x            the residual connection, for wo and w2
      OUT_SWIGLU      for w13, whose rows alternate w1 and w3 (LLMModel interleaves them at
                      load): rows 2i and 2i+1 become silu(a) * b, written to y[i]
      OUT_QKV         for qkv, the rows of wq, wk and wv one after another: query rows go to y,
                      key rows to the key cache and value rows to the value cache, and the query
                      and key pairs are rotated by RoPE on the way. A pair (2i, 2i+1) is always
                      within one subgroup's ROWS rows, because ROWS is even and every head and
                      every block starts at a multiple of it.
      OUT_BLOCK       OUT_ACCUMULATE for the block analysis: y += gain * W x, and token
                      capture_t's W x - the block's update - kept in the block's slot, where
                      llm_blocks.comp reads it once the token is sampled. LLMModel binds the
                      slot (gain first, then the update) at 8, and uses this mode only while the
                      analysis records or a gain is not 1. A gain of 1 adds exactly what
                      OUT_ACCUMULATE adds: 1 * v is v, also inside an fma.

    Unused bindings are still bound (LLMModel binds y everywhere), so no mode reads an empty
    binding point.
*/

#define OUT_WRITE 0
#define OUT_ACCUMULATE 1
#define OUT_SWIGLU 2
#define OUT_QKV 3
#define OUT_BLOCK 4

#define SLOT_HEADER 64      //floats before a block slot's update; BLOCK_SLOT_HEADER in LLMModel.h

layout(std430, binding = 3) buffer Output { float y[]; };           //y, or the query in OUT_QKV
layout(std430, binding = 4) buffer KeyCache { float key_cache[]; };
layout(std430, binding = 5) buffer ValueCache { float value_cache[]; };
layout(std430, binding = 6) readonly buffer RopeTable { vec2 rope[]; };   //(pos, head_size/2) of (cos, sin)
layout(std430, binding = 8) buffer BlockSlot { float slot[]; };     //OUT_BLOCK: [0] the gain, from SLOT_HEADER the update

uniform int out_mode;
uniform int y_offset;       //floats: token 0's first output
uniform int y_stride;       //floats from one token's outputs to the next's
uniform int q_dim;          //OUT_QKV: rows of wq, then kv_dim rows of wk, then kv_dim of wv
uniform int kv_dim;
uniform int head_size;
uniform int k_offset;       //OUT_QKV: token 0's row in each cache, in floats; tokens follow kv_dim apart
uniform int v_offset;
uniform int pos0;           //OUT_QKV: token 0's position
uniform int capture_t;      //OUT_BLOCK: the token (of the dispatch) whose update is kept, the last one

//v holds the sums of rows first_row..first_row+ROWS-1 for token t (of the dispatch), d the row count.
void WriteRows(int first_row, float v[ROWS], int t, int d){
    if (out_mode == OUT_BLOCK){
        float gain = slot[0];
        for (int r = 0;r < ROWS;r++){
            int row = first_row + r;
            if (row < d){
                int i = y_offset + t * y_stride + row;
                y[i] = y[i] + gain * v[r];
                if (t == capture_t){
                    slot[SLOT_HEADER + row] = v[r];
                }
            }
        }
    }else if (out_mode == OUT_WRITE || out_mode == OUT_ACCUMULATE){
        for (int r = 0;r < ROWS;r++){
            int row = first_row + r;
            if (row < d){
                int i = y_offset + t * y_stride + row;
                y[i] = out_mode == OUT_ACCUMULATE ? y[i] + v[r] : v[r];
            }
        }
    }else if (out_mode == OUT_SWIGLU){
        for (int r = 0;r < ROWS;r += 2){
            int row = first_row + r;
            if (row < d){
                float a = v[r];
                y[y_offset + t * y_stride + row / 2] = a / (1.0 + exp(-a)) * v[r + 1];
            }
        }
    }else{
        int pos = pos0 + t;
        for (int r = 0;r < ROWS;r += 2){
            int row = first_row + r;
            if (row >= d){
                break;
            }
            float a = v[r];
            float b = v[r + 1];
            if (row < q_dim + kv_dim){
                //RoPE, as runq: the pair's index within its head picks the angle
                int head_dim = (row < q_dim ? row : row - q_dim) % head_size;
                vec2 cs = rope[pos * (head_size / 2) + head_dim / 2];
                float ra = a * cs.x - b * cs.y;
                float rb = a * cs.y + b * cs.x;
                a = ra;
                b = rb;
            }
            if (row < q_dim){
                int i = y_offset + t * y_stride + row;
                y[i] = a;
                y[i + 1] = b;
            }else if (row < q_dim + kv_dim){
                int i = k_offset + t * kv_dim + row - q_dim;
                key_cache[i] = a;
                key_cache[i + 1] = b;
            }else{
                int i = v_offset + t * kv_dim + row - q_dim - kv_dim;
                value_cache[i] = a;
                value_cache[i + 1] = b;
            }
        }
    }
}
