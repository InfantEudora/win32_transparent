#include "ApplicationChasm.h"
#include "Debug.h"
#include "File.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "InputRecording.h"
#include "Window.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <thread>
#include <stdio.h>
#include <windows.h>

/*
    RECORDING FOR TESTERS (docs/demo_plan.md, step 4).

    ALWAYS RECORDING: every autosave starts a fresh recording (RecordFromHere), and so does entering a colony - so
    whatever went wrong, the run since the last autosave is on disk, and a tester can report a bug after it happened.
    A recording's start state is a save (CaptureRecordingState), so each file replays on its own. They are core's
    recordings with a name of their own, chasm_auto_<date>_<time>.rec, and only AUTO_RECORDINGS_KEPT of them are kept:
    a person's own (F9, input_record) are never touched. Off in a build with the panels, where it would take over the
    recording an agent starts with input_record; the panel's Save header turns it on there.

    REPORT A BUG (the pause menu, F12) writes reports/<date>_<time>/, ready to zip and send: the recording up to now -
    which starts from the last autosave, so carries it - the colony now, a screenshot, the log and the last run's,
    any crash report from the last days, and a page saying what each is. The files are gathered on a worker thread: the recording is saved
    by the physics thread on its next pass, and a screenshot waits for the render thread, neither of which the
    physics thread may wait for itself.

    REPLAYS from the title's list, with a bar while one runs: how far, and Stop. The pause and the speed are the
    calendar's, as in a game. Commands made during a replay are dropped by core (Scene::command_filter), so looking
    round cannot change what is replayed.
*/

static Debugger* debug = new Debugger("ChasmRecord",DEBUG_ALL);

#define AUTO_RECORDING_STEM     "chasm_auto"
#define AUTO_RECORDINGS_KEPT    10          //always recording's files kept, the oldest deleted
#define QUIT_WAIT_PASSES        60          //a quit waits at most this many passes for the recording to be saved
#define REPORT_WAIT_MS          4000        //the report waits at most this long for the recording to be saved
#define REPORT_CRASH_SECONDS    (2 * 24 * 3600)     //crash reports this recent go in a bug report

