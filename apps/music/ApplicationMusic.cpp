#include "ApplicationMusic.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include "Debug.h"
#include "File.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

static Debugger* debug = new Debugger("ApplicationMusic", DEBUG_ALL);

ApplicationMusic::ApplicationMusic():Application(){
    //Nothing in the scene is worth inspecting - it is empty on purpose. The Engine window stays for
    //frame timing, which is the first place a music engine that got too expensive would show.
    f_show_scene_window = false;
    f_show_inspector_window = false;
    f_show_engine_window = true;
}

void ApplicationMusic::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->f_render_skybox = false;
    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    main_window->Resize(1280,800);
    main_scene = CreateNewScene("Music");

    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    if (!soundsystem->f_initialised){
        //Not fatal: an offline render needs no device, and that is half of what this bench is for.
        debug->Warn("No sound device - live playback is off, music_render still works\n");
    }
    LoadAndPlay(score_file);

#ifdef USE_MCP
    RegisterMCPTools();
#endif
}

bool ApplicationMusic::LoadAndPlay(const std::string& file){
    auto score = std::make_shared<MusicScore>();
    std::string error;
    if (!LoadMusicScore(file.c_str(), *score, error)){
        //The old score keeps playing: a typo in the JSON should be a message, not silence.
        score_error = error;
        debug->Err("%s\n", error.c_str());
        return false;
    }
    score_error.clear();
    score_file = file;
    player.Start(soundsystem, score, 1);
    return true;
}

void ApplicationMusic::PreRender(void){
    std::lock_guard<std::mutex> lock(request_mutex);
    if (!f_request_pending) return;
    f_request_ok = LoadAndPlay(requested_file);
    f_request_pending = false;
    request_cv.notify_all();
}

bool ApplicationMusic::RequestReload(const std::string& file, std::string& error, int timeout_ms){
    std::unique_lock<std::mutex> lock(request_mutex);
    requested_file = file;
    f_request_pending = true;
    if (!request_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]{ return !f_request_pending; })){
        error = "the render thread did not get to it in time";
        return false;
    }
    if (!f_request_ok) error = score_error;
    return f_request_ok;
}

//--- state ---------------------------------------------------------------------------------

namespace {
std::string KeyName(int root, int mode){
    return MusicPitchClassName(root) + " " + MusicModeName(mode);
}
}

json ApplicationMusic::StateJson(){
    const MusicStatus st = player.GetStatus();
    const MusicParams p = player.GetParams();
    const std::shared_ptr<const MusicScore> score = player.GetScore();

    json j;
    j["score"] = score ? score->name : "";
    j["score_file"] = score_file;
    if (!score_error.empty()) j["score_error"] = score_error;
    j["playing"] = player.IsPlaying();
    j["key"] = KeyName(st.root_pc, st.mode);
    if (st.pending_root_pc >= 0 || st.pending_mode >= 0){
        j["pending_key"] = KeyName(st.pending_root_pc >= 0 ? st.pending_root_pc : st.root_pc,
                                   st.pending_mode >= 0 ? st.pending_mode : st.mode);
    }
    j["bar"] = st.bar;
    j["beat"] = st.beat;
    j["time_s"] = st.time_s;
    j["params"] = {{"suspense", p.suspense}, {"bpm", p.bpm}, {"master", p.master},
                   {"bed_gain", p.bed_gain}, {"voice_gain", p.voice_gain}};
    json beds = json::array();
    for (size_t i = 0; i < st.bed_names.size(); i++){
        beds.push_back({{"name", st.bed_names[i]}, {"gain", st.bed_gains[i]}, {"transpose", st.bed_transpose[i]}});
    }
    j["beds"] = beds;
    json voices = json::array(), stingers = json::array();
    if (score){
        for (const MusicVoiceDef& v : score->voices){
            voices.push_back({{"name", v.name}, {"sample", v.sample},
                              {"register", MusicNoteName(v.low_midi) + ".." + MusicNoteName(v.high_midi)},
                              {"density_calm", v.density_calm}, {"density_tense", v.density_tense}});
        }
        for (const MusicStingerDef& s : score->stingers) stingers.push_back(s.name);
    }
    j["voices"] = voices;
    j["stingers"] = stingers;
    json recent = json::array();
    for (const MusicNoteLog& n : st.recent){
        recent.push_back({{"t", n.time_s}, {"voice", n.voice}, {"note", MusicNoteName(n.midi)}, {"tension", n.f_tension}});
    }
    j["notes"] = {{"sounding", st.notes_sounding}, {"total", st.notes_total}, {"recent", recent}};
    j["output"] = {{"peak_db", st.peak_db}, {"rms_db", st.rms_db}, {"clipped", st.clipped}};
    return j;
}

