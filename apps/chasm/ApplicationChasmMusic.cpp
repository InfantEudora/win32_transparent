#include "ApplicationChasm.h"
#include "Debug.h"

#include <algorithm>
#include <cmath>
#include <stdio.h>

#ifdef USE_SOUND
#include "MusicPlayer.h"
#include "MusicScore.h"
#endif
#ifdef USE_IMGUI
#include "imgui.h"
#endif

static Debugger* debug = new Debugger("ChasmMusic",DEBUG_ALL);

/*
    THE MUSIC (areas_plan.md, "The music"; sound_plan.md step 7; apps/music/readme.md, "chasm.json").

    WHAT IT SOUNDS LIKE IS NOT IN THIS FILE. The score is designed and tuned in apps/music and handed over with
    `make publish GAME=chasm SCORE=chasm`, which writes assets/music/chasm.json and its samples as .ogg. Run that again
    after a change there; the game plays whatever it was last given.

    FOUR PLAYERS ON THE ONE SCORE, each on a bus of its own under `music`:
      - the LAND plays the season's section - spring, summer, autumn, winter - and is sent the next when the calendar
        turns, which it takes at its next bar line with the score's crossfade. It plays under the title too;
      - the CLOUDS, the CHASM and the DESERT play those sections as LAYERS over it, each faded in by its SHARE of the
        view: how much of the ground round the view's centre is unexplored, how much is chasm someone has seen, and how
        much is desert someone has seen. The land is turned down under them, so two keys are never heard at full
        together - and out altogether under the desert, which has no seasons: it takes the land's place rather than
        lying over it. Only while the world shows.
    A layer silent for a few seconds is paused, so its engine costs nothing, and let go again the moment it is wanted.

    It plays on in a pause, as the water does: a colony is often paused to think. The score's stingers come from the
    cue table's `music` action - the chasm's first sight is one (`reveal`).

    PHYSICS THREAD, every pass, from UpdateSound. Presentation only: nothing here reaches the rules.
*/

#define CHASM_MUSIC_SCORE       "music/chasm.json"
//A layer is heard from FROM of the view to FULL, on a smooth step; the land goes down by DUCK under a layer at full -
//by DESERT_DUCK under the desert, whose hijaz clashes with every season's scale.
#define MUSIC_SHARE_FROM        0.15f
#define MUSIC_SHARE_FULL        0.60f
#define MUSIC_LAND_DUCK         0.70f
#define MUSIC_DESERT_DUCK       1.00f
//music_cells: any area's ground, the chasm's floor, the desert. One cell can be more than one, on an area's edge.
#define MUSIC_CELL_LAND         1
#define MUSIC_CELL_CHASM        2
#define MUSIC_CELL_DESERT       4
//The layers move over about this long - the camera sweeping past a patch of cloud is not a swell.
#define MUSIC_EASE_S            3.0f
//The view's ground: within this share of the camera's distance of its centre, looked at on a grid this many across,
//every this many passes; the middle counts most.
#define MUSIC_VIEW_REACH        0.8f
#define MUSIC_VIEW_SAMPLES      15
#define MUSIC_SHARE_PASSES      5
//A layer silent this long is paused.
#define MUSIC_LAYER_REST_S      4.0f
//What the game sent the music, kept for the Music panel and chasm_sound: the last this many.
#define MUSIC_SENT_KEPT         12
//The suspense in winter's storm at full (weather_plan.md), from the score's own. Current tuning.
#define MUSIC_STORM_SUSPENSE    0.75f

