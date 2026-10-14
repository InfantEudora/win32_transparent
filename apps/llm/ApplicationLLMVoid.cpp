#include "ApplicationLLM.h"
#include "VoidBrief.h"
#include "Debug.h"
#include "File.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
    The Void (docs/void_plan.md): its window, reading Chasm, asking the model, and the void_* tools.
    The brief itself is VoidBrief's; this is the plumbing around it.
*/

static Debugger* debug = new Debugger("Void",DEBUG_ALL);

#define VOID_MCP_TIMEOUT_MS     10000   //per call; chasm_economy on a big colony is the slow one
#define VOID_BUDGET_MS          25.0    //generation per frame, as the Chat's
#define VOID_SYSTEM_FILE        "void_system_strict.txt"    //in apps/llm/void, beside the step-0 briefs

typedef std::chrono::steady_clock Clock;

//--- talking to Chasm ---------------------------------------------------------------------------

/*
    One MCP tool call to an app on this machine: a JSON-RPC POST to 127.0.0.1:port/mcp, blocking, the
    reply read until the server closes the connection - which MCPServer does after every answer. The
    tool's text is parsed as JSON, which is what every Chasm tool the brief reads returns.

    App-side, standing in for a core MCP client until the Void is decided on. Any thread but the
    render thread, which runs the model: a call takes as long as Chasm takes to answer.
*/
static bool McpCall(int port, const std::string& tool, const json& args, json& result, std::string& error){
#ifdef USE_NET
    static std::once_flag wsa_once;
    std::call_once(wsa_once,[](){
        WSADATA data;
        WSAStartup(MAKEWORD(2,2),&data);    //reference counted: harmless beside the MCP server's own
    });
    SOCKET s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s == INVALID_SOCKET){
        error = "no socket (" + std::to_string(WSAGetLastError()) + ")";
        return false;
    }
    DWORD timeout = VOID_MCP_TIMEOUT_MS;
    setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
    //127.0.0.1 and never localhost: the server binds IPv4 only, and ::1 first costs two seconds a call.
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s,(sockaddr*)&addr,sizeof(addr)) == SOCKET_ERROR){
        closesocket(s);
        error = "nothing answers on 127.0.0.1:" + std::to_string(port) + " - is Chasm running with --mcp-port " + std::to_string(port) + "?";
        return false;
    }
    std::string body = json{ {"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",{{"name",tool},{"arguments",args}}} }.dump();
    std::string request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nContent-Type: application/json\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    for (size_t sent = 0;sent < request.size();){
        int n = send(s,request.data() + sent,(int)(request.size() - sent),0);
        if (n <= 0){
            closesocket(s);
            error = "sending " + tool + " failed";
            return false;
        }
        sent += (size_t)n;
    }
    std::string reply;
    char buf[16384];
    int n;
    while ((n = recv(s,buf,sizeof(buf),0)) > 0){
        reply.append(buf,n);
    }
    closesocket(s);
    size_t head = reply.find("\r\n\r\n");
    json rpc = head == std::string::npos ? json() : json::parse(reply.substr(head + 4),nullptr,false);
    if (rpc.is_discarded() || !rpc.is_object()){
        error = head == std::string::npos ? tool + " did not answer in time" : tool + " answered something that is not JSON";
        return false;
    }
    if (rpc.contains("error")){
        const json& e = rpc["error"];
        error = tool + ": " + (e.is_object() ? e.value("message",std::string("failed")) : e.dump());
        return false;
    }
    std::string text;
    const json& content = rpc["result"]["content"];
    for (const json& c:content.is_array() ? content : json::array()){
        if (c.is_object() && c.value("type",std::string()) == "text"){
            text += c.value("text",std::string());
        }
    }
    result = json::parse(text,nullptr,false);
    if (result.is_discarded()){
        error = tool + ": " + text.substr(0,200);
        return false;
    }
    return true;
#else
    (void)port; (void)tool; (void)args; (void)result;
    error = "this build has no sockets (USE_NET=0)";
    return false;
