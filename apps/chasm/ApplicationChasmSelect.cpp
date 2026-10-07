#include "ApplicationChasm.h"
#include "ChasmHud.h"
#include "BuildingMesh.h"
#include "ZoneMesh.h"
#include "MeshBuild.h"
#include "Window.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include <algorithm>
#include <cmath>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/*
    PLAY MODE'S SELECTION (docs/selection_plan.md). In play mode the debug line mesh gives way to:
      - an OUTLINE round what the cursor is over (faint) and what is selected (Blender's orange), and in
        a paler colour round what belongs with it - a house's family, a workplace's worker, a site's
        carriers, a person's house and workplace. The outline itself is core's; buildings are merged
        into one mesh a chunk, so each outlined building is rebuilt alone into an OUTLINE-ONLY object;
      - a BADGE over everyone out of doors: his job's icon ringed by his health, the icon a drop of water
        while he is running out of it (needs_plan.md). The people who belong with the selection have
        theirs PINNED - on a stem, in the selection's colour, kept on screen - so they can be found
        across the map (a person is a few pixels tall at play zoom);
      - a CARD on the right saying what it is - a store's goods with chips to set what it takes.
    View state only: the one thing that reaches the simulation is a chip's store_allow, a recorded zone
    command like the debug panel's.

    Threads: the hover and the click are worked out on the physics thread (UpdatePick, which has the
    camera's ray); the outlines are rebuilt on the render thread (PreRender); the card and pins are
    drawn there too (DrawOverlay), which hands what the mouse can press back to UpdatePick.
*/

#define SELECT_COLOUR       vec4(1.00f,0.62f,0.16f,1.00f)   //HUD_SELECT: Blender's selection orange
#define RELATED_COLOUR      vec4(1.00f,0.90f,0.62f,0.85f)   //what belongs with the selection
#define HOVER_COLOUR        vec4(1.00f,0.95f,0.82f,0.50f)   //under the cursor
#define PERSON_PICK_PX      14.0f       //a person is picked within this many pixels (at 810 high) of his middle
#define PERSON_MIDDLE       0.55f       //world units above his feet: where he is picked
#define PERSON_PIN          1.55f       //...and where his pin points
#define THIRST_MARK_WATER   NEEDS_PRESSING_WATER    //water left at which his badge shows a drop: when he goes for it
#define THIRST_MARK_ROOF    0.3f        //a family's drop points this far over its house's roof
#define BADGE_RADIUS        11.0f       //a person's badge's dark disc, at 810 high
#define BADGE_RING          9.0f        //...the health ring's middle, on the disc
#define BADGE_RING_W        2.2f        //...and its width
#define BADGE_ICON          0.85f       //the icons' scale, against the drop's size at 1
#define BADGE_LOW_HEALTH    (2 * CALENDAR_DAY_TICKS)    //his ring goes amber here, as the card's bar does
#define FIELD_OUTLINE_LIFT  0.06f       //a field's outline slab over its ground
#define RIGHT_CLICK_SLOP    6           //pixels a right press may move and still be a click, not a pan
#define CARD_W              300.0f      //at 810 high
#define SHAPE_PICK_STEP     0.2f        //world units between the samples of a pick ray among the buildings
#define SHAPE_PICK_ABOVE    8.0f        //the ray is sampled from this far above the ground it meets: past any roof
#define SHAPE_ROOF          0.6f        //a roof's reach above the eaves, as the pick sees it (BuildingMesh: 0.85 at the ridge)
#define SHAPE_TENT          0.85f       //a tent, as tall as it is drawn
#define SHAPE_WINCH         2.6f        //a winch's gantry

namespace {

/*
    The core outline (selection_plan.md, being built in core). Until it lands an outline-only object
    must not draw at all - it is the building's geometry again, and would fight the real one - so the
    selection's objects stay hidden and people get no outline; the cards and pins work regardless.
*/
#define CHASM_OUTLINE_READY 1
void SetOutlineOf(Object* o, const vec4& colour){
#if CHASM_OUTLINE_READY
    o->SetOutline(colour);
#else
    (void)o;
    (void)colour;
#endif
}

//Where a point lands on the window, from the camera's last world-to-clip matrix. False behind the camera.
bool ScreenOf(Camera* cam, float width, float height, const vec3& p, vec2& out){
    const fmat4& m = cam->mat_cam;
    float cx = m.vertex[0].x * p.x + m.vertex[1].x * p.y + m.vertex[2].x * p.z + m.vertex[3].x;
    float cy = m.vertex[0].y * p.x + m.vertex[1].y * p.y + m.vertex[2].y * p.z + m.vertex[3].y;
    float cw = m.vertex[0].w * p.x + m.vertex[1].w * p.y + m.vertex[2].w * p.z + m.vertex[3].w;
    if (cw <= 1e-4f){
        return false;
    }
    out = vec2((cx / cw * 0.5f + 0.5f) * width,(1.0f - (cy / cw * 0.5f + 0.5f)) * height);
    return true;
}

//The badges' icons' own colours - the HUD's are for text and states.
#define ICON_WOOD       UIColor(198,148,98,255)     //a handle, a log, a bucket
#define ICON_WOOD_END   UIColor(234,200,150,255)    //a log's cut end
#define ICON_STEEL      UIColor(214,220,224,255)    //an axe's head, a hammer's, a bucket's handle
#define ICON_WHEAT      UIColor(236,200,100,255)

/*
    THE BADGES' ICONS: a few opaque strokes each, so where they overlap does not show (AddLine's note), on
    a grid about 15 across round `c` - `s` the scale, 1 a pixel at 810 high.
*/
//A drop of water: its round end, and circles shrinking from it to the point, each a stroke from the round
//end - together the cone of a drop.
void DrawDropIcon(UIOverlay* o, const vec2& c, float s){
    vec2 base = c + vec2(0.0f,1.9f * s);    //the middle of the drop's round end
    float round = 4.1f * s;
    float reach = 8.0f * s;                 //from there to the point's last circle
    const int steps = 8;                    //about a pixel apart at 810 high
    for (int i = 0; i <= steps; i++){
        float t = (float)i / steps;
        float ri = round + (0.5f * s - round) * t;
        o->AddLine(base,base - vec2(0.0f,reach * t),ri * 2.0f,HUD_WATER);
    }
    vec2 glint = base + vec2(-1.6f * s,-0.5f * s);
    o->AddLine(glint,glint,1.8f * s,HudFaded(HUD_MARK,200));
}

/*
    A woodcutter's axe: the handle leaning right, and off its top a blade fanned out to a curved edge - a
    wedge, which is what tells it from the hammer's block at this size (a neck and an edge read as one).
*/
void DrawAxeIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-5.3f,6.8f),p(1.4f,-5.2f),1.8f * s,ICON_WOOD);
    vec2 hub = p(0.7f,-3.9f);
    const float a0 = 1.13f, a1 = 2.88f;     //65 to 165 degrees from twelve o'clock: across the handle
    const int fan = 8;
    for (int i = 0; i <= fan; i++){
        float a = a0 + (a1 - a0) * i / fan;
        o->AddLine(hub,hub + vec2(sinf(a),-cosf(a)) * (5.0f * s),1.8f * s,ICON_STEEL);
    }
    o->AddArc(hub,5.0f * s,a0,a1,1.4f * s,HUD_MARK);
}

//A farmer's ear of wheat: a stalk, a grain on top and three pairs down it.
void DrawWheatIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(0.0f,6.5f),p(0.0f,-2.0f),1.4f * s,ICON_WHEAT);
    o->AddLine(p(0.0f,-4.6f),p(0.0f,-6.4f),2.6f * s,ICON_WHEAT);
    for (int i = 0; i < 3; i++){
        float y = -3.4f + 2.5f * i;
        o->AddLine(p(0.0f,y),p(-2.4f,y - 2.0f),2.6f * s,ICON_WHEAT);
        o->AddLine(p(0.0f,y),p(2.4f,y - 2.0f),2.6f * s,ICON_WHEAT);
    }
}