namespace {

const char* PlayerName(int i){
    return (i == CHASM_MUSIC_CLOUDS) ? "clouds" : (i == CHASM_MUSIC_CHASM) ? "chasm" : (i == CHASM_MUSIC_DESERT) ? "desert" : "land";
}

float LayerDuck(int i){
    return (i == CHASM_MUSIC_DESERT) ? MUSIC_DESERT_DUCK : MUSIC_LAND_DUCK;
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
    static const char* bus_names[CHASM_MUSIC_PLAYERS] = {"music_land","music_clouds","music_chasm","music_desert"};
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
            PlayMusicStinger(name,"cue " + a.cue);
        }
        if (str("section",name)){
            MusicEvent e;
            e.type = MusicEvent::SECTION;
            e.name = name;
            e.f_now = f_now;
            music[CHASM_MUSIC_LAND]->Post(e);
            MusicSend("section " + (name.empty() ? std::string("(the next)") : name) + " - cue " + a.cue);
        }
    });
    MusicSend("section " + music_season + " - the start");
    debug->Info("Music: %s, %i sections, the land in %s\n",CHASM_MUSIC_SCORE,(int)score->sections.size(),
                music_season.c_str());
#endif
}

void ApplicationChasm::MusicSend(const std::string& what){
    music_sent.push_back(MusicSent{sound_pass,calendar_tick,what});
    if (music_sent.size() > MUSIC_SENT_KEPT){
        music_sent.erase(music_sent.begin());
    }
}

//The land plays it: a cue's (`why` names it), or the Music panel's. One the score has not got is told, not posted - a cue
//table naming a stinger the score has lost shows here.
void ApplicationChasm::PlayMusicStinger(const std::string& name, const std::string& why){
#ifdef USE_SOUND
    if (!music[CHASM_MUSIC_LAND] || !music_score){
        return;
    }
    auto it = std::find_if(music_score->stingers.begin(),music_score->stingers.end(),[&name](const MusicStingerDef& s){
        return s.name == name;
    });
    if (it == music_score->stingers.end()){
        MusicSend("stinger " + name + " - not in the score (" + why + ")");
        return;
    }
    MusicEvent e;
    e.type = MusicEvent::STINGER;
    e.name = name;
    music[CHASM_MUSIC_LAND]->Post(e);
    MusicSend("stinger " + name + " - " + why);
#endif
}

/*
    The score's two axes, which nothing in the game moves yet: SUSPENSE, calm to tense, and BRIGHTNESS, dark to bright (core
    MusicEngine.h) - held for every player at once, to hear what they would do. Below 0, the player's own again.
*/
void ApplicationChasm::HoldMusicMood(float suspense, float brightness){
    music_mood_held[0] = suspense;
    music_mood_held[1] = brightness;
#ifdef USE_SOUND
    const MusicParams own;
    for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
        if (!music[i]){
            continue;
        }
        MusicParams p = music[i]->GetParams();
        p.suspense = (suspense >= 0.0f) ? std::min(1.0f,suspense) : own.suspense;
        p.brightness = (brightness >= 0.0f) ? std::min(1.0f,brightness) : own.brightness;
        music[i]->SetParams(p);
    }
#endif
}

/*
    THE VIEW'S SHARES: round the view's centre, on a grid out to MUSIC_VIEW_REACH of the camera's distance, each point
    weighed by how near the middle it is - how much of the land there is unexplored, how much is chasm that has been
    seen, and how much is desert that has been seen. Off the land - past the grid's ragged edge, a balcony or an
    island - is not counted either way.
*/
void ApplicationChasm::MeasureMusicShares(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const Exploration> ex = GetExploration();
    if (!w || !w->areas || !w->sight || !ex || ex->Empty()){
        for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
            music_share[i] = 0.0f;
        }
        return;
    }
    if (music_cells_world != w.get()){
        music_cells_world = w.get();
        music_cells.assign((size_t)w->sight->width * w->sight->height,0);
        for (const Area& a : w->areas->areas){
            uint8_t mark = MUSIC_CELL_LAND | ((a.kind == AREA_CHASM) ? MUSIC_CELL_CHASM : 0)
                                           | ((a.kind == AREA_DESERT) ? MUSIC_CELL_DESERT : 0);
            for (int c : a.cells){
                music_cells[(size_t)c] |= mark;
            }
        }
    }
    const float reach = cam_distance * MUSIC_VIEW_REACH;
    const float step = 2.0f * reach / (float)(MUSIC_VIEW_SAMPLES - 1);
    float total = 0.0f;
    float clouds = 0.0f;
    float chasm = 0.0f;
    float desert = 0.0f;
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
                continue;
            }
            if (music_cells[(size_t)c] & MUSIC_CELL_CHASM){
                chasm += weight;
            }
            if (music_cells[(size_t)c] & MUSIC_CELL_DESERT){
                desert += weight;
            }
        }
    }
    music_share[CHASM_MUSIC_CLOUDS] = (total > 0.0f) ? clouds / total : 0.0f;
    music_share[CHASM_MUSIC_CHASM] = (total > 0.0f) ? chasm / total : 0.0f;
    music_share[CHASM_MUSIC_DESERT] = (total > 0.0f) ? desert / total : 0.0f;
}