#endif
}

/*
    A whole read: the four replies the brief is made of, then the brief. Captures nothing live, so it
    runs on the background worker or an MCP thread alike. A failed read keeps the last brief with the
    error beside it, so the window still shows what was there and the next read still has something to
    compare with.
*/
static VoidBrief VoidRead(int port, std::shared_ptr<const VoidBrief> last){
    VoidState state;
    std::string error;
    if (!McpCall(port,"chasm_time",json::object(),state.time,error)
        || !McpCall(port,"chasm_economy",json{ {"op","status"} },state.economy,error)
        || !McpCall(port,"chasm_issues",json::object(),state.issues,error)){
        VoidBrief failed = last ? *last : VoidBrief();
        failed.error = error;
        failed.at = Clock::now();
        return failed;
    }
    //A Chasm from before the zombies has no chasm_zombie, and one from before the Void no op buildings: less
    //to tell of, rather than no brief.
    std::string optional_error;
    if (!McpCall(port,"chasm_zombie",json{ {"op","status"} },state.zombies,optional_error)){
        state.zombies = json::object();
    }
    if (!McpCall(port,"chasm_economy",json{ {"op","buildings"} },state.buildings,optional_error)
        || !state.buildings.is_object() || !state.buildings.contains("buildings")){
        state.buildings = json::object();   //an older Chasm answers an unknown op with the status
    }
    return VoidBuildBrief(state,last.get());
}

void ApplicationLLM::VoidRequestRead(void){
    int port = void_port;
    std::shared_ptr<const VoidBrief> last = void_brief.Get();
    void_requested = Clock::now();
    void_brief.Request([port,last](){ return VoidRead(port,last); });
}

//--- the system prompt --------------------------------------------------------------------------

bool ApplicationLLM::VoidLoadSystem(std::string& error){
    FILE* f = fopen(void_system_path,"rb");
    if (!f){
        error = std::string("cannot open ") + void_system_path;
        return false;
    }
    size_t n = fread(void_system,1,sizeof(void_system) - 1,f);
    void_system[n] = '\0';
    bool f_whole = fgetc(f) == EOF;
    fclose(f);
    if (!f_whole){
        error = "the file is longer than the " + std::to_string(sizeof(void_system) - 1) + " characters the editor holds; only its start was loaded";
        return false;
    }
    return true;
}

bool ApplicationLLM::VoidSaveSystem(std::string& error){
    FILE* f = fopen(void_system_path,"wb");
    if (!f){
        error = std::string("cannot write ") + void_system_path;
        return false;
    }
    size_t n = strlen(void_system);
    bool f_ok = fwrite(void_system,1,n,f) == n;
    fclose(f);
    if (!f_ok){
        error = std::string("writing ") + void_system_path + " failed";
    }
    return f_ok;
}

//--- the brief as it will be asked -------------------------------------------------------------

void ApplicationLLM::VoidFollowRead(void){
    std::shared_ptr<const VoidBrief> b = void_brief.Get();
    if (!f_void_edited && b->f_read && b->at != void_edit_from){
        void_edit = b->text;
        void_edit_from = b->at;
        void_edit_date = b->date;
    }
}

//The menu as a text has it - the lines "A. ...", "B. ..." after WHAT YOU CAN ADVISE - so an edited menu is read as edited.
static std::vector<std::string> MenuOptions(const std::string& text){
    std::vector<std::string> out;
    size_t at = text.rfind("WHAT YOU CAN ADVISE:");
    for (size_t pos = (at == std::string::npos) ? at : text.find('\n',at);pos != std::string::npos;){
        size_t end = text.find('\n',pos + 1);
        std::string line = text.substr(pos + 1,end == std::string::npos ? std::string::npos : end - pos - 1);
        while (!line.empty() && isspace((unsigned char)line.back())){
            line.pop_back();
        }
        if (line.size() > 2 && line[0] == (char)('A' + out.size()) && line[1] == '.'){
            size_t a = line.find_first_not_of(' ',2);
            out.push_back(a == std::string::npos ? std::string() : line.substr(a));
        }
        pos = end;
    }
    return out;
}

