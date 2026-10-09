#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "Window.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include <algorithm>
#include <cmath>
#include <stdio.h>

/*
    NEEDS ATTENTION (docs/issues_plan.md, the user's 2026-10-09): what wants doing, over the tools' grid -
    a choice the player has left open, something placed that cannot work as it is, and what to build next
    while the game is being learned. Worked out from the published zones and economy, every frame and by
    the tool, and never saved: the one part of it that changes the run - a crop left unchosen defaulting to
    AUTO - is the economy's (Economy.h, ECONOMY_CROP_GRACE). The rows are the user's first eight.
*/
#define ISSUES_SHOWN        5
#define ISSUES_RED_DAYS     2.0f    //a hint whose deadline is closer than this goes red, to the top

namespace {

//What the colony holds to live on, in days of it for everyone: water, and food at what each unit is worth.
//Stores, the camp's supplies and the houses' larders - not what lies at a workplace or is being carried.
void DaysHeld(const ZoneState& z, const EconomyState& e, float& water, float& food){
    int64_t water_ticks = 0;
    int64_t food_ticks = 0;
    for (size_t id = 1; id < e.stock.size() && id < z.buildings.size(); id++){
        int kind = z.buildings[id].kind;
        if (z.buildings[id].size <= 0 || (kind != ZONE_KIND_STORE && kind != ZONE_KIND_CAMP && kind != ZONE_KIND_HOUSE)){
            continue;
        }
        water_ticks += (int64_t)e.stock[id][GOOD_WATER] * NEEDS_WATER_WORTH;
        for (int g = 0; g < GOOD_COUNT; g++){
            food_ticks += (int64_t)e.stock[id][g] * EconomyFoodWorth(g);
        }
    }
    float people_days = (float)std::max<size_t>(1,e.workers.size()) * CALENDAR_DAY_TICKS;
    water = (float)water_ticks / people_days;
    food = (float)food_ticks / people_days;
}

std::string DaysText(const char* what, float days){
    char buf[48];
    int d = (int)std::floor(days);
    if (d < 1){
        snprintf(buf,sizeof(buf),"%s UNDER A DAY",what);
    }else{
        snprintf(buf,sizeof(buf),"%s %i DAY%s",what,d,d == 1 ? "" : "S");
    }
    return buf;
}

const char* WorkplaceName(int kind){
    switch (kind){
        case ZONE_KIND_WOODCUTTER:  return "THE WOODCUTTER'S HUT";
        case ZONE_KIND_WATER:       return "THE WATER COLLECTOR";
        case ZONE_KIND_BUILDERS:    return "THE BUILDERS' HUT";
        default:                    return "A WORKPLACE";
    }
}

#ifdef USE_MCP
const char* TierName(int tier){
    switch (tier){
        case ISSUE_DANGER:      return "danger";
        case ISSUE_COUNTDOWN:   return "countdown";
        case ISSUE_STUCK:       return "stuck";
        default:                return "next";
    }
}
#endif

}

