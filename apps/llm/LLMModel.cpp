#include "LLMModel.h"
#include "Shader.h"
#include "Debug.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <random>

static Debugger* debug = new Debugger("LLMModel",DEBUG_ALL);

//Rows per workgroup of the matrix kernels: ROWS in llm_matvec_*.comp (whose KSPLIT subgroups
//share them), ROWS * SUBGROUPS in llm_matmat_*.comp. Change one, change the other.
#define MATVEC_ROWS 4
#define MATMAT_ROWS 8
//Tokens per workgroup of the batch kernels: TOK in llm_matmat_*.comp.
#define MATMAT_TOKENS 8

typedef std::chrono::steady_clock Clock;

/*
    glBindBufferRange, loaded here because core's glad (core/glad.cpp) does not load it. A
    STAND-IN: it belongs in core next to glBindBufferBase, and this goes once it is there. Used
    to bind one block's slot (see Matrix); loaded by Load, with the context current.
*/
typedef void (APIENTRYP LLMBindBufferRangeProc)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
static LLMBindBufferRangeProc BindBufferRange = NULL;

//--- kernels ------------------------------------------------------------------------------------

GLint LLMKernel::Loc(const char* name){
    auto it = locations.find(name);
    if (it != locations.end()){
        return it->second;
    }
    GLint loc = glGetUniformLocation(shader->progid,name);
    if (loc < 0){
        //A uniform the compiler optimised away, or a typo. Said once, then remembered, so a
        //kernel that legitimately ignores one does not spam the log every dispatch.
        debug->Warn("%s: no uniform \"%s\"\n",shader->fname.c_str(),name);
    }
    locations[name] = loc;
    return loc;
}

void LLMKernel::Set(const char* name, int value){
    GLint loc = Loc(name);
    auto it = values.find(loc);
    if (it != values.end() && it->second == value){
        return;
    }
    values[loc] = value;
    glProgramUniform1i(shader->progid,loc,value);
}

void LLMKernel::SetArray(const char* name, const int* values, int count){
    glProgramUniform1iv(shader->progid,Loc(name),count,values);
}

//--- loading ------------------------------------------------------------------------------------

LLMModel::~LLMModel(){
    Unload();
}

GLuint LLMModel::CreateBuffer(size_t bytes, const void* data){
    GLuint buffer = 0;
    glCreateBuffers(1,&buffer);
    //GL_DYNAMIC_STORAGE_BIT on every buffer, weights included: glNamedBufferSubData needs it,
    //and costs nothing for a buffer that is never updated.
    glNamedBufferStorage(buffer,bytes,data,GL_DYNAMIC_STORAGE_BIT);
    gpu_bytes += bytes;
    return buffer;
}

//Describes the matrix at ptr in the file and moves ptr past it. No GPU work.
bool LLMModel::MapTensor(LLMTensor& t, const uint8_t*& ptr, int rows, int cols, int bits){
    t.rows = rows;
    t.cols = cols;
    t.bits = bits;
    size_t count = (size_t)rows * cols;
    t.q_bytes = count * bits / 8;
    size_t bytes = t.q_bytes + count / config.group_size * sizeof(float);
    if (ptr + bytes > mapped + file_size){
        return false;
    }
    t.host = ptr;
    ptr += bytes;
    return true;
}

void LLMModel::UploadTensor(LLMTensor& t){
    size_t count = (size_t)t.rows * t.cols;
    t.buffer = CreateBuffer(t.q_bytes + count / config.group_size * sizeof(float),t.host);
}

LLMTensor LLMModel::PackRows(const std::vector<const LLMTensor*>& parts, bool interleave){
    const LLMTensor& first = *parts[0];
    size_t row_bytes = (size_t)first.cols * first.bits / 8;
    size_t row_scales = first.cols / config.group_size;
    LLMTensor t;
    t.bits = first.bits;
    t.cols = first.cols;
    for (const LLMTensor* p:parts){
        t.rows += p->rows;
    }
    t.q_bytes = (size_t)t.rows * row_bytes;
    std::vector<uint8_t> staging(t.q_bytes + (size_t)t.rows * row_scales * sizeof(float));
    uint8_t* values = staging.data();
    float* scales = (float*)(staging.data() + t.q_bytes);
    auto copy_row = [&](const LLMTensor* p, int r){
        memcpy(values,p->host + r * row_bytes,row_bytes);
        memcpy(scales,(const float*)(p->host + p->q_bytes) + r * row_scales,row_scales * sizeof(float));
        values += row_bytes;
        scales += row_scales;
    };
    if (interleave){
        for (int r = 0;r < first.rows;r++){
            for (const LLMTensor* p:parts){
                copy_row(p,r);
            }
        }
    }else{
        for (const LLMTensor* p:parts){
            for (int r = 0;r < p->rows;r++){
                copy_row(p,r);
            }
        }
    }
    t.buffer = CreateBuffer(staging.size(),staging.data());
    return t;
}