//A water carrier's bucket: rows narrowing to the bottom, water at the brim, the handle over it.
void DrawBucketIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddArc(p(0.0f,-1.2f),4.6f * s,-1.5707963f,1.5707963f,1.3f * s,ICON_STEEL);
    const int rows = 5;
    for (int i = 0; i < rows; i++){
        float t = (float)i / (rows - 1);
        float y = -1.2f + 6.0f * t;
        float half = 4.6f - 1.4f * t;
        o->AddLine(p(-half,y),p(half,y),2.2f * s,(i == 0) ? HUD_WATER : ICON_WOOD);
    }
}

//A builder's hammer: the handle leaning right, the head across its top.
void DrawHammerIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-4.0f,6.2f),p(1.6f,-1.8f),1.8f * s,ICON_WOOD);
    o->AddLine(p(-0.9f,-5.2f),p(4.6f,-1.3f),3.4f * s,ICON_STEEL);
}

//The jobless's log, for the wood they carry to the sites: lying across, its cut end toward us.
void DrawLogIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    vec2 end = p(3.8f,-1.8f);
    o->AddLine(p(-4.8f,2.6f),end,4.8f * s,ICON_WOOD);
    o->AddLine(end,end,4.8f * s,ICON_WOOD_END);
    o->AddLine(end,end,1.4f * s,ICON_WOOD);
}

/*
    A PERSON'S BADGE (the user's, 2026-10-07): one over everyone out of doors - his job on a dark disc, and
    round it a ring that is his health, full at full and running back to twelve o'clock as it goes. While he
    is short of water the drop takes the job's place, so the badge says the one thing most worth knowing.
    The ring is coloured as the card's health bar - amber with two days left - and red once he is dry, losing
    it three times as fast. `pin`, for someone selected or related to the selection, rings the whole badge in
    that colour: it is what his pin was.
*/
void DrawPersonBadge(UIOverlay* o, const vec2& c, float s, const EconomyWorker& k, uint32_t pin){
    float back = BADGE_RADIUS * s;
    o->AddRect(c - vec2(back,back),c + vec2(back,back),back,HudFaded(HUD_PANEL,220));
    if (pin){
        float edge = back + 1.0f * s;
        o->AddRectOutline(c - vec2(edge,edge),c + vec2(edge,edge),edge,2.5f * s,pin);
    }
    float ring = BADGE_RING * s;
    o->AddRectOutline(c - vec2(ring,ring),c + vec2(ring,ring),ring,BADGE_RING_W * s,HudFaded(HUD_TEXT,70));
    float health = std::max(0.0f,std::min(1.0f,(float)k.health / NEEDS_HEALTH_FULL));
    uint32_t colour = EconomyDry(k) ? HUD_DANGER : (k.health <= BADGE_LOW_HEALTH) ? HUD_AMBER : HUD_TEXT;
    if (health > 0.0f){
        o->AddArc(c,ring,0.0f,health * 6.2831853f,BADGE_RING_W * s,colour);
    }
    float is = BADGE_ICON * s;
    if (k.water < THIRST_MARK_WATER){
        DrawDropIcon(o,c,is);
        return;
    }
    switch (k.job){
        case WORKER_JOB_WOODCUTTER: DrawAxeIcon(o,c,is); break;
        case WORKER_JOB_FARMER:     DrawWheatIcon(o,c,is); break;
        case WORKER_JOB_WATER:      DrawBucketIcon(o,c,is); break;
        case WORKER_JOB_BUILDER:    DrawHammerIcon(o,c,is); break;
        default:                    DrawLogIcon(o,c,is); break;
    }
}

//A family indoors short of water: the drop over its roof, ringed red if any of them is dry. No health ring -
//a roof has several people under it.
void DrawRoofThirst(UIOverlay* o, const vec2& c, float s, bool f_dry){
    float back = BADGE_RADIUS * s;
    o->AddRect(c - vec2(back,back),c + vec2(back,back),back,HudFaded(HUD_PANEL,220));
    if (f_dry){
        float ring = BADGE_RING * s;
        o->AddRectOutline(c - vec2(ring,ring),c + vec2(ring,ring),ring,BADGE_RING_W * s,HUD_DANGER);
    }
    DrawDropIcon(o,c,BADGE_ICON * s);
}

//Where a person's feet are: on the ground of the plot he stands on, as the workers' view puts him.
vec3 FeetOf(const ChasmWorld& w, const EconomyWorker& k){
    GridPick pick = w.picker->Pick(k.pos);
    float y = pick.f_hit ? w.terrain->GroundHeight(k.pos,w.terrain->Height(pick.plot)) : 0.0f;
    return vec3(k.pos.x,y,k.pos.y);
}

const EconomyWorker* PersonById(const EconomyState& e, uint32_t id){
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            return &k;
        }
    }
    return NULL;
}

//What he is doing, as a sentence's end - the card's and the house's list's.
const char* Doing(const EconomyWorker& k){
    switch (k.state){
        case WORKER_AT_HOME:    return EconomyIndoors(k) ? "at home" : (k.house ? "at his tent" : "waiting");
        case WORKER_TO_WORK:    return (k.prop >= 0) ? "going to a tree" : "going to work";
        case WORKER_WORKING:    return (k.prop >= 0) ? "felling" : "picking up";
        case WORKER_TO_STORE:   return "carrying to a store";
        case WORKER_UNLOADING:  return "unloading";
        case WORKER_TO_HOME:    return "going home";
        case WORKER_HOLDING:    return "waiting with a load";
        case WORKER_TO_FETCH:   return "fetching wood";
        case WORKER_FETCHING:   return "picking up wood";
        case WORKER_TO_SITE:    return "carrying wood to a site";
        case WORKER_BUILDING:   return "building";
        case WORKER_TO_ROAD:    return (k.prop >= 0) ? "going to clear a road" : "going to lay a road";
        case WORKER_CLEARING:   return "clearing a road";
        case WORKER_LAYING:     return "laying a road";
        case WORKER_TO_SUPPLY:{
            uint8_t want = EconomyPressing(k);
            bool f_water = (want & GOOD_BIT(GOOD_WATER)) != 0;
            bool f_food = (want & GOODS_FOOD) != 0;
            return (f_water && f_food) ? "going for food and water" : f_water ? "going for water" : "going for food";
        }
        case WORKER_SUPPLYING:  return "eating and drinking";
        case WORKER_TO_GET:     return (k.carry_good == GOOD_WATER) ? "fetching water for home" : "fetching food for home";
        case WORKER_GETTING:    return "picking up for home";
        case WORKER_TO_LARDER:  return "taking it home";
        default:                return "";
    }
}

const char* KindTitle(int kind){
    switch (kind){
        case ZONE_KIND_HOUSE:       return "House";
        case ZONE_KIND_STORE:       return "Store";
        case ZONE_KIND_WOODCUTTER:  return "Woodcutter's hut";
        case ZONE_KIND_WATER:       return "Water collector";
        case ZONE_KIND_FIELD:       return "Field";
        case ZONE_KIND_WINCH:       return "Winch";
        case ZONE_KIND_CAMP:        return "Camp";
        case ZONE_KIND_BRIDGE:      return "Bridge";
        case ZONE_KIND_BUILDERS:    return "Builders' hut";
        default:                    return "?";
    }
}

const char* JobTitle(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "Woodcutter";
        case WORKER_JOB_FARMER:     return "Farmer";
        case WORKER_JOB_WATER:      return "Water carrier";
        case WORKER_JOB_BUILDER:    return "Builder";
        default:                    return "No job";
    }
}

