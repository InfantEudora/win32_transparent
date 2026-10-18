#include "ApplicationChasm.h"
#include "Debug.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "Window.h"
#include "File.h"
#ifdef USE_SOUND
#include "SoundSystem.h"
#endif
#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <stdio.h>

/*
    THE PAUSE MENU, THE LISTS AND THE SAVES (docs/demo_plan.md, steps 1 and 2).

    THE PAUSE MENU is over the frozen game, not a scene of its own: the world stays drawn, dimmed, its calendar
    stopped as the speed button's pause stops it, and the HUD put away - as under the Statistics page. Escape opens
    it once nothing is in hand or selected (EscapeStep), and goes back to the game from it.

    THE LISTS are the title's and the pause menu's alike: New game's four maps, the saves to load, the saves to save
    over. Choosing a map or a save with a colony in play asks first (CHASM_ASK_LEAVE); a yes autosaves the colony and
    goes - so leaving loses nothing, and the question only asks whether to go.

    A LOAD OR A NEW MAP holds the physics thread while the map is generated - half a second in a debug build - so it
    waits a frame behind "Loading" (Busy, RunBusy): the word is up before the window stalls.

    SAVES are named for what they are (the user's: automatic): "Seed 12 - year 2, summer 4", kept in the file's
    "meta" with its kind and the build that wrote it, beside the state ChasmSaveToJson writes - which a replay's
    start state never reads. AUTOSAVES at the first dawn of each season and on leaving a colony, rotating through
    AUTOSAVE_SLOTS files; F5 and F8 the quick save. None of this is simulation input: nothing here is recorded.
*/

static Debugger* debug = new Debugger("ChasmMenu",DEBUG_ALL);

#define MENU_SHADE          UIColor(0,0,0,120)      //over the frozen game, under the pause menu
#define MENU_BOX            UIColor(22,28,32,225)   //the pause menu's and the lists' panel
#define MENU_ROWS           7                       //saves a page of a list shows
#define MENU_NOTE_SECONDS   3.0f                    //"Saved ..." stays up this long
#define AUTOSAVE_SLOTS      3                       //autosave_1 .. autosave_3, the oldest written over
#define QUICK_SAVE          "quicksave"

/*
    THE FOUR STARTS (the user's, 2026-10-16): New game offers four maps, each a seed and a size chosen by hand rather
    than a random seed, so every tester's colony starts on one of a few maps known to play. These are placeholders
    until they are chosen: the map every test so far has used, and three more of other sizes.
*/
struct ChasmStart{
    const char* name;
    uint32_t seed;
    int fine_cells;             //GridSettings::target_fine_cells: the map's size
};
static const ChasmStart CHASM_STARTS[] = {
    {"Seed 12",12,50000},
    {"Seed 3",3,35000},
    {"Seed 27",27,50000},
    {"Seed 41",41,80000},
};
#define CHASM_START_COUNT   ((int)(sizeof(CHASM_STARTS) / sizeof(CHASM_STARTS[0])))

namespace {

enum{
    PAUSE_ITEM_RESUME = 1,
    PAUSE_ITEM_SAVE,
    PAUSE_ITEM_LOAD,
    PAUSE_ITEM_STATS,
    PAUSE_ITEM_SETTINGS,
    PAUSE_ITEM_REPORT,
    PAUSE_ITEM_TITLE,
    PAUSE_ITEM_QUIT,
    SCREEN_ITEM_BACK,
    SCREEN_ITEM_OLDER,
    SCREEN_ITEM_NEWER,
    SCREEN_ITEM_NEW_SAVE,
    SCREEN_ITEM_NONE,
    SETTINGS_ITEM_MSAA,
    SETTINGS_ITEM_MASTER,
    SETTINGS_ITEM_MUSIC,
    SETTINGS_ITEM_EFFECTS,
    SETTINGS_ITEM_FULLSCREEN,
    SETTINGS_ITEM_UI_SIZE,
    SCREEN_ITEM_ROW = 100,      //+ the row: a map in CHASM_STARTS, or a save in screen_saves
};

const char* SizeName(int fine_cells){
    return (fine_cells < 40000) ? "small map" : (fine_cells < 70000) ? "medium map" : "large map";
}

//"16 Oct 13:19", in the player's own time.
std::string WrittenText(int64_t t){
    if (t <= 0){
        return "";
    }
    time_t tt = (time_t)t;
    struct tm* lt = localtime(&tt);
    char buf[32];
    if (!lt || !strftime(buf,sizeof(buf),"%d %b %H:%M",lt)){
        return "";
    }
    return buf;
}

std::string EntryLabel(const ChasmSaveEntry& e){
    std::string label = (e.kind == "auto") ? "Autosave: " + e.title : (e.kind == "quick") ? "Quick save: " + e.title : e.title;
    if (e.people > 0){
        label += "      " + std::to_string(e.people) + ((e.people == 1) ? " person" : " people");
    }
    std::string when = WrittenText(e.written);
    if (!when.empty()){
        label += "      " + when;
    }
    return label;
}

}

