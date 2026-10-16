#include "ApplicationChasm.h"
#include "Debug.h"

#include <algorithm>
#include <cmath>

#ifdef USE_SOUND
#include "MusicPlayer.h"
#include "MusicScore.h"
#endif

static Debugger* debug = new Debugger("ChasmMusic",DEBUG_ALL);

/*
    THE MUSIC (areas_plan.md, "The music"; sound_plan.md step 7; apps/music/readme.md, "chasm.json").

    WHAT IT SOUNDS LIKE IS NOT IN THIS FILE. The score is designed and tuned in apps/music and handed over with
    `make publish GAME=chasm SCORE=chasm`, which writes assets/music/chasm.json and its samples as .ogg. Run that again
    after a change there; the game plays whatever it was last given.

    THREE PLAYERS ON THE ONE SCORE, each on a bus of its own under `music`:
      - the LAND plays the season's section - spring, summer, autumn, winter - and is sent the next when the calendar
        turns, which it takes at its next bar line with the score's crossfade. It plays under the title too;
      - the CLOUDS and the CHASM play those sections as LAYERS over it, each faded in by its SHARE of the view: how
        much of the ground round the view's centre is unexplored, and how much is chasm someone has seen. The land
        is turned down under them, so two keys are never heard at full together. Only while the world shows.
    A layer silent for a few seconds is paused, so its engine costs nothing, and let go again the moment it is wanted.

    It plays on in a pause, as the water does: a colony is often paused to think. The score's stingers come from the
    cue table's `music` action - the chasm's first sight is one (`reveal`).

    PHYSICS THREAD, every pass, from UpdateSound. Presentation only: nothing here reaches the rules.
*/

#define CHASM_MUSIC_SCORE       "music/chasm.json"
//A layer is heard from FROM of the view to FULL, on a smooth step; the land goes down by DUCK under a layer at full.
#define MUSIC_SHARE_FROM        0.15f
#define MUSIC_SHARE_FULL        0.60f
#define MUSIC_LAND_DUCK         0.70f
//The layers move over about this long - the camera sweeping past a patch of cloud is not a swell.
#define MUSIC_EASE_S            3.0f
//The view's ground: within this share of the camera's distance of its centre, looked at on a grid this many across,
//every this many passes; the middle counts most.
#define MUSIC_VIEW_REACH        0.8f
#define MUSIC_VIEW_SAMPLES      15
#define MUSIC_SHARE_PASSES      5
//A layer silent this long is paused.
#define MUSIC_LAYER_REST_S      4.0f

namespace {

const char* PlayerName(int i){
    return (i == CHASM_MUSIC_CLOUDS) ? "clouds" : (i == CHASM_MUSIC_CHASM) ? "chasm" : "land";
}

//0 below FROM, 1 from FULL, smooth between.
float LayerLevel(float share){
    float t = std::max(0.0f,std::min(1.0f,(share - MUSIC_SHARE_FROM) / (MUSIC_SHARE_FULL - MUSIC_SHARE_FROM)));
    return t * t * (3.0f - 2.0f * t);
}

} //namespace