//--- asking -------------------------------------------------------------------------------------

/*
    A fresh conversation every time: the system prompt and the brief, as step 0 sent them through the
    Chat. Nothing of an earlier answer is carried over; the brief is what the Void knows - the window's,
    edited or not.
*/
bool ApplicationLLM::VoidAsk(std::string& error){
    if (!model || !model->IsLoaded()){
        error = "no model loaded";
        return false;
    }
    if (chat.f_generating){
        error = "the Chat window is still answering";
        return false;
    }
    VoidFollowRead();       //a read published since the last frame, by void_ask's own read
    if (void_edit.empty()){
        std::shared_ptr<const VoidBrief> b = void_brief.Get();
        error = b->error.empty() ? "no brief: read Chasm, or write one" : b->error;
        return false;
    }
    void_chat.Reset();
    void_chat.system_prompt = void_system;
    if (void_chat.Context(*model) == chat.Context(*model)){
        chat.Invalidate();      //one context between them: the Chat feeds its conversation again next time
    }
    if (!void_chat.Send(*model,tokenizer,void_edit,error)){
        return false;
    }
    void_asked_text = void_edit;
    void_asked_options = MenuOptions(void_edit);
    void_asked_date = void_edit_date;
    f_void_asked_edited = f_void_edited;
    return true;
}

json ApplicationLLM::VoidAnswerJson(void){
    std::string reply = void_chat.messages.empty() ? std::string() : void_chat.messages.back().text;
    //The letter: the first thing on the reply's first line, past any markup - the 8B writes **C**.
    size_t i = 0;
    while (i < reply.size() && (isspace((unsigned char)reply[i]) || reply[i] == '*' || reply[i] == '#')){
        i++;
    }
    int choice = -1;
    if (i < reply.size() && reply[i] >= 'A' && reply[i] <= 'Z' && (i + 1 == reply.size() || !isalpha((unsigned char)reply[i + 1]))){
        choice = reply[i] - 'A';
    }
    size_t eol = reply.find('\n',i);
    std::string words = eol == std::string::npos ? std::string() : reply.substr(eol + 1);
    size_t a = words.find_first_not_of(" \t\r\n");
    words = a == std::string::npos ? std::string() : words.substr(a,words.find_last_not_of(" \t\r\n") - a + 1);
    json j{ {"reply",reply},{"words",words},{"generating",void_chat.f_generating},{"brief_date",void_asked_date},
            {"asked",void_asked_text},{"edited",f_void_asked_edited},
            {"prompt_tokens",void_chat.prompt_tokens},{"prompt_seconds",void_chat.prompt_seconds},
            {"tokens_per_second",void_chat.tokens_per_second} };
    if (choice >= 0 && choice < (int)void_asked_options.size()){
        j["choice"] = std::string(1,(char)('A' + choice));
        j["option"] = void_asked_options[choice];
    }else{
        j["choice"] = "";
        j["note"] = "no letter from the menu at the start of the reply";
    }
    return j;
}

void ApplicationLLM::VoidUpdate(void){
    void_brief.Adopt();
    VoidFollowRead();
    double since = std::chrono::duration<double>(Clock::now() - void_requested).count();
    if (f_void_auto && !void_brief.IsBusy() && since >= void_auto_seconds){
        VoidRequestRead();
    }
    if (f_void_ask){
        f_void_ask = false;
        void_error.clear();
        VoidAsk(void_error);
    }
    //Never in the same frame as the Chat's reply: both sample from the model's one logits buffer.
    if (void_chat.f_generating && !chat.f_generating && model && model->IsLoaded()){
        void_chat.Step(*model,tokenizer,VOID_BUDGET_MS);
    }
}

//--- the window ---------------------------------------------------------------------------------