//--- The pause menu ---------------------------------------------------------------------------------------------------

//PHYSICS THREAD. The game paused under it, as it was before afterwards - as the pop-up does (OpenAsk).
void ApplicationChasm::OpenPause(){
    if (f_pause_open.load() || TitleShowing()){
        return;
    }
    pause_menu.Clear();
    pause_menu.layout.top = 0.30f;
    pause_menu.layout.width = 0.34f;
    pause_menu.layout.item_h = 0.058f;
    pause_menu.AddButton("Resume",PAUSE_ITEM_RESUME);
    pause_menu.AddButton("Save",PAUSE_ITEM_SAVE);
    pause_menu.AddButton("Load",PAUSE_ITEM_LOAD);
    pause_menu.AddButton("Statistics",PAUSE_ITEM_STATS);
    pause_menu.AddButton("Settings",PAUSE_ITEM_SETTINGS);
    pause_menu.AddButton("Report a bug",PAUSE_ITEM_REPORT);
    pause_menu.AddButton("Main menu",PAUSE_ITEM_TITLE);
    pause_menu.AddButton("Quit",PAUSE_ITEM_QUIT);
    pause_menu.SetFocus(0);
    menu_screen = CHASM_SCREEN_MAIN;
    f_pause_paused_game = false;
    if (world_scene && !world_scene->IsPhysicsPaused()){
        world_scene->PausePhysics(true);
        f_pause_paused_game = true;
    }
    f_pause_open = true;
    debug->Info("Pause menu\n");
}

void ApplicationChasm::ClosePause(){
    if (!f_pause_open.load()){
        return;
    }
    if (f_pause_paused_game && world_scene){
        world_scene->PausePhysics(false);
    }
    f_pause_paused_game = false;
    menu_screen = CHASM_SCREEN_MAIN;
    f_pause_open = false;
}

/*
    PHYSICS THREAD, from UpdateView: chasm_menu's pause, then the menu's own input while it is up - true then, so
    nothing under it moves. A list it opened goes first. Statistics opens the page over it; closing the page comes
    back here.
*/
bool ApplicationChasm::UpdatePause(InputController* input){
    if (pause_request.exchange(0) == 1){
        OpenPause();
    }
    if (!f_pause_open.load()){
        return false;
    }
    if (UpdateScreen(input)){
        return true;
    }
    UIMenuInput in;
    bool f_back = false;
    if (!ReadMenuInput(input,in,f_back)){
        return true;
    }
    if (f_back){
        ClosePause();
        return true;
    }
    UIMenuResult r = pause_menu.Update(in);
    if (r.event != UI_MENU_ACTIVATED){
        return true;
    }
    switch (r.id){
        case PAUSE_ITEM_RESUME: ClosePause(); break;
        case PAUSE_ITEM_SAVE:   OpenScreen(CHASM_SCREEN_SAVE); break;
        case PAUSE_ITEM_LOAD:   OpenScreen(CHASM_SCREEN_LOAD); break;
        case PAUSE_ITEM_STATS:  OpenStats(); break;
        case PAUSE_ITEM_SETTINGS: OpenScreen(CHASM_SCREEN_SETTINGS); break;
        case PAUSE_ITEM_REPORT: StartReport(); break;   //the menu put away first, so the picture is the game
        case PAUSE_ITEM_TITLE:
            //Main menu loses nothing: the colony stays loaded under the title, and Continue comes back to it.
            ClosePause();
            OpenTitle();
            break;
        case PAUSE_ITEM_QUIT:   OpenAsk(CHASM_ASK_QUIT); break;
    }
    return true;
}

//--- The lists ----------------------------------------------------------------------------------------------------------