//The first plot of a building, for a pick made by id (a tool) rather than by a click.
int FirstPlotOf(const ZoneState& z, uint32_t id){
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            return (int)v;
        }
    }
    return -1;
}

/*
    The people who belong with a selection, for their outlines and pins: a house's or camp's family,
    a workplace's worker, a site's carriers, a store's carriers (to it or fetching from it, for a site or home).
*/
void RelatedPeople(const PlayPick& p, const ZoneState& z, const EconomyState& e, std::vector<uint32_t>& out){
    out.clear();
    if (p.kind != PLAY_PICK_BUILDING || p.id >= z.buildings.size()){
        return;
    }
    int kind = z.buildings[p.id].kind;
    for (const EconomyWorker& k : e.workers){
        bool f = k.site == p.id || k.building == p.id;
        if (kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_CAMP){
            f = f || k.house == p.id;
        }
        if (kind == ZONE_KIND_STORE){
            f = f || (k.store == p.id && (k.state == WORKER_TO_STORE || k.state == WORKER_TO_FETCH ||
                                          k.state == WORKER_UNLOADING || k.state == WORKER_FETCHING ||
                                          k.state == WORKER_TO_GET || k.state == WORKER_GETTING));
        }
        if (f){
            out.push_back(k.id);
        }
    }
}

/*
    THE PLACES that belong with a selection, for their outlines in the related colour - what a player
    asks "whose is this?" of (the user, 2026-10-06). One list, so a new kind of belonging is a case here:
      - a woodcutter's hut: his woodpile's lot, and his worker's house;
      - any other workplace (a field, a collector): its worker's house;
      - a house: its family's workplaces - a farmer's field, a woodcutter's hut and lot;
      - a person: his house and workplace, and a woodcutter's lot.
    A garden belongs to nobody yet (it does nothing yet). Each listed once, the selection itself never.
*/
struct RelatedPlaces{
    std::vector<uint32_t> buildings;    //sorted
    std::vector<int> plots;             //ground plots - a lot - sorted
};

void FindRelatedPlaces(const PlayPick& p, const ZoneState& z, const EconomyState& e, RelatedPlaces& out){
    out.buildings.clear();
    out.plots.clear();
    auto building = [&](uint32_t id){
        if (id && id < z.buildings.size() && z.buildings[id].size > 0 &&
            !(p.kind == PLAY_PICK_BUILDING && p.id == id)){
            out.buildings.push_back(id);
        }
    };
    auto lot_of = [&](uint32_t id){
        if (id < e.pile_plot.size() && e.pile_plot[id] >= 0){
            out.plots.push_back(e.pile_plot[id]);
        }
    };
    if (p.kind == PLAY_PICK_PERSON){
        for (const EconomyWorker& k : e.workers){
            if (k.id == p.id){
                building(k.house);
                building(k.building);
                lot_of(k.building);
            }
        }
    }else if (p.kind == PLAY_PICK_BUILDING && p.id < z.buildings.size()){
        int kind = z.buildings[p.id].kind;
        if (kind == ZONE_KIND_WOODCUTTER){
            lot_of(p.id);
        }
        for (const EconomyWorker& k : e.workers){
            if (k.building == p.id){
                building(k.house);              //where the one who works here lives
            }
            if ((kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_CAMP) && k.house == p.id){
                building(k.building);           //where its family work
                lot_of(k.building);
            }
        }
    }
    std::sort(out.buildings.begin(),out.buildings.end());
    out.buildings.erase(std::unique(out.buildings.begin(),out.buildings.end()),out.buildings.end());
    std::sort(out.plots.begin(),out.plots.end());
    out.plots.erase(std::unique(out.plots.begin(),out.plots.end()),out.plots.end());
}

/*
    THE CARD: a title, then lines - text, a text with a bar (0..1), a row of a store's goods as chips, or
    one chip that turns a setting on and off. Made from the published states, for the overlay and for the
    tool alike.
*/
#define CARD_TEXT   0
#define CARD_BAR    1
#define CARD_CHIPS  2
#define CARD_TOGGLE 3       //a site's "Build first"
struct CardLine{
    int type = CARD_TEXT;
    std::string text;
    uint32_t colour = HUD_TEXT;
    float bar = 0.0f;
    uint8_t chips_on = 0;       //CARD_CHIPS: the goods taken; CARD_TOGGLE: 1 while on
    bool f_locked = false;      //...and whether they can be changed here
};
struct Card{
    std::string title;
    std::vector<CardLine> lines;
    void Text(const std::string& t, uint32_t colour = HUD_TEXT){
        CardLine l;
        l.text = t;
        l.colour = colour;
        lines.push_back(l);
    }
    void Bar(const std::string& t, float f, uint32_t colour = HUD_TEXT){
        CardLine l;
        l.type = CARD_BAR;
        l.text = t;
        l.bar = std::max(0.0f,std::min(1.0f,f));
        l.colour = colour;
        lines.push_back(l);
    }
};

std::string Format(const char* fmt, ...) __attribute__((format(printf,1,2)));
std::string Format(const char* fmt, ...){
    char buf[256];
    va_list args;
    va_start(args,fmt);
    vsnprintf(buf,sizeof(buf),fmt,args);
    va_end(args);
    return buf;
}

std::string GoodsList(const EconomyState& e, uint32_t id){
    std::string s;
    for (int g = 0; g < GOOD_COUNT; g++){
        int n = EconomyStock(e,id,g);
        if (n){
            s += Format("%s%i %s",s.empty() ? "" : ", ",n,GoodName(g));
        }
    }
    return s;
}

void PersonCard(const ChasmWorld& w, const ZoneState& z, const EconomyState& e, const EconomyWorker& k, Card& c){
    (void)w;
    c.title = EconomyPersonName(k);
    c.Text(Format("Age %i",k.age),HUD_TEXT_DIM);
    if (k.house == 0){
        c.Text("Nowhere to live",HUD_AMBER);
    }else if (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP){
        c.Text("Lives in the camp - waiting for a house",HUD_AMBER);
    }else{
        c.Text(Format("Lives in house %u",k.house));
    }
    if (k.job == WORKER_JOB_NONE){
        c.Text("No job: carries wood for the builders");
    }else if (k.job == WORKER_JOB_BUILDER){
        c.Text(Format("Builder at the builders' hut %u: carries wood to sites and builds",k.building));
    }else{
        c.Text(Format("%s at %s %u",JobTitle(k.job),KindTitle(z.buildings[k.building].kind),k.building));
    }
    std::string doing = Doing(k);
    if (k.carry > 0){
        doing += Format(", with %i %s",k.carry,GoodName(k.carry_good));
    }
    if (!doing.empty()){
        doing[0] = (char)toupper(doing[0]);
        c.Text(doing,HUD_TEXT_DIM);
    }
    /*
        His needs (needs_plan.md): health - which is also how fed he is - and water, amber once a day or two
        is all that is left. Dry, the water line says what it costs him.
    */
    float health = (float)k.health / NEEDS_HEALTH_FULL;
    c.Bar(Format("Health %i%%",(int)std::ceil(health * 100.0f)),health,
          (k.health <= 2 * CALENDAR_DAY_TICKS) ? HUD_AMBER : HUD_TEXT);
    float water = (float)k.water / NEEDS_WATER_FULL;
    if (EconomyDry(k)){
        c.Bar("Water 0% - thirsty, losing health fast",0.0f,HUD_AMBER);
    }else{
        c.Bar(Format("Water %i%%",(int)std::ceil(water * 100.0f)),water,
              (k.water <= CALENDAR_DAY_TICKS) ? HUD_AMBER : HUD_TEXT);
    }
    for (int sk = 0; sk < SKILL_COUNT; sk++){
        std::string name = SkillName(sk);
        name[0] = (char)toupper(name[0]);
        c.Bar(Format("%s %i",name.c_str(),(int)k.skills[sk]),k.skills[sk] / 10.0f);
    }
    (void)e;
}

