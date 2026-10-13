#ifndef _LLM_MODEL_H_
#define _LLM_MODEL_H_

#include "glad.h"
#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <vector>

class Shader;

/*
    A quantized Llama-style model (Ministral 3, Mistral 7B) on the GPU, and its forward pass as
    a chain of compute dispatches. RENDER THREAD ONLY, every method - it is all GL.

    --- THE FILE ----------------------------------------------------------------------------
    The .bin format of the llama_c project's runq: a 256-byte header, the rmsnorm weights in
    fp32, then every matrix as its quantized values followed directly by one fp32 scale per
    group. Version 3 is 4-bit (Q4_0, groups of 32), version 2 is int8 (Q8_0); the token
    embedding may be 8-bit in a 4-bit file ("q40e8"), and with a shared classifier it doubles
    as the output layer. export_safetensors.py in llama_c writes it; runq.c reads it.

    --- ON THE GPU --------------------------------------------------------------------------
    Each matrix gets ONE buffer in the file's own format - values, then scales - so the shaders
    see the layout runq's AVX2 kernels see; they bind it twice, as packed values and as floats.
    One buffer per matrix because a shader storage block is capped (2 GB on NVIDIA) and the
    largest matrix, the 8B's embedding, is 0.6 GB, while the whole model is not.

    Two matrices are packed differently from the file, at load, and both to save dispatches:
      - qkv: wq, wk and wv one after another, row for row. One dispatch makes the query, the
        key and the value, puts the latter two in the cache and applies RoPE on the way.
      - w13: the rows of w1 and w3 interleaved (w1 row 0, w3 row 0, w1 row 1, ...), so the
        two rows SwiGLU combines come out of the same subgroup and leave as one value.
    llm_output.glsl has the details of both.

    Activations are fp32 throughout; runq quantizes them to int8 before every matmul. That is
    the one deliberate difference, it makes this a little MORE accurate than the reference, and
    it is why nothing here can be compared with runq bit for bit.

    --- ONE TOKEN, OR A BATCH ---------------------------------------------------------------
    Forward is the generation step: one token, seven dispatches per layer (rmsnorm, qkv,
    attention, wo, rmsnorm, w13, w2). It is limited by how fast the weights stream in, and by the
    ~10 us the GPU idles at every barrier between dependent dispatches (measured on an RTX 2070).
    ForwardBatch takes up to MAX_BATCH tokens at consecutive positions - a prompt - through the
    matrix-times-matrix kernels, which use each weight for 8 tokens at once.
*/

#define MAX_BATCH 32        //tokens per ForwardBatch; MAX_TOKENS in llm_embed.comp
#define BLOCK_CANDIDATES 8  //tokens whose logits the block analysis splits up; MAX_CANDIDATES in llm_blocks.comp
#define BLOCK_SLOT_HEADER 64//floats before a block slot's update (the gain is the first); SLOT_HEADER in the shaders
#define BLOCK_RECORDS_MAX 4096  //the analysis keeps this many tokens, dropping the oldest

struct LLMConfig{
    int dim = 0;            //transformer width
    int hidden_dim = 0;     //FFN width
    int n_layers = 0;
    int n_heads = 0;        //query heads
    int n_kv_heads = 0;     //key/value heads (grouped-query attention)
    int vocab_size = 0;
    int seq_len = 0;        //the longest context the model was exported for
    int head_size = 0;
    int weight_bits = 4;    //4 (version 3) or 8 (version 2)
    int embed_bits = 4;     //the token embedding's bits, 8 in a q40e8 file
    int group_size = 32;    //values per scale
    bool f_shared_classifier = false;
    float rope_theta = 10000.0f;
    float yarn_factor = 0.0f;       //0 = plain RoPE
    int yarn_orig_max = 0;
    float yarn_beta_fast = 32.0f, yarn_beta_slow = 1.0f;
    float rope_attn_factor = 1.0f;  //scales RoPE's cos and sin
};

//One quantized matrix: rows x cols values, in one buffer (or, for host-only descriptors of the
//file's matrices, in no buffer at all - see the checks).
struct LLMTensor{
    GLuint buffer = 0;
    int bits = 4;
    int rows = 0, cols = 0;
    size_t q_bytes = 0;             //the quantized values; the scales start right after
    const uint8_t* host = NULL;     //into the mapped file; NULL for a matrix packed at load
    int ScaleOffset() const { return (int)(q_bytes / 4); }  //in floats, from the buffer's start
};