/*
    RoPE's cos and sin for every position, computed on the CPU once.

    On the CPU rather than in the shader because of the size of the angle: position times
    frequency reaches thousands of radians, and a GPU's fast cos/sin lose accuracy badly there.
    runq computes cosf(pos * freq) on the CPU per token; this table holds exactly those values,
    so the one thing that would otherwise drift with the context length does not.

    The frequencies are runq's rope_frequencies: plain RoPE, or YaRN as in HF transformers.
*/
void LLMModel::BuildRopeTable(void){
    int half = config.head_size / 2;
    std::vector<float> freq(half);
    for (int i = 0;i < half;i++){
        freq[i] = 1.0f / powf(config.rope_theta,(2 * i) / (float)config.head_size);
    }
    if (config.yarn_factor > 0.0f){
        double two_pi = 2.0 * 3.14159265358979323846;
        double log_base = 2.0 * log(config.rope_theta);
        float low = floor(config.head_size * log(config.yarn_orig_max / (config.yarn_beta_fast * two_pi)) / log_base);
        float high = ceil(config.head_size * log(config.yarn_orig_max / (config.yarn_beta_slow * two_pi)) / log_base);
        if (low < 0){ low = 0; }
        if (high > config.head_size - 1){ high = config.head_size - 1; }
        if (low == high){ high += 0.001f; }
        for (int i = 0;i < half;i++){
            float ramp = (i - low) / (high - low);
            ramp = ramp < 0.0f ? 0.0f : ramp > 1.0f ? 1.0f : ramp;
            freq[i] = freq[i] / config.yarn_factor * ramp + freq[i] * (1.0f - ramp);
        }
    }
    std::vector<float> table((size_t)context_len * half * 2);
    for (int pos = 0;pos < context_len;pos++){
        for (int i = 0;i < half;i++){
            float val = pos * freq[i];
            table[((size_t)pos * half + i) * 2 + 0] = cosf(val) * config.rope_attn_factor;
            table[((size_t)pos * half + i) * 2 + 1] = sinf(val) * config.rope_attn_factor;
        }
    }
    rope_table = CreateBuffer(table.size() * sizeof(float),table.data());
}

bool LLMModel::BuildKernel(LLMKernel& k, const char* file, std::string& compile_log){
    bool f_built;
    if (!k.shader){
        k.shader = new Shader();
        //A kernel that does not compile is the normal state of a shader being written, not a
        //reason to take the app down - see Shader::f_fatal_on_error.
        k.shader->f_fatal_on_error = false;
        k.file = file;
        f_built = k.shader->BuildCompute(file);
    }else{
        f_built = k.shader->Reload();
    }
    //A rebuilt program may have its uniforms at other locations, and has none of the old values.
    k.Forget();
    if (!k.shader->compile_log.empty() || !f_built){
        compile_log += k.file + (f_built ? ": built\n" : ": FAILED\n") + k.shader->compile_log + "\n";
    }
    return f_built;
}

bool LLMModel::BuildKernels(std::string& compile_log){
    struct { LLMKernel* kernel; const char* file; } list[] = {
        {&k_embed,"shaders/llm_embed.comp"},
        {&k_rmsnorm,"shaders/llm_rmsnorm.comp"},
        {&k_matvec_q4,"shaders/llm_matvec_q4.comp"},
        {&k_matvec_q8,"shaders/llm_matvec_q8.comp"},
        {&k_matmat_q4,"shaders/llm_matmat_q4.comp"},
        {&k_matmat_q8,"shaders/llm_matmat_q8.comp"},
        {&k_attention,"shaders/llm_attention.comp"},
        {&k_blocks,"shaders/llm_blocks.comp"},
    };
    bool f_ok = true;
    for (auto& entry:list){
        f_ok = BuildKernel(*entry.kernel,entry.file,compile_log) && f_ok;
    }
    for (auto& entry:role_kernels){
        f_ok = BuildKernel(entry.second,entry.second.file.c_str(),compile_log) && f_ok;
    }
    return f_ok;
}

bool LLMModel::ReloadKernels(std::string& compile_log){
    return BuildKernels(compile_log);
}