void ApplicationChasm::SetupMusic(){
#ifdef USE_SOUND
    if (!soundsystem || !soundsystem->f_initialised){
        return;
    }
    auto score = std::make_shared<MusicScore>();
    std::string error;
    if (!LoadMusicScore(CHASM_MUSIC_SCORE,*score,error)){
        debug->Warn("Music: %s - playing without it\n",error.c_str());
        return;
    }
    music_score = score;
    int bus = soundsystem->AddBus("music");
    music_bus = (bus >= 0) ? bus : SOUND_BUS_MASTER;
    static const char* bus_names[CHASM_MUSIC_PLAYERS] = {"music_land","music_clouds","music_chasm"};
    //The land starts in the season the world is in - a debug build starts in one; a ship build's title in spring.
    music_season = CalendarSeasonName(CalendarDateOf(calendar_tick).season);
    for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
        int b = soundsystem->AddBus(bus_names[i],music_bus);
        music_buses[i] = (b >= 0) ? b : music_bus;
        //The layers from silence; the land at once.
        music_layer[i] = (i == CHASM_MUSIC_LAND) ? 1.0f : 0.0f;
        soundsystem->SetBusGain(music_buses[i],music_layer[i]);
        const char* section = (i == CHASM_MUSIC_LAND) ? music_season.c_str() : PlayerName(i);
        if (score->SectionIndex(section) < 0){
            debug->Warn("Music: the score has no section '%s'\n",section);
        }
        music[i] = new MusicPlayer();
        //Each its own seed, so the layers are not the same notes. A layer starts held: silent until it is wanted -
        //and already counted as at rest, so it is not let go on the first pass only to be held again.
        f_music_held[i] = (i != CHASM_MUSIC_LAND);
        music_quiet[i] = f_music_held[i] ? (int)(MUSIC_LAYER_REST_S * CALENDAR_TICKS_PER_SECOND) + 1 : 0;
        music[i]->Start(soundsystem,score,(uint32_t)(i + 1),music_buses[i],section,f_music_held[i]);
    }
    /*
        The cue table's `music` action, for the moments the rules make: the land is sent
          stinger  one of the score's stingers - the chasm's first sight is `reveal`
          section  a section by name, "" the next; `now` skips the wait for the bar line
        A request, as in archer: the music takes it on its own clock.
    */
    cues.SetActionHandler("music",[this](const CueAction& a){
        if (!music[CHASM_MUSIC_LAND]){
            return;
        }
        auto str = [&a](const char* key, std::string& out){
            auto it = a.params.find(key);
            if (it == a.params.end() || !it->is_string()){
                return false;
            }
            out = it->get<std::string>();
            return true;
        };
        auto it_now = a.params.find("now");
        const bool f_now = it_now != a.params.end() && it_now->is_boolean() && it_now->get<bool>();
        std::string name;
        if (str("stinger",name)){
            MusicEvent e;
            e.type = MusicEvent::STINGER;
            e.name = name;
            music[CHASM_MUSIC_LAND]->Post(e);
        }
        if (str("section",name)){
            MusicEvent e;
            e.type = MusicEvent::SECTION;
            e.name = name;
            e.f_now = f_now;
            music[CHASM_MUSIC_LAND]->Post(e);
        }
    });
    debug->Info("Music: %s, %i sections, the land in %s\n",CHASM_MUSIC_SCORE,(int)score->sections.size(),
                music_season.c_str());
#endif
}

/*
    THE VIEW'S SHARES: round the view's centre, on a grid out to MUSIC_VIEW_REACH of the camera's distance, each point
    weighed by how near the middle it is - how much of the land there is unexplored, and how much is chasm that has
    been seen. Off the land - past the grid's ragged edge, a balcony or an island - is not counted either way.
*/
void ApplicationChasm::MeasureMusicShares(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const Exploration> ex = GetExploration();
    if (!w || !w->areas || !w->sight || !ex || ex->Empty()){
        music_share[CHASM_MUSIC_CLOUDS] = 0.0f;
        music_share[CHASM_MUSIC_CHASM] = 0.0f;
        return;
    }
    if (music_cells_world != w.get()){
        music_cells_world = w.get();
        music_cells.assign((size_t)w->sight->width * w->sight->height,0);
        for (const Area& a : w->areas->areas){
            for (int c : a.cells){
                uint8_t mark = (a.kind == AREA_CHASM) ? 2 : 1;
                music_cells[(size_t)c] = std::max(music_cells[(size_t)c],mark);
            }
        }
    }
    const float reach = cam_distance * MUSIC_VIEW_REACH;
    const float step = 2.0f * reach / (float)(MUSIC_VIEW_SAMPLES - 1);
    float total = 0.0f;
    float clouds = 0.0f;
    float chasm = 0.0f;
    for (int i = 0; i < MUSIC_VIEW_SAMPLES; i++){
        for (int j = 0; j < MUSIC_VIEW_SAMPLES; j++){
            vec2 off(-reach + step * (float)i,-reach + step * (float)j);
            float d = off.length() / reach;
            int c = (d <= 1.0f) ? ex->CellAt(vec2(camera_target.x,camera_target.z) + off) : -1;
            if (c < 0 || c >= (int)music_cells.size() || !music_cells[(size_t)c]){
                continue;
            }
            float weight = 1.0f - d * d;
            total += weight;
            if (!ex->cells[(size_t)c]){
                clouds += weight;
            }else if (music_cells[(size_t)c] == 2){
                chasm += weight;
            }
        }
    }
    music_share[CHASM_MUSIC_CLOUDS] = (total > 0.0f) ? clouds / total : 0.0f;
    music_share[CHASM_MUSIC_CHASM] = (total > 0.0f) ? chasm / total : 0.0f;
}

