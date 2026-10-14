#include "ApplicationLLM.h"
#include "LLMChat.h"
#include "Debug.h"
#ifdef USE_MCP
//MCPServer.h and nothing else from it: with USE_MCP=0 that class is not compiled, and the
//header also pulls winsock in with the include-order constraint it documents - both
//pointless in a build with no debug server. See the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>

static Debugger* debug = new Debugger("ApplicationLLM",DEBUG_ALL);

ApplicationLLM::ApplicationLLM():Application(){
    debug->Info("Created new ApplicationLLM.\n");
    //There is nothing in the scene to inspect; the Engine window stays for frame timing.
    f_show_scene_window = false;
    f_show_inspector_window = false;
    f_show_engine_window = true;
    /*
        An OBJECT from the start, not a null json. The panel calls model_info.value(...) every
        frame, and value() on anything but an object throws - which in this build, with
        JSON_NOEXCEPTION, is an abort() with no crash report. That is exactly how the first
        version of this app died two seconds after starting.
    */
    model_info = json{ {"loaded",false} };
}

void ApplicationLLM::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    //An empty scene: the renderer is here because the frame loop and ImGui draw through it,
    //not because there is anything to look at.
    renderer->f_render_skybox = false;
    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    main_scene = CreateNewScene("LLM Scene");

    ReadGpuInfo();
    VoidInit();

#ifdef USE_MCP
    RegisterMCPTools();
    RegisterVoidTools();
#endif
}

void ApplicationLLM::ReadGpuInfo(void){
    LLMGpuInfo info;
    info.vendor = (const char*)glGetString(GL_VENDOR);
    info.renderer = (const char*)glGetString(GL_RENDERER);
    info.version = (const char*)glGetString(GL_VERSION);
    info.glsl_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);

    //GL_MAX_SHADER_STORAGE_BLOCK_SIZE is a 64-bit query in the spec, but glGetInteger64v is not
    //loaded by core's glad. NVIDIA reports 2^31-1 here, which an int holds; a driver offering
    //more would be clamped to that by glGetIntegerv, which is still the right answer for a
    //buffer indexed with a 32-bit int in GLSL.
    GLint v = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&v);             info.max_ssbo_block_size = v;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&info.max_ssbo_bindings);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&info.ssbo_offset_alignment);
    glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE,&info.max_shared_memory);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,&info.max_invocations);
    for (int i = 0;i < 3;i++){
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,i,&info.max_group_size[i]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,i,&info.max_group_count[i]);
    }

    //The extensions that matter to a compute workload, out of the few hundred a desktop driver
    //lists. Listing them all would bury the four that decide anything.
    static const char* INTERESTING[] = {
        "GL_KHR_shader_subgroup","GL_NV_shader_subgroup_partitioned","GL_ARB_shader_ballot",
        "GL_NV_gpu_shader5","GL_ARB_gpu_shader_int64","GL_AMD_gpu_shader_half_float",
        "GL_NV_shader_atomic_float","GL_ARB_shader_atomic_counter_ops","GL_NVX_gpu_memory_info",
        "GL_ATI_meminfo","GL_KHR_debug","GL_NV_cooperative_matrix","GL_EXT_shader_16bit_storage",
        "GL_NV_gpu_shader_fp64","GL_ARB_compute_variable_group_size",
    };
    //glGetStringi is NULL only on a context older than GL 3.0, which could not run this app anyway.
    PFNGLGETSTRINGIPROC get_stringi = glGetStringi;
    if (get_stringi){
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS,&count);
        for (int i = 0;i < count;i++){
            const char* ext = (const char*)get_stringi(GL_EXTENSIONS,i);
            if (!ext){
                continue;
            }
            for (const char* want:INTERESTING){
                if (strcmp(ext,want) == 0){
                    info.extensions.push_back(ext);
                }
            }
        }
    }else{
        debug->Err("glGetStringi did not load - the extension list will be empty\n");
    }
    for (const std::string& ext:info.extensions){
        if (ext == "GL_KHR_shader_subgroup"){ info.f_subgroups = true; }
        if (ext == "GL_NVX_gpu_memory_info"){ info.f_nvx_memory_info = true; }
    }
    if (info.f_subgroups){
        glGetIntegerv(GL_SUBGROUP_SIZE_KHR,&info.subgroup_size);
        GLint features = 0;
        glGetIntegerv(GL_SUBGROUP_SUPPORTED_FEATURES_KHR,&features);
        info.subgroup_features = (unsigned int)features;
    }
    if (info.f_nvx_memory_info){
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX,&info.vram_total_kb);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX,&info.vram_available_kb);
    }
    //A query this driver does not know leaves GL_INVALID_ENUM behind; clear it here rather than
    //let it surface in whatever the renderer checks next and send someone looking there.
    while (glGetError() != GL_NO_ERROR){}

    debug->Info("GPU: %s / %s, GL %s\n",info.vendor.c_str(),info.renderer.c_str(),info.version.c_str());
    debug->Info("SSBO block max %lld bytes, shared memory %d bytes, subgroups %s (size %d)\n",
                info.max_ssbo_block_size,info.max_shared_memory,info.f_subgroups ? "yes" : "no",info.subgroup_size);

    std::lock_guard<std::mutex> lock(info_mutex);
    gpu = info;
}

json ApplicationLLM::GpuInfoJson(void){
    std::lock_guard<std::mutex> lock(info_mutex);
    json j;
    j["vendor"] = gpu.vendor;
    j["renderer"] = gpu.renderer;
    j["gl_version"] = gpu.version;
    j["glsl_version"] = gpu.glsl_version;
    j["max_ssbo_block_size"] = gpu.max_ssbo_block_size;
    j["max_ssbo_bindings"] = gpu.max_ssbo_bindings;
    j["ssbo_offset_alignment"] = gpu.ssbo_offset_alignment;
    j["max_shared_memory"] = gpu.max_shared_memory;
    j["max_invocations"] = gpu.max_invocations;
    j["max_group_size"] = {gpu.max_group_size[0],gpu.max_group_size[1],gpu.max_group_size[2]};
    j["max_group_count"] = {gpu.max_group_count[0],gpu.max_group_count[1],gpu.max_group_count[2]};
    j["subgroups"] = gpu.f_subgroups;
    j["subgroup_size"] = gpu.subgroup_size;
    j["subgroup_features"] = gpu.subgroup_features;
    j["vram_total_mb"] = gpu.vram_total_kb / 1024;
    j["vram_available_mb_at_start"] = gpu.vram_available_kb / 1024;
    j["extensions"] = gpu.extensions;
    return j;
}

json ApplicationLLM::ModelInfoJson(void){
    json j;
    j["loaded"] = model && model->IsLoaded();
    if (!model || !model->IsLoaded()){
        return j;
    }
    const LLMConfig& c = model->config;
    j["path"] = model->path;
    j["dim"] = c.dim;
    j["hidden_dim"] = c.hidden_dim;
    j["n_layers"] = c.n_layers;
    j["n_heads"] = c.n_heads;
    j["n_kv_heads"] = c.n_kv_heads;
    j["head_size"] = c.head_size;
    j["vocab_size"] = c.vocab_size;
    j["seq_len"] = c.seq_len;
    j["weight_bits"] = c.weight_bits;
    j["embed_bits"] = c.embed_bits;
    j["group_size"] = c.group_size;
    j["shared_classifier"] = c.f_shared_classifier;
    j["yarn_factor"] = c.yarn_factor;
    j["context_len"] = model->context_len;
    j["contexts"] = model->n_contexts;
    j["file_gb"] = model->file_size / 1e9;
    j["gpu_gb"] = model->gpu_bytes / 1e9;
    j["load_seconds"] = model->load_seconds;
    j["tokenizer"] = tokenizer.IsLoaded() ? (tokenizer.f_tekken ? "tekken" : "sentencepiece") : "none";
    j["tool_calls"] = tokenizer.tool_calls >= 0;
    return j;
}

