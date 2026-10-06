#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "Window.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "Debug.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <cctype>

/*
    THE CALENDAR AND THE SPEED (gameplay_plan.md P1). The date is Calendar.h's, from one simulation
    number; this file advances it, sets it, shows it, and runs the clock faster or slower.

    The speeds are whole multiples of normal, so at every one a tick is still exactly a tick: 3x is
    150 ticks a second of the same 20 ms, never 50 ticks of 60 ms. A year at 3x is 10 minutes.
*/

namespace {

const float speeds[] = {1.0f,2.0f,3.0f};

}

int ApplicationChasm::SpeedCount(){
    return (int)(sizeof(speeds) / sizeof(speeds[0]));
}

float ApplicationChasm::SpeedFactor(int index){
    return speeds[std::max(0,std::min(SpeedCount() - 1,index))];
}

/*
    Any thread. physics_time_factor is read once per pass by the physics loop, outside its lock - one
    float, which is how core's own slider sets it too.
*/
void ApplicationChasm::SetSpeed(int index){
    index = std::max(0,std::min(SpeedCount() - 1,index));
    speed_index = index;
    physics_time_factor = speeds[index];
}

//PHYSICS THREAD, the last thing a tick does: the tick that ran is a tick of the colony's life.
void ApplicationChasm::TickCalendar(){
    calendar_tick++;
    calendar_shown = calendar_tick;
}

void ApplicationChasm::SetCalendar(uint64_t tick){
    calendar_tick = tick;
    calendar_shown = tick;
}

/*
    PHYSICS THREAD, from UpdateView, every pass - so space unpauses a paused game. Neither key is
    simulation input: pausing and the speed change when ticks run, not what they do, so neither is
    recorded and a replay runs the same at any speed. The edges are read first either way, so a
    press made while the window was in the background does not fire when it comes back (Scene.cpp,
    the pause key's note).
*/
void ApplicationChasm::UpdateTimeKeys(){
    InputController* input = main_scene->inputcontroller;
    bool f_pause = input->WasKeyPressed(INPUT_CHASM_PAUSE);
    bool f_slower = input->WasKeyPressed(INPUT_CHASM_SLOWER);
    bool f_faster = input->WasKeyPressed(INPUT_CHASM_FASTER);
    if (!input->IsInputLive()){
        return;
    }
    if (f_pause){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }
    if (f_slower){
        SetSpeed(speed_index - 1);
    }
    if (f_faster){
        //Faster from a pause also starts the clock again: that key means "go", whatever came before.
        if (main_scene->IsPhysicsPaused()){
            main_scene->PausePhysics(false);
        }else{
            SetSpeed(speed_index + 1);
        }
    }
}

float ApplicationChasm::SnowFrontNow(){
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        return -1e30f;
    }
    float cover = snow_cover_override.load();
    if (cover < 0.0f){
        cover = CalendarSnowCover(calendar_shown.load());
    }
    return CalendarSnowFrontZ(std::min(1.0f,cover),g->bounds_min.y,g->bounds_max.y);
}

/*
    The date command: the colony's clock set to a day and a tick in it. A debug tool's, mostly (to
    look at a winter without waiting 20 minutes for one) - and RECORDED like a zone command, since the
    date is simulation state and a replay must jump where the run did. The day goes in the subtype,
    which a float could not carry exactly past 2^24 ticks (four days of play).
*/
void ApplicationChasm::RegisterTimeCommands(){
    main_scene->RegisterCommandHandler(CHASM_CMD_CALENDAR,
        [this](const SimCommand& cmd) -> objectid_t {
            uint64_t in_day = (uint64_t)std::max(0.0f,std::min((float)(CALENDAR_DAY_TICKS - 1),cmd.value[0]));
            SetCalendar((uint64_t)cmd.subtype * CALENDAR_DAY_TICKS + in_day);
            calendar_commands_done++;
            return OBJECTID_INVALID;
        });
}

//--- The overlay ----------------------------------------------------------------------------------