bool LLMModel::Load(const std::string& file_path, int context_len_, int n_contexts_, std::string& error){
    Unload();
    auto start = Clock::now();
    path = file_path;
    if (!BindBufferRange){
        BindBufferRange = (LLMBindBufferRangeProc)wglGetProcAddress("glBindBufferRange");
        if (!BindBufferRange){
            error = "glBindBufferRange did not load";
            return false;
        }
    }

    file_handle = CreateFileA(file_path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file_handle == INVALID_HANDLE_VALUE){
        error = "cannot open " + file_path;
        return false;
    }
    LARGE_INTEGER size;
    GetFileSizeEx(file_handle,&size);
    file_size = (size_t)size.QuadPart;
    mapping_handle = CreateFileMappingA(file_handle,NULL,PAGE_READONLY,0,0,NULL);
    mapped = mapping_handle ? (const uint8_t*)MapViewOfFile(mapping_handle,FILE_MAP_READ,0,0,0) : NULL;
    if (!mapped || file_size < 256){
        error = "cannot map " + file_path;
        Unload();
        return false;
    }

    /*
        The header, read field by field at the offsets runq's fread sequence leaves them at. Not a
        packed struct: export_safetensors.py writes the shared-classifier flag as ONE byte, so
        everything after it sits at an odd offset (group_size at 37), which a C struct would pad.
    */
    auto rd_i = [&](size_t offset){ int v; memcpy(&v,mapped + offset,4); return v; };
    auto rd_f = [&](size_t offset){ float v; memcpy(&v,mapped + offset,4); return v; };
    if ((uint32_t)rd_i(0) != 0x616b3432){
        error = "not a runq checkpoint (bad magic)";
        Unload();
        return false;
    }
    int version = rd_i(4);
    if (version != 2 && version != 3){
        error = "checkpoint version " + std::to_string(version) + ", need 2 (int8) or 3 (4-bit)";
        Unload();
        return false;
    }
    LLMConfig& c = config;
    c = LLMConfig();
    c.weight_bits = version == 3 ? 4 : 8;
    c.dim = rd_i(8); c.hidden_dim = rd_i(12); c.n_layers = rd_i(16); c.n_heads = rd_i(20);
    c.n_kv_heads = rd_i(24); c.vocab_size = rd_i(28); c.seq_len = rd_i(32);
    c.f_shared_classifier = mapped[36] != 0;
    c.group_size = rd_i(37);
    float theta = rd_f(41);
    int head_size = rd_i(45);
    int ebits = rd_i(49);
    float factor = rd_f(53);
    if (theta > 0.0f){ c.rope_theta = theta; }
    c.head_size = head_size > 0 ? head_size : c.dim / c.n_heads;
    c.embed_bits = ebits > 0 ? ebits : c.weight_bits;
    if (factor > 0.0f){
        c.yarn_factor = factor;
        c.yarn_orig_max = rd_i(57);
        c.yarn_beta_fast = rd_f(61);
        c.yarn_beta_slow = rd_f(65);
    }
    float attn_factor = rd_f(69);
    if (attn_factor > 0.0f){ c.rope_attn_factor = attn_factor; }
    if (c.group_size != 32 && c.group_size != 64){
        error = "group size " + std::to_string(c.group_size) + " - the kernels handle 32 and 64";
        Unload();
        return false;
    }
    //What the kernels' layout relies on: rows in fours (subgroups of ROWS = 4 rows), heads and
    //the query/key/value blocks starting at a multiple of four so RoPE pairs never straddle two
    //subgroups, and widths that are whole groups of 32.
    if (c.head_size % 4 != 0 || c.head_size > 256 || c.dim % 32 != 0 || c.hidden_dim % 32 != 0){
        error = "unsupported shape: head_size " + std::to_string(c.head_size) + ", dim " + std::to_string(c.dim) +
                ", hidden_dim " + std::to_string(c.hidden_dim);
        Unload();
        return false;
    }

    int q_dim = c.n_heads * c.head_size;
    int kv_dim = c.n_kv_heads * c.head_size;
    gpu_bytes = 0;
    context_len = context_len_ > 0 && context_len_ < c.seq_len ? context_len_ : c.seq_len;
    n_contexts = n_contexts_ > 0 ? n_contexts_ : 1;

    //The rmsnorm weights, in fp32, in one buffer: attention, FFN, final.
    const uint8_t* ptr = mapped + 256;
    size_t norm_floats = (size_t)c.n_layers * c.dim * 2 + c.dim;
    norms = CreateBuffer(norm_floats * sizeof(float),ptr);
    ptr += norm_floats * sizeof(float);

    //Walk the file's matrices in its order, then upload: as they are, or packed.
    bool f_ok = MapTensor(tok_embedding,ptr,c.vocab_size,c.dim,c.embed_bits);
    std::vector<LLMTensor> file_wo, file_w2;
    std::vector<LLMTensor>* lists[] = {&file_wq,&file_wk,&file_wv,&file_wo,&file_w1,&file_w2,&file_w3};
    int shapes[7][2] = {
        {q_dim,c.dim},{kv_dim,c.dim},{kv_dim,c.dim},{c.dim,q_dim},
        {c.hidden_dim,c.dim},{c.dim,c.hidden_dim},{c.hidden_dim,c.dim},
    };
    for (int m = 0;m < 7 && f_ok;m++){
        lists[m]->resize(c.n_layers);
        for (int l = 0;l < c.n_layers && f_ok;l++){
            f_ok = MapTensor(lists[m]->at(l),ptr,shapes[m][0],shapes[m][1],c.weight_bits);
        }
    }
    if (f_ok && !c.f_shared_classifier){
        f_ok = MapTensor(wcls,ptr,c.vocab_size,c.dim,c.weight_bits);
    }
    if (!f_ok){
        error = "the file ends before the last tensor - truncated, or not the layout its header says";
        Unload();
        return false;
    }
    UploadTensor(tok_embedding);
    if (c.f_shared_classifier){
        wcls = tok_embedding;   //the same buffer, described twice; freed once (see Unload)
    }else{
        UploadTensor(wcls);
    }
    qkv.resize(c.n_layers);
    w13.resize(c.n_layers);
    wo = file_wo;
    w2 = file_w2;
    for (int l = 0;l < c.n_layers;l++){
        qkv[l] = PackRows({&file_wq[l],&file_wk[l],&file_wv[l]},false);
        w13[l] = PackRows({&file_w1[l],&file_w3[l]},true);
        UploadTensor(wo[l]);
        UploadTensor(w2[l]);
    }

    BuildRopeTable();
    xb_dim = q_dim > c.dim ? q_dim : c.dim;
    x = CreateBuffer((size_t)MAX_BATCH * c.dim * 4,NULL);
    xb = CreateBuffer((size_t)MAX_BATCH * xb_dim * 4,NULL);
    q = CreateBuffer((size_t)MAX_BATCH * q_dim * 4,NULL);
    hb = CreateBuffer((size_t)MAX_BATCH * c.hidden_dim * 4,NULL);
    att = CreateBuffer((size_t)c.n_heads * MAX_BATCH * context_len * 4,NULL);
    logits = CreateBuffer((size_t)MAX_BATCH * c.vocab_size * 4,NULL);
    for (int i = 0;i < n_contexts;i++){
        key_caches.push_back(CreateBuffer((size_t)c.n_layers * context_len * kv_dim * 4,NULL));
        value_caches.push_back(CreateBuffer((size_t)c.n_layers * context_len * kv_dim * 4,NULL));
    }
    host_logits.assign((size_t)MAX_BATCH * c.vocab_size,0.0f);

    /*
        The block analysis: a slot per block, bound one at a time with glBindBufferRange, so each
        slot is padded to start at an offset the driver accepts for that. Every gain starts at 1.
    */
    GLint align = 16;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&align);
    int align_floats = align > 4 ? align / 4 : 1;
    slot_floats = (BLOCK_SLOT_HEADER + c.dim + align_floats - 1) / align_floats * align_floats;
    std::vector<float> slots((size_t)2 * c.n_layers * slot_floats,0.0f);
    for (int b = 0;b < 2 * c.n_layers;b++){
        slots[(size_t)b * slot_floats] = 1.0f;
    }
    block_slots = CreateBuffer(slots.size() * 4,slots.data());
    block_stats = CreateBuffer((size_t)(2 * c.n_layers + 2) * (3 + BLOCK_CANDIDATES) * 4,NULL);
    block_gains.assign(2 * c.n_layers,1.0f);
    f_gains_active = false;
    f_capture_pending = false;
    block_records.clear();

    std::string log;
    if (!BuildKernels(log)){
        error = "kernels did not build:\n" + log;
        Unload();
        return false;
    }

    //Every upload above is queued; wait for them so the load time is the real one.
    glFinish();
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR){
        //Most likely GL_OUT_OF_MEMORY: the model and its KV caches do not fit beside whatever
        //else this GPU holds.
        char buf[64];
        snprintf(buf,sizeof(buf),"GL error 0x%04X while uploading",gl_error);
        error = buf;
        Unload();
        return false;
    }
    load_seconds = std::chrono::duration<double>(Clock::now() - start).count();
    f_loaded = true;
    debug->Info("Loaded %s: dim %d, %d layers, vocab %d, %.2f GB on the GPU in %.1f s\n",
                file_path.c_str(),c.dim,c.n_layers,c.vocab_size,gpu_bytes / 1e9,load_seconds);
    return true;
}