/*
    A compute program plus the uniform locations it has been asked for, looked up once, and the
    value each uniform was last given: Set does nothing for a value the program already has.
    That, and giving each role of the matrix kernel a program of its own (see Matrix), is what
    keeps a dispatch from changing uniforms the GPU may still be using - see the note at
    LLMModel::Matrix.
*/
struct LLMKernel{
    Shader* shader = NULL;
    std::string file;
    std::map<std::string,GLint> locations;
    std::map<GLint,int> values;
    GLint Loc(const char* name);
    void Set(const char* name, int value);
    void SetArray(const char* name, const int* values, int count);
    void Forget(){ locations.clear(); values.clear(); }  //after a rebuild
};

//Where a matrix kernel's result goes: llm_output.glsl's modes.
enum LLMOutMode { OUT_WRITE = 0, OUT_ACCUMULATE = 1, OUT_SWIGLU = 2, OUT_QKV = 3, OUT_BLOCK = 4 };
struct LLMOut{
    LLMOutMode mode = OUT_WRITE;
    GLuint y = 0;
    int y_offset = 0;       //floats
    int y_stride = 0;       //floats between tokens
    int context = 0;        //OUT_QKV: whose cache
    int layer = 0;          //OUT_QKV
    int pos0 = 0;           //OUT_QKV: position of the first token
    int block = -1;         //OUT_BLOCK: whose slot, see BlockIndex
};

/*
    What the block analysis found for one token: how every attention and FFN block changed the
    residual stream on the way to the next token, and how much of the final logits each one wrote.
    Blocks are numbered as they run, attention of layer l at 2l, its FFN at 2l + 1.

    Everything per block is as the block computed it, BEFORE its gain: a switched-off block still
    shows what it would have added. Multiply rel_update and share by gain for what it did add
    (cos_distance is already the one with the gain - it does not scale linearly).
*/
struct LLMBlockRecord{
    int context = 0;
    int pos = 0;                //of the token the forward pass took in
    int token = 0;              //that token
    int chosen = 0;             //the token sampled from its logits: the first candidate
    std::vector<float> gain;            //(blocks) as they were for this token
    std::vector<float> rel_update;      //(blocks) |dx| / |x|, runq's rel_update
    std::vector<float> cos_distance;    //(blocks) 1 - cos(x, x + gain dx), runq's cos_distance
    std::vector<int> candidates;        //chosen first, then the highest other logits
    std::vector<float> logit, prob;     //(candidates) as the forward pass gave them
    /*
        (blocks + 1) x candidates, block-major: block b's direct share of candidate c's logit at
        b * candidates + c, before its gain; the embedding's share is the last row. With the gains
        applied the shares add up to the logit - check is how far off that sum is, at most.
    */
    std::vector<float> share;
    float check = 0.0f;
    float rebuild_error = 0.0f; //|x_final - (x0 + sum gain dx)| / |x_final|: the slots against the real stream
    //What recording this token cost: in all, from the call to the record (the wait for the GPU
    //included, which on a GPU shared with another busy process is mostly waiting for its turn),
    //and of that the CPU's own work and llm_blocks.comp's GPU time (0 while profiling).
    double ms = 0.0, cpu_ms = 0.0, gpu_ms = 0.0;
};

class LLMModel{
public:
    ~LLMModel();

    /*
        Maps the file, uploads every tensor, builds the kernels, and makes n_contexts KV caches of
        context_len positions each (0 = the model's seq_len). On failure returns false with the
        reason in error, and leaves nothing allocated.

        A context is one conversation's memory: a duet runs two characters on the same weights,
        each with a cache of its own, as runq's duet does with two RunStates. The caches are the
        part that grows with the context - the 8B's is 1.1 GB at 4096 positions - so a duet
        that needs a thousand positions per character is better served by two short ones.
    */
    bool Load(const std::string& path, int context_len, int n_contexts, std::string& error);
    void Unload();
    bool IsLoaded() const { return f_loaded; }

    //Rebuilds every kernel from its .comp - the edit-and-rerun loop. compile_log collects every
    //kernel's log; returns whether all of them built.
    bool ReloadKernels(std::string& compile_log);

    /*
        One token at one position of one context: runs the whole network and returns the
        logits, copied back into host memory (vocab_size floats, valid until the next call).
        Positions must be fed in order from 0, as with any KV cache, and stay below context_len.
    */
    const float* Forward(int token, int pos, int context = 0);
    /*
        nb (<= MAX_BATCH) tokens at positions pos0..pos0+nb-1: the same result as nb calls of
        Forward, for a fraction of the time. Returns the last token's logits, or with all_logits
        every token's (nb rows of vocab_size floats).
    */
    const float* ForwardBatch(const int* tokens, int nb, int pos0, int context, bool all_logits);
    //Any number of tokens, in batches; returns the last one's logits.
    const float* ForwardTokens(const int* tokens, int n, int pos0, int context = 0);
    int context_len = 0;
    int n_contexts = 0;