void BuildingCard(const ChasmWorld& w, const ZoneState& z, const EconomyState* e, uint32_t id, Card& c){
    const ZoneBuilding& b = z.buildings[id];
    ZoneBuildingInfo info = ZoneBuildingFigures(w,z,id);
    c.title = KindTitle(b.kind);
    int site_wood = (b.kind == ZONE_KIND_FIELD) ? 0 : EconomySiteWood(w,z,id);
    if (site_wood > 0){
        //Under construction: how far along, and who is carrying for it.
        int brought = e ? std::min(site_wood,EconomySiteBrought(*e,id)) : 0;
        c.title += info.storeys > 0 ? " - building on" : " - being built";
        c.Bar(Format("Wood %i of %i",brought,site_wood),site_wood ? (float)brought / site_wood : 0.0f,HUD_AMBER);
        c.Text(Format("%i storey%s to build%s",info.site_storeys,info.site_storeys == 1 ? "" : "s",
                      info.storeys ? Format(", %i standing",info.storeys).c_str() : ""),HUD_TEXT_DIM);
        if (e){
            int carriers = 0;
            for (const EconomyWorker& k : e->workers){
                carriers += (k.site == id) ? 1 : 0;
            }
            c.Text(carriers ? Format("%i carrying for it",carriers) : std::string("Waiting for someone free to carry"),
                   HUD_TEXT_DIM);
        }
        //The player's say over the queue (construction_plan.md): this one before every other site.
        CardLine first;
        first.type = CARD_TOGGLE;
        first.text = "Build first";
        first.chips_on = b.f_priority;
        c.lines.push_back(first);
    }
    if (!e){
        return;
    }
    if (b.kind == ZONE_KIND_HOUSE || b.kind == ZONE_KIND_CAMP){
        int holds = (id < e->capacity.size()) ? e->capacity[id] : 0;
        int living = 0;
        for (const EconomyWorker& k : e->workers){
            living += (k.house == id) ? 1 : 0;
        }
        if (b.kind == ZONE_KIND_CAMP){
            c.Text(Format("%i tents, %i living here",holds,living));
            if (living > 0){
                c.Text(Format("%i need%s a house",living,living == 1 ? "s" : ""),HUD_AMBER);
            }
        }else if (info.storeys > 0){
            c.Text(Format("Holds %i, %i living here",holds,living));
        }
        for (const EconomyWorker& k : e->workers){
            if (k.house == id){
                c.Text(Format("  %s, %i - %s, %s",EconomyPersonName(k).c_str(),k.age,JobTitle(k.job),Doing(k)),
                       EconomyIndoors(k) ? HUD_TEXT_DIM : HUD_TEXT);
            }
        }
        //Its family's own food, water and firewood (ECONOMY_HOUSE_KEEPS), which they eat and drink from.
        int room = (id < e->room.size()) ? e->room[id] : 0;
        if (b.kind == ZONE_KIND_HOUSE && room > 0){
            int held = EconomyStockTotal(*e,id);
            c.Bar(Format("Food, water and firewood %i of %i",held,room),(float)held / room,HUD_TEXT_DIM);
            std::string larder = GoodsList(*e,id);
            if (!larder.empty()){
                c.Text("  " + larder,HUD_TEXT_DIM);
            }
        }
    }
    if (b.kind == ZONE_KIND_STORE && info.storeys > 0){
        int room = (id < e->room.size()) ? e->room[id] : 0;
        int held = EconomyStockTotal(*e,id);
        c.Bar(Format("Holds %i of %i",held,room),room ? (float)held / room : 0.0f);
        for (int g = 0; g < GOOD_COUNT; g++){
            int n = EconomyStock(*e,id,g);
            if (n){
                c.Bar(Format("  %s %i",GoodName(g),n),room ? (float)n / room : 0.0f,HUD_TEXT_DIM);
            }
        }
        bool f_attached = id < e->attached.size() && e->attached[id] != 0;
        CardLine chips;
        chips.type = CARD_CHIPS;
        chips.chips_on = f_attached ? EconomyAccepts(*e,id) : b.allow;
        chips.f_locked = f_attached;
        c.Text(f_attached ? "Takes (next to a workplace: its goods only):" : "Takes:",HUD_TEXT_DIM);
        c.lines.push_back(chips);
    }
    if (b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_FIELD){
        if (b.kind == ZONE_KIND_FIELD){
            std::string crop = ZoneCropName(b.crop);
            crop[0] = (char)toupper(crop[0]);
            c.Bar(Format("%s, %.0f%% grown",crop.c_str(),EconomyFieldGrowth(*e,z,id) * 100.0f),EconomyFieldGrowth(*e,z,id));
        }
        bool f_worked = false;
        for (const EconomyWorker& k : e->workers){
            if (k.building == id){
                f_worked = true;
                int skill = EconomyJobSkill(k.job);
                c.Text(Format("%s (%s %i) - %s",EconomyPersonName(k).c_str(),SkillName(skill),(int)k.skills[skill],Doing(k)));
            }
        }
        if (!f_worked && info.storeys > 0){
            c.Text("Nobody works here - it needs a free adult with a house",HUD_AMBER);
        }
        //His woodpile is on a lot beside the hut: with none standing there, he fells nothing.
        bool f_no_lot = b.kind == ZONE_KIND_WOODCUTTER && (id >= e->pile_plot.size() || e->pile_plot[id] < 0);
        if (f_no_lot){
            c.Text("No lot beside the hut: nowhere to pile wood",HUD_AMBER);
        }
        std::string waiting = GoodsList(*e,id);
        if (!waiting.empty()){
            c.Text(b.kind == ZONE_KIND_WOODCUTTER ? "On the woodpile: " + waiting : "Waiting to be carried: " + waiting,HUD_TEXT_DIM);
        }
    }
    if (b.kind == ZONE_KIND_CAMP){
        std::string supplies = GoodsList(*e,id);
        if (!supplies.empty()){
            c.Text("Supplies: " + supplies,HUD_TEXT_DIM);
        }
    }
    if (b.kind == ZONE_KIND_WINCH && info.storeys > 0){
        c.Text("Lowers to the balcony below",HUD_TEXT_DIM);
    }
    //Its builders (builders_plan.md): its places filled before any other workplace's.
    if (b.kind == ZONE_KIND_BUILDERS){
        int places = (id < e->places.size()) ? e->places[id] : 0;
        int working = 0;
        for (const EconomyWorker& k : e->workers){
            working += (k.building == id) ? 1 : 0;
        }
        c.Text(Format("%i of %i builder%s",working,places,places == 1 ? "" : "s"),(working < places) ? HUD_AMBER : HUD_TEXT);
        if (info.storeys == 0){
            c.Text("Its first builder builds it",HUD_TEXT_DIM);
        }
        for (const EconomyWorker& k : e->workers){
            if (k.building == id){
                c.Text(Format("  %s (strength %i) - %s",EconomyPersonName(k).c_str(),(int)k.skills[SKILL_STRENGTH],Doing(k)));
            }
        }
        if (working < places){
            c.Text("Too few people: every place here is filled before any other workplace's",HUD_TEXT_DIM);
        }
    }
}

bool MakeCard(const PlayPick& p, const ChasmWorld& w, const ZoneState& z, const EconomyState* e, Card& c){
    if (p.kind == PLAY_PICK_PERSON && e){
        const EconomyWorker* k = PersonById(*e,p.id);
        if (k){
            PersonCard(w,z,*e,*k,c);
            return true;
        }
    }else if (p.kind == PLAY_PICK_BUILDING && p.id < z.buildings.size() && z.buildings[p.id].size > 0){
        BuildingCard(w,z,(e && e->world == z.world) ? e : NULL,p.id,c);
        return true;
    }
    return false;
}

