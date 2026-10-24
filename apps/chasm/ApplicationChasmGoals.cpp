#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "ChasmStats.h"
#include "Window.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdio.h>

/*
    THE DEMO'S GOALS AND ITS END (docs/demo_plan.md step 7; the user's, 2026-10-15): two goals, the first winter
    through with nobody dead and the chasm reached - a winch built, someone lowered by it - each in progress, met or
    failed; a death stops the game behind a pop-up; and the end, the demo complete or the colony lost, is the
    statistics page with its own heading and buttons (ApplicationChasmStats.cpp).
    A death of OLD AGE is no villager lost (the user's, 2026-10-24): it fails no goal and stops nothing. The death line
    over the build bar, the journal and the statistics still tell it, as they tell every death.

    THE RULES: whether a goal is met is worked out from the state - the dead with their ticks, the calendar, the
    DISCOVERIES - and told once, as an event the journal keeps (EVENT_GOAL_MET and the rest), on the tick it comes
    true, so a replay ends the same way and a load does not tell it again. THE VIEWER: the pop-ups and the end's page
    read those events as they come, as the notice line does, and pause the game the way the pause menu does - which
    changes nothing in the colony.

    THE SWITCHES (debug builds; the user's, 2026-10-18): each of what the viewer tells or stops for can be turned off -
    from the Goals panel, or chasm_menu's `triggers` - for testing, where a death or a lost colony is what is being looked
    at rather than the end of the game: a goal's row on the GOALS panel and its notice (and the winter's line in a
    death's pop-up), a death's pop-up, the colony lost's page and its line over the build bar, the demo complete's
    page. The VIEWER's, as the pause menu is: never recorded or saved. The rules still tell every goal and end into the
    journal, so the statistics, the end's page and a replay are as they were.
*/
#define GOALS_MET_COLOUR    UIColor(140,200,110)    //a goal met, on the panel and the end's page

namespace {

//A goal's line in capitals, for the panel and the page.
std::string GoalCaps(int goal){
    std::string t = ChasmGoalTitle(goal);
    std::transform(t.begin(),t.end(),t.begin(),::toupper);
    return t;
}

}

/*
    Whether a goal is met (CHASM_GOAL_*): as the journal has it once it was told, else as the state stands -
      - THROUGH THE WINTER: failed by any death before the first dawn of the second spring - but of old age - met at
        that dawn;
      - THE CHASM: met with a winch finished and someone lowered by one to a balcony. It is never failed.
*/
int ChasmGoalState(int goal, const EconomyState& e, const ChasmDiscoveries& d){
    if (goal >= 0 && goal < CHASM_GOALS && d.goal_told[goal] >= 0){
        return d.goal_told[goal];
    }
    if (goal == CHASM_GOAL_WINTER){
        for (const EconomyDeath& x : e.dead){
            if (x.tick < (uint64_t)CALENDAR_YEAR_TICKS && x.cause != NEEDS_DIED_OLD_AGE){
                return CHASM_GOAL_FAILED;
            }
        }
        return (e.tick >= (uint64_t)CALENDAR_YEAR_TICKS) ? CHASM_GOAL_MET : CHASM_GOAL_GOING;
    }
    bool f_met = (d.mask & (1u << DISCOVERY_WINCH)) && (d.mask & (1u << DISCOVERY_BALCONY));
    return f_met ? CHASM_GOAL_MET : CHASM_GOAL_GOING;
}