void ChasmIssues(const ZoneState& z, const EconomyState& e, std::vector<ChasmIssue>& out){
    out.clear();
    if (e.world != z.world || e.workers.empty()){
        return;
    }
    size_t n = z.buildings.size();
    //One pass over the plots and one over the cells: what of each building stands, and a field still a site.
    std::vector<uint8_t> stands(n,0);
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] && z.standing[v] > 0){
            stands[z.building[v]] = 1;
        }
    }
    std::vector<uint8_t> field_site(n,0);
    for (size_t q = 0; q < z.field.size(); q++){
        if (z.field[q] && !z.field_built[q]){
            field_site[z.field[q]] = 1;
        }
    }
    int placed[ZONE_KIND_COUNT] = {};
    bool f_producing = false;   //a woodcutter or collector standing: goods are being made
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || b.kind >= ZONE_KIND_COUNT){
            continue;
        }
        placed[b.kind]++;
        f_producing = f_producing || ((b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER) && stands[id]);
    }

    //1. A field left without a crop, counting down to AUTO.
    for (const EconomyField& f : e.fields){
        uint32_t id = f.building;
        if (id >= n || z.buildings[id].kind != ZONE_KIND_FIELD || z.buildings[id].size <= 0
            || z.buildings[id].crop != ZONE_CROP_NONE || f.f_sown || f.last >= 0 || field_site[id]){
            continue;
        }
        ChasmIssue i;
        i.kind = "choose_crop";
        i.tier = ISSUE_COUNTDOWN;
        i.left = 1.0f - std::min(1.0f,(float)f.wait / ECONOMY_CROP_GRACE);
        i.order = i.left;
        i.text = "FIELD: CHOOSE A CROP";
        i.value = "AUTO";
        i.building = id;
        out.push_back(i);
    }

    //2-6. The next steps, each with its deadline where there is one.
    float water_days = 0.0f;
    float food_days = 0.0f;
    DaysHeld(z,e,water_days,food_days);
    auto hint = [&](const char* kind, int order, const char* text, const std::string& value, int tool, float days){
        ChasmIssue i;
        i.kind = kind;
        i.tier = (days < ISSUES_RED_DAYS) ? ISSUE_DANGER : ISSUE_NEXT;
        i.order = (i.tier == ISSUE_DANGER) ? days : (float)order;
        i.text = text;
        i.value = value;
        i.tool = tool;
        out.push_back(i);
    };
    if (!placed[ZONE_KIND_WATER]){
        hint("build_water",2,"BUILD A WATER COLLECTOR",DaysText("WATER",water_days),CHASM_TOOL_WATER,water_days);
    }
    if (!placed[ZONE_KIND_FIELD]){
        hint("build_field",3,"BUILD A FIELD",DaysText("FOOD",food_days),CHASM_TOOL_FIELD,food_days);
    }
    if (!placed[ZONE_KIND_WOODCUTTER]){
        int wood = EconomyStoredTotals(e,z)[GOOD_WOOD];
        char buf[32];
        snprintf(buf,sizeof(buf),"WOOD %i",wood);
        hint("build_woodcutter",4,"BUILD A WOODCUTTER",buf,CHASM_TOOL_WOODCUTTER,1e9f);
    }
    int in_camp = 0;
    for (const EconomyWorker& k : e.workers){
        in_camp += (k.house == 0 || (k.house < n && z.buildings[k.house].kind == ZONE_KIND_CAMP)) ? 1 : 0;
    }
    if (in_camp > 0){
        char buf[32];
        snprintf(buf,sizeof(buf),"%i IN THE CAMP",in_camp);
        hint("build_houses",5,"BUILD HOUSES",buf,CHASM_TOOL_HOUSE,1e9f);
    }
    //A store only once something is made: until then there is nothing to keep, and the camp holds the supplies.
    if (!placed[ZONE_KIND_STORE] && f_producing){
        hint("build_store",6,"BUILD A STORE","",CHASM_TOOL_STORE,1e9f);
    }

    //7-8. What stands and cannot work as it is: a place nobody fills, a woodcutter with nowhere to pile.
    std::vector<int> working(n,0);
    std::vector<int> living(n,0);
    for (const EconomyWorker& k : e.workers){
        if (k.building && k.building < n){
            working[k.building]++;
        }
        if (k.house && k.house < n){
            living[k.house]++;
        }
    }
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        bool f_workplace = b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_BUILDERS;
        if (f_workplace && stands[id] && id < e.places.size() && e.places[id] > 0 && working[id] == 0){
            ChasmIssue i;
            i.kind = "nobody_at";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = std::string("NOBODY AT ") + WorkplaceName(b.kind);
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        if (b.kind == ZONE_KIND_WOODCUTTER && (id >= e.pile_plot.size() || e.pile_plot[id] < 0)){
            ChasmIssue i;
            i.kind = "no_lot";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = "WOODCUTTER: NO LOT TO PILE ON";
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        /*
            9-10, the houses (the user's, 2026-10-09; lifeline_plan.md): one shut in on every side, with no way in
            (EconomyShutIn) - red, at the very top, with anyone inside, who cannot get out; and one with more
            people than it holds, a child born into a full house, wanting a storey or a plot more.
        */
        if (b.kind == ZONE_KIND_HOUSE){
            if (EconomyShutIn(e,(uint32_t)id)){
                ChasmIssue i;
                i.kind = "house_shut_in";
                i.tier = living[id] ? ISSUE_DANGER : ISSUE_STUCK;
                i.order = living[id] ? 0.0f : (float)id;
                i.text = "HOUSE SHUT IN";
                if (living[id]){
                    i.value = std::to_string(living[id]) + " INSIDE";
                }
                i.building = (uint32_t)id;
                out.push_back(i);
            }else if (stands[id] && id < e.capacity.size() && living[id] > e.capacity[id]){
                ChasmIssue i;
                i.kind = "house_crowded";
                i.tier = ISSUE_STUCK;
                i.order = (float)id;
                i.text = "HOUSE CROWDED";
                i.value = std::to_string(living[id]) + " IN ROOM FOR " + std::to_string(e.capacity[id]);
                i.building = (uint32_t)id;
                out.push_back(i);
            }
        }
    }

    std::stable_sort(out.begin(),out.end(),[](const ChasmIssue& a, const ChasmIssue& b){
        if (a.tier != b.tier){
            return a.tier < b.tier;
        }
        return a.order < b.order;
    });
}

