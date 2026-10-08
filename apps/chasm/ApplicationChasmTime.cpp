#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
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

const float speeds[] = {1.0f,2.0f,4.0f};

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
      - TOP MIDDLE, the colony's stores.
    Both take the mouse where they are drawn (OverlayHit), in every mode: a press on them presses their
    buttons, and paints or selects nothing beneath.

    Sized off the window's height, so it is the same part of the picture at any size.
*/
void ApplicationChasm::DrawOverlay(void){
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    float s = std::max(0.5f,(float)main_window->height / 810.0f);
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    bool f_paused = main_scene && main_scene->IsPhysicsPaused();
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
        The colony's stores, at the top in the middle (docs/economy_plan.md): wood, food (the three crops
        summed) and water - what lies in stores, not what waits at a workplace or is being carried.
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
        //Each item's text, measured, so the bar is as wide as what it says - centred on the screen.
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
        float cx = (float)main_window->width * 0.5f;
        vec2 q0(cx - total * 0.5f,12.0f * s);
        vec2 q1(cx + total * 0.5f,q0.y + 30.0f * s);
        overlay->AddRect(q0,q1,9.0f * s,HUD_PANEL);
        overlay_hits_drawing.push_back(OverlayHit{q0,q1,OVERLAY_HIT_BLOCK,0});
        float x = q0.x + side;
        for (int i = 0; i < 4; i++){
            overlay->AddText(items[i],vec2(x,q0.y + 21.0f * s),size,colours[i],UI_ALIGN_LEFT);
            x += widths[i] + gap;
        }
    }

    //Play mode's own UI: the tools, the jobs and over them the neediest, and the selection's card and pins
    //(selection_plan.md).
    if (PlayMode()){
        DrawBuildBar(s);
        float jobs_top = DrawJobsOverview(s);
        DrawNeediest(s,jobs_top - 8.0f * s);
        DrawSelectionOverlay(s);
    }
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
    and the camp; the lines; the winch, and ERASE last.