    /*
        One matrix's kernel against the CPU: W x for nb random inputs (nb 1 uses the one-token
        kernel, more the batch kernel), the CPU decoding the weights straight from the file the
        way runq does. Returns the largest absolute difference and the largest absolute value of
        the CPU results, so the difference can be judged relative to the size of the numbers.
        name: qkv w13 wo w2 (per layer) or wcls.
    */
    bool TestMatrix(const std::string& name, int layer, int nb, unsigned seed, double& max_diff, double& max_ref, std::string& error);

    /*
        Profiling: with f_profile set, every dispatch is wrapped in a GL_TIME_ELAPSED query and
        the GPU times are added up per label ("matvec w13", "attention", ...), plus the CPU time
        spent issuing the calls and waiting for the logits. The queries serialise the GPU a
        little, so a profiled token is slower than an unprofiled one; the proportions are what
        it is for.
    */
    bool f_profile = false;
    /*
        Fold the one-token path's rmsnorms into the matrix kernels (true) or run them as
        dispatches of their own (false, the default). Folding saves 52 dispatches per token, but
        every workgroup then works out the norm from all of x, and with w13's 4608 workgroups that
        re-reading costs more than the dispatches did: 88-95 against 102 tok/s on the 3B
        (2026-10-13). Kept, because it pays off once the norm comes from partial sums the
        previous kernel leaves behind instead of from x.
    */
    bool f_fold_norm = false;
    std::map<std::string,double> profile_gpu_ms;    //summed over the profiled calls
    double profile_issue_ms = 0.0;                  //CPU: from the call's start to the readback
    double profile_readback_ms = 0.0;               //CPU: inside glGetNamedBufferSubData
    int profile_calls = 0;
    void ResetProfile();

    /*
        --- THE BLOCK ANALYSIS ---------------------------------------------------------------
        runq's -o statistics and -x ablation, per token and while the model talks.

        With f_block_stats set, every forward pass keeps each block's update to the residual
        stream (for a batch, its last token's), and RecordBlocks - called with the token sampled
        from that pass's logits - turns them into an LLMBlockRecord: one small dispatch
        (llm_blocks.comp) and a readback of a few hundred floats. The updates are kept in the
        same dispatch that adds them to the stream (OUT_BLOCK), so a recorded forward pass runs
        the same kernels as any other.

        A block's gain scales its update before it joins the stream: 1 as trained, 0 switched
        off, which is runq's ablation (-x a12, f3). Gains act from the next token on. Tokens
        already in a KV cache keep the keys and values the old gains made, so a block switched
        off mid-conversation still reaches later tokens through them; feed the conversation
        again for the effect on everything.

        Neither costs anything while unused: with recording off and every gain 1 the residual
        connections run in OUT_ACCUMULATE, exactly as before.
    */
    bool f_block_stats = false;
    int Blocks() const { return 2 * config.n_layers; }
    static int BlockIndex(int layer, bool f_ffn){ return 2 * layer + (f_ffn ? 1 : 0); }
    void SetBlockGain(int block, float gain);
    float BlockGain(int block) const { return block >= 0 && block < (int)block_gains.size() ? block_gains[block] : 1.0f; }
    bool AnyBlockGain() const { return f_gains_active; }    //is any gain not 1
    /*
        After sampling chosen from the logits of the last Forward or ForwardBatch: the record of
        that forward pass, appended to block_records. Does nothing (false) unless that pass was
        recorded and has not been recorded since - so it can be called after every sample.
    */
    bool RecordBlocks(int chosen);
    std::deque<LLMBlockRecord> block_records;   //the last BLOCK_RECORDS_MAX, oldest first
    long block_records_total = 0;               //ever recorded, so a viewer sees new ones arrive

    LLMConfig config;
    std::string path;
    size_t file_size = 0;
    size_t gpu_bytes = 0;           //everything this model allocated on the GPU
    double load_seconds = 0.0;

private:
    bool f_loaded = false;

    //The mapped file. Kept for the CPU reference checks; nothing on the GPU path reads it after
    //the upload.
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = NULL;
    const uint8_t* mapped = NULL;