void LLMModel::Unload(void){
    auto del = [](GLuint& b){ if (b){ glDeleteBuffers(1,&b); b = 0; } };
    bool f_shared = wcls.buffer == tok_embedding.buffer;
    del(tok_embedding.buffer);
    if (f_shared){
        wcls.buffer = 0;
    }else{
        del(wcls.buffer);
    }
    for (auto* list:{&qkv,&wo,&w13,&w2}){
        for (LLMTensor& t:*list){
            del(t.buffer);
        }
        list->clear();
    }
    for (auto* list:{&file_wq,&file_wk,&file_wv,&file_w1,&file_w3}){
        list->clear();
    }
    del(norms); del(rope_table);
    del(x); del(xb); del(q); del(hb); del(att); del(logits);
    del(block_slots); del(block_stats);
    block_gains.clear();
    f_gains_active = false;
    f_capture_pending = false;
    block_records.clear();     //their token ids mean nothing to the next model
    for (GLuint& b:key_caches){ del(b); }
    for (GLuint& b:value_caches){ del(b); }
    key_caches.clear();
    value_caches.clear();
    if (mapped){ UnmapViewOfFile(mapped); mapped = NULL; }
    if (mapping_handle){ CloseHandle(mapping_handle); mapping_handle = NULL; }
    if (file_handle != INVALID_HANDLE_VALUE){ CloseHandle(file_handle); file_handle = INVALID_HANDLE_VALUE; }
    gpu_bytes = 0;
    f_loaded = false;
    //The kernels stay: they do not depend on the model, and keeping them makes a reload of a
    //different model skip the compile.
}

//--- dispatching --------------------------------------------------------------------------------

static void Bind(int binding, GLuint buffer){
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,binding,buffer);
}