//PHYSICS THREAD: a list, read afresh - the saves as they are on disk now.
void ApplicationChasm::OpenScreen(int screen){
    screen_page = 0;
    screen_saves.clear();
    screen_recordings.clear();
    if (screen == CHASM_SCREEN_LOAD || screen == CHASM_SCREEN_SAVE){
        screen_saves = ChasmSaveEntries();
    }
    if (screen == CHASM_SCREEN_REPLAY){
        screen_recordings = RecordingEntries();
    }
    if (screen == CHASM_SCREEN_SAVE){
        //Not over an autosave or the quick save: those are written by themselves, and would be again.
        screen_saves.erase(std::remove_if(screen_saves.begin(),screen_saves.end(),[](const ChasmSaveEntry& e){
            return e.kind == "auto" || e.kind == "quick";
        }),screen_saves.end());
    }
    menu_screen = screen;
    BuildScreen();
}

//PHYSICS THREAD: the list's items - a page of saves at a time, MENU_ROWS of them, with the way to the others.
void ApplicationChasm::BuildScreen(){
    int screen = menu_screen.load();
    screen_menu.Clear();
    screen_menu.layout.top = 0.30f;
    screen_menu.layout.item_h = 0.050f;
    screen_menu.layout.gap = 0.008f;
    screen_menu.layout.width = (screen == CHASM_SCREEN_NEW) ? 0.62f : 1.05f;
    if (screen == CHASM_SCREEN_SETTINGS){
        //From the store, so the screen always shows what is saved - archer's settings, without its Controls: there
        //are no keys to map yet, and no pad to play with.
        screen_menu.layout.top = 0.34f;
        screen_menu.layout.width = 0.84f;
        screen_menu.layout.item_h = 0.060f;
        screen_menu.layout.gap = 0.012f;
        const int msaa = settings.GetInt("msaa");
        screen_menu.AddChoice("Anti-aliasing",{"Off","4x","16x"},(msaa <= 1) ? 0 : ((msaa <= 8) ? 1 : 2),SETTINGS_ITEM_MSAA);
        screen_menu.AddSlider("Master volume",settings.GetFloat("volume_master"),0.0f,1.0f,0.05f,SETTINGS_ITEM_MASTER);
        screen_menu.AddSlider("Music",settings.GetFloat("volume_music"),0.0f,1.0f,0.05f,SETTINGS_ITEM_MUSIC);
        screen_menu.AddSlider("Sound effects",settings.GetFloat("volume_effects"),0.0f,1.0f,0.05f,SETTINGS_ITEM_EFFECTS);
        screen_menu.AddToggle("Start full screen",settings.GetBool("fullscreen"),SETTINGS_ITEM_FULLSCREEN);
        //The interface's size (ChasmHud.h): the step nearest what is saved, so a hand-edited value still shows one.
        std::vector<std::string> sizes;
        int size_at = 0;
        for (int i = 0; i < HUD_UI_SIZES; i++){
            sizes.push_back(std::to_string((int)(HUD_UI_SIZE_STEPS[i] * 100.0f + 0.5f)) + "%");
            size_at = (fabsf(HUD_UI_SIZE_STEPS[i] - settings.GetFloat("ui_size"))
                       < fabsf(HUD_UI_SIZE_STEPS[size_at] - settings.GetFloat("ui_size"))) ? i : size_at;
        }
        screen_menu.AddChoice("Interface size",sizes,size_at,SETTINGS_ITEM_UI_SIZE);
    }else if (screen == CHASM_SCREEN_NEW){
        for (int i = 0; i < CHASM_START_COUNT; i++){
            std::string label = std::string(CHASM_STARTS[i].name) + " - " + SizeName(CHASM_STARTS[i].fine_cells);
            screen_menu.AddButton(label.c_str(),SCREEN_ITEM_ROW + i);
        }
    }else{
        if (screen == CHASM_SCREEN_SAVE){
            screen_menu.AddButton("New save",SCREEN_ITEM_NEW_SAVE);
        }
        bool f_replays = screen == CHASM_SCREEN_REPLAY;
        int count = f_replays ? (int)screen_recordings.size() : (int)screen_saves.size();
        int from = screen_page * MENU_ROWS;
        int to = std::min(count,from + MENU_ROWS);
        for (int i = from; i < to; i++){
            std::string label = f_replays ? RecordingLabel(screen_recordings[i]) : EntryLabel(screen_saves[i]);
            screen_menu.AddButton(label.c_str(),SCREEN_ITEM_ROW + i);
        }
        if (screen != CHASM_SCREEN_SAVE && count == 0){
            screen_menu.SetEnabled(screen_menu.AddButton(f_replays ? "No recordings yet" : "No saves yet",SCREEN_ITEM_NONE),false);
        }
        if (to < count){
            screen_menu.AddButton(f_replays ? "Older recordings" : "Older saves",SCREEN_ITEM_OLDER);
        }
        if (screen_page > 0){
            screen_menu.AddButton(f_replays ? "Newer recordings" : "Newer saves",SCREEN_ITEM_NEWER);
        }
    }
    int back = screen_menu.AddButton("Back",SCREEN_ITEM_BACK);
    screen_menu.SetFocus(screen_menu.Item(0).f_enabled ? 0 : back);
}