namespace {

//The seasons' own colours, for the name and the year bar: a spring green, a summer gold, an autumn
//rust, a winter's pale blue.
const uint32_t season_colours[CALENDAR_SEASONS] = {
    UIColor(156,204,112),UIColor(236,202,92),UIColor(220,126,68),UIColor(196,220,242)
};

uint32_t Faded(uint32_t c, uint8_t alpha){
    return (c & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
}

}

#define HUD_PANEL       UIColor( 22, 28, 32,178)    //dark and see-through, so the map shows behind it
#define HUD_TEXT        UIColor(238,236,226,255)
#define HUD_TEXT_DIM    UIColor(238,236,226,150)
#define HUD_MARK        UIColor(255,255,255,255)

/*
    RENDER THREAD. A panel at the top of the screen, in every build (the game's own UI, not a debug
    panel): the season by name in its colour, the day, the year, and the speed - two bars when paused,
    one chevron per multiple of normal speed otherwise. Under it the year as a bar of its four seasons,
    the part gone bright, with a mark at today; and under the day a thin line filling as it passes.

    Sized off the window's height, so it is the same part of the picture at any size.
*/
void ApplicationChasm::DrawOverlay(void){
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    float s = std::max(0.5f,(float)main_window->height / 810.0f);
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    bool f_paused = main_scene && main_scene->IsPhysicsPaused();

    float w = 440.0f * s;
    float h = 66.0f * s;
    float pad = 16.0f * s;
    vec2 p0((float)main_window->width * 0.5f - w * 0.5f,12.0f * s);
    vec2 p1(p0.x + w,p0.y + h);
    overlay->AddRect(p0,p1,12.0f * s,HUD_PANEL);

    //The line of text: season, day, year, and the speed in a box of its own at the right.
    float text = 20.0f * s;
    float baseline = p0.y + 29.0f * s;
    float speed_w = 64.0f * s;
    float right = p1.x - pad - speed_w;
    char buf[32];
    std::string season = CalendarSeasonName(d.season);
    std::transform(season.begin(),season.end(),season.begin(),::toupper);
    overlay->AddText(season.c_str(),vec2(p0.x + pad,baseline),text,season_colours[d.season],UI_ALIGN_LEFT);
    snprintf(buf,sizeof(buf),"DAY %i",d.day);
    float day_x = p0.x + pad + (right - p0.x - pad) * 0.5f;
    overlay->AddText(buf,vec2(day_x,baseline),text,HUD_TEXT,UI_ALIGN_CENTER);
    snprintf(buf,sizeof(buf),"YEAR %i",d.year);
    overlay->AddText(buf,vec2(right - 8.0f * s,baseline),text,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    //The day passing, under its number.
    vec2 day_size = overlay->MeasureText("DAY 10",text);
    vec2 line0(day_x - day_size.x * 0.5f,baseline + 5.0f * s);
    vec2 line1(day_x + day_size.x * 0.5f,line0.y + 2.0f * s);
    overlay->AddRect(line0,line1,1.0f * s,Faded(HUD_TEXT,50));
    overlay->AddRect(line0,vec2(line0.x + (line1.x - line0.x) * d.day_fraction,line1.y),1.0f * s,Faded(HUD_TEXT,190));

    //The speed: a box at the right, with two bars or one chevron per multiple.
    vec2 b0(p1.x - pad - speed_w,p0.y + 9.0f * s);
    vec2 b1(p1.x - pad,b0.y + 30.0f * s);
    overlay->AddRect(b0,b1,7.0f * s,f_paused ? UIColor(196,92,72,210) : UIColor(255,255,255,28));
    vec2 bc((b0.x + b1.x) * 0.5f,(b0.y + b1.y) * 0.5f);
    if (f_paused){
        float bar_w = 5.0f * s;
        float bar_h = 16.0f * s;
        float gap = 4.0f * s;
        overlay->AddRect(vec2(bc.x - gap - bar_w,bc.y - bar_h * 0.5f),vec2(bc.x - gap,bc.y + bar_h * 0.5f),1.5f * s,HUD_TEXT);
        overlay->AddRect(vec2(bc.x + gap,bc.y - bar_h * 0.5f),vec2(bc.x + gap + bar_w,bc.y + bar_h * 0.5f),1.5f * s,HUD_TEXT);
    }else{
        std::string chevrons((size_t)std::max(1,(int)SpeedFactor(speed_index)),'>');
        overlay->AddText(chevrons.c_str(),vec2(bc.x,bc.y + 7.0f * s),text,HUD_TEXT,UI_ALIGN_CENTER);
    }

    //The year: four seasons end to end, what has gone bright, a mark at today.
    float y0 = p0.y + 46.0f * s;
    float y1 = y0 + 8.0f * s;
    float x0 = p0.x + pad;
    float x1 = p1.x - pad;
    float gap = 3.0f * s;
    float season_w = (x1 - x0 - gap * (CALENDAR_SEASONS - 1)) / CALENDAR_SEASONS;
    for (int k = 0; k < CALENDAR_SEASONS; k++){
        float a = x0 + k * (season_w + gap);
        overlay->AddRect(vec2(a,y0),vec2(a + season_w,y1),3.0f * s,Faded(season_colours[k],70));
        float done = (k < d.season) ? 1.0f : (k == d.season ? d.season_fraction : 0.0f);
        if (done > 0.0f){
            overlay->AddRect(vec2(a,y0),vec2(a + season_w * done,y1),3.0f * s,Faded(season_colours[k],235));
        }
    }
    float mark = x0 + d.season * (season_w + gap) + season_w * d.season_fraction;
    overlay->AddRect(vec2(mark - 1.5f * s,y0 - 3.0f * s),vec2(mark + 1.5f * s,y1 + 3.0f * s),1.5f * s,HUD_MARK);

    /*
        The colony's stores, under the date (docs/economy_plan.md): wood, food (the three crops summed)
        and water - what lies in stores, not what waits at a workplace or is being carried.
    */
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (e && z && e->world == z->world){
        std::array<int,GOOD_COUNT> t = EconomyStoredTotals(*e,*z);
        int food = t[GOOD_WHEAT] + t[GOOD_GREENS] + t[GOOD_BEANS];
        int people = (int)e->workers.size();
        int housed = 0;
        for (const EconomyWorker& k : e->workers){
            housed += k.house ? 1 : 0;
        }
        //Each item's text, measured, so the bar is as wide as what it says - centred under the date.
        const uint32_t colours[4] = {UIColor(238,236,226),UIColor(206,160,110),UIColor(236,202,92),UIColor(140,190,240)};
        char items[4][32];
        if (housed < people){
            snprintf(items[0],sizeof(items[0]),"PEOPLE %i/%i",housed,people);  //housed, of all of them
        }else{
            snprintf(items[0],sizeof(items[0]),"PEOPLE %i",people);
        }
        snprintf(items[1],sizeof(items[1]),"WOOD %i",t[GOOD_WOOD]);
        snprintf(items[2],sizeof(items[2]),"FOOD %i",food);
        snprintf(items[3],sizeof(items[3]),"WATER %i",t[GOOD_WATER]);
        const float size = 16.0f * s;
        const float gap = 22.0f * s;
        const float side = 14.0f * s;
        float widths[4];
        float total = side * 2.0f + gap * 3.0f;
        for (int i = 0; i < 4; i++){
            widths[i] = overlay->MeasureText(items[i],size).x;
            total += widths[i];
        }
        float cx = p0.x + w * 0.5f;
        vec2 q0(cx - total * 0.5f,p1.y + 6.0f * s);
        vec2 q1(cx + total * 0.5f,q0.y + 30.0f * s);
        overlay->AddRect(q0,q1,9.0f * s,HUD_PANEL);
        float x = q0.x + side;
        for (int i = 0; i < 4; i++){
            overlay->AddText(items[i],vec2(x,q0.y + 21.0f * s),size,colours[i],UI_ALIGN_LEFT);
            x += widths[i] + gap;
        }
    }
}

//--- The panel ------------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationChasm::RenderTimePanel(){
    if (!ImGui::CollapsingHeader("Time",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    uint64_t tick = calendar_shown.load();
    CalendarDate d = CalendarDateOf(tick);
    ImGui::Text("year %i, %s, day %i of %i (%.0f%%) - tick %llu",d.year,CalendarSeasonName(d.season),d.day,
                CALENDAR_DAYS_PER_SEASON,d.day_fraction * 100.0f,(unsigned long long)tick);
    ImGui::Text("snow cover %.2f, front at z %.0f",CalendarSnowCover(tick),SnowFrontNow());
    ImGui::TextDisabled("space pauses, - and = slow down and speed up");
    bool f_paused = main_scene->IsPhysicsPaused();
    if (ImGui::Button(f_paused ? "Run##time" : "Pause##time")){
        main_scene->PausePhysics(!f_paused);
    }
    for (int i = 0; i < SpeedCount(); i++){
        ImGui::SameLine();
        char label[16];
        snprintf(label,sizeof(label),"%gx##speed",speeds[i]);
        if (ImGui::RadioButton(label,speed_index == i)){
            SetSpeed(i);
        }
    }
    //A jump forward to the next start of each season: a recorded command, like any change to the date.
    ImGui::TextDisabled("to the next:");
    for (int k = 0; k < CALENDAR_SEASONS; k++){
        ImGui::SameLine();
        char label[24];
        snprintf(label,sizeof(label),"%s##jump",CalendarSeasonName(k));
        if (ImGui::Button(label)){
            const uint32_t year_days = CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON;
            uint32_t today = (uint32_t)(tick / CALENDAR_DAY_TICKS);
            uint32_t start = (uint32_t)(d.year - 1) * year_days + (uint32_t)k * CALENDAR_DAYS_PER_SEASON;
            if (start <= today){
                start += year_days;
            }
            SimCommand cmd;
            cmd.type = CHASM_CMD_CALENDAR;
            cmd.flags = SIM_CMD_FLAG_RECORD;
            cmd.subtype = start;
            cmd.value[0] = 0.0f;
            main_scene->SubmitCommand(cmd);
        }
    }
    float cover = snow_cover_override.load();
    bool f_override = cover >= 0.0f;
    if (ImGui::Checkbox("snow override",&f_override)){
        snow_cover_override = f_override ? 1.0f : -1.0f;
    }
    if (f_override){
        ImGui::SameLine();
        float c = snow_cover_override.load();
        if (ImGui::SliderFloat("##snowcover",&c,0.0f,1.0f)){
            snow_cover_override = c;
        }
    }
}
#endif

//--- The tool -------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterTimeTools(){
    MCPServer::Get()->RegisterTool("chasm_time",
        "The colony's calendar and the game speed (gameplay_plan.md P1). Returns the date (year from 1, "
        "season, day of the season from 1, how far through the day), the tick, the snow's cover (0 none "
        "past the mountain, 1 winter's furthest) and its front's mean z, the speed and whether paused. "
        "Optional: speed (1, 2 or 3 times normal - the viewer's, not recorded); paused (true/false); "
        "set_day (days since the colony began, from 0: 0 is spring of year 1, 30 winter of year 1, 40 "
        "spring of year 2) with day_fraction 0-1 - a RECORDED command, applied before this returns; "
        "snow_cover (0-1, negative for off) - a view-only override of the snow for looking at winter "
        "on any date, never read by the simulation. include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"speed",{{"type","number"}}},
                {"paused",{{"type","boolean"}}},
                {"set_day",{{"type","integer"}}},
                {"day_fraction",{{"type","number"}}},
                {"snow_cover",{{"type","number"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            if (args.contains("speed")){
                float want = args["speed"].get<float>();
                int best = 0;
                for (int i = 1; i < SpeedCount(); i++){
                    if (std::fabs(speeds[i] - want) < std::fabs(speeds[best] - want)){
                        best = i;
                    }
                }
                SetSpeed(best);
            }
            if (args.contains("paused")){
                bool f_paused = args["paused"].get<bool>();
                main_scene->AtTickBoundary([&](){
                    main_scene->PausePhysics(f_paused);
                });
            }
            if (args.contains("snow_cover")){
                snow_cover_override = std::min(1.0f,args["snow_cover"].get<float>());
            }
            bool f_ran = true;
            if (args.contains("set_day")){
                SimCommand cmd;
                cmd.type = CHASM_CMD_CALENDAR;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)std::max(0,args["set_day"].get<int>());
                cmd.value[0] = std::floor(std::max(0.0f,std::min(1.0f,args.value("day_fraction",0.0f))) * (CALENDAR_DAY_TICKS - 1));
                uint32_t before = calendar_commands_done.load();
                SubmitCommandAndWait(cmd);
                f_ran = calendar_commands_done.load() != before;
            }
            uint64_t tick = calendar_shown.load();
            CalendarDate d = CalendarDateOf(tick);
            json result{
                {"tick",tick},
                {"year",d.year},
                {"season",CalendarSeasonName(d.season)},
                {"day",d.day},
                {"day_fraction",d.day_fraction},
                {"days_since_start",tick / CALENDAR_DAY_TICKS},
                {"snow_cover",CalendarSnowCover(tick)},
                {"snow_cover_override",snow_cover_override.load()},
                {"snow_front_z",SnowFrontNow()},
                {"speed",SpeedFactor(speed_index)},
                {"paused",main_scene->IsPhysicsPaused()},
                {"ran",f_ran}
            };
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