static void Barrier(){
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void LLMModel::ResetProfile(void){
    profile_gpu_ms.clear();
    profile_issue_ms = 0.0;
    profile_readback_ms = 0.0;
    profile_calls = 0;
}

void LLMModel::Dispatch(const char* label, GLuint groups_x, GLuint groups_y){
    if (!f_profile){
        glDispatchCompute(groups_x,groups_y,1);
        return;
    }
    if (pending_queries.size() >= query_pool.size()){
        GLuint more[256];
        glGenQueries(256,more);
        query_pool.insert(query_pool.end(),more,more + 256);
    }
    GLuint query = query_pool[pending_queries.size()];
    glBeginQuery(GL_TIME_ELAPSED,query);
    glDispatchCompute(groups_x,groups_y,1);
    glEndQuery(GL_TIME_ELAPSED);
    pending_queries.push_back({label,query});
}

void LLMModel::CollectProfile(Clock::time_point issue_start, Clock::time_point readback_start){
    if (!f_profile){
        return;
    }
    auto end = Clock::now();
    profile_issue_ms += std::chrono::duration<double,std::milli>(readback_start - issue_start).count();
    profile_readback_ms += std::chrono::duration<double,std::milli>(end - readback_start).count();
    //The readback waited for the GPU, so every query has its answer by now.
    for (auto& entry:pending_queries){
        GLuint64 ns = 0;
        glGetQueryObjectui64v(entry.second,GL_QUERY_RESULT,&ns);
        profile_gpu_ms[entry.first] += ns / 1e6;
    }
    pending_queries.clear();
    profile_calls++;
}

void LLMModel::Embed(const int* tokens, int nb){
    glUseProgram(k_embed.shader->progid);
    Bind(0,tok_embedding.buffer);
    Bind(1,tok_embedding.buffer);
    Bind(2,x);
    k_embed.SetArray("tokens",tokens,nb);
    k_embed.Set("dim",config.dim);
    k_embed.Set("bits",tok_embedding.bits);
    k_embed.Set("gs",config.group_size);
    k_embed.Set("s_offset",tok_embedding.ScaleOffset());
    Dispatch("embed",(config.dim + 255) / 256,nb);
}

void LLMModel::RmsNorm(int nb, GLuint x_buf, int x_offset, int x_stride, int weight_offset, GLuint out_buf, int out_offset, int out_stride){
    glUseProgram(k_rmsnorm.shader->progid);
    Bind(0,x_buf);
    Bind(1,norms);
    Bind(2,out_buf);
    k_rmsnorm.Set("n",config.dim);
    k_rmsnorm.Set("x_offset",x_offset);
    k_rmsnorm.Set("x_stride",x_stride);
    k_rmsnorm.Set("w_offset",weight_offset);
    k_rmsnorm.Set("o_offset",out_offset);
    k_rmsnorm.Set("o_stride",out_stride);
    Dispatch("rmsnorm",nb);
}

/*
    ONE PROGRAM PER ROLE. The one-token path runs the same kernel for qkv, wo, w13, w2 and the
    classifier, one after another, each with different uniforms. With a single program every
    dispatch rewrote a dozen uniforms of a program the GPU was still running, and generation got
    SLOWER when the dispatches were merged (103 -> 85 tok/s) although the GPU's own work went
    down: the time went into gaps between the dispatches. So each label gets an instance of its
    own, whose uniforms stay put from layer to layer except the few that really change, and
    LLMKernel::Set skips a value the program already has.
*/
void LLMModel::Matrix(const char* label, const LLMTensor& w, int nb, GLuint x_buf, int x_offset, int x_stride, int norm_offset, const LLMOut& out){
    LLMConfig& c = config;
    bool f_batch = nb > 1;
    LLMKernel* kernel;
    if (f_batch){
        kernel = w.bits == 4 ? &k_matmat_q4 : &k_matmat_q8;
    }else{
        std::string role = std::string(label) + (w.bits == 4 ? "/q4" : "/q8");
        auto it = role_kernels.find(role);
        if (it == role_kernels.end()){
            std::string log;
            kernel = &role_kernels[role];   //std::map: stays valid as the map grows
            if (!BuildKernel(*kernel,w.bits == 4 ? "shaders/llm_matvec_q4.comp" : "shaders/llm_matvec_q8.comp",log)){
                debug->Err("%s: %s\n",role.c_str(),log.c_str());
            }
        }else{
            kernel = &it->second;
        }
    }
    LLMKernel& k = *kernel;
    int kv_dim = c.n_kv_heads * c.head_size;
    glUseProgram(k.shader->progid);
    Bind(0,w.buffer);       //the values, as packed uints
    Bind(1,w.buffer);       //the same buffer, as floats, for the scales
    Bind(2,x_buf);
    Bind(3,out.y);
    //The caches are bound only for OUT_QKV; elsewhere y stands in, so no binding point the
    //shader declares is ever empty.
    Bind(4,out.mode == OUT_QKV ? key_caches[out.context] : out.y);
    Bind(5,out.mode == OUT_QKV ? value_caches[out.context] : out.y);
    Bind(6,rope_table);
    //the block's slot for OUT_BLOCK - the gain and room for its update - and y as a stand-in elsewhere
    if (out.mode == OUT_BLOCK){
        BindBufferRange(GL_SHADER_STORAGE_BUFFER,8,block_slots,(GLintptr)out.block * slot_floats * 4,(GLsizeiptr)slot_floats * 4);
    }else{
        Bind(8,out.y);
    }
    k.Set("n",w.cols);
    k.Set("d",w.rows);
    k.Set("s_offset",w.ScaleOffset());
    k.Set("x_offset",x_offset / 4);     //the kernels read x as vec4
    if (w.bits == 8){
        k.Set("gs",c.group_size);
    }
    if (f_batch){
        k.Set("x_stride",x_stride / 4);
        k.Set("nb",nb);
    }else{
        Bind(7,norms);
        k.Set("norm_offset",norm_offset >= 0 ? norm_offset / 4 : -1);
    }
    k.Set("out_mode",out.mode);
    k.Set("y_offset",out.y_offset);
    k.Set("y_stride",out.y_stride);
    k.Set("q_dim",c.n_heads * c.head_size);
    k.Set("kv_dim",kv_dim);
    k.Set("head_size",c.head_size);
    int row = out.layer * context_len * kv_dim + out.pos0 * kv_dim;    //token 0's row in the caches
    k.Set("k_offset",row);
    k.Set("v_offset",row);
    k.Set("pos0",out.pos0);
    k.Set("capture_t",nb - 1);
    if (f_batch){
        Dispatch(label,(w.rows + MATMAT_ROWS - 1) / MATMAT_ROWS,(nb + MATMAT_TOKENS - 1) / MATMAT_TOKENS);
    }else{
        Dispatch(label,(w.rows + MATVEC_ROWS - 1) / MATVEC_ROWS);
    }
}

void LLMModel::Attention(int nb, int pos0, int context, int layer){
    LLMConfig& c = config;
    int kv_dim = c.n_kv_heads * c.head_size;
    glUseProgram(k_attention.shader->progid);
    Bind(0,q);
    Bind(1,key_caches[context]);
    Bind(2,value_caches[context]);
    Bind(3,att);
    Bind(4,xb);
    k_attention.Set("pos0",pos0);
    k_attention.Set("head_size",c.head_size);
    k_attention.Set("n_heads",c.n_heads);
    k_attention.Set("kv_dim",kv_dim);
    k_attention.Set("kv_mul",c.n_heads / c.n_kv_heads);
    k_attention.Set("context_len",context_len);
    k_attention.Set("layer_offset",layer * context_len * kv_dim);
    k_attention.Set("q_stride",c.n_heads * c.head_size);
    k_attention.Set("o_stride",xb_dim);
    Dispatch("attention",c.n_heads,nb);
}

const float* LLMModel::ReadLogits(int rows, Clock::time_point issue_start){
    //A shader wrote them and glGetNamedBufferSubData reads them: that is the BUFFER_UPDATE
    //barrier, not the storage one.
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    auto readback_start = Clock::now();
    glGetNamedBufferSubData(logits,0,(size_t)rows * config.vocab_size * 4,host_logits.data());
    CollectProfile(issue_start,readback_start);
    return host_logits.data();
}

//--- the forward pass ---------------------------------------------------------------------------

const float* LLMModel::Forward(int token, int pos, int context){
    LLMConfig& c = config;
    if (!f_loaded || pos < 0 || pos >= context_len || context < 0 || context >= n_contexts || token < 0 || token >= c.vocab_size){
        return NULL;
    }
    auto issue_start = Clock::now();
    Embed(&token,1);
    Barrier();
    for (int l = 0;l < c.n_layers;l++){
        //rmsnorm folded into the matrix kernels: they read the raw residual stream x
        LLMOut out_qkv;
        out_qkv.mode = OUT_QKV;
        out_qkv.y = q;
        out_qkv.context = context;
        out_qkv.layer = l;
        out_qkv.pos0 = pos;
        if (f_fold_norm){
            Matrix("matvec qkv",qkv[l],1,x,0,0,l * c.dim,out_qkv);
        }else{
            RmsNorm(1,x,0,0,l * c.dim,xb,0,0);
            Barrier();
            Matrix("matvec qkv",qkv[l],1,xb,0,0,-1,out_qkv);
        }
        Barrier();
        Attention(1,pos,context,l);
        Barrier();
        //the residual connections
        Matrix("matvec wo",wo[l],1,xb,0,0,-1,ResidualOut(BlockIndex(l,false),0));
        Barrier();
        LLMOut out_hb;
        out_hb.mode = OUT_SWIGLU;
        out_hb.y = hb;
        if (f_fold_norm){
            Matrix("matvec w13",w13[l],1,x,0,0,(c.n_layers + l) * c.dim,out_hb);
        }else{
            RmsNorm(1,x,0,0,(c.n_layers + l) * c.dim,xb,0,0);
            Barrier();
            Matrix("matvec w13",w13[l],1,xb,0,0,-1,out_hb);
        }
        Barrier();
        Matrix("matvec w2",w2[l],1,hb,0,0,-1,ResidualOut(BlockIndex(l,true),0));
        Barrier();
    }
    //The final norm on its own: folded into the classifier, each of its 16384 workgroups would
    //work out the same scale again.
    RmsNorm(1,x,0,0,2 * c.n_layers * c.dim,xb,0,0);
    Barrier();
    LLMOut out_logits;
    out_logits.y = logits;
    Matrix("matvec wcls",wcls,1,xb,0,0,-1,out_logits);
    NoteCapture(context,pos,token,0,0);
    return ReadLogits(1,issue_start);
}

const float* LLMModel::ForwardBatch(const int* tokens, int nb, int pos0, int context, bool all_logits){
    LLMConfig& c = config;
    if (!f_loaded || nb < 1 || nb > MAX_BATCH || pos0 < 0 || pos0 + nb > context_len || context < 0 || context >= n_contexts){
        return NULL;
    }
    for (int i = 0;i < nb;i++){
        if (tokens[i] < 0 || tokens[i] >= c.vocab_size){
            return NULL;
        }
    }
    int q_dim = c.n_heads * c.head_size;
    auto issue_start = Clock::now();
    Embed(tokens,nb);
    Barrier();
    for (int l = 0;l < c.n_layers;l++){
        RmsNorm(nb,x,0,c.dim,l * c.dim,xb,0,xb_dim);
        Barrier();
        LLMOut out_qkv;
        out_qkv.mode = OUT_QKV;
        out_qkv.y = q;
        out_qkv.y_stride = q_dim;
        out_qkv.context = context;
        out_qkv.layer = l;
        out_qkv.pos0 = pos0;
        Matrix("matmat qkv",qkv[l],nb,xb,0,xb_dim,-1,out_qkv);
        Barrier();
        Attention(nb,pos0,context,l);
        Barrier();
        Matrix("matmat wo",wo[l],nb,xb,0,xb_dim,-1,ResidualOut(BlockIndex(l,false),c.dim));
        Barrier();
        RmsNorm(nb,x,0,c.dim,(c.n_layers + l) * c.dim,xb,0,xb_dim);
        Barrier();
        LLMOut out_hb;
        out_hb.mode = OUT_SWIGLU;
        out_hb.y = hb;
        out_hb.y_stride = c.hidden_dim;
        Matrix("matmat w13",w13[l],nb,xb,0,xb_dim,-1,out_hb);
        Barrier();
        Matrix("matmat w2",w2[l],nb,hb,0,c.hidden_dim,-1,ResidualOut(BlockIndex(l,true),c.dim));
        Barrier();
    }
    LLMOut out_logits;
    out_logits.y = logits;
    out_logits.y_stride = c.vocab_size;
    if (all_logits){
        RmsNorm(nb,x,0,c.dim,2 * c.n_layers * c.dim,xb,0,xb_dim);
        Barrier();
        Matrix(nb > 1 ? "matmat wcls" : "matvec wcls",wcls,nb,xb,0,xb_dim,-1,out_logits);
        NoteCapture(context,pos0 + nb - 1,tokens[nb - 1],nb - 1,nb - 1);
        return ReadLogits(nb,issue_start);
    }
    //only the last token's logits: its row alone through the final norm and the classifier
    RmsNorm(1,x,(nb - 1) * c.dim,0,2 * c.n_layers * c.dim,xb,0,0);
    Barrier();
    Matrix("matvec wcls",wcls,1,xb,0,0,-1,out_logits);
    NoteCapture(context,pos0 + nb - 1,tokens[nb - 1],nb - 1,0);
    return ReadLogits(1,issue_start);
}

const float* LLMModel::ForwardTokens(const int* tokens, int n, int pos0, int context){
    const float* result = NULL;
    for (int i = 0;i < n;i += MAX_BATCH){
        int nb = n - i < MAX_BATCH ? n - i : MAX_BATCH;
        result = nb == 1 ? Forward(tokens[i],pos0 + i,context) : ForwardBatch(tokens + i,nb,pos0 + i,context,false);
        if (!result){
            return NULL;
        }
    }
    return result;
}

//--- the block analysis -------------------------------------------------------------------------

LLMOut LLMModel::ResidualOut(int block, int stride){
    LLMOut out;
    out.mode = BlockMode() ? OUT_BLOCK : OUT_ACCUMULATE;
    out.y = x;
    out.y_stride = stride;
    out.block = block;
    return out;
}

void LLMModel::NoteCapture(int context, int pos, int token, int x_row, int logits_row){
    //Only a recorded pass leaves something to record; a pass that merely had gains does not.
    f_capture_pending = f_block_stats;
    capture_context = context;
    capture_pos = pos;
    capture_token = token;
    capture_x_row = x_row;
    capture_logits_row = logits_row;
}

void LLMModel::SetBlockGain(int block, float gain){
    if (!f_loaded || block < 0 || block >= Blocks()){
        return;
    }
    block_gains[block] = gain;
    glNamedBufferSubData(block_slots,(GLintptr)block * slot_floats * 4,4,&gain);
    f_gains_active = false;
    for (float g:block_gains){
        f_gains_active = f_gains_active || g != 1.0f;
    }
}

bool LLMModel::RecordBlocks(int chosen){
    if (!f_loaded || !f_capture_pending || chosen < 0 || chosen >= config.vocab_size){
        return false;
    }
    f_capture_pending = false;
    auto start = Clock::now();
    LLMConfig& c = config;
    int n_blocks = Blocks();
    const float* lg = host_logits.data() + (size_t)capture_logits_row * c.vocab_size;

    //The candidates: the chosen token, then the highest of the others. One pass over the
    //vocabulary keeping a short sorted list, rather than a sort of all of it.
    //(Their probabilities need the softmax's normaliser, which llm_blocks.comp works out.)
    int cand[BLOCK_CANDIDATES];
    int n = 1;
    cand[0] = chosen;
    for (int i = 0;i < c.vocab_size;i++){
        if (i == chosen || (n == BLOCK_CANDIDATES && lg[i] <= lg[cand[n - 1]])){
            continue;
        }
        int k = n < BLOCK_CANDIDATES ? n++ : n - 1;
        while (k > 1 && lg[cand[k - 1]] < lg[i]){
            cand[k] = cand[k - 1];
            k--;
        }
        cand[k] = i;
    }
    double cpu_ms = std::chrono::duration<double,std::milli>(Clock::now() - start).count();

    //The slots hold the updates the last forward pass wrote; make them visible to this dispatch.
    Barrier();
    glUseProgram(k_blocks.shader->progid);
    Bind(0,tok_embedding.buffer);
    Bind(1,tok_embedding.buffer);
    Bind(2,wcls.buffer);
    Bind(3,wcls.buffer);
    Bind(4,block_slots);
    Bind(5,norms);
    Bind(6,x);
    Bind(7,block_stats);
    Bind(8,logits);
    k_blocks.Set("logits_offset",capture_logits_row * c.vocab_size);
    k_blocks.Set("vocab",c.vocab_size);
    k_blocks.Set("dim",c.dim);
    k_blocks.Set("n_blocks",n_blocks);
    k_blocks.Set("slot",slot_floats);
    k_blocks.Set("token",capture_token);
    k_blocks.Set("x_offset",capture_x_row * c.dim);
    k_blocks.Set("norm_offset",2 * c.n_layers * c.dim);
    k_blocks.SetArray("candidates",cand,n);
    k_blocks.Set("n_candidates",n);
    k_blocks.Set("e_bits",tok_embedding.bits);
    k_blocks.Set("e_gs",c.group_size);
    k_blocks.Set("e_s_offset",tok_embedding.ScaleOffset());
    k_blocks.Set("c_bits",wcls.bits);
    k_blocks.Set("c_gs",c.group_size);
    k_blocks.Set("c_s_offset",wcls.ScaleOffset());
    //Timed with a query of its own, except while profiling: Dispatch then wraps it in one, and two
    //GL_TIME_ELAPSED queries cannot be open at once.
    bool f_timed = !f_profile;
    if (f_timed){
        if (!block_timer){
            glGenQueries(1,&block_timer);
        }
        glBeginQuery(GL_TIME_ELAPSED,block_timer);
    }
    Dispatch("blocks",n_blocks + 2);    //the blocks, the embedding, the softmax's normaliser
    if (f_timed){
        glEndQuery(GL_TIME_ELAPSED);
    }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    const int outs = 3 + BLOCK_CANDIDATES;
    std::vector<float> s((size_t)(n_blocks + 2) * outs);
    glGetNamedBufferSubData(block_stats,0,s.size() * 4,s.data());
    auto readback_end = Clock::now();

    LLMBlockRecord r;
    if (f_timed){
        GLuint64 ns = 0;
        glGetQueryObjectui64v(block_timer,GL_QUERY_RESULT,&ns);   //the readback waited for it: ready
        r.gpu_ms = ns / 1e6;
    }
    r.context = capture_context;
    r.pos = capture_pos;
    r.token = capture_token;
    r.chosen = chosen;
    r.gain = block_gains;
    r.candidates.assign(cand,cand + n);
    const float* norm = s.data() + (size_t)(n_blocks + 1) * outs;    //the largest logit, sum exp(logit - largest)
    for (int k = 0;k < n;k++){
        r.logit.push_back(lg[cand[k]]);
        r.prob.push_back((float)(exp(lg[cand[k]] - norm[0]) / norm[1]));
    }
    //the embedding's workgroup: the real final stream's size (the classifier's rms), and how far
    //the slots' rebuild of it is off
    const float* e = s.data() + (size_t)n_blocks * outs;
    double xf2 = e[0];
    double rms = sqrt(xf2 / c.dim + 1e-5);
    r.rebuild_error = xf2 > 0.0 ? (float)sqrt(e[2] / xf2) : 0.0f;
    r.share.resize((size_t)(n_blocks + 1) * n);
    for (int b = 0;b <= n_blocks;b++){
        const float* sb = s.data() + (size_t)b * outs;
        if (b < n_blocks){
            double g = block_gains[b];
            double xx = sb[0], dd = sb[1], xd = sb[2];
            double yy = xx + 2.0 * g * xd + g * g * dd;    //|x + g dx|^2
            r.rel_update.push_back(xx > 0.0 ? (float)sqrt(dd / xx) : 0.0f);
            r.cos_distance.push_back(xx > 0.0 && yy > 0.0 ? (float)(1.0 - (xx + g * xd) / sqrt(xx * yy)) : 0.0f);
        }
        for (int k = 0;k < n;k++){
            r.share[(size_t)b * n + k] = (float)(sb[3 + k] / rms);
        }
    }
    for (int k = 0;k < n;k++){
        double sum = r.share[(size_t)n_blocks * n + k];
        for (int b = 0;b < n_blocks;b++){
            sum += block_gains[b] * r.share[(size_t)b * n + k];
        }
        r.check = std::max(r.check,(float)fabs(sum - r.logit[k]));
    }
    auto end = Clock::now();
    r.ms = std::chrono::duration<double,std::milli>(end - start).count();
    r.cpu_ms = cpu_ms + std::chrono::duration<double,std::milli>(end - readback_end).count();
    block_records.push_back(std::move(r));
    while (block_records.size() > BLOCK_RECORDS_MAX){
        block_records.pop_front();
    }
    block_records_total++;
    return true;
}

//--- checks -------------------------------------------------------------------------------------

/*
    The value at (row, col) of one of the file's matrices, decoded on the CPU from the mapped file
    the way runq's dequantize does: Q4_0 groups of 32 with element k in the low nibble of byte k
    and element k + 16 in the high nibble, offset by 8; Q8_0 one signed byte per value.
*/
static float Dequant(const LLMTensor& t, int group_size, size_t row, size_t col){
    size_t e = row * t.cols + col;
    const float* scales = (const float*)(t.host + t.q_bytes);
    if (t.bits == 4){
        size_t g = e / 32, k = e % 32;
        uint8_t b = t.host[g * 16 + (k & 15)];
        int v = (k < 16 ? (b & 0x0F) : (b >> 4)) - 8;
        return v * scales[g];
    }
    return (int8_t)t.host[e] * scales[e / group_size];
}

bool LLMModel::TestMatrix(const std::string& name, int layer, int nb, unsigned seed, double& max_diff, double& max_ref, std::string& error){
    if (nb < 1 || nb > MAX_BATCH){
        error = "nb must be 1.." + std::to_string(MAX_BATCH);
        return false;
    }
    bool f_per_layer = name != "wcls";
    if (f_per_layer && (layer < 0 || layer >= config.n_layers)){
        error = "layer out of range";
        return false;
    }
    //the GPU matrix, and for each of its rows the file's matrix and row it came from
    const LLMTensor* gpu = NULL;
    std::vector<std::pair<const LLMTensor*,int>> source;
    if (name == "qkv"){
        gpu = &qkv[layer];
        for (const LLMTensor* t:{&file_wq[layer],&file_wk[layer],&file_wv[layer]}){
            for (int r = 0;r < t->rows;r++){ source.push_back({t,r}); }
        }
    }else if (name == "w13"){
        gpu = &w13[layer];
        for (int r = 0;r < file_w1[layer].rows;r++){
            source.push_back({&file_w1[layer],r});
            source.push_back({&file_w3[layer],r});
        }
    }else if (name == "wo" || name == "w2" || name == "wcls"){
        gpu = name == "wo" ? &wo[layer] : name == "w2" ? &w2[layer] : &wcls;
        for (int r = 0;r < gpu->rows;r++){ source.push_back({gpu,r}); }
    }else{
        error = "unknown matrix " + name + " (qkv w13 wo w2 wcls)";
        return false;
    }

    //nb random inputs, as the GPU sees them, in scratch buffers of their own
    std::mt19937 rng(seed);
    std::normal_distribution<float> normal(0.0f,1.0f);
    std::vector<float> xs((size_t)nb * gpu->cols);
    for (float& v:xs){ v = normal(rng); }
    GLuint x_buf = CreateBuffer(xs.size() * 4,xs.data());
    GLuint y_buf = CreateBuffer((size_t)nb * gpu->rows * 4,NULL);
    LLMOut out;
    out.y = y_buf;
    out.y_stride = gpu->rows;
    Matrix("test",*gpu,nb,x_buf,0,gpu->cols,-1,out);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    std::vector<float> ys((size_t)nb * gpu->rows);
    glGetNamedBufferSubData(y_buf,0,ys.size() * 4,ys.data());
    glDeleteBuffers(1,&x_buf);
    glDeleteBuffers(1,&y_buf);
    gpu_bytes -= xs.size() * 4 + ys.size() * 4;

    max_diff = 0.0;
    max_ref = 0.0;
    std::vector<float> row(gpu->cols);
    for (int r = 0;r < gpu->rows;r++){
        for (int col = 0;col < gpu->cols;col++){
            row[col] = Dequant(*source[r].first,config.group_size,source[r].second,col);
        }
        for (int b = 0;b < nb;b++){
            double ref = 0.0;
            const float* xr = xs.data() + (size_t)b * gpu->cols;
            for (int col = 0;col < gpu->cols;col++){
                ref += (double)row[col] * xr[col];
            }
            max_diff = fmax(max_diff,fabs(ref - ys[(size_t)b * gpu->rows + r]));
            max_ref = fmax(max_ref,fabs(ref));
        }
    }
    return true;
}