//What a goal in progress wants next, as the panel says it: "23 DAYS TO GO", "BUILD A WINCH".
std::string ChasmGoalNext(int goal, const EconomyState& e, const ChasmDiscoveries& d){
    if (goal == CHASM_GOAL_WINTER){
        uint64_t left = (e.tick < (uint64_t)CALENDAR_YEAR_TICKS) ? (uint64_t)CALENDAR_YEAR_TICKS - e.tick : 0;
        int days = (int)((left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS);
        char buf[32];
        snprintf(buf,sizeof(buf),"%i DAY%s TO GO",days,days == 1 ? "" : "S");
        return buf;
    }
    if (!(d.mask & (1u << DISCOVERY_CHASM))){
        return "FIND THE CHASM";
    }
    if (!(d.mask & (1u << DISCOVERY_WINCH))){
        return "BUILD A WINCH";
    }
    return "LOWER SOMEONE DOWN";
}

const char* ApplicationChasm::TriggerName(int t){
    switch (t){
        case CHASM_GOAL_WINTER:         return "winter";
        case CHASM_GOAL_CHASM:          return "chasm";
        case CHASM_TRIGGER_DEATH:       return "death";
        case CHASM_TRIGGER_LOST:        return "colony_lost";
        case CHASM_TRIGGER_COMPLETE:    return "demo_complete";
        default:                        return "?";
    }
}

void ApplicationChasm::SetTrigger(int t, bool f_on){
    if (t < 0 || t >= CHASM_TRIGGERS){
        return;
    }
    if (f_on){
        triggers_off.fetch_and(~(1u << t));
    }else{
        triggers_off.fetch_or(1u << t);
    }
}

//Nobody left alive, and somebody dead: not a colony that never had anyone.
bool ApplicationChasm::ColonyLost(){
    const EconomyState& e = economy.State();
    return e.workers.empty() && !e.dead.empty();
}

/*
    PHYSICS THREAD, from RunSimulationTick after the exploration (so the discoveries are this tick's): the goals and the
    end as rules. Each is told once - the journal says whether it has been (UpdateDiscoveries reads it) - on the tick it
    comes true: the colony lost, with nothing after it; a goal met or failed; and the demo complete once both are one or
    the other. Met or not, the demo then has its end, so a colony that lost someone in its first winter still gets one.
*/
void ApplicationChasm::TickGoals(){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    const EconomyState& e = economy.State();
    if (!w || e.world != w){
        return;
    }
    UpdateDiscoveries(*w);
    const ChasmDiscoveries& d = discoveries;
    if (d.f_lost_told){
        return;
    }
    auto told = [&](int kind, uint32_t person, int amount){
        EconomyEvent ev;
        ev.kind = kind;
        ev.person = person;
        ev.amount = amount;
        economy.Event(ev,calendar_tick);
    };
    if (e.workers.empty() && !e.dead.empty()){
        told(EVENT_COLONY_LOST,e.dead.back().id,0);
        return;
    }
    int decided = 0;
    int met = 0;
    for (int g = 0; g < CHASM_GOALS; g++){
        int state = ChasmGoalState(g,e,d);
        if (d.goal_told[g] < 0 && state == CHASM_GOAL_MET){
            told(EVENT_GOAL_MET,(g == CHASM_GOAL_CHASM) ? d.person[DISCOVERY_BALCONY] : 0,g);
        }else if (d.goal_told[g] < 0 && state == CHASM_GOAL_FAILED){
            //The death that failed it: the first of the year that was not of old age.
            uint32_t who = 0;
            uint64_t first = UINT64_MAX;
            for (const EconomyDeath& x : e.dead){
                if (x.tick < first && x.cause != NEEDS_DIED_OLD_AGE){
                    first = x.tick;
                    who = x.id;
                }
            }
            told(EVENT_GOAL_FAILED,who,g);
        }
        decided += (state != CHASM_GOAL_GOING) ? 1 : 0;
        met += (state == CHASM_GOAL_MET) ? 1 : 0;
    }
    if (decided == CHASM_GOALS && !d.f_complete_told){
        told(EVENT_DEMO_COMPLETE,0,met);
    }
}

/*
    PHYSICS THREAD, from UpdateView in the game, before the question's input: the events the last ticks made, as the
    notice line reads them (only those of the last half day: a replay's start can bring old ones). In play mode -
      - a DEATH, or several on one tick, stops the game behind the one pop-up: who died and of what, and the first
        goal failed with it if it was; Continue or Main menu;
      - THE COLONY LOST, or THE DEMO COMPLETE, opens the end's page - once nothing else is up, so after the pop-up
        of the death that brought it, and never over a menu.
*/
void ApplicationChasm::UpdateEnd(){
    const EconomyState& e = economy.State();
    if (e.world.get() != end_world || e.events_made < end_seen){
        end_world = e.world.get();
        end_seen = e.events_made;
        end_pending = CHASM_END_NONE;
        return;
    }
    if (e.events_made > end_seen){
        size_t fresh = std::min((size_t)(e.events_made - end_seen),e.events.size());
        std::vector<const EconomyEvent*> deaths;
        bool f_failed = false;
        bool f_complete = false;
        bool f_lost = false;
        for (size_t i = e.events.size() - fresh; i < e.events.size(); i++){
            const EconomyEvent& ev = e.events[i];
            if (e.tick >= ev.tick + CALENDAR_DAY_TICKS / 2){
                continue;
            }
            if (ev.kind == EVENT_DIED && ev.amount != NEEDS_DIED_OLD_AGE){
                deaths.push_back(&ev);      //...a loss: old age is told on the death line alone
            }
            f_failed = f_failed || (ev.kind == EVENT_GOAL_FAILED && ev.amount == CHASM_GOAL_WINTER);
            f_complete = f_complete || ev.kind == EVENT_DEMO_COMPLETE;
            f_lost = f_lost || ev.kind == EVENT_COLONY_LOST;
        }
        end_seen = e.events_made;
        //Each held to its switch (THE SWITCHES, above): one turned off is told nowhere and stops nothing.
        if (PlayMode() && f_game_on.load() && !TitleShowing()){
            if (f_lost && TriggerOn(CHASM_TRIGGER_LOST)){
                end_pending = CHASM_END_LOST;
            }else if (!deaths.empty() && TriggerOn(CHASM_TRIGGER_DEATH)){
                std::string head = (deaths.size() == 1) ? "A villager died"
                                                       : std::to_string(deaths.size()) + " villagers died";
                std::string detail;
                const size_t named = 4;
                for (size_t i = 0; i < deaths.size() && i < named; i++){
                    std::string name = "Someone";
                    for (const EconomyDeath& x : e.dead){
                        //...with his rank and kills (ranks_plan.md): "Jelle De Wit, Expert, 2 kills, killed by a zombie".
                        name = (x.id == deaths[i]->person) ? EconomyDeathName(x) + EconomyKillsTold(x.kills) : name;
                    }
                    int cause = deaths[i]->amount;
                    detail += (detail.empty() ? "" : "\n") + name
                              + ((cause == NEEDS_DIED_ZOMBIE) ? std::string(", killed by a zombie") : ", of " + std::string(EconomyDeathCause(cause)));
                }
                if (deaths.size() > named){
                    detail += "\nand " + std::to_string(deaths.size() - named) + " more";
                }
                if (f_failed && TriggerOn(CHASM_GOAL_WINTER)){
                    detail += "\nThe first goal is failed: not everyone lived to see the second spring.";
                }
                OpenAsk(CHASM_ASK_DEATH,head,detail);
            }
            if (f_complete && end_pending == CHASM_END_NONE && TriggerOn(CHASM_TRIGGER_COMPLETE)){
                end_pending = CHASM_END_COMPLETE;
            }
        }
    }
    //...and an end waiting that was switched off since: dropped, not opened later.
    if (end_pending != CHASM_END_NONE && !TriggerOn((end_pending == CHASM_END_LOST) ? CHASM_TRIGGER_LOST : CHASM_TRIGGER_COMPLETE)){
        end_pending = CHASM_END_NONE;
    }
    if (end_pending != CHASM_END_NONE && ask_kind.load() == CHASM_ASK_NONE && !f_stats_open.load() && !f_pause_open.load()
        && !TitleShowing() && busy_action.kind == CHASM_ACT_NONE){
        OpenEnd(end_pending);
        end_pending = CHASM_END_NONE;
    }
}

/*
    RENDER THREAD, from DrawOverlay in play mode: the GOALS panel at the top left, under `top` - each goal with its mark
    in its state's colour (dim in progress, green met, red failed) and at its right what it wants next, or MET or
    FAILED. Its look is NEEDS ATTENTION's. A goal switched off (THE SWITCHES) has no row, and with none left there is
    no panel. Returns its foot, or `top` with nothing drawn.
*/
float ApplicationChasm::DrawGoals(float s, float top){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ChasmDiscoveries> d = GetDiscoveries();
    if (!e || !d || !e->world){
        return top;
    }
    const float size = 14.0f * s;
    const float title_size = 12.0f * s;
    const float title_h = 18.0f * s;
    const float line_h = 26.0f * s;
    const float pad = 12.0f * s;
    const float mark = 8.0f * s;
    const float mark_gap = 9.0f * s;
    const float col_gap = 22.0f * s;
    std::string text[CHASM_GOALS];
    std::string value[CHASM_GOALS];
    int state[CHASM_GOALS];
    int shown[CHASM_GOALS];
    int rows = 0;
    float w = 0.0f;
    for (int g = 0; g < CHASM_GOALS; g++){
        if (!TriggerOn(g)){
            continue;
        }
        shown[rows++] = g;
        state[g] = ChasmGoalState(g,*e,*d);
        text[g] = GoalCaps(g);
        value[g] = (state[g] == CHASM_GOAL_MET) ? "MET" : (state[g] == CHASM_GOAL_FAILED) ? "FAILED" : ChasmGoalNext(g,*e,*d);
        w = std::max(w,pad * 2.0f + mark + mark_gap + overlay->MeasureText(text[g].c_str(),size).x + col_gap
                       + overlay->MeasureText(value[g].c_str(),size).x);
    }
    if (!rows){
        return top;
    }
    vec2 p0(16.0f * s,top);
    vec2 p1(p0.x + w,p0.y + pad * 0.8f + title_h + line_h * rows + pad * 0.4f);
    overlay->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    overlay->AddText("GOALS",vec2(p0.x + pad,p0.y + pad * 0.8f + 11.0f * s),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    for (int r = 0; r < rows; r++){
        int g = shown[r];
        float mid = p0.y + pad * 0.8f + title_h + line_h * (float)r + line_h * 0.5f;
        uint32_t colour = (state[g] == CHASM_GOAL_MET) ? GOALS_MET_COLOUR : (state[g] == CHASM_GOAL_FAILED) ? HUD_DANGER : HUD_TEXT_DIM;
        overlay->AddRect(vec2(p0.x + pad,mid - mark * 0.5f),vec2(p0.x + pad + mark,mid + mark * 0.5f),mark * 0.5f,colour);
        overlay->AddText(text[g].c_str(),vec2(p0.x + pad + mark + mark_gap,mid + 5.0f * s),size,
                         (state[g] == CHASM_GOAL_GOING) ? HUD_TEXT : HUD_TEXT_DIM,UI_ALIGN_LEFT);
        overlay->AddText(value[g].c_str(),vec2(p1.x - pad,mid + 5.0f * s),size,(state[g] == CHASM_GOAL_GOING) ? HUD_TEXT_DIM : colour,
                         UI_ALIGN_RIGHT);
    }
    return p1.y + 8.0f * s;
}

#ifdef USE_IMGUI
/*
    The panel's GOALS (debug builds): every goal and end, how it stands, and its switch (THE SWITCHES, above) - ticked,
    play mode shows it, tells it and stops for it as it always has; unticked, none of those.
*/
void ApplicationChasm::RenderGoalsPanel(){
#ifdef DEBUG
    if (!ImGui::CollapsingHeader("Goals")){
        return;
    }
    ImGui::TextDisabled("what play mode shows, tells and stops for - unticked, none of it");
    ImGui::TextDisabled("(the rules journal every goal and end either way)");
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ChasmDiscoveries> d = GetDiscoveries();
    bool f_ours = e && d && e->world;
    auto row = [&](int t, const std::string& label, const std::string& stands){
        bool f_on = TriggerOn(t);
        if (ImGui::Checkbox((label + "##trigger" + std::to_string(t)).c_str(),&f_on)){
            SetTrigger(t,f_on);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s",stands.c_str());
    };
    for (int g = 0; g < CHASM_GOALS; g++){
        std::string stands = "-";
        if (f_ours){
            int st = ChasmGoalState(g,*e,*d);
            stands = (st == CHASM_GOAL_MET) ? "met" : (st == CHASM_GOAL_FAILED) ? "failed" : ChasmGoalNext(g,*e,*d);
            std::transform(stands.begin(),stands.end(),stands.begin(),::tolower);
        }
        row(g,std::string("goal: ") + ChasmGoalTitle(g),stands);
    }
    row(CHASM_TRIGGER_DEATH,"a death: its pop-up, the game paused",f_ours ? std::to_string(e->dead.size()) + " dead" : "-");
    row(CHASM_TRIGGER_LOST,"the colony lost: its page",f_ours ? (d->f_lost_told ? "told" : "not yet") : "-");
    row(CHASM_TRIGGER_COMPLETE,"the demo complete: its page",f_ours ? (d->f_complete_told ? "told" : "not yet") : "-");
    if (ImGui::SmallButton("All on##triggers")){
        triggers_off = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All off##triggers")){
        triggers_off = (1u << CHASM_TRIGGERS) - 1u;
    }
#endif
}
#endif