bool ApplicationMusic::KeyEventFromJson(const json& args, int current_root, MusicEvent& e, std::string& error){
    e = MusicEvent();
    e.type = MusicEvent::KEY;
    if (args.contains("root") && args["root"].is_string()){
        e.root_pc = MusicPitchClassFromName(args["root"].get<std::string>());
        if (e.root_pc < 0){ error = "root must be a note name like A or F#"; return false; }
    }
    if (args.contains("shift") && args["shift"].is_number_integer()){
        const int base = e.root_pc >= 0 ? e.root_pc : current_root;
        e.root_pc = ((base + args["shift"].get<int>()) % 12 + 12) % 12;
    }
    if (args.contains("mode") && args["mode"].is_string()){
        e.mode = MusicModeFromName(args["mode"].get<std::string>());
        if (e.mode < 0){ error = "mode must be one of pentatonic_minor, aeolian, dorian, phrygian, pentatonic_major, ionian"; return false; }
    }
    if (e.root_pc < 0 && e.mode < 0){ error = "give a root, a shift or a mode"; return false; }
    e.f_now = args.contains("now") && args["now"].is_boolean() && args["now"].get<bool>();
    return true;
}

//--- offline render ------------------------------------------------------------------------

std::string ApplicationMusic::RendersDirectory(){
    //Beside the app's own folder, found from the exe the same way the asset roots are.
    std::string dir = GetExecutableDirectory() + "/../renders";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

json ApplicationMusic::RenderJson(const std::string& file, double seconds, const MusicParams& p, uint32_t seed,
                                  const std::vector<MusicPlayer::TimedEvent>& timeline){
    std::string name = file.empty() ? std::string("render.wav") : file;
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".wav") != 0) name += ".wav";
    //A bare file name only: this writes where it is told, and "where" is not an MCP argument.
    name = std::filesystem::path(name).filename().string();
    const std::string path = RendersDirectory() + "/" + name;

    MusicStatus st;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    if (!player.RenderToFile(path, seconds, p, seed, timeline, st, error)) return json{{"error", error}};
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::error_code ec;
    json j;
    j["path"] = std::filesystem::weakly_canonical(path, ec).generic_string();
    j["seconds"] = seconds;
    j["seed"] = seed;
    j["rendered_in_s"] = took;
    j["final_key"] = KeyName(st.root_pc, st.mode);
    j["notes_total"] = st.notes_total;
    j["clipped"] = st.clipped;
    j["last_second"] = {{"peak_db", st.peak_db}, {"rms_db", st.rms_db}};
    return j;
}

//--- panel ---------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationMusic::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderMusicPanel();
}