//Where a building is, for the view to go to: the middle of its plots, or of a field's cells. False if it is gone.
bool ChasmBuildingPoint(const ChasmWorld& w, const ZoneState& z, uint32_t id, vec2& out){
    vec2 sum(0.0f,0.0f);
    int count = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            sum += w.grid->fine.pos[v];
            count++;
        }
    }
    for (size_t q = 0; count == 0 && q < z.field.size(); q++){
        if (z.field[q] == id){
            for (int j = 0; j < 4; j++){
                sum += w.grid->FineQuadCentre((int)q * 4 + j) * 0.25f;
            }
            count++;
        }
    }
    if (count == 0){
        return false;
    }
    out = sum * (1.0f / (float)count);
    return true;
}

/*
    RENDER THREAD, from DrawOverlay in play mode: the first ISSUES_SHOWN of the list over the tools' grid, its
    right edge on the grid's and at least as wide - `bottom` is the top of the grid's stack. Each row a mark
    in its tier's colour (red a deadline close, amber a countdown or something stuck, dim a next step), what
    to do, and at the right its value; a countdown drains a line under it. A click on a row selects its place
    and takes the view there (OVERLAY_HIT_BUILDING), or takes up its tool (OVERLAY_HIT_TAKE_TOOL). Returns its
    top - `bottom` itself with nothing to say.
*/
float ApplicationChasm::DrawIssues(float s, float bottom){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z){
        return bottom;
    }
    std::vector<ChasmIssue> issues;
    ChasmIssues(*z,*e,issues);
    if (issues.empty()){
        return bottom;
    }
    int shown = std::min(ISSUES_SHOWN,(int)issues.size());
    PlayPick sel;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
    }
    int tool = paint_tool.load();
    const float size = 14.0f * s;
    const float title_size = 12.0f * s;
    const float title_h = 18.0f * s;
    const float line_h = 26.0f * s;
    const float pad = 12.0f * s;
    const float mark = 8.0f * s;
    const float mark_gap = 9.0f * s;
    const float col_gap = 14.0f * s;
    float w = build_bar_max.x - build_bar_min.x;
    for (int r = 0; r < shown; r++){
        float row = pad * 2.0f + mark + mark_gap + overlay->MeasureText(issues[r].text.c_str(),size).x;
        if (!issues[r].value.empty()){
            row += col_gap + overlay->MeasureText(issues[r].value.c_str(),size).x;
        }
        w = std::max(w,row);
    }
    float h = pad * 0.8f + title_h + line_h * (float)shown + pad * 0.4f;
    vec2 p1(build_bar_max.x,bottom);
    vec2 p0(p1.x - w,bottom - h);
    overlay->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    float title_y = p0.y + pad * 0.8f + 11.0f * s;
    overlay->AddText("NEEDS ATTENTION",vec2(p0.x + pad,title_y),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    if ((int)issues.size() > shown){
        char more[24];
        snprintf(more,sizeof(more),"+%i MORE",(int)issues.size() - shown);
        overlay->AddText(more,vec2(p1.x - pad,title_y),title_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }
    for (int r = 0; r < shown; r++){
        const ChasmIssue& i = issues[r];
        float top = p0.y + pad * 0.8f + title_h + line_h * (float)r;
        float mid = top + line_h * 0.5f;
        bool f_lit = (i.building && sel.kind == PLAY_PICK_BUILDING && sel.id == i.building) || (i.tool >= 0 && tool == i.tool);
        if (f_lit){
            overlay->AddRect(vec2(p0.x + 4.0f * s,top + 1.0f * s),vec2(p1.x - 4.0f * s,top + line_h - 1.0f * s),6.0f * s,
                             HudFaded(HUD_SELECT,60));
        }
        uint32_t colour = (i.tier == ISSUE_DANGER) ? HUD_DANGER : (i.tier == ISSUE_NEXT) ? HUD_TEXT_DIM : HUD_AMBER;
        overlay->AddRect(vec2(p0.x + pad,mid - mark * 0.5f),vec2(p0.x + pad + mark,mid + mark * 0.5f),mark * 0.5f,colour);
        float text_x = p0.x + pad + mark + mark_gap;
        overlay->AddText(i.text.c_str(),vec2(text_x,mid + 5.0f * s),size,(i.tier == ISSUE_DANGER) ? HUD_DANGER : HUD_TEXT,
                         UI_ALIGN_LEFT);
        if (!i.value.empty()){
            overlay->AddText(i.value.c_str(),vec2(p1.x - pad,mid + 5.0f * s),size,(i.tier == ISSUE_DANGER) ? HUD_DANGER : HUD_TEXT_DIM,
                             UI_ALIGN_RIGHT);
        }
        if (i.left >= 0.0f){
            //The countdown: what is left of it, draining from the right, under the row's words.
            vec2 line0(text_x,mid + 9.0f * s);
            vec2 line1(p1.x - pad,line0.y + 2.0f * s);
            overlay->AddRect(line0,line1,1.0f * s,HudFaded(HUD_AMBER,50));
            overlay->AddRect(line0,vec2(line0.x + (line1.x - line0.x) * i.left,line1.y),1.0f * s,HUD_AMBER);
        }
        if (i.building){
            overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_BUILDING,(int)i.building});
        }else if (i.tool >= 0){
            overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_TAKE_TOOL,i.tool});
        }
    }
    return p0.y;
}