/*
    A building alone, for its outline: the zones with every other building taken out, so its cells are
    drawn as if it stood by itself - its own silhouette, walls closed where it meets a neighbour. A field
    is a slab over its cells instead: its crops would outline every plant.
*/
void BuildSelectionMesh(const ChasmWorld& w, const ZoneState& z, uint32_t id, std::vector<vertex>& out){
    out.clear();
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].size <= 0){
        return;
    }
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    if (z.buildings[id].kind == ZONE_KIND_FIELD){
        for (size_t c = 0; c < z.field.size(); c++){
            if (z.field[c] != id){
                continue;
            }
            for (int q = (int)c * 4; q < (int)c * 4 + 4; q++){
                const GridQuad& quad = g.fine.quads[q];
                vec3 p[4];
                for (int k = 0; k < 4; k++){
                    vec2 at = g.fine.pos[quad.v[k]];
                    p[k] = vec3(at.x,w.terrain->GroundHeight(at,w.terrain->Height(quad.v[k])) + FIELD_OUTLINE_LIFT,at.y);
                }
                MeshQuad(out,p[0],p[1],p[2],p[3],vec3(0.0f,1.0f,0.0f),PAL_FIELD);
            }
        }
        return;
    }
    ZoneState only;
    only.world = z.world;
    only.buildings = z.buildings;
    only.building.assign(z.building.size(),0);
    only.storeys.assign(z.storeys.size(),0);
    only.standing.assign(z.standing.size(),0);
    only.ground = z.ground;
    only.ground_built = z.ground_built;
    only.field.assign(z.field.size(),0);
    std::vector<int> quads;
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] != id){
            continue;
        }
        only.building[v] = id;
        only.storeys[v] = z.storeys[v];
        only.standing[v] = z.standing[v];
        for (int i = 0; i < picker.PlotQuadCount((int)v); i++){
            quads.push_back(picker.PlotQuadCorner((int)v,i) / 4);
        }
    }
    std::sort(quads.begin(),quads.end());
    quads.erase(std::unique(quads.begin(),quads.end()),quads.end());
    for (int q : quads){
        BuildHouseCell(w,only,q,out);
    }
}

/*
    How high what stands on a plot reaches, by how it is drawn (BuildingMesh.cpp): a house body on its
    footing rounded to a half-storey, its storeys - a site's planned ones too, so its scaffold is picked
    - and some of its roof; a tent; a winch's gantry. Below the ground (no building): -1e30.
*/
float ShapeTop(const ChasmWorld& w, const ZoneState& z, int plot){
    if (!z.building[plot]){
        return -1e30f;
    }
    float ground = w.terrain->ground[plot];
    switch (z.KindOf(plot)){
        case ZONE_KIND_CAMP:    return ground + SHAPE_TENT;
        case ZONE_KIND_WINCH:   return ground + SHAPE_WINCH;
        case ZONE_KIND_BRIDGE:  return terrain_levels[w.terrain->level[plot]].height + ZONE_BRIDGE_DECK + 0.45f;
        default:{
            const float half = ZONE_STOREY_HEIGHT * 0.5f;
            float foot = (float)std::lround(ground / half) * half;
            return foot + z.storeys[plot] * ZONE_STOREY_HEIGHT + SHAPE_ROOF;
        }
    }
}

/*
    The building the ray through a pixel meets first, BY ITS SHAPE: sampled from well above the ground
    the cursor is over down to that ground, each sample asking the plot under it whether what stands
    there reaches that high. So a tall house is picked by its roof, not by the ground behind it. 0 if
    the ray reaches the ground first.
*/
uint32_t BuildingAlongRay(const ChasmWorld& w, const ZoneState& z, const ray& r, const GridPick& ground, int* plot_out){
    if (!ground.f_hit || r.direction.y >= -1e-4f){
        return 0;
    }
    float ground_y = w.terrain->GroundHeight(ground.at,w.terrain->Height(ground.plot));
    float t1 = (ground_y - r.origin.y) / r.direction.y;
    float t0 = std::max(0.0f,(ground_y + SHAPE_PICK_ABOVE - r.origin.y) / r.direction.y);
    int last = -1;
    float top = -1e30f;
    for (float t = t0; t <= t1 + SHAPE_PICK_STEP; t += SHAPE_PICK_STEP){
        vec3 p = r.origin + r.direction * t;
        GridPick at = w.picker->Pick(vec2(p.x,p.z));
        if (!at.f_hit){
            continue;
        }
        if (at.plot != last){
            last = at.plot;
            top = ShapeTop(w,z,at.plot);
        }
        if (p.y <= top){
            *plot_out = at.plot;
            return z.building[at.plot];
        }
    }
    return 0;
}

}

//--- Picking (physics thread) ----------------------------------------------------------------------

/*
    What is under the cursor in play mode: a person first - the nearest on screen within PERSON_PICK_PX
    of his middle, out of doors - since he stands on a plot that would otherwise win; then the building
    or field on the plot under the cursor.
*/
PlayPick ApplicationChasm::PlayPickUnder(int2 px, const GridPick& hover){
    PlayPick out;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return out;
    }
    float s = std::max(0.5f,(float)main_window->height / 810.0f);
    if (e && e->world == w){
        float best = PERSON_PICK_PX * s;
        vec2 at((float)px.x,(float)px.y);
        for (const EconomyWorker& k : e->workers){
            if (EconomyIndoors(k)){
                continue;
            }
            vec2 sp;
            if (ScreenOf(main_scene->camera,(float)main_window->width,(float)main_window->height,
                         FeetOf(*w,k) + vec3(0.0f,PERSON_MIDDLE,0.0f),sp)){
                float d = (sp - at).length();
                if (d < best){
                    best = d;
                    out.kind = PLAY_PICK_PERSON;
                    out.id = k.id;
                }
            }
        }
        if (out.kind != PLAY_PICK_NONE){
            return out;
        }
    }
    //A building by its shape first: its roof, its walls, a tent - then whatever is on the ground under the cursor.
    if (z && z->world == w && !z->building.empty()){
        ray r = main_scene->camera->GetPixelRay(px);
        int plot = -1;
        uint32_t b = BuildingAlongRay(*w,*z,r,hover,&plot);
        if (b){
            out.kind = PLAY_PICK_BUILDING;
            out.id = b;
            out.plot = plot;
            return out;
        }
    }
    if (hover.f_hit && z && z->world == w && hover.plot >= 0 && hover.plot < (int)z->building.size()){
        uint32_t b = z->building[hover.plot];
        if (!b && hover.coarse_quad >= 0 && hover.coarse_quad < (int)z->field.size()){
            b = z->field[hover.coarse_quad];
        }
        if (b){
            out.kind = PLAY_PICK_BUILDING;
            out.id = b;
            out.plot = hover.plot;
        }
    }
    return out;
}

//The topmost overlay place under the pixel, if any. Any thread.
bool ApplicationChasm::OverlayHitAt(int2 px, OverlayHit* hit){
    std::lock_guard<std::mutex> lock(pick_mutex);
    vec2 p((float)px.x,(float)px.y);
    for (size_t i = overlay_hits.size(); i-- > 0;){
        const OverlayHit& h = overlay_hits[i];
        if (p.x >= h.a.x && p.x <= h.b.x && p.y >= h.a.y && p.y <= h.b.y){
            //A button inside a panel: the button, wherever the panel came in the list.
            *hit = h;
            for (size_t j = i; j-- > 0;){
                const OverlayHit& o = overlay_hits[j];
                if (o.action != OVERLAY_HIT_BLOCK && h.action == OVERLAY_HIT_BLOCK &&
                    p.x >= o.a.x && p.x <= o.b.x && p.y >= o.a.y && p.y <= o.b.y){
                    *hit = o;
                }
            }
            return true;
        }
    }
    return false;
}

