#include "ApplicationMusic.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include "Debug.h"
#include "File.h"

#include <algorithm>
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

ApplicationMusic::~ApplicationMusic(){
    if (audition_thread.joinable()) audition_thread.join();
    if (export_thread.joinable()) export_thread.join();
}

void ApplicationMusic::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->f_render_skybox = false;
    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    main_window->Resize(1600,900);
    main_scene = CreateNewScene("Music");

    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    if (!soundsystem->f_initialised){
        //Not fatal: an offline render needs no device, and that is half of what this bench is for.
        debug->Warn("No sound device - live playback is off, music_render still works\n");
    }
    LoadAndPlay(score_file);

    //The library, the tool that measures it and the folder its exports go to, all found from the
    //exe like the asset roots. assets is this app's own root, so a score names an export as
    //music/sounds/<name>.wav - the same name a game's copy of the score has (make publish).
    const std::string exe = GetExecutableDirectory();
    library.SetPaths(exe + "/../samples", exe + "/../../../tools/samplescan/build/samplescan.exe", exe + "/../assets/music/sounds");
    std::string error;
    if (!library.Refresh(error)) debug->Warn("Library: %s\n", error.c_str());

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
//A part's section as a person reads it: its name, or "all" for a part in every section.
std::string SectionName(const MusicStatus& st, int section){
    if (section == -2) return "next";
    return (section >= 0 && section < (int)st.section_names.size()) ? st.section_names[section] : "all";
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
    if (!st.section_names.empty()){
        json sec = {{"playing", SectionName(st, st.section)}, {"bar", st.section_bar}, {"all", st.section_names}};
        if (st.section_bars > 0) sec["bars"] = st.section_bars;
        if (st.pending_section != -1) sec["pending"] = SectionName(st, st.pending_section);
        j["section"] = sec;
    }
    j["params"] = {{"suspense", p.suspense}, {"brightness", p.brightness}, {"bpm", p.bpm}, {"master", p.master},
                   {"bed_gain", p.bed_gain}, {"voice_gain", p.voice_gain},
                   {"pause_fade_s", p.pause_fade_s}, {"resume_fade_s", p.resume_fade_s}};
    //"held" is the part worth checking: the fade has finished and the music's clock has stopped.
    j["pause"] = {{"paused", st.f_paused}, {"held", st.f_held}, {"gain", st.pause_gain}};
    json beds = json::array();
    for (size_t i = 0; i < st.bed_names.size(); i++){
        beds.push_back({{"name", st.bed_names[i]}, {"gain", st.bed_gains[i]}, {"transpose", st.bed_transpose[i]},
                        {"height", i < st.bed_heights.size() ? st.bed_heights[i] : 0.5f},
                        {"section", SectionName(st, i < st.bed_sections.size() ? st.bed_sections[i] : -1)}});
    }
    if (!st.auditioning.empty()) j["auditioning"] = st.auditioning;
    j["beds"] = beds;
    json voices = json::array(), stingers = json::array();
    if (score){
        for (size_t i = 0; i < score->voices.size(); i++){
            const MusicVoiceDef& v = score->voices[i];
            voices.push_back({{"name", v.name}, {"sample", v.sample},
                              {"register", MusicNoteName(v.low_midi) + ".." + MusicNoteName(v.high_midi)},
                              {"density_calm", v.density_calm}, {"density_tense", v.density_tense},
                              {"height", i < st.voice_heights.size() ? st.voice_heights[i] : 0.5f},
                              {"section", SectionName(st, v.section)}});
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
    //The MUSIC's clock at the end, which falls behind `seconds` by however long a pause held it.
    j["final_time_s"] = st.time_s;
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
    RenderLibraryPanel();
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

    //--- pause ----------------------------------------------------------------------------
    //The game's pause, tried here first: a fade out, a hold that stops the music's clock, and a
    //fade back in to where it stopped. See MusicEngine.h.
    MusicEvent pe;
    pe.type = MusicEvent::PAUSE;
    if (ImGui::Button(st.f_paused ? "Resume" : "Pause", ImVec2(80, 0))){ pe.value = st.f_paused ? 0.0f : 1.0f; player.Post(pe); }
    ImGui::SameLine();
    if (st.f_held) ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "Paused - held at %.1f s", st.time_s);
    else if (st.f_paused) ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "Fading out... %.0f%%", st.pause_gain * 100.0f);
    else if (st.pause_gain < 1.0f) ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), "Fading in... %.0f%%", st.pause_gain * 100.0f);

    //--- sections -------------------------------------------------------------------------
    if (!st.section_names.empty()){
        if (st.section_bars > 0) ImGui::Text("Section: %s, bar %d of %d", SectionName(st, st.section).c_str(), st.section_bar + 1, st.section_bars);
        else ImGui::Text("Section: %s, bar %d", SectionName(st, st.section).c_str(), st.section_bar + 1);
        if (st.pending_section != -1){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), "-> %s at the next bar", SectionName(st, st.pending_section).c_str());
        }
        MusicEvent se;
        se.type = MusicEvent::SECTION;
        ImGui::PushID("sections");      //a section may share its name with a stinger's button
        for (size_t i = 0; i < st.section_names.size(); i++){
            if (i) ImGui::SameLine();
            if (ImGui::Button(st.section_names[i].c_str())){ se.name = st.section_names[i]; player.Post(se); }
        }
        ImGui::PopID();
        ImGui::SameLine();
        if (ImGui::Button("Next section")){ se.name.clear(); player.Post(se); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every section change waits for the next bar line");
    }
    ImGui::Separator();

    //--- the knobs ------------------------------------------------------------------------
    bool f_changed = false;
    ImGui::PushItemWidth(-140.0f);
    f_changed |= ImGui::SliderFloat("Suspense", &p.suspense, 0.0f, 1.0f, "%.2f");
    f_changed |= ImGui::SliderFloat("Brightness", &p.brightness, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 rumble and bass, 1 high and bright, 0.5 as scored");
    f_changed |= ImGui::SliderFloat("Tempo (bpm)", &p.bpm, 30.0f, 120.0f, "%.0f");
    f_changed |= ImGui::SliderFloat("Master", &p.master, 0.0f, 1.5f, "%.2f");
    f_changed |= ImGui::SliderFloat("Beds", &p.bed_gain, 0.0f, 2.0f, "%.2f");
    f_changed |= ImGui::SliderFloat("Voices", &p.voice_gain, 0.0f, 2.0f, "%.2f");
    f_changed |= ImGui::SliderFloat("Pause fade (s)", &p.pause_fade_s, 0.1f, 5.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long Pause takes to fade the music out before holding it");
    f_changed |= ImGui::SliderFloat("Resume fade (s)", &p.resume_fade_s, 0.1f, 5.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long Resume takes to bring it back, from where it stopped");
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
        const float h = i < st.bed_heights.size() ? st.bed_heights[i] : 0.5f;
        const int bed_section = i < st.bed_sections.size() ? st.bed_sections[i] : -1;
        char tag[64] = "";
        if (!st.section_names.empty()) snprintf(tag, sizeof tag, "  [%s]", SectionName(st, bed_section).c_str());
        //A bed of another section, faded out, in grey: it is in the score, not in the music.
        const bool f_idle = st.bed_gains[i] <= 0.0f && bed_section >= 0 && bed_section != st.section;
        if (f_idle) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (st.bed_transpose[i] != 0) ImGui::Text("%s  (%+d)  height %.2f%s", st.bed_names[i].c_str(), st.bed_transpose[i], h, tag);
        else ImGui::Text("%s  height %.2f%s", st.bed_names[i].c_str(), h, tag);
        if (f_idle) ImGui::PopStyleColor();
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

//--- library -------------------------------------------------------------------------------

bool ApplicationMusic::AuditionFile(const std::string& file, std::string& error){
    std::lock_guard<std::mutex> lock(audition_mutex);
    const std::string wav = RendersDirectory() + "/audition.wav";
    //Not trimmed: the point of an audition is to hear the file as it is, lead-in and tail included.
    if (!library.Export(file, wav, false, error)) return false;
    auto sample = std::make_shared<MusicSample>();
    if (!LoadMusicSampleFile(wav, file, *sample, error)) return false;
    player.Audition(sample);
    return true;
}

void ApplicationMusic::StartPanelExport(const std::string& file, const std::string& name, bool f_trim, bool f_replace, bool f_missing){
    if (f_export_busy.exchange(true)) return;
    if (export_thread.joinable()) export_thread.join();
    export_thread = std::thread([this, file, name, f_trim, f_replace, f_missing](){
        std::string error;
        if (f_missing) library.ExportMissing(error);
        else library.ExportToSound(file, name, f_trim, f_replace, error);
        {
            std::lock_guard<std::mutex> lock(audition_mutex);
            export_error = error;
        }
        f_export_busy = false;
    });
}

json ApplicationMusic::EntryJson(const MusicLibrary::Entry& e, bool f_full){
    json j;
    j["file"] = e.file;
    j["state"] = MusicLibrary::StateName(e.state);
    if (!e.exported_as.empty()){
        json names = json::array();
        for (const std::string& n : e.exported_as) names.push_back("music/sounds/" + n + ".wav");
        j["exported_as"] = names;
    }
    if (f_full){
        for (const auto& kv : e.fields) if (!kv.second.empty()) j[kv.first] = kv.second;
        return j;
    }
    //The columns a person sorts by; library_get has the rest.
    for (const char* c : {"category", "instrument", "root", "comment", "guess", "why", "pitch_note", "key",
                          "duration_s", "envelope", "loopable", "name_mood"}){
        const std::string v = e.Get(c);
        if (!v.empty()) j[c] = v;
    }
    return j;
}

#ifdef USE_IMGUI
void ApplicationMusic::SelectLibraryFile(const MusicLibrary::Entry& e){
    library_selected = e.file;
    edit_category = -1;
    const std::vector<std::string>& cats = MusicLibrary::Categories();
    for (size_t i = 0; i < cats.size(); i++) if (cats[i] == e.Get("category")) edit_category = (int)i;
    snprintf(edit_instrument, sizeof edit_instrument, "%s", e.Get("instrument").c_str());
    snprintf(edit_root, sizeof edit_root, "%s", e.Get("root").c_str());
    snprintf(edit_comment, sizeof edit_comment, "%s", e.Get("comment").c_str());
    snprintf(edit_export_name, sizeof edit_export_name, "%s", MusicLibrary::SuggestExportName(e).c_str());
    edit_export_trim = MusicLibrary::SuggestTrim(e);
    for (const MusicLibrary::ExportDef& d : library.Exports()) if (d.file == e.file && d.name == edit_export_name) edit_export_trim = d.f_trim;
}

void ApplicationMusic::RenderLibraryPanel(void){
    ImGui::SetNextWindowPos(ImVec2(820,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640,780),ImGuiCond_FirstUseEver);
    ImGui::Begin("Library");

    const std::vector<MusicLibrary::Entry> all = library.Snapshot();
    int counts[4] = {0, 0, 0, 0};
    for (const MusicLibrary::Entry& e : all) counts[e.state]++;
    ImGui::Text("%d files: %d not scanned, %d unclassified, %d classified", (int)all.size(),
                counts[MusicLibrary::NOT_SCANNED], counts[MusicLibrary::UNCLASSIFIED], counts[MusicLibrary::CLASSIFIED]);
    if (counts[MusicLibrary::MISSING]) ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "%d in the catalog but gone from disk", counts[MusicLibrary::MISSING]);

    if (library.IsScanning()) ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), "Scanning...");
    else{
        const int fresh = counts[MusicLibrary::NOT_SCANNED];
        ImGui::BeginDisabled(fresh == 0);
        char label[48];
        snprintf(label, sizeof label, "Scan new (%d)", fresh);
        if (ImGui::Button(label)) library.StartScan(true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Measure only the files that are not scanned yet. Every other row is left as it is.");
        ImGui::SameLine();
        if (ImGui::Button("Scan all")) library.StartScan(false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-measure every file with tools/samplescan. Your categories and notes are kept.");
        ImGui::SameLine();
        if (ImGui::Button("Refresh")){ std::string error; library.Refresh(error); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-read the catalog and look for new files, without measuring");
        //On a fresh checkout every wav is missing - exports.csv is kept, the wavs are not.
        int missing = 0;
        for (const MusicLibrary::ExportDef& d : library.Exports()) missing += !d.f_on_disk;
        if (missing){
            ImGui::SameLine();
            char label[48];
            snprintf(label, sizeof label, "Export missing (%d)", missing);
            ImGui::BeginDisabled(f_export_busy);
            if (ImGui::Button(label)) StartPanelExport("", "", false, false, true);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Write every wav samples/exports.csv lists that is not in assets/music/sounds - what make samples does");
        }
    }
    const std::string message = library.Message();
    if (!message.empty()){ ImGui::SameLine(); ImGui::TextWrapped("%s", message.c_str()); }

    ImGui::SetNextItemWidth(160);
    ImGui::Combo("##filter", &library_filter, "Needs attention\0Classified\0Classified, not exported\0All\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##find", "find", library_find, sizeof library_find);

    //--- the list -------------------------------------------------------------------------
    const ImVec4 state_colour[4] = {ImVec4(1,0.8f,0.3f,1), ImVec4(0.5f,0.8f,1,1), ImVec4(0.5f,1,0.5f,1), ImVec4(1,0.4f,0.4f,1)};
    const MusicLibrary::Entry* selected = nullptr;
    const std::string find = library_find;
    if (ImGui::BeginTable("library", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_Resizable, ImVec2(0, 320))){
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Note/key", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableHeadersRow();
        for (const MusicLibrary::Entry& e : all){
            if (e.file == library_selected) selected = &e;
            const bool f_attention = e.state == MusicLibrary::NOT_SCANNED || e.state == MusicLibrary::UNCLASSIFIED;
            if (library_filter == 0 && !f_attention) continue;
            if ((library_filter == 1 || library_filter == 2) && e.state != MusicLibrary::CLASSIFIED) continue;
            if (library_filter == 2 && !e.exported_as.empty()) continue;
            if (!find.empty() && e.file.find(find) == std::string::npos) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(e.file.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, state_colour[e.state]);
            if (ImGui::Selectable(MusicLibrary::StateName(e.state), e.file == library_selected, ImGuiSelectableFlags_SpanAllColumns)){
                SelectLibraryFile(e);
                selected = &e;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            std::string shown = e.file;
            if (shown.rfind("unsorted/", 0) == 0) shown = shown.substr(9);
            ImGui::TextUnformatted(shown.c_str());
            //What a score calls it, where it has a name yet.
            for (const std::string& n : e.exported_as){ ImGui::SameLine(); ImGui::TextDisabled("-> %s", n.c_str()); }
            ImGui::TableSetColumnIndex(2);
            //A role typed by hand, or the guess in grey - so the list says which is which.
            if (!e.Get("category").empty()) ImGui::TextUnformatted(e.Get("category").c_str());
            else if (!e.Get("guess").empty()) ImGui::TextDisabled("%s?", e.Get("guess").c_str());
            ImGui::TableSetColumnIndex(3);
            const std::string tonal = !e.Get("root").empty() ? e.Get("root")
                                    : !e.Get("pitch_note").empty() ? e.Get("pitch_note") : e.Get("key");
            ImGui::TextUnformatted(tonal.c_str());
            ImGui::TableSetColumnIndex(4);
            if (!e.Get("duration_s").empty()) ImGui::Text("%.1f s", atof(e.Get("duration_s").c_str()));
        }
        ImGui::EndTable();
    }

    //--- the selected file ----------------------------------------------------------------
    ImGui::Separator();
    if (!selected){
        ImGui::TextDisabled("Select a file to see how it measured and to classify it.");
        ImGui::End();
        return;
    }
    const MusicLibrary::Entry& e = *selected;
    ImGui::TextWrapped("%s", e.file.c_str());
    ImGui::TextColored(state_colour[e.state], "%s", MusicLibrary::StateName(e.state));

    const MusicStatus st = player.GetStatus();
    const bool f_busy = f_audition_busy;
    ImGui::SameLine();
    if (ImGui::Button(f_busy ? "Loading..." : "Audition") && !f_busy){
        if (audition_thread.joinable()) audition_thread.join();
        f_audition_busy = true;
        const std::string file = e.file;
        audition_thread = std::thread([this, file](){
            std::string error;
            AuditionFile(file, error);
            std::lock_guard<std::mutex> lock(audition_mutex);
            audition_error = error;
            f_audition_busy = false;
        });
    }
    if (!st.auditioning.empty()){
        ImGui::SameLine();
        if (ImGui::Button("Stop")) player.Audition(nullptr);
    }
    {
        std::lock_guard<std::mutex> lock(audition_mutex);
        if (!audition_error.empty()) ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", audition_error.c_str());
    }

    if (e.state == MusicLibrary::NOT_SCANNED){
        ImGui::TextWrapped("Not measured yet. Press Scan new to measure it, then classify it here.");
        ImGui::End();
        return;
    }

    //What samplescan made of it - the evidence for the guess, and for correcting it.
    ImGui::Text("Guess: %s", e.Get("guess").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", e.Get("why").c_str());
    if (e.Get("pitch_kind") == "single"){
        ImGui::Text("Pitch: %s %s c   %s%% voiced   +-%s c", e.Get("pitch_note").c_str(), e.Get("pitch_cents").c_str(),
                    std::to_string((int)(atof(e.Get("voiced").c_str()) * 100)).c_str(), e.Get("stability_cents").c_str());
    }
    else ImGui::Text("Pitch: %s   key %s (r %s)", e.Get("pitch_kind").c_str(), e.Get("key").c_str(), e.Get("key_r").c_str());
    if (!e.Get("notes").empty()) ImGui::TextWrapped("Notes (%s): %s", e.Get("note_count").c_str(), e.Get("notes").c_str());
    ImGui::Text("Envelope: %s, attack %s ms   %s s%s", e.Get("envelope").c_str(), e.Get("attack_ms").c_str(),
                e.Get("duration_s").c_str(), e.Get("loopable") == "yes" ? ", loopable" : "");
    ImGui::Text("Level: peak %s dB, rms %s dB   noisiness %s, low end %s", e.Get("peak_db").c_str(), e.Get("rms_db").c_str(),
                e.Get("noisiness").c_str(), e.Get("low_share").c_str());
    if (!e.Get("bpm").empty()) ImGui::Text("Rhythm: %s onsets, pulse %s bpm (strength %s)", e.Get("onsets").c_str(),
                                           e.Get("bpm").c_str(), e.Get("beat_strength").c_str());
    std::string hints = e.Get("name_instrument");
    if (!e.Get("name_mood").empty()) hints += (hints.empty() ? "" : ", ") + e.Get("name_mood");
    if (!e.Get("name_note").empty()) hints += (hints.empty() ? "" : ", ") + e.Get("name_note");
    if (!hints.empty()) ImGui::Text("Name says: %s", hints.c_str());

    //--- classifying ----------------------------------------------------------------------
    ImGui::Separator();
    ImGui::PushItemWidth(-100.0f);
    const std::vector<std::string>& cats = MusicLibrary::Categories();
    if (ImGui::BeginCombo("Category", edit_category >= 0 ? cats[edit_category].c_str() : "(none)")){
        if (ImGui::Selectable("(none)", edit_category < 0)) edit_category = -1;
        for (int i = 0; i < (int)cats.size(); i++) if (ImGui::Selectable(cats[i].c_str(), i == edit_category)) edit_category = i;
        ImGui::EndCombo();
    }
    ImGui::InputText("Instrument", edit_instrument, sizeof edit_instrument);
    ImGui::InputText("Root", edit_root, sizeof edit_root);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The note it plays, like F#3 - by ear, if the measurement is off");
    ImGui::InputText("Comment", edit_comment, sizeof edit_comment);
    ImGui::PopItemWidth();

    if (ImGui::Button("Accept guess")){
        for (int i = 0; i < (int)cats.size(); i++) if (cats[i] == e.Get("guess")) edit_category = i;
        if (!edit_root[0] && e.Get("pitch_kind") == "single") snprintf(edit_root, sizeof edit_root, "%s", e.Get("pitch_note").c_str());
        if (!edit_instrument[0]) snprintf(edit_instrument, sizeof edit_instrument, "%s", e.Get("name_instrument").c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fill the fields from the guess - then Save");
    ImGui::SameLine();
    if (ImGui::Button("Save")){
        std::string error;
        library.Classify(e.file, {{"category", edit_category >= 0 ? cats[edit_category] : std::string()},
                                  {"instrument", edit_instrument}, {"root", edit_root}, {"comment", edit_comment}}, error);
        if (error.empty()) library.Refresh(error);
    }

    //--- exporting ------------------------------------------------------------------------
    //To assets/music/sounds/<name>.wav, where a score names it as music/sounds/<name>.wav - and into
    //exports.csv, so `make samples` makes it again on a fresh checkout.
    ImGui::Separator();
    if (!e.exported_as.empty()){
        std::string names;
        for (const std::string& n : e.exported_as) names += (names.empty() ? "" : ", ") + ("music/sounds/" + n + ".wav");
        ImGui::Text("Exported as %s", names.c_str());
    }
    else ImGui::TextDisabled("Not exported - a score cannot use it yet");
    std::string taken_by;
    for (const MusicLibrary::ExportDef& d : library.Exports()) if (d.name == edit_export_name && d.file != e.file) taken_by = d.file;
    const bool f_valid = MusicLibrary::ValidExportName(edit_export_name);

    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##exportname", edit_export_name, sizeof edit_export_name);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The wav's name: a score plays it as music/sounds/<name>.wav");
    ImGui::SameLine();
    ImGui::Checkbox("Trim", &edit_export_trim);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cut the silence off both ends. On for notes, hits and most beds;\n"
                                                  "off for a loop, whose seam is its first and last sample.");
    ImGui::SameLine();
    const bool f_exporting = f_export_busy;
    const char* export_label = f_exporting ? "Exporting..." : !taken_by.empty() ? "Replace" : "Export";
    ImGui::BeginDisabled(f_exporting || !f_valid);
    if (ImGui::Button(export_label)) StartPanelExport(e.file, edit_export_name, edit_export_trim, !taken_by.empty(), false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Write assets/music/sounds/%s.wav. A score already playing it hears the new one after Reload.", edit_export_name);
    if (!f_valid) ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Letters, digits, _ and - only");
    else if (!taken_by.empty()) ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "music/sounds/%s.wav is %s now - Replace re-points it", edit_export_name, taken_by.c_str());
    {
        std::lock_guard<std::mutex> lock(audition_mutex);
        if (!export_error.empty()) ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", export_error.c_str());
    }
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
        "tension notes), brightness (0 rumble and bass .. 1 high and bright, 0.5 as scored - weighs "
        "parts by their height, moves voices to the bottom or top of their register, and tilts the "
        "mix), bpm, master, bed_gain, voice_gain, and pause_fade_s / resume_fade_s (how long "
        "music_pause takes each way). Takes effect within one audio block. Returns the state.",
        json{{"type","object"},{"properties",{
            {"suspense",{{"type","number"}}}, {"brightness",{{"type","number"}}},
            {"bpm",{{"type","number"}}}, {"master",{{"type","number"}}},
            {"bed_gain",{{"type","number"}}}, {"voice_gain",{{"type","number"}}},
            {"pause_fade_s",{{"type","number"}}}, {"resume_fade_s",{{"type","number"}}}}}},
        [this, pl](const json& args) -> json {
            MusicParams p = pl->GetParams();
            auto num = [&](const char* k, float& v){ if (args.contains(k) && args[k].is_number()) v = args[k].get<float>(); };
            num("suspense", p.suspense); num("bpm", p.bpm); num("master", p.master);
            num("bed_gain", p.bed_gain); num("voice_gain", p.voice_gain); num("brightness", p.brightness);
            num("pause_fade_s", p.pause_fade_s); num("resume_fade_s", p.resume_fade_s);
            p.suspense = std::max(0.0f, std::min(1.0f, p.suspense));
            p.brightness = std::max(0.0f, std::min(1.0f, p.brightness));
            p.bpm = std::max(20.0f, std::min(240.0f, p.bpm));
            p.pause_fade_s = std::max(0.01f, std::min(10.0f, p.pause_fade_s));
            p.resume_fade_s = std::max(0.01f, std::min(10.0f, p.resume_fade_s));
            pl->SetParams(p);
            return StateJson();
        });

    MCPServer::Get()->RegisterTool("music_pause",
        "Pause or resume the music, the way the game's pause will: `paused` true fades it out over "
        "pause_fade_s and then HOLDS it - its clock, beat, loops and ringing notes all stop where "
        "the fade ended - and false fades it back in over resume_fade_s from exactly there. Returns "
        "the state; its `pause` says paused, held (faded out and stopped) and the fade's gain.",
        json{{"type","object"},{"properties",{{"paused",{{"type","boolean"}}}}},{"required",json::array({"paused"})}},
        [this, pl](const json& args) -> json {
            if (!args.contains("paused") || !args["paused"].is_boolean()) return json{{"error", "paused must be true or false"}};
            MusicEvent e;
            e.type = MusicEvent::PAUSE;
            e.value = args["paused"].get<bool>() ? 1.0f : 0.0f;
            pl->Post(e);
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

    MCPServer::Get()->RegisterTool("music_section",
        "Move to another section of the score - a different set of beds and voices on the same "
        "theme (see music_state's section.all). `name` picks one; without it, the next. Waits for "
        "the next bar unless `now` is true; the beds then crossfade over the score's section_fade_s.",
        json{{"type","object"},{"properties",{{"name",{{"type","string"}}}, {"now",{{"type","boolean"}}}}}},
        [this, pl](const json& args) -> json {
            const std::shared_ptr<const MusicScore> score = pl->GetScore();
            if (!score || score->sections.empty()) return json{{"error", "this score has no sections"}};
            MusicEvent e;
            e.type = MusicEvent::SECTION;
            e.name = args.value("name", std::string());
            if (!e.name.empty() && score->SectionIndex(e.name) < 0) return json{{"error", "no section called \"" + e.name + "\" - see music_state"}};
            e.f_now = args.contains("now") && args["now"].is_boolean() && args["now"].get<bool>();
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
        "notes, key and levels. Parameters default to the live ones; any of suspense, brightness, "
        "bpm, master, bed_gain, voice_gain overrides them for the render. `seed` makes it repeatable "
        "(default 1). `timeline` is a list of events at times: {at_s, root|shift|mode, now} for a key "
        "change, {at_s, suspense} or {at_s, brightness} to move one, {at_s, stinger} for a stinger, "
        "{at_s, section, now} for a section change (\"\" = the next one), {at_s, pause: true|false} "
        "to pause or resume (pause_fade_s / resume_fade_s override the fades). The result's "
        "final_time_s is the music's own clock, which a pause holds back.",
        json{{"type","object"},{"properties",{
            {"seconds",{{"type","number"}}}, {"file",{{"type","string"}}}, {"seed",{{"type","integer"}}},
            {"suspense",{{"type","number"}}}, {"brightness",{{"type","number"}}},
            {"bpm",{{"type","number"}}}, {"master",{{"type","number"}}},
            {"bed_gain",{{"type","number"}}}, {"voice_gain",{{"type","number"}}},
            {"pause_fade_s",{{"type","number"}}}, {"resume_fade_s",{{"type","number"}}},
            {"timeline",{{"type","array"},{"items",{{"type","object"}}}}}}}},
        [this, pl](const json& args) -> json {
            MusicParams p = pl->GetParams();
            auto num = [&](const json& from, const char* k, float& v){ if (from.contains(k) && from[k].is_number()) v = from[k].get<float>(); };
            num(args, "suspense", p.suspense); num(args, "bpm", p.bpm); num(args, "master", p.master);
            num(args, "bed_gain", p.bed_gain); num(args, "voice_gain", p.voice_gain); num(args, "brightness", p.brightness);
            num(args, "pause_fade_s", p.pause_fade_s); num(args, "resume_fade_s", p.resume_fade_s);
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
                    else if (t.contains("brightness") && t["brightness"].is_number()){
                        te.event.type = MusicEvent::BRIGHTNESS;
                        te.event.value = t["brightness"].get<float>();
                    }
                    else if (t.contains("pause") && t["pause"].is_boolean()){
                        te.event.type = MusicEvent::PAUSE;
                        te.event.value = t["pause"].get<bool>() ? 1.0f : 0.0f;
                    }
                    else if (t.contains("section") && t["section"].is_string()){
                        te.event.type = MusicEvent::SECTION;
                        te.event.name = t["section"].get<std::string>();
                        const std::shared_ptr<const MusicScore> score = pl->GetScore();
                        if (!te.event.name.empty() && (!score || score->SectionIndex(te.event.name) < 0))
                            return json{{"error", "no section called \"" + te.event.name + "\""}};
                        te.event.f_now = t.contains("now") && t["now"].is_boolean() && t["now"].get<bool>();
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
    //--- the library ------------------------------------------------------------------------
    MCPServer::Get()->RegisterTool("library_list",
        "The sample library (apps/music/samples): every file with its state - 'not scanned' (new, "
        "never measured), 'unclassified' (measured, only a guess), 'classified' (has a category) or "
        "'missing' - and its role, note or key, length and mood. `state` filters to one of those. "
        "Re-reads the catalog and the folder first, so a file dropped in a moment ago is listed.",
        json{{"type","object"},{"properties",{{"state",{{"type","string"}}}}}},
        [this](const json& args) -> json {
            std::string error;
            library.Refresh(error);
            const std::string want = args.value("state", std::string());
            json files = json::array();
            json counts = json::object();
            for (const MusicLibrary::Entry& e : library.Snapshot()){
                const std::string state = MusicLibrary::StateName(e.state);
                counts[state] = counts.value(state, 0) + 1;
                if (want.empty() || want == state) files.push_back(EntryJson(e, false));
            }
            json j{{"counts", counts}, {"files", files}, {"scanning", library.IsScanning()}};
            if (!error.empty()) j["error"] = error;
            return j;
        });

    MCPServer::Get()->RegisterTool("library_get",
        "Every measured column for one library file (see tools/samplescan for what each means).",
        json{{"type","object"},{"properties",{{"file",{{"type","string"}}}}},{"required",json::array({"file"})}},
        [this](const json& args) -> json {
            const std::string file = args.value("file", std::string());
            for (const MusicLibrary::Entry& e : library.Snapshot()) if (e.file == file) return EntryJson(e, true);
            return json{{"error", "no such file in the library: " + file + " - see library_list"}};
        });

    MCPServer::Get()->RegisterTool("library_scan",
        "Measure the library with tools/samplescan and wait for it (a few seconds for a few dozen "
        "files). only_new: true measures just the files that are not scanned yet and leaves every "
        "other row alone; otherwise every file is re-measured. Hand-set categories, instruments, "
        "roots and comments are kept either way. Returns the counts afterwards.",
        json{{"type","object"},{"properties",{
            {"only_new", {{"type","boolean"},{"description","measure only files not scanned yet (default false)"}}}}}},
        [this](const json& args) -> json {
            const bool f_only_new = args.contains("only_new") && args["only_new"].is_boolean() && args["only_new"].get<bool>();
            library.StartScan(f_only_new);  //false if one is already running, which this then waits for too
            if (!library.WaitForScan(180000)) return json{{"error", "the scan is still running after three minutes"}};
            json counts = json::object();
            for (const MusicLibrary::Entry& e : library.Snapshot()){
                const std::string state = MusicLibrary::StateName(e.state);
                counts[state] = counts.value(state, 0) + 1;
            }
            return json{{"message", library.Message()}, {"counts", counts}};
        });

    MCPServer::Get()->RegisterTool("library_classify",
        "Set the hand-kept columns of one scanned library file: any of category (one of note_struck, "
        "note_sustained, drone, hum, texture, hit, rhythm, phrase, mix, stinger - or \"\" to clear), "
        "instrument, root (the note it plays, e.g. F#3) and comment. Only what is given changes.",
        json{{"type","object"},{"properties",{
            {"file",{{"type","string"}}}, {"category",{{"type","string"}}}, {"instrument",{{"type","string"}}},
            {"root",{{"type","string"}}}, {"comment",{{"type","string"}}}}},{"required",json::array({"file"})}},
        [this](const json& args) -> json {
            const std::string file = args.value("file", std::string());
            std::map<std::string, std::string> human;
            for (const char* c : {"category", "instrument", "root", "comment"}){
                if (args.contains(c) && args[c].is_string()) human[c] = args[c].get<std::string>();
            }
            if (human.count("category") && !human["category"].empty()){
                const std::vector<std::string>& cats = MusicLibrary::Categories();
                if (std::find(cats.begin(), cats.end(), human["category"]) == cats.end()){
                    return json{{"error", "unknown category '" + human["category"] + "'"}};
                }
            }
            std::string error;
            if (!library.Classify(file, human, error)) return json{{"error", error}};
            library.Refresh(error);
            for (const MusicLibrary::Entry& e : library.Snapshot()) if (e.file == file) return EntryJson(e, false);
            return json{{"ok", true}};
        });

    MCPServer::Get()->RegisterTool("library_audition",
        "Play one library file as it is, beside the music, so the person at the desk can hear it - "
        "by `file` as library_list names it. `stop`: true stops the audition.",
        json{{"type","object"},{"properties",{{"file",{{"type","string"}}},{"stop",{{"type","boolean"}}}}}},
        [this](const json& args) -> json {
            if (args.value("stop", false)){
                player.Audition(nullptr);
                return json{{"ok", true}};
            }
            std::string error;
            if (!AuditionFile(args.value("file", std::string()), error)) return json{{"error", error}};
            return json{{"auditioning", args.value("file", std::string())}};
        });

    MCPServer::Get()->RegisterTool("library_export",
        "Export one library file as assets/music/sounds/<name>.wav, so a score can name it as "
        "music/sounds/<name>.wav, and record it in samples/exports.csv (which `make samples` rebuilds the "
        "wavs from). `name` defaults to instrument_root (kalimba_Fs3); `trim` cuts the silence off "
        "both ends and defaults to on unless the file is loopable. A name another file already has "
        "is refused unless `replace` is true. `missing`: true instead writes every listed export "
        "whose wav is not on disk. A score playing the wav hears the new one after music_reload.",
        json{{"type","object"},{"properties",{
            {"file",{{"type","string"}}}, {"name",{{"type","string"}}}, {"trim",{{"type","boolean"}}},
            {"replace",{{"type","boolean"}}}, {"missing",{{"type","boolean"}}}}}},
        [this](const json& args) -> json {
            std::string error;
            library.Refresh(error);
            error.clear();
            if (args.contains("missing") && args["missing"].is_boolean() && args["missing"].get<bool>()){
                const int n = library.ExportMissing(error);
                json j{{"written", n}, {"message", library.Message()}};
                if (!error.empty()) j["error"] = error;
                return j;
            }
            const std::string file = args.value("file", std::string());
            const MusicLibrary::Entry* found = nullptr;
            const std::vector<MusicLibrary::Entry> all = library.Snapshot();
            for (const MusicLibrary::Entry& e : all) if (e.file == file) found = &e;
            if (!found) return json{{"error", "no such file in the library: " + file + " - see library_list"}};
            const std::string name = (args.contains("name") && args["name"].is_string()) ? args["name"].get<std::string>()
                                                                                        : MusicLibrary::SuggestExportName(*found);
            const bool f_trim = (args.contains("trim") && args["trim"].is_boolean()) ? args["trim"].get<bool>()
                                                                                    : MusicLibrary::SuggestTrim(*found);
            const bool f_replace = args.contains("replace") && args["replace"].is_boolean() && args["replace"].get<bool>();
            if (!library.ExportToSound(file, name, f_trim, f_replace, error)) return json{{"error", error}};
            return json{{"sample", "music/sounds/" + name + ".wav"}, {"trim", f_trim}, {"message", library.Message()}};
        });
}
#endif