    //--- weights ---
    GLuint norms = 0;               //rms_att (layers*dim), rms_ffn (layers*dim), rms_final (dim)
    LLMTensor tok_embedding;        //(vocab, dim)
    std::vector<LLMTensor> qkv, wo, w13, w2;    //on the GPU, see the header comment
    LLMTensor wcls;                 //the classifier, or a copy of tok_embedding's description when shared
    //The file's own matrices, host-only, for the checks: what qkv and w13 were packed from.
    std::vector<LLMTensor> file_wq, file_wk, file_wv, file_w1, file_w3;
    GLuint rope_table = 0;          //(context_len, head_size/2) vec2(cos, sin), see BuildRopeTable

    //--- activations, fp32, MAX_BATCH rows each ---
    GLuint x = 0;                   //the residual stream (dim)
    GLuint xb = 0;                  //normalised input, then the attention's output (max(dim, q_dim))
    GLuint q = 0;                   //queries (q_dim)
    GLuint hb = 0;                  //the FFN's hidden values after SwiGLU (hidden_dim)
    GLuint att = 0;                 //attention scores (n_heads x MAX_BATCH x context_len)
    GLuint logits = 0;              //(vocab_size)
    int xb_dim = 0;
    std::vector<GLuint> key_caches, value_caches;  //per context: (layers, context_len, kv_dim)
    std::vector<float> host_logits;

    //--- the block analysis ---
    GLuint block_slots = 0;         //a slot per block: the gain, then (from BLOCK_SLOT_HEADER) its update
    int slot_floats = 0;            //per slot, padded so every slot starts at a bindable offset
    GLuint block_stats = 0;         //llm_blocks.comp's output, (blocks + 2) x (3 + BLOCK_CANDIDATES)
    GLuint block_timer = 0;         //GL_TIME_ELAPSED around it; made on first use and kept, like the kernels
    std::vector<float> block_gains; //host copy of the slots' gains
    bool f_gains_active = false;
    //The forward pass whose updates the slots hold, until RecordBlocks has used it.
    bool f_capture_pending = false;
    int capture_context = 0, capture_pos = 0, capture_token = 0;
    int capture_x_row = 0;          //its row of x: the last of a batch
    int capture_logits_row = 0;     //and of the logits, which hold only the last row unless all_logits
    bool BlockMode() const { return f_block_stats || f_gains_active; }
    //wo and w2's output: the residual connection, through block's slot when BlockMode.
    LLMOut ResidualOut(int block, int stride);
    void NoteCapture(int context, int pos, int token, int x_row, int logits_row);

    //--- kernels ---
    LLMKernel k_embed, k_rmsnorm, k_matvec_q4, k_matvec_q8, k_matmat_q4, k_matmat_q8, k_attention, k_blocks;
    //The one-token matrix kernel once more per role (label), built on first use.
    std::map<std::string,LLMKernel> role_kernels;
    bool BuildKernel(LLMKernel& k, const char* file, std::string& compile_log);

    GLuint CreateBuffer(size_t bytes, const void* data);
    bool MapTensor(LLMTensor& t, const uint8_t*& ptr, int rows, int cols, int bits);
    void UploadTensor(LLMTensor& t);
    //A matrix made of the rows of others: concatenated (interleave false) or taken in turn.
    LLMTensor PackRows(const std::vector<const LLMTensor*>& parts, bool interleave);
    void BuildRopeTable();
    bool BuildKernels(std::string& compile_log);

    //glDispatchCompute, timed under label when profiling.
    void Dispatch(const char* label, GLuint groups_x, GLuint groups_y = 1);
    std::vector<GLuint> query_pool;
    std::vector<std::pair<const char*,GLuint>> pending_queries;
    void CollectProfile(std::chrono::steady_clock::time_point issue_start, std::chrono::steady_clock::time_point readback_start);

    void Embed(const int* tokens, int nb);
    void RmsNorm(int nb, GLuint x_buf, int x_offset, int x_stride, int weight_offset, GLuint out_buf, int out_offset, int out_stride);
    /*
        The matrix w times nb inputs, rows of x_buf from x_offset, x_stride floats apart; nb 1
        runs the one-token kernel, which can normalise its input (norm_offset into the norm
        weights, or -1), more runs the batch kernel, whose input must be normalised already.
    */
    void Matrix(const char* label, const LLMTensor& w, int nb, GLuint x_buf, int x_offset, int x_stride, int norm_offset, const LLMOut& out);
    void Attention(int nb, int pos0, int context, int layer);
    const float* ReadLogits(int rows, std::chrono::steady_clock::time_point issue_start);
};

#endif