/*
    PHYSICS THREAD, from UpdatePick in play mode, before any painting: the overlay takes the mouse
    where it is drawn (a press on it does what it says and nothing beneath is picked or painted); a
    right click - pressed and let go without dragging, which would be a pan - puts a tool down, or
    with none clears the selection; and with no tool the hover and a click's selection.
*/
void ApplicationChasm::UpdatePlayPick(GridPick& hover, int2 px, bool& f_over_scene, bool& f_clicked){
    InputController* input = main_scene->inputcontroller;
    OverlayHit hit;
    if (f_over_scene && OverlayHitAt(px,&hit)){
        f_over_scene = false;
        hover = GridPick();
        if (f_clicked){
            if (hit.action == OVERLAY_HIT_TOOL){
                paint_tool = (paint_tool == hit.value) ? CHASM_TOOL_SELECT : hit.value;
                pick_version++;
            }else if (hit.action == OVERLAY_HIT_CLOSE){
                std::lock_guard<std::mutex> lock(pick_mutex);
                play_selected = PlayPick();
                play_pick_version++;
            }else if (hit.action == OVERLAY_HIT_STORE_ALLOW || hit.action == OVERLAY_HIT_SITE_FIRST){
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()){
                    int plot = (sel.plot >= 0 && sel.plot < (int)z->building.size() && z->building[sel.plot] == sel.id)
                               ? sel.plot : FirstPlotOf(*z,sel.id);
                    if (hit.action == OVERLAY_HIT_STORE_ALLOW){
                        uint8_t mask = z->buildings[sel.id].allow ^ (uint8_t)GOOD_BIT(hit.value);
                        SubmitZone(ZONE_OP_STORE_ALLOW,plot,mask);
                    }else{
                        SubmitZone(ZONE_OP_SITE_PRIORITY,plot,z->buildings[sel.id].f_priority ? 0 : 1);
                    }
                }
            }
        }
        f_clicked = false;
    }
    //The right button: a click, not a drag.
    if (input->WasKeyPressed(INPUT_CLICK_RIGHT) && f_over_scene){
        right_press_px = px;
        f_right_press = true;
    }
    if (input->WasKeyReleased(INPUT_CLICK_RIGHT) && f_right_press){
        f_right_press = false;
        if (std::abs(px.x - right_press_px.x) <= RIGHT_CLICK_SLOP && std::abs(px.y - right_press_px.y) <= RIGHT_CLICK_SLOP){
            if (paint_tool != CHASM_TOOL_SELECT){
                paint_tool = CHASM_TOOL_SELECT;
                pick_version++;
            }else{
                std::lock_guard<std::mutex> lock(pick_mutex);
                if (play_selected.kind != PLAY_PICK_NONE){
                    play_selected = PlayPick();
                    play_pick_version++;
                }
            }
        }
    }
    PlayPick under;
    if (paint_tool == CHASM_TOOL_SELECT && f_over_scene){
        under = PlayPickUnder(px,hover);
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (under != play_hover){
        play_hover = under;
        play_pick_version++;
    }
    //A click on nothing - bare ground - clears the selection.
    if (paint_tool == CHASM_TOOL_SELECT && f_clicked && f_over_scene && under != play_selected){
        play_selected = under;
        play_pick_version++;
    }else if (paint_tool == CHASM_TOOL_SELECT && f_clicked && f_over_scene){
        play_selected.plot = under.plot;
    }
}

//--- The outlines (render thread) ------------------------------------------------------------------

/*
    RENDER THREAD, from PreRender. The buildings' outline-only objects - rebuilt when what they hold, or
    the zones under it, change - and every person's outline, set each frame (cheap: a colour). Off in
    debug mode, where the line mesh is the cursor.
*/
void ApplicationChasm::UploadSelection(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!select_outline[0]){
        main_scene->AtTickBoundary([&](){
            for (int i = 0; i < SELECT_OUTLINES; i++){
                Object* o = new Object();
                o->name = "Selection outline " + std::to_string(i);
                o->SetMesh(new Mesh());
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetCastsShadow(false);
                o->SetVisibility(false);
#if CHASM_OUTLINE_READY
                o->SetOutlineOnly(true);
#endif
                main_scene->AddObject(o);
                select_outline[i] = o;
            }
        });
    }
    PlayPick hover;
    PlayPick sel;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        hover = play_hover;
        sel = play_selected;
    }
    bool f_play = PlayMode();
    bool f_ready = w && z && z->world == w;
    //What each slot outlines: the selected building, the hovered one; then what belongs with the selection.
    uint32_t want[SELECT_OUTLINES] = {0,0,0,0};
    vec4 colour[SELECT_OUTLINES] = {SELECT_COLOUR,HOVER_COLOUR,RELATED_COLOUR,RELATED_COLOUR};
    RelatedPlaces related_places;
    if (f_play && f_ready){
        if (sel.kind == PLAY_PICK_BUILDING){
            want[0] = sel.id;
        }
        if (hover.kind == PLAY_PICK_BUILDING && hover != sel){
            want[1] = hover.id;
        }
        if (e && e->world == w){
            FindRelatedPlaces(sel,*z,*e,related_places);
        }
    }
    std::vector<vertex> verts;
    /*
        Slots 2 and 3: every related building in one mesh, and every related ground plot in another - one
        outline round the lot, however many plots - rebuilt when the list or the zones change.
    */
    for (int i = 2; i < SELECT_OUTLINES; i++){
        Object* o = select_outline[i];
        if (!o){
            continue;
        }
        bool f_ground = (i == 3);
        bool f_any = f_ground ? !related_places.plots.empty() : !related_places.buildings.empty();
        if (!f_any){
            o->SetVisibility(false);
            if (f_ground){
                select_related_plots.clear();
            }else{
                select_related_buildings.clear();
            }
            continue;
        }
        bool f_same = f_ground ? related_places.plots == select_related_plots
                               : related_places.buildings == select_related_buildings;
        if (!f_same || z->version != select_outline_zones[i]){
            select_outline_zones[i] = z->version;
            verts.clear();
            if (f_ground){
                select_related_plots = related_places.plots;
                for (int v : related_places.plots){
                    BuildPlotTile(*w,v,FIELD_OUTLINE_LIFT,verts);
                }
            }else{
                select_related_buildings = related_places.buildings;
                std::vector<vertex> one;
                for (uint32_t id : related_places.buildings){
                    BuildSelectionMesh(*w,*z,id,one);
                    verts.insert(verts.end(),one.begin(),one.end());
                }
            }
            if (verts.empty()){
                o->SetVisibility(false);
                continue;
            }
            o->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
        }
        SetOutlineOf(o,colour[i]);
        o->SetVisibility(CHASM_OUTLINE_READY != 0);
    }
    for (int i = 0; i < 2; i++){
        Object* o = select_outline[i];
        if (!o){
            continue;
        }
        if (want[i] == 0){
            o->SetVisibility(false);
            select_outline_id[i] = 0;
            continue;
        }
        if (want[i] != select_outline_id[i] || z->version != select_outline_zones[i]){
            select_outline_id[i] = want[i];
            select_outline_zones[i] = z->version;
            BuildSelectionMesh(*w,*z,want[i],verts);
            if (verts.empty()){
                o->SetVisibility(false);
                select_outline_id[i] = 0;
                continue;
            }
            o->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
        }
        SetOutlineOf(o,colour[i]);
        o->SetVisibility(CHASM_OUTLINE_READY != 0);
    }
    //The people: the selected one, the one under the cursor, and those who belong with the selection.
    std::vector<uint32_t> related;
    if (f_play && f_ready && e && e->world == w){
        RelatedPeople(sel,*z,*e,related);
    }
    for (size_t i = 0; i < worker_objects.size(); i++){
        vec4 c(0.0f,0.0f,0.0f,0.0f);
        if (f_play && e && i < e->workers.size()){
            uint32_t id = e->workers[i].id;
            if (sel.kind == PLAY_PICK_PERSON && sel.id == id){
                c = SELECT_COLOUR;
            }else if (std::find(related.begin(),related.end(),id) != related.end()){
                c = RELATED_COLOUR;
            }else if (hover.kind == PLAY_PICK_PERSON && hover.id == id){
                c = HOVER_COLOUR;
            }
        }
        SetOutlineOf(worker_objects[i],c);
    }
}