void ApplicationChasm::UpdateMusic(){
#ifdef USE_SOUND
    if (!music_score || !music[CHASM_MUSIC_LAND]){
        return;
    }
    //The season - or the section the Music panel holds it at: sent once when it turns, or when a load or a new map lands
    //in another.
    std::string season = CalendarSeasonName(CalendarDateOf(calendar_tick).season);
    std::string section = music_section_held.empty() ? season : music_section_held;
    if (section != music_season){
        music_season = section;
        MusicEvent e;
        e.type = MusicEvent::SECTION;
        e.name = section;
        music[CHASM_MUSIC_LAND]->Post(e);
        MusicSend("section " + section + (music_section_held.empty() ? " - the season" : " - held"));
    }
    /*
        THE STORM (weather_plan.md, C): the suspense rises with the storm at the view and falls back to the score's own
        after it, so winter's snow wind and low wind swell, its cold strings come in, and the serene winter is the score
        at calm. The engine eases the beds itself. The Music panel's hold wins; let go, the storm takes over again.
    */
    if (music_mood_held[0] < 0.0f){
        const MusicParams own;
        float s = own.suspense + (MUSIC_STORM_SUSPENSE - own.suspense) * weather_storm.load();
        if (std::fabs(s - music_storm_suspense) > 0.005f){
            music_storm_suspense = s;
            for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
                if (music[i]){
                    MusicParams p = music[i]->GetParams();
                    p.suspense = s;
                    music[i]->SetParams(p);
                }
            }
        }
    }else{
        music_storm_suspense = -1.0f;
    }
    //The layers only while the world shows: under the title, the land alone.
    const bool f_shown = !TitleShowing() && world_scene && main_scene == world_scene;
    if (!f_shown || f_music_shares_held){
        for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
            music_share[i] = f_shown ? music_share_held[i] : 0.0f;
        }
    }else if (sound_pass % MUSIC_SHARE_PASSES == 0){
        MeasureMusicShares();
    }
    const float ease = std::min(1.0f,1.0f / (MUSIC_EASE_S * (float)CALENDAR_TICKS_PER_SECOND));
    float duck = 0.0f;
    for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
        music_layer[i] += (LayerLevel(music_share[i]) - music_layer[i]) * ease;
        duck = std::max(duck,LayerDuck(i) * music_layer[i]);
    }
    music_layer[CHASM_MUSIC_LAND] = 1.0f - duck;
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
    //How it is driven, and what holds it instead (the Music panel, `music` below).
    out["section_from"] = music_section_held.empty() ? "season" : "held";
    out["shares_held"] = f_music_shares_held;
    MusicParams p = music[CHASM_MUSIC_LAND] ? music[CHASM_MUSIC_LAND]->GetParams() : MusicParams();
    out["mood"] = json{{"suspense",std::round(p.suspense * 100.0f) / 100.0f},
                       {"brightness",std::round(p.brightness * 100.0f) / 100.0f},
                       {"held",music_mood_held[0] >= 0.0f || music_mood_held[1] >= 0.0f}};
    json sent = json::array();
    for (const MusicSent& m : music_sent){
        sent.push_back(json{{"pass",m.pass},{"tick",m.tick},{"what",m.what}});
    }
    out["sent"] = sent;
