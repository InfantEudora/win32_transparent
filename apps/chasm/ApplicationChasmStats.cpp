#include "ApplicationChasm.h"
#include "ChasmStats.h"
#include "ChasmHud.h"
#include "UIOverlay.h"
#include "Window.h"
#include "Debug.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdio.h>
#include <thread>

/*
    THE STATISTICS PAGE (docs/statistics_plan.md): the colony NOW, OVER TIME and in its JOURNAL, over the frozen game.
    Tab opens it, or a click on the top bar's figures; Tab again, Escape or its close mark close it. While it is up the
    game is paused, as under the pop-up, and it has the input: nothing under it moves. Until the pause menu is built
    (demo_plan.md step 1) that is how it is reached; the menu's Statistics is to open the same page.

    Every figure is ChasmStats.h's, from the published state: the rules' history (Economy.h) and the colony as it
    stands. Drawn on the overlay, so it is in every build. Charts (the dataviz rules): one measure to an axis - wood
    and stone are each a chart of their own, too alike to share one - thin lines, recessive grids, a direct label
    where two lines share a chart, and the day's values under the mouse.
*/

static Debugger* debug = new Debugger("ChasmStats",DEBUG_ALL);

#define STATS_SHADE         UIColor(0,0,0,120)      //over the world while the page is up
#define STATS_PANEL         UIColor(22,28,32,242)
#define STATS_TILE          UIColor(255,255,255,12) //a total's tile, and a chart's ground
#define STATS_GRID          UIColor(255,255,255,24) //gridlines: there to be read past
#define STATS_AXIS          UIColor(255,255,255,70) //a chart's baseline, and the hover's line
#define STATS_SEASONS_SHOWN 8                       //the seasons a bar chart shows: two years
#define STATS_SCROLL_LINES  3                       //journal lines a turn of the wheel scrolls
#define STATS_END_FOOT      62.0f                   //the end's foot under the columns: its goals and buttons (x s)
#define STATS_END_BUTTON_W  190.0f
#define STATS_END_BUTTON_H  40.0f