//--- The card and the pins (render thread) ---------------------------------------------------------

void ApplicationChasm::DrawSelectionOverlay(float s){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !z || z->world != w){
        return;
    }
    const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
    PlayPick sel;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
    }
    float width = (float)main_window->width;
    float height = (float)main_window->height;
    /*
        PINNED: everyone who belongs with the selection, and the selected person himself - his badge on a
        stem in the selection's colour, kept on screen at its edge when he is off it, so he can be found.
    */
    std::vector<uint32_t> pinned;
    if (ep){
        RelatedPeople(sel,*z,*ep,pinned);
        if (sel.kind == PLAY_PICK_PERSON){
            pinned.push_back(sel.id);
        }
    }
    //Where a pin over a world point goes: kept on screen, clear of the build bar along the bottom, with
    //whether it had to be (then it has no stem). False behind the camera.
    auto pin_spot = [&](const vec3& stem, const vec3& top, float rise, vec2& head, vec2& dot, bool& f_off){
        vec2 tip;
        if (!ScreenOf(main_scene->camera,width,height,top,tip)){
            return false;
        }
        ScreenOf(main_scene->camera,width,height,stem,head);
        float margin = 18.0f * s;
        float bottom = height - 64.0f * s;
        f_off = tip.x < margin || tip.y < margin || tip.x > width - margin || tip.y > bottom;
        dot = vec2(std::max(margin,std::min(width - margin,tip.x)),std::max(margin,std::min(bottom,tip.y - rise * s)));
        return true;
    };
    auto person_spot = [&](const EconomyWorker& k, vec2& head, vec2& dot, bool& f_off){
        vec3 feet = FeetOf(*w,k);
        return pin_spot(feet + vec3(0.0f,PERSON_PIN * 0.75f,0.0f),feet + vec3(0.0f,PERSON_PIN,0.0f),16.0f,head,dot,f_off);
    };

    /*
        THE BADGES (the user's, 2026-10-07): one over everyone out of doors, where a pin would be - so a
        selection adds a stem and its colour and nothing moves. A pinned badge is kept on screen at its
        edge; the rest only drawn where they are on it, or a crowd at the edges would bury the ones that
        are there to be found. A family indoors short of water gets a drop over its roof. Roofs first, then
        everyone, then the pinned, so those stay on top - the selected person's last of all.
    */
    if (ep){
        struct Roof{
            uint32_t house;
            vec3 top;
            bool f_dry;
        };
        std::vector<Roof> roofs;
        for (const EconomyWorker& k : ep->workers){
            if (!EconomyIndoors(k) || k.water >= THIRST_MARK_WATER){
                continue;
            }
            bool f_dry = EconomyDry(k);
            auto r = std::find_if(roofs.begin(),roofs.end(),[&](const Roof& o){ return o.house == k.house; });
            if (r != roofs.end()){
                r->f_dry = r->f_dry || f_dry;
                continue;
            }
            //He is at his door, on the house's first plot (Economy::HomePlot) - its roof is over him.
            GridPick pick = w->picker->Pick(k.pos);
            float top = (pick.f_hit && z->building[pick.plot] == k.house) ? ShapeTop(*w,*z,pick.plot)
                                                                          : FeetOf(*w,k).y + PERSON_PIN;
            roofs.push_back(Roof{k.house,vec3(k.pos.x,top + THIRST_MARK_ROOF,k.pos.y),f_dry});
        }
        for (const Roof& r : roofs){
            vec2 head, dot;
            bool f_off = false;
            if (pin_spot(r.top,r.top,20.0f,head,dot,f_off) && !f_off){
                overlay->AddLine(head,dot,2.0f * s,HudFaded(HUD_WATER,220));
                DrawRoofThirst(overlay,dot,s,r.f_dry);
            }
        }
        for (const EconomyWorker& k : ep->workers){
            if (EconomyIndoors(k) || std::find(pinned.begin(),pinned.end(),k.id) != pinned.end()){
                continue;
            }
            //On a stem in the disc's colour: floating free, a crowd's badges could be anybody's.
            vec2 head, dot;
            bool f_off = false;
            if (person_spot(k,head,dot,f_off) && !f_off){
                overlay->AddLine(head,dot,2.0f * s,HudFaded(HUD_PANEL,220));
                DrawPersonBadge(overlay,dot,s,k,0);
            }
        }
        for (uint32_t id : pinned){
            const EconomyWorker* k = PersonById(*ep,id);
            if (!k || EconomyIndoors(*k)){
                continue;
            }
            vec2 head, dot;
            bool f_off = false;
            if (!person_spot(*k,head,dot,f_off)){
                continue;
            }
            uint32_t colour = (sel.kind == PLAY_PICK_PERSON && sel.id == id) ? HUD_SELECT : HUD_AMBER;
            if (!f_off){
                overlay->AddLine(head,dot,2.0f * s,HudFaded(colour,220));
            }
            DrawPersonBadge(overlay,dot,s,*k,colour);
        }
    }

    //THE CARD, on the right under the date's level.
    Card card;
    if (!MakeCard(sel,*w,*z,ep,card)){
        return;
    }
    const float pad = 14.0f * s;
    const float title_size = 19.0f * s;
    const float text_size = 14.0f * s;
    const float line_h = 20.0f * s;
    const float bar_h = 26.0f * s;
    const float chips_h = 30.0f * s;
    float card_w = CARD_W * s;
    float h = pad + 26.0f * s;
    for (const CardLine& l : card.lines){
        h += (l.type == CARD_BAR) ? bar_h : (l.type == CARD_CHIPS || l.type == CARD_TOGGLE) ? chips_h : line_h;
        //A long line widens the card rather than running off it.
        card_w = std::max(card_w,overlay->MeasureText(l.text.c_str(),text_size).x + pad * 2.0f);
    }
    card_w = std::min(card_w,width * 0.45f);
    h += pad * 0.5f;
    vec2 c0(width - 16.0f * s - card_w,128.0f * s);
    vec2 c1(width - 16.0f * s,c0.y + h);
    overlay->AddRect(c0,c1,10.0f * s,HUD_PANEL);
    overlay->AddRect(vec2(c0.x,c0.y),vec2(c0.x + 4.0f * s,c1.y),2.0f * s,HUD_SELECT);
    overlay_hits_drawing.push_back(OverlayHit{c0,c1,OVERLAY_HIT_BLOCK,0});
    float y = c0.y + pad + 14.0f * s;
    overlay->AddText(card.title.c_str(),vec2(c0.x + pad,y),title_size,HUD_TEXT,UI_ALIGN_LEFT);
    //The close mark, top right.
    vec2 x0(c1.x - pad - 14.0f * s,c0.y + pad - 4.0f * s);
    vec2 x1(c1.x - pad + 2.0f * s,x0.y + 16.0f * s);
    overlay->AddLine(x0 + vec2(3.0f * s,3.0f * s),x1 - vec2(3.0f * s,3.0f * s),2.0f * s,HUD_TEXT_DIM);
    overlay->AddLine(vec2(x1.x - 3.0f * s,x0.y + 3.0f * s),vec2(x0.x + 3.0f * s,x1.y - 3.0f * s),2.0f * s,HUD_TEXT_DIM);
    overlay_hits_drawing.push_back(OverlayHit{x0 - vec2(4.0f * s,4.0f * s),x1 + vec2(4.0f * s,4.0f * s),OVERLAY_HIT_CLOSE,0});
    y += 12.0f * s;
    //A chip: lit while on; a click turns it over (not while it is fixed). Returns where the next one goes.
    auto chip = [&](float x, const char* name, bool f_on, bool f_locked, int action, int value) -> float {
        float tw = overlay->MeasureText(name,text_size).x;
        vec2 k0(x,y - 24.0f * s);
        vec2 k1(x + tw + 14.0f * s,y - 4.0f * s);
        uint8_t alpha = f_locked ? 110 : 235;
        if (f_on){
            overlay->AddRect(k0,k1,8.0f * s,HudFaded(HUD_AMBER,alpha));
        }else{
            overlay->AddRectOutline(k0,k1,8.0f * s,1.5f * s,HudFaded(HUD_TEXT,alpha / 2));
        }
        overlay->AddText(name,vec2(x + 7.0f * s,y - 9.0f * s),text_size,f_on ? HudFaded(HUD_PANEL,255) : HudFaded(HUD_TEXT,alpha),
                         UI_ALIGN_LEFT);
        if (!f_locked){
            overlay_hits_drawing.push_back(OverlayHit{k0,k1,action,value});
        }
        return k1.x + 6.0f * s;
    };
    for (const CardLine& l : card.lines){
        if (l.type == CARD_TEXT){
            y += line_h;
            overlay->AddText(l.text.c_str(),vec2(c0.x + pad,y - 5.0f * s),text_size,l.colour,UI_ALIGN_LEFT);
        }else if (l.type == CARD_BAR){
            y += bar_h;
            overlay->AddText(l.text.c_str(),vec2(c0.x + pad,y - 11.0f * s),text_size,l.colour,UI_ALIGN_LEFT);
            vec2 b0(c0.x + pad,y - 6.0f * s);
            vec2 b1(c1.x - pad,y - 2.0f * s);
            overlay->AddRect(b0,b1,2.0f * s,HudFaded(HUD_TEXT,45));
            overlay->AddRect(b0,vec2(b0.x + (b1.x - b0.x) * l.bar,b1.y),2.0f * s,HudFaded(l.colour,210));
        }else if (l.type == CARD_TOGGLE){
            y += chips_h;
            chip(c0.x + pad,l.text.c_str(),l.chips_on != 0,l.f_locked,OVERLAY_HIT_SITE_FIRST,0);
        }else{
            //A chip per good: lit if the store takes it.
            y += chips_h;
            float x = c0.x + pad;
            for (int g = 0; g < GOOD_COUNT; g++){
                x = chip(x,GoodName(g),(l.chips_on & GOOD_BIT(g)) != 0,l.f_locked,OVERLAY_HIT_STORE_ALLOW,g);
            }
        }
    }
}