namespace {

//"1:23", or "1:02:03" past the hour.
std::string Clock(float seconds){
    int t = std::max(0,(int)seconds);
    char buf[32];
    if (t >= 3600){
        snprintf(buf,sizeof(buf),"%d:%02d:%02d",t / 3600,(t / 60) % 60,t % 60);
    }else{
        snprintf(buf,sizeof(buf),"%d:%02d",t / 60,t % 60);
    }
    return buf;
}

//A file another thread - the log's own - may hold open for writing: read with every sharing allowed.
bool CopyShared(const std::string& from, const std::string& to){
    HANDLE in = CreateFileA(from.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,NULL,
                            OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (in == INVALID_HANDLE_VALUE){
        return false;
    }
    HANDLE out = CreateFileA(to.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    bool f_ok = out != INVALID_HANDLE_VALUE;
    char buffer[65536];
    DWORD got = 0;
    while (f_ok && ReadFile(in,buffer,sizeof(buffer),&got,NULL) && got > 0){
        DWORD wrote = 0;
        f_ok = WriteFile(out,buffer,got,&wrote,NULL) && wrote == got;
    }
    CloseHandle(in);
    if (out != INVALID_HANDLE_VALUE){
        CloseHandle(out);
    }
    return f_ok;
}

std::string BaseName(const std::string& path){
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

//"2026-10-16 14:03:11" as "16 Oct 14:03".
std::string RecordedText(const std::string& at){
    struct tm t = {};
    if (sscanf(at.c_str(),"%d-%d-%d %d:%d:%d",&t.tm_year,&t.tm_mon,&t.tm_mday,&t.tm_hour,&t.tm_min,&t.tm_sec) != 6){
        return at;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    char buf[32];
    return strftime(buf,sizeof(buf),"%d %b %H:%M",&t) ? std::string(buf) : at;
}

std::string MetaText(const nlohmann::json& state, const char* key){
    if (!state.is_object() || !state.contains("meta") || !state["meta"].is_object()){
        return "";
    }
    const nlohmann::json& meta = state["meta"];
    return (meta.contains(key) && meta[key].is_string()) ? meta[key].get<std::string>() : std::string();
}

}

//--- Always recording ---------------------------------------------------------------------------------------------------

//PHYSICS THREAD, or any thread through CloseTitle: a fresh recording from here, if always recording - the oldest kept
//ones let go first.
void ApplicationChasm::RecordFromHere(){
    if (!f_always_record.load() || f_menu_replay.load() || main_window->inputcontroller->IsReplaying()){
        return;
    }
    RequestRecordingRestart(AUTO_RECORDING_STEM);
    std::vector<std::string> kept;
    for (const std::string& name : ListRecordings()){
        if (name.compare(0,strlen(AUTO_RECORDING_STEM) + 1,AUTO_RECORDING_STEM "_") == 0){
            kept.push_back(name);
        }
    }
    //By name, which is by date: the oldest first.
    for (size_t i = 0; i + AUTO_RECORDINGS_KEPT <= kept.size(); i++){
        DeleteFileA((recordings_dir + "/" + kept[i]).c_str());
    }
}

/*
    PHYSICS THREAD, every pass from UpdateView, title or game: a replay that has ended - played out or stopped - leaves
    the colony as it went, played on and recorded from there; a quit waiting for its recording to be written; a bug
    report the worker has finished.
*/
void ApplicationChasm::ServiceRecording(){
    InputController* input = main_window->inputcontroller;
    bool f_replaying = input->IsReplaying();
    if (f_was_replaying && !f_replaying){
        f_menu_replay = false;
        debug->Info("Replay ended\n");
        if (!TitleShowing()){
            RecordFromHere();
        }
    }
    f_was_replaying = f_replaying;
    if (f_quit_pending){
        bool f_saved = GetRecordingsSaved() != quit_recordings_saved || !input->IsRecording();
        if (f_saved || ++quit_wait_passes > QUIT_WAIT_PASSES){
            f_quit_pending = false;
            Window::RequestQuitAll();
        }
    }
    if (report_stage.load() == 2 && ask_kind.load() == CHASM_ASK_NONE){
        report_stage = 0;
        std::string dir;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(menu_text_mutex);
            dir = report_dir;
            error = report_error;
        }
        if (error.empty()){
            OpenAsk(CHASM_ASK_REPORT,"Bug report written",dir);
        }else{
            OpenAsk(CHASM_ASK_NOTE,"The bug report is not complete",error);
        }
    }
}

//PHYSICS THREAD: Quit, answered - the colony autosaved, and the recording since the last one saved before going.
void ApplicationChasm::Quit(){
    if (f_game_on.load()){
        Autosave("",false);
    }
    InputController* input = main_window->inputcontroller;
    if (input->IsRecording()){
        quit_recordings_saved = GetRecordingsSaved();
        quit_wait_passes = 0;
        f_quit_pending = true;
        RequestRecordingStop();
        return;
    }
    Window::RequestQuitAll();
}

//--- Report a bug -------------------------------------------------------------------------------------------------------

/*
    PHYSICS THREAD, from the pause menu or F12: the folder made, the colony now saved into it, the recording so far
    saved by core on the next pass (and a fresh one started, as an autosave would) - and the rest to a worker.
*/
void ApplicationChasm::StartReport(){
    if (report_stage.load() != 0 || TitleShowing()){
        return;
    }
    ClosePause();
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    char stamp[32] = "report";
    if (lt){
        strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S",lt);
    }
    std::string reports = ChasmFolder("reports");
    CreateDirectoryA(reports.c_str(),NULL);
    std::string dir = reports + "/" + stamp;
    char full[MAX_PATH];
    if (GetFullPathNameA(dir.c_str(),MAX_PATH,full,NULL)){
        dir = full;
    }
    if (!CreateDirectoryA(dir.c_str(),NULL) && GetLastError() != ERROR_ALREADY_EXISTS){
        OpenAsk(CHASM_ASK_NOTE,"The bug report could not be written","No folder at " + dir);
        return;
    }
    std::string error;
    WriteSave(dir + "/now.json","report",error);
    InputController* input = main_window->inputcontroller;
    bool f_recording = input->IsRecording();
    uint32_t before = GetRecordingsSaved();
    if (f_recording){
        RequestRecordingRestart(f_always_record.load() ? AUTO_RECORDING_STEM : "");
    }
    report_stage = 1;
    int frames = overlay_frames.load();
    debug->Info("Bug report: %s\n",dir.c_str());
    std::thread([this,dir,f_recording,before,frames](){
        WriteReport(dir,f_recording,before,frames);
    }).detach();
}

/*
    A WORKER THREAD: the rest of the report, gathered as each part becomes ready - the recording once core has saved
    it, the picture once a frame without the menu has been drawn - then report_stage 2 for the physics thread to say
    so (ServiceRecording).
*/
void ApplicationChasm::WriteReport(const std::string& dir, bool f_recording, uint32_t recordings_before, int frames_before){
    auto wait_for = [](const std::function<bool()>& f_done, int ms){
        for (int t = 0; t < ms && !f_done(); t += 20){
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return f_done();
    };
    std::vector<std::string> lines;
    std::string error;
    //The run since the last autosave - or since the recording began.
    std::string recording;
    if (!f_recording){
        lines.push_back("(no recording: none was running)");
    }else if (wait_for([&](){ return GetRecordingsSaved() != recordings_before; },REPORT_WAIT_MS)){
        std::string path = ResolveRecordingPath("");
        recording = BaseName(path);
        if (CopyShared(path,dir + "/" + recording)){
            lines.push_back(recording + " - the run since the last autosave: Replay a recording on the title, or input_replay");
        }else{
            error = "The recording could not be copied.";
        }
    }else{
        error = "The recording was not saved in time.";
    }
    //Not the last autosave as well: the recording's start state is that autosave, whichever colony the newest file is.
    lines.push_back("now.json - the colony when the report was made: a save to load");
    //The game as it shows, once a frame without the pause menu has been drawn.
    wait_for([&](){ return overlay_frames.load() - frames_before >= 3; },1000);
    std::vector<uint8_t> png = renderer ? renderer->RequestScreenshot(true,2000) : std::vector<uint8_t>();
    FILE* f = png.empty() ? NULL : fopen((dir + "/screenshot.png").c_str(),"wb");
    if (f){
        fwrite(png.data(),1,png.size(),f);
        fclose(f);
        lines.push_back("screenshot.png - the game when the report was made");
    }
    //The log so far, and the last run's: if the game crashed before this run, that is where it says so.
    std::string log = Debugger::LogFilePath();
    if (!log.empty() && CopyShared(log,dir + "/" + BaseName(log))){
        lines.push_back(BaseName(log) + " - everything logged this run");
        size_t dot = log.find_last_of('.');
        std::string previous = (dot == std::string::npos) ? log + "_previous" : log.substr(0,dot) + "_previous" + log.substr(dot);
        if (CopyShared(previous,dir + "/" + BaseName(previous))){
            lines.push_back(BaseName(previous) + " - the run before this one");
        }
    }
    //Crash reports beside the exe from the last days (core/CrashReport.h), and the settings if there are any.
    std::string exe = GetExecutableDirectory();
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((exe + "/crash_*").c_str(),&fd);
    if (h != INVALID_HANDLE_VALUE){
        int64_t now = (int64_t)time(NULL);
        do{
            uint64_t t = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            int64_t written = (int64_t)(t / 10000000ull) - 11644473600ll;
            if (now - written <= REPORT_CRASH_SECONDS && CopyShared(exe + "/" + fd.cFileName,dir + "/" + fd.cFileName)){
                lines.push_back(std::string(fd.cFileName) + " - a crash report");
            }
        }while (FindNextFileA(h,&fd));
        FindClose(h);
    }
    if (CopyShared(exe + "/settings.json",dir + "/settings.json")){
        lines.push_back("settings.json - the game's settings");
    }
    //The page that says what all of it is.
    time_t now = time(NULL);
    char written[64] = "";
    struct tm* lt = localtime(&now);
    if (lt){
        strftime(written,sizeof(written),"%Y-%m-%d %H:%M:%S",lt);
    }
    FILE* page = fopen((dir + "/report.txt").c_str(),"w");
    if (page){
        fprintf(page,"Chasm bug report\n\nBuild:   %s\nWritten: %s\nColony:  %s\n\n",BuildName().c_str(),written,
                SaveTitle().c_str());
        for (const std::string& line : lines){
            fprintf(page,"  %s\n",line.c_str());
        }
        fprintf(page,"\nSay what happened, and what you expected instead, in the message you send this folder with.\n");
        fclose(page);
    }else if (error.empty()){
        error = "report.txt could not be written.";
    }
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        report_dir = dir;
        report_error = error.empty() ? std::string() : error + " The rest is in " + dir;
    }
    debug->Info("Bug report written: %s (%zu files)%s\n",dir.c_str(),lines.size(),error.empty() ? "" : " - incomplete");
    report_stage = 2;
}

//--- Replays ------------------------------------------------------------------------------------------------------------

//PHYSICS THREAD: the recordings that replay here - Chasm's, newest first - by what their start state says.
std::vector<ApplicationChasm::RecordingEntry> ApplicationChasm::RecordingEntries(){
    std::vector<RecordingEntry> out;
    for (const std::string& name : ListRecordings()){
        RecordingEntry e;
        e.path = recordings_dir + "/" + name;
        InputRecording rec;
        std::string error;
        if (!rec.Load(e.path,main_window->inputcontroller,error) || (!rec.app.empty() && rec.app != app_name)){
            continue;
        }
        e.recorded_at = rec.recorded_at;
        e.ticks = (rec.end > rec.begin) ? rec.end - rec.begin : 0;
        e.title = MetaText(rec.state,"title");
        e.build = MetaText(rec.state,"build");
        if (e.title.empty()){
            e.title = name.substr(0,name.size() - 4);
        }
        out.push_back(e);
    }
    std::sort(out.begin(),out.end(),[](const RecordingEntry& a, const RecordingEntry& b){
        return a.recorded_at > b.recorded_at;
    });
    return out;
}

//A recording's line in the replay list.
std::string ApplicationChasm::RecordingLabel(const RecordingEntry& e){
    float tps = (physics_tps > 0.0f) ? physics_tps : 50.0f;
    std::string label = e.title + "      " + Clock((float)e.ticks / tps) + "      " + RecordedText(e.recorded_at);
    if (!e.build.empty() && e.build != BuildName()){
        label += "      (another build)";
    }
    return label;
}

//--- Drawing ------------------------------------------------------------------------------------------------------------

/*
    RENDER THREAD, from DrawOverlay with the HUD: top left, clear of the top bar - REC and how long, while recording;
    while replaying, the replay's bar: how far through, and Stop. Its pause and speed are the calendar's.
*/
void ApplicationChasm::DrawRecording(float s){
    InputController* input = main_window->inputcontroller;
    bool f_rec = input->IsRecording();
    bool f_rep = input->IsReplaying();
    if (!f_rec && !f_rep){
        return;
    }
    float tps = (physics_tps > 0.0f) ? physics_tps : 50.0f;
    vec2 p0(16.0f * s,12.0f * s);
    if (!f_rep){
        std::string text = "REC  " + Clock((float)input->GetRecordingTicks() / tps);
        vec2 size = overlay->MeasureText(text.c_str(),15.0f * s);
        vec2 p1(p0.x + size.x + 40.0f * s,p0.y + 28.0f * s);
        overlay->AddRect(p0,p1,14.0f * s,HUD_PANEL);
        vec2 dot(p0.x + 14.0f * s,(p0.y + p1.y) * 0.5f);
        float r = 4.5f * s;
        overlay->AddRect(dot - vec2(r,r),dot + vec2(r,r),r,UIColor(226,60,50,255));
        overlay->AddText(text.c_str(),vec2(p0.x + 26.0f * s,p0.y + 19.5f * s),15.0f * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        return;
    }
    float at = (float)input->GetReplayPosition();
    float end = (float)input->GetReplayEnd();
    vec2 p1(p0.x + 330.0f * s,p0.y + 58.0f * s);
    overlay->AddRect(p0,p1,12.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    std::string text = "REPLAY  " + Clock(at / tps) + " / " + Clock(end / tps);
    overlay->AddText(text.c_str(),vec2(p0.x + 16.0f * s,p0.y + 26.0f * s),17.0f * s,HUD_TEXT,UI_ALIGN_LEFT);
    //How far through, under it.
    vec2 l0(p0.x + 16.0f * s,p0.y + 40.0f * s);
    vec2 l1(p1.x - 96.0f * s,l0.y + 4.0f * s);
    float k = (end > 0.0f) ? std::min(1.0f,std::max(0.0f,at / end)) : 0.0f;
    overlay->AddRect(l0,l1,2.0f * s,UIColor(255,255,255,40));
    overlay->AddRect(l0,vec2(l0.x + (l1.x - l0.x) * k,l1.y),2.0f * s,HUD_SELECT);
    //Stop, lit under the mouse.
    vec2 b0(p1.x - 80.0f * s,p0.y + 12.0f * s);
    vec2 b1(p1.x - 12.0f * s,p1.y - 12.0f * s);
    vec2 mouse((float)overlay_mouse_x.load(),(float)overlay_mouse_y.load());
    bool f_over = mouse.x >= b0.x && mouse.x <= b1.x && mouse.y >= b0.y && mouse.y <= b1.y;
    overlay->AddRect(b0,b1,6.0f * s,f_over ? UIColor(255,255,255,60) : UIColor(255,255,255,24));
    overlay->AddText("STOP",vec2((b0.x + b1.x) * 0.5f,b0.y + 22.0f * s),15.0f * s,HUD_TEXT,UI_ALIGN_CENTER);
    overlay_hits_drawing.push_back(OverlayHit{b0,b1,OVERLAY_HIT_REPLAY_STOP,0});
}