#endif
    return out;
}

/*
    chasm_sound's `music`, at a tick boundary - what the Music panel does: section (a section's name, "" the season's),
    clouds, chasm and desert (0..1 hold the layers' shares, below 0 lets them go), stinger (one of the score's), suspense
    and brightness (0..1, below 0 the score's own).
*/
void ApplicationChasm::SteerMusic(const json& m){
#ifdef USE_SOUND
    if (!m.is_object() || !music_score){
        return;
    }
    auto it = m.find("section");
    if (it != m.end() && it->is_string()){
        std::string name = it->get<std::string>();
        if (name.empty() || music_score->SectionIndex(name) >= 0){
            music_section_held = name;
        }
    }
    for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
        auto s = m.find(PlayerName(i));
        if (s != m.end() && s->is_number()){
            float v = s->get<float>();
            if (v < 0.0f){
                f_music_shares_held = false;
            }else{
                if (!f_music_shares_held){
                    std::copy(music_share,music_share + CHASM_MUSIC_PLAYERS,music_share_held);
                }
                f_music_shares_held = true;
                music_share_held[i] = std::min(1.0f,v);
            }
        }
    }
    it = m.find("stinger");
    if (it != m.end() && it->is_string()){
        PlayMusicStinger(it->get<std::string>(),"chasm_sound");
    }
    float mood[2] = {music_mood_held[0],music_mood_held[1]};
    bool f_mood = false;
    static const char* mood_keys[2] = {"suspense","brightness"};
    for (int k = 0; k < 2; k++){
        auto v = m.find(mood_keys[k]);
        if (v != m.end() && v->is_number()){
            mood[k] = v->get<float>();
            f_mood = true;
        }
    }
    if (f_mood){
        HoldMusicMood(mood[0],mood[1]);
    }
#endif
}
#endif

