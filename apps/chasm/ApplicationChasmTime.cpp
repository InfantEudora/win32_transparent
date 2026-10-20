#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "ChasmStats.h"
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

    At every speed a tick is still exactly a tick: 4x is four ticks of the same 20 ms in each pass of
    the physics loop (core's batches, Application::physics_time_factor), never one tick of 80 ms. The
    PLAY speeds are 1x, 2x and 4x (the user's, 2026-10-08) - the calendar's button and the keys go
    through them; the tool and the panel set any factor from 1 to 10. A year at 4x is 7.5 minutes.
*/

namespace {

//What every site takes, by building id and good - its storeys still to build, a field's cells still staked out
//(fields_plan.md) - whatever has been brought. One pass over the plots, not one a building: this is every frame.
std::vector<std::array<int,GOOD_COUNT>> SiteCosts(const ZoneState& z){
    std::vector<std::array<int,GOOD_COUNT>> cost(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.storeys[v] > z.standing[v] && z.building[v] < cost.size()){
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[z.building[v]][g] += EconomyPlotLeftCost(*z.world,z,(int)v,g);
            }
        }
    }
    for (size_t c = 0; c < z.field.size(); c++){
        if (z.field[c] && !z.field_built[c] && z.field[c] < cost.size()){
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[z.field[c]][g] += EconomyCellCost(z.buildings[z.field[c]].kind,g);
            }
        }
    }
    return cost;
}

const float speeds[] = {1.0f,2.0f,4.0f};

}

GoodsMask ChasmBarGoods(const ZoneState& z, const EconomyState& e, const std::array<int,GOOD_COUNT>& stored){
    GoodsMask wanted = 0;
    std::vector<std::array<int,GOOD_COUNT>> cost = SiteCosts(z);
    for (size_t id = 1; id < cost.size(); id++){
        for (int g = 0; g < GOOD_COUNT; g++){
            wanted |= (cost[id][g] > EconomySiteBrought(e,(uint32_t)id,g)) ? GOOD_BIT(g) : (GoodsMask)0;
        }
    }
    GoodsMask shown = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        if (GoodCategory(g) != GOOD_CATEGORY_FOOD && (g == GOOD_WOOD || g == GOOD_WATER || stored[g] > 0 || (wanted & GOOD_BIT(g)))){
            shown |= GOOD_BIT(g);
        }
    }
    return shown;
}

int ApplicationChasm::SpeedCount(){
    return (int)(sizeof(speeds) / sizeof(speeds[0]));
}

float ApplicationChasm::SpeedFactor(int index){
    return speeds[std::max(0,std::min(SpeedCount() - 1,index))];
}