#ifdef USE_MCP
void ApplicationChasm::RegisterIssueTools(){
    MCPServer::Get()->RegisterTool("chasm_issues",
        "NEEDS ATTENTION (docs/issues_plan.md): everything the panel over the tools' grid would list, in its order - "
        "the panel shows the first five, in play mode. Each: kind (choose_crop, build_water, build_field, "
        "build_woodcutter, build_houses, build_store, nobody_at, no_lot), tier (danger: a deadline under two days; "
        "countdown: a choice the game makes when it runs out; stuck; next: a next step), text and value as drawn, "
        "left (a countdown's share still to run, 1 to 0) and wait/grace ticks for a field, building (what a click "
        "selects) or tool (what it takes up). View only. include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const EconomyState> e = GetEconomy();
            std::shared_ptr<const ZoneState> z = GetZones();
            json list = json::array();
            if (e && z){
                std::vector<ChasmIssue> issues;
                ChasmIssues(*z,*e,issues);
                for (const ChasmIssue& i : issues){
                    json one{{"kind",i.kind},{"tier",TierName(i.tier)},{"text",i.text},{"value",i.value}};
                    if (i.left >= 0.0f){
                        one["left"] = i.left;
                        const EconomyField* f = EconomyFieldOf(*e,i.building);
                        one["wait"] = f ? f->wait : 0;
                        one["grace"] = ECONOMY_CROP_GRACE;
                    }
                    if (i.building){
                        one["building"] = i.building;
                    }
                    if (i.tool >= 0){
                        one["tool"] = i.tool;
                    }
                    list.push_back(one);
                }
            }
            json result{{"issues",list},{"shown",std::min(ISSUES_SHOWN,(int)list.size())}};
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