//PHYSICS THREAD: the list in front's input, if one is - true then.
bool ApplicationChasm::UpdateScreen(InputController* input){
    int screen = menu_screen.load();
    if (screen == CHASM_SCREEN_MAIN){
        return false;
    }
    UIMenuInput in;
    bool f_back = false;
    if (!ReadMenuInput(input,in,f_back)){
        return true;
    }
    UIMenuResult r = screen_menu.Update(in);
    if (f_back || (r.event == UI_MENU_ACTIVATED && r.id == SCREEN_ITEM_BACK)){
        menu_screen = CHASM_SCREEN_MAIN;
        return true;
    }
    if (r.event == UI_MENU_CHANGED && screen == CHASM_SCREEN_SETTINGS){
        //Into the store and saved at once - a few hundred bytes - so closing the window by any route keeps it.
        //ApplySettings takes it up on the next pass.
        const UIMenuItem it = screen_menu.Item(r.item);
        switch (r.id){
            case SETTINGS_ITEM_MSAA:        settings.SetInt("msaa",(it.choice == 0) ? 1 : ((it.choice == 1) ? 4 : 16)); break;
            case SETTINGS_ITEM_MASTER:      settings.SetFloat("volume_master",it.value); break;
            case SETTINGS_ITEM_MUSIC:       settings.SetFloat("volume_music",it.value); break;
            case SETTINGS_ITEM_EFFECTS:     settings.SetFloat("volume_effects",it.value); break;
            case SETTINGS_ITEM_FULLSCREEN:  settings.SetBool("fullscreen",it.f_on); break;
            case SETTINGS_ITEM_UI_SIZE:     settings.SetFloat("ui_size",HUD_UI_SIZE_STEPS[std::max(0,std::min(HUD_UI_SIZES - 1,it.choice))]); break;
        }
        if (!settings.Save()){
            debug->Err("Could not save the settings to %s\n",settings.Path().c_str());
        }
        return true;
    }
    if (r.event != UI_MENU_ACTIVATED){
        return true;
    }
    if (r.id == SCREEN_ITEM_OLDER || r.id == SCREEN_ITEM_NEWER){
        screen_page += (r.id == SCREEN_ITEM_OLDER) ? 1 : -1;
        BuildScreen();
        //The focus where it was: on the way to the next page, so pressing on keeps paging.
        for (int i = 0; i < screen_menu.Count(); i++){
            if (screen_menu.Item(i).id == r.id){
                screen_menu.SetFocus(i);
            }
        }
        return true;
    }
    if (r.id == SCREEN_ITEM_NEW_SAVE){
        //Named for when it was written, which no other save has been: its title is in the file.
        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        char name[48];
        if (!lt || !strftime(name,sizeof(name),"save_%Y%m%d_%H%M%S",lt)){
            snprintf(name,sizeof(name),"save_%lld",(long long)now);
        }
        std::string error;
        ShowNote(WriteSave(name,"save",error) ? "Saved: " + SaveTitle() : error);
        menu_screen = CHASM_SCREEN_MAIN;
        return true;
    }
    int row = r.id - SCREEN_ITEM_ROW;
    if (row < 0){
        return true;
    }
    MenuAction a;
    if (screen == CHASM_SCREEN_NEW && row < CHASM_START_COUNT){
        a.kind = CHASM_ACT_NEW;
        a.start = row;
        Choose(a);
    }else if (screen == CHASM_SCREEN_REPLAY){
        if (row < (int)screen_recordings.size()){
            a.kind = CHASM_ACT_REPLAY;
            a.name = screen_recordings[row].path;
            Choose(a);
        }
    }else if (row < (int)screen_saves.size()){
        a.name = screen_saves[row].name;
        if (screen == CHASM_SCREEN_LOAD){
            a.kind = CHASM_ACT_LOAD;
            Choose(a);
        }else{
            a.kind = CHASM_ACT_OVERWRITE;
            ask_action = a;
            OpenAsk(CHASM_ASK_OVERWRITE,"",screen_saves[row].title);
        }
    }
    return true;
}

