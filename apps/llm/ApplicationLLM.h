#ifndef _APPLICATION_LLM_H_
#define _APPLICATION_LLM_H_

#include "Application.h"
#include "LLMModel.h"
#include "LLMTokenizer.h"
#include "LLMChat.h"
#include "VoidBrief.h"
#include "BackgroundWork.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/*
    llm - a bench for running a small language model in GL compute shaders.

    The model is Ministral 3 (3B or 8B) in the quantized .bin format of the llama_c project's
    runq, which is also the CPU reference this app is checked against: same file, same
    tokenizer, and the loss of a text and greedy output should agree with runq's within the
    rounding of fp32 versus runq's int8 activations. Nothing here is bit-exact with runq, and
    that is expected rather than a bug to chase.

    Four windows: LLM (the GPU's limits, the model, loading one), Chat (a conversation with
    the model, the reply streaming in), Blocks (what each attention and FFN block did for
    every token, and switching blocks off) and Void (a running Chasm read into a brief, and the
    model advising on it - docs/void_plan.md). The llm_* MCP tools do the same and more:
    generate, score a text against runq's loss, run a two-character duet, check a kernel
    against the CPU, profile, read the block analysis; the void_* tools drive the Void.

    --- WHY IN THE ENGINE, AND NOT A STANDALONE TOOL -----------------------------------------
    Whatever runs here runs in a game unchanged: the same GL 4.5 context, the same Shader class
    (BuildCompute, and Reload so a .comp is edited and re-run without a restart), the same
    #version the engine prepends. And the MCP server is what lets an agent load a model, run a
    prompt, time it and compare it with runq without anyone at the keyboard.

    --- WHERE THE THREADS ARE -----------------------------------------------------------------
    Every GL call happens on the RENDER thread: Init, PreRender and DrawImGuiUI. The llm_* MCP
    tool handlers run on their own threads and touch no GL. Anything that needs the model is
    handed to the render thread as a job (RunOnRenderThread) and run whole in PreRender - a
    long job, a load or a duet, simply holds that one frame back, which in a bench is fine and
    keeps the model single-threaded without a lock of its own.
*/

/*
    Work handed from an MCP thread to the render thread. The result is a json, because that is
    what every tool returns anyway.
*/
struct LLMJob{
    std::function<json()> work;
    json result;
    bool f_done = false;    //behind job_mutex
};

/*
    What the GL implementation offers a compute workload, read once in Init.

    These are the numbers that decide how the model can be laid out: whether a whole tensor
    fits in one shader storage block (the 3B's embedding table is 400 MB), how much shared
    memory a workgroup may have, and whether the subgroup operations the reductions would like
    to use exist at all.
*/
struct LLMGpuInfo{
    std::string vendor, renderer, version, glsl_version;
    long long max_ssbo_block_size = 0;      //bytes, GL_MAX_SHADER_STORAGE_BLOCK_SIZE
    int max_ssbo_bindings = 0;              //GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS
    int ssbo_offset_alignment = 0;          //GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT
    int max_shared_memory = 0;              //bytes, GL_MAX_COMPUTE_SHARED_MEMORY_SIZE
    int max_invocations = 0;                //GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
    int max_group_size[3] = {0,0,0};        //GL_MAX_COMPUTE_WORK_GROUP_SIZE
    int max_group_count[3] = {0,0,0};       //GL_MAX_COMPUTE_WORK_GROUP_COUNT
    bool f_subgroups = false;               //GL_KHR_shader_subgroup
    int subgroup_size = 0;                  //GL_SUBGROUP_SIZE_KHR
    unsigned int subgroup_features = 0;     //GL_SUBGROUP_SUPPORTED_FEATURES_KHR bits
    bool f_nvx_memory_info = false;         //GL_NVX_gpu_memory_info
    int vram_total_kb = 0;                  //GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
    int vram_available_kb = 0;              //GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, at start
    std::vector<std::string> extensions;    //the ones of interest to compute, not all of them
};

class ApplicationLLM : public Application{
public:
    ApplicationLLM();