#ifdef USE_IMGUI
//A multiline editor on a std::string that grows as it is typed into, as imgui_stdlib does (not built here).
static int ResizeString(ImGuiInputTextCallbackData* data){
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize){
        std::string* s = (std::string*)data->UserData;
        s->resize(data->BufTextLen);
        data->Buf = (char*)s->c_str();
    }
    return 0;
}

static bool InputTextMultilineString(const char* label, std::string& s, const ImVec2& size){
    return ImGui::InputTextMultiline(label,(char*)s.c_str(),s.capacity() + 1,size,
                                     ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_WordWrap,ResizeString,&s);
}

/*
    The Void window: where Chasm is, the brief - as last read, or as edited here - why the Void would speak
    now, its system prompt (edited here, loaded from and saved to a file), and its answer with the letter
    read out of it.
*/
void ApplicationLLM::RenderVoidPanel(void){
    ImGui::SetNextWindowPos(ImVec2(320,360),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420,430),ImGuiCond_FirstUseEver);
    ImGui::Begin("Void");
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Chasm port",&void_port);
    void_port = std::max(1,std::min(65535,void_port));
    ImGui::SameLine();
    if (void_brief.IsBusy()){
        ImGui::TextDisabled("reading...");
    }else if (ImGui::Button("Read")){
        VoidRequestRead();
    }
    ImGui::Checkbox("read every",&f_void_auto);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("s",&void_auto_seconds,1.0f,5.0f,"%.0f");
    void_auto_seconds = std::max(1.0f,void_auto_seconds);

    std::shared_ptr<const VoidBrief> b = void_brief.Get();
    double age = std::chrono::duration<double>(Clock::now() - b->at).count();
    if (!b->error.empty()){
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"%s",b->error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (b->f_read){
        ImGui::Text("%s, read %.0f s ago",b->date.c_str(),age);
        ImGui::PushTextWrapPos(0.0f);
        if (b->reasons.empty()){
            ImGui::TextDisabled("Calm: nothing calls for the Void.");
        }else{
            std::string why;
            for (const std::string& r:b->reasons){
                why += (why.empty() ? "" : "; ") + r;
            }
            ImGui::TextColored(ImVec4(1.0f,0.8f,0.4f,1.0f),"Would speak: %s",why.c_str());
        }
        ImGui::PopTextWrapPos();
    }else if (b->error.empty()){
        ImGui::TextDisabled("Nothing read yet. Start Chasm with --mcp-port %d, then Read.",void_port);
    }
    //Always there, read or not: a brief can be written or pasted by hand and asked without a Chasm.
    if (ImGui::CollapsingHeader("Brief",ImGuiTreeNodeFlags_DefaultOpen)){
        if (InputTextMultilineString("##brief",void_edit,ImVec2(-1,ImGui::GetTextLineHeight() * 14))){
            f_void_edited = true;
        }
        if (ImGui::SmallButton("Copy brief")){
            ImGui::SetClipboardText(void_edit.c_str());
        }
        if (f_void_edited && b->f_read){
            ImGui::SameLine();
            if (ImGui::SmallButton("Back to the latest read")){
                f_void_edited = false;
                void_edit_from = Clock::time_point();
                VoidFollowRead();
            }
        }
        if (f_void_edited){
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(b->f_read && b->at != void_edit_from ? "Edited, and kept: Chasm has been read since, and the reads are not taken in."
                                                                    : "Edited, and kept: later reads leave it alone.");
            ImGui::PopTextWrapPos();
        }
    }

    if (ImGui::CollapsingHeader("System prompt")){
        ImGui::InputText("file",void_system_path,sizeof(void_system_path));
        if (ImGui::Button("Load")){
            void_system_status.clear();
            if (VoidLoadSystem(void_system_status)){
                void_system_status = "loaded";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")){
            void_system_status.clear();
            if (VoidSaveSystem(void_system_status)){
                void_system_status = "saved";
            }
        }
        if (!void_system_status.empty()){
            ImGui::SameLine();
            ImGui::TextWrapped("%s",void_system_status.c_str());
        }
        ImGui::InputTextMultiline("##void_system",void_system,sizeof(void_system),ImVec2(-1,ImGui::GetTextLineHeight() * 10),
                                  ImGuiInputTextFlags_WordWrap);
        ImGui::TextDisabled("Used from the next Ask on.");
    }

    ImGui::Separator();
    bool f_loaded = model && model->IsLoaded();
    if (void_chat.f_generating){
        if (ImGui::Button("Stop")){
            void_chat.Stop();
        }
    }else if (!f_loaded){
        ImGui::TextDisabled("Load a model in the LLM window to ask the Void.");
    }else if (ImGui::Button("Ask the Void")){
        f_void_ask = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("temperature",&void_chat.temperature,0.0f,1.0f,"%.2f");
    if (!void_error.empty()){
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"%s",void_error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!void_chat.messages.empty()){
        json answer = VoidAnswerJson();
        if (void_asked_date.empty()){
            ImGui::TextDisabled("on a brief written by hand");
        }else{
            ImGui::TextDisabled("on the brief of %s%s",void_asked_date.c_str(),f_void_asked_edited ? ", edited" : "");
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.75f,0.65f,1.0f,1.0f),"%s",void_chat.messages.back().text.c_str());
        if (!void_chat.f_generating){
            std::string choice = answer.value("choice",std::string());
            if (choice.empty()){
                ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"No letter from the menu.");
            }else{
                ImGui::TextDisabled("%s: %s",choice.c_str(),answer.value("option",std::string()).c_str());
            }
            ImGui::TextDisabled("prompt %d tokens in %.2f s, reply %.1f tok/s",void_chat.prompt_tokens,void_chat.prompt_seconds,
                                void_chat.tokens_per_second);
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
#endif //USE_IMGUI

//--- MCP ----------------------------------------------------------------------------------------

#ifdef USE_MCP
//The brief as a tool returns it.
static json VoidBriefJson(const VoidBrief& b){
    json j{ {"read",b.f_read},{"date",b.date},{"brief",b.text},{"options",b.options},{"reasons",b.reasons},
            {"calm",b.f_read && b.reasons.empty()} };
    if (!b.error.empty()){
        j["error"] = b.error;
    }
    return j;
}

void ApplicationLLM::RegisterVoidTools(void){
    MCPServer::Get()->RegisterTool("void_read",
        "Read the running Chasm now (chasm_time, chasm_economy status and buildings, chasm_issues, chasm_zombie) and write the "
        "Void's brief: the sections and the lettered menu the model is sent, the reasons the Void would speak "
        "now (none: calm), and the date. `port` also becomes the Void window's port. Nothing is asked of the model.",
        json{
            {"type","object"},
            {"properties", {
                {"port", {{"type","integer"},{"description","Chasm's MCP port, default the window's (8769)"}}}
            }}
        },
        [this](const json& args) -> json {
            int port = RunOnRenderThread([this,args]() -> json {
                if (args.contains("port")){
                    void_port = args.value("port",void_port);
                }
                return json{ {"port",void_port} };
            },10000).value("port",8769);
            std::shared_ptr<const VoidBrief> b = std::make_shared<const VoidBrief>(VoidRead(port,void_brief.Get()));
            void_brief.Set(b);
            return VoidBriefJson(*b);
        });

    MCPServer::Get()->RegisterTool("void_ask",
        "Ask the Void about the brief, as the window's Ask does: a fresh conversation of its system prompt and the "
        "brief, the reply generated a few tokens a frame. By default Chasm is read first and that brief asked, "
        "replacing any edit in the window. `brief` asks on this text instead, as an edit (its menu is read from its "
        "own \"A. ...\" lines); read false asks on the window's brief as it stands, edited or not. Returns the reply, "
        "the letter at its start and the option it names, the words after it, the text asked, and the speed. "
        "`system` replaces the system prompt first, for this and later asks (not saved to its file - void_system "
        "does that).",
        json{
            {"type","object"},
            {"properties", {
                {"read", {{"type","boolean"},{"description","read Chasm first, default true unless brief is given"}}},
                {"brief", {{"type","string"},{"description","the brief to ask on, as an edit"}}},
                {"system", {{"type","string"}}},
                {"temperature", {{"type","number"},{"description","default the window's (0)"}}}
            }}
        },
        [this](const json& args) -> json {
            bool f_read = args.value("read",!args.contains("brief"));
            if (f_read){
                int port = RunOnRenderThread([this]() -> json { return json{ {"port",void_port} }; },10000).value("port",8769);
                std::shared_ptr<const VoidBrief> b = std::make_shared<const VoidBrief>(VoidRead(port,void_brief.Get()));
                void_brief.Set(b);
                if (!b->error.empty()){
                    return VoidBriefJson(*b);
                }
            }
            json start = RunOnRenderThread([this,args,f_read]() -> json {
                if (args.contains("brief")){
                    void_edit = args.value("brief",std::string());
                    void_edit_date.clear();     //not a read's: written elsewhere
                    f_void_edited = true;
                }else if (f_read){
                    f_void_edited = false;      //the read just made is what is asked
                }
                if (args.contains("system")){
                    snprintf(void_system,sizeof(void_system),"%s",args.value("system",std::string()).c_str());
                }
                if (args.contains("temperature")){
                    void_chat.temperature = args.value("temperature",0.0f);
                }
                std::string error;
                if (!VoidAsk(error)){
                    return json{ {"error",error} };
                }
                return json{ {"started",true} };
            },60000);
            if (start.contains("error")){
                return start;
            }
            //PreRender generates the reply a slice per frame; ask every 50 ms whether it is done
            for (int waited = 0;waited < 300000;waited += 50){
                Sleep(50);
                json state = RunOnRenderThread([this]() -> json { return json{ {"generating",void_chat.f_generating} }; },10000);
                if (!state.value("generating",true)){
                    break;
                }
            }
            json answer = RunOnRenderThread([this]() -> json { return VoidAnswerJson(); },10000);
            answer["brief"] = VoidBriefJson(*void_brief.Get());
            return answer;
        });

    MCPServer::Get()->RegisterTool("void_system",
        "The Void's system prompt: returns it and its file; `text` replaces it, `save` writes it to the file "
        "(`file` first changes which), `load` reads it from the file.",
        json{
            {"type","object"},
            {"properties", {
                {"text", {{"type","string"}}},
                {"file", {{"type","string"},{"description","an absolute path"}}},
                {"save", {{"type","boolean"}}},
                {"load", {{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            return RunOnRenderThread([this,args]() -> json {
                if (args.contains("file")){
                    snprintf(void_system_path,sizeof(void_system_path),"%s",args.value("file",std::string()).c_str());
                }
                std::string error;
                if (args.value("load",false) && !VoidLoadSystem(error)){
                    return json{ {"error",error} };
                }
                if (args.contains("text")){
                    snprintf(void_system,sizeof(void_system),"%s",args.value("text",std::string()).c_str());
                }
                if (args.value("save",false) && !VoidSaveSystem(error)){
                    return json{ {"error",error} };
                }
                return json{ {"file",void_system_path},{"text",void_system} };
            },10000);
        });
}
#endif //USE_MCP

//Init's part: where the system prompt lives, and loading it.
void ApplicationLLM::VoidInit(void){
    snprintf(void_system_path,sizeof(void_system_path),"%s/../void/%s",GetExecutableDirectory().c_str(),VOID_SYSTEM_FILE);
    if (!VoidLoadSystem(void_system_status)){
        debug->Warn("The Void's system prompt: %s\n",void_system_status.c_str());
    }
    void_chat.context = 0;          //the Chat keeps the last
    void_chat.temperature = 0.0f;   //the same brief, the same answer: wordings compare
    void_chat.max_tokens = 160;
}