//--- Loading and starting -----------------------------------------------------------------------------------------------

//A load or a new map, asked first over a colony in play - whose yes autosaves it (UpdateAsk).
void ApplicationChasm::Choose(const MenuAction& a){
    //A lost colony has nothing left to lose (demo_plan.md step 7): left without a question.
    if (f_game_on.load() && !ColonyLost()){
        ask_action = a;
        OpenAsk(CHASM_ASK_LEAVE);
        return;
    }
    Busy(a);
}

void ApplicationChasm::Busy(const MenuAction& a){
    busy_action = a;
    busy_frame = overlay_frames.load();
    std::lock_guard<std::mutex> lock(menu_text_mutex);
    menu_busy = (a.kind == CHASM_ACT_NEW) ? "Making the map ..." : (a.kind == CHASM_ACT_REPLAY) ? "Starting the replay ..."
                                                                                                : "Loading ...";
}

/*
    PHYSICS THREAD, from UpdateView or UpdateTitle: the load or the new map waiting, once "Loading" has been drawn -
    two frames counted, so one whole frame was drawn after it was asked for. Then into the colony, no menu left up
    over it; a load that cannot be made says why instead.
*/
bool ApplicationChasm::RunBusy(){
    if (busy_action.kind == CHASM_ACT_NONE){
        return false;
    }
    if (overlay_frames.load() - busy_frame < 2){
        return true;
    }
    MenuAction a = busy_action;
    busy_action = MenuAction();
    std::string error;
    bool f_ok = true;
    if (a.kind == CHASM_ACT_LOAD){
        f_ok = LoadSaveNamed(a.name,error);
    }else if (a.kind == CHASM_ACT_NEW){
        StartNewGame(a.start);
    }else if (a.kind == CHASM_ACT_REPLAY){
        //Core switches to the world and puts back the recording's start (RestoreRecordingState) on its next passes;
        //the recording running is saved first, as it would be at a scene switch.
        f_menu_replay = true;
        RequestRecordingStop();
        f_ok = RequestReplay(a.name,true,error);
        if (!f_ok){
            f_menu_replay = false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        menu_busy.clear();
    }
    if (!f_ok){
        OpenAsk(CHASM_ASK_NOTE,(a.kind == CHASM_ACT_REPLAY) ? "This recording cannot be replayed" : "This save cannot be loaded",
                error);
        return true;
    }
    //A colony entered is recorded from here, if always recording - a replay is not.
    RecordFromHere();
    CloseStats();
    ClosePause();
    menu_screen = CHASM_SCREEN_MAIN;
    CloseTitle();
    if (!error.empty()){
        ShowNote(error);
    }
    return true;
}

/*
    PHYSICS THREAD: a save by name, as the menus load one. Its map is made again from its settings and compared BEFORE
    anything is replaced (demo_plan.md): a save from a build whose generator made another map says so, instead of
    loading onto the wrong plots - and the colony in play is still there. Refused plots (`error`, the load still
    made) are the rules' say on a stale save, as LoadSave says.
*/
bool ApplicationChasm::LoadSaveNamed(const std::string& name, std::string& error){
    nlohmann::json j;
    ChasmSave save;
    if (!ChasmSaveRead(name,j,error) || !ChasmSaveFromJson(j,save,error)){
        return false;
    }
    const nlohmann::json meta = j.value("meta",nlohmann::json::object());
    auto meta_text = [&](const char* key){
        return (meta.is_object() && meta.contains(key) && meta[key].is_string()) ? meta[key].get<std::string>() : std::string();
    };
    if (!save.world_hash.empty()){
        std::shared_ptr<const ChasmWorld> w = GetWorld();
        uint64_t hash = 0;
        if (w && w->grid->settings == save.settings){
            hash = w->grid->Hash();
        }else{
            std::unique_ptr<Grid> g(new Grid());
            g->Generate(save.settings);
            hash = g->Hash();
        }
        char hex[32];
        snprintf(hex,sizeof(hex),"%016llx",(unsigned long long)hash);
        if (save.world_hash != hex){
            std::string by = meta_text("build");
            error = "It was made by " + (by.empty() ? std::string("another build") : by) +
                    ", whose map is not this build's.";
            debug->Warn("Load %s refused: saved on world %s, these settings now give %s\n",name.c_str(),
                        save.world_hash.c_str(),hex);
            return false;
        }
    }
    std::string refused;
    LoadSave(save,refused);
    std::string colony = meta_text("colony");
    EnteredColony(colony.empty() ? "Seed " + std::to_string(save.settings.seed) : colony);
    error = refused;
    debug->Info("Loaded %s: %s\n",name.c_str(),SaveTitle().c_str());
    return true;
}

//PHYSICS THREAD: a new colony on one of the four maps - its camp, the first dawn of spring, nothing explored.
void ApplicationChasm::StartNewGame(int start){
    const ChasmStart& st = CHASM_STARTS[std::max(0,std::min(CHASM_START_COUNT - 1,start))];
    GridSettings s;
    s.seed = st.seed;
    s.target_fine_cells = st.fine_cells;
    RegenerateGrid(s);
    next_settings = s;
    //The colony's start now rather than on the next tick, so the view can be put on its camp.
    EnsureZonesWorld();
    EnteredColony(st.name);
    debug->Info("New game: %s, seed %u, %i fine cells\n",st.name,st.seed,st.fine_cells);
}

//PHYSICS THREAD: a colony entered, loaded or new - its name for its saves, nothing in hand, the view on it.
void ApplicationChasm::EnteredColony(const std::string& colony){
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        colony_name = colony;
    }
    f_game_on = true;
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    autosave_season = (d.year - 1) * CALENDAR_SEASONS + d.season;
    paint_tool = CHASM_TOOL_SELECT;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        play_selected = PlayPick();
        play_group.clear();
        play_pick_version++;
    }
    FrameColony();
}