    void Init(void) override;
    void PreRender(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif

private:
    //Written once by Init on the render thread, read by the panel and by llm_status.
    std::mutex info_mutex;
    LLMGpuInfo gpu;
    //A copy of what the panel and llm_status show about the model, refreshed by the render
    //thread after every job, so neither has to touch the model itself. Behind info_mutex.
    json model_info;

    //RENDER THREAD ONLY. Created on the first load and kept: unloading frees the GPU memory but
    //keeps the compiled kernels.
    LLMModel* model = NULL;
    LLMTokenizer tokenizer;     //RENDER THREAD ONLY, loaded with the model

    /*
        The Chat window's conversation, and what the panel asks of the render thread. All of it is
        RENDER THREAD ONLY: the panel (DrawImGuiUI) and PreRender both run there, and the llm_chat
        tool reaches it through RunOnRenderThread. A reply is generated a few tokens per frame in
        PreRender, so the window shows it as it grows.

        Every job that runs the model on other contexts calls chat.Invalidate(): the chat lives in
        the last context, which a duet also uses, and an invalidated chat feeds its conversation
        again before it carries on.
    */
    LLMChatSession chat;
    char chat_system[4096] = "";
    char chat_input[4096] = "";
    bool f_chat_send = false;           //the panel's Send, serviced in PreRender
    bool f_chat_scroll = false;         //scroll the transcript to the end on the next frame
    std::string chat_error;
    //Every job that feeds the model calls this: both conversations feed theirs again before going on.
    void InvalidateChats(){ chat.Invalidate(); void_chat.Invalidate(); }

    /*
        --- THE VOID (docs/void_plan.md) ----------------------------------------------------------
        A running Chasm read over its MCP tools into a brief and a menu (VoidBrief), and the model
        asked to choose one option and say why, as the Void. It only advises: nothing is sent back
        to the game.

        Reading is blocking HTTP, so it runs on the background worker (LatestResult), never on the
        render thread that runs the model; the void_read tool reads on its own MCP thread instead.
        The answer is a conversation of its own, started fresh for every question, in context 0
        while the Chat keeps the last - and never generated in the same frame as the Chat's reply,
        as both read the model's one logits buffer.
    */
    int void_port = 8769;                   //Chasm's MCP port
    bool f_void_auto = false;               //read again every void_auto_seconds
    float void_auto_seconds = 5.0f;
    LatestResult<VoidBrief> void_brief;     //Request and Adopt on the render thread
    std::chrono::steady_clock::time_point void_requested;
    char void_system_path[512] = "";
    char void_system[8192] = "";            //the Void's system prompt, editable in its window
    std::string void_system_status;
    /*
        The brief as it will be asked, editable in the window: the latest read until someone changes
        it, and from then on their text, which later reads leave alone. RENDER THREAD ONLY.
    */
    std::string void_edit;
    bool f_void_edited = false;
    std::chrono::steady_clock::time_point void_edit_from;  //the read it was taken from
    std::string void_edit_date;
    void VoidFollowRead();                  //RENDER THREAD: an unedited brief takes the latest read
    LLMChatSession void_chat;               //RENDER THREAD ONLY
    std::vector<std::string> void_asked_options;    //the menu the reply answers, as the asked text has it
    std::string void_asked_date;
    std::string void_asked_text;
    bool f_void_asked_edited = false;
    bool f_void_ask = false;                //the window's Ask, serviced in PreRender
    std::string void_error;
    void VoidInit();                        //from Init: the system prompt's file, loaded
    void VoidRequestRead();                 //RENDER THREAD
    void VoidUpdate();                      //RENDER THREAD, from PreRender
    bool VoidAsk(std::string& error);       //RENDER THREAD
    json VoidAnswerJson();                  //RENDER THREAD: the reply, the letter in it and its option
    bool VoidLoadSystem(std::string& error);
    bool VoidSaveSystem(std::string& error);

    //The LLM window's load controls, serviced in PreRender like a job.
    char load_model_path[512] = "H:/llama_c/llama2.c/ministral3_3b_instruct_q40e8.bin";
    char load_tokenizer_path[512] = "H:/llama_c/llama2.c/ministral3_tokenizer.bin";
    int load_context = 4096;
    int load_contexts = 2;
    bool f_load_request = false;
    std::string load_status;
    json LoadModel(const std::string& path, const std::string& tok, int context, int contexts);   //RENDER THREAD

    //Init only. RENDER THREAD - it is all glGet*.
    void ReadGpuInfo();
    json GpuInfoJson();
    json ModelInfoJson();       //RENDER THREAD

    //--- jobs ---
    std::mutex job_mutex;
    std::deque<std::shared_ptr<LLMJob>> jobs;
    /*
        Queue work for the render thread and wait for it. NOT callable from the render thread
        itself, which would wait for a frame it is holding up. On a timeout the job still runs
        when its turn comes; only the answer is lost, and the error says so.
    */
    json RunOnRenderThread(std::function<json()> work, int timeout_ms);

    /*
        The Blocks window: the model's block analysis (see LLMModel), token by token. RENDER
        THREAD ONLY, like the records it draws.
    */
    int blocks_view = 2;            //BLOCKS_VIEW_*: push to chosen, the most direct "who wrote this token"
    long blocks_selected = -1;      //the selected token, as a block_records_total count; -1 follows the newest
    int blocks_back = 0;            //how many tokens the heatmap is scrolled back from the newest
    int blocks_compare = 1;         //the candidate the chosen token is compared with: the runner-up
    char blocks_spec[128] = "";
    std::string blocks_spec_error;
    //runq's -x syntax: a12, f3-5, l20, comma-separated, each optionally :gain (default gain).
    bool ApplyBlockSpec(const std::string& spec, float gain, std::string& error);
    json BlocksJson(int last);      //for llm_blocks: the newest `last` records

#ifdef USE_IMGUI
    void RenderLLMPanel();
    void RenderChatPanel();
    void RenderBlocksPanel();
    void RenderVoidPanel();
#endif

#ifdef USE_MCP
    void RegisterMCPTools();
    void RegisterVoidTools();
#endif
};

#endif