#ifdef USE_IMGUI
/*
    RENDER THREAD with physics_mutex held, under the Time and Sun panels: how the game drives the music - the land's
    section and what chose it, each layer's share of the view and the level it makes, the score's mood, and what the
    game last sent - and a hold on each, to hear the score as the game would play it elsewhere.
*/
void ApplicationChasm::RenderMusicPanel(){
    if (!ImGui::CollapsingHeader("Music")){
        return;
    }
#ifdef USE_SOUND
    if (!music_score || !music[CHASM_MUSIC_LAND]){
        ImGui::TextDisabled("no music: no sound device, or %s did not load",CHASM_MUSIC_SCORE);
        return;
    }
    static const char* pitch[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    ImGui::TextDisabled("%s, %i sections - the music volume %.0f%%",CHASM_MUSIC_SCORE,(int)music_score->sections.size(),
                        music_volume.load() * 100.0f);
    float over = 0.0f;
    for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
        over = std::max(over,music_layer[i]);
    }
    for (int i = 0; i < CHASM_MUSIC_PLAYERS; i++){
        if (!music[i]){
            continue;
        }
        MusicStatus st = music[i]->GetStatus();
        auto name_of = [&st](int s){
            return (s >= 0 && s < (int)st.section_names.size()) ? st.section_names[s].c_str() : "";
        };
        ImGui::Separator();
        const char* key = (st.root_pc >= 0 && st.root_pc < 12) ? pitch[st.root_pc] : "?";
        char next[48] = "";
        if (st.pending_section >= 0 || st.pending_section == -2){
            snprintf(next,sizeof(next)," (next %s)",st.pending_section >= 0 ? name_of(st.pending_section) : "the next");
        }
        ImGui::Text("%s: %s%s, bar %i - %s %s",PlayerName(i),name_of(st.section),next,st.section_bar,key,MusicModeName(st.mode));
        //The level each bus is at, and what put it there - the land turned down under the loudest layer.
        if (i == CHASM_MUSIC_LAND){
            ImGui::Text("  %s, level %.2f%s, %.0f dB",music_section_held.empty() ? "the season's" : "held",music_layer[i],
                        over > 0.001f ? " under the layers" : "",st.rms_db);
        }else if (f_music_held[i]){
            ImGui::Text("  share %.2f -> %.2f, level %.2f, rests",music_share[i],LayerLevel(music_share[i]),music_layer[i]);
        }else{
            ImGui::Text("  share %.2f -> %.2f, level %.2f, %.0f dB",music_share[i],LayerLevel(music_share[i]),music_layer[i],
                        st.rms_db);
        }
    }
    ImGui::Separator();
    //The holds: a section, the shares, the mood.
    const char* held = music_section_held.empty() ? "the season's" : music_section_held.c_str();
    if (ImGui::BeginCombo("section##music",held)){
        if (ImGui::Selectable("the season's",music_section_held.empty())){
            music_section_held.clear();
        }
        for (const MusicSectionDef& s : music_score->sections){
            if (ImGui::Selectable(s.name.c_str(),music_section_held == s.name)){
                music_section_held = s.name;
            }
        }
        ImGui::EndCombo();
    }
    bool f_shares = f_music_shares_held;
    if (ImGui::Checkbox("hold the layers' shares##music",&f_shares)){
        if (f_shares && !f_music_shares_held){
            std::copy(music_share,music_share + CHASM_MUSIC_PLAYERS,music_share_held);
        }
        f_music_shares_held = f_shares;
    }
    if (f_music_shares_held){
        for (int i = CHASM_MUSIC_CLOUDS; i < CHASM_MUSIC_PLAYERS; i++){
            char label[32];
            snprintf(label,sizeof(label),"%s##musicshare",PlayerName(i));
            ImGui::SliderFloat(label,&music_share_held[i],0.0f,1.0f,"%.2f");
        }
    }
    bool f_mood = music_mood_held[0] >= 0.0f || music_mood_held[1] >= 0.0f;
    MusicParams p = music[CHASM_MUSIC_LAND]->GetParams();
    if (ImGui::Checkbox("hold the mood##music",&f_mood)){
        if (f_mood){
            HoldMusicMood(p.suspense,p.brightness);
        }else{
            HoldMusicMood(-1.0f,-1.0f);
        }
    }
    if (f_mood){
        float m[2] = {music_mood_held[0],music_mood_held[1]};
        bool f_moved = ImGui::SliderFloat("suspense##music",&m[0],0.0f,1.0f,"%.2f");
        f_moved |= ImGui::SliderFloat("brightness##music",&m[1],0.0f,1.0f,"%.2f");
        if (f_moved){
            HoldMusicMood(m[0],m[1]);
        }
    }else{
        ImGui::Text("suspense %.2f, brightness %.2f",p.suspense,p.brightness);
        ImGui::TextDisabled("  nothing in the game moves these yet");
    }
    ImGui::TextDisabled("stingers:");
    for (const MusicStingerDef& s : music_score->stingers){
        ImGui::SameLine();
        char label[64];
        snprintf(label,sizeof(label),"%s##stinger",s.name.c_str());
        if (ImGui::Button(label)){
            PlayMusicStinger(s.name,"the panel");
        }
    }
    //What the game sent it, newest first.
    ImGui::TextDisabled("sent, newest first:");
    for (auto it = music_sent.rbegin(); it != music_sent.rend(); ++it){
        CalendarDate d = CalendarDateOf(it->tick);
        ImGui::TextWrapped("%s %i, pass %llu: %s",CalendarSeasonName(d.season),d.day,(unsigned long long)it->pass,it->what.c_str());
    }
#else
    ImGui::TextDisabled("no music in a USE_SOUND=0 build");
#endif
}
#endif