//--- Saving -------------------------------------------------------------------------------------------------------------

//Any thread: what a save of the colony now is titled - its name, its day.
std::string ApplicationChasm::SaveTitle(){
    std::string colony;
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        colony = colony_name;
    }
    if (colony.empty()){
        std::shared_ptr<const Grid> g = GetGrid();
        colony = g ? "Seed " + std::to_string(g->settings.seed) : "Chasm";
    }
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    char buf[128];
    snprintf(buf,sizeof(buf),"%s - year %i, %s %i",colony.c_str(),d.year,CalendarSeasonName(d.season),d.day);
    return buf;
}

//PHYSICS THREAD (it reads the published copies, as MakeSave does): the colony, with its meta, to saves/<name>.json.
bool ApplicationChasm::WriteSave(const std::string& name, const char* kind, std::string& error){
    nlohmann::json j = ChasmSaveToJson(MakeSave());
    std::string colony;
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        colony = colony_name;
    }
    std::string title = SaveTitle();
    j["meta"] = nlohmann::json{{"title",title},{"kind",kind},{"colony",colony},{"build",BuildName()}};
    if (!ChasmSaveWrite(name,j,error)){
        debug->Warn("Save %s: %s\n",name.c_str(),error.c_str());
        return false;
    }
    debug->Info("Saved %s (%s): %s\n",name.c_str(),kind,title.c_str());
    return true;
}

/*
    PHYSICS THREAD: into the oldest of the autosave slots - or one not yet written - but never into `keep`. An autosave
    is where a recording starts, if always recording (RecordFromHere) - unless the colony is being left, when the
    recording running is saved instead.
*/
void ApplicationChasm::Autosave(const std::string& keep, bool f_record){
    std::string slot;
    int64_t oldest = std::numeric_limits<int64_t>::max();
    for (int i = 1; i <= AUTOSAVE_SLOTS; i++){
        std::string name = "autosave_" + std::to_string(i);
        int64_t t = ChasmSaveWritten(name);
        if (name != keep && t < oldest){
            oldest = t;
            slot = name;
        }
    }
    std::string error;
    //A lost colony is not kept: it would push a colony worth going back to out of the slots. Nor is a test map
    //(test_maps_plan.md), for the same reason - it is saved by hand or not at all.
    std::shared_ptr<const Grid> g = GetGrid();
    bool f_test = g && !GridKindHasChasm(g->settings.kind);
    if (!slot.empty() && !ColonyLost() && !f_test && WriteSave(slot,"auto",error)){
        ShowNote("Autosaved");
    }
    if (f_record){
        RecordFromHere();
    }
}