void ApplicationMusic::RenderMusicPanel(void){
    ImGui::SetNextWindowPos(ImVec2(340,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460,760),ImGuiCond_FirstUseEver);
    ImGui::Begin("Music");
    ImGui::PushItemWidth(-140.0f);

    const MusicStatus st = player.GetStatus();
    MusicParams p = player.GetParams();
    const std::shared_ptr<const MusicScore> score = player.GetScore();

    //--- score ----------------------------------------------------------------------------
    ImGui::Text("Score: %s (%s)", score ? score->name.c_str() : "-", score_file.c_str());
    ImGui::SameLine();
    //The panel IS the render thread, so it can reload directly rather than through a request.
    if (ImGui::Button("Reload")) LoadAndPlay(score_file);
    if (!score_error.empty()) ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", score_error.c_str());
    if (!player.IsPlaying()) ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "Not playing - no sound device?");

    ImGui::Text("Key: %s", KeyName(st.root_pc, st.mode).c_str());
    if (st.pending_root_pc >= 0 || st.pending_mode >= 0){
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), "-> %s at the next bar",
                           KeyName(st.pending_root_pc >= 0 ? st.pending_root_pc : st.root_pc,
                                   st.pending_mode >= 0 ? st.pending_mode : st.mode).c_str());
    }
    ImGui::Text("Bar %d beat %d   %.1f s", st.bar + 1, st.beat + 1, st.time_s);
    ImGui::Separator();

    //--- the knobs ------------------------------------------------------------------------
    bool f_changed = false;
    ImGui::PushItemWidth(-140.0f);
    f_changed |= ImGui::SliderFloat("Suspense", &p.suspense, 0.0f, 1.0f, "%.2f");
    f_changed |= ImGui::SliderFloat("Tempo (bpm)", &p.bpm, 30.0f, 120.0f, "%.0f");
    f_changed |= ImGui::SliderFloat("Master", &p.master, 0.0f, 1.5f, "%.2f");
    f_changed |= ImGui::SliderFloat("Beds", &p.bed_gain, 0.0f, 2.0f, "%.2f");
    f_changed |= ImGui::SliderFloat("Voices", &p.voice_gain, 0.0f, 2.0f, "%.2f");
    ImGui::PopItemWidth();
    if (f_changed) player.SetParams(p);

    //--- key ------------------------------------------------------------------------------
    static int key_root = -1, key_mode = -1;
    if (key_root < 0){ key_root = st.root_pc; key_mode = st.mode; }
    if (ImGui::BeginCombo("Root", MusicPitchClassName(key_root).c_str())){
        for (int r = 0; r < 12; r++) if (ImGui::Selectable(MusicPitchClassName(r).c_str(), r == key_root)) key_root = r;
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Mode", MusicModeName(key_mode))){
        for (int m = 0; m < MUSIC_MODE_COUNT; m++) if (ImGui::Selectable(MusicModeName(m), m == key_mode)) key_mode = m;
        ImGui::EndCombo();
    }
    MusicEvent e;
    e.type = MusicEvent::KEY;
    if (ImGui::Button("Change at next bar")){ e.root_pc = key_root; e.mode = key_mode; player.Post(e); }
    ImGui::SameLine();
    if (ImGui::Button("Now")){ e.root_pc = key_root; e.mode = key_mode; e.f_now = true; player.Post(e); }
    ImGui::SameLine();
    if (ImGui::Button("Up a 5th")){ e.root_pc = (st.root_pc + 7) % 12; player.Post(e); }
    ImGui::SameLine();
    if (ImGui::Button("Down a 5th")){ e.root_pc = (st.root_pc + 5) % 12; player.Post(e); }

    //--- stingers -------------------------------------------------------------------------
    if (score && !score->stingers.empty()){
        ImGui::Separator();
        ImGui::Text("Stingers:");
        for (const MusicStingerDef& s : score->stingers){
            ImGui::SameLine();
            if (ImGui::Button(s.name.c_str())){
                MusicEvent t;
                t.type = MusicEvent::STINGER;
                t.name = s.name;
                player.Post(t);
            }
        }
    }

    //--- beds -----------------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Beds");
    for (size_t i = 0; i < st.bed_names.size(); i++){
        char label[64];
        snprintf(label, sizeof label, "%.2f", st.bed_gains[i]);
        ImGui::ProgressBar(std::min(1.0f, st.bed_gains[i]), ImVec2(120, 0), label);
        ImGui::SameLine();
        if (st.bed_transpose[i] != 0) ImGui::Text("%s  (%+d)", st.bed_names[i].c_str(), st.bed_transpose[i]);
        else ImGui::Text("%s", st.bed_names[i].c_str());
    }

    //--- notes ----------------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Notes: %d sounding, %d played", st.notes_sounding, st.notes_total);
    for (int i = (int)st.recent.size() - 1; i >= 0 && i >= (int)st.recent.size() - 12; i--){
        const MusicNoteLog& n = st.recent[i];
        if (n.f_tension) ImGui::TextColored(ImVec4(1,0.6f,0.4f,1), "%7.1f  %-12s %s  tension", n.time_s, n.voice.c_str(), MusicNoteName(n.midi).c_str());
        else ImGui::Text("%7.1f  %-12s %s", n.time_s, n.voice.c_str(), MusicNoteName(n.midi).c_str());
    }

    //--- output ---------------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Output: peak %.1f dB, rms %.1f dB, limiter %d", st.peak_db, st.rms_db, st.clipped);
    ImGui::SliderFloat("Render (s)", &render_seconds, 5.0f, 120.0f, "%.0f");
    if (ImGui::Button("Render to wav")){
        json r = RenderJson("render.wav", render_seconds, p, 1, {});
        last_render = r.contains("path") ? r["path"].get<std::string>() : r.value("error", std::string("?"));
    }
    if (!last_render.empty()) ImGui::TextWrapped("%s", last_render.c_str());

    ImGui::PopItemWidth();
    ImGui::End();
}
#endif