void ApplicationChasm::UpdateMusic(){
#ifdef USE_SOUND
    if (!music_score || !music[CHASM_MUSIC_LAND]){
        return;
    }
    //The season: sent once when it turns, or when a load or a new map lands in another.
    std::string season = CalendarSeasonName(CalendarDateOf(calendar_tick).season);
    if (season != music_season){
        music_season = season;
        MusicEvent e;
        e.type = MusicEvent::SECTION;
        e.name = season;
        music[CHASM_MUSIC_LAND]->Post(e);
    }
    //The layers only while the world shows: under the title, the land alone.
    const bool f_shown = !TitleShowing() && world_scene && main_scene == world_scene;
    if (!f_shown){
        music_share[CHASM_MUSIC_CLOUDS] = 0.0f;
        music_share[CHASM_MUSIC_CHASM] = 0.0f;
    }else if (sound_pass % MUSIC_SHARE_PASSES == 0){
        MeasureMusicShares();
    }
    const float ease = std::min(1.0f,1.0f / (MUSIC_EASE_S * (float)CALENDAR_TICKS_PER_SECOND));
    float over = 0.0f;
    for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
        music_layer[i] += (LayerLevel(music_share[i]) - music_layer[i]) * ease;
        over = std::max(over,music_layer[i]);
    }
    music_layer[CHASM_MUSIC_LAND] = 1.0f - MUSIC_LAND_DUCK * over;
    for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
        soundsystem->SetBusGain(music_buses[i],music_layer[i]);
        if (i == CHASM_MUSIC_LAND){
            continue;
        }
        //A layer at rest pauses its engine, and is let go the moment its share asks for it again.
        bool f_wanted = LayerLevel(music_share[i]) > 0.0f || music_layer[i] > 0.001f;
        music_quiet[i] = f_wanted ? 0 : music_quiet[i] + 1;
        bool f_hold = music_quiet[i] > (int)(MUSIC_LAYER_REST_S * CALENDAR_TICKS_PER_SECOND);
        if (f_hold != f_music_held[i]){
            f_music_held[i] = f_hold;
            MusicEvent e;
            e.type = MusicEvent::PAUSE;
            e.value = f_hold ? 1.0f : 0.0f;
            e.f_now = true;             //silent either way: the bus does the fading
            music[i]->Post(e);
        }
    }
#endif
}

#ifdef USE_MCP
//For chasm_sound: what each player is playing, its share of the view and its level.
json ApplicationChasm::MusicStatusJson(){
    json out = json::object();
#ifdef USE_SOUND
    if (!music_score){
        out["loaded"] = false;
        return out;
    }
    out["loaded"] = true;
    out["score"] = CHASM_MUSIC_SCORE;
    out["season"] = music_season;
    for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
        if (!music[i]){
            continue;
        }
        MusicStatus st = music[i]->GetStatus();
        std::string section = (st.section >= 0 && st.section < (int)st.section_names.size()) ? st.section_names[st.section] : "";
        std::string pending = (st.pending_section >= 0 && st.pending_section < (int)st.section_names.size())
                              ? st.section_names[st.pending_section] : "";
        out[PlayerName(i)] = json{{"section",section},{"pending",pending},{"bar",st.bar},
                                  {"share",std::round(music_share[i] * 100.0f) / 100.0f},
                                  {"level",std::round(music_layer[i] * 1000.0f) / 1000.0f},{"held",f_music_held[i]}};
    }
#endif
    return out;
}
#endif