//A whole text file, or an empty string with error set. MCP threads may call it: no GL.
static bool ReadTextFile(const std::string& path, std::string& text, std::string& error){
    FILE* f = fopen(path.c_str(),"rb");
    if (!f){
        error = "cannot open " + path;
        return false;
    }
    text.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf,1,sizeof(buf),f)) > 0){
        text.append(buf,n);
    }
    fclose(f);
    return true;
}

json ApplicationLLM::RunOnRenderThread(std::function<json()> work, int timeout_ms){
    std::shared_ptr<LLMJob> job = std::make_shared<LLMJob>();
    job->work = work;
    {
        std::lock_guard<std::mutex> lock(job_mutex);
        jobs.push_back(job);
    }
    //Polled rather than signalled, the same shape as testfx's WaitForEffectRequest.
    for (int waited_ms = 0;waited_ms < timeout_ms;waited_ms += 5){
        {
            std::lock_guard<std::mutex> lock(job_mutex);
            if (job->f_done){
                return job->result;
            }
        }
        Sleep(5);
    }
    return json{ {"error","the render thread did not finish in " + std::to_string(timeout_ms / 1000) +
                          " s; the job still runs when its turn comes, but this answer is lost"} };
}

json ApplicationLLM::LoadModel(const std::string& path, const std::string& tok, int context, int contexts){
    if (!model){
        model = new LLMModel();
    }
    chat.Reset();
    void_chat.Reset();
    std::string error;
    if (!model->Load(path,context,contexts,error)){
        return json{ {"error",error} };
    }
    if (!tokenizer.Load(tok,model->config.vocab_size,error)){
        model->Unload();
        return json{ {"error",error} };
    }
    return json{ {"model",ModelInfoJson()} };
}

/*
    Frame thread, once per frame, before the scene is drawn.

    First every queued job, whole - taken out under the lock and run without it, so a tool
    queueing the next job is never blocked behind a long one. Then what the panels asked for:
    a load, a chat message. Then a slice of the chat's reply: CHAT_BUDGET_MS of generation per
    frame, so the window keeps redrawing and shows the reply as it grows. At about 10 ms a token
    that is two or three tokens a frame; the frame rate drops while it answers, which in a bench
    is the right trade.
*/
#define CHAT_BUDGET_MS 25.0

void ApplicationLLM::PreRender(void){
    std::deque<std::shared_ptr<LLMJob>> todo;
    {
        std::lock_guard<std::mutex> lock(job_mutex);
        todo.swap(jobs);
    }
    bool f_changed = !todo.empty();
    for (std::shared_ptr<LLMJob>& job:todo){
        json result = job->work();
        std::lock_guard<std::mutex> lock(job_mutex);
        job->result = result;
        job->f_done = true;
    }
    if (f_load_request){
        f_load_request = false;
        json result = LoadModel(load_model_path,load_tokenizer_path,load_context,load_contexts);
        if (result.contains("error")){
            load_status = result.value("error",std::string());
        }else{
            char buf[64];
            snprintf(buf,sizeof(buf),"loaded in %.1f s",model->load_seconds);
            load_status = buf;
        }
        f_changed = true;
    }
    if (f_chat_send){
        f_chat_send = false;
        chat_error.clear();
        if (void_chat.f_generating){
            chat_error = "the Void is still speaking";
        }else if (model && model->IsLoaded()){
            chat.system_prompt = chat_system;
            if (chat.Send(*model,tokenizer,chat_input,chat_error)){
                chat_input[0] = '\0';
            }
        }else{
            chat_error = "no model loaded";
        }
        f_chat_scroll = true;
    }
    if (chat.f_generating && model && model->IsLoaded()){
        chat.Step(*model,tokenizer,CHAT_BUDGET_MS);
        f_chat_scroll = true;
    }
    VoidUpdate();
    if (f_changed){
        json info = ModelInfoJson();
        std::lock_guard<std::mutex> lock(info_mutex);
        model_info = info;
    }
}

//--- the block analysis -------------------------------------------------------------------------

//The Blocks window's views; described at RenderBlocksPanel. Here because llm_blocks sets them too.
enum { BLOCKS_VIEW_UPDATE, BLOCKS_VIEW_TURN, BLOCKS_VIEW_PUSH, BLOCKS_VIEW_VERSUS };

/*
    A token's text, made safe to show and to put in a json: valid UTF-8, with line breaks and
    other control characters written out, and any byte that is not part of a whole UTF-8
    character as <XX>. A Tekken token is raw bytes and often half a character; json refuses to
    serialise that, and in this build (JSON_NOEXCEPTION) refusing is an abort.
*/
static std::string ShowText(const std::string& s){
    std::string out;
    size_t i = 0;
    while (i < s.size()){
        unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool f_ok = len > 0 && i + len <= s.size();
        for (int k = 1;f_ok && k < len;k++){
            f_ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
        }
        if (f_ok && len >= 3){
            //no overlong forms, no UTF-16 surrogates, nothing past U+10FFFF
            unsigned char c1 = (unsigned char)s[i + 1];
            f_ok = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 >= 0xA0) && !(c == 0xF0 && c1 < 0x90) && !(c == 0xF4 && c1 >= 0x90);
        }
        char buf[8];
        if (!f_ok){
            snprintf(buf,sizeof(buf),"<%02X>",c);
            out += buf;
            i++;
        }else if (len == 1 && (c < 0x20 || c == 0x7F)){
            if (c == '\n'){ out += "\\n"; }
            else if (c == '\t'){ out += "\\t"; }
            else { snprintf(buf,sizeof(buf),"<%02X>",c); out += buf; }
            i++;
        }else{
            out.append(s,i,len);
            i += len;
        }
    }
    return out;
}

//runq's names for blocks: a12 the attention of layer 12, f12 its FFN.
static std::string BlockSpecName(int block){
    return (block % 2 ? "f" : "a") + std::to_string(block / 2);
}

bool ApplicationLLM::ApplyBlockSpec(const std::string& spec, float gain, std::string& error){
    if (!model || !model->IsLoaded()){
        error = "no model loaded";
        return false;
    }
    //every item checked before any gain changes: a typo halfway leaves the gains as they were
    int n_layers = model->config.n_layers;
    std::vector<std::pair<int,float>> changes;
    size_t start = 0;
    while (start < spec.size()){
        size_t comma = spec.find(',',start);
        std::string item = spec.substr(start,comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? spec.size() : comma + 1;
        item.erase(std::remove(item.begin(),item.end(),' '),item.end());
        if (item.empty()){
            continue;
        }
        float g = gain;
        size_t colon = item.find(':');
        if (colon != std::string::npos){
            char* end = NULL;
            g = strtof(item.c_str() + colon + 1,&end);
            if (end == item.c_str() + colon + 1 || *end != '\0'){
                error = "bad gain in \"" + item + "\"";
                return false;
            }
            item = item.substr(0,colon);
        }
        int lo = 0, hi = 0;
        int got = item.size() > 1 ? sscanf(item.c_str() + 1,"%d-%d",&lo,&hi) : 0;
        if (got == 1){
            hi = lo;
        }
        if ((item[0] != 'a' && item[0] != 'f' && item[0] != 'l') || got < 1 || lo < 0 || hi < lo || hi >= n_layers){
            error = "bad block \"" + item + "\": expected e.g. a12, f3-5 or l20, layers 0.." + std::to_string(n_layers - 1);
            return false;
        }
        for (int l = lo;l <= hi;l++){
            if (item[0] != 'f'){ changes.push_back({LLMModel::BlockIndex(l,false),g}); }
            if (item[0] != 'a'){ changes.push_back({LLMModel::BlockIndex(l,true),g}); }
        }
    }
    for (auto& change:changes){
        model->SetBlockGain(change.first,change.second);
    }
    return true;
}

json ApplicationLLM::BlocksJson(int last){
    if (!model || !model->IsLoaded()){
        return json{ {"error","no model loaded"} };
    }
    int n_blocks = model->Blocks();
    json gains = json::object();
    for (int b = 0;b < n_blocks;b++){
        if (model->BlockGain(b) != 1.0f){
            gains[BlockSpecName(b)] = model->BlockGain(b);
        }
    }
    json records = json::array();
    const std::deque<LLMBlockRecord>& all = model->block_records;
    int count = std::min(last,(int)all.size());
    double ms = 0.0;
    for (int i = (int)all.size() - count;i < (int)all.size();i++){
        const LLMBlockRecord& r = all[i];
        int n = (int)r.candidates.size();
        json candidates = json::array();
        for (int k = 0;k < n;k++){
            candidates.push_back(json{ {"token",r.candidates[k]},{"text",ShowText(tokenizer.Decode(r.token,r.candidates[k]))},
                                       {"logit",r.logit[k]},{"prob",r.prob[k]} });
        }
        //the shares as they were added, gain included; the embedding's last
        std::vector<float> chosen_share, runner_up_share;
        for (int b = 0;b <= n_blocks;b++){
            float g = b < n_blocks ? r.gain[b] : 1.0f;
            chosen_share.push_back(g * r.share[(size_t)b * n]);
            if (n > 1){
                runner_up_share.push_back(g * r.share[(size_t)b * n + 1]);
            }
        }
        records.push_back(json{
            {"context",r.context},{"pos",r.pos},
            {"input",ShowText(tokenizer.Decode(0,r.token))},{"chosen",ShowText(tokenizer.Decode(r.token,r.chosen))},
            {"candidates",candidates},
            {"rel_update",r.rel_update},{"cos_distance",r.cos_distance},
            {"share_chosen",chosen_share},{"share_runner_up",runner_up_share},
            {"check",r.check},{"rebuild_error",r.rebuild_error},{"ms",r.ms},{"cpu_ms",r.cpu_ms},{"gpu_ms",r.gpu_ms},
        });
        ms += r.ms;
    }
    return json{
        {"recording",model->f_block_stats},{"records_total",model->block_records_total},{"records_kept",(int)all.size()},
        {"gains",gains},{"ms_per_token",count > 0 ? ms / count : 0.0},
        {"layout","per block: attention of layer l at 2l, its FFN at 2l+1; rel_update before the gain, cos_distance "
                  "after it; share_* is each block's direct share of that candidate's logit with its gain, the "
                  "embedding's share last, and they add up to the logit within check"},
        {"records",records},
    };
}

//--- UI -----------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationLLM::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderLLMPanel();
    RenderChatPanel();
    RenderBlocksPanel();
    RenderVoidPanel();
}