int ApplicationChasm::NearestSpeed(){
    int best = 0;
    for (int i = 1; i < SpeedCount(); i++){
        if (std::fabs(speeds[i] - physics_time_factor) < std::fabs(speeds[best] - physics_time_factor)){
            best = i;
        }
    }
    return best;
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

void ApplicationChasm::SetSpeedFactor(float factor){
    factor = std::max(1.0f,std::min(PHYSICS_TIME_FACTOR_MAX,factor));
    physics_time_factor = factor;
    int i = NearestSpeed();
    speed_index = (std::fabs(speeds[i] - factor) < 0.01f) ? i : -1;
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
bool ApplicationChasm::KeysLive(){
    return main_scene->inputcontroller->IsInputLive() && !UIWantsKeyboard();
}

void ApplicationChasm::UpdateTimeKeys(){
    InputController* input = main_scene->inputcontroller;
    bool f_pause = input->WasKeyPressed(INPUT_CHASM_PAUSE);
    bool f_slower = input->WasKeyPressed(INPUT_CHASM_SLOWER);
    bool f_faster = input->WasKeyPressed(INPUT_CHASM_FASTER);
    if (!KeysLive()){
        return;
    }
    if (f_pause){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }
    //From another factor (the tool's, the panel's), the keys go on from the play speed nearest it.
    int at = (speed_index >= 0) ? (int)speed_index : NearestSpeed();
    if (f_slower){
        SetSpeed(at - 1);
    }
    if (f_faster){
        //Faster from a pause also starts the clock again: that key means "go", whatever came before.
        if (main_scene->IsPhysicsPaused()){
            main_scene->PausePhysics(false);
        }else{
            SetSpeed(at + 1);
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


/*
    PHYSICS THREAD, from UpdatePick: a press on the calendar's buttons. Pause is space's; speed goes round -
    the next speed, back to normal after the fastest - and changes it whether or not the clock runs, so it
    can be set before going again. Neither is recorded, like the keys.
*/
void ApplicationChasm::PressTimeButton(int action){
    if (action == OVERLAY_HIT_PAUSE){
        main_scene->PausePhysics(!main_scene->IsPhysicsPaused());
    }else if (action == OVERLAY_HIT_SPEED){
        int at = (speed_index >= 0) ? (int)speed_index : NearestSpeed();
        SetSpeed((at + 1) % SpeedCount());
    }
}

/*
    RENDER THREAD. The game's own HUD at the top of the screen, in every build (not a debug panel):
      - TOP RIGHT, the calendar: the season by name in its colour, the day, the year, and two buttons -
        PAUSE (two bars; lit, with the arrow that starts the clock again, while paused) and SPEED (one
        chevron per multiple of normal; a press goes to the next). Under it the year as a bar of its four
        seasons, the part gone bright, with a mark at today; and under the day a thin line filling as it passes.
      - TOP MIDDLE, the colony's stores - and a press on them opens the Statistics page (ApplicationChasmStats.cpp).
    Both take the mouse where they are drawn (OverlayHit), in every mode: a press on them presses their
    buttons, and paints or selects nothing beneath.

    Sized off the window's height, so it is the same part of the picture at any size.
*/
void ApplicationChasm::DrawOverlay(void){
    //Counted for a load waiting behind "Loading" (RunBusy): a frame has gone by, drawn or not.
    overlay_frames++;
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    float s = HudUiScale((float)main_window->height,settings.GetFloat("ui_size"));     //ChasmHud.h
    //...and the menus', as ReadMenuInput gives them on the physics thread: here too, so a menu just opened is drawn at
    //its size on its first frame, before its input has been read.
    float menu_scale = HudMenuScale((float)main_window->height,settings.GetFloat("ui_size"));
    for (UIMenu* m : {&title_menu,&ask_menu,&pause_menu,&screen_menu,&end_menu}){
        m->layout.scale = menu_scale;
    }
    //The title (ApplicationChasmTitle.cpp): its own picture, none of the game's HUD.
    if (TitleShowing()){
        DrawTitle(s);
        DrawAsk(s);
        DrawBusyAndNote(s);
        return;
    }
    //The Statistics page (ApplicationChasmStats.cpp): over the world without the HUD - nothing of the HUD can be pressed
    //while the page has the input, and its edges would only show round the page's. The pause menu likewise
    //(ApplicationChasmMenu.cpp), under the page when the page was opened from it.
    if (f_stats_open.load() || f_pause_open.load()){
        overlay_hits_drawing.clear();
        if (f_stats_open.load()){
            DrawStats(s);
        }else{
            DrawPause(s);
        }
        DrawAsk(s);
        DrawBusyAndNote(s);
        std::lock_guard<std::mutex> lock(pick_mutex);
        overlay_hits.swap(overlay_hits_drawing);
        return;
    }
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    bool f_paused = world_scene && world_scene->IsPhysicsPaused();
    //What the mouse can press on the overlay is gathered while drawing and handed to UpdatePick at the end.
    overlay_hits_drawing.clear();

    float w = 400.0f * s;
    float h = 66.0f * s;
    float pad = 16.0f * s;
    vec2 p0((float)main_window->width - 16.0f * s - w,12.0f * s);
    vec2 p1(p0.x + w,p0.y + h);
    overlay->AddRect(p0,p1,12.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});

    //The line of text: season, day, year - and at the right the two buttons, pause and then speed.
    float text = 20.0f * s;
    float baseline = p0.y + 29.0f * s;
    const float button_w = 46.0f * s;
    const float button_gap = 6.0f * s;
    vec2 speed0(p1.x - pad - button_w,p0.y + 9.0f * s);
    vec2 speed1(p1.x - pad,speed0.y + 30.0f * s);
    vec2 pause0(speed0.x - button_gap - button_w,speed0.y);
    vec2 pause1(speed0.x - button_gap,speed1.y);
    float right = pause0.x - 4.0f * s;
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

    //PAUSE: two bars to stop the clock; lit while it is stopped, with the arrow that starts it again.
    overlay->AddRect(pause0,pause1,7.0f * s,f_paused ? UIColor(196,92,72,210) : UIColor(255,255,255,28));
    vec2 pc((pause0.x + pause1.x) * 0.5f,(pause0.y + pause1.y) * 0.5f);
    if (f_paused){
        //The arrow: a fan of upright lines, tall at the left, closing to a point at the right.
        float aw = 12.0f * s;
        float ah = 16.0f * s;
        int n = std::max(4,(int)aw);
        for (int i = 0; i <= n; i++){
            float t = (float)i / n;
            float x = pc.x - aw * 0.4f + aw * t;
            float half = ah * 0.5f * (1.0f - t);
            overlay->AddLine(vec2(x,pc.y - half),vec2(x,pc.y + half),aw / n + 0.6f,HUD_TEXT);
        }
    }else{
        float bar_w = 5.0f * s;
        float bar_h = 16.0f * s;
        float gap = 4.0f * s;
        overlay->AddRect(vec2(pc.x - gap - bar_w,pc.y - bar_h * 0.5f),vec2(pc.x - gap,pc.y + bar_h * 0.5f),1.5f * s,HUD_TEXT);
        overlay->AddRect(vec2(pc.x + gap,pc.y - bar_h * 0.5f),vec2(pc.x + gap + bar_w,pc.y + bar_h * 0.5f),1.5f * s,HUD_TEXT);
    }
    overlay_hits_drawing.push_back(OverlayHit{pause0,pause1,OVERLAY_HIT_PAUSE,0});
    //SPEED: one chevron per multiple of normal - dim while paused, the speed it will go again at. Four
    //chevrons (4x) a size smaller to fit; a factor that is no play speed (the tool's, the panel's) as its
    //number. Amber while the simulation cannot keep up with it.
    float factor = physics_time_factor;
    int si = speed_index;
    float reached = GetPhysicsSpeed();
    bool f_short = !f_paused && factor > 1.0f && reached < factor * 0.9f;
    overlay->AddRect(speed0,speed1,7.0f * s,UIColor(255,255,255,28));
    vec2 sc((speed0.x + speed1.x) * 0.5f,(speed0.y + speed1.y) * 0.5f);
    std::string label;
    float label_size = text;
    if (si >= 0){
        label.assign((size_t)std::max(1,(int)SpeedFactor(si)),'>');
        label_size = (label.size() > 3) ? text * 0.75f : text;
    }else{
        snprintf(buf,sizeof(buf),"%gx",std::round(factor * 10.0f) / 10.0f);
        label = buf;
        label_size = text * 0.75f;
    }
    overlay->AddText(label.c_str(),vec2(sc.x,sc.y + label_size * 0.35f),label_size,
                     f_paused ? HUD_TEXT_DIM : f_short ? HUD_AMBER : HUD_TEXT,UI_ALIGN_CENTER);
    overlay_hits_drawing.push_back(OverlayHit{speed0,speed1,OVERLAY_HIT_SPEED,0});
    //Under the calendar, while it falls short: what it is managing - the simulation's work is the limit.
    if (f_short){
        snprintf(buf,sizeof(buf),"RUNNING AT %.1fx",reached);
        overlay->AddText(buf,vec2(p1.x - 4.0f * s,p1.y + 18.0f * s),13.0f * s,HUD_AMBER,UI_ALIGN_RIGHT);
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
        The colony's goods, at the top in the middle (docs/economy_plan.md; goods_plan.md, "The view"): PEOPLE, then
        the goods by category - WOOD, FOOD and WATER always, and any other good while the colony holds some or a site
        still wants it, so a colony that has never seen rope is shown no ROPE 0. FOOD is the foods summed, since any
        of them feeds someone - provisions, the posted people's, apart - and hovering it lists them. What lies in
        stores and the camp's supplies, not what waits at a workplace or is being carried.
    */
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::vector<std::pair<int,std::string>> food_tip;   //FOOD hovered: each food and its line, drawn over everything
    vec2 food_tip_at;
    if (e && z && e->world == z->world){
        std::array<int,GOOD_COUNT> t = EconomyStoredTotals(*e,*z);
        int food = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            food += (GOODS_FOOD & GOOD_BIT(g)) ? t[g] : 0;
        }
        GoodsMask shown = ChasmBarGoods(*z,*e,t);
        int people = (int)e->workers.size();
        int housed = 0;
        for (const EconomyWorker& k : e->workers){
            housed += k.house ? 1 : 0;
        }
        //Each item's text, measured, so the bar is as wide as what it says - centred on the screen. The goods in
        //their colours on the cards' strips (HudGoodColour): food in wheat's, the gold the foods share here.
        struct BarItem{
            std::string text;
            uint32_t colour;
            bool f_food;
        };
        std::vector<BarItem> items;
        char buf[48];
        if (housed < people){
            snprintf(buf,sizeof(buf),"PEOPLE %i/%i",housed,people);  //housed, of all of them
        }else{
            snprintf(buf,sizeof(buf),"PEOPLE %i",people);
        }
        items.push_back(BarItem{buf,HUD_TEXT,false});
        for (int g : HudGoodsOrder()){
            if (GoodCategory(g) == GOOD_CATEGORY_FOOD){
                if (g == GOOD_WHEAT){
                    snprintf(buf,sizeof(buf),"FOOD %i",food);
                    items.push_back(BarItem{buf,HudGoodColour(GOOD_WHEAT),true});
                }
                continue;
            }
            if (!(shown & GOOD_BIT(g))){
                continue;
            }
            std::string name = GoodName(g);
            std::transform(name.begin(),name.end(),name.begin(),::toupper);
            snprintf(buf,sizeof(buf),"%s %i",name.c_str(),t[g]);
            items.push_back(BarItem{buf,HudGoodColour(g),false});
        }
        const float size = 16.0f * s;
        const float gap = 22.0f * s;
        const float side = 14.0f * s;
        const float glyph_w = 14.0f * s;        //THE STATISTICS' mark at the right end: a press on the bar opens them
        std::vector<float> widths;
        float total = side * 2.0f + gap * (float)items.size() + glyph_w;
        for (const BarItem& it : items){
            widths.push_back(overlay->MeasureText(it.text.c_str(),size).x);
            total += widths.back();
        }
        float cx = (float)main_window->width * 0.5f;
        vec2 q0(cx - total * 0.5f,12.0f * s);
        vec2 q1(cx + total * 0.5f,q0.y + 30.0f * s);
        float mx = (float)overlay_mouse_x.load();
        float my = (float)overlay_mouse_y.load();
        bool f_bar_hover = mx >= q0.x && mx <= q1.x && my >= q0.y && my <= q1.y;
        overlay->AddRect(q0,q1,9.0f * s,HUD_PANEL);
        overlay_hits_drawing.push_back(OverlayHit{q0,q1,OVERLAY_HIT_STATS,0});
        float x = q0.x + side;
        float food_x = -1.0f;
        float food_w = 0.0f;
        for (size_t i = 0; i < items.size(); i++){
            overlay->AddText(items[i].text.c_str(),vec2(x,q0.y + 21.0f * s),size,items[i].colour,UI_ALIGN_LEFT);
            if (items[i].f_food){
                food_x = x;
                food_w = widths[i];
            }
            x += widths[i] + gap;
        }
        //The mark: three bars rising, brighter while the mouse is on the bar.
        for (int b = 0; b < 3; b++){
            float bx = x + b * (glyph_w / 3.0f);
            float bh = (5.0f + 4.0f * b) * s;
            float base = q0.y + 21.0f * s;
            overlay->AddRect(vec2(bx,base - bh),vec2(bx + glyph_w / 3.0f - 1.5f * s,base),1.0f * s,
                             f_bar_hover ? HUD_TEXT : HUD_TEXT_DIM);
        }
        //Hovering FOOD: its foods, each in its colour, under the bar - drawn last, over a card.
        if (food_x >= 0.0f && mx >= food_x - gap * 0.5f && mx <= food_x + food_w + gap * 0.5f && my >= q0.y && my <= q1.y){
            for (int g : HudGoodsOrder()){
                if (GoodCategory(g) == GOOD_CATEGORY_FOOD){
                    snprintf(buf,sizeof(buf),"%i %s%s",t[g],GoodName(g),(GOODS_FOOD & GOOD_BIT(g)) ? "" : " - for posted people");
                    food_tip.push_back(std::make_pair(g,std::string(buf)));
                }
            }
            food_tip_at = vec2(food_x - side,q1.y + 6.0f * s);
        }
    }

    /*
        THE TOP LEFT, one under another so none hides the next: the demo's goals (play mode, ApplicationChasmGoals.cpp),
        REC or the replay's bar (ApplicationChasmRecord.cpp), then the selection's colony (SelectionColony).
    */
    hud_left_y = 12.0f * s;
    if (PlayMode()){
        hud_left_y = DrawGoals(s,hud_left_y);
    }
    hud_left_y = DrawRecording(s,hud_left_y);
    //Play mode's own UI: the tools and over them what needs attention (issues_plan.md), the jobs and over
    //them the neediest, and the selection's card and pins (selection_plan.md).
    if (PlayMode()){
        float bar_top = DrawBuildBar(s);
        build_stack_left = build_bar_min.x;
        build_stack_top = DrawIssues(s,bar_top - 6.0f * s);
        float jobs_top = DrawJobsOverview(s);
        DrawNeediest(s,jobs_top - 8.0f * s);
        DrawSelectionOverlay(s);
        DrawWalkTip(s);
        DrawCostTips(s);
        DrawForageMenu(s);      //last: over the rest, and its chips win where they overlap (OverlayHitAt)
    }
    if (!food_tip.empty()){
        const float text_size = 14.0f * s;
        const float line_h = 20.0f * s;
        const float side = 14.0f * s;
        float w = 0.0f;
        for (const auto& line : food_tip){
            w = std::max(w,overlay->MeasureText(line.second.c_str(),text_size).x);
        }
        vec2 t0 = food_tip_at;
        vec2 t1(t0.x + side * 2.0f + 15.0f * s + w,t0.y + 10.0f * s + line_h * food_tip.size());
        overlay->AddRect(t0,t1,9.0f * s,HudFaded(HUD_PANEL,235));
        for (size_t i = 0; i < food_tip.size(); i++){
            float base = t0.y + line_h * (i + 1);
            vec2 sw(t0.x + side,base - 10.0f * s);
            overlay->AddRect(sw,sw + vec2(9.0f * s,9.0f * s),1.5f * s,HudFaded(HudGoodColour(food_tip[i].first),235));
            overlay->AddText(food_tip[i].second.c_str(),vec2(sw.x + 15.0f * s,base),text_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
    }
    DrawAsk(s);     //a question over the game, over everything (ApplicationChasmTitle.cpp)
    DrawBusyAndNote(s);
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        overlay_hits.swap(overlay_hits_drawing);
    }
}

/*
    RENDER THREAD, from DrawOverlay in play mode (construction_plan.md): with no panels, the tools in a
    grid in the bottom right corner - the user's, 2026-10-08: a bar along the bottom had grown too full -
    each with its key, the one in hand lit, and a click on one takes it up. Over the grid, while anything
    is being built, how far along it is; at the bottom in the middle, why a placement was just refused.

    A row a group: a house and the ground round it; where goods are kept and got; the field, the builders
    and the camp; the lines; the winch and the defences - the range, the tower; the workshops that make one
    good of another - the ropewalk (goods_plan.md), with room for those to come - and ERASE last, on a row of
    its own. A cell with no name is left empty. Returns the top of the grid, or of the BUILDING line
    over it: what stacks over them (DrawIssues) goes above that.
*/
namespace {
struct BarTool{ const char* key; const char* name; int tool; };
const BarTool chasm_bar[] = {
    {"1","HOUSE",CHASM_TOOL_HOUSE},{"4","GARDEN",CHASM_TOOL_GARDEN},{"5","LOT",CHASM_TOOL_LOT},
    {"8","STORE",CHASM_TOOL_STORE},{"0","WATER",CHASM_TOOL_WATER},{"9","WOODCUTTER",CHASM_TOOL_WOODCUTTER},
    {"2","FIELD",CHASM_TOOL_FIELD},{"G","BUILDERS",CHASM_TOOL_BUILDERS},{"C","CAMP",CHASM_TOOL_CAMP},
    {"6","ROAD",CHASM_TOOL_ROAD},{"7","WALL",CHASM_TOOL_WALL},{"J","BRIDGE",CHASM_TOOL_BRIDGE},
    {"H","WINCH",CHASM_TOOL_WINCH},{"R","RANGE",CHASM_TOOL_RANGE},{"T","TOWER",CHASM_TOOL_TOWER},
    {"K","ROPEWALK",CHASM_TOOL_ROPEWALK},{"M","STONECUTTER",CHASM_TOOL_STONECUTTER},{"O","MINE",CHASM_TOOL_MINE},
    {"3","ERASE",CHASM_TOOL_ERASE}};

/*
    THE TOOL'S TIP (the user's, 2026-10-18; drag_plan.md): what it is, and how it is placed - a drag that goes on with a
    new building past each one's size, or a click for the one-plot kinds - in plain words, its sizes and storeys read
    from the rules. Its price and its controls go under it (DrawToolTip).
*/
void ToolTipText(int tool, int kind, std::string& what, std::string& how, const char*& controls){
    char buf[192];
    int plots = ZoneKindMaxPlots(kind);
    int storeys = ZoneKindMaxStoreys(kind);
    controls = "Let go to build what's green - Right-click cancels - Shift+drag removes";
    switch (tool){
        case CHASM_TOOL_HOUSE:
            what = "Home for one family. It keeps its own food, water and firewood; the bigger, the more.";
            snprintf(buf,sizeof(buf),"Drag to lay a row of houses: a new one starts every %i plots. "
                     "Click a house for a storey more, up to %i.",plots,storeys);
            break;
        case CHASM_TOOL_GARDEN:
            what = "A walled garden for the house it touches.";
            snprintf(buf,sizeof(buf),"Drag over the plots it should cover. Select a house first to make it that house's.");
            break;
        case CHASM_TOOL_LOT:
            what = "A fenced yard for a house or a woodcutter's hut. A woodcutter stacks his woodpile on it.";
            snprintf(buf,sizeof(buf),"Drag over the plots it should cover. Select the hut first to make it his.");
            break;
        case CHASM_TOOL_STORE:
            what = "Holds goods for the whole colony. Next to a workplace it takes only that workplace's goods.";
            snprintf(buf,sizeof(buf),"Drag for a store of up to %i plots; past that a new one starts. "
                     "Click it for a storey more, up to %i.",plots,storeys);
            break;
        case CHASM_TOOL_WATER:
            what = "A collector at the water's edge. It fills while it has a carrier, who takes the water to a store.";
            snprintf(buf,sizeof(buf),"Only on the last dry plot of a bank. Drag along the bank for up to %i plots; "
                     "the longer, the more it holds.",plots);
            break;
        case CHASM_TOOL_WOODCUTTER:
            what = "The only one who fells trees, both for wood and to clear roads and building sites.";
            snprintf(buf,sizeof(buf),"Drag for a hut of up to %i plots; more floor keeps more wood indoors. "
                     "Click it for a storey more, up to %i.",plots,storeys);
            break;
        case CHASM_TOOL_FIELD:
            what = "Farmed by the family whose land it is: sown, grown through the warm seasons, harvested home.";
            snprintf(buf,sizeof(buf),"Drag over the large squares: up to %i to a field, then a new one starts. "
                     "Buildings keep a plot away from it.",ZoneKindMaxPlots(ZONE_KIND_FIELD));
            break;
        case CHASM_TOOL_BUILDERS:
            what = "A builder for every plot and storey. They build, make provisions and keep the towers stocked.";
            snprintf(buf,sizeof(buf),"Drag for a hut of up to %i plots. Click it for a storey more, up to %i.",plots,storeys);
            break;
        case CHASM_TOOL_CAMP:
            what = "Tents for anyone without a house, and the settlers' supplies. One camp per settlement.";
            snprintf(buf,sizeof(buf),"Drag to pitch a tent on each plot. Outside every settlement it founds a new one.");
            break;
        case CHASM_TOOL_ROAD:
            what = "People walk twice as fast on it. It costs only work: its trees are felled, then people without a job lay it.";
            snprintf(buf,sizeof(buf),"Drag along the route; let go to lay what's green.");
            controls = "Let go to build what's green - Right-click cancels - Shift+drag takes it up";
            break;
        case CHASM_TOOL_WALL:
            what = "A palisade that stops walkers once it stands. Where a road crosses it, there's a gate.";
            snprintf(buf,sizeof(buf),"Drag it like a road. Over a road it makes a gate whose door can be shut.");
            controls = "Let go to build what's green - Right-click cancels - Shift+drag takes it up";
            break;
        case CHASM_TOOL_BRIDGE:
            what = "Crosses a river from bank to bank, never a lake or the chasm. It's placed whole.";
            snprintf(buf,sizeof(buf),"Press on a dry bank and let go on the other; it shows green when it can span the river.");
            controls = "Let go on the far bank to build - Right-click puts the tool down";
            break;
        case CHASM_TOOL_WINCH:
            what = "Takes people down to the balcony below and back up.";
            snprintf(buf,sizeof(buf),"Not dragged: click to place one, on the rim right above a balcony. "
                     "The preview shows where it lands.");
            controls = "Click to place - Shift+click removes - Right-click puts the tool down";
            break;
        case CHASM_TOOL_RANGE:
            what = "Archers train here all year, one trainee for each cell.";
            snprintf(buf,sizeof(buf),"Drag over the large squares: up to %i to a range, then a new one starts.",
                     ZoneKindMaxPlots(ZONE_KIND_RANGE));
            break;
        case CHASM_TOOL_TOWER:
            what = "A guard tower, both storeys built at once. Its water and provisions are its guards' pantry.";
            snprintf(buf,sizeof(buf),"Not dragged: click to place one. On a wall it takes the wall's place.");
            controls = "Click to place - Shift+click removes - Right-click puts the tool down";
            break;
        case CHASM_TOOL_ROPEWALK:
            snprintf(buf,sizeof(buf),"Makes rope from hemp. It works from %i plots, and a 3rd and 4th make it faster.",
                     ECONOMY_ROPEWALK_MIN_PLOTS);
            what = buf;
            snprintf(buf,sizeof(buf),"Drag for a ropewalk of up to %i plots; past that, a new one starts.",plots);
            break;
        case CHASM_TOOL_STONECUTTER:
            what = "Breaks the rocks around his hut for stone, starting with any boulder on a building site.";
            snprintf(buf,sizeof(buf),"Drag for a hut of up to %i plots.",plots);
            break;
        case CHASM_TOOL_MINE:
            what = "Digs floatstone or stone from a deposit: a seam's foot, the mountain's foot or an outcrop.";
            snprintf(buf,sizeof(buf),"Not dragged: click to place one, on a deposit - it shows green only there.");
            controls = "Click to place - Shift+click removes - Right-click puts the tool down";
            break;
        case CHASM_TOOL_WELL:
            what = "A debug well: water anyone may take, as much as the seed gives it. set_stock can make it endless.";
            snprintf(buf,sizeof(buf),"Not dragged: click to place one wherever a house could stand. It stands at once.");
            controls = "Click to place - Shift+click removes - Right-click puts the tool down";
            break;
        case CHASM_TOOL_RIVER:
            what = "A debug river: it runs off the map's edge, joins a river, falls over a rim or sinks, by where it ends.";
            snprintf(buf,sizeof(buf),"Drag from its source along its course. Let go and the map is made again with it.");
            controls = "Let go to place - the debug panel lists the water, to take it away";
            break;
        case CHASM_TOOL_HEIGHT:
            what = "Debug height: the ground raised, or lowered below 0, smoothly out to the radius. Strokes add up.";
            snprintf(buf,sizeof(buf),"Click for a hill, drag for a ridge. Let go and the map is made again with it.");
            controls = "Let go to paint - the debug panel lists the strokes, to undo them";
            break;
        case CHASM_TOOL_CLIFF:
            what = "Debug cliffs: plots set down in steps below the plateau, walls round them. Only a winch crosses one.";
            snprintf(buf,sizeof(buf),"Drag as a road, at the panel's width and depth. Let go and the map is made again with it.");
            controls = "Let go to paint - the debug panel lists the strokes, to undo them";
            break;
        case CHASM_TOOL_POND:
            what = "A debug pond: still water pressed into the plateau, clear of rims, cracks, the mountain and the edge.";
            snprintf(buf,sizeof(buf),"Drag out from its middle to its water's edge. Let go and the map is made again with it.");
            controls = "Let go to place - the debug panel lists the water, to take it away";
            break;
        case CHASM_TOOL_ERASE:
            what = "Removes a building's plot, a yard, a road or a field's cell. An erased site leaves what was brought as a crate.";
            snprintf(buf,sizeof(buf),"Drag over what should go and it turns red; let go to erase it.");
            controls = "Let go to erase what's red - Right-click cancels";
            break;
        default:
            what.clear();
            buf[0] = 0;
            controls = "";
            break;
    }
    how = buf;
}
}

float ApplicationChasm::DrawBuildBar(float s){
    const BarTool* bar = chasm_bar;
    const size_t bar_count = sizeof(chasm_bar) / sizeof(chasm_bar[0]);
    /*
        Only what has been DISCOVERED (demo_plan.md step 6): a tool still waiting for its discovery is left out, the
        rest closing up in the same order. One a discovery has just brought is marked NEW until it is first taken up;
        what a colony has on entering it is not new (discoveries_epoch).
    */
    uint32_t locked = ToolsLocked();
    uint32_t epoch = discoveries_epoch.load();
    if (epoch != tools_known_epoch){
        tools_known_epoch = epoch;
        tools_known = ~locked;
        tools_new = 0;
    }
    tools_new |= ~locked & ~tools_known;
    tools_known = ~locked;
    std::vector<BarTool> shown;
    for (size_t i = 0; i < bar_count; i++){
        if (!((locked >> bar[i].tool) & 1u)){
            shown.push_back(bar[i]);
        }
    }
    const int count = (int)shown.size();
    const int columns = 3;
    const int rows = (count + columns - 1) / columns;
    const float size = 15.0f * s;
    const float key_size = 12.0f * s;
    const float key_gap = 6.0f * s;         //from a key to its name
    const float inner = 9.0f * s;           //a cell's text from its sides
    const float cell_h = 28.0f * s;
    const float gap = 4.0f * s;             //between cells
    const float pad = 8.0f * s;             //the panel round them
    //Every cell as wide as the widest, so the columns line up.
    float cell_w = 0.0f;
    for (int i = 0; i < count; i++){
        float w = overlay->MeasureText(shown[i].key,key_size).x + key_gap + overlay->MeasureText(shown[i].name,size).x;
        cell_w = std::max(cell_w,w + inner * 2.0f);
    }
    float cx = (float)main_window->width * 0.5f;
    float bottom = (float)main_window->height - 14.0f * s;
    vec2 q1((float)main_window->width - 16.0f * s,bottom);
    vec2 q0(q1.x - pad * 2.0f - cell_w * columns - gap * (columns - 1),bottom - pad * 2.0f - cell_h * rows - gap * (rows - 1));
    overlay->AddRect(q0,q1,10.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{q0,q1,OVERLAY_HIT_BLOCK,0});
    build_bar_min = q0;
    build_bar_max = q1;
    int tool = paint_tool.load();
    if (tool >= 0 && tool < 32){
        tools_new &= ~(1u << tool);
    }
    //The tool under the mouse, for its price tip beside the grid (DrawCostTips).
    float mx = (float)overlay_mouse_x.load();
    float my = (float)overlay_mouse_y.load();
    bar_tip_tool = -1;
    for (int i = 0; i < count; i++){
        vec2 c0(q0.x + pad + (i % columns) * (cell_w + gap),q0.y + pad + (i / columns) * (cell_h + gap));
        vec2 c1(c0.x + cell_w,c0.y + cell_h);
        if (mx >= c0.x && mx <= c1.x && my >= c0.y && my <= c1.y){
            bar_tip_tool = shown[i].tool;
            bar_tip_a = c0;
            bar_tip_b = c1;
        }
        bool f_on = (tool == shown[i].tool);
        bool f_new = (tools_new >> shown[i].tool) & 1u;
        overlay->AddRect(c0,c1,6.0f * s,f_on ? UIColor(236,202,92,70) : f_new ? HudFaded(HUD_AMBER,40) : UIColor(255,255,255,14));
        float base = c0.y + 19.0f * s;
        overlay->AddText(shown[i].key,vec2(c0.x + inner,base - 1.0f * s),key_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        float key_w = overlay->MeasureText(shown[i].key,key_size).x;
        overlay->AddText(shown[i].name,vec2(c0.x + inner + key_w + key_gap,base),size,
                         (f_on || f_new) ? HUD_TEXT : Faded(HUD_TEXT,170),UI_ALIGN_LEFT);
        if (f_new){
            //NEW: a dot in the corner, in the notice's amber.
            const float dot = 3.0f * s;
            vec2 at(c1.x - 6.0f * s,c0.y + 6.0f * s);
            overlay->AddRect(vec2(at.x - dot,at.y - dot),vec2(at.x + dot,at.y + dot),dot,HUD_AMBER);
        }
        //A click on a tool takes it up, as its key does (selection_plan.md).
        overlay_hits_drawing.push_back(OverlayHit{c0,c1,OVERLAY_HIT_TOOL,shown[i].tool});
    }
    /*
        Why the last placement was refused, for a moment at the bottom in the middle - play mode has no panel
        to say it (under the clouds, too close to water, too wide to bridge) - or the last order (no way there:
        defence_plan.md), whichever came last. In the same place, in amber, an ARCH taken down by the player's last
        command (arches_plan.md step 4) - floors paid for, and maybe a house's places, gone without a word otherwise -
        and the rules' NOTICES (below).
    */
    {
        auto now = std::chrono::steady_clock::now();
        uint32_t refusals = zone_refusals.load();
        uint32_t orders = order_refusals.load();
        uint32_t notices = arch_notices.load();
        uint32_t drags = drag_refusals.load();     //a drag's release leaving out what the rules refuse (drag_plan.md)
        /*
            A GROUP's orders (selection_plan.md, "Several people selected"): their refusals held back until all of them
            are done, then one line for the lot - nothing if all were sent. Given up after a few seconds unanswered.
        */
        uint32_t group = group_orders.load();
        if (group){
            bool f_done = order_commands_done.load() >= group_orders_done_at.load();
            bool f_stale = now.time_since_epoch().count() - group_orders_since.load()
                           > std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(10)).count();
            uint32_t refused = orders - group_orders_refused_at.load();
            if ((f_done || f_stale) && refused > 0){
                char sent[48];
                if (refused >= group){
                    snprintf(sent,sizeof(sent),"none of %u sent",group);
                }else{
                    snprintf(sent,sizeof(sent),"%u of %u sent",group - refused,group);
                }
                std::lock_guard<std::mutex> lock(grid_mutex);
                refusal_shown = sent + (order_last_refusal.empty() ? std::string() : " - " + order_last_refusal);
                refusal_since = now;
                f_refusal_notice = false;
                refusal_seconds = 3.0f;
            }
            if (f_done || f_stale){
                group_orders = 0;
            }
            order_refusal_seen = orders;
        }
        if (notices != arch_notice_seen){
            arch_notice_seen = notices;
            refusal_seen = refusals;
            order_refusal_seen = orders;
            drag_refusal_seen = drags;
            refusal_since = now;
            refusal_shown = "the arch over the road came down with it";
            f_refusal_notice = true;
            refusal_seconds = 3.0f;
        }else if (refusals != refusal_seen || orders != order_refusal_seen || drags != drag_refusal_seen){
            bool f_order = orders != order_refusal_seen;
            bool f_drag = !f_order && drags != drag_refusal_seen;
            refusal_seen = refusals;
            order_refusal_seen = orders;
            drag_refusal_seen = drags;
            refusal_since = now;
            std::lock_guard<std::mutex> lock(grid_mutex);
            const std::string& why = f_order ? order_last_refusal : f_drag ? drag_last_refusal : zone_last_refusal;
            refusal_shown = why.empty() ? std::string() : (f_order ? "CAN'T SEND: " : "CAN'T BUILD: ") + why;
            f_refusal_notice = false;
            refusal_seconds = 3.0f;
        }
        /*
            The rules' NOTICES (Economy.h's events; defence_plan.md, "Scouting"): a scout with nothing to take, an ordered
            man's pack run out, a floatstone seam seen (stone_plan.md) - the newest since the last look, in amber and for as
            long as a death, since it comes from play rather than a click. Only one of the last half day: a load brings none
            along, but a replay's start can. The other events in the ring are not told here.
        */
        std::shared_ptr<const EconomyState> ne = GetEconomy();
        if (ne && (ne->world.get() != notices_world || ne->events_made < notices_seen)){
            notices_world = ne->world.get();
            notices_seen = ne->events_made;
        }
        /*
            And a DISCOVERY (demo_plan.md step 6) says what it brings - "NEW TOOLS: WATER, BRIDGE" - when this event is
            the one that made it: the discoveries are published on the tick, before the economy that carries the event.
            The first builders' hut and the first winch finished are told for that alone.
        */
        std::shared_ptr<const ChasmDiscoveries> found = GetDiscoveries();
        auto discovery_of = [&](const EconomyEvent& ev){
            int d = -1;
            if (ev.kind == EVENT_DISCOVERED){
                const AreaData* areas = ne->world ? ne->world->areas.get() : nullptr;
                int kind = (areas && ev.amount >= 0 && ev.amount < (int)areas->areas.size()) ? areas->areas[(size_t)ev.amount].kind : -1;
                d = (kind == AREA_RIVER || kind == AREA_WATERFALL) ? DISCOVERY_RIVER : (kind == AREA_CHASM) ? DISCOVERY_CHASM : -1;
            }else if (ev.kind == EVENT_SEAM_SEEN){
                d = DISCOVERY_FLOATSTONE;
            }else if (ev.kind == EVENT_BOULDER_FOUND){
                d = DISCOVERY_BOULDER;
            }else if (ev.kind == EVENT_BALCONY_REACHED){
                d = DISCOVERY_BALCONY;
            }else if (ev.kind == EVENT_BUILDING_FINISHED){
                d = (ev.amount == ZONE_KIND_BUILDERS) ? DISCOVERY_BUILDERS : (ev.amount == ZONE_KIND_WINCH) ? DISCOVERY_WINCH : -1;
            }
            return (d >= 0 && found && found->tick[d] == (int64_t)ev.tick && found->person[d] == ev.person) ? d : -1;
        };
        const EconomyEvent* newest = NULL;
        int frosts = 0;     //the frost's on the newest's tick: one line for every field it took
        if (ne && ne->events_made > notices_seen){
            size_t fresh = std::min((size_t)(ne->events_made - notices_seen),ne->events.size());
            for (size_t i = ne->events.size() - fresh; i < ne->events.size(); i++){
                int kind = ne->events[i].kind;
                //An area found is told only when it is the first of its kind (areas_plan.md, the user's): the first
                //waterfall, not the second.
                bool f_notice = kind == EVENT_NOTHING_TO_TAKE || kind == EVENT_RAN_OUT || kind == EVENT_NO_TENT
                                || kind == EVENT_SEAM_SEEN || (kind == EVENT_DISCOVERED && (ne->events[i].what & 1))
                                || kind == EVENT_FROST_TOOK || kind == EVENT_GREW_UP || kind == EVENT_GREW_OLD || kind == EVENT_EXPECTING
                                || kind == EVENT_BOULDER_FOUND || kind == EVENT_BALCONY_REACHED
                                || kind == EVENT_WALL_BROKEN || kind == EVENT_GATE_BROKEN || kind == EVENT_BREACHED
                                || kind == EVENT_RANKED
                                || (kind == EVENT_BUILDING_FINISHED && discovery_of(ne->events[i]) >= 0)
                                || ((kind == EVENT_GOAL_MET || kind == EVENT_GOAL_FAILED) && TriggerOn(ne->events[i].amount));
                newest = f_notice ? &ne->events[i] : newest;
                frosts += (kind == EVENT_FROST_TOOK) ? 1 : 0;
            }
            notices_seen = ne->events_made;
        }
        if (newest){
            const EconomyEvent& n = *newest;
            std::string name = "someone";
            for (const EconomyWorker& k : ne->workers){
                name = (k.id == n.person) ? EconomyPersonName(k) : name;
            }
            for (const EconomyDeath& d : ne->dead){
                name = (d.id == n.person) ? EconomyDeathName(d) : name;
            }
            bool f_water = (n.what & GOOD_BIT(GOOD_WATER)) != 0;
            bool f_food = (n.what & GOODS_EATEN) != 0;
            const AreaData* areas = ne->world ? ne->world->areas.get() : nullptr;
            int area_kind = (areas && n.amount >= 0 && n.amount < (int)areas->areas.size()) ? areas->areas[(size_t)n.amount].kind : -1;
            std::string told = (n.kind == EVENT_DISCOVERED) ? name + " has found " + AreaKindTold(area_kind)
                             : (n.kind == EVENT_NOTHING_TO_TAKE) ? "no food or water for " + name + " to take"
                             : (n.kind == EVENT_NO_TENT) ? "no tent to spare for " + name + " to take"
                             : (n.kind == EVENT_SEAM_SEEN) ? name + " has seen floatstone in a balcony's wall"
                             : (n.kind == EVENT_GREW_UP) ? name + " has grown up"
                             : (n.kind == EVENT_GREW_OLD) ? name + " has grown old, and works no more"
                             : (n.kind == EVENT_BOULDER_FOUND) ? name + " has found a boulder"
                             : (n.kind == EVENT_BALCONY_REACHED) ? name + " has reached a balcony"
                             //WALLS BREAK (zombies_plan.md step 4): what the zombies broke through.
                             : (n.kind == EVENT_WALL_BROKEN) ? std::string("the zombies have broken the wall")
                             : (n.kind == EVENT_GATE_BROKEN) ? std::string("the zombies have broken a gate")
                             : (n.kind == EVENT_BREACHED) ? std::string("the zombies have broken into a ") + ChasmKindLabel(n.amount,false)
                             //RANKS (ranks_plan.md): risen by a kill.
                             : (n.kind == EVENT_RANKED) ? name + " has risen to " + EconomyRankName(n.amount)
                             : (n.kind == EVENT_BUILDING_FINISHED) ? std::string(n.amount == ZONE_KIND_WINCH ? "the first winch"
                                                                                                         : "the builders' hut")
                                                                     + " is finished"
                             : name + " has run out of " + ((f_water && f_food) ? "food and water" : f_water ? "water" : "food");
            //The demo's goals (demo_plan.md step 7): met or failed, by name.
            if (n.kind == EVENT_GOAL_MET || n.kind == EVENT_GOAL_FAILED){
                told = std::string(n.kind == EVENT_GOAL_MET ? "goal met: " : "goal failed: ") + ChasmGoalTitle(n.amount);
            }
            //The frost (fields_plan.md): one line for all it took at the turn of winter.
            if (n.kind == EVENT_FROST_TOOK){
                told = (frosts > 1) ? "the frost took the crops on " + std::to_string(frosts) + " fields"
                                    : std::string("the frost took a field of ") + ZoneCropName(n.amount);
            }
            //Expecting (lifeline_plan.md): the couple, by one surname when they share it.
            if (n.kind == EVENT_EXPECTING){
                std::string partner;
                for (const EconomyWorker& k : ne->workers){
                    if (k.id == n.person && k.partner){
                        for (const EconomyWorker& p : ne->workers){
                            partner = (p.id == k.partner) ? EconomyPersonName(p) : partner;
                        }
                    }
                }
                size_t a = name.find(' ');
                size_t b = partner.find(' ');
                bool f_same = a != std::string::npos && b != std::string::npos && name.substr(a) == partner.substr(b);
                told = partner.empty() ? name + " is expecting a child"
                     : f_same ? name.substr(0,a) + " and " + partner + " are expecting a child"
                     : name + " and " + partner + " are expecting a child";
            }
            int d = discovery_of(n);
            std::string brings = (d >= 0) ? ChasmDiscoveryTools(d) : std::string();
            if (!brings.empty()){
                told += " - new tools: " + brings;
            }
            if (ne->tick < n.tick + CALENDAR_DAY_TICKS / 2){
                refusal_shown = told;
                refusal_since = now;
                f_refusal_notice = true;
                refusal_seconds = brings.empty() ? 5.0f : 8.0f;     //new tools: long enough to find them on the bar
            }
        }
        float age = std::chrono::duration<float>(now - refusal_since).count();
        if (age < refusal_seconds && !refusal_shown.empty()){
            std::string text = refusal_shown;
            std::transform(text.begin(),text.end(),text.begin(),::toupper);
            vec2 m = overlay->MeasureText(text.c_str(),size);
            uint8_t alpha = (uint8_t)(255.0f * std::min(1.0f,(refusal_seconds - age) * 2.0f));
            const float side = 14.0f * s;
            vec2 r0(cx - m.x * 0.5f - side,bottom - 30.0f * s);
            vec2 r1(cx + m.x * 0.5f + side,bottom);
            overlay->AddRect(r0,r1,9.0f * s,HudFaded(HUD_PANEL,(uint8_t)(alpha * 0.7f)));
            uint32_t colour = f_refusal_notice ? HUD_AMBER : UIColor(236,140,110);
            overlay->AddText(text.c_str(),vec2(cx,r0.y + 21.0f * s),size,HudFaded(colour,alpha),UI_ALIGN_CENTER);
        }
    }
    /*
        A DEATH says so (needs_plan.md, step 5), as a refusal does but in red and for longer, on the line above it -
        "BENTE VEEN DIED OF THIRST", and how many more since the last one told. Only a death of the last half day:
        a load or a new map brings its dead along, and they are old news. And a LOST colony - nobody left alive,
        somebody dead - says so above that, for as long as it is so.
    */
    {
        const float death_seconds = 5.0f;       //on screen this long, where a refusal has 3
        auto now = std::chrono::steady_clock::now();
        std::shared_ptr<const EconomyState> de = GetEconomy();
        size_t n = de ? de->dead.size() : 0;
        if (!de || de->world.get() != deaths_world || n < deaths_seen){
            deaths_world = de ? de->world.get() : nullptr;
            deaths_seen = n;
        }
        if (n > deaths_seen){
            const EconomyDeath& d = de->dead.back();
            if (de->tick < d.tick + CALENDAR_DAY_TICKS / 2){
                const char* of = EconomyDeathCause(d.cause);
                int more = (int)(n - deaths_seen) - 1;
                //Bitten (zombies_plan.md, step 2): killed, not died of.
                //...and his rank and kills (ranks_plan.md): "Jelle De Wit, Expert, 2 kills, was killed by a zombie".
                std::string fame = EconomyKillsTold(d.kills);
                death_shown = EconomyDeathName(d) + fame + (fame.empty() ? "" : ",")
                              + ((d.cause == NEEDS_DIED_ZOMBIE) ? std::string(" was killed by a zombie") : " died of " + std::string(of))
                              + (more > 0 ? " (and " + std::to_string(more) + " more)" : std::string());
                std::transform(death_shown.begin(),death_shown.end(),death_shown.begin(),::toupper);
                death_since = now;
            }
            deaths_seen = n;
        }
        auto line = [&](const std::string& text, float y, uint8_t alpha){
            vec2 m = overlay->MeasureText(text.c_str(),size);
            const float side = 14.0f * s;
            vec2 r0(cx - m.x * 0.5f - side,y - 30.0f * s);
            vec2 r1(cx + m.x * 0.5f + side,y);
            overlay->AddRect(r0,r1,9.0f * s,HudFaded(HUD_PANEL,(uint8_t)(alpha * 0.8f)));
            overlay->AddText(text.c_str(),vec2(cx,r0.y + 21.0f * s),size,HudFaded(HUD_DANGER,alpha),UI_ALIGN_CENTER);
        };
        float age = std::chrono::duration<float>(now - death_since).count();
        if (age < death_seconds && !death_shown.empty()){
            line(death_shown,bottom - 36.0f * s,(uint8_t)(255.0f * std::min(1.0f,(death_seconds - age) * 2.0f)));
        }
        if (de && de->workers.empty() && !de->dead.empty() && TriggerOn(CHASM_TRIGGER_LOST)){
            line("THE COLONY IS LOST - NOBODY IS LEFT",bottom - 72.0f * s,255);
        }
    }
    //What is being built: the sites, and what they have had of what they need, by good (defence_plan.md step 4).
    float top = q0.y;
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (e && z && e->world == z->world){
        std::vector<std::array<int,GOOD_COUNT>> cost = SiteCosts(*z);
        int sites = 0;
        std::array<int,GOOD_COUNT> needed{};
        std::array<int,GOOD_COUNT> brought{};
        for (size_t id = 1; id < cost.size(); id++){
            if (std::any_of(cost[id].begin(),cost[id].end(),[](int x){ return x > 0; })){
                sites++;
                for (int g = 0; g < GOOD_COUNT; g++){
                    needed[g] += cost[id][g];
                    brought[g] += std::min(cost[id][g],EconomySiteBrought(*e,(uint32_t)id,g));
                }
            }
        }
        //Gardens and lots still to build count as a site a plot (Economy.h, CONSTRUCTION); a wall still to
        //build as one a line, its wood plot by plot (line_works_plan.md, step 3).
        std::vector<uint32_t> wall_lines;
        for (size_t v = 0; v < z->ground.size(); v++){
            bool f_wall = ZoneWallPlanned(*z,(int)v);
            if ((ZoneEnclosesGround(z->ground[v]) || f_wall) && !z->ground_built[v] && z->storeys[v] == 0){
                int need = EconomyGroundCost(*z->world,*z,(int)v);
                if (!f_wall || !z->line[v]){
                    sites++;
                }else if (std::find(wall_lines.begin(),wall_lines.end(),z->line[v]) == wall_lines.end()){
                    wall_lines.push_back(z->line[v]);
                    sites++;
                }
                needed[GOOD_WOOD] += need;
                brought[GOOD_WOOD] += std::min(need,EconomyGroundBrought(*e,(int)v));
            }
        }
        if (sites > 0){
            //Over the grid, at its right edge: wood always, any other good while some site wants it.
            char buf2[160];
            int at = snprintf(buf2,sizeof(buf2),"BUILDING %i  -  WOOD %i/%i",sites,brought[GOOD_WOOD],needed[GOOD_WOOD]);
            for (int g = 0; g < GOOD_COUNT; g++){
                if (g != GOOD_WOOD && needed[g] > 0 && at > 0 && at < (int)sizeof(buf2)){
                    std::string name = GoodName(g);
                    std::transform(name.begin(),name.end(),name.begin(),::toupper);
                    at += snprintf(buf2 + at,sizeof(buf2) - at,", %s %i/%i",name.c_str(),brought[g],needed[g]);
                }
            }
            vec2 m = overlay->MeasureText(buf2,size);
            const float side = 14.0f * s;
            vec2 r1(q1.x,q0.y - 6.0f * s);
            vec2 r0(r1.x - m.x - side * 2.0f,r1.y - 30.0f * s);
            overlay->AddRect(r0,r1,9.0f * s,HUD_PANEL);
            overlay->AddText(buf2,vec2((r0.x + r1.x) * 0.5f,r0.y + 21.0f * s),size,UIColor(206,160,110),UI_ALIGN_CENTER);
            top = r0.y;
        }
    }
    return top;
}

/*
    RENDER THREAD, from DrawCostTips: THE TOOL'S TIP, at the bottom (the user's, 2026-10-18) - left of the tools' grid,
    level with its foot - for the tool the mouse is on there: its name and key, what it is and how it is placed
    (ToolTipText), about what it costs (ToolCostTip), and its controls.
*/
void ApplicationChasm::DrawToolTip(float s){
    int tool = bar_tip_tool;
    const BarTool* b = NULL;
    for (const BarTool& t : chasm_bar){
        b = (t.tool == tool) ? &t : b;
    }
    if (!b){
        return;
    }
    std::string what, how;
    const char* controls = "";
    ToolTipText(tool,ToolKind(tool),what,how,controls);
    std::string price = ToolCostTip(tool);
    struct Line{
        std::string text;
        float size;
        uint32_t colour;
        float after;        //the gap under it
    };
    const float pad = 14.0f * s;
    //Wrapped at a reading width, and within what is left of the screen beside the grid - clear of the panels in the
    //bottom left at the game's size.
    float most = std::min(620.0f * s,build_bar_min.x - 10.0f * s - 16.0f * s - pad * 2.0f);
    std::vector<Line> lines;
    auto put = [&](const std::string& text, float size, uint32_t colour, float after){
        std::string row;
        size_t at = 0;
        while (at <= text.size()){
            size_t end = text.find(' ',at);
            std::string word = text.substr(at,(end == std::string::npos) ? std::string::npos : end - at);
            std::string longer = row.empty() ? word : row + " " + word;
            if (!row.empty() && overlay->MeasureText(longer.c_str(),size).x > most){
                lines.push_back(Line{row,size,colour,3.0f * s});
                row = word;
            }else{
                row = longer;
            }
            if (end == std::string::npos){
                break;
            }
            at = end + 1;
        }
        lines.push_back(Line{row,size,colour,after});
    };
    put(b->name,16.0f * s,HUD_TEXT,8.0f * s);
    put(what,14.0f * s,HUD_TEXT,5.0f * s);
    put(how,14.0f * s,HUD_TEXT,10.0f * s);
    if (!price.empty()){
        put(price,13.0f * s,HUD_AMBER,6.0f * s);
    }
    put(controls,12.0f * s,HUD_TEXT_DIM,0.0f);
    float w = 0.0f;
    float h = pad * 2.0f;
    for (const Line& l : lines){
        w = std::max(w,overlay->MeasureText(l.text.c_str(),l.size).x);
        h += l.size + l.after;
    }
    w += pad * 2.0f;
    vec2 p1(build_bar_min.x - 10.0f * s,build_bar_max.y);
    vec2 p0(std::max(16.0f * s,p1.x - w),p1.y - h);
    overlay->AddRect(p0,p1,10.0f * s,HudFaded(HUD_PANEL,235));
    float y = p0.y + pad;
    for (size_t i = 0; i < lines.size(); i++){
        const Line& l = lines[i];
        y += l.size;
        overlay->AddText(l.text.c_str(),vec2(p0.x + pad,y),l.size,l.colour,UI_ALIGN_LEFT);
        //The key beside the name, as the grid shows it.
        if (i == 0){
            float nx = overlay->MeasureText(l.text.c_str(),l.size).x;
            overlay->AddText(b->key,vec2(p0.x + pad + nx + 8.0f * s,y),12.0f * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        }
        y += l.after;
    }
}

/*
    RENDER THREAD, from DrawOverlay in play mode (builders_plan.md): in the bottom left corner, how many work
    at each job against its places - amber where a place stands empty, which with too few people is the
    newest workplace's - and the jobless, who carry wood and build too, and any away at a post (defence_plan.md).
    Above the tools' grid only when the window is too narrow for the two side by side. Only what has places or
    people. Returns its top: what stacks over it (DrawNeediest) goes above that.
*/
float ApplicationChasm::DrawJobsOverview(float s){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    float corner = (float)main_window->height - 14.0f * s;
    if (!e || !z || e->world != z->world || e->workers.empty()){
        return corner;
    }
    EconomyJobCount counts[WORKER_JOB_COUNT];
    int jobless = 0;
    EconomyJobCounts(*e,*z,counts,&jobless);
    struct Row{ const char* name; int working; int places; bool f_places; };
    std::vector<Row> rows;
    static const int shown[] = {WORKER_JOB_BUILDER,WORKER_JOB_WOODCUTTER,WORKER_JOB_FARMER,WORKER_JOB_WATER,
                                WORKER_JOB_PICKER,WORKER_JOB_ARCHER,WORKER_JOB_GUARD,WORKER_JOB_ROPEMAKER,
                                WORKER_JOB_STONECUTTER,WORKER_JOB_MINER};
    static const char* names[] = {"BUILDERS","WOODCUTTERS","FARMERS","WATER CARRIERS","PICKERS","ARCHERS","GUARDS",
                                  "ROPEMAKERS","STONECUTTERS","MINERS"};
    static_assert(sizeof(shown) / sizeof(shown[0]) == sizeof(names) / sizeof(names[0]),"a name for every job shown");
    for (int i = 0; i < (int)(sizeof(shown) / sizeof(shown[0])); i++){
        const EconomyJobCount& c = counts[shown[i]];
        if (c.places > 0 || c.working > 0){
            rows.push_back(Row{names[i],c.working,c.places,true});
        }
    }
    rows.push_back(Row{"NO JOB",jobless,0,false});
    //Away at a post (defence_plan.md) - each still counted in his own row, whose place waits for him.
    int posted = 0;
    for (const EconomyWorker& k : e->workers){
        posted += (k.post >= 0) ? 1 : 0;
    }
    if (posted > 0){
        rows.push_back(Row{"POSTED",posted,0,false});
    }
    const float size = 14.0f * s;
    const float line_h = 20.0f * s;
    const float pad = 12.0f * s;
    float label_w = 0.0f;
    for (const Row& r : rows){
        label_w = std::max(label_w,overlay->MeasureText(r.name,size).x);
    }
    float w = pad * 2.0f + label_w + overlay->MeasureText("00 / 00",size).x + 14.0f * s;
    float h = pad * 0.6f + line_h * (float)rows.size() + pad * 0.4f;
    float x0 = 16.0f * s;
    float bottom = corner;
    if (x0 + w > build_bar_min.x - 10.0f * s){
        bottom = build_stack_top - 8.0f * s;     //over the grid and what stacks on it (DrawIssues)
    }
    vec2 p0(x0,bottom - h);
    vec2 p1(x0 + w,bottom);
    overlay->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    float y = p0.y + pad * 0.6f;
    for (const Row& r : rows){
        y += line_h;
        bool f_short = r.f_places && r.working < r.places;
        uint32_t colour = !r.f_places ? HUD_TEXT_DIM : f_short ? HUD_AMBER : HUD_TEXT;
        overlay->AddText(r.name,vec2(p0.x + pad,y - 5.0f * s),size,colour,UI_ALIGN_LEFT);
        char value[24];
        if (r.f_places){
            snprintf(value,sizeof(value),"%i / %i",r.working,r.places);
        }else{
            snprintf(value,sizeof(value),"%i",r.working);
        }
        overlay->AddText(value,vec2(p1.x - pad,y - 5.0f * s),size,colour,UI_ALIGN_RIGHT);
    }
    return p0.y;
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
    //Any factor, 1-10 (core batches the ticks), and what the machine is managing at it.
    float factor = physics_time_factor;
    if (ImGui::SliderFloat("speed##factor",&factor,1.0f,PHYSICS_TIME_FACTOR_MAX,"%.1fx")){
        SetSpeedFactor(factor);
    }
    float reached = GetPhysicsSpeed();
    bool f_short = !f_paused && physics_time_factor > 1.0f && reached < physics_time_factor * 0.9f;
    ImGui::TextColored(f_short ? ImVec4(1.0f,0.6f,0.35f,1.0f) : ImVec4(0.7f,0.7f,0.7f,1.0f),"running at %.2fx%s",reached,
                       f_short ? " - the simulation cannot keep up" : "");
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
        "past the mountain, 1 winter's furthest) and its front's mean z, the speed asked for and the "
        "speed reached (lower when the simulation cannot keep up), and whether paused. Optional: speed "
        "(any factor 1-10 - the play speeds are 1, 2 and 4 - the viewer's, not recorded); paused (true/false); "
        "set_day (days since the colony began, from 0: 0 is spring of year 1, 30 winter of year 1, 40 "
        "spring of year 2) with day_fraction 0-1 - a RECORDED command, applied before this returns; "
        "snow_cover (0-1, negative for off) - a view-only override of the snow for looking at winter "
        "on any date, never read by the simulation. The `sun` it returns is the one shining - elevation (degrees "
        "up), azimuth (degrees from north, clockwise), kelvin (its warmth), brightness, colour, whether held - and "
        "the date's (seasons_plan.md, \"The sun\"); `sun` as an argument holds it - any of elevation (20-85), "
        "azimuth, kelvin, brightness, the date's for the rest - or {held:false} lets it follow the date again, as "
        "the Sun panel does. A look only. The `weather` it returns (weather_plan.md) is the storm at the view (0-1), "
        "the view's distance south of the snow front's line (negative: under the snow), the wind there, the snow's "
        "thickness, the big gusts so far and whether the next may come; `weather` as an argument takes storm (0-1: "
        "a storm held over the view, the front's line through it), held (false lets it go), strength (the wind's, 1 as "
        "tuned) and gust (true: the big gust now, stinger and all) - as the Weather panel does. A look only. "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"speed",{{"type","number"}}},
                {"paused",{{"type","boolean"}}},
                {"set_day",{{"type","integer"}}},
                {"day_fraction",{{"type","number"}}},
                {"snow_cover",{{"type","number"}}},
                {"sun",{{"type","object"}}},
                {"weather",{{"type","object"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            if (args.contains("speed")){
                SetSpeedFactor(args["speed"].get<float>());
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
            //The sun (ApplicationChasmSeasons.cpp): held or let go, and turned at once rather than on the next pass.
            //And the weather (ApplicationChasmWeather.cpp), measured again at once.
            json sun;
            json weather;
            main_scene->AtTickBoundary([&](){
                if (args.contains("weather")){
                    SteerWeather(args["weather"]);
                }
                UpdateWeather();
                if (args.contains("sun")){
                    SteerSun(args["sun"]);
                }
                UpdateSun();
                sun = SunStatusJson();
                weather = WeatherStatusJson();
            });
            uint64_t tick = calendar_shown.load();
            CalendarDate d = CalendarDateOf(tick);
            json result{
                {"sun",sun},
                {"weather",weather},
                {"tick",tick},
                {"year",d.year},
                {"season",CalendarSeasonName(d.season)},
                {"day",d.day},
                {"day_fraction",d.day_fraction},
                {"days_since_start",tick / CALENDAR_DAY_TICKS},
                {"snow_cover",CalendarSnowCover(tick)},
                {"snow_cover_override",snow_cover_override.load()},
                {"snow_front_z",SnowFrontNow()},
                {"speed",physics_time_factor},
                {"speed_reached",GetPhysicsSpeed()},
                {"paused",main_scene->IsPhysicsPaused()},
                {"ran",f_ran}
            };
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