//--- The tool --------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterSelectTools(){
    MCPServer::Get()->RegisterTool("chasm_select",
        "Play mode's selection (docs/selection_plan.md) - what a click selects there, without the mouse. "
        "op select: the building at x,z (or by `building` id), or a person by `person` id; op clear; op under "
        "px,py: what the cursor would pick at that pixel (and the plot and building the ground pick alone gives); op "
        "status (default): the selection and hover, the card as text lines, and the people pinned with it. "
        "View state only - nothing recorded. The card and outlines show in play mode (chasm_view play). "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"building",{{"type","integer"}}},
                {"person",{{"type","integer"}}},
                {"px",{{"type","integer"}}},
                {"py",{{"type","integer"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            std::shared_ptr<const ZoneState> z = GetZones();
            std::shared_ptr<const EconomyState> e = GetEconomy();
            if (!w || !z || z->world != w){
                return json{{"error","no world"}};
            }
            std::string op = args.value("op",std::string("status"));
            //What the cursor would pick at a pixel - a person, a building by its shape, the ground's -
            //so picking can be checked without a mouse.
            if (op == "under" && args.contains("px") && args.contains("py") && args["px"].is_number() && args["py"].is_number()){
                int2 px(args["px"].get<int>(),args["py"].get<int>());
                json out;
                main_scene->AtTickBoundary([&](){
                    GridPick ground = PickUnderPixel(px);
                    PlayPick p = PlayPickUnder(px,ground);
                    const char* kinds[] = {"none","building","person"};
                    out = json{{"kind",kinds[std::max(0,std::min(2,p.kind))]},{"id",p.id},{"plot",p.plot},
                               {"ground_plot",ground.f_hit ? ground.plot : -1},
                               {"ground_building",(ground.f_hit && ground.plot < (int)z->building.size()) ? z->building[ground.plot] : 0}};
                });
                return json{{"under",out}};
            }
            if (op == "clear" || op == "select"){
                PlayPick p;
                if (op == "select"){
                    //Checked as numbers first: this build has no exceptions, so a get<> on a null aborts the app.
                    if (args.contains("person") && args["person"].is_number_unsigned()){
                        p.kind = PLAY_PICK_PERSON;
                        p.id = args["person"].get<uint32_t>();
                    }else if (args.contains("building") && args["building"].is_number_unsigned()){
                        p.kind = PLAY_PICK_BUILDING;
                        p.id = args["building"].get<uint32_t>();
                        p.plot = FirstPlotOf(*z,p.id);
                        if (p.id >= z->buildings.size() || z->buildings[p.id].size <= 0){
                            return json{{"error","no such building"}};
                        }
                    }else if (args.contains("x") && args.contains("z") && args["x"].is_number() && args["z"].is_number()){
                        GridPick g = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                        if (g.f_hit){
                            uint32_t b = z->building[g.plot];
                            if (!b){
                                b = z->field[g.coarse_quad];
                            }
                            if (b){
                                p.kind = PLAY_PICK_BUILDING;
                                p.id = b;
                                p.plot = g.plot;
                            }
                        }
                    }
                    if (p.kind == PLAY_PICK_NONE){
                        return json{{"error","nothing to select there"}};
                    }
                }
                std::lock_guard<std::mutex> lock(pick_mutex);
                play_selected = p;
                play_pick_version++;
            }
            PlayPick sel, hover;
            {
                std::lock_guard<std::mutex> lock(pick_mutex);
                sel = play_selected;
                hover = play_hover;
            }
            auto pick_json = [](const PlayPick& p) -> json {
                const char* kinds[] = {"none","building","person"};
                return json{{"kind",kinds[std::max(0,std::min(2,p.kind))]},{"id",p.id},{"plot",p.plot}};
            };
            json result{{"selected",pick_json(sel)},{"hover",pick_json(hover)},{"play_mode",PlayMode()}};
            const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
            Card card;
            if (MakeCard(sel,*w,*z,ep,card)){
                json lines = json::array();
                for (const CardLine& l : card.lines){
                    if (l.type == CARD_CHIPS){
                        json takes = json::array();
                        for (int g = 0; g < GOOD_COUNT; g++){
                            if (l.chips_on & GOOD_BIT(g)){
                                takes.push_back(GoodName(g));
                            }
                        }
                        lines.push_back(json{{"chips",takes},{"locked",l.f_locked}});
                    }else if (l.type == CARD_TOGGLE){
                        lines.push_back(json{{"toggle",l.text},{"on",l.chips_on != 0}});
                    }else{
                        lines.push_back(l.type == CARD_BAR ? json{{"text",l.text},{"bar",l.bar}} : json(l.text));
                    }
                }
                result["card"] = json{{"title",card.title},{"lines",lines}};
            }
            if (ep){
                std::vector<uint32_t> related;
                RelatedPeople(sel,*z,*ep,related);
                result["pinned"] = related;
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