void ApplicationLLM::RenderLLMPanel(void){
    //Laid out for the 1280x800 window the app opens with: the Engine panel on the left, this one
    //beside it, the Chat window filling the rest. imgui.ini remembers any change after that.
    ImGui::SetNextWindowPos(ImVec2(320,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420,330),ImGuiCond_FirstUseEver);
    ImGui::Begin("LLM");
    LLMGpuInfo info;
    {
        std::lock_guard<std::mutex> lock(info_mutex);
        info = gpu;
    }
    ImGui::Text("%s",info.renderer.c_str());
    ImGui::Text("GL %s",info.version.c_str());
    ImGui::Separator();
    ImGui::Text("SSBO block max   %lld MB",info.max_ssbo_block_size / (1024 * 1024));
    ImGui::Text("shared memory    %d KB",info.max_shared_memory / 1024);
    ImGui::Text("invocations      %d",info.max_invocations);
    ImGui::Text("subgroups        %s, size %d",info.f_subgroups ? "yes" : "no",info.subgroup_size);
    if (info.f_nvx_memory_info){
        ImGui::Text("VRAM             %d MB, %d MB free at start",info.vram_total_kb / 1024,info.vram_available_kb / 1024);
    }
    ImGui::Separator();
    json m;
    {
        std::lock_guard<std::mutex> lock(info_mutex);
        m = model_info;
    }
    if (m.value("loaded",false)){
        ImGui::TextWrapped("%s",m.value("path",std::string()).c_str());
        ImGui::Text("dim %d, %d layers, vocab %d, %d-bit",m.value("dim",0),m.value("n_layers",0),
                    m.value("vocab_size",0),m.value("weight_bits",0));
        ImGui::Text("%d contexts of %d positions",m.value("contexts",0),m.value("context_len",0));
        ImGui::Text("%.2f GB on the GPU, loaded in %.1f s",m.value("gpu_gb",0.0),m.value("load_seconds",0.0));
    }else{
        ImGui::Text("no model loaded");
    }
    if (ImGui::CollapsingHeader("Load a model",m.value("loaded",false) ? 0 : ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::InputText("model",load_model_path,sizeof(load_model_path));
        ImGui::InputText("tokenizer",load_tokenizer_path,sizeof(load_tokenizer_path));
        ImGui::InputInt("context",&load_context,256,1024);
        ImGui::InputInt("contexts",&load_contexts);
        load_context = std::max(64,load_context);
        load_contexts = std::max(1,std::min(4,load_contexts));
        ImGui::TextDisabled("8B on an 8 GB card: context 2048");
        if (ImGui::Button("Load")){
            f_load_request = true;
            load_status = "loading...";
        }
        if (!load_status.empty()){
            ImGui::SameLine();
            ImGui::TextWrapped("%s",load_status.c_str());
        }
    }
    ImGui::End();
}

/*
    The Chat window: a conversation with the loaded model, the reply streaming in as PreRender
    generates it. Enter sends, Ctrl+Enter starts a new line. The system prompt counts from the next
    New conversation on, since the one in use is already in the model's context.
*/
void ApplicationLLM::RenderChatPanel(void){
    ImGui::SetNextWindowPos(ImVec2(750,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520,770),ImGuiCond_FirstUseEver);
    ImGui::Begin("Chat");
    bool f_loaded = model && model->IsLoaded();
    if (!f_loaded){
        ImGui::TextWrapped("Load a model in the LLM window first.");
        ImGui::End();
        return;
    }
    if (ImGui::CollapsingHeader("System prompt")){
        ImGui::InputTextMultiline("##system",chat_system,sizeof(chat_system),ImVec2(-1,ImGui::GetTextLineHeight() * 8));
        ImGui::TextDisabled("Used from the next New conversation on.");
    }

    //the transcript, leaving room below for the input and the controls
    float footer = ImGui::GetFrameHeightWithSpacing() * 3 + ImGui::GetTextLineHeight() * 5;
    ImGui::BeginChild("transcript",ImVec2(0,-footer),true);
    for (const LLMChatSession::Message& msg:chat.messages){
        bool f_user = msg.role == "user";
        ImGui::TextColored(f_user ? ImVec4(0.55f,0.75f,1.0f,1.0f) : ImVec4(0.6f,0.9f,0.6f,1.0f),"%s",f_user ? "You" : "Model");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(msg.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (f_chat_scroll){
        ImGui::SetScrollHereY(1.0f);
        f_chat_scroll = false;
    }
    ImGui::EndChild();

    bool f_enter = ImGui::InputTextMultiline("##input",chat_input,sizeof(chat_input),ImVec2(-1,ImGui::GetTextLineHeight() * 3.5f),
                                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    if (chat.f_generating){
        if (ImGui::Button("Stop")){
            chat.Stop();
        }
    }else if ((ImGui::Button("Send") || f_enter) && chat_input[0] != '\0'){
        f_chat_send = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New conversation")){
        chat.Reset();
        chat_error.clear();
    }
    ImGui::SameLine();
    ImGui::Text("%d / %d positions",chat.Used(),model->context_len);
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("temperature",&chat.temperature,0.0f,1.5f,"%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("top-p",&chat.topp,0.5f,1.0f,"%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("max tokens",&chat.max_tokens,64);
    chat.max_tokens = std::max(1,chat.max_tokens);
    if (!chat_error.empty()){
        ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"%s",chat_error.c_str());
    }else if (!chat.messages.empty()){
        ImGui::TextDisabled("prompt %d tokens in %.2f s, reply %.1f tok/s",chat.prompt_tokens,chat.prompt_seconds,chat.tokens_per_second);
    }
    ImGui::End();
}

/*
    The Blocks window: the block analysis (see LLMModel) as a heatmap, a column per token and a
    row per block - the newest token on the right, the network read bottom to top: the
    embedding, layer 0's attention, its FFN, and so on up to the last layer. Four views of the
    same records:

      update size      |dx| / |x| as added (gain included), log scale: how much a block wrote
      turn             1 - cos(x, x + gain dx), log scale: how far it turned the stream
      push to chosen   the block's direct share of the chosen token's logit, signed
      chosen vs other  that share minus the one for another candidate - the runner-up, or the one
                       clicked below the heatmap: which blocks decided between the two

    Beside it the selected token (click a column; the newest while none is picked) as a bar per
    block on the same rows, so one token can be read exactly. Clicking a row's label switches that
    block off, and on again; the Gains tab sets any gain. The mouse wheel scrolls back in time.
*/

//Block b's value in view for record r (b == n_blocks is the embedding), or false where the view
//has none: the embedding updates nothing, and a record may lack the candidate compared with.
static bool BlockValue(const LLMBlockRecord& r, int b, int n_blocks, int view, int compare, float& v){
    int n = (int)r.candidates.size();
    float g = b < n_blocks ? r.gain[b] : 1.0f;
    if (view == BLOCKS_VIEW_UPDATE || view == BLOCKS_VIEW_TURN){
        if (b >= n_blocks){
            return false;
        }
        v = view == BLOCKS_VIEW_UPDATE ? fabsf(g) * r.rel_update[b] : r.cos_distance[b];
        return true;
    }
    if (view == BLOCKS_VIEW_VERSUS){
        if (compare < 1 || compare >= n){
            return false;
        }
        v = g * (r.share[(size_t)b * n] - r.share[(size_t)b * n + compare]);
        return true;
    }
    v = g * r.share[(size_t)b * n];
    return true;
}

/*
    The colour scales, from the dataviz palette's dark-surface steps. A magnitude is one hue,
    blue, rising from the chart surface to the ramp's lightest step, so "near nothing" recedes
    into the background. A signed share is blue (against) through a neutral gray to red (for);
    that pair was run through the palette validator for the dark surface: colour-blind
    separation dE 19, contrast above 3:1.
*/
#define BLOCKS_SURFACE 0x1a1a19
#define BLOCKS_NEUTRAL 0x383835
#define BLOCKS_AMBER 0xfab219       //the status palette's warning: a block whose gain is not 1

static ImU32 Hex(unsigned rgb){
    return IM_COL32((rgb >> 16) & 0xFF,(rgb >> 8) & 0xFF,rgb & 0xFF,255);
}

static ImU32 Mix(unsigned a, unsigned b, float t){
    auto channel = [&](int shift){
        return (int)(((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t + 0.5f);
    };
    return IM_COL32(channel(16),channel(8),channel(0),255);
}

/*
    The scale the visible tokens are drawn on, fitted to them: a magnitude on a log scale from
    its 2nd to its 98th percentile (log10 lo..hi), a signed share from -range to +range, range its
    98th percentile of |share|. Fitted rather than fixed because the blocks' sizes span two orders
    of magnitude while one block's differ from token to token by a few tens of percent; a fixed
    scale showed the first and hid the second.
*/
struct BlockScaleFit{
    float lo = -2.0f, hi = 0.5f;
    float range = 1.0f;
};

//Where v sits on the scale: 0..1 for a magnitude, -1..1 for a signed share.
static float BlockScale(int view, float v, const BlockScaleFit& fit){
    if (view >= BLOCKS_VIEW_PUSH){
        return std::max(-1.0f,std::min(1.0f,v / fit.range));
    }
    float t = (log10f(std::max(v,1e-8f)) - fit.lo) / std::max(fit.hi - fit.lo,1e-3f);
    return std::max(0.0f,std::min(1.0f,t));
}

//p as a short label: two decimals, or its order of magnitude below 1%.
static std::string ShowProb(float p){
    char buf[16];
    snprintf(buf,sizeof(buf),p >= 0.01f || p <= 0.0f ? "%.2f" : "%.0e",p);
    return buf;
}

static ImU32 BlockColour(int view, float t){
    if (view == BLOCKS_VIEW_UPDATE || view == BLOCKS_VIEW_TURN){
        static const unsigned STOPS[] = {BLOCKS_SURFACE,0x0d366b,0x184f95,0x256abf,0x3987e5,0x6da7ec,0x9ec5f4,0xcde2fb};
        const int n = sizeof(STOPS) / sizeof(STOPS[0]);
        float f = t * (n - 1);
        int i = std::min((int)f,n - 2);
        return Mix(STOPS[i],STOPS[i + 1],f - i);
    }
    return t < 0.0f ? Mix(BLOCKS_NEUTRAL,0x3987e5,-t) : Mix(BLOCKS_NEUTRAL,0xe66767,t);
}

static std::string BlockLabel(int block, int n_blocks){
    if (block >= n_blocks){
        return "embedding";
    }
    return "L" + std::to_string(block / 2) + (block % 2 ? " FFN" : " attention");
}

void ApplicationLLM::RenderBlocksPanel(void){
    //Tall and narrow, in the column between the LLM and Chat windows: the rows want height more
    //than the tokens want width.
    ImGui::SetNextWindowPos(ImVec2(248,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(484,770),ImGuiCond_FirstUseEver);
    ImGui::Begin("Blocks");
    if (!model || !model->IsLoaded()){
        ImGui::TextWrapped("Load a model in the LLM window first.");
        ImGui::End();
        return;
    }
    int n_blocks = model->Blocks();
    std::deque<LLMBlockRecord>& records = model->block_records;
    int size = (int)records.size();
    long first_id = model->block_records_total - size;     //the count of records[0]

    ImGui::Checkbox("Record",&model->f_block_stats);
    ImGui::SameLine();
    static const char* VIEWS[] = {"update size","turn","push to chosen","chosen vs other"};
    ImGui::SetNextItemWidth(140);
    ImGui::Combo("##view",&blocks_view,VIEWS,4);
    ImGui::SameLine();
    if (ImGui::Button("Clear")){
        records.clear();
        size = 0;
        first_id = model->block_records_total;
        blocks_selected = -1;
        blocks_back = 0;
    }
    ImGui::SameLine();
    double ms = 0.0;
    int recent = std::min(size,50);
    for (int i = size - recent;i < size;i++){
        ms += records[i].ms;
    }
    ImGui::Text("%d tokens, %.2f ms each",size,recent > 0 ? ms / recent : 0.0);
    int changed = 0;
    for (int b = 0;b < n_blocks;b++){
        changed += model->BlockGain(b) != 1.0f;
    }
    if (changed > 0){
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Hex(BLOCKS_AMBER)),"%d %s changed",changed,changed == 1 ? "block" : "blocks");
    }

    if (ImGui::BeginTabBar("##blocks")){
        if (ImGui::BeginTabItem("Tokens")){
            //--- geometry: the heatmap takes what the text below it leaves ---
            int rows = n_blocks + 1;
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float footer = ImGui::GetTextLineHeightWithSpacing() * 5.5f;
            float cell_h = floorf((avail.y - footer) / rows);
            cell_h = std::max(3.0f,std::min(9.0f,cell_h));
            float cell_w = 5.0f;
            float gap = cell_h >= 5.0f ? 1.0f : 0.0f;      //a line of surface between cells, where they are big enough
            float label_w = ImGui::CalcTextSize("L00").x + 10.0f;
            float bar_w = avail.x > 420.0f ? 130.0f : 0.0f;
            float bar_gap = bar_w > 0.0f ? 12.0f : 0.0f;
            int cols = std::max(1,(int)((avail.x - label_w - bar_w - bar_gap) / cell_w));
            float grid_h = rows * cell_h;
            blocks_back = std::max(0,std::min(blocks_back,size - cols));
            int last = size - 1 - blocks_back;              //the newest token shown
            int first = std::max(0,last - cols + 1);
            int sel = blocks_selected < 0 ? size - 1 : (int)(blocks_selected - first_id);
            if (sel < 0 || sel >= size){
                sel = size - 1;     //the selected token was dropped or cleared
                blocks_selected = -1;
            }
            //the scale, fitted to the visible cells (see BlockScaleFit)
            BlockScaleFit fit;
            {
                bool f_signed = blocks_view >= BLOCKS_VIEW_PUSH;
                std::vector<float> values;
                for (int i = first;i <= last;i++){
                    for (int b = 0;b <= n_blocks;b++){
                        float v;
                        if (BlockValue(records[i],b,n_blocks,blocks_view,blocks_compare,v) && (f_signed || v > 0.0f)){
                            values.push_back(f_signed ? fabsf(v) : log10f(v));
                        }
                    }
                }
                if (!values.empty()){
                    auto percentile = [&](int p){
                        size_t k = std::min(values.size() - 1,values.size() * p / 100);
                        std::nth_element(values.begin(),values.begin() + k,values.end());
                        return values[k];
                    };
                    if (f_signed){
                        fit.range = std::max(percentile(98),1e-3f);
                    }else{
                        fit.lo = percentile(2);
                        fit.hi = std::max(percentile(98),fit.lo + 0.1f);
                    }
                }
            }
            auto row_block = [&](int row){ return row < n_blocks ? n_blocks - 1 - row : n_blocks; };

            //--- the heatmap ---
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();
            float hx = origin.x + label_w;
            draw->AddRectFilled(ImVec2(hx,origin.y),ImVec2(hx + cols * cell_w,origin.y + grid_h),Hex(BLOCKS_SURFACE));
            for (int i = first;i <= last;i++){
                float cx = hx + (i - first) * cell_w;
                for (int row = 0;row < rows;row++){
                    float v;
                    if (BlockValue(records[i],row_block(row),n_blocks,blocks_view,blocks_compare,v)){
                        float y = origin.y + row * cell_h;
                        draw->AddRectFilled(ImVec2(cx,y),ImVec2(cx + cell_w - 1.0f,y + cell_h - gap),
                                            BlockColour(blocks_view,BlockScale(blocks_view,v,fit)));
                    }
                }
            }
            ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            if (sel >= first && sel <= last){
                float cx = hx + (sel - first) * cell_w;
                draw->AddRect(ImVec2(cx - 1.0f,origin.y - 1.0f),ImVec2(cx + cell_w,origin.y + grid_h + 1.0f),ink);
            }
            //labels every fifth layer, and a mark beside every block whose gain is not 1. The
            //embedding's row has a label only where it has values, and then takes L0's place,
            //which would overlap it.
            float text_h = ImGui::GetTextLineHeight();
            bool f_embedding_row = blocks_view >= BLOCKS_VIEW_PUSH;
            for (int l = 0;l < model->config.n_layers;l++){
                bool f_label = l % 5 == 0 || l == model->config.n_layers - 1;
                if (f_label && !(l == 0 && f_embedding_row)){
                    float y = origin.y + (n_blocks - 1 - 2 * l) * cell_h;  //the line between its two rows
                    draw->AddText(ImVec2(origin.x,y - text_h * 0.5f),muted,("L" + std::to_string(l)).c_str());
                }
            }
            if (f_embedding_row){
                draw->AddText(ImVec2(origin.x,origin.y + n_blocks * cell_h + cell_h * 0.5f - text_h * 0.5f),muted,"emb");
            }
            for (int b = 0;b < n_blocks;b++){
                if (model->BlockGain(b) != 1.0f){
                    float y = origin.y + (n_blocks - 1 - b) * cell_h;
                    draw->AddRectFilled(ImVec2(hx - 6.0f,y),ImVec2(hx - 2.0f,y + std::max(cell_h - gap,2.0f)),Hex(BLOCKS_AMBER));
                }
            }

            //hover, click and wheel over the labels and the cells
            ImGui::InvisibleButton("##heatmap",ImVec2(label_w + cols * cell_w,grid_h));
            if (ImGui::IsItemHovered()){
                ImVec2 m = ImGui::GetIO().MousePos;
                int row = std::max(0,std::min(rows - 1,(int)((m.y - origin.y) / cell_h)));
                int b = row_block(row);
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f){
                    blocks_back = std::max(0,std::min(size - cols,blocks_back + (int)(wheel * 4.0f)));
                }
                if (m.x < hx){
                    if (b < n_blocks){
                        ImGui::SetTooltip("%s, gain %.2f\nclick: switch it %s",BlockLabel(b,n_blocks).c_str(),model->BlockGain(b),
                                          model->BlockGain(b) != 1.0f ? "back on (gain 1)" : "off (gain 0)");
                        if (ImGui::IsItemClicked()){
                            model->SetBlockGain(b,model->BlockGain(b) != 1.0f ? 1.0f : 0.0f);
                        }
                    }
                }else{
                    int i = first + (int)((m.x - hx) / cell_w);
                    if (i >= first && i <= last){
                        const LLMBlockRecord& r = records[i];
                        int n = (int)r.candidates.size();
                        std::string tip = BlockLabel(b,n_blocks);
                        char buf[256];
                        if (b < n_blocks){
                            snprintf(buf,sizeof(buf),", gain %.2f\nupdate |dx|/|x| %.4f%s\nturn 1-cos %.5f",r.gain[b],r.rel_update[b],
                                     r.gain[b] != 1.0f ? " before the gain" : "",r.cos_distance[b]);
                            tip += buf;
                        }
                        float g = b < n_blocks ? r.gain[b] : 1.0f;
                        snprintf(buf,sizeof(buf),"\nshare of the logit of '%s': %+.3f",ShowText(tokenizer.Decode(r.token,r.chosen)).c_str(),
                                 g * r.share[(size_t)b * n]);
                        tip += buf;
                        if (blocks_compare >= 1 && blocks_compare < n){
                            snprintf(buf,sizeof(buf),"\nshare of the logit of '%s': %+.3f",
                                     ShowText(tokenizer.Decode(r.token,r.candidates[blocks_compare])).c_str(),g * r.share[(size_t)b * n + blocks_compare]);
                            tip += buf;
                        }
                        snprintf(buf,sizeof(buf),"\n\npos %d: '%s' -> '%s'",r.pos,ShowText(tokenizer.Decode(0,r.token)).c_str(),
                                 ShowText(tokenizer.Decode(r.token,r.chosen)).c_str());
                        tip += buf;
                        ImGui::SetTooltip("%s",tip.c_str());
                        if (ImGui::IsItemClicked()){
                            blocks_selected = i == size - 1 ? -1 : first_id + i;    //the newest keeps following
                        }
                    }
                }
            }

            //--- the selected token, a bar per block on the heatmap's rows ---
            if (bar_w > 0.0f && sel >= 0){
                const LLMBlockRecord& r = records[sel];
                float bx = hx + cols * cell_w + bar_gap;
                bool f_signed = blocks_view >= BLOCKS_VIEW_PUSH;
                float zero = f_signed ? bx + bar_w * 0.5f : bx;
                draw->AddRectFilled(ImVec2(bx,origin.y),ImVec2(bx + bar_w,origin.y + grid_h),Hex(BLOCKS_SURFACE));
                for (int row = 0;row < rows;row++){
                    float v;
                    if (BlockValue(r,row_block(row),n_blocks,blocks_view,blocks_compare,v)){
                        float t = BlockScale(blocks_view,v,fit);
                        float y = origin.y + row * cell_h;
                        float end = zero + t * (f_signed ? bar_w * 0.5f : bar_w);
                        draw->AddRectFilled(ImVec2(std::min(zero,end),y),ImVec2(std::max(zero,end),y + cell_h - gap),BlockColour(blocks_view,t));
                    }
                }
                if (f_signed){
                    draw->AddLine(ImVec2(zero,origin.y),ImVec2(zero,origin.y + grid_h),muted);
                }
                ImGui::SetCursorScreenPos(ImVec2(bx,origin.y));
                ImGui::InvisibleButton("##bars",ImVec2(bar_w,grid_h));
                if (ImGui::IsItemHovered()){
                    int row = std::max(0,std::min(rows - 1,(int)((ImGui::GetIO().MousePos.y - origin.y) / cell_h)));
                    float v;
                    if (BlockValue(r,row_block(row),n_blocks,blocks_view,blocks_compare,v)){
                        ImGui::SetTooltip("%s: %.4g",BlockLabel(row_block(row),n_blocks).c_str(),v);
                    }
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x,origin.y + grid_h + ImGui::GetStyle().ItemSpacing.y));

            //--- the legend ---
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                float lw = 140.0f, lh = text_h * 0.7f;
                const int steps = 28;
                for (int s = 0;s < steps;s++){
                    float t = (s + 0.5f) / steps;
                    if (blocks_view >= BLOCKS_VIEW_PUSH){
                        t = t * 2.0f - 1.0f;
                    }
                    draw->AddRectFilled(ImVec2(p.x + lw * s / steps,p.y + (text_h - lh) * 0.5f),
                                        ImVec2(p.x + lw * (s + 1) / steps,p.y + (text_h + lh) * 0.5f),BlockColour(blocks_view,t));
                }
                ImGui::Dummy(ImVec2(lw,text_h));
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                if (blocks_view == BLOCKS_VIEW_UPDATE){
                    ImGui::TextDisabled("|dx| / |x| as added, %.2g to %.2g, log scale",powf(10.0f,fit.lo),powf(10.0f,fit.hi));
                }else if (blocks_view == BLOCKS_VIEW_TURN){
                    ImGui::TextDisabled("1 - cos(x, x + dx), %.2g to %.2g, log scale",powf(10.0f,fit.lo),powf(10.0f,fit.hi));
                }else if (blocks_view == BLOCKS_VIEW_PUSH){
                    ImGui::TextDisabled("direct share of the chosen token's logit, -%.2f to +%.2f (red: for it)",fit.range,fit.range);
                }else{
                    ImGui::TextDisabled("share for the chosen token minus for the other, -%.2f to +%.2f",fit.range,fit.range);
                }
                ImGui::PopTextWrapPos();
            }

            //--- the selected token: its candidates, and the text around it ---
            if (sel < 0){
                ImGui::TextWrapped("Tick Record and let the model write: the Chat window, llm_generate or llm_duet. "
                                   "Every token it samples gets a column.");
            }else{
                const LLMBlockRecord& r = records[sel];
                int n = (int)r.candidates.size();
                ImGui::Text("pos %d%s", r.pos,blocks_selected < 0 ? " (newest)" : "");
                for (int k = 0;k < n;k++){
                    char label[160];
                    snprintf(label,sizeof(label),"'%s' %s##candidate%d",ShowText(tokenizer.Decode(r.token,r.candidates[k])).c_str(),
                             ShowProb(r.prob[k]).c_str(),k);
                    ImVec2 size_k = ImGui::CalcTextSize(label,NULL,true);
                    ImGui::SameLine();
                    if (ImGui::GetContentRegionAvail().x < size_k.x){
                        ImGui::NewLine();
                    }
                    bool f_marked = k == 0 || (blocks_view == BLOCKS_VIEW_VERSUS && k == blocks_compare);
                    if (ImGui::Selectable(label,f_marked,0,size_k) && k > 0){
                        blocks_compare = k;
                        blocks_view = BLOCKS_VIEW_VERSUS;
                    }
                    if (ImGui::IsItemHovered()){
                        ImGui::SetTooltip(k == 0 ? "the chosen token, logit %.2f" : "logit %.2f - click to compare the chosen token with this one",r.logit[k]);
                    }
                }
                //the text around it: the tokens before and after it in the same context
                int a = sel, z = sel;
                while (a > 0 && sel - a < 60 && records[a - 1].context == r.context && records[a - 1].pos == records[a].pos - 1){
                    a--;
                }
                while (z < size - 1 && z - sel < 15 && records[z + 1].context == r.context && records[z + 1].pos == records[z].pos + 1){
                    z++;
                }
                std::string before, after;
                for (int i = a;i < sel;i++){
                    before += tokenizer.Decode(records[i].token,records[i].chosen);
                }
                for (int i = sel + 1;i <= z;i++){
                    after += tokenizer.Decode(records[i].token,records[i].chosen);
                }
                std::string text = (a > 0 ? "..." : "") + ShowText(before) + " [" + ShowText(tokenizer.Decode(r.token,r.chosen)) + "] " + ShowText(after);
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s",text.c_str());
                ImGui::PopStyleColor();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("shares add up to the logit within %.1e, rebuilt stream off by %.1e; recording took %.2f ms "
                                    "(CPU %.2f, GPU %.3f)",r.check,r.rebuild_error,r.ms,r.cpu_ms,r.gpu_ms);
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gains")){
            ImGui::TextWrapped("A gain scales a block's update before it joins the residual stream: 1 as trained, 0 switched off "
                               "(runq's -x). It acts from the next token on. Tokens already in the context keep the keys and "
                               "values the old gains made, so start a new conversation to see the effect on all of it.");
            ImGui::SetNextItemWidth(170);
            ImGui::InputTextWithHint("##spec","a12, f3-5, l20, a7:0.5",blocks_spec,sizeof(blocks_spec));
            ImGui::SameLine();
            if (ImGui::Button("Switch off")){
                blocks_spec_error.clear();
                ApplyBlockSpec(blocks_spec,0.0f,blocks_spec_error);
            }
            ImGui::SameLine();
            if (ImGui::Button("Switch on")){
                blocks_spec_error.clear();
                ApplyBlockSpec(blocks_spec,1.0f,blocks_spec_error);
            }
            ImGui::SameLine();
            if (ImGui::Button("All on")){
                for (int b = 0;b < n_blocks;b++){
                    model->SetBlockGain(b,1.0f);
                }
            }
            if (!blocks_spec_error.empty()){
                ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"%s",blocks_spec_error.c_str());
            }
            ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##gains",3,flags,ImVec2(0,ImGui::GetContentRegionAvail().y))){
                ImGui::TableSetupScrollFreeze(0,1);
                ImGui::TableSetupColumn("layer");
                ImGui::TableSetupColumn("attention");
                ImGui::TableSetupColumn("FFN");
                ImGui::TableHeadersRow();
                for (int l = 0;l < model->config.n_layers;l++){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("L%d",l);
                    for (int f = 0;f < 2;f++){
                        ImGui::TableNextColumn();
                        int b = LLMModel::BlockIndex(l,f == 1);
                        float g = model->BlockGain(b);
                        ImGui::SetNextItemWidth(180);
                        ImGui::PushID(b);
                        if (ImGui::SliderFloat("##gain",&g,0.0f,2.0f,"%.2f")){
                            model->SetBlockGain(b,g);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
#endif //USE_IMGUI

//--- MCP ----------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationLLM::RegisterMCPTools(void){
    MCPServer::Get()->RegisterTool("llm_status",
        "What this GL implementation offers a compute workload: the largest shader storage block, "
        "shared memory per workgroup, workgroup limits, subgroup support, VRAM, and the "
        "compute-relevant extensions. These decide how a model can be laid out on the GPU.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            (void)args;
            json result;
            result["gpu"] = GpuInfoJson();
            std::lock_guard<std::mutex> lock(info_mutex);
            result["model"] = model_info;
            return result;
        });

    MCPServer::Get()->RegisterTool("llm_load",
        "Load a runq .bin checkpoint (e.g. H:/llama_c/llama2.c/ministral3_3b_instruct_q40e8.bin) and "
        "its tokenizer (e.g. H:/llama_c/llama2.c/ministral3_tokenizer.bin) onto the GPU: every matrix "
        "into a buffer of its own, the compute kernels, and `contexts` KV caches of `context` "
        "positions each - a duet needs 2. Replaces a model already loaded. Returns the model's "
        "shape and how much GPU memory it took. For Ministral 3 8B on an 8 GB card, use context 2048.",
        json{
            {"type","object"},
            {"properties", {
                {"path", {{"type","string"},{"description","the .bin file, an absolute path"}}},
                {"tokenizer", {{"type","string"},{"description","the model's tokenizer .bin, an absolute path"}}},
                {"context", {{"type","integer"},{"description","positions per KV cache, default the model's seq_len (4096)"}}},
                {"contexts", {{"type","integer"},{"description","how many KV caches, default 2"}}}
            }},
            {"required",json::array({"path","tokenizer"})}
        },
        [this](const json& args) -> json {
            std::string file = args.value("path",std::string());
            std::string tok = args.value("tokenizer",std::string());
            int context = args.value("context",0);
            int contexts = args.value("contexts",2);
            return RunOnRenderThread([this,file,tok,context,contexts]() -> json {
                return LoadModel(file,tok,context,contexts);
            },180000);
        });

    MCPServer::Get()->RegisterTool("llm_generate",
        "Generate text from a prompt, as runq's generate mode: the prompt is continued, so wrap it as "
        "[INST]...[/INST] for an instruct model's answer. `steps` counts the prompt too. Returns the "
        "text and the speed of the prompt and of the generation.",
        json{
            {"type","object"},
            {"properties", {
                {"prompt", {{"type","string"}}},
                {"steps", {{"type","integer"},{"description","positions in total, prompt included, default 256"}}},
                {"temperature", {{"type","number"},{"description","default 0 (greedy)"}}},
                {"topp", {{"type","number"},{"description","default 0.9"}}},
                {"seed", {{"type","integer"},{"description","default 1"}}}
            }},
            {"required",json::array({"prompt"})}
        },
        [this](const json& args) -> json {
            LLMGenerateArgs g;
            g.prompt = args.value("prompt",std::string());
            g.steps = args.value("steps",256);
            g.temperature = args.value("temperature",0.0f);
            g.topp = args.value("topp",0.9f);
            g.seed = args.value("seed",1);
            return RunOnRenderThread([this,g]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                InvalidateChats();
                return LLMGenerate(*model,tokenizer,g);
            },300000);
        });

    MCPServer::Get()->RegisterTool("llm_loss",
        "Score a text the way runq's loss mode does: the mean -log probability of its tokens, and the "
        "perplexity. Documents are separated by lines \"---\"; in a chat transcript only the replies "
        "(after each [/INST]) are scored. Pass `file` (e.g. H:/llama_c/llama2.c/eval/chasm_ministral.txt) "
        "or `text`.",
        json{
            {"type","object"},
            {"properties", {
                {"file", {{"type","string"},{"description","a text file, an absolute path"}}},
                {"text", {{"type","string"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string text = args.value("text",std::string());
            if (args.contains("file")){
                std::string error;
                if (!ReadTextFile(args.value("file",std::string()),text,error)){
                    return json{ {"error",error} };
                }
            }
            return RunOnRenderThread([this,text]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                InvalidateChats();
                return LLMLoss(*model,tokenizer,text);
            },600000);
        });

    MCPServer::Get()->RegisterTool("llm_duet",
        "Two characters talk, as runq's duet mode: the same weights, a KV cache each. `scenario` is a "
        "file holding the two (a name line, then the system prompt; the two separated by a line "
        "\"---\"), e.g. H:/llama_c/llama2.c/duet/chasm_report.txt. With `tools` (a file with a JSON "
        "list per character, separated by \"---\"), the talk ends after `lines` lines or at a tool "
        "call, and then each character that has not called one is made to. Returns the transcript "
        "in runq's format, the calls, and the speed.",
        json{
            {"type","object"},
            {"properties", {
                {"scenario", {{"type","string"},{"description","the scenario file, an absolute path"}}},
                {"tools", {{"type","string"},{"description","the tools file, an absolute path, optional"}}},
                {"opening", {{"type","string"},{"description","the first character's first line, optional"}}},
                {"lines", {{"type","integer"},{"description","default 10"}}},
                {"max_tokens", {{"type","integer"},{"description","per line, default 120"}}},
                {"temperature", {{"type","number"},{"description","default 0.15"}}},
                {"topp", {{"type","number"},{"description","default 0.9"}}},
                {"seed", {{"type","integer"},{"description","default 1"}}}
            }},
            {"required",json::array({"scenario"})}
        },
        [this](const json& args) -> json {
            LLMDuetArgs d;
            std::string error;
            if (!ReadTextFile(args.value("scenario",std::string()),d.scenario,error)){
                return json{ {"error",error} };
            }
            if (args.contains("tools") && !ReadTextFile(args.value("tools",std::string()),d.tools,error)){
                return json{ {"error",error} };
            }
            d.opening = args.value("opening",std::string());
            d.lines = args.value("lines",10);
            d.max_tokens = args.value("max_tokens",120);
            d.temperature = args.value("temperature",0.15f);
            d.topp = args.value("topp",0.9f);
            d.seed = args.value("seed",1);
            return RunOnRenderThread([this,d]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                InvalidateChats();
                return LLMDuet(*model,tokenizer,d);
            },600000);
        });

    MCPServer::Get()->RegisterTool("llm_chat",
        "Say something in the Chat window's conversation and wait for the model's reply - the same "
        "path the window uses, so this is also how to test it. The reply is generated a few tokens per "
        "frame, as in the window. `reset` (or a new `system` prompt) starts a new conversation first. "
        "Returns the reply, the speed and how full the conversation is; include_screenshot adds a "
        "picture of the window.",
        json{
            {"type","object"},
            {"properties", {
                {"message", {{"type","string"}}},
                {"system", {{"type","string"},{"description","a system prompt; starts a new conversation"}}},
                {"reset", {{"type","boolean"},{"description","start a new conversation first"}}},
                {"temperature", {{"type","number"}}},
                {"topp", {{"type","number"}}},
                {"max_tokens", {{"type","integer"}}},
                {"include_screenshot", {{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string message = args.value("message",std::string());
            json start = RunOnRenderThread([this,args,message]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                if (args.value("reset",false) || args.contains("system")){
                    chat.Reset();
                    std::string system = args.value("system",std::string());
                    snprintf(chat_system,sizeof(chat_system),"%s",system.c_str());
                }
                if (args.contains("temperature")){ chat.temperature = args.value("temperature",0.15f); }
                if (args.contains("topp")){ chat.topp = args.value("topp",0.9f); }
                if (args.contains("max_tokens")){ chat.max_tokens = args.value("max_tokens",512); }
                if (message.empty()){
                    return json{ {"reset",true} };
                }
                chat.system_prompt = chat_system;
                std::string error;
                if (!chat.Send(*model,tokenizer,message,error)){
                    return json{ {"error",error} };
                }
                f_chat_scroll = true;
                return json{ {"started",true} };
            },60000);
            if (start.contains("error") || message.empty()){
                return start;
            }
            //PreRender generates the reply a slice per frame; ask every 50 ms whether it is done
            for (int waited = 0;waited < 300000;waited += 50){
                Sleep(50);
                json state = RunOnRenderThread([this]() -> json { return json{ {"generating",chat.f_generating} }; },10000);
                if (!state.value("generating",true)){
                    break;
                }
            }
            json result = RunOnRenderThread([this]() -> json {
                std::string reply = chat.messages.empty() ? std::string() : chat.messages.back().text;
                return json{ {"reply",reply},{"generating",chat.f_generating},{"tokens_per_second",chat.tokens_per_second},
                             {"prompt_tokens",chat.prompt_tokens},{"prompt_seconds",chat.prompt_seconds},
                             {"used",chat.Used()},{"context_len",model ? model->context_len : 0} };
            },10000);
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("llm_unload",
        "Free the model's GPU memory. The compiled kernels are kept.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            (void)args;
            return RunOnRenderThread([this]() -> json {
                chat.Reset();
                void_chat.Reset();
                if (model){
                    model->Unload();
                }
                return json{ {"unloaded",true} };
            },30000);
        });

    MCPServer::Get()->RegisterTool("llm_reload_kernels",
        "Recompile every compute kernel from its .comp in apps/llm/assets/shaders - the edit-and-"
        "rerun loop, no rebuild or restart. A kernel that fails keeps its last working program; "
        "the compile log says which and why.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            (void)args;
            return RunOnRenderThread([this]() -> json {
                if (!model){
                    model = new LLMModel();
                }
                std::string log;
                bool f_ok = model->ReloadKernels(log);
                return json{ {"ok",f_ok},{"compile_log",log} };
            },60000);
        });

    MCPServer::Get()->RegisterTool("llm_test_matrix",
        "Check one matrix's GPU kernel against the CPU: W x for nb random inputs, the CPU decoding the "
        "weights straight from the file the way runq does. nb 1 tests the one-token kernel "
        "(llm_matvec_*), more the batch kernel (llm_matmat_*). qkv and w13 are the matrices packed at "
        "load, checked row by row against the file's wq/wk/wv and w1/w3. Reports the largest "
        "difference and the largest value, so the difference can be judged relative to the "
        "numbers. Expect float rounding (1e-6 relative), not exact equality.",
        json{
            {"type","object"},
            {"properties", {
                {"matrix", {{"type","string"},{"description","qkv w13 wo w2 or wcls"}}},
                {"layer", {{"type","integer"},{"description","layer index, ignored for wcls"}}},
                {"nb", {{"type","integer"},{"description","inputs, 1 (default) to 32"}}},
                {"seed", {{"type","integer"},{"description","seed of the random inputs, default 1"}}}
            }},
            {"required",json::array({"matrix"})}
        },
        [this](const json& args) -> json {
            std::string name = args.value("matrix",std::string());
            int layer = args.value("layer",0);
            int nb = args.value("nb",1);
            unsigned seed = (unsigned)args.value("seed",1);
            return RunOnRenderThread([this,name,layer,nb,seed]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                double max_diff = 0.0, max_ref = 0.0;
                std::string error;
                if (!model->TestMatrix(name,layer,nb,seed,max_diff,max_ref,error)){
                    return json{ {"error",error} };
                }
                return json{ {"matrix",name},{"layer",layer},{"nb",nb},{"max_abs_diff",max_diff},{"max_abs_value",max_ref},
                             {"relative",max_ref > 0.0 ? max_diff / max_ref : 0.0} };
            },300000);
        });

    MCPServer::Get()->RegisterTool("llm_forward_tokens",
        "Run token ids through the model from position 0 and return the top logits after the last "
        "one, plus the speed. One token at a time (the generation path), or with batch in batches "
        "of up to 32 (the prompt path). The raw check of the forward pass, before any tokenizer is "
        "involved: compare the top tokens with what runq predicts for the same ids "
        "(scratchpad/ref_forward in llama_c).",
        json{
            {"type","object"},
            {"properties", {
                {"tokens", {{"type","array"},{"items",{{"type","integer"}}},{"description","token ids, starting with BOS (1)"}}},
                {"top", {{"type","integer"},{"description","how many of the highest logits to return, default 5"}}},
                {"batch", {{"type","boolean"},{"description","feed them in batches (the prompt path) instead of one by one"}}},
                {"profile", {{"type","boolean"},{"description","time every dispatch and return GPU ms per token by kernel, plus CPU issue and readback time"}}},
                {"fold_norm", {{"type","boolean"},{"description","experiment: true folds the rmsnorms into the matrix kernels instead of running them as dispatches of their own (default false)"}}}
            }},
            {"required",json::array({"tokens"})}
        },
        [this](const json& args) -> json {
            std::vector<int> tokens = args.value("tokens",std::vector<int>());
            int top = args.value("top",5);
            bool f_batch = args.value("batch",false);
            bool f_fold = args.value("fold_norm",false);
            bool f_profile = args.value("profile",false);
            return RunOnRenderThread([this,tokens,top,f_batch,f_profile,f_fold]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                if (tokens.empty() || (int)tokens.size() > model->context_len){
                    return json{ {"error","need 1 to context_len tokens"} };
                }
                model->f_profile = f_profile;
                model->f_fold_norm = f_fold;
                InvalidateChats();
                model->ResetProfile();
                const float* logits = NULL;
                auto start = std::chrono::steady_clock::now();
                if (f_batch){
                    logits = model->ForwardTokens(tokens.data(),(int)tokens.size(),0);
                }else{
                    for (size_t i = 0;i < tokens.size() && (i == 0 || logits);i++){
                        logits = model->Forward(tokens[i],(int)i);
                    }
                }
                double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                model->f_profile = false;
                model->f_fold_norm = false;
                if (!logits){
                    return json{ {"error","the forward pass refused a token or position"} };
                }
                json profile;
                if (f_profile && model->profile_calls > 0){
                    double n = (double)tokens.size();
                    json kernels = json::object();
                    double gpu_total = 0.0;
                    for (auto& entry:model->profile_gpu_ms){
                        kernels[entry.first] = entry.second / n;
                        gpu_total += entry.second / n;
                    }
                    profile = json{ {"gpu_ms_per_token_by_kernel",kernels},{"gpu_ms_per_token",gpu_total},
                                    {"cpu_issue_ms_per_token",model->profile_issue_ms / n},
                                    {"cpu_readback_wait_ms_per_token",model->profile_readback_ms / n} };
                }
                std::vector<int> order(model->config.vocab_size);
                for (int i = 0;i < (int)order.size();i++){ order[i] = i; }
                int k = std::min(top,(int)order.size());
                std::partial_sort(order.begin(),order.begin() + k,order.end(),
                                  [logits](int a, int b){ return logits[a] > logits[b]; });
                json best = json::array();
                for (int i = 0;i < k;i++){
                    best.push_back(json{ {"token",order[i]},{"logit",logits[order[i]]} });
                }
                json result = json{ {"top",best},{"tokens",(int)tokens.size()},{"seconds",seconds},
                                    {"tokens_per_second",tokens.size() / seconds} };
                if (!profile.is_null()){
                    result["profile"] = profile;
                }
                return result;
            },300000);
        });

    MCPServer::Get()->RegisterTool("llm_blocks",
        "The block analysis, runq's -o statistics and -x ablation per token: with recording on, every "
        "token the model samples (llm_generate, llm_duet, the chat) gets, per attention and FFN block, "
        "the size of its update to the residual stream (rel_update, |dx|/|x|), how far it turned it "
        "(cos_distance), and its direct share of the logits of the chosen token and the runner-up. "
        "The shares plus the embedding's add up to the logit (check). `spec` sets gains in runq's -x "
        "syntax (a12, f3-5, l20, a7:0.5) to `gain`, default 0 = switched off; gains act from the next "
        "token on. Returns the state and the newest `last` records; include_screenshot shows the "
        "Blocks window.",
        json{
            {"type","object"},
            {"properties", {
                {"record", {{"type","boolean"},{"description","turn recording on or off"}}},
                {"clear", {{"type","boolean"},{"description","drop the records kept so far"}}},
                {"reset_gains", {{"type","boolean"},{"description","every gain back to 1, before spec"}}},
                {"spec", {{"type","string"},{"description","blocks whose gain to set: a12, f3-5, l20, comma-separated, each optionally :gain"}}},
                {"gain", {{"type","number"},{"description","the gain for spec items without one, default 0"}}},
                {"last", {{"type","integer"},{"description","how many of the newest records to return, default 1, at most 64"}}},
                {"view", {{"type","string"},{"description","the window's view: update, turn, push or versus"}}},
                {"include_screenshot", {{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            json result = RunOnRenderThread([this,args]() -> json {
                if (!model || !model->IsLoaded()){
                    return json{ {"error","no model loaded"} };
                }
                if (args.contains("record")){ model->f_block_stats = args.value("record",false); }
                if (args.value("clear",false)){
                    model->block_records.clear();
                    blocks_selected = -1;
                    blocks_back = 0;
                }
                if (args.value("reset_gains",false)){
                    for (int b = 0;b < model->Blocks();b++){
                        model->SetBlockGain(b,1.0f);
                    }
                }
                if (args.contains("spec")){
                    std::string error;
                    if (!ApplyBlockSpec(args.value("spec",std::string()),args.value("gain",0.0f),error)){
                        return json{ {"error",error} };
                    }
                }
                std::string view = args.value("view",std::string());
                if (view == "update"){ blocks_view = BLOCKS_VIEW_UPDATE; }
                else if (view == "turn"){ blocks_view = BLOCKS_VIEW_TURN; }
                else if (view == "push"){ blocks_view = BLOCKS_VIEW_PUSH; }
                else if (view == "versus"){ blocks_view = BLOCKS_VIEW_VERSUS; }
                return BlocksJson(std::max(0,std::min(64,args.value("last",1))));
            },30000);
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });
}
#endif //USE_MCP