/*
    PHYSICS THREAD, from UpdateView: the first dawn of a season autosaves - the season one on from the last pass's,
    on its first day, so a load, a tool's or a replay's start, landing anywhere in the year does not. Not while
    replaying: a replay is someone's run again, not a colony to keep.
*/
void ApplicationChasm::UpdateAutosave(){
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    int key = (d.year - 1) * CALENDAR_SEASONS + d.season;
    bool f_dawn = autosave_season >= 0 && key == autosave_season + 1 && d.day == 1;
    autosave_season = key;
    if (f_dawn && f_game_on.load() && !main_scene->inputcontroller->IsReplaying()){
        Autosave();
    }
}

//PHYSICS THREAD, from UpdateView in the game: F5 saves, F8 loads it again - no question, which is what they are for -
//and F12 reports a bug (ApplicationChasmRecord.cpp).
void ApplicationChasm::QuickKeys(InputController* input){
    bool f_save = input->WasKeyPressed(INPUT_CHASM_QUICK_SAVE);
    bool f_load = input->WasKeyPressed(INPUT_CHASM_QUICK_LOAD);
    bool f_report = input->WasKeyPressed(INPUT_CHASM_REPORT);
    if (!KeysLive()){
        return;
    }
    if (f_report){
        StartReport();
    }else if (f_save){
        std::string error;
        ShowNote(WriteSave(QUICK_SAVE,"quick",error) ? "Quick saved: " + SaveTitle() : error);
    }else if (f_load){
        if (ChasmSaveWritten(QUICK_SAVE) < 0){
            ShowNote("No quick save yet: F5 makes one");
            return;
        }
        MenuAction a;
        a.kind = CHASM_ACT_LOAD;
        a.name = QUICK_SAVE;
        Busy(a);
    }
}

void ApplicationChasm::ShowNote(const std::string& note){
    std::lock_guard<std::mutex> lock(menu_text_mutex);
    menu_note = note;
    menu_note_at = std::chrono::steady_clock::now();
}

//--- Settings -----------------------------------------------------------------------------------------------------------

/*
    THE PLAYER'S SETTINGS (demo_plan.md step 3; core/Settings.h): archer's - anti-aliasing, the master, music and effects
    volumes, and starting full screen - in settings.json beside the exe, from disk only, never a baked asset. Read in
    the constructor, so "start full screen" is known before the window is made. The volumes default to 1, so a first
    run sounds as the cue table and the score were mixed.
*/
void ApplicationChasm::LoadSettings(){
    settings.DeclareBool("fullscreen",false);
    settings.DeclareInt("msaa",4,1,16);
    settings.DeclareFloat("volume_master",1.0f,0.0f,1.0f);
    settings.DeclareFloat("volume_music",1.0f,0.0f,1.0f);
    settings.DeclareFloat("volume_effects",1.0f,0.0f,1.0f);
    settings.DeclareFloat("ui_size",1.0f,HUD_UI_SIZE_STEPS[0],HUD_UI_SIZE_STEPS[HUD_UI_SIZES - 1]);     //ChasmHud.h
    std::string report;
    bool f_loaded = settings.Load(GetExecutableDirectory() + "/settings.json",&report);
    if (!report.empty()){
        debug->Warn("Settings %s:\n%s",settings.Path().c_str(),report.c_str());
    }
    debug->Info("Settings %s %s\n",f_loaded ? "loaded from" : "defaulted - no file at",settings.Path().c_str());
    f_start_fullscreen = settings.GetBool("fullscreen");
}