//--- MCP -----------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationMusic::RegisterMCPTools(void){
    MCPServer::Get()->RegisterTool("music_state",
        "What the music is doing right now: score, key (and any key change waiting for the next "
        "bar), bar and beat, the parameters, each bed's current gain and transposition, the voices "
        "and their registers, the last notes played, and the output level over the last second.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json { (void)args; return StateJson(); });

    MusicPlayer* pl = &player;
    MCPServer::Get()->RegisterTool("music_set",
        "Change the live parameters. Any subset of: suspense (0 calm .. 1 tense - moves every "
        "bed's gain and every voice's density between the score's calm and tense values, and adds "
        "tension notes), bpm, master, bed_gain, voice_gain. Takes effect within one audio block. "
        "Returns the state.",
        json{{"type","object"},{"properties",{
            {"suspense",{{"type","number"}}}, {"bpm",{{"type","number"}}}, {"master",{{"type","number"}}},
            {"bed_gain",{{"type","number"}}}, {"voice_gain",{{"type","number"}}}}}},
        [this, pl](const json& args) -> json {
            MusicParams p = pl->GetParams();
            auto num = [&](const char* k, float& v){ if (args.contains(k) && args[k].is_number()) v = args[k].get<float>(); };
            num("suspense", p.suspense); num("bpm", p.bpm); num("master", p.master);
            num("bed_gain", p.bed_gain); num("voice_gain", p.voice_gain);
            p.suspense = std::max(0.0f, std::min(1.0f, p.suspense));
            p.bpm = std::max(20.0f, std::min(240.0f, p.bpm));
            pl->SetParams(p);
            return StateJson();
        });

    MCPServer::Get()->RegisterTool("music_key",
        "Change key. `root` (\"E\", \"F#\"), and/or `shift` in semitones from the current root (7 = "
        "up a fifth, 5 = down a fifth), and/or `mode` (pentatonic_minor, aeolian, dorian, "
        "phrygian, pentatonic_major, ionian). Waits for the next bar unless `now` is true. Pitched "
        "beds cross over to the new key; voices walk to the nearest note of the new scale.",
        json{{"type","object"},{"properties",{
            {"root",{{"type","string"}}}, {"shift",{{"type","integer"}}}, {"mode",{{"type","string"}}},
            {"now",{{"type","boolean"}}}}}},
        [this, pl](const json& args) -> json {
            MusicEvent e;
            std::string error;
            if (!KeyEventFromJson(args, pl->GetStatus().root_pc, e, error)) return json{{"error", error}};
            pl->Post(e);
            return StateJson();
        });

    MCPServer::Get()->RegisterTool("music_stinger",
        "Play one of the score's stingers now, by name (see music_state).",
        json{{"type","object"},{"properties",{{"name",{{"type","string"}}}}},{"required",json::array({"name"})}},
        [this, pl](const json& args) -> json {
            const std::string name = args.value("name", std::string());
            const std::shared_ptr<const MusicScore> score = pl->GetScore();
            bool f_found = false;
            if (score) for (const MusicStingerDef& s : score->stingers) f_found |= (s.name == name);
            if (!f_found) return json{{"error", "no stinger called \"" + name + "\" - see music_state"}};
            MusicEvent e;
            e.type = MusicEvent::STINGER;
            e.name = name;
            pl->Post(e);
            return StateJson();
        });

    MCPServer::Get()->RegisterTool("music_reload",
        "Re-read the score JSON and every sample it names, and start it from the top. `score` "
        "loads a different one (an asset name like \"music/jungle.json\"). A score that fails to "
        "load is reported and the old one keeps playing. The live parameters carry over, except "
        "bpm, which the score sets.",
        json{{"type","object"},{"properties",{{"score",{{"type","string"}}}}}},
        [this](const json& args) -> json {
            std::string error;
            if (!RequestReload(args.value("score", score_file), error)) return json{{"error", error}};
            return StateJson();
        });

    MCPServer::Get()->RegisterTool("music_render",
        "Render the current score OFFLINE to a stereo 48 kHz wav in apps/music/renders/, without "
        "touching what is playing - then measure it with tools/samplescan (--file) to check the "
        "notes, key and levels. Parameters default to the live ones; any of suspense, bpm, master, "
        "bed_gain, voice_gain overrides them for the render. `seed` makes it repeatable (default 1). "
        "`timeline` is a list of events at times: {at_s, root|shift|mode, now} for a key change, "
        "{at_s, suspense} to move suspense, {at_s, stinger} for a stinger.",
        json{{"type","object"},{"properties",{
            {"seconds",{{"type","number"}}}, {"file",{{"type","string"}}}, {"seed",{{"type","integer"}}},
            {"suspense",{{"type","number"}}}, {"bpm",{{"type","number"}}}, {"master",{{"type","number"}}},
            {"bed_gain",{{"type","number"}}}, {"voice_gain",{{"type","number"}}},
            {"timeline",{{"type","array"},{"items",{{"type","object"}}}}}}}},
        [this, pl](const json& args) -> json {
            MusicParams p = pl->GetParams();
            auto num = [&](const json& from, const char* k, float& v){ if (from.contains(k) && from[k].is_number()) v = from[k].get<float>(); };
            num(args, "suspense", p.suspense); num(args, "bpm", p.bpm); num(args, "master", p.master);
            num(args, "bed_gain", p.bed_gain); num(args, "voice_gain", p.voice_gain);
            const double seconds = std::max(1.0, std::min(600.0, args.value("seconds", 30.0)));
            const uint32_t seed = (uint32_t)std::max(1, args.value("seed", 1));

            std::vector<MusicPlayer::TimedEvent> timeline;
            if (args.contains("timeline") && args["timeline"].is_array()){
                int root = pl->GetStatus().root_pc;
                for (const json& t : args["timeline"]){
                    if (!t.is_object() || !t.contains("at_s") || !t["at_s"].is_number()) return json{{"error", "every timeline entry needs at_s"}};
                    MusicPlayer::TimedEvent te;
                    te.at_s = t["at_s"].get<double>();
                    if (t.contains("stinger") && t["stinger"].is_string()){
                        te.event.type = MusicEvent::STINGER;
                        te.event.name = t["stinger"].get<std::string>();
                    }
                    else if (t.contains("suspense") && t["suspense"].is_number()){
                        te.event.type = MusicEvent::SUSPENSE;
                        te.event.value = t["suspense"].get<float>();
                    }
                    else{
                        std::string error;
                        if (!KeyEventFromJson(t, root, te.event, error)) return json{{"error", error}};
                        if (te.event.root_pc >= 0) root = te.event.root_pc;     //shifts chain
                    }
                    timeline.push_back(te);
                }
                std::sort(timeline.begin(), timeline.end(), [](const MusicPlayer::TimedEvent& a, const MusicPlayer::TimedEvent& b){ return a.at_s < b.at_s; });
            }
            return RenderJson(args.value("file", std::string("render.wav")), seconds, p, seed, timeline);
        });
}
#endif