namespace {

enum{
    END_ITEM_KEEP = 1,
    END_ITEM_TITLE,
    END_ITEM_LOAD,
    END_ITEM_NEW,
};

//The page's pieces, from the window alone - so the physics thread finds its close mark where the render thread drew it.
struct StatsLayout{
    vec2 p0, p1;                //the panel
    vec2 close0, close1;        //its close mark
    float top = 0.0f;           //the columns' top, under the header
    float bottom = 0.0f;
    float a0, a1, b0, b1, c0, c1;   //NOW, OVER TIME, the JOURNAL
};

//`f_end`: the page as an END's (demo_plan.md step 7), its columns shorter by the foot that holds its buttons.
StatsLayout StatsLayoutOf(float w, float h, float s, bool f_end = false){
    StatsLayout l;
    float margin = 28.0f * s;
    l.p0 = vec2(margin,margin);
    l.p1 = vec2(w - margin,h - margin);
    float pad = 24.0f * s;
    l.close1 = vec2(l.p1.x - 14.0f * s,l.p0.y + 14.0f * s + 28.0f * s);
    l.close0 = vec2(l.close1.x - 28.0f * s,l.close1.y - 28.0f * s);
    l.top = l.p0.y + 74.0f * s;
    l.bottom = l.p1.y - pad - (f_end ? STATS_END_FOOT * s : 0.0f);
    float inner = (l.p1.x - l.p0.x) - pad * 2.0f;
    float gap = 28.0f * s;
    float a = (inner - gap * 2.0f) * 0.25f;
    float b = (inner - gap * 2.0f) * 0.47f;
    l.a0 = l.p0.x + pad;
    l.a1 = l.a0 + a;
    l.b0 = l.a1 + gap;
    l.b1 = l.b0 + b;
    l.c0 = l.b1 + gap;
    l.c1 = l.p1.x - pad;
    return l;
}

//A round top for an axis, at or over `v`: 1, 2 or 5 of a power of ten - and never under 1.
float NiceCeil(float v){
    if (v <= 1.0f){
        return 1.0f;
    }
    float p = powf(10.0f,floorf(log10f(v)));
    for (float m : {1.0f,2.0f,5.0f,10.0f}){
        if (m * p >= v - 1e-4f){
            return m * p;
        }
    }
    return 10.0f * p;
}

std::string Number(float v, int decimals){
    char buf[32];
    snprintf(buf,sizeof(buf),"%.*f",decimals,v);
    return buf;
}

//The seasons' own colours, as the calendar draws them (ApplicationChasmTime.cpp) - a strip under a season's bar so the
//year reads, never the bar itself: spring's green and summer's gold are too alike to tell a bar by.
const uint32_t season_strip[CALENDAR_SEASONS] = {
    UIColor(156,204,112,150),UIColor(236,202,92,150),UIColor(220,126,68,150),UIColor(196,220,242,150)
};

//One line of a chart: its name, its colour, a value a point, and how many decimals its numbers show.
struct ChartLine{
    std::string name;
    uint32_t colour;
    std::vector<float> values;
    int decimals = 0;
};

//Up to the first `n` characters that fit `width`, with "..." when cut.
std::string Fit(const UIOverlay* o, const std::string& text, float size, float width){
    if (o->MeasureText(text.c_str(),size).x <= width){
        return text;
    }
    std::string t = text;
    while (!t.empty() && o->MeasureText((t + "...").c_str(),size).x > width){
        t.pop_back();
    }
    return t + "...";
}

/*
    A LINE CHART of days: `days` the x of every point, each line a value a point, on one axis from 0. Its title and,
    with one line, today's value in it; with two, each line named at its end. Under the mouse, the nearest day: a line
    down the chart, a mark on each line and the day's values.
*/
void DrawLineChart(UIOverlay* o, vec2 a, vec2 b, const std::string& title, const std::vector<int>& days,
                   const std::vector<ChartLine>& lines, vec2 mouse, float s){
    o->AddRect(a,b,6.0f * s,STATS_TILE);
    const float title_size = 12.0f * s;
    const float label_size = 10.5f * s;
    std::string head = title;
    if (lines.size() == 1 && !lines[0].values.empty()){
        head += "   " + Number(lines[0].values.back(),lines[0].decimals);
    }
    o->AddText(head.c_str(),vec2(a.x + 10.0f * s,a.y + 17.0f * s),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    float end_room = (lines.size() > 1) ? 58.0f * s : 0.0f;
    vec2 q0(a.x + 34.0f * s,a.y + 28.0f * s);
    vec2 q1(b.x - 10.0f * s - end_room,b.y - 18.0f * s);
    if (days.empty() || q1.x <= q0.x || q1.y <= q0.y){
        o->AddText("no days yet",vec2((a.x + b.x) * 0.5f,(q0.y + q1.y) * 0.5f),label_size,HUD_TEXT_DIM,UI_ALIGN_CENTER);
        return;
    }
    float most = 0.0f;
    bool f_whole = true;        //counts only: the axis goes in whole numbers
    for (const ChartLine& l : lines){
        for (float v : l.values){
            most = std::max(most,v);
        }
        f_whole = f_whole && l.decimals == 0;
    }
    float top = std::max(NiceCeil(most),f_whole ? 2.0f : 1.0f);
    int d0 = days.front();
    int d1 = std::max(days.back(),d0 + 1);
    auto x_of = [&](int d){ return q0.x + (q1.x - q0.x) * (float)(d - d0) / (float)(d1 - d0); };
    auto y_of = [&](float v){ return q1.y - (q1.y - q0.y) * std::min(1.0f,std::max(0.0f,v / top)); };
    //Gridlines at a half and the top, the baseline firmer; their values at the left.
    for (int k = 0; k <= 2; k++){
        float v = top * (float)k * 0.5f;
        float y = y_of(v);
        o->AddLine(vec2(q0.x,y),vec2(q1.x,y),1.0f * s,(k == 0) ? STATS_AXIS : STATS_GRID);
        int decimals = (top < 2.0f && k == 1) ? 1 : 0;
        o->AddText(Number(v,decimals).c_str(),vec2(q0.x - 6.0f * s,y + 4.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }
    //Its first day and its last under the ends; between them the years' first days, marked and named - every one, or
    //every few once there are many, and none crowding an end's date.
    std::string first_date = ChasmStatsDate((uint64_t)d0 * CALENDAR_DAY_TICKS);
    std::string last_date = ChasmStatsDate((uint64_t)days.back() * CALENDAR_DAY_TICKS);
    float first_w = o->MeasureText(first_date.c_str(),label_size).x;
    float last_w = o->MeasureText(last_date.c_str(),label_size).x;
    o->AddText(first_date.c_str(),vec2(q0.x,q1.y + 13.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    if (days.back() > d0){
        o->AddText(last_date.c_str(),vec2(x_of(days.back()),q1.y + 13.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }
    const int year_days = CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON;
    int years = (d1 - d0) / year_days + 1;
    int every = std::max(1,years / 6);
    for (int y = d0 / year_days + 1; y * year_days <= d1; y++){
        float x = x_of(y * year_days);
        o->AddLine(vec2(x,q0.y),vec2(x,q1.y),1.0f * s,STATS_GRID);
        if (y % every == 0 && x > q0.x + first_w + 20.0f * s && x < x_of(days.back()) - last_w - 20.0f * s){
            char name[16];
            snprintf(name,sizeof(name),"Y%i",y + 1);
            o->AddText(name,vec2(x,q1.y + 13.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_CENTER);
        }
    }
    //The lines: a point a pixel at most, the last always.
    int stride = std::max(1,(int)(days.size() / std::max(1.0f,q1.x - q0.x)));
    for (const ChartLine& l : lines){
        if (l.values.size() != days.size()){
            continue;
        }
        if (days.size() == 1){
            vec2 p(x_of(days[0]),y_of(l.values[0]));
            o->AddRect(p - vec2(3.0f * s,3.0f * s),p + vec2(3.0f * s,3.0f * s),3.0f * s,l.colour);
            continue;
        }
        size_t prev = 0;
        for (size_t i = stride; ; i += stride){
            size_t at = std::min(i,days.size() - 1);
            o->AddLine(vec2(x_of(days[prev]),y_of(l.values[prev])),vec2(x_of(days[at]),y_of(l.values[at])),2.0f * s,l.colour);
            prev = at;
            if (at == days.size() - 1){
                break;
            }
        }
    }
    //Two lines on one chart: each named at its end, in the text's colour beside a mark in its own.
    if (lines.size() > 1){
        std::vector<std::pair<float,size_t>> ends;
        for (size_t i = 0; i < lines.size(); i++){
            ends.push_back(std::make_pair(lines[i].values.empty() ? q1.y : y_of(lines[i].values.back()),i));
        }
        std::sort(ends.begin(),ends.end());
        float last_y = -1e9f;
        for (const auto& e : ends){
            float y = std::max(e.first,last_y + 13.0f * s);    //apart, if the lines end together
            last_y = y;
            const ChartLine& l = lines[e.second];
            o->AddRect(vec2(q1.x + 6.0f * s,y - 4.0f * s),vec2(q1.x + 14.0f * s,y + 4.0f * s),2.0f * s,l.colour);
            o->AddText(l.name.c_str(),vec2(q1.x + 18.0f * s,y + 4.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
    }
    //The day under the mouse.
    if (mouse.x >= q0.x && mouse.x <= q1.x && mouse.y >= a.y && mouse.y <= b.y){
        size_t best = 0;
        for (size_t i = 1; i < days.size(); i++){
            if (std::fabs(x_of(days[i]) - mouse.x) < std::fabs(x_of(days[best]) - mouse.x)){
                best = i;
            }
        }
        float x = x_of(days[best]);
        o->AddLine(vec2(x,q0.y),vec2(x,q1.y),1.0f * s,STATS_AXIS);
        std::vector<std::string> text;
        text.push_back(ChasmStatsDate((uint64_t)days[best] * CALENDAR_DAY_TICKS));
        for (const ChartLine& l : lines){
            if (best < l.values.size()){
                vec2 p(x,y_of(l.values[best]));
                o->AddRect(p - vec2(4.0f * s,4.0f * s),p + vec2(4.0f * s,4.0f * s),4.0f * s,UIColor(22,28,32,255));
                o->AddRect(p - vec2(3.0f * s,3.0f * s),p + vec2(3.0f * s,3.0f * s),3.0f * s,l.colour);
                text.push_back(l.name + "  " + Number(l.values[best],l.decimals));
            }
        }
        float w = 0.0f;
        for (const std::string& t : text){
            w = std::max(w,o->MeasureText(t.c_str(),label_size).x);
        }
        const float line_h = 14.0f * s;
        float tx = (x + 10.0f * s + w + 16.0f * s <= b.x) ? x + 10.0f * s : x - 10.0f * s - w - 16.0f * s;
        vec2 t0(tx,q0.y);
        vec2 t1(tx + w + 16.0f * s,q0.y + 8.0f * s + line_h * text.size());
        o->AddRect(t0,t1,5.0f * s,UIColor(12,16,18,235));
        for (size_t i = 0; i < text.size(); i++){
            o->AddText(text[i].c_str(),vec2(t0.x + 8.0f * s,t0.y + line_h * (i + 1)),label_size,(i == 0) ? HUD_TEXT_DIM : HUD_TEXT,
                       UI_ALIGN_LEFT);
        }
    }
}

/*
    A BAR CHART by season: the last STATS_SEASONS_SHOWN, left to right in the order lived, each over a strip of its
    season's colour so the year reads; the tallest bar's value on it, any other's under the mouse. Its title, and the
    total since the history began at its right.
*/
void DrawSeasonBars(UIOverlay* o, vec2 a, vec2 b, const std::string& title, int total,
                    const std::vector<ChasmStatsSeason>& seasons, int ChasmStatsSeason::*field, uint32_t colour, vec2 mouse,
                    float s){
    o->AddRect(a,b,6.0f * s,STATS_TILE);
    const float title_size = 12.0f * s;
    const float label_size = 10.5f * s;
    o->AddText(title.c_str(),vec2(a.x + 10.0f * s,a.y + 17.0f * s),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    o->AddText(std::to_string(total).c_str(),vec2(b.x - 10.0f * s,a.y + 17.0f * s),title_size,HUD_TEXT,UI_ALIGN_RIGHT);
    vec2 q0(a.x + 10.0f * s,a.y + 40.0f * s);
    vec2 q1(b.x - 10.0f * s,b.y - 18.0f * s);
    size_t first = (seasons.size() > STATS_SEASONS_SHOWN) ? seasons.size() - STATS_SEASONS_SHOWN : 0;
    int most = 0;
    size_t tallest = first;
    for (size_t i = first; i < seasons.size(); i++){
        if (seasons[i].*field > most){
            most = seasons[i].*field;
            tallest = i;
        }
    }
    float top = NiceCeil((float)most);
    float slot = (q1.x - q0.x) / STATS_SEASONS_SHOWN;
    float gap = 2.0f * s;
    for (size_t i = first; i < seasons.size(); i++){
        int k = (int)(i - first);
        float x0 = q0.x + slot * k;
        float x1 = x0 + slot;
        o->AddRect(vec2(x0 + gap,q1.y + 2.0f * s),vec2(x1 - gap,q1.y + 4.0f * s),1.0f * s,
                   season_strip[((seasons[i].season % CALENDAR_SEASONS) + CALENDAR_SEASONS) % CALENDAR_SEASONS]);
        int v = seasons[i].*field;
        float h = (q1.y - q0.y) * (float)v / top;
        if (v > 0){
            o->AddRect(vec2(x0 + gap + slot * 0.12f,q1.y - h),vec2(x1 - gap - slot * 0.12f,q1.y),2.0f * s,colour);
        }
        bool f_hover = mouse.x >= x0 && mouse.x < x1 && mouse.y >= a.y && mouse.y <= b.y;
        if ((i == tallest && v > 0) || f_hover){
            o->AddText(std::to_string(v).c_str(),vec2((x0 + x1) * 0.5f,q1.y - h - 4.0f * s),label_size,HUD_TEXT,UI_ALIGN_CENTER);
        }
        if (f_hover){
            o->AddText(ChasmStatsSeasonName(seasons[i].season).c_str(),vec2((x0 + x1) * 0.5f,q1.y + 13.0f * s),label_size,
                       HUD_TEXT,UI_ALIGN_CENTER);
        }
    }
    o->AddLine(vec2(q0.x,q1.y),vec2(q1.x,q1.y),1.0f * s,STATS_AXIS);
    //The first season shown and the last, under their bars - the last only clear of the first, and neither while the
    //mouse names one.
    if (!seasons.empty() && !(mouse.x >= q0.x && mouse.x <= q1.x && mouse.y >= a.y && mouse.y <= b.y)){
        std::string first_name = ChasmStatsSeasonName(seasons[first].season);
        std::string last_name = ChasmStatsSeasonName(seasons.back().season);
        float first_end = q0.x + o->MeasureText(first_name.c_str(),label_size).x;
        float last_end = std::max(q0.x + slot * (float)(seasons.size() - first),first_end);
        o->AddText(first_name.c_str(),vec2(q0.x,q1.y + 13.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        if (seasons.size() - first > 1 && last_end - o->MeasureText(last_name.c_str(),label_size).x > first_end + 8.0f * s){
            o->AddText(last_name.c_str(),vec2(last_end,q1.y + 13.0f * s),label_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
        }
    }
}

//A journal line's mark, by kind - its words say what it is, so the colour only helps the eye along.
uint32_t JournalColour(int kind){
    switch (kind){
        case EVENT_BORN:                return UIColor(156,204,112);
        case EVENT_DIED:                return HUD_DANGER;
        case EVENT_HARVEST:             return HudGoodColour(GOOD_WHEAT);
        case EVENT_BUILDING_FINISHED:   return HudGoodColour(GOOD_WOOD);
        case EVENT_SETTLED:             return HUD_SELECT;
        case EVENT_SEAM_SEEN:           return HudGoodColour(GOOD_FLOATSTONE);
        case EVENT_DISCOVERED:          return UIColor(120,190,230);
        case EVENT_GREW_UP:
        case EVENT_GREW_OLD:
        case EVENT_EXPECTING:           return UIColor(156,204,112);
        case EVENT_FROST_TOOK:          return UIColor(200,224,240);
        case EVENT_BOULDER_FOUND:       return HudGoodColour(GOOD_STONE);
        case EVENT_ZEPPELIN_STAGE:      return HudGoodColour(GOOD_CANVAS);
        case EVENT_BALCONY_REACHED:     return UIColor(120,190,230);
        case EVENT_GOAL_MET:
        case EVENT_DEMO_COMPLETE:       return HUD_SELECT;
        case EVENT_GOAL_FAILED:
        case EVENT_COLONY_LOST:
        case EVENT_WALL_BROKEN:
        case EVENT_GATE_BROKEN:
        case EVENT_BREACHED:            return HUD_DANGER;
        default:                        return HUD_TEXT_DIM;
    }
}

}

//--- Opening and closing --------------------------------------------------------------------------------------------

//PHYSICS THREAD. The game paused under it, as it was before afterwards - as the pop-up does (OpenAsk).
void ApplicationChasm::OpenStats(){
    if (f_stats_open.load() || TitleShowing()){
        return;
    }
    f_stats_paused_game = false;
    if (world_scene && !world_scene->IsPhysicsPaused()){
        world_scene->PausePhysics(true);
        f_stats_paused_game = true;
    }
    stats_scroll = 0;
    stats_colony = 0;       //all of them, each time it opens (BY COLONY)
    f_stats_open = true;
    debug->Info("Statistics page\n");
}

void ApplicationChasm::CloseStats(){
    if (!f_stats_open.load()){
        return;
    }
    if (f_stats_paused_game && world_scene){
        world_scene->PausePhysics(false);
    }
    f_stats_paused_game = false;
    f_stats_open = false;
}

/*
    PHYSICS THREAD, from UpdateView: chasm_stats's open or close, then Tab to open - and while the page is up its own
    input, true then, so nothing under it moves: Tab or Escape to close, the wheel to scroll the journal, a click on
    the close mark. The mouse's movement spent here, as a menu spends it (ReadMenuInput).
*/
bool ApplicationChasm::UpdateStats(InputController* input){
    int request = stats_request.exchange(0);
    if (request == 1){
        OpenStats();
    }else if (request == 2 && stats_end.load() == CHASM_END_NONE){
        CloseStats();
    }
    bool f_tab = input->WasKeyPressed(INPUT_CHASM_STATS);
    bool f_escape = input->WasKeyPressed(INPUT_CHASM_MENU);
    bool f_click = input->WasKeyPressed(INPUT_CLICK_LEFT);
    bool f_live = KeysLive();
    if (!f_stats_open.load()){
        if (f_tab && f_live){
            OpenStats();
            return true;
        }
        return false;
    }
    input->GetDelta(INPUT_MOUSE_DELTA_X);
    input->GetDelta(INPUT_MOUSE_DELTA_Y);
    int wheel = input->GetDelta(INPUT_MOUSE_WHEEL);
    if (wheel != 0 && main_window->f_has_focus){
        std::shared_ptr<const EconomyState> e = GetEconomy();
        int lines = e ? (int)e->history.journal.size() : 0;
        stats_scroll = std::max(0,std::min(lines - 1,stats_scroll.load() - wheel * STATS_SCROLL_LINES));
    }
    int2 px = input->GetRelativeMousePosition();
    bool f_in = main_window->f_has_focus && px.x >= 0 && px.y >= 0 && px.x < main_window->width && px.y < main_window->height;
    if (!f_overlay_mouse_pinned){
        overlay_mouse_x = f_in ? px.x : -1;
        overlay_mouse_y = f_in ? px.y : -1;
    }
    //An END's page (demo_plan.md step 7) is left only by its buttons; its journal scrolls as the page's does.
    if (stats_end.load() != CHASM_END_NONE){
        return UpdateEndPage(input);
    }
    float s = HudUiScale((float)main_window->height,settings.GetFloat("ui_size"));
    StatsLayout l = StatsLayoutOf((float)main_window->width,(float)main_window->height,s);
    bool f_on_close = f_in && px.x >= l.close0.x && px.x <= l.close1.x && px.y >= l.close0.y && px.y <= l.close1.y;
    if ((f_live && (f_tab || f_escape)) || (f_click && f_on_close && !UIWantsMouse())){
        CloseStats();
    }
    //BY COLONY (settlements_plan.md step 4): a chip by the title - all of them, or one colony - where DrawStats drew it;
    //pressed by the mouse, or by chasm_select's op press, which the play mode's picking under the page never takes.
    OverlayHit hit;
    if (f_script_press.exchange(false)){
        std::lock_guard<std::mutex> lock(pick_mutex);
        px = script_press_px;
        f_click = true;
        f_in = true;
    }
    if (f_click && f_in && !UIWantsMouse() && OverlayHitAt(px,&hit) && hit.action == OVERLAY_HIT_STATS_COLONY){
        stats_colony = (uint32_t)std::max(0,hit.value);
        stats_scroll = 0;
    }
    return true;
}

//--- The end's page (demo_plan.md step 7) -------------------------------------------------------------------------------

/*
    PHYSICS THREAD, from UpdateEnd: the page as THE END - the demo complete, with Keep playing or Main menu, or the colony
    lost, with Load, New game or Main menu: with nothing left to lose, those two neither ask nor autosave (Choose). The
    game paused under it as under the page, and the first button focused.
*/
void ApplicationChasm::OpenEnd(int end){
    end_menu.Clear();
    if (end == CHASM_END_COMPLETE){
        end_menu.AddButton("Keep playing",END_ITEM_KEEP);
    }else{
        end_menu.AddButton("Load",END_ITEM_LOAD);
        end_menu.AddButton("New game",END_ITEM_NEW);
    }
    end_menu.AddButton("Main menu",END_ITEM_TITLE);
    end_menu.SetFocus(0);
    CloseStats();
    stats_end = end;
    OpenStats();
    debug->Info("The end: %s\n",(end == CHASM_END_COMPLETE) ? "the demo complete" : "the colony lost");
}

void ApplicationChasm::CloseEnd(){
    stats_end = CHASM_END_NONE;
    CloseStats();
}

//A button of the end's in its row along the foot, right-aligned under the journal - from the window alone, so the
//physics thread finds it where the render thread drew it (chasm_menu's clicks too).
UIMenuRect ApplicationChasm::EndButtonAt(int item){
    float w = (float)main_window->width;
    float h = (float)main_window->height;
    float s = HudUiScale(h,settings.GetFloat("ui_size"));
    StatsLayout l = StatsLayoutOf(w,h,s,true);
    int count = end_menu.Count();
    const float bw = STATS_END_BUTTON_W * s;
    const float bh = STATS_END_BUTTON_H * s;
    const float gap = 14.0f * s;
    UIMenuRect r;
    r.x1 = l.c1 - (float)(count - 1 - item) * (bw + gap);
    r.x0 = r.x1 - bw;
    r.y0 = l.bottom + (STATS_END_FOOT * s - bh) * 0.5f + 6.0f * s;
    r.y1 = r.y0 + bh;
    return r;
}

/*
    PHYSICS THREAD, from UpdateStats while the page is an end's: its buttons have the input. A row, not a column: left and
    right move along it as up and down do, the pointer focuses the button under it and a release there presses it.
    Escape keeps playing when the demo is complete; a lost colony has nothing to go back to.
*/
bool ApplicationChasm::UpdateEndPage(InputController* input){
    UIMenuInput in;
    bool f_back = false;
    if (!ReadMenuInput(input,in,f_back)){
        return true;
    }
    in.up = in.up || in.left;
    in.down = in.down || in.right;
    in.left = false;
    in.right = false;
    int clicked = -1;
    if (in.f_pointer){
        for (int i = 0; i < end_menu.Count(); i++){
            if (EndButtonAt(i).Contains(in.pointer_x,in.pointer_y)){
                end_menu.SetFocus(i);
                clicked = in.pointer_released ? i : -1;
            }
        }
    }
    in.f_pointer = false;
    UIMenuResult r = end_menu.Update(in);
    int end = stats_end.load();
    int id = (clicked >= 0) ? end_menu.Item(clicked).id : (r.event == UI_MENU_ACTIVATED) ? r.id : 0;
    if (id == 0 && f_back && end == CHASM_END_COMPLETE){
        id = END_ITEM_KEEP;
    }
    switch (id){
        case END_ITEM_KEEP:
            CloseEnd();
            break;
        case END_ITEM_TITLE:
            CloseEnd();
            OpenTitle();
            break;
        case END_ITEM_LOAD:
        case END_ITEM_NEW:
            CloseEnd();
            OpenTitle();
            OpenScreen(id == END_ITEM_LOAD ? CHASM_SCREEN_LOAD : CHASM_SCREEN_NEW);
            break;
    }
    return true;
}

/*
    RENDER THREAD, from DrawStats on an end's page: its foot - the goals at the left, each with its mark and how it
    ended, and the buttons at the right, the focused one outlined.
*/
void ApplicationChasm::DrawEndFoot(float s){
    float w = (float)main_window->width;
    float h = (float)main_window->height;
    StatsLayout l = StatsLayoutOf(w,h,s,true);
    overlay->AddLine(vec2(l.a0,l.bottom + 6.0f * s),vec2(l.c1,l.bottom + 6.0f * s),1.0f * s,STATS_GRID);
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ChasmDiscoveries> d = GetDiscoveries();
    float line_y = l.bottom + 30.0f * s;
    if (e && d){
        for (int g = 0; g < CHASM_GOALS; g++){
            int state = ChasmGoalState(g,*e,*d);
            uint32_t colour = (state == CHASM_GOAL_MET) ? UIColor(140,200,110) : (state == CHASM_GOAL_FAILED) ? HUD_DANGER : HUD_TEXT_DIM;
            std::string text = ChasmGoalTitle(g);
            std::transform(text.begin(),text.end(),text.begin(),::toupper);
            text += (state == CHASM_GOAL_MET) ? " - MET" : (state == CHASM_GOAL_FAILED) ? " - FAILED" : " - NOT YET";
            float y = line_y + (float)g * 20.0f * s;
            const float mark = 8.0f * s;
            overlay->AddRect(vec2(l.a0,y - 9.0f * s),vec2(l.a0 + mark,y - 9.0f * s + mark),mark * 0.5f,colour);
            overlay->AddText(text.c_str(),vec2(l.a0 + mark + 9.0f * s,y),13.0f * s,HUD_TEXT,UI_ALIGN_LEFT);
        }
    }
    UIMenuStyle style = MenuStyle();
    int focus = end_menu.Focus();
    vec2 mouse((float)overlay_mouse_x.load(),(float)overlay_mouse_y.load());
    for (int i = 0; i < end_menu.Count(); i++){
        UIMenuRect r = EndButtonAt(i);
        bool f_focus = i == focus || r.Contains(mouse.x,mouse.y);
        vec2 b0(r.x0,r.y0);
        vec2 b1(r.x1,r.y1);
        if (f_focus){
            overlay->AddRect(b0 - vec2(2.0f * s,2.0f * s),b1 + vec2(2.0f * s,2.0f * s),9.0f * s,style.outline_focus);
        }
        overlay->AddRect(b0,b1,8.0f * s,f_focus ? style.panel_focus : UIColor(54,64,70,255));
        UIMenuItem it = end_menu.Item(i);
        overlay->AddText(it.label.c_str(),vec2((r.x0 + r.x1) * 0.5f,(r.y0 + r.y1) * 0.5f + 6.0f * s),17.0f * s,
                         f_focus ? style.text : style.text_dim,UI_ALIGN_CENTER);
    }
}

//--- The page ---------------------------------------------------------------------------------------------------------

//RENDER THREAD, from DrawOverlay in the HUD's place, under a question.
void ApplicationChasm::DrawStats(float s){
    if (!f_stats_open.load()){
        return;
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    overlay->AddRect(vec2(0.0f,0.0f),vec2(w,h),0.0f,STATS_SHADE);
    //An END's page (demo_plan.md step 7): its own heading, and its buttons in a foot - no close mark, its buttons are
    //the way out.
    int end = stats_end.load();
    StatsLayout l = StatsLayoutOf(w,h,s,end != CHASM_END_NONE);
    overlay->AddRect(l.p0,l.p1,14.0f * s,STATS_PANEL);
    vec2 mouse((float)overlay_mouse_x.load(),(float)overlay_mouse_y.load());
    /*
        BY COLONY (settlements_plan.md, step 4): with more than one colony in the history, the page shows all of them or
        one, chosen by the chips beside the title - its own history (ChasmColonyHistory) read as the whole's is. An end's
        page is the whole colony's.
    */
    std::vector<uint32_t> colonies = (e && end == CHASM_END_NONE) ? ChasmStatsColonies(*e) : std::vector<uint32_t>();
    uint32_t colony = stats_colony.load();
    colony = (colonies.size() > 1 && std::find(colonies.begin(),colonies.end(),colony) != colonies.end()) ? colony : 0;
    std::string colony_title = "COLONY " + std::to_string(colony);
    const char* title = (end == CHASM_END_COMPLETE) ? "DEMO COMPLETE" : (end == CHASM_END_LOST) ? "THE COLONY IS LOST"
                      : colony ? colony_title.c_str() : "THE COLONY";
    if (end == CHASM_END_NONE){
        //The close mark: an X in a square, lit under the mouse.
        bool f_over_close = mouse.x >= l.close0.x && mouse.x <= l.close1.x && mouse.y >= l.close0.y && mouse.y <= l.close1.y;
        overlay->AddRect(l.close0,l.close1,6.0f * s,f_over_close ? UIColor(255,255,255,40) : UIColor(255,255,255,14));
        vec2 cc = (l.close0 + l.close1) * 0.5f;
        float arm = 7.0f * s;
        overlay->AddLine(cc + vec2(-arm,-arm),cc + vec2(arm,arm),2.0f * s,HUD_TEXT);
        overlay->AddLine(cc + vec2(-arm,arm),cc + vec2(arm,-arm),2.0f * s,HUD_TEXT);
        overlay->AddText("TAB OR ESC TO CLOSE",vec2(l.close0.x - 12.0f * s,cc.y + 5.0f * s),12.0f * s,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }else{
        DrawEndFoot(s);
    }
    overlay->AddText(title,vec2(l.a0,l.p0.y + 42.0f * s),26.0f * s,(end == CHASM_END_LOST) ? HUD_DANGER : HUD_TEXT,UI_ALIGN_LEFT);
    if (!e || !z || e->world != z->world){
        overlay->AddText("no colony",vec2((l.p0.x + l.p1.x) * 0.5f,(l.top + l.bottom) * 0.5f),16.0f * s,HUD_TEXT_DIM,UI_ALIGN_CENTER);
        return;
    }
    EconomyHistory colony_hist;
    if (colony){
        colony_hist = ChasmColonyHistory(e->history,colony);
    }
    const EconomyHistory& hist = colony ? colony_hist : e->history;
    /*
        A colony not there when the history began - its start has no row of it - goes back to its first row: FOUNDED then,
        or, in a history kept from before the colonies were counted (a start with no colonies' rows), KEPT SINCE then.
    */
    const std::vector<EconomyDay>& at_start = e->history.start.colonies;
    bool f_later = colony && std::none_of(at_start.begin(),at_start.end(),[&](const EconomyDay& r){ return r.colony == colony; });
    bool f_founded = f_later && !at_start.empty();
    //Its date, and how long the history goes back: the landing's, or the load of a save from before it was kept.
    float chips_x = l.a0;
    {
        std::string since = ChasmStatsDate(e->tick);
        char buf[96];
        int lived = (int)((e->tick - std::min(e->tick,hist.from)) / CALENDAR_DAY_TICKS);
        if (f_founded){
            snprintf(buf,sizeof(buf),"   -   FOUNDED %s",ChasmStatsDate(hist.from).c_str());
        }else if (hist.from == 0 && !f_later){
            snprintf(buf,sizeof(buf),"   -   %i DAY%s SINCE THE LANDING",lived,lived == 1 ? "" : "S");
        }else{
            snprintf(buf,sizeof(buf),"   -   KEPT SINCE %s",ChasmStatsDate(hist.from).c_str());
        }
        since += buf;
        float title_w = overlay->MeasureText(title,26.0f * s).x;
        overlay->AddText(since.c_str(),vec2(l.a0 + title_w + 18.0f * s,l.p0.y + 41.0f * s),13.0f * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        chips_x = l.a0 + title_w + 18.0f * s + overlay->MeasureText(since.c_str(),13.0f * s).x + 28.0f * s;
    }
    //The chips: ALL, then each colony by its number - the one shown lit, each a press.
    if (colonies.size() > 1){
        const float chip_text = 12.0f * s;
        const float chip_pad = 10.0f * s;
        float x = chips_x;
        float y0 = l.p0.y + 25.0f * s;
        float y1 = y0 + 22.0f * s;
        for (size_t i = 0; i <= colonies.size(); i++){
            uint32_t c = (i == 0) ? 0 : colonies[i - 1];
            std::string label = (c == 0) ? std::string("ALL") : "COLONY " + std::to_string(c);
            float cw = overlay->MeasureText(label.c_str(),chip_text).x + chip_pad * 2.0f;
            bool f_on = c == colony;
            bool f_over = mouse.x >= x && mouse.x <= x + cw && mouse.y >= y0 && mouse.y <= y1;
            overlay->AddRect(vec2(x,y0),vec2(x + cw,y1),6.0f * s,
                             f_on ? HudFaded(HUD_SELECT,150) : f_over ? UIColor(255,255,255,40) : UIColor(255,255,255,14));
            overlay->AddText(label.c_str(),vec2(x + chip_pad,y1 - 7.0f * s),chip_text,f_on ? HUD_TEXT : HUD_TEXT_DIM,UI_ALIGN_LEFT);
            overlay_hits_drawing.push_back(OverlayHit{vec2(x,y0),vec2(x + cw,y1),OVERLAY_HIT_STATS_COLONY,(int)c});
            x += cw + 6.0f * s;
        }
    }
    ChasmStatsNow now = ChasmStatsNowOf(*e,*z,colony);
    ChasmStatsTotals totals = ChasmStatsSum(hist);
    const float head = 13.0f * s;           //a section's heading
    const float text = 14.0f * s;
    const float small = 12.0f * s;
    const float line_h = 20.0f * s;
    char buf[160];

    //--- NOW --------------------------------------------------------------------------------------------------------
    {
        float x = l.a0;
        float y = l.top;
        auto heading = [&](const char* name){
            overlay->AddText(name,vec2(x,y + 12.0f * s),head,HUD_TEXT_DIM,UI_ALIGN_LEFT);
            overlay->AddLine(vec2(x,y + 18.0f * s),vec2(l.a1,y + 18.0f * s),1.0f * s,STATS_GRID);
            y += 30.0f * s;
        };
        auto row = [&](const std::string& label, const std::string& value, uint32_t colour){
            if (y + line_h > l.bottom){
                return;
            }
            y += line_h;
            overlay->AddText(label.c_str(),vec2(x,y - 5.0f * s),text,colour,UI_ALIGN_LEFT);
            overlay->AddText(value.c_str(),vec2(l.a1,y - 5.0f * s),text,HUD_TEXT,UI_ALIGN_RIGHT);
        };
        heading("NOW");
        //The people: a big number, then by stage.
        snprintf(buf,sizeof(buf),"%i",now.people);
        overlay->AddText(buf,vec2(x,y + 26.0f * s),34.0f * s,HUD_TEXT,UI_ALIGN_LEFT);
        float big_w = overlay->MeasureText(buf,34.0f * s).x;
        overlay->AddText(now.people == 1 ? "PERSON" : "PEOPLE",vec2(x + big_w + 10.0f * s,y + 26.0f * s),text,HUD_TEXT_DIM,
                         UI_ALIGN_LEFT);
        y += 38.0f * s;
        //The old only once there are any (OLD AGE, lifeline_plan.md): hours into a game, so not a "0 old" until then.
        std::string old = now.row.people[LIFE_OLD] ? std::to_string(now.row.people[LIFE_OLD]) + " old, " : std::string();
        snprintf(buf,sizeof(buf),"%s%i adult%s, %i young, %i child%s, %i bab%s",old.c_str(),now.row.people[LIFE_ADULT],
                 now.row.people[LIFE_ADULT] == 1 ? "" : "s",now.row.people[LIFE_YOUNG],now.row.people[LIFE_CHILD],
                 now.row.people[LIFE_CHILD] == 1 ? "" : "ren",now.row.people[LIFE_BABY],now.row.people[LIFE_BABY] == 1 ? "y" : "ies");
        overlay->AddText(Fit(overlay,buf,small,l.a1 - x).c_str(),vec2(x,y),small,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        y += 17.0f * s;
        snprintf(buf,sizeof(buf),"%i expecting, %i in a camp or homeless",now.expecting,now.in_camp);
        overlay->AddText(Fit(overlay,buf,small,l.a1 - x).c_str(),vec2(x,y),small,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        y += 14.0f * s;
        //What there is to live on, in days for everyone at their shares - the charts' figure.
        bool f_people = now.people > 0;
        snprintf(buf,sizeof(buf),"%.1f days",now.food_days);
        row("FOOD",f_people ? buf : "-",HudGoodColour(GOOD_WHEAT));
        snprintf(buf,sizeof(buf),"%.1f days",now.water_days);
        row("WATER",f_people ? buf : "-",HudGoodColour(GOOD_WATER));
        snprintf(buf,sizeof(buf),"%i houses, %i tents, %i beds",now.houses,now.tents,now.beds);
        row("HOMES",buf,HUD_TEXT_DIM);
        y += 12.0f * s;
        //Every good the colony has, wherever it lies, in the goods' order - two to a line, each with its colour.
        heading("GOODS HELD");
        {
            int column = 0;
            float cw = (l.a1 - x) * 0.5f;
            for (int g : HudGoodsOrder()){
                if (now.row.held[g] <= 0 || y + line_h > l.bottom){
                    continue;
                }
                if (column == 0){
                    y += line_h;
                }
                float gx = x + cw * column;
                overlay->AddRect(vec2(gx,y - 14.0f * s),vec2(gx + 9.0f * s,y - 5.0f * s),2.0f * s,HudGoodColour(g));
                overlay->AddText(GoodName(g),vec2(gx + 15.0f * s,y - 5.0f * s),text,HUD_TEXT_DIM,UI_ALIGN_LEFT);
                overlay->AddText(std::to_string(now.row.held[g]).c_str(),vec2(gx + cw - 12.0f * s,y - 5.0f * s),text,HUD_TEXT,
                                 UI_ALIGN_RIGHT);
                column = 1 - column;
            }
        }
        y += 12.0f * s;
        //The buildings by kind standing whole, two to a line as the goods are; then what is still being built.
        heading("BUILDINGS");
        {
            int column = 0;
            float cw = (l.a1 - x) * 0.5f;
            int building = 0;
            for (int k = 1; k < ZONE_KIND_COUNT; k++){
                building += now.building[k];
                if (now.standing[k] <= 0 || y + line_h > l.bottom){
                    continue;
                }
                if (column == 0){
                    y += line_h;
                }
                float gx = x + cw * column;
                std::string count = std::to_string(now.standing[k]);
                float count_w = overlay->MeasureText(count.c_str(),text).x;
                std::string label = Fit(overlay,ChasmKindLabel(k,now.standing[k] != 1),small,cw - count_w - 22.0f * s);
                overlay->AddText(label.c_str(),vec2(gx,y - 5.0f * s),small,HUD_TEXT_DIM,UI_ALIGN_LEFT);
                overlay->AddText(count.c_str(),vec2(gx + cw - 12.0f * s,y - 5.0f * s),text,HUD_TEXT,UI_ALIGN_RIGHT);
                column = 1 - column;
            }
            if (building > 0){
                row("being built",std::to_string(building),HUD_AMBER);
            }
        }
        y += 12.0f * s;
        //The settlements, each with its people and beds; the dead by cause.
        snprintf(buf,sizeof(buf),"COLONIES  %i",(int)now.settlements.size());
        heading(buf);
        //In the order founded - the economy's, by their oldest homes' ids - each by its colony number, as COLONY N.
        for (size_t i = 0; i < now.settlements.size(); i++){
            const ChasmStatsSettlement& st = now.settlements[i];
            snprintf(buf,sizeof(buf),"%i people, %i beds",st.people,st.beds);
            row("colony " + std::to_string(st.colony),buf,HUD_TEXT_DIM);
        }
        y += 12.0f * s;
        snprintf(buf,sizeof(buf),"THE DEAD  %i",now.dead);
        heading(buf);
        for (int c = 0; c < CHASM_STATS_CAUSES; c++){
            if (now.dead_of[c] > 0){
                row(c == NEEDS_DIED_ZOMBIE ? "killed by zombies" : std::string("of ") + EconomyDeathCause(c),
                    std::to_string(now.dead_of[c]),HUD_TEXT_DIM);
            }
        }
    }

    //--- OVER TIME --------------------------------------------------------------------------------------------------
    {
        float x0 = l.b0;
        float x1 = l.b1;
        float y = l.top;
        overlay->AddText(f_founded ? "SINCE IT WAS FOUNDED" : (hist.from == 0 && !f_later) ? "SINCE THE LANDING" : "SINCE IT WAS KEPT",
                         vec2(x0,y + 12.0f * s),head,HUD_TEXT_DIM,
                         UI_ALIGN_LEFT);
        overlay->AddLine(vec2(x0,y + 18.0f * s),vec2(x1,y + 18.0f * s),1.0f * s,STATS_GRID);
        y += 28.0f * s;
        //The totals, a tile each: summed from the journal and the ledger now, never kept.
        int harvested = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            harvested += totals.harvested[g];
        }
        struct Tile{ const char* label; int value; };
        const Tile tiles[] = {{"BORN",totals.born},{"DIED",totals.died},{"HARVESTED",harvested},{"TREES FELLED",totals.felled},
                              {"BUILT",totals.finished},{"ZOMBIES KILLED",totals.zombies}};
        //The last, zombies killed (KILLS), only once there were any: a colony that never met one is not told of them.
        const int tile_count = (int)(sizeof(tiles) / sizeof(tiles[0])) - ((totals.zombies > 0) ? 0 : 1);
        float tg = 8.0f * s;
        float tw = (x1 - x0 - tg * (tile_count - 1)) / tile_count;
        float th = 50.0f * s;
        for (int i = 0; i < tile_count; i++){
            vec2 t0(x0 + (tw + tg) * i,y);
            overlay->AddRect(t0,t0 + vec2(tw,th),6.0f * s,STATS_TILE);
            overlay->AddText(std::to_string(tiles[i].value).c_str(),vec2(t0.x + 10.0f * s,t0.y + 25.0f * s),22.0f * s,HUD_TEXT,
                             UI_ALIGN_LEFT);
            overlay->AddText(tiles[i].label,vec2(t0.x + 10.0f * s,t0.y + 42.0f * s),10.5f * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
        y += th + 18.0f * s;
        //BY DAY: a point a day closed, the start's first and today's last, as they are now.
        overlay->AddText("BY DAY",vec2(x0,y + 12.0f * s),head,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        overlay->AddLine(vec2(x0,y + 18.0f * s),vec2(x1,y + 18.0f * s),1.0f * s,STATS_GRID);
        y += 28.0f * s;
        std::vector<int> days;
        ChartLine people{"people",HUD_TEXT,{},0};
        ChartLine food{"food",HudGoodColour(GOOD_WHEAT),{},1};
        ChartLine water{"water",HudGoodColour(GOOD_WATER),{},1};
        ChartLine wood{"wood",HudGoodColour(GOOD_WOOD),{},0};
        ChartLine stone{"stone",HudGoodColour(GOOD_STONE),{},0};
        auto point = [&](int day, const EconomyDay& row){
            days.push_back(day);
            int heads = 0;
            for (int p : row.people){
                heads += p;
            }
            people.values.push_back((float)heads);
            float fd = 0.0f;
            float wd = 0.0f;
            ChasmStatsDays(row,fd,wd);
            food.values.push_back(fd);
            water.values.push_back(wd);
            wood.values.push_back((float)row.held[GOOD_WOOD]);
            stone.values.push_back((float)row.held[GOOD_STONE]);
        };
        //The start is the first day's beginning, a row its end: drawn a day apart.
        point(hist.start.day,hist.start);
        for (const EconomyDay& d : hist.days){
            point(d.day + 1,d);
        }
        point(std::max(days.back(),(int)(e->tick / CALENDAR_DAY_TICKS) + (e->tick % CALENDAR_DAY_TICKS ? 1 : 0)),now.row);
        float bars_h = 116.0f * s;
        float charts_h = l.bottom - y - bars_h - 46.0f * s;
        float cg = 10.0f * s;
        float cw = (x1 - x0 - cg) * 0.5f;
        float ch = (charts_h - cg) * 0.5f;
        DrawLineChart(overlay,vec2(x0,y),vec2(x0 + cw,y + ch),"PEOPLE",days,{people},mouse,s);
        DrawLineChart(overlay,vec2(x0 + cw + cg,y),vec2(x1,y + ch),"FOOD AND WATER, DAYS OF IT",days,{food,water},mouse,s);
        DrawLineChart(overlay,vec2(x0,y + ch + cg),vec2(x0 + cw,y + ch * 2.0f + cg),"WOOD",days,{wood},mouse,s);
        DrawLineChart(overlay,vec2(x0 + cw + cg,y + ch + cg),vec2(x1,y + ch * 2.0f + cg),"STONE",days,{stone},mouse,s);
        y += charts_h + 18.0f * s;
        //BY SEASON: four small bar charts, a measure each.
        overlay->AddText("BY SEASON",vec2(x0,y + 12.0f * s),head,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        overlay->AddLine(vec2(x0,y + 18.0f * s),vec2(x1,y + 18.0f * s),1.0f * s,STATS_GRID);
        y += 28.0f * s;
        std::vector<ChasmStatsSeason> seasons = ChasmStatsSeasons(hist);
        float bw = (x1 - x0 - cg * 3.0f) / 4.0f;
        float by1 = std::min(l.bottom,y + bars_h);
        DrawSeasonBars(overlay,vec2(x0,y),vec2(x0 + bw,by1),"BORN",totals.born,seasons,&ChasmStatsSeason::born,
                       UIColor(156,204,112),mouse,s);
        DrawSeasonBars(overlay,vec2(x0 + (bw + cg),y),vec2(x0 + bw * 2.0f + cg,by1),"DIED",totals.died,seasons,
                       &ChasmStatsSeason::died,HUD_DANGER,mouse,s);
        DrawSeasonBars(overlay,vec2(x0 + (bw + cg) * 2.0f,y),vec2(x0 + bw * 3.0f + cg * 2.0f,by1),"HARVESTED",harvested,seasons,
                       &ChasmStatsSeason::harvested,HudGoodColour(GOOD_WHEAT),mouse,s);
        DrawSeasonBars(overlay,vec2(x0 + (bw + cg) * 3.0f,y),vec2(x1,by1),"FELLED",totals.felled,seasons,
                       &ChasmStatsSeason::felled,HudGoodColour(GOOD_WOOD),mouse,s);
    }

    //--- THE JOURNAL ------------------------------------------------------------------------------------------------
    {
        float x0 = l.c0;
        float x1 = l.c1;
        float y = l.top;
        snprintf(buf,sizeof(buf),"JOURNAL  %i",(int)hist.journal.size());
        overlay->AddText(buf,vec2(x0,y + 12.0f * s),head,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        overlay->AddLine(vec2(x0,y + 18.0f * s),vec2(x1,y + 18.0f * s),1.0f * s,STATS_GRID);
        y += 24.0f * s;
        const float jline = 21.0f * s;
        int fits = std::max(1,(int)((l.bottom - y - 20.0f * s) / jline));
        int count = (int)hist.journal.size();
        int scroll = std::min(stats_scroll.load(),std::max(0,count - fits));
        if (count == 0){
            overlay->AddText("nothing yet",vec2(x0,y + jline),text,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
        float date_w = overlay->MeasureText("Y00 SUMMER 10",small).x + 12.0f * s;
        //Newest first: the scroll is how many of the newest are above the top.
        for (int i = 0; i < fits && count - 1 - scroll - i >= 0; i++){
            const EconomyEvent& ev = hist.journal[count - 1 - scroll - i];
            y += jline;
            overlay->AddRect(vec2(x0,y - 13.0f * s),vec2(x0 + 4.0f * s,y - 1.0f * s),1.5f * s,JournalColour(ev.kind));
            overlay->AddText(ChasmStatsDate(ev.tick).c_str(),vec2(x0 + 12.0f * s,y - 3.0f * s),small,HUD_TEXT_DIM,UI_ALIGN_LEFT);
            std::string words = Fit(overlay,ChasmJournalText(*e,ev),text,x1 - x0 - 12.0f * s - date_w);
            overlay->AddText(words.c_str(),vec2(x0 + 12.0f * s + date_w,y - 3.0f * s),text,HUD_TEXT,UI_ALIGN_LEFT);
        }
        //Where the list is, when it is longer than the page.
        if (count > fits){
            snprintf(buf,sizeof(buf),"%i-%i of %i  -  the wheel scrolls",scroll + 1,std::min(count,scroll + fits),count);
            overlay->AddText(buf,vec2(x0,l.bottom),small,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
    }
#ifdef DEBUG
    //A debug build says so at the foot of the page when the history and the state it was kept beside disagree.
    std::vector<std::string> problems = ChasmStatsCheck(*e);
    if (!problems.empty()){
        std::string first = "CHECK: " + problems[0] + (problems.size() > 1 ? " (+" + std::to_string(problems.size() - 1) + ")" : "");
        overlay->AddText(Fit(overlay,first,small,l.p1.x - l.a0 - 24.0f * s).c_str(),vec2(l.a0,l.p1.y - 8.0f * s),small,HUD_DANGER,
                         UI_ALIGN_LEFT);
    }
#endif
}

/*
    PHYSICS THREAD, from TickEconomy in debug builds: the check run once a day closes - a line in the log for each
    disagreement, so one is found the day it happens rather than when someone opens the page.
*/
void ApplicationChasm::CheckStats(){
#ifdef DEBUG
    const EconomyHistory& h = economy.State().history;
    if (h.days.size() == stats_checked_days){
        return;
    }
    stats_checked_days = h.days.size();
    for (const std::string& p : ChasmStatsCheck(economy.State())){
        debug->Err("Statistics check, day %i: %s\n",h.today.day,p.c_str());
    }
#endif
}

//--- The tool ---------------------------------------------------------------------------------------------------------

#ifdef USE_MCP
namespace {

json DayJson(const EconomyDay& d){
    json held = json::object();
    for (int g = 0; g < GOOD_COUNT; g++){
        if (d.held[g]){
            held[GoodName(g)] = d.held[g];
        }
    }
    float food = 0.0f;
    float water = 0.0f;
    ChasmStatsDays(d,food,water);
    json row{{"day",d.day},{"people",json{{"babies",d.people[LIFE_BABY]},{"children",d.people[LIFE_CHILD]},
                                          {"young",d.people[LIFE_YOUNG]},{"adults",d.people[LIFE_ADULT]},
                                          {"old",d.people[LIFE_OLD]}}},
             {"held",held},{"food_days",food},{"water_days",water},{"felled",d.felled},{"broken",d.broken},
             {"storeys",d.storeys},{"zombies",d.zombies}};
    //Its colonies' rows (BY COLONY), each the same with its `colony` - 0 nobody's.
    if (!d.colonies.empty()){
        json colonies = json::array();
        for (const EconomyDay& r : d.colonies){
            json one = DayJson(r);
            one["colony"] = r.colony;
            colonies.push_back(one);
        }
        row["colonies"] = colonies;
    }
    return row;
}

//A cause of death as a key: "a zombie" is the words' (EconomyDeathCause), not a name.
const char* CauseKey(int cause){
    return (cause == NEEDS_DIED_ZOMBIE) ? "zombie" : EconomyDeathCause(cause);
}

json EventJson(const EconomyState& e, const EconomyEvent& ev){
    json what = json::array();
    for (int g = 0; g < GOOD_COUNT; g++){
        if (ev.what & GOOD_BIT(g)){
            what.push_back(GoodName(g));
        }
    }
    return json{{"tick",ev.tick},{"date",ChasmStatsDate(ev.tick)},{"kind",EconomyEventName(ev.kind)},{"person",ev.person},
                {"building",ev.building},{"plot",ev.plot},{"x",ev.pos.x},{"z",ev.pos.y},{"what",what},{"amount",ev.amount},{"colony",ev.colony},
                {"text",ChasmJournalText(e,ev)}};
}

}

void ApplicationChasm::RegisterStatsTools(){
    MCPServer::Get()->RegisterTool("chasm_stats",
        "THE STATISTICS (docs/statistics_plan.md): the colony's history as the rules keep it (Economy.h, THE HISTORY) - "
        "saved, so a load shows the same - and the figures the Statistics page shows. op status (default): `now` (people "
        "by stage, expecting, in_camp, homes, beds, goods held anywhere, food_days and water_days for everyone at their "
        "shares, buildings standing and being built by kind, settlements, the dead by cause); `totals` since the history "
        "began, summed from the journal and the ledger now (born, died and of what, harvests and what they gave, buildings "
        "finished by kind, settlements founded, seams seen, trees felled, rocks broken, storeys raised, zombies killed); "
        "`seasons` (born, died, harvested, felled - each season from the first); `from` (the tick it began: 0 a colony's "
        "landing, else the load of a save from before it), `start`, `today` and the last `days` ledger rows (default all, "
        "a row a day: day, people by stage, held, food_days, water_days, felled, broken, storeys, zombies); the last "
        "`journal` lines (default 40, -1 all: tick, date, kind, person, building, plot, x, z, what, amount, text); and "
        "`check` - empty, or what in the history disagrees with the state it was kept beside (statistics_plan.md: the "
        "dead, the born, the forest, the zombies killed against everyone's kills, each row's people, each row's colonies "
        "adding up to it). op open / op close: the page, over the game paused under it - as Tab does. `page` says whether it "
        "is up. BY COLONY (docs/settlements_plan.md, step 4): `colony` N narrows every figure but `check` to that colony - "
        "its own rows, journal and now - and shows it on the page, as its chip does (0 all, the default; ignored with one "
        "colony); `colonies` lists those it can show, and a row's `colonies` its colonies' rows (colony 0 nobody's). "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"colony",{{"type","integer"},{"description","a colony's number: its figures alone, and the page shows it; 0 all"}}},
                {"days",{{"type","integer"},{"description","ledger rows, the newest: default all"}}},
                {"journal",{{"type","integer"},{"description","journal lines, the newest: default 40, -1 all"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string op = args.value("op",std::string("status"));
            if (op == "open" || op == "close"){
                stats_request = (op == "open") ? 1 : 2;
                //Served on the next pass (UpdateStats): wait for it, a moment at most.
                for (int i = 0; i < 40 && stats_request.load() != 0; i++){
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }else if (op != "status"){
                return json{{"error","op: status, open or close"}};
            }
            std::shared_ptr<const EconomyState> e = GetEconomy();
            std::shared_ptr<const ZoneState> z = GetZones();
            if (!e || !z || e->world != z->world){
                return json{{"error","no colony"}};
            }
            //BY COLONY: the colony asked for - and the page's - else all of them.
            std::vector<uint32_t> colonies = ChasmStatsColonies(*e);
            uint32_t colony = (uint32_t)std::max(0,args.value("colony",0));
            colony = (colonies.size() > 1 && std::find(colonies.begin(),colonies.end(),colony) != colonies.end()) ? colony : 0;
            if (args.contains("colony")){
                stats_colony = colony;
            }
            EconomyHistory colony_hist;
            if (colony){
                colony_hist = ChasmColonyHistory(e->history,colony);
            }
            const EconomyHistory& h = colony ? colony_hist : e->history;
            ChasmStatsNow n = ChasmStatsNowOf(*e,*z,colony);
            json kinds_standing = json::object();
            json kinds_building = json::object();
            for (int k = 1; k < ZONE_KIND_COUNT; k++){
                if (n.standing[k]){
                    kinds_standing[ZoneKindName(k)] = n.standing[k];
                }
                if (n.building[k]){
                    kinds_building[ZoneKindName(k)] = n.building[k];
                }
            }
            json settlements = json::array();
            for (const ChasmStatsSettlement& st : n.settlements){
                settlements.push_back(json{{"id",st.id},{"colony",st.colony},{"people",st.people},{"beds",st.beds}});
            }
            json dead_of = json::object();
            for (int c = 0; c < CHASM_STATS_CAUSES; c++){
                dead_of[CauseKey(c)] = n.dead_of[c];
            }
            json now = DayJson(n.row);
            now.erase("felled");
            now.erase("broken");
            now.erase("storeys");
            now.erase("zombies");
            now["day"] = (int)(e->tick / CALENDAR_DAY_TICKS);
            now["date"] = ChasmStatsDate(e->tick);
            now["total"] = n.people;
            now["expecting"] = n.expecting;
            now["in_camp"] = n.in_camp;
            now["homes"] = json{{"houses",n.houses},{"tents",n.tents},{"beds",n.beds}};
            now["buildings"] = json{{"standing",kinds_standing},{"being_built",kinds_building}};
            now["settlements"] = settlements;
            now["dead"] = json{{"total",n.dead},{"of",dead_of}};
            ChasmStatsTotals t = ChasmStatsSum(h);
            json harvested = json::object();
            for (int g = 0; g < GOOD_COUNT; g++){
                if (t.harvested[g]){
                    harvested[GoodName(g)] = t.harvested[g];
                }
            }
            json finished = json::object();
            for (int k = 1; k < ZONE_KIND_COUNT; k++){
                if (t.finished_kind[k]){
                    finished[ZoneKindName(k)] = t.finished_kind[k];
                }
            }
            json died_of = json::object();
            for (int c = 0; c < CHASM_STATS_CAUSES; c++){
                died_of[CauseKey(c)] = t.died_of[c];
            }
            json totals{{"born",t.born},{"died",t.died},{"died_of",died_of},{"harvests",t.harvests},{"harvested",harvested},
                        {"finished",t.finished},{"finished_kind",finished},{"settled",t.settled},{"seams_seen",t.seams},
                        {"felled",t.felled},{"broken",t.broken},{"storeys",t.storeys},{"zombies",t.zombies}};
            json seasons = json::array();
            for (const ChasmStatsSeason& se : ChasmStatsSeasons(h)){
                seasons.push_back(json{{"season",se.season},{"name",ChasmStatsSeasonName(se.season)},{"born",se.born},
                                       {"died",se.died},{"harvested",se.harvested},{"felled",se.felled}});
            }
            int want_days = args.value("days",-1);
            size_t day_count = (want_days < 0) ? h.days.size() : std::min(h.days.size(),(size_t)want_days);
            json days = json::array();
            for (size_t i = h.days.size() - day_count; i < h.days.size(); i++){
                days.push_back(DayJson(h.days[i]));
            }
            int want_lines = args.value("journal",40);
            size_t line_count = (want_lines < 0) ? h.journal.size() : std::min(h.journal.size(),(size_t)want_lines);
            json journal = json::array();
            for (size_t i = h.journal.size() - line_count; i < h.journal.size(); i++){
                journal.push_back(EventJson(*e,h.journal[i]));
            }
            json check = json::array();
            for (const std::string& p : ChasmStatsCheck(*e)){
                check.push_back(p);
            }
            //The calendar's tick, as a save keeps it - the economy's own is its last tick's, one short of it after a tick.
            json result{{"page",f_stats_open.load()},{"colony",colony},{"colonies",colonies},{"page_colony",stats_colony.load()},
                        {"tick",calendar_shown.load()},{"from",h.from},{"felled_from",h.felled_from},
                        {"broken_from",h.broken_from},{"kills_from",h.kills_from},{"now",now},{"totals",totals},{"seasons",seasons},
                        {"start",DayJson(h.start)},{"today",DayJson(h.today)},{"day_count",h.days.size()},{"days",days},
                        {"journal_count",h.journal.size()},{"journal",journal},{"check",check}};
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