/*
    PHYSICS THREAD, every pass from UpdateView: the settings into what the game reads - only on a CHANGE (a new
    revision), so chasm_sound's volume, which writes the same master, is not overruled every pass. The music's bus and
    the cue table's layer are set whenever they differ: nothing else sets either. All of it the view's - buses and the
    renderer, never the rules - so a recording replays the same at any setting.
*/
void ApplicationChasm::ApplySettings(){
    uint32_t rev = settings.Revision();
    if (rev != settings_applied){
        settings_applied = rev;
        sound_volume = settings.GetFloat("volume_master");
        music_volume = settings.GetFloat("volume_music");
        effects_volume = settings.GetFloat("volume_effects");
        int msaa = settings.GetInt("msaa");
        if (renderer && renderer->aa_samples != msaa){
            renderer->RequestAASamples(msaa);
        }
    }
#ifdef USE_SOUND
    if (soundsystem && soundsystem->f_initialised){
        //The score's own bus, over its three players' (ApplicationChasmMusic.cpp); and every sound but the music, the
        //cue table's layer (ApplicationChasmSound.cpp, SetupSound) - never a bus of the table, whose gains it sets.
        auto set = [this](int bus, float gain){
            if (bus >= 0 && bus != SOUND_BUS_MASTER && soundsystem->GetBusGain(bus) != gain){
                soundsystem->SetBusGain(bus,gain);
            }
        };
        set(music_bus,music_volume.load());
        set(sounds_bus,effects_volume.load());
    }
#endif
}

//--- Drawing ------------------------------------------------------------------------------------------------------------

//RENDER THREAD, from DrawOverlay in the HUD's place: the frozen game dimmed, and the menu or the list it opened.
void ApplicationChasm::DrawPause(float s){
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    overlay->AddRect(vec2(0.0f,0.0f),vec2(w,h),0.0f,MENU_SHADE);
    //The build in the corner, as on the title: a screenshot of a pause names what it was taken in.
    DrawHudBuild(overlay,w,h,s);
    if (menu_screen.load() != CHASM_SCREEN_MAIN){
        DrawScreen(s);
        return;
    }
    DrawMenuPanel(pause_menu,"PAUSED",SaveTitle(),s,MENU_BOX,true);
}

//RENDER THREAD: the list in front, over the title or the pause menu's shade.
void ApplicationChasm::DrawScreen(float s){
    int screen = menu_screen.load();
    std::string heading;
    std::string detail;
    if (screen == CHASM_SCREEN_NEW){
        heading = "NEW GAME";
        detail = std::to_string(EconomySettlerCount()) + " settlers and their camp, on the first day of spring";
    }else if (screen == CHASM_SCREEN_LOAD){
        heading = "LOAD";
    }else if (screen == CHASM_SCREEN_REPLAY){
        heading = "REPLAY A RECORDING";
        detail = "A run played again from where it began - STOP on its bar ends it";
    }else if (screen == CHASM_SCREEN_SETTINGS){
        heading = "SETTINGS";
        detail = "Kept as they change. Alt+Enter switches full screen now";
    }else{
        heading = "SAVE";
        detail = SaveTitle();
    }
    DrawMenuPanel(screen_menu,heading,detail,s,MENU_BOX,true);
}

//RENDER THREAD, last over everything: "Loading" while it waits, and a note for its few seconds.
void ApplicationChasm::DrawBusyAndNote(float s){
    std::string busy;
    std::string note;
    float age = 0.0f;
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        busy = menu_busy;
        note = menu_note;
        age = std::chrono::duration<float>(std::chrono::steady_clock::now() - menu_note_at).count();
    }
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    if (!busy.empty()){
        overlay->AddRect(vec2(0.0f,0.0f),vec2(w,h),0.0f,MENU_SHADE);
        vec2 size = overlay->MeasureText(busy.c_str(),26.0f * s);
        vec2 c(w * 0.5f,h * 0.5f);
        vec2 half(size.x * 0.5f + 40.0f * s,34.0f * s);
        overlay->AddRect(c - half,c + half,12.0f * s,MENU_BOX);
        overlay->AddText(busy.c_str(),vec2(c.x,c.y + 9.0f * s),26.0f * s,HUD_TEXT,UI_ALIGN_CENTER);
    }
    if (!note.empty() && age < MENU_NOTE_SECONDS){
        //Fading over its last half second.
        float k = std::min(1.0f,(MENU_NOTE_SECONDS - age) * 2.0f);
        vec2 size = overlay->MeasureText(note.c_str(),17.0f * s);
        vec2 c(w * 0.5f,h - 70.0f * s);
        vec2 half(size.x * 0.5f + 24.0f * s,22.0f * s);
        overlay->AddRect(c - half,c + half,10.0f * s,HudFaded(MENU_BOX,(uint8_t)(225.0f * k)));
        overlay->AddText(note.c_str(),vec2(c.x,c.y + 6.0f * s),17.0f * s,HudFaded(HUD_TEXT,(uint8_t)(255.0f * k)),UI_ALIGN_CENTER);
    }
}