*/
void ApplicationChasm::DrawBuildBar(float s){
    struct BarTool{ const char* key; const char* name; int tool; };
    static const BarTool bar[] = {
        {"1","HOUSE",CHASM_TOOL_HOUSE},{"4","GARDEN",CHASM_TOOL_GARDEN},{"5","LOT",CHASM_TOOL_LOT},
        {"8","STORE",CHASM_TOOL_STORE},{"0","WATER",CHASM_TOOL_WATER},{"9","WOODCUTTER",CHASM_TOOL_WOODCUTTER},
        {"2","FIELD",CHASM_TOOL_FIELD},{"G","BUILDERS",CHASM_TOOL_BUILDERS},{"C","CAMP",CHASM_TOOL_CAMP},
        {"6","ROAD",CHASM_TOOL_ROAD},{"7","WALL",CHASM_TOOL_WALL},{"J","BRIDGE",CHASM_TOOL_BRIDGE},
        {"H","WINCH",CHASM_TOOL_WINCH},{"3","ERASE",CHASM_TOOL_ERASE}};
    const int count = (int)(sizeof(bar) / sizeof(bar[0]));
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
        float w = overlay->MeasureText(bar[i].key,key_size).x + key_gap + overlay->MeasureText(bar[i].name,size).x;
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
    for (int i = 0; i < count; i++){
        vec2 c0(q0.x + pad + (i % columns) * (cell_w + gap),q0.y + pad + (i / columns) * (cell_h + gap));
        vec2 c1(c0.x + cell_w,c0.y + cell_h);
        bool f_on = (tool == bar[i].tool);
        overlay->AddRect(c0,c1,6.0f * s,f_on ? UIColor(236,202,92,70) : UIColor(255,255,255,14));
        float base = c0.y + 19.0f * s;
        overlay->AddText(bar[i].key,vec2(c0.x + inner,base - 1.0f * s),key_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        float key_w = overlay->MeasureText(bar[i].key,key_size).x;
        overlay->AddText(bar[i].name,vec2(c0.x + inner + key_w + key_gap,base),size,f_on ? HUD_TEXT : Faded(HUD_TEXT,170),
                         UI_ALIGN_LEFT);
        //A click on a tool takes it up, as its key does (selection_plan.md).
        overlay_hits_drawing.push_back(OverlayHit{c0,c1,OVERLAY_HIT_TOOL,bar[i].tool});
    }
    /*
        Why the last placement was refused, for a moment at the bottom in the middle - play mode has no panel
        to say it (under the clouds, too close to water, too wide to bridge).
    */
    {
        auto now = std::chrono::steady_clock::now();
        uint32_t refusals = zone_refusals.load();
        if (refusals != refusal_seen){
            refusal_seen = refusals;
            refusal_since = now;
        }
        std::string why;
        {
            std::lock_guard<std::mutex> lock(grid_mutex);
            why = zone_last_refusal;
        }
        float age = std::chrono::duration<float>(now - refusal_since).count();
        if (refusals > 0 && age < 3.0f && !why.empty()){
            std::string text = "CAN'T BUILD: " + why;
            std::transform(text.begin(),text.end(),text.begin(),::toupper);
            vec2 m = overlay->MeasureText(text.c_str(),size);
            uint8_t alpha = (uint8_t)(255.0f * std::min(1.0f,(3.0f - age) * 2.0f));
            const float side = 14.0f * s;
            vec2 r0(cx - m.x * 0.5f - side,bottom - 30.0f * s);
            vec2 r1(cx + m.x * 0.5f + side,bottom);
            overlay->AddRect(r0,r1,9.0f * s,HudFaded(HUD_PANEL,(uint8_t)(alpha * 0.7f)));
            overlay->AddText(text.c_str(),vec2(cx,r0.y + 21.0f * s),size,HudFaded(UIColor(236,140,110),alpha),UI_ALIGN_CENTER);
        }
    }
    //What is being built: the sites, and the wood they have had of what they need.
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (e && z && e->world == z->world){
        //One pass over the plots, not one a building: this is every frame.
        std::vector<int> wood(z->buildings.size(),0);
        for (size_t v = 0; v < z->building.size(); v++){
            if (z->storeys[v] > z->standing[v]){
                wood[z->building[v]] += (z->storeys[v] - z->standing[v]) * EconomyStoreyWood(*z->world,*z,(int)v);
            }
        }
        //A field still staked out (fields_plan.md): a site by its id, so much a cell.
        for (size_t c = 0; c < z->field.size(); c++){
            if (z->field[c] && !z->field_built[c] && z->field[c] < wood.size()){
                wood[z->field[c]] += ECONOMY_FIELD_WOOD_PER_CELL;
            }
        }
        int sites = 0;
        int needed = 0;
        int brought = 0;
        for (size_t id = 1; id < wood.size(); id++){
            if (wood[id] > 0){
                sites++;
                needed += wood[id];
                brought += std::min(wood[id],EconomySiteBrought(*e,(uint32_t)id));
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
                needed += need;
                brought += std::min(need,EconomyGroundBrought(*e,(int)v));
            }
        }
        if (sites > 0){
            //Over the grid, at its right edge.
            char buf2[64];
            snprintf(buf2,sizeof(buf2),"BUILDING %i  -  WOOD %i/%i",sites,brought,needed);
            vec2 m = overlay->MeasureText(buf2,size);
            const float side = 14.0f * s;
            vec2 r1(q1.x,q0.y - 6.0f * s);
            vec2 r0(r1.x - m.x - side * 2.0f,r1.y - 30.0f * s);
            overlay->AddRect(r0,r1,9.0f * s,HUD_PANEL);
            overlay->AddText(buf2,vec2((r0.x + r1.x) * 0.5f,r0.y + 21.0f * s),size,UIColor(206,160,110),UI_ALIGN_CENTER);
        }
    }
}

/*
    RENDER THREAD, from DrawOverlay in play mode (builders_plan.md): in the bottom left corner, how many work
    at each job against its places - amber where a place stands empty, which with too few people is the
    newest workplace's - and the jobless, who carry wood and build too. Above the tools' grid only when the
    window is too narrow for the two side by side. Only what has places or people. Returns its top: what
    stacks over it (DrawNeediest) goes above that.
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
    static const int shown[] = {WORKER_JOB_BUILDER,WORKER_JOB_WOODCUTTER,WORKER_JOB_FARMER,WORKER_JOB_WATER};
    static const char* names[] = {"BUILDERS","WOODCUTTERS","FARMERS","WATER CARRIERS"};
    for (int i = 0; i < 4; i++){
        const EconomyJobCount& c = counts[shown[i]];
        if (c.places > 0 || c.working > 0){
            rows.push_back(Row{names[i],c.working,c.places,true});
        }
    }
    rows.push_back(Row{"NO JOB",jobless,0,false});
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
        bottom = build_bar_min.y - 8.0f * s;
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
                {"speed",physics_time_factor},
                {"speed_reached",GetPhysicsSpeed()},
                {"paused",main_scene->IsPhysicsPaused()},
                {"ran",f_ran}
            };
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
