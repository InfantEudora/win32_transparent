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
#include <chrono>
#include <cmath>
#include <ctype.h>
#include <thread>
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
    View state only: what reaches the simulation is a card's chips - recorded zone commands like the debug
    panel's - and ORDERS (defence_plan.md): a right click sending the selected person to a plot, and his
    card's Release, both recorded CHASM_CMD_ORDERs.

    Threads: the hover and the click are worked out on the physics thread (UpdatePick, which has the
    camera's ray); the outlines are rebuilt on the render thread (PreRender); the card and pins are
    drawn there too (DrawOverlay), which hands what the mouse can press back to UpdatePick.
*/

#define SELECT_COLOUR       vec4(1.00f,0.62f,0.16f,1.00f)   //HUD_SELECT: Blender's selection orange
#define RELATED_COLOUR      vec4(1.00f,0.90f,0.62f,0.85f)   //what belongs with the selection
#define HOVER_COLOUR        vec4(1.00f,0.95f,0.82f,0.50f)   //a building or crate under the cursor
#define PERSON_HOVER_COLOUR vec4(0.45f,0.78f,1.00f,1.00f)   //a person under the cursor, or his line on a card: light blue
#define HUD_PERSON_HOVER    UIColor(115,199,255,255)        //...the same, behind his line on a card or a list
#define PERSON_NEAR_PX      8.0f        //a person is picked within this many pixels (at 810 high) of the line up his figure
#define PERSON_HIDDEN_SLACK 0.75f       //...unless what the cursor is on stands this much nearer the eye than he does
#define PICK_READ_SLOP      4           //pixels the engine's readback may lag the cursor by and still be its answer
#define PERSON_PIN          1.55f       //world units above his feet where his pin points
#define CRATE_PICK_PX       12.0f       //a crate is picked within this many pixels (at 810 high) of its middle
#define CRATE_MIDDLE        0.2f        //...which is this far above the ground under it (crate_a stands 0.40)
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
#define SHAPE_WELL          1.9f        //a well's roof at its ridge (BuildingMesh.cpp, BuildWell)

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
#define ICON_BALL       UIColor(214,92,70,255)      //a child's ball
#define ICON_FRUIT      UIColor(226,96,84,255)      //a picker's berries (foraging_plan.md)
#define ICON_LEAF       UIColor(126,178,84,255)
#define ICON_ROPE       UIColor(206,188,146,255)    //a ropemaker's coil: rope's tan (HudGoodColour), lifted to read on the disc

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

//A stonecutter's pick (stone_plan.md): the axe's handle, and across its top a curved head pointed at both ends.
void DrawPickIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-5.0f,6.5f),p(2.0f,-4.5f),1.8f * s,ICON_WOOD);
    //An arc round a point down the handle, through its top: the head bows over the hand, as a pick's does.
    o->AddArc(p(-0.7f,-0.3f),5.3f * s,-0.55f,1.69f,2.2f * s,ICON_STEEL);
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
    A posted person's spear (defence_plan.md): the shaft leaning right, and a broad leaf of a blade - widest a
    third of the way up, to a point. A thin blade read as a slash across the disc, a "no" sign, at badge size.
*/
void DrawSpearIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-5.4f,6.4f),p(0.6f,-0.6f),2.0f * s,ICON_WOOD);
    vec2 base = p(-0.2f,0.2f);
    vec2 tip = p(5.4f,-6.6f);
    const int steps = 8;
    for (int i = 0; i < steps; i++){
        float t = (i + 0.5f) / steps;
        float width = 8.0f * std::min(1.0f,t * 2.5f) * (1.0f - t);     //a leaf: up to about 4.8 at t 0.4
        o->AddLine(base + (tip - base) * ((float)i / steps),base + (tip - base) * ((float)(i + 1) / steps),
                   std::max(0.8f,width) * s,ICON_STEEL);
    }
}

//An archer's bow (range_plan.md): its limb an arc, its string straight across, an arrow nocked on it.
void DrawBowIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddArc(p(-2.6f,0.0f),7.0f * s,-1.05f,1.05f,1.8f * s,ICON_WOOD);
    vec2 top = p(-2.6f + 7.0f * 0.5f,-7.0f * 0.866f);
    vec2 bottom = p(-2.6f + 7.0f * 0.5f,7.0f * 0.866f);
    o->AddLine(top,bottom,0.7f * s,HUD_TEXT_DIM);
    o->AddLine(p(0.9f,0.0f),p(-6.0f,0.0f),1.1f * s,ICON_WOOD);
    o->AddLine(p(-6.0f,0.0f),p(-4.4f,-1.4f),1.0f * s,ICON_STEEL);
    o->AddLine(p(-6.0f,0.0f),p(-4.4f,1.4f),1.0f * s,ICON_STEEL);
}

//A child's ball (lifeline_plan.md), with a pale band round it.
void DrawBallIcon(UIOverlay* o, const vec2& c, float s){
    float r = 4.6f * s;
    o->AddRect(c - vec2(r,r),c + vec2(r,r),r,ICON_BALL);
    o->AddArc(c,r * 0.62f,-0.6f,1.3f,1.5f * s,ICON_WOOD_END);
}

//A picker's berries (foraging_plan.md): three, in a cluster under a leaf.
void DrawBerriesIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-0.6f,-3.4f),p(4.2f,-6.0f),2.4f * s,ICON_LEAF);
    float r = 2.7f * s;
    for (const vec2& at : {p(-2.6f,1.8f),p(2.6f,1.8f),p(0.0f,-2.4f)}){
        o->AddRect(at - vec2(r,r),at + vec2(r,r),r,ICON_FRUIT);
    }
}

/*
    A ropemaker's coil of rope (goods_plan.md): two turns spiralling out from the middle, its loose end off to
    the bottom right. Without it he fell through to the jobless's log.
*/
void DrawCoilIcon(UIOverlay* o, const vec2& c, float s){
    auto at = [&](float a, float r){ return c + vec2(sinf(a),-cosf(a)) * (r * s); };
    const float a0 = 2.3561945f;            //so the last turn ends at half past four, where the end hangs off
    const float turns = 2.0f;
    const int steps = 32;
    vec2 last = at(a0,1.2f);
    for (int i = 1; i <= steps; i++){
        float t = (float)i / steps;
        vec2 next = at(a0 + t * turns * 6.2831853f,1.2f + 4.4f * t);
        o->AddLine(last,next,1.5f * s,ICON_ROPE);
        last = next;
    }
    o->AddLine(last,c + vec2(6.0f * s,6.6f * s),1.5f * s,ICON_ROPE);
}

//A baby swaddled: a pale face on a bundle - a baby's, and its mother's while she cares for it.
void DrawBabyIcon(UIOverlay* o, const vec2& c, float s){
    auto p = [&](float x, float y){ return c + vec2(x * s,y * s); };
    o->AddLine(p(-1.2f,1.6f),p(1.6f,1.6f),6.4f * s,ICON_STEEL);
    float r = 2.4f * s;
    vec2 face = p(-1.8f,-1.2f);
    o->AddRect(face - vec2(r,r),face + vec2(r,r),r,ICON_WOOD_END);
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
    //Posted, his post is his job while it lasts (defence_plan.md) - whatever his work was.
    if (k.post >= 0){
        DrawSpearIcon(o,c,is);
        return;
    }
    switch (k.job){
        case WORKER_JOB_WOODCUTTER: DrawAxeIcon(o,c,is); break;
        case WORKER_JOB_STONECUTTER: DrawPickIcon(o,c,is); break;
        case WORKER_JOB_MINER:      DrawPickIcon(o,c,is); break;
        case WORKER_JOB_FARMER:     DrawWheatIcon(o,c,is); break;
        case WORKER_JOB_WATER:      DrawBucketIcon(o,c,is); break;
        case WORKER_JOB_PICKER:     DrawBerriesIcon(o,c,is); break;
        case WORKER_JOB_ROPEMAKER:  DrawCoilIcon(o,c,is); break;
        case WORKER_JOB_BUILDER:    DrawHammerIcon(o,c,is); break;
        case WORKER_JOB_ARCHER:     DrawBowIcon(o,c,is); break;
        case WORKER_JOB_GUARD:      DrawSpearIcon(o,c,is); break;      //a tower's: posted there (defence_plan.md)
        case WORKER_JOB_PLAYING:    DrawBallIcon(o,c,is); break;
        case WORKER_JOB_BABY:
        case WORKER_JOB_CARING:     DrawBabyIcon(o,c,is); break;
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
/*
    WHERE HE IS GOING (the user's, 2026-10-10): the rest of a person's route as small dots, PATH_DOT_EVERY apart along
    it - counted from its start, so they stay put as he walks and go out one by one as he passes them - and a ring
    where it ends. At the route's own height (RouteHeight), so they climb the stairs and run along the walls with him.
    Returns a key that changes when the marks would: the route, and how many dots he has passed.
*/
#define PATH_DOT_EVERY      0.75f
#define PATH_DOT_R          0.075f
#define PATH_RING_IN        0.22f
#define PATH_RING_OUT       0.34f
#define PATH_LIFT           0.06f   //over what he walks on, clear of its facets
#define PATH_SIDES          10

uint64_t PathMarksKey(const EconomyWorker& k){
    float done = k.along;
    for (int s = 0; s < k.seg && s + 1 < (int)k.route.size(); s++){
        done += (k.route[s + 1] - k.route[s]).length();
    }
    uint64_t h = ((uint64_t)k.id << 40) ^ ((uint64_t)k.route.size() << 24) ^ (uint64_t)(int64_t)(done / PATH_DOT_EVERY);
    const vec2& end = k.route.back();
    h ^= ((uint64_t)(uint32_t)(int32_t)(end.x * 64.0f) << 20) ^ ((uint64_t)(uint32_t)(int32_t)(end.y * 64.0f) << 4);
    return h ? h : 1;
}

void BuildPathMarks(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k, std::vector<vertex>& out){
    out.clear();
    if (k.route.size() < 2 || k.seg + 1 >= (int)k.route.size()){
        return;
    }
    const vec3 up(0.0f,1.0f,0.0f);
    auto disc = [&](const vec2& c, float y, float r_in, float r_out){
        for (int i = 0; i < PATH_SIDES; i++){
            float a0 = 6.2831853f * (float)i / PATH_SIDES;
            float a1 = 6.2831853f * (float)(i + 1) / PATH_SIDES;
            vec2 d0(std::cos(a0),std::sin(a0));
            vec2 d1(std::cos(a1),std::sin(a1));
            vec3 o0(c.x + d0.x * r_out,y,c.y + d0.y * r_out);
            vec3 o1(c.x + d1.x * r_out,y,c.y + d1.y * r_out);
            if (r_in <= 0.0f){
                MeshTri(out,vec3(c.x,y,c.y),o0,o1,up,PAL_WINDOW_LIT,PAL_EFFECTS);
            }else{
                vec3 i0(c.x + d0.x * r_in,y,c.y + d0.y * r_in);
                vec3 i1(c.x + d1.x * r_in,y,c.y + d1.y * r_in);
                MeshQuad(out,i0,o0,o1,i1,up,PAL_WINDOW_LIT,PAL_EFFECTS);
            }
        }
    };
    //How far along the route he is, and how long it is.
    float done = k.along;
    float total = 0.0f;
    for (int s = 0; s + 1 < (int)k.route.size(); s++){
        float len = (k.route[s + 1] - k.route[s]).length();
        done += (s < k.seg) ? len : 0.0f;
        total += len;
    }
    //Every dot past him, up to the ring.
    int s = 0;
    float leg_start = 0.0f;
    for (float d = (std::floor(done / PATH_DOT_EVERY) + 1.0f) * PATH_DOT_EVERY; d < total - PATH_RING_OUT; d += PATH_DOT_EVERY){
        while (s + 1 < (int)k.route.size() - 1 && d > leg_start + (k.route[s + 1] - k.route[s]).length()){
            leg_start += (k.route[s + 1] - k.route[s]).length();
            s++;
        }
        vec2 dir = k.route[s + 1] - k.route[s];
        float len = dir.length();
        float along = d - leg_start;
        vec2 at = k.route[s] + ((len > 1e-5f) ? dir * (along / len) : vec2(0.0f,0.0f));
        disc(at,RouteHeight(w,z,k,s,along) + PATH_LIFT,0.0f,PATH_DOT_R);
    }
    int last = (int)k.route.size() - 1;
    float y_end = RouteHeight(w,z,k,last,0.0f) + PATH_LIFT;
    disc(k.route[last],y_end,PATH_RING_IN,PATH_RING_OUT);
    disc(k.route[last],y_end,0.0f,PATH_DOT_R);
}

vec3 FeetOf(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k){
    //Up on a deck where his route has him (ramparts_plan.md), and off it where his body stands (SPACING), as he is drawn.
    vec2 body = EconomyBody(k);
    return vec3(body.x,WorkerHeight(w,z,k),body.y);
}

const EconomyWorker* PersonById(const EconomyState& e, uint32_t id){
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            return &k;
        }
    }
    return NULL;
}

/*
    THE COLONY (settlements_plan.md, the user's 2026-10-12: which settlement a tent belongs to "is currently not visible
    at all"): what the selection belongs to - a building, field or yard its plot's (a building's door's), a gate or a
    crate its plot's, a person his home's, or with none the one he stands in. Named COLONY 1, 2, 3 in the order they
    were founded (EconomyState::settlements, the oldest first). A group: each of theirs. NO COLONY for something of
    none; empty with nothing selected.
*/
std::string SelectionColony(const PlayPick& sel, const std::vector<uint32_t>& group, const ChasmWorld& w, const EconomyState& e){
    auto of_plot = [&](int v) -> uint32_t {
        return (v >= 0 && v < (int)e.settlement.size()) ? e.settlement[v] : 0;
    };
    auto of_person = [&](uint32_t id) -> uint32_t {
        const EconomyWorker* k = PersonById(e,id);
        if (!k){
            return 0;
        }
        if (uint32_t mine = EconomyPersonSettlement(e,*k)){
            return mine;
        }
        GridPick at = w.picker->Pick(k->pos);
        return at.f_hit ? of_plot(at.plot) : 0;
    };
    std::vector<uint32_t> of;
    switch (sel.kind){
        case PLAY_PICK_BUILDING:    of.push_back(EconomySettlementOf(e,sel.id)); break;
        case PLAY_PICK_PERSON:      of.push_back(of_person(sel.id)); break;
        case PLAY_PICK_GATE:
        case PLAY_PICK_GROUND:      of.push_back(of_plot((sel.plot >= 0) ? sel.plot : (int)sel.id)); break;
        case PLAY_PICK_CRATE:
            for (const EconomyCrate& c : e.crates){
                if (c.id == sel.id){
                    of.push_back(of_plot(c.plot));
                }
            }
            break;
        case PLAY_PICK_GROUP:
            for (uint32_t id : group){
                of.push_back(of_person(id));
            }
            break;
        default:
            return std::string();
    }
    std::vector<int> numbers;
    bool f_none = false;
    for (uint32_t settlement : of){
        int n = 0;
        for (size_t i = 0; i < e.settlements.size(); i++){
            n = (e.settlements[i].id == settlement) ? (int)i + 1 : n;
        }
        if (!n){
            f_none = true;
        }else if (std::find(numbers.begin(),numbers.end(),n) == numbers.end()){
            numbers.push_back(n);
        }
    }
    std::sort(numbers.begin(),numbers.end());
    if (numbers.empty()){
        return f_none ? "NO COLONY" : "";
    }
    std::string name = (numbers.size() == 1) ? "COLONY " : "COLONIES ";
    for (size_t i = 0; i < numbers.size(); i++){
        name += std::string((i == 0) ? "" : (i + 1 == numbers.size() && !f_none) ? " AND " : ", ") + std::to_string(numbers[i]);
    }
    return f_none ? name + " AND NONE" : name;
}

/*
    An errand for a site, by the good it is for - `phase` 0 going for it, 1 picking it up, 2 taking it to the
    site: food by its category, any other good by name ("fetching rope for a site"). For a guard tower, its site
    or its stock kept full (defence_plan.md, step 7), "for a tower". Made once a good, since Doing hands back a
    const char*.
*/
const char* SiteErrand(int phase, int good, bool f_tower = false){
    static const std::vector<std::array<std::string,6>> phrases = []{
        std::vector<std::array<std::string,6>> p(GOOD_COUNT + 1);
        for (int g = -1; g < GOOD_COUNT; g++){
            std::string what = (g < 0) ? "goods" : GoodIsFood(g) ? "food" : GoodName(g);
            p[g + 1] = {"fetching " + what + " for a site","picking up " + what,"carrying " + what + " to a site",
                        "fetching " + what + " for a tower","picking up " + what,"carrying " + what + " to a tower"};
        }
        return p;
    }();
    int i = (good >= 0 && good < GOOD_COUNT) ? good + 1 : 0;
    return phrases[i][std::max(0,std::min(2,phase)) + (f_tower ? 3 : 0)].c_str();
}

//A load on its way to a store, by name - "carrying beans to a store" - as a site's errand is: a store is given goods,
//not "food". Made once a good, as SiteErrand's.
const char* StoreErrand(int good){
    static const std::vector<std::string> phrases = []{
        std::vector<std::string> p(GOOD_COUNT + 1);
        for (int g = -1; g < GOOD_COUNT; g++){
            p[g + 1] = (g < 0) ? std::string("carrying to a store") : "carrying " + std::string(GoodName(g)) + " to a store";
        }
        return p;
    }();
    return phrases[(good >= 0 && good < GOOD_COUNT) ? good + 1 : 0].c_str();
}

//Whether his workplace is a camp - its own woodcutter's or water carrier's (Economy.h, THE CAMP'S OWN WORK).
bool CampWork(const EconomyWorker& k, const ZoneState* z){
    return z && k.building && k.building < z->buildings.size() && z->buildings[k.building].kind == ZONE_KIND_CAMP;
}

/*
    Where his LOAD is going, for its strip's header: "for a site", "for the camp"... Empty where that is not settled
    yet - a log on his shoulder while he goes for a second, a load he still has on him while he eats - since where it
    goes is looked for once he is done (Economy::TickWorker, deliver).
*/
std::string LoadFor(const EconomyWorker& k, const ZoneState& z){
    if (k.site){
        bool f_tower = !EconomyIsGroundSite(k.site) && k.site < z.buildings.size() && z.buildings[k.site].kind == ZONE_KIND_TOWER;
        return f_tower ? "for a tower" : "for a site";
    }
    bool f_camp_home = k.house && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP;
    switch (k.state){
        //Home - but a harvest of the camp's own land goes into its supplies, the colony's (PickHarvest: `store` the camp).
        case WORKER_TO_PANTRY:  return (f_camp_home && k.store == k.house) ? "for the camp" : "for home";
        case WORKER_TO_WORKSHOP:
        case WORKER_MAKING:     return (k.job == WORKER_JOB_ROPEMAKER) ? "for the ropewalk" : "for the builders' hut";
        case WORKER_TO_STORE:
        case WORKER_UNLOADING:  return k.store ? "for a store" : CampWork(k,&z) ? "for the camp" : "for the woodpile";
        //The camp's picker between plants: back to its door with them (pick_on).
        case WORKER_TO_WORK:
        case WORKER_WORKING:    return (k.job == WORKER_JOB_PICKER && CampWork(k,&z)) ? "for the camp" : "";
        default:                return "";
    }
}

/*
    SETTLING (settlements_plan.md, the user's 2026-10-11): sent onto another settlement's ground, inside its borders now,
    and no tent on him - so his card offers Settle here. Economy::SettleHere says the rest: a bed to spare there.
*/
bool CanSettleHere(const EconomyState& e, const EconomyWorker& k){
    if (k.post < 0 || k.tent == ECONOMY_TENT_CARRIED || !e.world || k.post >= (int)e.settlement.size()
        || !EconomyCanOrder(k,e.tick)){
        return false;       //a child settles with the grown-up he goes along with (WITH THE GROWN)
    }
    uint32_t there = e.settlement[k.post];
    if (!there || there == EconomyPersonSettlement(e,k)){
        return false;
    }
    GridPick here = e.world->picker->Pick(k.pos);
    return here.f_hit && here.plot >= 0 && here.plot < (int)e.settlement.size() && e.settlement[here.plot] == there;
}

#define CARD_CUT_OFF_DISTANCE   3.0f    //world units from his house's door: waiting further off, he is cut off from it
//What he is doing, as a sentence's end - the card's and the house's list's. With the zones, a wall's way
//cleared is told from a road's (line_works_plan.md, step 3): the same errand on another line. With the economy,
//a post he is kept from is told from one he stands on.
const char* Doing(const EconomyWorker& k, const ZoneState* z = NULL, const EconomyState* e = NULL){
    //A guard's post is his tower (defence_plan.md) - unless an order has him elsewhere.
    bool f_tower = k.job == WORKER_JOB_GUARD && k.post < 0;
    int line_plot = (z && k.site && EconomyIsGroundSite(k.site)) ? EconomyGroundSitePlot(k.site) : -1;
    //Carrying for a guard tower - its site's goods, or its stock kept full (step 7).
    bool f_for_tower = z && k.site && !EconomyIsGroundSite(k.site) && k.site < z->buildings.size()
                       && z->buildings[k.site].kind == ZONE_KIND_TOWER;
    if (line_plot >= 0 && line_plot < (int)z->ground.size() && z->ground[line_plot] == ZONE_GROUND_WALL){
        if (k.state == WORKER_TO_ROAD){
            return "going to clear a wall's way";
        }
        if (k.state == WORKER_CLEARING){
            return "clearing a wall's way";
        }
    }
    //Waiting "at home" away from the house's door: cut off from it - a river or a wall, or the ice gone from under
    //his way back (seasons_plan.md, ICE) - and he tries the way again now and then (Economy::TickWorker).
    if (k.state == WORKER_AT_HOME && !EconomyIndoors(k) && k.house && z && e && e->world && k.house < z->buildings.size()
        && z->buildings[k.house].kind != ZONE_KIND_CAMP && k.house < e->door.size() && e->door[k.house] >= 0
        && (e->world->grid->fine.pos[e->door[k.house]] - k.pos).length() > CARD_CUT_OFF_DISTANCE){
        return "cut off from home, looking for a way back";
    }
    switch (k.state){
        case WORKER_AT_HOME:    return (k.with && k.job == WORKER_JOB_BABY) ? "carried by its mother"
                                     : EconomyIndoors(k) ? "at home" : (k.house ? "at the tent" : "waiting");
        //A woodcutter out for wood - a log first, a tree if none lies near - and, with one on him, for a second log;
        //at it, felling for longer than a pick-up takes.
        //A camp's water carrier out to the water's edge to draw a load (settlements_plan.md), not to a collector.
        //A camp's picker out to a plant, and on to the next while his load has room (foraging_plan.md).
        case WORKER_TO_WORK:    return (k.job == WORKER_JOB_PICKER && CampWork(k,z)) ? ((k.carry > 0) ? "going to pick more fruit" : "going to pick fruit")
                                     : (k.prop >= 0 && k.job == WORKER_JOB_STONECUTTER) ? "going to break a rock"
                                     : (k.job == WORKER_JOB_MINER) ? "going to dig at the mine"
                                     : (k.prop >= 0) ? ((k.carry > 0) ? "going for another log" : "going for wood")
                                     : (k.job == WORKER_JOB_WATER && CampWork(k,z)) ? "going to draw water" : "going to work";
        case WORKER_WORKING:    return (k.job == WORKER_JOB_PICKER && CampWork(k,z)) ? "picking fruit"
                                     : (k.prop < 0 && k.job == WORKER_JOB_WATER && CampWork(k,z)) ? "drawing water"
                                     : (k.job == WORKER_JOB_MINER) ? "digging"
                                     : (k.prop < 0) ? "picking up"
                                     : (k.job == WORKER_JOB_STONECUTTER) ? "breaking a rock"
                                     //Felling while the tree stands, then the log taken up - by the tree, not the time
                                     //left: a strong woodcutter's last second of felling read as picking it up.
                                     : (e && k.prop < (int)e->prop_state.size()) ? ((e->prop_state[k.prop] == PROP_STATE_STANDING)
                                                                                    ? "felling" : "picking up a log")
                                     : (k.timer > (int)(ECONOMY_PICK_SECONDS * CALENDAR_TICKS_PER_SECOND)) ? "felling" : "picking up a log";
        //Store 0 is no store: the camp's door for its own carriers, a woodcutter's woodpile (Economy::TickWorker, deliver).
        case WORKER_TO_STORE:   return (k.store == 0 && k.job == WORKER_JOB_WATER && CampWork(k,z)) ? "carrying water to the camp"
                                     : (k.store == 0 && k.job == WORKER_JOB_PICKER && CampWork(k,z)) ? "carrying fruit to the camp"
                                     : (k.store == 0 && k.job == WORKER_JOB_WOODCUTTER) ? (CampWork(k,z) ? "carrying wood to the camp"
                                                                                                          : "carrying wood to the woodpile")
                                     : StoreErrand(k.carry_good);
        case WORKER_UNLOADING:  return "unloading";
        case WORKER_TO_HOME:    return "going home";
        case WORKER_HOLDING:    return "waiting with a load";
        //For a site: whatever good it wants (defence_plan.md step 4) - rope too (goods_plan.md).
        case WORKER_TO_FETCH:   return SiteErrand(0,k.carry_good,f_for_tower);
        case WORKER_FETCHING:   return SiteErrand(1,k.carry_good,f_for_tower);
        case WORKER_TO_SITE:    return SiteErrand(2,k.carry_good,f_for_tower);
        case WORKER_BUILDING:   return "building";
        case WORKER_TO_ROAD:    return (k.prop >= 0) ? "going to clear a road" : "going to lay a road";
        case WORKER_CLEARING:   return "clearing a road";
        case WORKER_LAYING:     return "laying a road";
        case WORKER_TO_SUPPLY:{
            //A tent to take comes before gathering (settlements_plan.md) - a pressing need before either.
            if (k.tent == ECONOMY_TENT_TO_TAKE && !EconomyPressing(k)){
                return "going for a tent";
            }
            if (k.gather > 0){
                return "gathering supplies";        //a scout's pack, before his post (defence_plan.md)
            }
            GoodsMask want = EconomyPressing(k);
            bool f_water = (want & GOOD_BIT(GOOD_WATER)) != 0;
            bool f_food = (want & GOODS_FOOD) != 0;
            return (f_water && f_food) ? "going for food and water" : f_water ? "going for water" : "going for food";
        }
        case WORKER_SUPPLYING:  return (k.tent == ECONOMY_TENT_TO_TAKE && !EconomyPressing(k)) ? "packing a tent"
                                     : (k.gather > 0) ? "packing supplies" : "eating and drinking";
        case WORKER_TO_GET:     return (k.carry_good == GOOD_WATER) ? "fetching water for home"
                                     : (k.carry_good == GOOD_WOOD) ? "fetching firewood for home" : "fetching food for home";
        case WORKER_GETTING:    return "picking up for home";
        case WORKER_TO_PANTRY:  return "taking it home";
        case WORKER_TO_SOW:     return "going to sow";
        case WORKER_SOWING:     return "sowing";
        case WORKER_TO_PLAY:    return "going out to play";
        case WORKER_PLAYING:    return "playing";
        //Posted (defence_plan.md): standing there, or kept from it - a wall, a gate - and waiting for a way.
        //...sent to pick a plant or draw water by hand there (foraging_plan.md), and at it.
        case WORKER_TO_POST:    return (k.forage == FORAGE_FRUIT) ? "going to pick fruit"
                                     : (k.forage == FORAGE_WATER) ? "going to the water's edge"
                                     : f_tower ? "going up the tower" : "going to the post";
        case WORKER_ON_POST:    return (e && !EconomyAtPost(*e,k)) ? (f_tower ? "cut off from the tower" : "cut off from the post")
                                     : (k.forage == FORAGE_FRUIT) ? "picking fruit"
                                     : (k.forage == FORAGE_WATER) ? "drawing water"
                                     : f_tower ? "on watch on the tower" : "standing guard";
        case WORKER_TO_RANGE:   return "going to the range";
        case WORKER_TRAINING:   return "training";
        //A builder as a caretaker: provisions for the sites (defence_plan.md, step 5) - or a ropemaker, the same
        //errand with hemp, at his ropewalk (goods_plan.md).
        case WORKER_TO_INPUT:   return (k.carry_good == GOOD_HEMP) ? "fetching hemp for rope"
                                     : (k.carry_good == GOOD_BEANS) ? "fetching beans for provisions" : "fetching wheat for provisions";
        case WORKER_TAKING_INPUT: return (k.carry_good == GOOD_HEMP) ? "picking up hemp"
                                       : (k.carry_good == GOOD_BEANS) ? "picking up beans" : "picking up wheat";
        case WORKER_TO_WORKSHOP: return (k.job == WORKER_JOB_ROPEMAKER) ? ((k.carry > 0) ? "taking hemp to the ropewalk" : "going to make rope")
                                      : (k.carry <= 0) ? "going to make provisions"
                                      : (k.carry_good == GOOD_BEANS) ? "taking beans to the hut" : "taking wheat to the hut";
        case WORKER_MAKING:     return (k.carry_good == GOOD_ROPE) ? "making rope" : "making provisions";
        //A caretaker taking a crate's goods to a store (crates_plan.md).
        case WORKER_TO_CRATE:   return (k.job == WORKER_JOB_STONECUTTER) ? "going for rubble" : "going to move a crate";
        case WORKER_LIFTING:    return (k.job == WORKER_JOB_STONECUTTER) ? "lifting stone" : "lifting from a crate";
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
        case ZONE_KIND_RANGE:       return "Archery range";
        case ZONE_KIND_TOWER:       return "Guard tower";
        case ZONE_KIND_ROPEWALK:    return "Ropewalk";
        case ZONE_KIND_STONECUTTER: return "Stonecutter's hut";
        case ZONE_KIND_MINE:        return "Mine";
        case ZONE_KIND_WELL:        return "Well (debug)";
        default:                    return "?";
    }
}

const char* JobTitle(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "Woodcutter";
        case WORKER_JOB_FARMER:     return "Farmer";
        case WORKER_JOB_WATER:      return "Water carrier";
        case WORKER_JOB_PICKER:     return "Picker";
        case WORKER_JOB_BUILDER:    return "Builder";
        case WORKER_JOB_ARCHER:     return "Archer";
        case WORKER_JOB_GUARD:      return "Guard";
        case WORKER_JOB_ROPEMAKER:  return "Ropemaker";
        case WORKER_JOB_STONECUTTER: return "Stonecutter";
        case WORKER_JOB_MINER:      return "Miner";
        case WORKER_JOB_PLAYING:    return "Child";
        case WORKER_JOB_BABY:       return "Baby";
        case WORKER_JOB_CARING:     return "Caring";
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
    A GARDEN OR LOT BEING BUILT (yards_plan.md, the user's 2026-10-10: selectable like a field site). Ground has no
    id, so what a click on one selects is its PATCH: the plots of that ground and that owner joined to the one
    clicked, built or not - one patch while any of it is still a site, staked out (ZoneMesh.cpp), and nothing to
    select once it all stands, as before. Picked by its lowest plot (PLAY_PICK_GROUND), so a click anywhere on
    it is the same selection.
*/
bool GroundSitePlot(const ZoneState& z, int v){
    return ZoneEnclosesGround(z.ground[v]) && !z.ground_built[v] && z.storeys[v] == 0;
}

//Plot `plot`'s patch, sorted - empty if it is not garden or lot ground, or none of the patch is a site.
void GroundPatch(const ZoneState& z, int plot, std::vector<int>& out){
    out.clear();
    if (!z.world || plot < 0 || plot >= (int)z.ground.size() || !ZoneEnclosesGround(z.ground[plot]) || z.storeys[plot]){
        return;
    }
    int ground = z.ground[plot];
    uint32_t owner = ZoneGroundOwner(z,plot);
    bool f_site = false;
    std::vector<int> todo(1,plot);
    while (!todo.empty()){
        int v = todo.back();
        todo.pop_back();
        if (std::find(out.begin(),out.end(),v) != out.end()){
            continue;
        }
        out.push_back(v);
        f_site = f_site || GroundSitePlot(z,v);
        int nb[8];
        int n = ZonePlotNeighbours(*z.world,v,nb,8);
        for (int i = 0; i < n; i++){
            if (z.ground[nb[i]] == ground && !z.storeys[nb[i]] && ZoneGroundOwner(z,nb[i]) == owner){
                todo.push_back(nb[i]);
            }
        }
    }
    if (!f_site){
        out.clear();
    }
    std::sort(out.begin(),out.end());
}

/*
    The people who belong with a selection, for their outlines and pins: a house's or camp's family,
    a workplace's worker, a field's farmer (land_plan.md), a site's carriers - a garden's or lot's too, at any
    plot of it - a store's carriers (to it or fetching from it, for a site or home), and everyone on his way to a
    crate or taking from it (crates_plan.md).
*/
void RelatedPeople(const PlayPick& p, const ZoneState& z, const EconomyState& e, std::vector<uint32_t>& out){
    out.clear();
    if (p.kind == PLAY_PICK_GROUND){
        std::vector<int> patch;
        GroundPatch(z,(int)p.id,patch);
        for (const EconomyWorker& k : e.workers){
            if (EconomyIsGroundSite(k.site)
                && std::binary_search(patch.begin(),patch.end(),EconomyGroundSitePlot(k.site))){
                out.push_back(k.id);
            }
        }
        return;
    }
    if (p.kind == PLAY_PICK_CRATE){
        for (const EconomyWorker& k : e.workers){
            if (k.store == (ECONOMY_STORE_CRATE | p.id)){
                out.push_back(k.id);
            }
        }
        return;
    }
    if (p.kind != PLAY_PICK_BUILDING || p.id >= z.buildings.size()){
        return;
    }
    int kind = z.buildings[p.id].kind;
    uint32_t farmer = (kind == ZONE_KIND_FIELD && p.id < e.farmer_of.size()) ? e.farmer_of[p.id] : 0;
    for (const EconomyWorker& k : e.workers){
        bool f = k.site == p.id || k.building == p.id || (farmer && k.id == farmer);
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
      - any other workplace (a collector): its worker's house;
      - a field: the house - or camp - whose land it is (land_plan.md);
      - a house: its family's workplaces - a woodcutter's hut and lot - and its land's fields;
      - a person: his house and workplace, a woodcutter's lot, the fields a farmer farms, and his post;
      - a gate: its own plots - it has no building to outline - those of a gate on the slant together;
      - a garden or lot being built: the house or hut whose yard it is.
    A house or a hut also has its YARD (yards_plan.md) - every garden and lot plot it owns - so while one is
    painted for it, what it has already stands out. Each listed once, the selection itself never.
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
    if (p.kind == PLAY_PICK_GROUND && p.id < z.ground.size()){
        building(ZoneGroundOwner(z,(int)p.id));
    }else if (p.kind == PLAY_PICK_PERSON){
        for (const EconomyWorker& k : e.workers){
            if (k.id == p.id){
                building(k.house);
                building(k.building);
                lot_of(k.building);
                //Where he was sent (defence_plan.md): the plot itself, outlined as a lot's is.
                if (k.post >= 0 && k.post < (int)z.ground.size()){
                    out.plots.push_back(k.post);
                }
            }
        }
    }else if (p.kind == PLAY_PICK_GATE && z.world && p.plot >= 0 && p.plot < (int)z.gate.size() && z.gate[p.plot]){
        //A gate: its plot, and any gate plot joined to it - a road crossing on the slant gates two, one opening.
        std::vector<int> todo(1,p.plot);
        while (!todo.empty()){
            int v = todo.back();
            todo.pop_back();
            if (std::find(out.plots.begin(),out.plots.end(),v) != out.plots.end()){
                continue;
            }
            out.plots.push_back(v);
            int nb[8];
            int n = ZonePlotNeighbours(*z.world,v,nb,8);
            for (int i = 0; i < n; i++){
                if (z.gate[nb[i]]){
                    todo.push_back(nb[i]);
                }
            }
        }
        for (const EconomyField& f : e.fields){
            if (f.building < e.farmer_of.size() && e.farmer_of[f.building] == p.id){
                building(f.building);
            }
        }
    }else if (p.kind == PLAY_PICK_BUILDING && p.id < z.buildings.size()){
        int kind = z.buildings[p.id].kind;
        if (kind == ZONE_KIND_WOODCUTTER){
            lot_of(p.id);
        }
        //Its yard (yards_plan.md).
        if (ZoneKindHasYard(kind)){
            for (size_t v = 0; v < z.ground_owner.size(); v++){
                if (z.ground_owner[v] == p.id && ZoneGroundOwner(z,(int)v) == p.id){
                    out.plots.push_back((int)v);
                }
            }
        }
        //Land (land_plan.md): a field's owner, a house's or camp's fields.
        for (const EconomyField& f : e.fields){
            if (f.building == p.id){
                building(f.owner);
            }
            if ((kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_CAMP) && f.owner == p.id){
                building(f.building);
            }
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
    THE CARD: a title, then lines - text, a text with a bar (0..1), a row of a store's goods as chips, one
    chip that turns a setting on and off, or a goods strip: what a building holds and whose it is
    (ChasmHud.h). Made from the published states, for the overlay and for the tool alike.
*/
#define CARD_TEXT   0
#define CARD_BAR    1
#define CARD_CHIPS  2
#define CARD_TOGGLE 3       //a site's "Build first"; a posted person's "Release", "Grab Supplies" (own choice_action) - in a row
#define CARD_CHOICE 4       //a row of chips, one of them lit: a field's crops (fields_plan.md)
#define CARD_STOCK  5       //a goods strip (the user's, 2026-10-09)
#define CARD_PERSON 6       //a group's row: his badge, his line, `right` at its end, and an x taking him out of the group
struct CardLine{
    int type = CARD_TEXT;
    std::string text;
    uint32_t colour = HUD_TEXT;
    float bar = 0.0f;
    GoodsMask chips_on = 0;       //CARD_CHIPS: the goods taken; CARD_TOGGLE: 1 while on
    bool f_locked = false;      //...and whether they can be changed here
    std::vector<std::pair<std::string,int>> choices;    //CARD_CHOICE: each chip's label and value
    int choice_on = -1;         //...the value lit
    int choice_action = 0;      //...and what a click on one does (OVERLAY_HIT_*), with its value
    HudStock stock;             //CARD_STOCK
    uint32_t person = 0;        //CARD_TEXT about one person: hovering it outlines him, a click selects him; CARD_PERSON's
    std::string right;          //CARD_PERSON: at the row's right, dimmed - how long his pack lasts
};

//A crop's name as a card says it: capitalised.
std::string CropTitle(int crop){
    std::string s = ZoneCropName(crop);
    s[0] = (char)toupper(s[0]);
    return s;
}
struct Card{
    std::string title;
    std::string subtitle;           //at the title's right, by the close mark: a person's age and job
    std::vector<CardLine> lines;
    void Text(const std::string& t, uint32_t colour = HUD_TEXT){
        CardLine l;
        l.text = t;
        l.colour = colour;
        lines.push_back(l);
    }
    //A line about person `id` (selection_plan.md, "Selecting someone indoors"): a click on it selects him.
    void Person(const std::string& t, uint32_t id, uint32_t colour = HUD_TEXT){
        Text(t,colour);
        lines.back().person = id;
    }
    void Bar(const std::string& t, float f, uint32_t colour = HUD_TEXT){
        CardLine l;
        l.type = CARD_BAR;
        l.text = t;
        l.bar = std::max(0.0f,std::min(1.0f,f));
        l.colour = colour;
        lines.push_back(l);
    }
    void Stock(const std::string& label, const std::string& whose, const std::array<int,GOOD_COUNT>& goods, int room,
               bool f_kept, const std::array<int,GOOD_COUNT>& reserved = {}){
        CardLine l;
        l.type = CARD_STOCK;
        l.stock.label = label;
        l.stock.whose = whose;
        l.stock.goods = goods;
        l.stock.reserved = reserved;
        l.stock.room = room;
        l.stock.f_kept = f_kept;
        lines.push_back(l);
    }
    /*
        A shared stock BY GROUP (goods_plan.md, "The view"): holding goods of more than one group - materials, food
        and water, inputs (HudGoodsGroup) - its total on a line, then a strip for each group it holds any of, each
        against the whole room (or, with none, the whole stock), so together they fill as one strip over it would.
        Of one group, or empty, the one strip as ever.
    */
    void StockByGroup(const std::string& label, const std::string& whose, const std::array<int,GOOD_COUNT>& goods,
                      int room, bool f_kept, const std::array<int,GOOD_COUNT>& reserved = {}){
        std::array<std::array<int,GOOD_COUNT>,HUD_GROUP_COUNT> part{};
        std::array<std::array<int,GOOD_COUNT>,HUD_GROUP_COUNT> part_reserved{};
        std::array<int,HUD_GROUP_COUNT> count{};
        int total = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            int k = HudGoodsGroup(g);
            part[k][g] = goods[g];
            part_reserved[k][g] = reserved[g];
            count[k] += std::max(0,goods[g]);
            total += std::max(0,goods[g]);
        }
        int groups = (int)std::count_if(count.begin(),count.end(),[](int n){ return n > 0; });
        if (groups <= 1){
            Stock(label,whose,goods,room,f_kept,reserved);
            return;
        }
        Text((whose.empty() ? label : label + " - " + whose) + ": " + std::to_string(total)
             + (room > 0 ? " of " + std::to_string(room) : ""));
        for (int k = 0; k < HUD_GROUP_COUNT; k++){
            if (count[k] > 0){
                Stock(HudGoodsGroupName(k),"",part[k],room > 0 ? room : total,f_kept,part_reserved[k]);
                lines.back().stock.figure = std::to_string(count[k]);
            }
        }
    }
};

bool AnyGoods(const std::array<int,GOOD_COUNT>& goods){
    return std::any_of(goods.begin(),goods.end(),[](int n){ return n > 0; });
}

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

//A family's name, from any of them: a person's name less the first name.
//The family living in house `id`, by name - empty with nobody there. A couple's family goes by the surname their
//children take (COUPLES, Economy.h): a new couple keeps two, and the one the house is known by is the children's.
//A grown-up's family first: orphans squeezed in with them (Economy::HouseFamilies) do not name it.
std::string HouseFamily(const EconomyState& e, uint32_t id){
    for (int pass = 0; pass < 2; pass++){
        for (const EconomyWorker& k : e.workers){
            if (k.house != id || (pass == 0 && !EconomyCanOrder(k,e.tick))){
                continue;
            }
            bool f_couple = std::any_of(e.workers.begin(),e.workers.end(),[&](const EconomyWorker& o){
                return o.id == k.partner && o.house == id;
            });
            return EconomySurnameName(f_couple ? EconomyCoupleSurname(e,k.id,k.partner) : k.surname);
        }
    }
    return std::string();
}

//Whose land a field is, as a card says it (land_plan.md): "the Veen family's", "the camp's", "nobody's".
std::string LandName(const ZoneState& z, const EconomyState& e, uint32_t owner){
    if (!owner || owner >= z.buildings.size()){
        return "nobody's";
    }
    if (z.buildings[owner].kind == ZONE_KIND_CAMP){
        return "the camp's";
    }
    std::string family = HouseFamily(e,owner);
    return family.empty() ? Format("house %u's",owner) : "the " + family + " family's";
}

//A person's first name by id - "the late Ada" for one of the dead - empty for nobody (a settler couple's parents).
std::string FirstNameOf(const EconomyState& e, uint32_t id){
    auto first = [](const std::string& name){
        size_t at = name.find(' ');
        return (at == std::string::npos) ? name : name.substr(0,at);
    };
    if (!id){
        return std::string();
    }
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            return first(EconomyPersonName(k));
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == id){
            return "the late " + first(EconomyDeathName(d));
        }
    }
    return std::string();
}

void PersonCard(const ChasmWorld& w, const ZoneState& z, const EconomyState& e, const EconomyWorker& k, Card& c){
    (void)w;
    c.title = EconomyPersonName(k);
    /*
        AGE AND JOB at the title's right (the user's, 2026-10-19): "Age 25 - Woodcutter", and " / posted" while he is,
        so the job's line below says only where - and a posted person with no job needs no line at all.
    */
    const EconomyWorker* with = k.with ? PersonById(e,k.with) : NULL;
    bool f_along = with && !EconomyCanOrder(k,e.tick);
    bool f_posted = k.post >= 0 && !f_along;
    c.subtitle = Format("Age %i - %s%s",EconomyAgeAt(k.born,e.tick),JobTitle(k.job),f_posted ? " / posted" : "");
    //His RANK (ranks_plan.md) first, by the name: "Veteran, age 25 - Guard".
    int rank = EconomyRank(k.kills);
    if (rank > RANK_NONE){
        c.subtitle = Format("%s, age %i - %s%s",EconomyRankName(rank),EconomyAgeAt(k.born,e.tick),JobTitle(k.job),
                            f_posted ? " / posted" : "");
    }
    /*
        LIFELINES (lifeline_plan.md): stage, the birthday, whose child, and the children - living, then the dead.
    */
    int stage = EconomyStageAt(k.born,e.tick);
    std::string stage_name = EconomyStageName(stage);
    stage_name[0] = (char)toupper(stage_name[0]);
    c.Text(Format("%s - birthday %s",stage_name.c_str(),EconomyBirthdayName(k.born).c_str()),HUD_TEXT_DIM);
    std::string mother = FirstNameOf(e,k.mother);
    std::string father = FirstNameOf(e,k.father);
    if (!mother.empty() || !father.empty()){
        std::string both = (!mother.empty() && !father.empty()) ? father + " and " + mother : mother + father;
        c.Text(Format("%s of %s",k.f_female ? "Daughter" : "Son",both.c_str()),HUD_TEXT_DIM);
    }
    //BIRTHS: the partner, and a child on the way.
    std::string partner = FirstNameOf(e,k.partner);
    if (!partner.empty()){
        c.Text(Format("%s of %s",k.f_female ? "Wife" : "Husband",partner.c_str()),HUD_TEXT_DIM);
    }
    if (k.due){
        int64_t left = (int64_t)k.due - (int64_t)e.tick;
        int days = (int)std::max<int64_t>(0,(left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS);
        c.Text(days ? Format("Expecting - due in %i day%s",days,days == 1 ? "" : "s") : std::string("Expecting - due now, once home"));
    }
    std::string children;
    for (int f_dead = 0; f_dead < 2; f_dead++){
        auto add = [&](uint32_t id, uint32_t mother_id, uint32_t father_id){
            if (mother_id == k.id || father_id == k.id){
                children += (children.empty() ? "" : ", ") + FirstNameOf(e,id);
            }
        };
        if (!f_dead){
            for (const EconomyWorker& o : e.workers){
                add(o.id,o.mother,o.father);
            }
        }else{
            for (const EconomyDeath& d : e.dead){
                add(d.id,d.mother,d.father);
            }
        }
    }
    if (!children.empty()){
        c.Text("Children: " + children,HUD_TEXT_DIM);
    }
    if (k.house == 0){
        c.Text("Nowhere to live",HUD_AMBER);
    }else if (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP){
        c.Text("Lives in the camp - waiting for a house",HUD_AMBER);
    }else if (int over = EconomyCrowdedBy(e,z,k.house)){
        //Crowded (building_size_plan.md): slower at whatever work he does.
        c.Text(Format("Lives in house %u - crowded: works at %i%%",k.house,(int)std::lround(EconomyCrowdedPace(over) * 100.0f)),
               HUD_AMBER);
    }else{
        c.Text(Format("Lives in house %u",k.house));
    }
    if (k.job == WORKER_JOB_PLAYING){
        //When he grows up and takes a job: the day his childhood ends.
        int64_t left = k.born + LIFE_CHILD_TICKS - (int64_t)e.tick;
        int days = (int)std::max<int64_t>(0,(left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS);
        c.Text(Format("Plays near home - grows up in %i day%s",days,days == 1 ? "" : "s"));
    }else if (k.job == WORKER_JOB_BABY){
        int64_t left = k.born + LIFE_BABY_TICKS - (int64_t)e.tick;
        int days = (int)std::max<int64_t>(0,(left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS);
        c.Text(Format("In its mother's care - a child in %i day%s",days,days == 1 ? "" : "s"));
    }else if (k.job == WORKER_JOB_CARING){
        //Back to work when her baby turns one: the youngest of hers still a baby.
        int64_t left = 0;
        for (const EconomyWorker& o : e.workers){
            if (o.mother == k.id && o.job == WORKER_JOB_BABY){
                left = std::max<int64_t>(left,o.born + LIFE_BABY_TICKS - (int64_t)e.tick);
            }
        }
        int days = (int)std::max<int64_t>(0,(left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS);
        c.Text(Format("Caring for her baby - back to work in %i day%s",days,days == 1 ? "" : "s"));
    }else if (k.job == WORKER_JOB_NONE){
        //"No job" at the top says it all (the user's): what he carries for the sites shows in what he is doing.
    }else if (k.job == WORKER_JOB_BUILDER){
        c.Text(Format("At the builders' hut %u: carries to sites and builds",k.building));
    }else if (k.job == WORKER_JOB_ARCHER){
        c.Text(Format("Trains at the archery range %u",k.building));
    }else if (k.job == WORKER_JOB_GUARD){
        c.Text(Format("On the tower %u%s: lives on its stock",k.building,k.f_chosen ? ", posted by hand" : ""));
        //Released (step 8): home, and the tower takes one fewer, so nobody goes up for him.
        CardLine release;
        release.type = CARD_TOGGLE;
        release.text = "Release from the tower";
        release.choice_action = OVERLAY_HIT_GUARD_RELEASE;
        c.lines.push_back(release);
    }else if (k.job == WORKER_JOB_FARMER){
        //His share of his family's land (land_plan.md).
        int fields = 0;
        for (uint32_t farmer : e.farmer_of){
            fields += (farmer == k.id) ? 1 : 0;
        }
        c.Text(Format("Farms %s land: %i field%s",LandName(z,e,k.building).c_str(),fields,fields == 1 ? "" : "s"));
    }else{
        //"At the woodcutter's hut 3", as the builder's line says it - not a title mid-sentence.
        std::string kind = KindTitle(z.buildings[k.building].kind);
        kind[0] = (char)tolower(kind[0]);
        c.Text(Format("At the %s %u",kind.c_str(),k.building));
    }
    /*
        WITH THE GROWN (settlements_plan.md, the user's 2026-10-12): a child going along with a grown-up of his household,
        or a baby carried by its mother - the game's order, never the player's, so none of the posted chips below
        (Economy::WithTheGrown). And on the grown-up's card, who goes with them.
    */
    if (f_along && k.job != WORKER_JOB_BABY){
        c.Text(Format("Going with %s",EconomyPersonName(*with).c_str()),HUD_AMBER);     //a baby: "carried by its mother" (Doing)
    }
    int children_along = 0;
    int babies_along = 0;
    for (const EconomyWorker& o : e.workers){
        if (o.with == k.id){
            (o.job == WORKER_JOB_BABY ? babies_along : children_along)++;
        }
    }
    if (children_along || babies_along){
        std::string taking = children_along ? Format("Taking %i child%s along",children_along,children_along == 1 ? "" : "ren") : "";
        std::string carrying = (babies_along == 1) ? "carrying a baby" : babies_along ? Format("carrying %i babies",babies_along) : "";
        if (taking.empty()){
            carrying[0] = 'C';
        }
        c.Text(taking + ((!taking.empty() && !carrying.empty()) ? ", " : "") + carrying,HUD_AMBER);
    }
    /*
        POSTED (defence_plan.md): the post is the job while it lasts - "/ posted" at the top; work there was waits, its
        place kept. The Release chip ends it (OVERLAY_HIT_RELEASE, a recorded order), on one row with the other two.
    */
    if (f_posted){
        if (k.job >= 0){
            c.Text("The work waits until released",HUD_AMBER);
        }
        //ICE (seasons_plan.md): his only way home over the ice - cut off once it thaws (Economy::IceHome).
        for (const std::pair<uint32_t,int>& ice : e.ice_home){
            if (ice.first == k.id){
                c.Text(Format("The way home is over the ice - it thaws in about %i day%s",ice.second,ice.second == 1 ? "" : "s"),
                       (ice.second <= 1) ? HUD_DANGER : HUD_AMBER);
            }
        }
        CardLine release;
        release.type = CARD_TOGGLE;
        release.text = "Release";
        release.choice_action = OVERLAY_HIT_RELEASE;
        c.lines.push_back(release);
        //SCOUTING (defence_plan.md): his pack filled as full as he can carry first, lit while he is at it - a recorded
        //command, pressed again to go straight on (OVERLAY_HIT_GATHER).
        CardLine gather;
        gather.type = CARD_TOGGLE;
        gather.text = "Grab Supplies";
        gather.chips_on = (k.gather > 0) ? 1 : 0;
        gather.choice_action = OVERLAY_HIT_GATHER;
        c.lines.push_back(gather);
        //TAKING A TENT (settlements_plan.md): one from the nearest camp with one to spare, first - lit while he is to.
        if (k.tent != ECONOMY_TENT_CARRIED){
            CardLine tent;
            tent.type = CARD_TOGGLE;
            tent.text = "Pack a Tent";
            tent.chips_on = (k.tent == ECONOMY_TENT_TO_TAKE) ? 1 : 0;
            tent.choice_action = OVERLAY_HIT_TENT;
            c.lines.push_back(tent);
        }
    }
    //Sent into another settlement and there now, with no tent: he moves in as he is, to a bed to spare (settlements_plan.md).
    if (CanSettleHere(e,k)){
        CardLine settle;
        settle.type = CARD_TOGGLE;
        settle.text = "Settle here";
        settle.choice_action = OVERLAY_HIT_SETTLE;
        c.lines.push_back(settle);
    }
    //A tent on him (settlements_plan.md): pitched where he stands - his home from then on - posted or not.
    if (k.tent == ECONOMY_TENT_CARRIED){
        c.Text("Carrying a tent",HUD_AMBER);
        CardLine pitch;
        pitch.type = CARD_TOGGLE;
        pitch.text = "Pitch the tent here";
        pitch.choice_action = OVERLAY_HIT_PITCH;
        c.lines.push_back(pitch);
    }
    /*
        His PACK (defence_plan.md): what he carries to live on - posted, or on his way home - as a store shows what it
        holds (selection_plan.md, "What a person carries, as a strip"): hollow, his own. It has no room, since water
        is counted in units and food by the health it gives; what it is worth, in days, takes the room's place.
    */
    if (AnyGoods(k.pack)){
        auto days = [](float d){
            return (std::fabs(d - std::round(d)) < 0.05f) ? Format("%.0f",d) : Format("%.1f",d);
        };
        float water = (float)k.pack[GOOD_WATER] * NEEDS_WATER_WORTH / CALENDAR_DAY_TICKS;
        float food = (float)EconomyPackFood(k) / CALENDAR_DAY_TICKS;
        CardLine l;
        l.type = CARD_STOCK;
        l.stock.label = "Packed:";      //his own goes without saying (the user's): it is hollow, as kept stock is
        l.stock.goods = k.pack;
        l.stock.f_kept = true;
        l.stock.figure = Format("water %s days, food %s",days(water).c_str(),days(food).c_str());
        c.lines.push_back(l);
    }
    /*
        HOW FAR ALONG (selection_plan.md, the user's 2026-10-18): what he is doing, with a bar under it while that runs on
        his timer - felling, building, drawing water - filling as he gets on with it (EconomyTaskProgress). Then his LOAD,
        as a goods strip against what he can carry on this errand (EconomyLoadRoom), striped through: it is on its way
        to a site, a store, the camp or his home's pantry, never his own - that is his pack, hollow, above.
    */
    std::string doing = Doing(k,&z,&e);
    if (!doing.empty()){
        doing[0] = (char)toupper(doing[0]);
        float progress = EconomyTaskProgress(k);
        if (progress >= 0.0f){
            c.Bar(doing,progress);
        }else{
            c.Text(doing);
        }
    }
    if (k.carry > 0 && k.carry_good >= 0 && k.carry_good < GOOD_COUNT){
        CardLine l;
        l.type = CARD_STOCK;
        l.stock.label = "Load";
        l.stock.whose = LoadFor(k,z);
        l.stock.goods[k.carry_good] = k.carry;
        l.stock.room = EconomyLoadRoom(e,z,k);
        l.stock.f_load = true;
        c.lines.push_back(l);
    }
    /*
        A FIGHT (zombies_plan.md, step 2): a zombie within his reach - and so his MEAL waits, and none begins, while one
        is near (Economy.h, MEALS). The meal itself as a bar filling as he eats it.
    */
    bool f_zombie_near = false;
    for (const EconomyZombie& m : e.zombies){
        f_zombie_near = f_zombie_near || (EconomyBody(m) - EconomyBody(k)).length() <= NEEDS_MEAL_SAFE;
    }
    f_zombie_near = f_zombie_near && !EconomyIndoors(k) && EconomyWorkerLayer(k) == WALK_GROUND;
    if (k.swing > 0){
        c.Text("Fighting a zombie",HUD_DANGER);
    }else if (k.draw > 0){
        c.Text("Shooting at a zombie",HUD_AMBER);      //ARROWS (step 3): his meal waits for it too
    }else if (f_zombie_near){
        c.Text("A zombie is near",HUD_AMBER);
    }
    if (EconomyMealOn(k)){
        float seconds = (k.meal_good == GOOD_WATER) ? ECONOMY_DRINK_SECONDS : ECONOMY_MEAL_SECONDS;
        float total = std::max(1.0f,std::round(seconds * CALENDAR_TICKS_PER_SECOND));
        std::string what = (k.meal_good == GOOD_WATER) ? std::string("Drinking") : "Eating " + std::string(GoodName(k.meal_good));
        const char* waits = f_zombie_near ? " - waits while the zombie is near" : (k.draw > 0) ? " - waits while shooting" : "";
        c.Bar(what + waits,1.0f - (float)k.meal_ticks / total,(*waits) ? HUD_AMBER : HUD_TEXT);
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
    //ARCHERY (range_plan.md): what he has reached of what his precision allows - once he has trained.
    if (k.archery > 0 || k.job == WORKER_JOB_ARCHER){
        int cap = EconomyArcheryCap(k);
        c.Bar(Format("Archery %i%% (of %i%%)",(int)(k.archery * 100LL / ARCHERY_FULL),(int)(cap * 100LL / ARCHERY_FULL)),
              (float)k.archery / ARCHERY_FULL);
    }
    //KILLS (ranks_plan.md): the zombies he felled, once he has - how, when he has done both - and what the next RANK takes.
    int kills = EconomyKills(k.kills);
    std::string next;
    if (rank + 1 < RANK_COUNT){
        next = Format(" - %s at %i",EconomyRankName(rank + 1),EconomyRankKills(rank + 1));
    }
    if (kills > 0 && k.kills[KILL_BOW] > 0 && k.kills[KILL_HAND] > 0){
        c.Text(Format("Kills %i: %i by bow, %i by hand%s",kills,k.kills[KILL_BOW],k.kills[KILL_HAND],next.c_str()));
    }else if (kills > 0){
        c.Text(Format("Kills %i, by %s%s",kills,(k.kills[KILL_BOW] > 0) ? "bow" : "hand",next.c_str()));
    }
    (void)e;
}

/*
    A house's or a hut's YARD (yards_plan.md): what it owns, and the chips that take up the tool to add to it -
    the building staying selected, so what is painted is its own. A house's EXTEND (the user's, 2026-10-10) is
    the same for its plots: the house tool up, painting on from the house - while it has room for one more.
*/
void YardLines(const ZoneState& z, uint32_t id, Card& c){
    int kind = z.buildings[id].kind;
    int gardens = ZoneYardPlots(z,id,ZONE_GROUND_GARDEN);
    int lots = ZoneYardPlots(z,id,ZONE_GROUND_LOT);
    if (gardens + lots > 0){
        std::string yard;
        if (gardens){
            yard = Format("%i garden plot%s",gardens,gardens == 1 ? "" : "s");
        }
        if (lots){
            yard += (yard.empty() ? "" : ", ") + Format("%i lot plot%s",lots,lots == 1 ? "" : "s");
        }
        c.Text("Yard: " + yard,HUD_TEXT_DIM);
    }
    CardLine add;
    add.type = CARD_CHOICE;
    add.choice_action = OVERLAY_HIT_TAKE_TOOL;
    if (kind == ZONE_KIND_HOUSE && z.buildings[id].size < ZoneKindMaxPlots(kind)){
        add.choices.push_back(std::make_pair(std::string("Extend"),(int)CHASM_TOOL_HOUSE));
    }
    add.choices.push_back(std::make_pair(std::string("Add lot"),(int)CHASM_TOOL_LOT));
    if (ZoneOwnsGround(kind,ZONE_GROUND_GARDEN)){
        add.choices.push_back(std::make_pair(std::string("Add garden"),(int)CHASM_TOOL_GARDEN));
    }
    c.lines.push_back(add);
}

/*
    STOCK KEPT (goods_plan.md): what the colony keeps of the good a builders' hut or a ropewalk makes - the colony's
    number, so every one of them shows the same - against what the stores hold; the chips that set it
    (OVERLAY_HIT_KEEP); and an order for more (OVERLAY_HIT_MAKE_MORE), how far it has got and Cancel while it runs.
*/
void KeptLines(const EconomyState& e, const ZoneState& z, int good, Card& c){
    static const int steps[] = {0,5,10,20,40};
    static const int more[] = {5,10};
    int stored = EconomyStoredTotals(e,z)[good];
    int kept = e.kept[good];
    const char* name = GoodName(good);
    if (kept > 0){
        c.Text(Format("The colony keeps %i %s in its stores - %i there now",kept,name,stored));
    }else{
        c.Text(Format("The colony keeps no %s in its stores: %s",name,
                      (good == GOOD_ROPE) ? "it waits here, for the sites" : "they are made for the sites and towers"),HUD_TEXT_DIM);
    }
    CardLine keep;
    keep.type = CARD_CHOICE;
    for (int n : steps){
        keep.choices.push_back(std::make_pair(std::to_string(n),n));
    }
    keep.choice_on = kept;
    keep.choice_action = OVERLAY_HIT_KEEP;
    c.lines.push_back(keep);
    if (e.ordered[good] > 0){
        c.Text(Format("Making more: %i of %i in the stores",stored,e.ordered[good]));
    }else{
        c.Text("Make more, once",HUD_TEXT_DIM);
    }
    CardLine order;
    order.type = CARD_CHOICE;
    for (int n : more){
        order.choices.push_back(std::make_pair(Format("%i more",n),n));
    }
    if (e.ordered[good] > 0){
        order.choices.push_back(std::make_pair(std::string("Cancel"),0));
    }
    order.choice_action = OVERLAY_HIT_MAKE_MORE;
    c.lines.push_back(order);
    //Kept in stores no store takes it into: it can only wait at the workplace.
    bool f_taken = false;
    for (size_t s = 1; s < e.accepts.size() && !f_taken; s++){
        f_taken = (e.accepts[s] & GOOD_BIT(good)) != 0;
    }
    if (EconomyKeptLevel(e,good) > 0 && !f_taken){
        c.Text(Format("No store takes %s: none can be kept",name),HUD_AMBER);
    }
}

void BuildingCard(const ChasmWorld& w, const ZoneState& z, const EconomyState* e, uint32_t id, uint64_t tick, Card& c){
    const ZoneBuilding& b = z.buildings[id];
    ZoneBuildingInfo info = ZoneBuildingFigures(w,z,id);
    c.title = KindTitle(b.kind);
    std::array<int,GOOD_COUNT> site_cost = EconomySiteCost(w,z,id);
    bool f_field = b.kind == ZONE_KIND_FIELD;
    bool f_cells = ZoneKindOnCells(b.kind);     //a field, or a range painted like one (range_plan.md)
    if (f_cells){
        //Its SIZE (the user's, 2026-10-09), built and still a site: a harvest is its area's, cells vary in size.
        int cells = (int)std::count(z.field.begin(),z.field.end(),id);
        c.Text(Format("Size: %i cell%s, area %.0f",cells,cells == 1 ? "" : "s",info.floor_area + info.site_area),
               HUD_TEXT_DIM);
    }
    if (std::any_of(site_cost.begin(),site_cost.end(),[](int x){ return x > 0; })){
        /*
            Under construction: what it takes, what it has been brought of that - kept, a strip over the whole
            of what it takes (the goods strip, ChasmHud.h) - and who is carrying for it.
        */
        c.title += (info.storeys > 0 || (f_cells && info.floor_area > 0.0f)) ? " - building on" : " - being built";
        std::array<int,GOOD_COUNT> brought{};
        int cost = 0;
        std::string takes;
        for (int g : HudGoodsOrder()){
            if (site_cost[g] > 0){
                brought[g] = e ? std::min(site_cost[g],EconomySiteBrought(*e,id,g)) : 0;
                cost += site_cost[g];
                takes += Format("%s%i %s",takes.empty() ? "" : ", ",site_cost[g],GoodName(g));
            }
        }
        c.Stock("Brought","for this building",brought,cost,true);
        c.Text("It takes " + takes,HUD_TEXT_DIM);
        //Trees still standing on it (woodcutting_plan.md): nothing of it rises until a woodcutter has felled them.
        //...and BOULDERS (stone_plan.md), a stonecutter's.
        if (e){
            int trees = 0;
            int boulders = 0;
            if (f_cells){
                for (size_t q = 0; q < z.field.size(); q++){
                    trees += (z.field[q] == id && !z.field_built[q]) ? EconomyTreesOn(w,*e,-1,(int)q) : 0;
                    boulders += (z.field[q] == id && !z.field_built[q]) ? EconomyBouldersOn(w,*e,-1,(int)q) : 0;
                }
            }else{
                for (size_t v = 0; v < z.building.size(); v++){
                    trees += (z.building[v] == id && z.standing[v] == 0) ? EconomyTreesOn(w,*e,(int)v,-1) : 0;
                    boulders += (z.building[v] == id && z.standing[v] == 0) ? EconomyBouldersOn(w,*e,(int)v,-1) : 0;
                }
            }
            //...and why nobody is felling them, as NEEDS ATTENTION says (Economy::Derive): across the water, or too far.
            bool f_unreached = std::binary_search(e->trees_unreached.begin(),e->trees_unreached.end(),id);
            bool f_iced = std::binary_search(e->trees_iced.begin(),e->trees_iced.end(),id);
            if (trees > 0 && f_iced){
                c.Text(Format("%i tree%s to fell first - across the water: waits for the ice",trees,trees == 1 ? "" : "s"),
                       HUD_AMBER);
            }else if (trees > 0 && f_unreached){
                c.Text(Format("%i tree%s to fell first - no woodcutter near",trees,trees == 1 ? "" : "s"),HUD_AMBER);
            }else if (trees > 0){
                c.Text(Format("%i tree%s to fell first - a woodcutter's work",trees,trees == 1 ? "" : "s"),HUD_AMBER);
            }
            if (boulders > 0){
                c.Text(Format("%i boulder%s to break first - a stonecutter's work",boulders,boulders == 1 ? "" : "s"),HUD_AMBER);
            }
        }
        //Short of ROPE, with too little of it anywhere (goods_plan.md): the colony starts with none, so what it still
        //lacks to make some - as NEEDS ATTENTION's row says.
        if (e && site_cost[GOOD_ROPE] > brought[GOOD_ROPE]
            && EconomyGoodHeld(*e,GOOD_ROPE) < site_cost[GOOD_ROPE] - brought[GOOD_ROPE]){
            int chain = EconomyRopeChain(*e,z);
            if (chain == ECONOMY_ROPE_NO_HEMP){
                c.Text("No rope to be had: grow hemp on a field, and make it at a ropewalk",HUD_AMBER);
            }else if (chain == ECONOMY_ROPE_NO_ROPEWALK){
                c.Text("No rope to be had: build a ropewalk to make it of hemp",HUD_AMBER);
            }else{
                c.Text("Waiting for rope from the ropewalk",HUD_TEXT_DIM);
            }
        }
        if (f_field){
            //A field site (fields_plan.md): staked out, all its cells built at once.
            c.Text(Format("%i cell%s to build - then empty, to be sown",info.site_cells,info.site_cells == 1 ? "" : "s"),
                   HUD_TEXT_DIM);
        }else if (f_cells){
            //A range site (range_plan.md): the same, and a trainee a cell once it stands.
            c.Text(Format("%i cell%s to build - a trainee a cell",info.site_cells,info.site_cells == 1 ? "" : "s"),HUD_TEXT_DIM);
        }else{
            c.Text(Format("%i storey%s to build%s",info.site_storeys,info.site_storeys == 1 ? "" : "s",
                          info.storeys ? Format(", %i standing",info.storeys).c_str() : ""),HUD_TEXT_DIM);
        }
        if (e){
            int carriers = 0;
            for (const EconomyWorker& k : e->workers){
                carriers += (k.site == id) ? 1 : 0;
            }
            c.Text(carriers ? Format("%i carrying for it",carriers) : std::string("Waiting for someone free to carry"),
                   HUD_TEXT_DIM);
        }
        //The player's say over the queue (construction_plan.md): this one before every other site - a
        //building put first by one of its plots, a field by one of its cells (OVERLAY_HIT_SITE_FIRST).
        CardLine first;
        first.type = CARD_TOGGLE;
        first.text = "Build first";
        first.chips_on = b.f_priority;
        c.lines.push_back(first);
        /*
            CANCEL (construction_plan.md; the user's, 2026-10-16): the whole site taken off at once - a bridge is placed
            whole, and was erased plot by plot - while nothing of it stands. What it was brought is left as a crate.
        */
        if (info.storeys == 0 && !(f_cells && info.floor_area > 0.0f)){
            CardLine cancel;
            cancel.type = CARD_CHOICE;
            cancel.choice_action = OVERLAY_HIT_SITE_CANCEL;
            cancel.choices.push_back(std::make_pair(std::string("Cancel"),0));
            c.lines.push_back(cancel);
        }
        /*
            A field's starting crop, chosen while it is still being built (the user's, 2026-10-08), so its
            farmer sows it the day it stands - as long as it was never sown: a sown field growing by more
            cells keeps its next crop, on its card once they are built.
        */
        const EconomyField* grown = (f_field && e) ? EconomyFieldOf(*e,id) : NULL;
        if (f_field && (!grown || (!grown->f_sown && grown->last < 0))){
            c.Text("Start with:",HUD_TEXT_DIM);
            CardLine choice;
            choice.type = CARD_CHOICE;
            choice.choice_action = OVERLAY_HIT_FIELD_CROP;
            choice.choice_on = (b.crop < ZONE_CROP_COUNT || b.crop == ZONE_CROP_AUTO) ? (int)b.crop : -1;
            //AUTO as a start too (issues_plan.md), naming the most food: the frost is not asked until it stands.
            choice.choices.push_back(std::make_pair("Auto: " + CropTitle(EconomyAutoCrop(-1)),(int)ZONE_CROP_AUTO));
            for (int k = 0; k < ZONE_CROP_COUNT; k++){
                choice.choices.push_back(std::make_pair(CropTitle(k),k));
            }
            c.lines.push_back(choice);
        }
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
            //More than it holds - a child born into a full house (lifeline_plan.md): a storey or a plot more. What it
            //costs (building_size_plan.md): its people's work, and the chance of another child.
            if (living > holds){
                int pace = (int)std::lround(EconomyCrowdedPace(living - holds) * 100.0f);
                c.Text(Format("Crowded - they work at %i%%, a child less likely: add a storey or a plot",pace),HUD_AMBER);
            }
            //APPEAL (building_size_plan.md): what a couple here has of a child a year, and what raises it.
            int from_storeys = 0;
            int from_garden = 0;
            EconomyHouseAppeal(z,id,&from_storeys,&from_garden);
            std::string appeal;
            if (from_storeys > 0){
                appeal = Format("storeys +%i",from_storeys);
            }
            if (from_garden > 0){
                appeal += (appeal.empty() ? "" : ", ") + Format("garden +%i",from_garden);
            }
            c.Text(Format("A child: %i%% a year",EconomyBirthChance(*e,z,id))
                   + (appeal.empty() ? std::string(" - storeys and a garden raise it") : " (" + appeal + ")"),HUD_TEXT_DIM);
        }
        //SHUT IN on every side, with no door: nobody moves in, and those inside cannot get out (EconomyShutIn).
        if (b.kind == ZONE_KIND_HOUSE && EconomyShutIn(*e,id)){
            c.Text(living ? "Uninhabitable: shut in on every side - its family cannot get out"
                          : "Uninhabitable: shut in on every side, with no way in",living ? HUD_DANGER : HUD_AMBER);
        }
        //Its family, or the camp's settlers: a click on one selects him, at home or out (OVERLAY_HIT_FAMILY).
        for (const EconomyWorker& k : e->workers){
            if (k.house == id){
                c.Person(Format("  %s, %i - %s, %s",EconomyPersonName(k).c_str(),EconomyAgeAt(k.born,e->tick),JobTitle(k.job),
                                Doing(k,&z,e)),
                         k.id,EconomyIndoors(k) ? HUD_TEXT_DIM : HUD_TEXT);
            }
        }
        //Its LAND (land_plan.md): its fields and their built cells, who farms it, and what lies fallow.
        int fields = 0;
        int fallow = 0;
        std::vector<uint8_t> mine(z.buildings.size(),0);
        for (const EconomyField& f : e->fields){
            if (f.owner == id && f.building < mine.size()){
                mine[f.building] = 1;
                fields++;
                fallow += (f.building >= e->farmer_of.size() || !e->farmer_of[f.building]) ? 1 : 0;
            }
        }
        if (fields > 0){
            int cells = 0;
            for (size_t q = 0; q < z.field.size(); q++){
                cells += (z.field[q] < mine.size() && mine[z.field[q]] && z.field_built[q]) ? 1 : 0;
            }
            std::string farmers;
            for (const EconomyWorker& k : e->workers){
                if (k.job == WORKER_JOB_FARMER && k.building == id){
                    std::string name = EconomyPersonName(k);
                    farmers += (farmers.empty() ? "" : ", ") + name.substr(0,name.find(' '));
                }
            }
            c.Text(Format("Land: %i field%s, %i cell%s - %s",fields,fields == 1 ? "" : "s",cells,cells == 1 ? "" : "s",
                          farmers.empty() ? "nobody farms it" : ("farmed by " + farmers).c_str()),
                   farmers.empty() ? HUD_AMBER : HUD_TEXT);
            if (fallow > 0 && !farmers.empty()){
                c.Text(Format("%i field%s lie%s fallow: too few hands",fallow,fallow == 1 ? "" : "s",fallow == 1 ? "s" : ""),HUD_AMBER);
            }
        }
        if (b.kind == ZONE_KIND_HOUSE){
            YardLines(z,id,c);
        }
        /*
            Its family's own food, water and firewood (ECONOMY_HOUSE_KEEPS), which they live on: food and water in
            its PANTRY, against its room, and firewood in its WOODSHED, against that one's (Economy::HouseRoom - the
            two are apart, so one figure over both read "18 of 16"). KEPT while anyone lives there, anyone's once
            nobody does (EconomyPrivateStock). A camp's own is its pantry (STORES), its families', the same two
            rows while anyone lives there; its supplies, anyone's, follow below.
        */
        bool f_camp = b.kind == ZONE_KIND_CAMP;
        int room = (id < e->room.size()) ? e->room[id] : 0;
        int shed = (id < e->woodshed.size()) ? e->woodshed[id] : 0;
        std::array<int,GOOD_COUNT> own{};
        if (f_camp ? id < e->pantry.size() : id < e->stock.size()){
            own = f_camp ? e->pantry[id] : e->stock[id];
        }
        bool f_shown = f_camp ? living > 0 : room > 0;
        if (f_shown){
            bool f_kept = f_camp || EconomyPrivateStock(*e,z,id);
            std::string family = HouseFamily(*e,id);
            std::string whose = !f_kept ? "anyone's" : f_camp ? "the camp families'"
                              : family.empty() ? "its family's" : "the " + family + " family's";
            //Taken from only while nobody lives there - and a camp's pantry never: its supplies are what is taken.
            std::array<int,GOOD_COUNT> reserved = f_camp ? std::array<int,GOOD_COUNT>{} : EconomyReserved(*e,id);
            if (!f_kept && AnyGoods(own)){
                c.Text("Nobody lives here: anyone may take what it holds",HUD_TEXT_DIM);
            }
            std::array<int,GOOD_COUNT> food = own;
            std::array<int,GOOD_COUNT> fire{};
            std::array<int,GOOD_COUNT> food_reserved = reserved;
            std::array<int,GOOD_COUNT> fire_reserved{};
            std::swap(food[GOOD_WOOD],fire[GOOD_WOOD]);
            std::swap(food_reserved[GOOD_WOOD],fire_reserved[GOOD_WOOD]);
            c.Stock("Pantry",whose,food,room,f_kept,food_reserved);
            if (shed > 0 || fire[GOOD_WOOD] > 0){
                c.Stock("Woodshed",whose,fire,shed,f_kept,fire_reserved);
            }
        }
    }
    if (b.kind == ZONE_KIND_STORE && info.storeys > 0){
        int room = (id < e->room.size()) ? e->room[id] : 0;
        if (id < e->stock.size()){
            c.StockByGroup("Holds","anyone's",e->stock[id],room,false,EconomyReserved(*e,id));
        }
        bool f_attached = id < e->attached.size() && e->attached[id] != 0;
        CardLine chips;
        chips.type = CARD_CHIPS;
        chips.chips_on = f_attached ? EconomyAccepts(*e,id) : b.allow;
        chips.f_locked = f_attached;
        c.Text(f_attached ? "Takes (next to a workplace: its goods only):" : "Takes:",HUD_TEXT_DIM);
        c.lines.push_back(chips);
    }
    if (b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_FIELD || b.kind == ZONE_KIND_RANGE
        || b.kind == ZONE_KIND_ROPEWALK || b.kind == ZONE_KIND_STONECUTTER || b.kind == ZONE_KIND_MINE){
        /*
            A FIELD (fields_plan.md), once it stands: what grows on it, at what yield and how far; or, empty,
            what its farmer will sow. Then its choices - its starting crop until it has been sown once, then
            its next crop: AUTO (naming what it picks) or a crop, each with the yield it would get after the
            crop now growing (or last harvested).
        */
        const EconomyField* f = (f_field && info.site_cells == 0) ? EconomyFieldOf(*e,id) : NULL;
        int sow_now = -1;
        if (f){
            /*
                THE FROST (Economy.h): the growing weather the field has left - at its middle, where its farmer
                stands - against its farmer's pace: whether what grows ripens in time, and why nothing is
                sown when nothing is.
            */
            vec2 mid(0.0f,0.0f);
            auto first = std::find(z.field.begin(),z.field.end(),id);
            if (first != z.field.end()){
                for (int j = 0; j < 4; j++){
                    mid += w.grid->FineQuadCentre((int)(first - z.field.begin()) * 4 + j) * 0.25f;
                }
            }
            float pace = EconomyFieldPace(*e,z,id);
            int left = EconomyGrowingTicksLeft(w,mid,tick);
            int after = -1;
            if (f->f_sown && f->crop >= 0){
                float g = EconomyFieldGrowth(*e,z,id);
                c.Bar(Format("%s at %i%% yield - %.0f%% grown",CropTitle(f->crop).c_str(),f->yield,g * 100.0f),g);
                int ripen = EconomyRipenTicks(f->crop,f->grown,pace);
                if (pace > 0.0f && ripen > left){
                    int days = (ripen - left + CALENDAR_DAY_TICKS - 1) / CALENDAR_DAY_TICKS;
                    c.Text(Format("The frost will take it first - %i day%s short",days,days == 1 ? "" : "s"),HUD_DANGER);
                }
                after = f->crop;
            }else{
                if (f->frost >= 0){
                    c.Text(Format("The frost took its %s",ZoneCropName(f->frost)),HUD_AMBER);
                }
                int sow = EconomyFieldSowCrop(*f,b);
                sow_now = EconomyFieldSowNow(*f,b,pace,left);
                bool f_auto = (f->last >= 0) ? b.next_crop >= ZONE_CROP_COUNT : b.crop == ZONE_CROP_AUTO;
                if (sow < 0){
                    //The day it waits for the player, draining - then AUTO is chosen for him (issues_plan.md).
                    c.Bar("Empty - choose a crop to sow, or it starts with Auto",
                          1.0f - std::min(1.0f,(float)f->wait / ECONOMY_CROP_GRACE),HUD_AMBER);
                }else if (sow_now >= 0){
                    c.Text(Format("Empty - its farmer is to sow %s, at %i%%",ZoneCropName(sow_now),EconomyCropYield(f->last,sow_now)));
                }else if (CalendarDateOf(tick).season == SEASON_WINTER){
                    c.Text("Empty - nothing is sown in winter",HUD_TEXT_DIM);
                }else if (left == 0){
                    c.Text("Empty - under the snow until spring",HUD_TEXT_DIM);
                }else if (pace <= 0.0f){
                    c.Text(Format("Empty - %s, once somebody farms it",ZoneCropName(sow)),HUD_TEXT_DIM);
                }else if (!f_auto && EconomyRipenTicks(sow,0,pace) > EconomyGrowingSeasonTicks(w,mid,tick)){
                    c.Text(Format("%s never ripens here before the frost at its farmer's pace - choose another crop",
                                  CropTitle(sow).c_str()),HUD_AMBER);
                }else{
                    c.Text(f_auto ? std::string("Empty - too late in the year for any crop to ripen: sown in spring")
                                  : Format("Empty - too late in the year for %s to ripen: sown in spring",ZoneCropName(sow)),
                           HUD_TEXT_DIM);
                }
                after = f->last;
            }
            CardLine choice;
            choice.type = CARD_CHOICE;
            if (!f->f_sown && f->last < 0){
                c.Text("Start with:",HUD_TEXT_DIM);
                choice.choice_action = OVERLAY_HIT_FIELD_CROP;
                choice.choice_on = (b.crop < ZONE_CROP_COUNT || b.crop == ZONE_CROP_AUTO) ? (int)b.crop : -1;
                //AUTO names what it would sow now, the frost allowing - else the most food (issues_plan.md).
                ZoneBuilding as_auto = b;
                as_auto.crop = ZONE_CROP_AUTO;
                int pick = EconomyFieldSowNow(*f,as_auto,pace,left);
                choice.choices.push_back(std::make_pair("Auto: " + CropTitle(pick >= 0 ? pick : EconomyAutoCrop(-1)),
                                                        (int)ZONE_CROP_AUTO));
                for (int k = 0; k < ZONE_CROP_COUNT; k++){
                    choice.choices.push_back(std::make_pair(CropTitle(k),k));
                }
            }else{
                c.Text("Next:",HUD_TEXT_DIM);
                choice.choice_action = OVERLAY_HIT_FIELD_NEXT;
                choice.choice_on = b.next_crop;
                //AUTO names what it picks: while the field waits, what is sown now, the frost allowing.
                int pick = (!f->f_sown && sow_now >= 0 && b.next_crop >= ZONE_CROP_COUNT) ? sow_now : EconomyAutoCrop(after);
                choice.choices.push_back(std::make_pair("Auto: " + CropTitle(pick),(int)ZONE_CROP_AUTO));
                for (int k = 0; k < ZONE_CROP_COUNT; k++){
                    choice.choices.push_back(std::make_pair(Format("%s %i%%",CropTitle(k).c_str(),EconomyCropYield(after,k)),k));
                }
            }
            c.lines.push_back(choice);
        }
        if (f){
            /*
                WHOSE LAND (land_plan.md): its owner and the farmer its share falls to - or that it lies fallow -
                then the player's say: AUTO, or one of the nearest houses lived in, by their family's name.
            */
            std::string land = LandName(z,*e,f->owner) + " land";
            land[0] = (char)toupper(land[0]);
            c.Text(land,f->owner ? HUD_TEXT : HUD_AMBER);
            const EconomyWorker* k = PersonById(*e,(id < e->farmer_of.size()) ? e->farmer_of[id] : 0);
            if (k){
                c.Text(Format("%s (husbandry %i) - %s",EconomyPersonName(*k).c_str(),(int)k->skills[SKILL_HUSBANDRY],Doing(*k,&z)));
            }else{
                c.Text(f->owner ? "Lies fallow: nobody of the family is free to farm it" : "No house or camp can reach it to farm it",
                       HUD_AMBER);
            }
            vec2 at(0.0f,0.0f);
            int n_cells = 0;
            for (size_t q = 0; q < z.field.size(); q++){
                if (z.field[q] == id){
                    for (int j = 0; j < 4; j++){
                        at += w.grid->FineQuadCentre((int)q * 4 + j) * 0.25f;
                    }
                    n_cells++;
                }
            }
            at = at * (1.0f / (float)std::max(1,n_cells));
            std::vector<std::pair<float,uint32_t>> houses;
            for (size_t h = 1; h < z.buildings.size(); h++){
                int plot = (z.buildings[h].kind == ZONE_KIND_HOUSE && z.buildings[h].size > 0) ? FirstPlotOf(z,(uint32_t)h) : -1;
                if (plot >= 0 && !HouseFamily(*e,(uint32_t)h).empty()){
                    houses.push_back(std::make_pair((w.grid->fine.pos[plot] - at).length(),(uint32_t)h));
                }
            }
            std::sort(houses.begin(),houses.end());
            CardLine give;
            give.type = CARD_CHOICE;
            give.choice_action = OVERLAY_HIT_FIELD_LAND;
            bool f_given = std::any_of(houses.begin(),houses.end(),[&](const std::pair<float,uint32_t>& h){ return h.second == b.land_of; });
            give.choice_on = f_given ? (int)b.land_of : 0;
            give.choices.push_back(std::make_pair(std::string("Auto"),0));
            for (size_t t = 0; t < houses.size(); t++){
                //The four nearest - and the one it was given to, wherever that is.
                if (t < 4 || (f_given && houses[t].second == b.land_of)){
                    give.choices.push_back(std::make_pair(HouseFamily(*e,houses[t].second),(int)houses[t].second));
                }
            }
            if (houses.size() > 0){
                c.Text("Give to:",HUD_TEXT_DIM);
                c.lines.push_back(give);
            }
        }else{
            //A collector fills only off the snow (Economy.cpp): frozen, it gives nothing until the thaw and its
            //carrier waits at home - which, unexplained, looked like her doing nothing (user, 2026-10-08).
            if (b.kind == ZONE_KIND_WATER && info.storeys > 0 && EconomyFrozen(w,z,id,tick)){
                c.Text("Frozen: no water can be collected until the thaw",HUD_AMBER);
            }
            /*
                A ROPEWALK (goods_plan.md): too short to work below ECONOMY_ROPEWALK_MIN_PLOTS - painted, so not while
                its second plot is still being built - and it takes no ropemaker until that many stand; longer,
                faster; and no rope without hemp, which only a field grows.
            */
            bool f_short = false;
            if (b.kind == ZONE_KIND_ROPEWALK && info.storeys > 0){
                int plots = EconomyRopewalkPlots(z,id);
                f_short = b.size < ECONOMY_ROPEWALK_MIN_PLOTS;
                if (f_short){
                    c.Text(Format("Too short: a ropewalk works from %i plots - paint another beside it",ECONOMY_ROPEWALK_MIN_PLOTS),
                           HUD_AMBER);
                }else if (plots >= ECONOMY_ROPEWALK_MIN_PLOTS){
                    c.Text(Format("%i plots long: makes rope at x%.1f",plots,EconomyRopewalkPace(z,id)),HUD_TEXT_DIM);
                    std::array<int,GOOD_COUNT> stored = EconomyStoredTotals(*e,z);
                    if (stored[GOOD_HEMP] + ((id < e->stock.size()) ? e->stock[id][GOOD_HEMP] : 0) < ECONOMY_ROPE_HEMP){
                        c.Text("No hemp to make rope of - grow some on a field",HUD_AMBER);
                    }
                }
            }
            /*
                A MINE (stone_plan.md step 5): what it digs and what is left of it - a seam's floatstone running down
                to nothing, worked out at last; the mountain's stone and an outcrop's, never.
            */
            bool f_dug_out = false;
            if (b.kind == ZONE_KIND_MINE){
                int plot = FirstPlotOf(z,id);
                int d = (plot >= 0 && w.deposits) ? w.deposits->At(plot) : -1;
                if (d >= 0 && w.deposits->deposits[d].kind == DEPOSIT_SEAM){
                    const Deposit& dep = w.deposits->deposits[d];
                    int left = EconomyDepositLeft(*e,d);
                    f_dug_out = left <= 0;
                    if (f_dug_out){
                        c.Text("A floatstone seam - worked out: nothing is left to dig",HUD_AMBER);
                    }else{
                        c.Bar(Format("A floatstone seam - %i of %i left",left,dep.holds),(float)left / (float)std::max(1,dep.holds));
                    }
                }else if (d >= 0){
                    c.Text(w.deposits->deposits[d].kind == DEPOSIT_MOUNTAIN ? "Dug into the mountain: its stone never runs out"
                                                                          : "On an outcrop: its stone never runs out",HUD_TEXT_DIM);
                }
            }
            //WORKERS BY PLOTS (building_size_plan.md): how many it takes, and the plots that would take one more.
            if ((b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_STONECUTTER)
                && info.storeys > 0 && !b.f_stopped && id < e->places.size() && e->places[id] > 0){
                int places = e->places[id];
                int more = places * ECONOMY_PLOTS_PER_WORKER + 1;
                std::string takes = (places == 1) ? std::string("Takes one worker") : Format("Takes %i workers",places);
                c.Text((more <= ZoneKindMaxPlots(b.kind)) ? takes + Format(" - %i plots would take %i",more,places + 1)
                                                          : takes + Format(", one for every %i plots",ECONOMY_PLOTS_PER_WORKER),
                       HUD_TEXT_DIM);
            }
            bool f_worked = false;
            for (const EconomyWorker& k : e->workers){
                if (k.building == id){
                    f_worked = true;
                    int skill = EconomyJobSkill(k.job);
                    std::string line = Format("%s (%s %i) - %s",EconomyPersonName(k).c_str(),SkillName(skill),(int)k.skills[skill],
                                              Doing(k,&z,e));
                    if (k.job == WORKER_JOB_ARCHER){
                        line += Format(", archery %i%%",(int)(k.archery * 100LL / ARCHERY_FULL));     //range_plan.md
                    }
                    c.Text(line);
                }
            }
            //Something of it standing: a building's storey, or a range's built cell (range_plan.md).
            bool f_stands = info.storeys > 0 || (b.kind == ZONE_KIND_RANGE && info.floor_area > 0.0f);
            if (!f_worked && f_stands && b.f_stopped){
                //Stopped by the player (stopping_work_plan.md): nobody here is meant, not a want - and a collector
                //fills only while worked, so say what stops with it.
                c.Text(b.kind == ZONE_KIND_WATER ? "Stopped: no water is collected until it is set working again"
                       : b.kind == ZONE_KIND_RANGE ? "Stopped: nobody trains here until it is set going again"
                       : "Stopped: nobody works here until it is set working again",HUD_TEXT_DIM);
            }else if (!f_worked && f_stands && !f_short && !f_dug_out){
                c.Text(b.kind == ZONE_KIND_RANGE ? "Nobody trains here - it needs a free adult with a house"
                                                 : "Nobody works here - it needs a free adult with a house",HUD_AMBER);
            }
        }
        /*
            His wood goes in the hut, by its floor, and onto the lots of his yard (woodcutting_plan.md): with room for
            not even a log - a hut on a tiny plot, no lot - he fells nothing.
        */
        int wood_room = (b.kind == ZONE_KIND_WOODCUTTER && id < e->pile_room.size()) ? e->pile_room[id] : 0;
        bool f_no_room = b.kind == ZONE_KIND_WOODCUTTER && info.storeys > 0 && id < e->log_wood.size()
                         && wood_room < e->log_wood[id];
        if (f_no_room){
            c.Text("Too small to keep a log, and no lot beside it: nowhere to put wood",HUD_AMBER);
        }
        //What a log makes here (woodcutting_plan.md), and what a second storey on all of it would make of one.
        if (b.kind == ZONE_KIND_WOODCUTTER && id < e->log_wood.size() && e->log_wood[id] > 0){
            c.Text((e->log_wood[id] >= ECONOMY_LOFT_LOG_WOOD) ? Format("A log makes %i wood",e->log_wood[id])
                   : Format("A log makes %i wood - %i with a second storey on all of it",e->log_wood[id],ECONOMY_LOFT_LOG_WOOD),
                   HUD_TEXT_DIM);
        }
        if (b.kind == ZONE_KIND_WOODCUTTER){
            YardLines(z,id,c);
        }
        /*
            What waits here to be carried off: the woodpile and the collector's water, anyone's (FetchFrom), each
            against what it holds at most; a field's harvest, KEPT for its land - its family fetches it, its farmer
            carries the rest on (land_plan.md).
        */
        std::array<int,GOOD_COUNT> held = (id < e->stock.size()) ? e->stock[id] : std::array<int,GOOD_COUNT>{};
        if (b.kind == ZONE_KIND_WOODCUTTER && (wood_room > 0 || AnyGoods(held))){
            //What it holds, against its room, and where that room is.
            int indoors = (id < e->hut_room.size()) ? e->hut_room[id] : 0;
            int outside = wood_room - indoors;
            c.Stock("Wood","anyone's",held,wood_room,false,EconomyReserved(*e,id));
            std::string lot = (outside > 0) ? Format("%i on its lot",outside) : std::string("no lot");
            c.Text(indoors > 0 ? Format("Keeps %i indoors, ",indoors) + lot : "Too small to keep wood indoors - " + lot,HUD_TEXT_DIM);
        }else if (b.kind == ZONE_KIND_WATER && (info.storeys > 0 || AnyGoods(held))){
            c.Stock("Collected","anyone's",held,(id < e->water_room.size()) ? e->water_room[id] : 0,false,EconomyReserved(*e,id));
        }else if (b.kind == ZONE_KIND_ROPEWALK && (info.storeys > 0 || AnyGoods(held))){
            //Its rope, anyone's against the pile it stops at, and the hemp it is to be made of - its ropemaker's alone.
            std::array<int,GOOD_COUNT> rope{};
            std::array<int,GOOD_COUNT> hemp{};
            rope[GOOD_ROPE] = held[GOOD_ROPE];
            hemp[GOOD_HEMP] = held[GOOD_HEMP];
            c.Stock("Rope","anyone's",rope,ECONOMY_ROPEWALK_PILE,false,EconomyReserved(*e,id));
            if (hemp[GOOD_HEMP] > 0){
                c.Stock("Hemp","to make rope of",hemp,0,true);
            }
            if (info.storeys > 0){
                KeptLines(*e,z,GOOD_ROPE,c);
            }
        }else if (f_field && AnyGoods(held)){
            const EconomyField* land = EconomyFieldOf(*e,id);
            uint32_t owner = land ? land->owner : 0;
            c.Stock("Harvest",LandName(z,*e,owner),held,0,true,EconomyReserved(*e,id));
            if (owner && owner < z.buildings.size()){
                c.Text(z.buildings[owner].kind == ZONE_KIND_CAMP ? "Its farmer carries it to the camp's supplies"
                                                                 : "Its family's first - the rest is carried to a store",HUD_TEXT_DIM);
            }
        }else if (AnyGoods(held)){
            c.Text("Waiting to be carried: " + GoodsList(*e,id),HUD_TEXT_DIM);
        }
    }
    //A camp's SUPPLIES (STORES): the settlers' and the colony's, anyone's, under its families' own pantry above.
    if (b.kind == ZONE_KIND_CAMP && id < e->stock.size() && AnyGoods(e->stock[id])){
        c.StockByGroup("Supplies","anyone's",e->stock[id],0,false,EconomyReserved(*e,id));
    }
    if (b.kind == ZONE_KIND_WINCH && info.storeys > 0){
        c.Text("Lowers to the balcony below",HUD_TEXT_DIM);
    }
    //A WELL (water_tools_plan.md): the water it still holds, anyone's - or that it is kept full.
    if (b.kind == ZONE_KIND_WELL && id < e->stock.size()){
        std::array<int,GOOD_COUNT> water{};
        water[GOOD_WATER] = e->stock[id][GOOD_WATER];
        bool f_endless = EconomyWellEndless(*e,id);
        c.Stock("Water",f_endless ? "anyone's, kept full" : "anyone's",water,0,false,EconomyReserved(*e,id));
        c.Text(f_endless ? "Endless: it never runs dry" : (water[GOOD_WATER] > 0) ? "It runs dry once this is drawn"
                                                                              : "Drawn dry",
               (!f_endless && water[GOOD_WATER] == 0) ? HUD_AMBER : HUD_TEXT_DIM);
    }
    //Its builders (builders_plan.md): its places filled before any other workplace's.
    if (b.kind == ZONE_KIND_BUILDERS){
        int places = (id < e->places.size()) ? e->places[id] : 0;
        int working = 0;
        for (const EconomyWorker& k : e->workers){
            working += (k.building == id) ? 1 : 0;
        }
        if (b.f_stopped && working == 0){
            c.Text("Stopped: it takes no builders until it is set working again",HUD_TEXT_DIM);
        }else{
            c.Text(Format("%i of %i builder%s",working,places,places == 1 ? "" : "s"),(working < places) ? HUD_AMBER : HUD_TEXT);
        }
        if (info.storeys == 0 && !b.f_stopped){
            c.Text("Its first builder builds it",HUD_TEXT_DIM);
        }
        for (const EconomyWorker& k : e->workers){
            if (k.building == id){
                c.Text(Format("  %s (strength %i) - %s",EconomyPersonName(k).c_str(),(int)k.skills[SKILL_STRENGTH],Doing(k,&z)));
            }
        }
        if (working < places){
            c.Text("Too few people: every place here is filled before any other workplace's",HUD_TEXT_DIM);
        }
        //PROVISIONS (defence_plan.md): the wheat and beans to make them, and those made - kept: for the sites'
        //carriers, and nobody eats from it. Then what the colony keeps of them (goods_plan.md).
        if (id < e->stock.size() && AnyGoods(e->stock[id])){
            c.Stock("Holds","for provisions",e->stock[id],0,true,EconomyReserved(*e,id));
            c.Text("Wheat and beans are made into provisions here, for the sites and the stores",HUD_TEXT_DIM);
        }
        if (info.storeys > 0){
            KeptLines(*e,z,GOOD_PROVISIONS,c);
        }
    }
    /*
        STOPPING WORK (stopping_work_plan.md, the user's 2026-10-12): a workplace that stands - a storey, or a range's
        built cell - may be stopped, so its people are free for other work, and set going again (OVERLAY_HIT_WORK_STOP).
    */
    if (ZoneKindStoppable(b.kind) && (info.storeys > 0 || info.floor_area > 0.0f)){
        CardLine stop;
        stop.type = CARD_TOGGLE;
        stop.text = "Stop work";
        stop.chips_on = b.f_stopped ? 1 : 0;
        stop.choice_action = OVERLAY_HIT_WORK_STOP;
        c.lines.push_back(stop);
    }
    /*
        A GUARD TOWER (defence_plan.md): its guards, staffed by archery and then precision, and its stock - water and
        provisions, KEPT: what its guards live on, and nobody else may take.
    */
    if (b.kind == ZONE_KIND_TOWER){
        int places = (id < e->places.size()) ? e->places[id] : 0;
        int plot = FirstPlotOf(z,id);
        bool f_up = EconomyTowerUp(z,plot);
        int working = 0;
        for (const EconomyWorker& k : e->workers){
            working += (k.job == WORKER_JOB_GUARD && k.building == id) ? 1 : 0;
        }
        if (places > 0){
            c.Text(Format("%i of %i guard%s",working,places,places == 1 ? "" : "s"),(working < places) ? HUD_AMBER : HUD_TEXT);
        }else if (f_up){
            c.Text("Unmanned: it takes no guards");
        }
        for (const EconomyWorker& k : e->workers){
            if (k.job == WORKER_JOB_GUARD && k.building == id){
                c.Text(Format("  %s (archery %i%%, precision %i)%s - %s",EconomyPersonName(k).c_str(),
                              (int)(k.archery * 100LL / ARCHERY_FULL),(int)k.skills[SKILL_PRECISION],
                              k.f_chosen ? ", by hand" : "",Doing(k,&z,e)));
            }
        }
        if (places > 0 && working < places){
            c.Text("A guard needs a free adult with a house - the best archer first",HUD_TEXT_DIM);
        }
        /*
            GUARDS BY HAND (step 8): how many it takes, 0 up to its junction's places - chips, a recorded command
            (OVERLAY_HIT_TOWER_GUARDS) - and a Release chip for each guard it has (OVERLAY_HIT_GUARD_RELEASE, his id).
        */
        if (f_up && id < e->guards.size() && e->guards[id] >= 0){
            c.Text("Guards it takes",HUD_TEXT_DIM);
            CardLine count;
            count.type = CARD_CHOICE;
            for (int n = 0; n <= ZoneTowerGuards(w,plot); n++){
                count.choices.push_back(std::make_pair(std::to_string(n),n));
            }
            count.choice_on = e->guards[id];
            count.choice_action = OVERLAY_HIT_TOWER_GUARDS;
            c.lines.push_back(count);
            CardLine release;
            release.type = CARD_CHOICE;
            for (const EconomyWorker& k : e->workers){
                if (k.job == WORKER_JOB_GUARD && k.building == id){
                    release.choices.push_back(std::make_pair(FirstNameOf(*e,k.id),(int)k.id));
                }
            }
            release.choice_action = OVERLAY_HIT_GUARD_RELEASE;
            if (!release.choices.empty()){
                c.Text("Release a guard - the tower takes one fewer",HUD_TEXT_DIM);
                c.lines.push_back(release);
            }
            c.Text("Right click it with someone selected to post him here",HUD_TEXT_DIM);
        }
        if (id < e->stock.size() && (info.storeys > 0 || AnyGoods(e->stock[id]))){
            int room = EconomyTowerStock(w,plot,GOOD_WATER) + EconomyTowerStock(w,plot,GOOD_PROVISIONS);
            c.Stock("Holds","its guards'",e->stock[id],room,true,EconomyReserved(*e,id));
            c.Text("Its guards eat and drink from it, and nobody else",HUD_TEXT_DIM);
        }
        //KEPT FULL by the builders, ahead of their building (step 7): what it lacks, what is on its way, and
        //whether there is anyone to bring it - the jobless never do.
        if (f_up && id < e->stock.size()){
            std::array<int,GOOD_COUNT> lack = EconomyTowerShort(*e,w,id,plot);
            std::array<int,GOOD_COUNT> coming = EconomyComing(*e,id);
            int builders = 0;
            for (const EconomyWorker& k : e->workers){
                builders += (k.job == WORKER_JOB_BUILDER) ? 1 : 0;
            }
            std::string what;
            for (int g : {GOOD_WATER,GOOD_PROVISIONS}){
                if (lack[g] > 0){
                    what += Format("%s%i %s",what.empty() ? "" : " and ",lack[g],GoodName(g));
                }
            }
            int on_way = coming[GOOD_WATER] + coming[GOOD_PROVISIONS];
            if (what.empty()){
                c.Text("Full - the builders keep it so, before their building",HUD_TEXT_DIM);
            }else if (builders == 0){
                c.Text("Short of " + what + ", and no builders to keep it full",HUD_AMBER);
            }else if (on_way > 0){
                c.Text("Short of " + what + ": the builders are bringing it",HUD_TEXT);
            }else{
                c.Text("Short of " + what + ": the builders will bring it",HUD_TEXT);
            }
        }
        //Its LADDER is its door: guards climb it only from that side. Moved round to the next open side by its chip,
        //a recorded zone command (OVERLAY_HIT_LADDER).
        c.Text(Format("A %i-sided tower: %i guard%s. Its ladder is its door",w.picker->PlotQuadCount(plot),
                      ZoneTowerGuards(w,plot),ZoneTowerGuards(w,plot) == 1 ? "" : "s"),HUD_TEXT_DIM);
        CardLine ladder;
        ladder.type = CARD_TOGGLE;
        ladder.text = "Move ladder";
        ladder.choice_action = OVERLAY_HIT_LADDER;
        c.lines.push_back(ladder);
    }
}

/*
    A GATE's card (defence_plan.md, step 3): its door, open or shut, as a choice of two chips - a recorded
    zone command (OVERLAY_HIT_GATE) - and what that means for the walks through it.
*/
void GateCard(const ChasmWorld& w, const ZoneState& z, int plot, Card& c){
    c.title = "Gate";
    bool f_shut = ZoneGateShut(z,plot);
    if (f_shut){
        c.Text("Shut - nobody passes, our own people neither",HUD_AMBER);
    }else{
        c.Text("Open - anyone passes");
    }
    CardLine door;
    door.type = CARD_CHOICE;
    door.choices = {{"Open",0},{"Shut",1}};
    door.choice_on = f_shut ? 1 : 0;
    door.choice_action = OVERLAY_HIT_GATE;
    c.lines.push_back(door);
    //Its wall still planned beside it: a gap the door does not close.
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        if (ZoneWallPlanned(z,nb[i])){
            c.Text("Its wall still has gaps to build",HUD_TEXT_DIM);
            break;
        }
    }
}

/*
    A GARDEN'S OR LOT'S card while it is being built (GroundPatch): as a field site's - the wood brought of what its
    plots still take, how many are still to build, and who is carrying for it - and whose yard it is. No "Build
    first": ground has no place in the queue (construction_plan.md), and a site of it goes by its walk.
*/
void GroundSiteCard(const ZoneState& z, const EconomyState* e, const std::vector<int>& patch, Card& c){
    bool f_garden = z.ground[patch[0]] == ZONE_GROUND_GARDEN;
    int sites = 0;
    int need = 0;
    std::array<int,GOOD_COUNT> brought{};
    for (int v : patch){
        if (GroundSitePlot(z,v)){
            int n = EconomyGroundCost(*z.world,z,v);
            sites++;
            need += n;
            brought[GOOD_WOOD] += e ? std::min(n,EconomyGroundBrought(*e,v)) : 0;
        }
    }
    int standing = (int)patch.size() - sites;
    c.title = std::string(f_garden ? "Garden" : "Lot") + (standing > 0 ? " - building on" : " - being built");
    c.Stock("Brought",f_garden ? "for this garden" : "for this lot",brought,need,true);
    c.Text(Format("It takes %i wood",need),HUD_TEXT_DIM);
    //Trees still standing on it (woodcutting_plan.md): a plot of it goes up only once a woodcutter has felled its.
    int trees = 0;
    int boulders = 0;       //...and boulders (stone_plan.md)
    for (int v : patch){
        trees += (e && GroundSitePlot(z,v)) ? EconomyTreesOn(*z.world,*e,v,-1) : 0;
        boulders += (e && GroundSitePlot(z,v)) ? EconomyBouldersOn(*z.world,*e,v,-1) : 0;
    }
    if (trees > 0){
        c.Text(Format("%i tree%s to fell first - a woodcutter's work",trees,trees == 1 ? "" : "s"),HUD_AMBER);
    }
    if (boulders > 0){
        c.Text(Format("%i boulder%s to break first - a stonecutter's work",boulders,boulders == 1 ? "" : "s"),HUD_AMBER);
    }
    c.Text(Format("%i plot%s to build%s",sites,sites == 1 ? "" : "s",
                  standing ? Format(", %i standing",standing).c_str() : ""),HUD_TEXT_DIM);
    //Whose yard (yards_plan.md): a house's by its family, a hut's, or nobody's until a building beside it takes it.
    uint32_t owner = ZoneGroundOwner(z,patch[0]);
    if (owner && z.buildings[owner].kind == ZONE_KIND_HOUSE){
        std::string family = e ? HouseFamily(*e,owner) : std::string();
        c.Text(family.empty() ? std::string("Yard of an empty house") : "Yard of the " + family + " family's house");
    }else if (owner){
        c.Text(std::string("Yard of a ") + (z.buildings[owner].kind == ZONE_KIND_WOODCUTTER ? "woodcutter's hut" : "building"));
    }else{
        c.Text("Nobody's yard",HUD_TEXT_DIM);
    }
    if (e){
        int carriers = 0;
        for (const EconomyWorker& k : e->workers){
            carriers += (EconomyIsGroundSite(k.site)
                         && std::binary_search(patch.begin(),patch.end(),EconomyGroundSitePlot(k.site))) ? 1 : 0;
        }
        c.Text(carriers ? Format("%i carrying for it",carriers) : std::string("Waiting for someone free to carry"),
               HUD_TEXT_DIM);
    }
}

/*
    A CRATE's card (crates_plan.md, step 3): what it holds - anyone's, striped where somebody is already on his
    way to take it - where it came from and when, and who is coming for it and what for (Doing). The day is
    the season's, with the year only when it is not this one.
*/
void CrateCard(const ZoneState& z, const EconomyState& e, const EconomyCrate& crate, uint64_t tick, Card& c){
    uint32_t id = ECONOMY_STORE_CRATE | crate.id;
    c.title = (crate.origin == CRATE_FOUND) ? "Found crate" : (crate.origin == CRATE_CAMP) ? "A camp's goods"
            : (crate.origin == CRATE_SITE) ? "A site's goods"
            : EconomyCrateCrystal(crate) ? "Floatstone" : EconomyCrateRubble(crate.origin) ? "Rubble" : "Crate";
    c.StockByGroup("Holds","anyone's",crate.stock,0,false,EconomyReserved(e,id));
    CalendarDate when = CalendarDateOf(crate.tick);
    std::string date = std::string(CalendarSeasonName(when.season)) + " " + std::to_string(when.day)
                       + ((when.year != CalendarDateOf(tick).year) ? Format(", year %i",when.year) : std::string());
    std::string by = FirstNameOf(e,crate.by);
    if (crate.origin == CRATE_FOUND){
        c.Text("Found " + date,HUD_TEXT_DIM);
    }else if (crate.origin == CRATE_CAMP){
        c.Text("Left where a camp stood, " + date,HUD_TEXT_DIM);     //its last tent packed or erased (settlements_plan.md)
    }else if (crate.origin == CRATE_SITE){
        c.Text("Brought for a building taken off, " + date,HUD_TEXT_DIM);  //cancelled, or erased (construction_plan.md)
    }else if (crate.origin == CRATE_RUBBLE && EconomyCrateCrystal(crate)){
        c.Text("Fallen from a seam in the wall",HUD_TEXT_DIM);      //a seam's crystal (stone_plan.md)
    }else if (crate.origin == CRATE_RUBBLE){
        c.Text("Fallen from the chasm's walls",HUD_TEXT_DIM);       //laid with the colony, so no date (stone_plan.md)
    }else if (crate.origin == CRATE_BROKEN){
        c.Text("Broken from a rock, " + date,HUD_TEXT_DIM);         //by a stonecutter (stone_plan.md)
    }else{
        c.Text(by.empty() ? "Put down " + date : "Dropped by " + by + ", " + date,HUD_TEXT_DIM);
    }
    int coming = 0;
    for (const EconomyWorker& k : e.workers){
        if (k.store == id){
            c.Text(EconomyPersonName(k) + ", " + Doing(k,&z));
            coming++;
        }
    }
    if (!coming){
        c.Text("Nobody is on the way to it",HUD_TEXT_DIM);
    }
    //PICK UP (stone_plan.md): anyone free fetches it before building, from anywhere - lit while they are to. Free is a
    //builder, or anyone jobless and not posted (TickWorker's gate), so with none of either the card says why nothing happens.
    if (crate.f_pick_up && std::none_of(e.workers.begin(),e.workers.end(),[](const EconomyWorker& k){
            return k.job == WORKER_JOB_BUILDER || (k.job == WORKER_JOB_NONE && k.post < 0);
        })){
        c.Text("Nobody free to pick it up: no builders, and everyone has a job",HUD_AMBER);
    }else if (crate.f_pick_up && crate.f_too_far && !coming){
        //Turned down at the last look (ECONOMY_PICK_UP_WATER): more than that walk without water, there and to a store.
        float days = ECONOMY_PICK_UP_WATER * (float)NEEDS_WATER_FULL / (float)CALENDAR_DAY_TICKS;
        c.Text(Format("Too far to fetch: over %g days there and to a store",days),HUD_AMBER);
    }
    CardLine pick;
    pick.type = CARD_TOGGLE;
    pick.text = "Pick up";
    pick.chips_on = crate.f_pick_up ? 1 : 0;
    pick.choice_action = OVERLAY_HIT_PICK_UP;
    c.lines.push_back(pick);
}

/*
    A GROUP'S CARD (selection_plan.md, "Several people selected"): a row each - his badge, name, job and what he is
    doing, dimmed at home, and how long his pack lasts - each a click away from being selected alone, or an x from
    leaving the group. Then, for everyone posted among them, Release, Grab Supplies and Pack Tents, on one row: each
    person's own command, as on his card, lit while all of them are at it; and for those carrying a tent, Pitch.
*/
void GroupCard(const ZoneState& z, const EconomyState& e, const std::vector<uint32_t>& group, Card& c){
    c.title = Format("%i people",(int)group.size());
    int posted = 0;
    int gathering = 0;
    int tentless = 0;       //posted with no tent on them
    int taking = 0;         //...told to take one
    int carrying = 0;       //anyone with one on him
    int settling = 0;       //sent into another settlement, there now, and no tent on him
    for (uint32_t id : group){
        const EconomyWorker* k = PersonById(e,id);
        if (!k){
            continue;
        }
        CardLine l;
        l.type = CARD_PERSON;
        l.person = id;
        l.text = Format("%s - %s, %s",EconomyPersonName(*k).c_str(),JobTitle(k->job),Doing(*k,&z,&e));
        l.colour = EconomyIndoors(*k) ? HUD_TEXT_DIM : HUD_TEXT;
        //His pack by what runs out first: what a scout's day out is measured in.
        if (AnyGoods(k->pack)){
            float water = (float)k->pack[GOOD_WATER] * NEEDS_WATER_WORTH / CALENDAR_DAY_TICKS;
            float food = (float)EconomyPackFood(*k) / CALENDAR_DAY_TICKS;
            float days = std::min(water,food);
            l.right = (days < 1.0f) ? std::string("pack under a day") : Format("pack %.0f day%s",std::floor(days),
                                                                                 std::floor(days) == 1.0f ? "" : "s");
        }
        c.lines.push_back(l);
        if (!EconomyCanOrder(*k,e.tick)){
            continue;       //a child is given no order, and goes along with his grown-ups (WITH THE GROWN)
        }
        posted += (k->post >= 0) ? 1 : 0;
        gathering += (k->post >= 0 && k->gather > 0) ? 1 : 0;
        tentless += (k->post >= 0 && k->tent != ECONOMY_TENT_CARRIED) ? 1 : 0;
        taking += (k->post >= 0 && k->tent == ECONOMY_TENT_TO_TAKE) ? 1 : 0;
        carrying += (k->tent == ECONOMY_TENT_CARRIED) ? 1 : 0;
        settling += CanSettleHere(e,*k) ? 1 : 0;
    }
    if (posted > 0){
        c.Text(posted == (int)group.size() ? std::string("Posted") : Format("%i of them posted",posted),HUD_AMBER);
        CardLine release;
        release.type = CARD_TOGGLE;
        release.text = "Release";
        release.choice_action = OVERLAY_HIT_RELEASE;
        c.lines.push_back(release);
        CardLine gather;
        gather.type = CARD_TOGGLE;
        gather.text = "Grab Supplies";      //the person card's words (the user's, 2026-10-19)
        gather.chips_on = (gathering == posted) ? 1 : 0;
        gather.choice_action = OVERLAY_HIT_GATHER;
        c.lines.push_back(gather);
        //A tent each (settlements_plan.md) - for those posted without one, lit while all of them are to take one.
        if (tentless > 0){
            CardLine tent;
            tent.type = CARD_TOGGLE;
            tent.text = "Pack Tents";       //"Pack a Tent" on his card: one each
            tent.chips_on = (taking == tentless) ? 1 : 0;
            tent.choice_action = OVERLAY_HIT_TENT;
            c.lines.push_back(tent);
        }
    }
    //Those sent into another settlement and there now, with no tent: they move in as they are, each to a bed to spare.
    if (settling > 0){
        CardLine settle;
        settle.type = CARD_TOGGLE;
        settle.text = (settling == (int)group.size()) ? std::string("Settle here")
                                                      : Format("Settle here - %i of them",settling);
        settle.choice_action = OVERLAY_HIT_SETTLE;
        c.lines.push_back(settle);
    }
    //Their tents pitched where they stand, each beside the last: one camp.
    if (carrying > 0){
        c.Text(carrying == 1 ? std::string("One of them carries a tent") : Format("%i of them carry tents",carrying),HUD_AMBER);
        CardLine pitch;
        pitch.type = CARD_TOGGLE;
        pitch.text = carrying == 1 ? "Pitch the tent here" : "Pitch the tents here";
        pitch.choice_action = OVERLAY_HIT_PITCH;
        c.lines.push_back(pitch);
    }
}

bool MakeCard(const PlayPick& p, const ChasmWorld& w, const ZoneState& z, const EconomyState* e, uint64_t tick, Card& c,
              const std::vector<uint32_t>& group = {}){
    if (p.kind == PLAY_PICK_GROUP){
        if (e && !group.empty()){
            GroupCard(z,*e,group,c);
            return true;
        }
        return false;
    }
    if (p.kind == PLAY_PICK_CRATE && e){
        for (const EconomyCrate& crate : e->crates){
            if (crate.id == p.id){
                CrateCard(z,*e,crate,tick,c);
                return true;
            }
        }
        return false;       //emptied, or built over, since it was picked
    }
    if (p.kind == PLAY_PICK_GATE){
        if (p.plot >= 0 && p.plot < (int)z.gate.size() && z.gate[p.plot]){
            GateCard(w,z,p.plot,c);
            return true;
        }
    }else if (p.kind == PLAY_PICK_GROUND){
        //Gone once it all stands, or is erased: nothing of it left to build.
        std::vector<int> patch;
        GroundPatch(z,(int)p.id,patch);
        if (!patch.empty()){
            GroundSiteCard(z,(e && e->world == z.world) ? e : NULL,patch,c);
            return true;
        }
    }else if (p.kind == PLAY_PICK_PERSON && e){
        const EconomyWorker* k = PersonById(*e,p.id);
        if (k){
            PersonCard(w,z,*e,*k,c);
            return true;
        }
    }else if (p.kind == PLAY_PICK_BUILDING && p.id < z.buildings.size() && z.buildings[p.id].size > 0){
        BuildingCard(w,z,(e && e->world == z.world) ? e : NULL,p.id,tick,c);
        return true;
    }
    return false;
}

/*
    A building alone, for its outline: the zones with every other building taken out, so its cells are
    drawn as if it stood by itself - its own silhouette, walls closed where it meets a neighbour. A field
    is a slab over its cells instead: its crops would outline every plant. A garden or lot being built
    (PLAY_PICK_GROUND), named as a worker's ground site is (ECONOMY_SITE_GROUND | its lowest plot), is its
    patch's plots as tiles on the ground.
*/
void BuildSelectionMesh(const ChasmWorld& w, const ZoneState& z, uint32_t id, std::vector<vertex>& out){
    out.clear();
    if (EconomyIsGroundSite(id)){
        std::vector<int> patch;
        GroundPatch(z,EconomyGroundSitePlot(id),patch);
        for (int v : patch){
            BuildPlotTile(w,v,FIELD_OUTLINE_LIFT,out);
        }
        return;
    }
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].size <= 0){
        return;
    }
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    if (ZoneKindOnCells(z.buildings[id].kind)){
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
    only.gate = z.gate;     //a guard tower's ladder goes on a side no wall or gate runs off (BuildTower)
    only.field.assign(z.field.size(),0);
    //Its ARCHES too (arches_plan.md): part of it, outlined with it - given here, since with its neighbours gone
    //this state could not work them out.
    only.arch.assign(z.arch.size(),0);
    only.arch_owner.assign(z.arch_owner.size(),0);
    only.arch_built.assign(z.arch_built.size(),0);
    std::vector<int> quads;
    for (size_t v = 0; v < z.building.size(); v++){
        bool f_arch = z.ArchOwnerOf((int)v) == id;
        if (z.building[v] != id && !f_arch){
            continue;
        }
        if (f_arch){
            only.arch[v] = z.arch[v];
            only.arch_owner[v] = id;
            only.arch_built[v] = z.arch_built[v];
        }else{
            only.building[v] = id;
            only.storeys[v] = z.storeys[v];
            only.standing[v] = z.standing[v];
        }
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
        //An ARCH (arches_plan.md): its owner's, picked by its body above the passage (ArchUnder).
        if (z.ArchOf(plot) > 0){
            const float half = ZONE_STOREY_HEIGHT * 0.5f;
            return (float)std::lround(w.terrain->ground[plot] / half) * half + z.ArchOf(plot) * ZONE_STOREY_HEIGHT + SHAPE_ROOF;
        }
        return -1e30f;
    }
    float ground = w.terrain->ground[plot];
    switch (z.KindOf(plot)){
        case ZONE_KIND_CAMP:    return ground + SHAPE_TENT;
        case ZONE_KIND_WINCH:   return ground + SHAPE_WINCH;
        case ZONE_KIND_MINE:    return ground + SHAPE_WINCH;      //its portal, about a winch's height
        case ZONE_KIND_WELL:    return ground + SHAPE_WELL;
        case ZONE_KIND_BRIDGE:  return terrain_levels[w.terrain->level[plot]].height + ZONE_BRIDGE_DECK + 0.45f;
        default:{
            const float half = ZONE_STOREY_HEIGHT * 0.5f;
            float foot = (float)std::lround(ground / half) * half;
            return foot + z.storeys[plot] * ZONE_STOREY_HEIGHT + SHAPE_ROOF;
        }
    }
}

//A crate's middle (crates_plan.md), on the ground it is drawn on (ApplicationChasmCrates.cpp): where it is picked.
vec3 CrateMiddle(const ChasmWorld& w, const EconomyCrate& c){
    return vec3(c.pos.x,w.terrain->GroundHeight(c.pos,w.terrain->Height(c.plot)) + CRATE_MIDDLE,c.pos.y);
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
            //An arch is its owner's above its passage; through the passage the ray goes on, as a walker does.
            if (!z.building[at.plot]){
                const float half = ZONE_STOREY_HEIGHT * 0.5f;
                if (p.y < (float)std::lround(w.terrain->ground[at.plot] / half) * half + ZONE_STOREY_HEIGHT){
                    continue;
                }
                *plot_out = at.plot;
                return z.ArchOwnerOf(at.plot);
            }
            *plot_out = at.plot;
            return z.building[at.plot];
        }
    }
    return 0;
}


}

//--- Picking (physics thread) ----------------------------------------------------------------------

/*
    What is under the cursor in play mode: a person first, since he stands on a plot that would otherwise
    win; then a crate, the nearest on screen within CRATE_PICK_PX of its middle (crates_plan.md) and not under
    the clouds; then the
    building or field on the plot under the cursor - or a gate there, or a garden or lot being built.

    A PERSON BY HIS FIGURE (selection_plan.md, "Picked by the figure"). The people are pickable objects, so
    the engine reads back which one is under the cursor - exact at every zoom, and nobody hidden behind a
    roof is picked through it. At the far zoom a figure is a few pixels wide, so failing that, the nearest
    whose figure's middle line - feet to crown, on screen - passes within PERSON_NEAR_PX of the cursor, unless
    what the cursor is on stands in front of him. Close up, that lies inside the figure, which is target enough.
    The readback is a frame or two old: it counts only if it was read
    where the cursor is now, give or take PICK_READ_SLOP (a tool's pixel, InputController::PinPickPixel).
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
    vec2 at((float)px.x,(float)px.y);
    if (e && e->world == w){
        InputController* input = main_scene->inputcontroller;
        int2 read_at = input->GetHoveredPixel();
        bool f_read_here = std::abs(read_at.x - px.x) <= PICK_READ_SLOP && std::abs(read_at.y - px.y) <= PICK_READ_SLOP;
        objectid_t under = f_read_here ? input->GetHoveredObjectID() : OBJECTID_INVALID;
        if (under != OBJECTID_INVALID){
            std::lock_guard<std::mutex> lock(pick_mutex);
            for (const auto& f : person_objects){
                if (f.first == under){
                    out.kind = PLAY_PICK_PERSON;
                    out.id = f.second;
                    return out;
                }
            }
        }
        //How far from the eye the surface under the cursor is - none known over the sky, water or a tree.
        const vec3 eye = main_scene->camera->GetPosition();
        float surface = (under != OBJECTID_INVALID) ? (input->GetHoveredPosition() - eye).length() : INFINITY;
        float best = PERSON_NEAR_PX * s;
        for (const EconomyWorker& k : e->workers){
            if (EconomyIndoors(k)){
                continue;
            }
            vec3 feet = FeetOf(*w,z.get(),k);
            float tall = worker_figure_height[PersonFigure(k,e->tick)];
            vec2 a, b;
            if (!ScreenOf(main_scene->camera,(float)main_window->width,(float)main_window->height,feet,a) ||
                !ScreenOf(main_scene->camera,(float)main_window->width,(float)main_window->height,feet + vec3(0.0f,tall,0.0f),b)){
                continue;
            }
            vec2 ab = b - a;
            float len2 = ab.dot(ab);
            float t = (len2 > 1e-6f) ? std::max(0.0f,std::min(1.0f,(at - a).dot(ab) / len2)) : 0.0f;
            float d = (a + ab * t - at).length();
            if (d >= best || (feet + vec3(0.0f,tall * t,0.0f) - eye).length() > surface + PERSON_HIDDEN_SLACK){
                continue;
            }
            best = d;
            out.kind = PLAY_PICK_PERSON;
            out.id = k.id;
        }
        if (out.kind != PLAY_PICK_NONE){
            return out;
        }
        //...but none under the clouds: its card would tell what lies there, and PICK UP fetch it (the user's, 2026-10-18).
        std::shared_ptr<const Exploration> ex = GetExploration();
        best = CRATE_PICK_PX * s;
        for (const EconomyCrate& c : e->crates){
            if (ex && !ex->Explored(c.pos)){
                continue;
            }
            vec2 sp;
            if (ScreenOf(main_scene->camera,(float)main_window->width,(float)main_window->height,CrateMiddle(*w,c),sp)){
                float d = (sp - at).length();
                if (d < best){
                    best = d;
                    out.kind = PLAY_PICK_CRATE;
                    out.id = c.id;
                    out.plot = c.plot;
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
        }else if (z->gate[hover.plot]){
            //A wall's gate, for its door (defence_plan.md): the road plot under its crossbeam.
            out.kind = PLAY_PICK_GATE;
            out.id = (uint32_t)hover.plot;
            out.plot = hover.plot;
        }else{
            //A garden or lot being built (yards_plan.md): its patch, by its lowest plot.
            std::vector<int> patch;
            GroundPatch(*z,hover.plot,patch);
            if (!patch.empty()){
                out.kind = PLAY_PICK_GROUND;
                out.id = (uint32_t)patch[0];
                out.plot = hover.plot;
            }
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
    MCP THREAD. The renderer reads what is under the mouse back from its G-buffer, which a minimised window's
    tools cannot move: pinned at `px` instead (InputController::PinPickPixel), and an answer read there waited
    for - past the reads already in flight, three (Renderer's PICK_PBO_SLOTS), so none of them is taken for it.
*/
bool ApplicationChasm::WaitForPickAt(int2 px){
    InputController* input = main_scene->inputcontroller;
    input->PinPickPixel(px);
    uint32_t start = input->GetPickSerial();
    for (int i = 0; i < 200; i++){
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        int2 at = input->GetHoveredPixel();
        if (input->GetPickSerial() - start > 3 && at.x == px.x && at.y == px.y){
            return true;
        }
    }
    return false;
}

void ApplicationChasm::RestorePickPin(){
    InputController* input = main_scene->inputcontroller;
    if (f_overlay_mouse_pinned){
        input->PinPickPixel(int2(overlay_mouse_x.load(),overlay_mouse_y.load()));
    }else{
        input->UnpinPickPixel();
    }
}

/*
    THE SELECTION AS PEOPLE (selection_plan.md, "Several people selected"): one is a person selected, as ever - a
    child too; two or more a GROUP, of grown-ups only, since only they take orders and a group is for ordering.
*/
void ApplicationChasm::SelectPeople(std::vector<uint32_t> ids){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::vector<uint32_t> keep;
    for (uint32_t id : ids){
        const EconomyWorker* k = e ? PersonById(*e,id) : NULL;
        if (k && std::find(keep.begin(),keep.end(),id) == keep.end() && (ids.size() == 1 || EconomyCanOrder(*k,e->tick))){
            keep.push_back(id);
        }
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    play_selected = PlayPick();
    play_group.clear();
    play_people_before.clear();
    if (keep.size() >= 2){
        play_selected.kind = PLAY_PICK_GROUP;
        play_group = keep;
    }else if (keep.size() == 1){
        play_selected.kind = PLAY_PICK_PERSON;
        play_selected.id = keep[0];
    }
    play_pick_version++;
}

void ApplicationChasm::SelectLocked(const PlayPick& p){
    if (play_selected.kind == PLAY_PICK_GROUP){
        play_people_before = play_group;
    }else if (play_selected.kind == PLAY_PICK_PERSON){
        play_people_before = {play_selected.id};
    }
    if (p.kind == PLAY_PICK_NONE){
        play_people_before.clear();
    }
    play_selected = p;
    play_group.clear();
    play_pick_version++;
}

void ApplicationChasm::ToggleSelected(uint32_t person, bool f_card_name){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    const EconomyWorker* k = e ? PersonById(*e,person) : NULL;
    if (!k || !EconomyCanOrder(*k,e->tick)){
        return;
    }
    std::vector<uint32_t> ids = SelectedPeople();
    if (ids.empty() && f_card_name){
        std::lock_guard<std::mutex> lock(pick_mutex);
        ids = play_people_before;
    }
    auto at = std::find(ids.begin(),ids.end(),person);
    if (at != ids.end()){
        ids.erase(at);
    }else{
        ids.push_back(person);
        Say(person,"picked",CHASM_SAY_PICKED);     //added, he answers; taken out, he says nothing
    }
    SelectPeople(ids);
}

std::vector<uint32_t> ApplicationChasm::SelectedPeople(){
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (play_selected.kind == PLAY_PICK_GROUP){
        return play_group;
    }
    return (play_selected.kind == PLAY_PICK_PERSON) ? std::vector<uint32_t>{play_selected.id} : std::vector<uint32_t>();
}

void ApplicationChasm::PruneGroup(){
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        if (play_selected.kind != PLAY_PICK_GROUP){
            return;
        }
        ids = play_group;
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!e){
        return;
    }
    size_t was = ids.size();
    ids.erase(std::remove_if(ids.begin(),ids.end(),[&](uint32_t id){ return !PersonById(*e,id); }),ids.end());
    if (ids.size() != was){
        SelectPeople(ids);
    }
}

//THE BOX's people: grown-ups out of doors whose figure's middle lands inside the box on screen, behind a roof or not.
std::vector<uint32_t> ApplicationChasm::PeopleInBox(vec2 a, vec2 b){
    std::vector<uint32_t> out;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !e || e->world != w){
        return out;
    }
    vec2 lo(std::min(a.x,b.x),std::min(a.y,b.y));
    vec2 hi(std::max(a.x,b.x),std::max(a.y,b.y));
    for (const EconomyWorker& k : e->workers){
        if (EconomyIndoors(k) || !EconomyCanOrder(k,e->tick)){
            continue;
        }
        vec3 middle = FeetOf(*w,z.get(),k) + vec3(0.0f,worker_figure_height[PersonFigure(k,e->tick)] * 0.5f,0.0f);
        vec2 sp;
        if (ScreenOf(main_scene->camera,(float)main_window->width,(float)main_window->height,middle,sp) &&
            sp.x >= lo.x && sp.x <= hi.x && sp.y >= lo.y && sp.y <= hi.y){
            out.push_back(k.id);
        }
    }
    return out;
}

void ApplicationChasm::TakeBox(const std::vector<uint32_t>& ids, bool f_add){
    if (ids.empty()){
        return;
    }
    std::vector<uint32_t> people = f_add ? SelectedPeople() : std::vector<uint32_t>();
    for (uint32_t id : ids){
        if (std::find(people.begin(),people.end(),id) == people.end()){
            people.push_back(id);
        }
    }
    SelectPeople(people);
    SayPicked(ids);         //one of the box answers for it (ApplicationChasmSound.cpp)
}

/*
    PHYSICS THREAD: the selected sent to `plot` as a right click sends them (defence_plan.md) - onto tower `tower` as its
    guards, if not 0 - or, with `forage`, to pick or draw there by hand (foraging_plan.md). A GROUP (selection_plan.md,
    "Several people selected", step 3): a command each, nearest first, each spread round the plot - or to the next
    nearest ripe plant - and told as one line once all are done (group_orders, DrawBuildBar).
*/
void ApplicationChasm::SendSelected(int plot, int layer, uint32_t tower, int forage){
    std::vector<uint32_t> people = SelectedPeople();
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    auto send = [&](uint32_t id, bool f_spread){
        if (tower){
            SubmitGuard(id,tower);
        }else if (forage != FORAGE_NONE){
            SubmitForage(id,plot,forage,f_spread);
        }else{
            SubmitOrder(id,plot,layer,f_spread);
        }
    };
    if (people.size() >= 2 && w && z && e && e->world == w && z->world == w){
        group_orders_refused_at = order_refusals.load();
        group_orders_done_at = order_commands_done.load() + (uint32_t)people.size();
        group_orders_since = std::chrono::steady_clock::now().time_since_epoch().count();
        group_orders = (uint32_t)people.size();
        //The nearest first: he takes the plot clicked, or the tower's place, and the farther the rest round it.
        int at = tower ? FirstPlotOf(*z,tower) : plot;
        vec2 p = (at >= 0) ? w->grid->fine.pos[at] : vec2();
        std::vector<const EconomyWorker*> by;
        for (uint32_t id : people){
            if (const EconomyWorker* k = PersonById(*e,id)){
                by.push_back(k);
            }
        }
        std::stable_sort(by.begin(),by.end(),[&](const EconomyWorker* a, const EconomyWorker* b){
            return (a->pos - p).length() < (b->pos - p).length();
        });
        group_orders_done_at = group_orders_done_at.load() - (uint32_t)(people.size() - by.size());
        for (const EconomyWorker* k : by){
            send(k->id,true);
        }
    }else{
        for (uint32_t id : people){
            send(id,false);
        }
    }
}

/*
    RENDER THREAD. THE RIGHT CLICK'S MENU (foraging_plan.md): beside the cursor where it opened - Pick fruit over a ripe
    plant, with Post here; Draw water over water, where nobody may stand. Its panel is the menu's too, so a click on it
    between the choices neither closes it nor picks anything beneath.
*/
void ApplicationChasm::DrawForageMenu(float s){
    ForageMenu m;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        m = forage_menu;
    }
    if (!m.f_open || paint_tool != CHASM_TOOL_SELECT || SelectedPeople().empty()){
        return;
    }
    struct Item{ const char* name; int value; };
    std::vector<Item> items;
    if (m.f_fruit){
        items.push_back(Item{"Pick fruit",FORAGE_FRUIT});
    }
    if (m.f_water){
        items.push_back(Item{"Draw water",FORAGE_WATER});
    }else{
        items.push_back(Item{"Post here",FORAGE_NONE});
    }
    const float text_size = 14.0f * s;
    const float pad = 7.0f * s;
    const float row_h = 26.0f * s;
    float text_w = 0.0f;
    for (const Item& it : items){
        text_w = std::max(text_w,overlay->MeasureText(it.name,text_size).x);
    }
    float w = text_w + pad * 2.0f + 16.0f * s;
    float h = pad * 2.0f + row_h * (float)items.size();
    vec2 p0(m.at.x + 12.0f * s,m.at.y - h * 0.5f);
    p0.x = std::min(p0.x,(float)main_window->width - w - 8.0f * s);
    p0.y = std::max(8.0f * s,std::min(p0.y,(float)main_window->height - h - 8.0f * s));
    vec2 p1 = p0 + vec2(w,h);
    //All but opaque: it opens over whatever is there - the selected person's card, as often as not.
    overlay->AddRect(p0,p1,9.0f * s,HudFaded(HUD_PANEL,248));
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_FORAGE,-1});
    float y = p0.y + pad;
    for (size_t i = 0; i < items.size(); i++){
        vec2 k0(p0.x + pad,y + 2.0f * s);
        vec2 k1(p1.x - pad,y + row_h - 2.0f * s);
        //The first is what the click was for - the fruit, the water: lit, as a chip that is on.
        bool f_lit = i == 0 && items[i].value != FORAGE_NONE;
        if (f_lit){
            uint32_t fill = (items[i].value == FORAGE_FRUIT) ? HudGoodColour(GOOD_FRUIT) : HudGoodColour(GOOD_WATER);
            overlay->AddRect(k0,k1,8.0f * s,HudFaded(fill,235));
        }else{
            overlay->AddRectOutline(k0,k1,8.0f * s,1.5f * s,HudFaded(HUD_TEXT,118));
        }
        overlay->AddText(items[i].name,vec2(k0.x + 8.0f * s,k1.y - 7.0f * s),text_size,
                         f_lit ? HudFaded(HUD_PANEL,255) : HUD_TEXT,UI_ALIGN_LEFT);
        overlay_hits_drawing.push_back(OverlayHit{k0,k1,OVERLAY_HIT_FORAGE,items[i].value});
        y += row_h;
    }
}

/*
    PHYSICS THREAD, from UpdatePick in play mode, before any painting: the overlay takes the mouse
    where it is drawn (a press on it does what it says and nothing beneath is picked or painted); a
    right click - pressed and let go without dragging, which would be a pan - puts a tool down, sends
    a selected person to the plot under it (an ORDER, defence_plan.md), or with neither clears the
    selection; and with no tool the hover and a click's selection.
*/
void ApplicationChasm::UpdatePlayPick(GridPick& hover, int2 px, bool& f_over_scene, bool& f_clicked){
    InputController* input = main_scene->inputcontroller;
    PruneGroup();
    //A scripted click (chasm_select op press): this pass, at its pixel, as the mouse's would be - the
    //right button's too, and Shift.
    bool f_script_right = false;
    bool f_shift = input->IsKeyDown(INPUT_SHIFT);
    bool f_scripted = false;        //a script's press: pressed and let go at once, never a box
    if (f_script_press.exchange(false)){
        f_scripted = true;
        {
            std::lock_guard<std::mutex> lock(pick_mutex);
            px = script_press_px;
        }
        f_script_right = f_script_press_right.exchange(false);
        f_shift = f_script_press_shift.exchange(false);
        f_over_scene = px.x >= 0 && px.y >= 0 && px.x < main_window->width && px.y < main_window->height;
        f_clicked = f_over_scene && !f_script_right;
        if (f_over_scene){
            //The engine's surface there when the tool had it read there (WaitForPickAt) - a deck's layer, for an order.
            int2 read_at = input->GetHoveredPixel();
            hover = (read_at.x == px.x && read_at.y == px.y) ? PickUnderMouse(px) : PickUnderPixel(px);
        }
    }
    OverlayHit hit;
    uint32_t row_person = 0;        //a line or row naming a person, under the cursor: he is the hover
    //THE RIGHT CLICK'S MENU (foraging_plan.md): a click anywhere but on it closes it.
    if (f_clicked){
        OverlayHit on;
        if (!(OverlayHitAt(px,&on) && on.action == OVERLAY_HIT_FORAGE)){
            std::lock_guard<std::mutex> lock(pick_mutex);
            forage_menu.f_open = false;
        }
    }
    if (f_over_scene && OverlayHitAt(px,&hit)){
        f_over_scene = false;
        hover = GridPick();
        if (hit.action == OVERLAY_HIT_PERSON || hit.action == OVERLAY_HIT_FAMILY){
            row_person = (uint32_t)hit.value;
        }
        if (f_clicked){
            //Every press on the HUD clicks, whatever it does (ApplicationChasmSound.cpp).
            SignalUI("click");
            if (hit.action == OVERLAY_HIT_TOOL){
                paint_tool = (paint_tool == hit.value) ? CHASM_TOOL_SELECT : hit.value;
                pick_version++;
            }else if (hit.action == OVERLAY_HIT_PAUSE || hit.action == OVERLAY_HIT_SPEED){
                PressTimeButton(hit.action);    //the calendar's buttons (ApplicationChasmTime.cpp)
            }else if (hit.action == OVERLAY_HIT_STATS){
                OpenStats();                    //the top bar's figures: the Statistics page (ApplicationChasmStats.cpp)
            }else if (hit.action == OVERLAY_HIT_REPLAY_STOP){
                RequestReplayStop();            //the replay's bar (ApplicationChasmRecord.cpp)
            }else if (hit.action == OVERLAY_HIT_CLOSE){
                SelectPeople({});
            }else if (hit.action == OVERLAY_HIT_TAKE_TOOL){
                //A hint in NEEDS ATTENTION (issues_plan.md): its tool in hand - a second press keeps it there.
                paint_tool = hit.value;
                pick_version++;
            }else if (hit.action == OVERLAY_HIT_BUILDING){
                //A place in NEEDS ATTENTION: selected, and the view taken to it, as the neediest list does a person.
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && (uint32_t)hit.value < z->buildings.size() && z->buildings[hit.value].size > 0){
                    {
                        std::lock_guard<std::mutex> lock(pick_mutex);
                        PlayPick p;
                        p.kind = PLAY_PICK_BUILDING;
                        p.id = (uint32_t)hit.value;
                        p.plot = FirstPlotOf(*z,p.id);
                        SelectLocked(p);
                    }
                    cam_seek_building = (uint32_t)hit.value;
                }
            }else if (hit.action == OVERLAY_HIT_PLACE){
                cam_seek_plot = hit.value;      //...and one on the ground - a road's - the view taken there only
            }else if ((hit.action == OVERLAY_HIT_PERSON || hit.action == OVERLAY_HIT_FAMILY) && f_shift){
                //Shift on a name - the neediest's, a card's (selection_plan.md, "Several people selected"): added to the
                //selection or taken out, at home too, and the view stays.
                ToggleSelected((uint32_t)hit.value,hit.action == OVERLAY_HIT_FAMILY);
            }else if (hit.action == OVERLAY_HIT_PERSON){
                //The neediest list (DrawNeediest): him, selected, and the view taken to him (the camera's glide).
                SelectPeople({(uint32_t)hit.value});
                Say((uint32_t)hit.value,"picked",CHASM_SAY_PICKED);
                cam_seek_person = (uint32_t)hit.value;
            }else if (hit.action == OVERLAY_HIT_FAMILY){
                //A house's family line, or the camp's (selection_plan.md), or a group's row: him alone, at home or out.
                //The view stays - he is in the house, or his pin shows where he went.
                SelectPeople({(uint32_t)hit.value});
                Say((uint32_t)hit.value,"picked",CHASM_SAY_PICKED);
            }else if (hit.action == OVERLAY_HIT_GROUP_DROP){
                //A group's row's x: he leaves the group - the last but one leaves one person selected.
                std::vector<uint32_t> ids = SelectedPeople();
                ids.erase(std::remove(ids.begin(),ids.end(),(uint32_t)hit.value),ids.end());
                SelectPeople(ids);
            }else if (hit.action == OVERLAY_HIT_GATE){
                //A gate's card: its door shut (1) or opened (0) - a recorded zone command (defence_plan.md).
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                if (sel.kind == PLAY_PICK_GATE && sel.plot >= 0){
                    SubmitZone(ZONE_OP_GATE_SHUT,sel.plot,hit.value);
                }
            }else if (hit.action == OVERLAY_HIT_LADDER){
                //A guard tower's card: its ladder round to the next open side - a recorded zone command.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()
                    && z->buildings[sel.id].kind == ZONE_KIND_TOWER){
                    SubmitZone(ZONE_OP_TOWER_LADDER,FirstPlotOf(*z,sel.id),-1);
                }
            }else if (hit.action == OVERLAY_HIT_TOWER_GUARDS){
                //A guard tower's card: how many guards it takes (step 8) - a recorded economy command.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                if (sel.kind == PLAY_PICK_BUILDING){
                    SubmitGuards(sel.id,hit.value);
                }
            }else if (hit.action == OVERLAY_HIT_PICK_UP){
                //A crate's card: fetched first by anyone free, or - lit - back in the queue (stone_plan.md).
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const EconomyState> e = GetEconomy();
                if (e && sel.kind == PLAY_PICK_CRATE){
                    for (const EconomyCrate& c : e->crates){
                        if (c.id == sel.id){
                            SubmitPickUp(c.id,!c.f_pick_up);
                        }
                    }
                }
            }else if (hit.action == OVERLAY_HIT_GATHER){
                /*
                    A posted person's card, or a group's: gather a scout's pack first, or - lit - go straight on
                    (defence_plan.md). For a group, everyone posted among them: lit while all of them are at it, so a
                    press with some gathering sets the rest to it, and one with all of them stops them all.
                */
                std::shared_ptr<const EconomyState> e = GetEconomy();
                if (e){
                    std::vector<const EconomyWorker*> posted;
                    bool f_all = true;
                    for (uint32_t id : SelectedPeople()){
                        const EconomyWorker* k = PersonById(*e,id);
                        if (k && k->post >= 0){
                            posted.push_back(k);
                            f_all = f_all && k->gather > 0;
                        }
                    }
                    for (const EconomyWorker* k : posted){
                        if ((k->gather > 0) == f_all){
                            SubmitGather(k->id,!f_all);
                        }
                    }
                }
            }else if (hit.action == OVERLAY_HIT_TENT){
                //A posted person's card, or a group's: take a tent first, or not (settlements_plan.md) - as gathering,
                //for everyone posted without one.
                std::shared_ptr<const EconomyState> e = GetEconomy();
                if (e){
                    std::vector<const EconomyWorker*> posted;
                    bool f_all = true;
                    for (uint32_t id : SelectedPeople()){
                        const EconomyWorker* k = PersonById(*e,id);
                        if (k && k->post >= 0 && k->tent != ECONOMY_TENT_CARRIED){
                            posted.push_back(k);
                            f_all = f_all && k->tent == ECONOMY_TENT_TO_TAKE;
                        }
                    }
                    for (const EconomyWorker* k : posted){
                        if ((k->tent == ECONOMY_TENT_TO_TAKE) == f_all){
                            SubmitTent(k->id,!f_all);
                        }
                    }
                }
            }else if (hit.action == OVERLAY_HIT_FORAGE && hit.value >= 0){
                //The right click's menu (foraging_plan.md): its choice for the plot it opened on, and it closes.
                ForageMenu m;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    m = forage_menu;
                    forage_menu.f_open = false;
                }
                if (m.f_open){
                    SendSelected(m.plot,m.layer,0,hit.value);
                }
            }else if (hit.action == OVERLAY_HIT_SETTLE){
                //Everyone selected who may moves into the settlement they were sent to, each to a bed to spare there.
                std::shared_ptr<const EconomyState> e = GetEconomy();
                if (e){
                    for (uint32_t id : SelectedPeople()){
                        const EconomyWorker* k = PersonById(*e,id);
                        if (k && CanSettleHere(*e,*k)){
                            SubmitSettle(k->id);
                        }
                    }
                }
            }else if (hit.action == OVERLAY_HIT_PITCH){
                //Everyone selected carrying a tent pitches it where he stands, in the order selected: each beside the
                //last, one camp (Economy::PitchSpot).
                std::shared_ptr<const EconomyState> e = GetEconomy();
                if (e){
                    for (uint32_t id : SelectedPeople()){
                        const EconomyWorker* k = PersonById(*e,id);
                        if (k && k->tent == ECONOMY_TENT_CARRIED){
                            SubmitPitch(k->id);
                        }
                    }
                }
            }else if (hit.action == OVERLAY_HIT_KEEP || hit.action == OVERLAY_HIT_MAKE_MORE){
                //A builders' hut's or ropewalk's card: what the colony keeps of what it makes, or an order for more
                //(goods_plan.md) - a recorded economy command.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                int good = (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size())
                           ? EconomyMadeAt(z->buildings[sel.id].kind) : -1;
                if (good >= 0){
                    SubmitKeep(good,hit.value,hit.action == OVERLAY_HIT_MAKE_MORE);
                }
            }else if (hit.action == OVERLAY_HIT_GUARD_RELEASE){
                //A guard released from his tower: the tower card's chip names him, his own card's is him (step 8).
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                uint32_t person = hit.value ? (uint32_t)hit.value : (sel.kind == PLAY_PICK_PERSON) ? sel.id : 0;
                if (person){
                    SubmitGuard(person,0);
                }
            }else if (hit.action == OVERLAY_HIT_RELEASE){
                //A posted person's card, or a group's: back to their work (defence_plan.md) - a recorded order each.
                std::shared_ptr<const EconomyState> e = GetEconomy();
                for (uint32_t id : SelectedPeople()){
                    const EconomyWorker* k = e ? PersonById(*e,id) : NULL;
                    if (k && k->post >= 0){
                        SubmitOrder(id,-1);
                    }
                }
            }else if (hit.action == OVERLAY_HIT_WORK_STOP){
                //A workplace's work stopped or set going again (stopping_work_plan.md) - a recorded zone command, by a
                //plot of it, or a range's by a cell.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()){
                    int want = z->buildings[sel.id].f_stopped ? 0 : 1;
                    if (ZoneKindOnCells(z->buildings[sel.id].kind)){
                        auto cell = std::find(z->field.begin(),z->field.end(),sel.id);
                        if (cell != z->field.end()){
                            SubmitZone(ZONE_OP_RANGE_STOP,(int)(cell - z->field.begin()),want);
                        }
                    }else{
                        int plot = (sel.plot >= 0 && sel.plot < (int)z->building.size() && z->building[sel.plot] == sel.id)
                                   ? sel.plot : FirstPlotOf(*z,sel.id);
                        SubmitZone(ZONE_OP_WORK_STOP,plot,want);
                    }
                }
            }else if (hit.action == OVERLAY_HIT_SITE_CANCEL){
                //A site taken off whole (construction_plan.md) - a recorded zone command, by a plot of it or a cell -
                //and with it gone, nothing selected.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()){
                    auto cell = std::find(z->field.begin(),z->field.end(),sel.id);
                    if (ZoneKindOnCells(z->buildings[sel.id].kind) && cell != z->field.end()){
                        SubmitZone(ZONE_OP_FIELD_CANCEL,(int)(cell - z->field.begin()),0);
                    }else{
                        SubmitZone(ZONE_OP_SITE_CANCEL,FirstPlotOf(*z,sel.id),0);
                    }
                    SelectPeople({});
                }
            }else if (hit.action == OVERLAY_HIT_STORE_ALLOW || hit.action == OVERLAY_HIT_STORE_ALLOW_GROUP
                      || hit.action == OVERLAY_HIT_SITE_FIRST){
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()){
                    int plot = (sel.plot >= 0 && sel.plot < (int)z->building.size() && z->building[sel.plot] == sel.id)
                               ? sel.plot : FirstPlotOf(*z,sel.id);
                    int want = z->buildings[sel.id].f_priority ? 0 : 1;
                    auto cell = std::find(z->field.begin(),z->field.end(),sel.id);
                    if (hit.action == OVERLAY_HIT_STORE_ALLOW){
                        GoodsMask mask = z->buildings[sel.id].allow ^ (GoodsMask)GOOD_BIT(hit.value);
                        SubmitZone(ZONE_OP_STORE_ALLOW,plot,mask);
                    }else if (hit.action == OVERLAY_HIT_STORE_ALLOW_GROUP){
                        //A group's chip (goods_plan.md): all of the group, or - taking all of it already - none.
                        GoodsMask group = HudGoodsGroupMask(hit.value);
                        GoodsMask allow = z->buildings[sel.id].allow;
                        SubmitZone(ZONE_OP_STORE_ALLOW,plot,((allow & group) == group) ? (GoodsMask)(allow & ~group)
                                                                                        : (GoodsMask)(allow | group));
                    }else if (ZoneKindOnCells(z->buildings[sel.id].kind) && cell != z->field.end()){
                        SubmitZone(ZONE_OP_FIELD_PRIORITY,(int)(cell - z->field.begin()),want);     //a field or range: by a cell
                    }else{
                        SubmitZone(ZONE_OP_SITE_PRIORITY,plot,want);
                    }
                }
            }else if (hit.action == OVERLAY_HIT_FIELD_CROP || hit.action == OVERLAY_HIT_FIELD_NEXT
                      || hit.action == OVERLAY_HIT_FIELD_LAND){
                //A field's crops (fields_plan.md) and whose land it is (land_plan.md) - recorded zone commands
                //on a cell of it.
                PlayPick sel;
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    sel = play_selected;
                }
                std::shared_ptr<const ZoneState> z = GetZones();
                if (z && sel.kind == PLAY_PICK_BUILDING && sel.id < z->buildings.size()){
                    auto cell = std::find(z->field.begin(),z->field.end(),sel.id);
                    if (cell != z->field.end()){
                        int op = (hit.action == OVERLAY_HIT_FIELD_CROP) ? ZONE_OP_FIELD_CROP
                               : (hit.action == OVERLAY_HIT_FIELD_NEXT) ? ZONE_OP_FIELD_NEXT : ZONE_OP_FIELD_LAND;
                        SubmitZone(op,(int)(cell - z->field.begin()),hit.value);
                    }
                }
            }
        }
        f_clicked = false;
    }
    //The right button: a click, not a drag - or a script's, at its pixel.
    bool f_right_click = f_script_right && f_over_scene;
    if (input->WasKeyPressed(INPUT_CLICK_RIGHT) && f_over_scene){
        right_press_px = px;
        f_right_press = true;
    }
    if (input->WasKeyReleased(INPUT_CLICK_RIGHT) && f_right_press){
        f_right_press = false;
        f_right_click = std::abs(px.x - right_press_px.x) <= RIGHT_CLICK_SLOP && std::abs(px.y - right_press_px.y) <= RIGHT_CLICK_SLOP;
    }
    if (f_right_click){
        std::vector<uint32_t> people = SelectedPeople();
        {
            std::lock_guard<std::mutex> lock(pick_mutex);
            forage_menu.f_open = false;     //a right click anywhere closes it - or opens it afresh, below
        }
        if (paint_tool != CHASM_TOOL_SELECT){
            paint_tool = CHASM_TOOL_SELECT;
            pick_version++;
        }else if (!people.empty() && hover.f_hit && hover.plot >= 0){
            /*
                An ORDER: the selected person goes to the plot under the cursor and stands there. The economy
                decides whether he can, and the overlay says why not (CHASM_CMD_ORDER). On a GUARD TOWER - the one
                the cursor picks, or on the plot - he is posted there as its guard by hand instead (step 8).
                On a RIPE PLANT or on WATER (foraging_plan.md, the user's 2026-10-12), a menu instead - pick it, or draw
                water there, by hand, or only stand there - and its choice sends them (DrawForageMenu, SendSelected).
            */
            PlayPick on = PlayPickUnder(px,hover);
            std::shared_ptr<const ZoneState> z = GetZones();
            uint32_t tower = 0;
            if (z && on.kind == PLAY_PICK_BUILDING && on.id < z->buildings.size() && z->buildings[on.id].kind == ZONE_KIND_TOWER){
                tower = on.id;
            }else if (z && hover.plot < (int)z->building.size() && z->KindOf(hover.plot) == ZONE_KIND_TOWER){
                tower = z->building[hover.plot];
            }
            //On the layer the mouse is on - the wall-walk under an arch, the road under it (ramparts_plan.md); picked by
            //the ray alone, there is none to tell: its top.
            int layer = hover.f_surface ? hover.layer : -1;
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            std::shared_ptr<const EconomyState> e = GetEconomy();
            bool f_fruit = false;
            bool f_water = false;
            //Under the clouds no menu (the user's, 2026-10-18): what it offers would say what lies there. He is only sent.
            std::shared_ptr<const Exploration> ex = GetExploration();
            bool f_seen = !ex || (w && hover.plot < (int)w->grid->fine.pos.size() && ex->Explored(w->grid->fine.pos[hover.plot]));
            if (f_seen && !tower && w && z && e && e->world == w && z->world == w && hover.plot < (int)w->terrain->wet.size()){
                f_fruit = EconomyRipePlantOn(*e,hover.plot) >= 0;
                f_water = w->terrain->wet[hover.plot] && !ZoneBridgeWalkable(*z,hover.plot);
            }
            if (f_fruit || f_water){
                std::lock_guard<std::mutex> lock(pick_mutex);
                forage_menu.f_open = true;
                forage_menu.at = vec2((float)px.x,(float)px.y);
                forage_menu.plot = hover.plot;
                forage_menu.layer = layer;
                forage_menu.f_fruit = f_fruit;
                forage_menu.f_water = f_water;
            }else{
                SendSelected(hover.plot,layer,tower,FORAGE_NONE);
            }
        }else{
            bool f_any;
            {
                std::lock_guard<std::mutex> lock(pick_mutex);
                f_any = play_selected.kind != PLAY_PICK_NONE;
            }
            if (f_any){
                SelectPeople({});
            }
        }
    }
    PlayPick under;
    if (paint_tool == CHASM_TOOL_SELECT && f_over_scene){
        under = PlayPickUnder(px,hover);
    }else if (row_person){
        under.kind = PLAY_PICK_PERSON;
        under.id = row_person;
    }
    /*
        THE PRESS (selection_plan.md, "Several people selected", step 2): with no tool in hand, a CLICK once let go
        within RIGHT_CLICK_SLOP of where it began, or else a BOX - so a press that becomes a box has selected and
        cleared nothing on the way. A script's press is let go at once.
    */
    bool f_select = paint_tool == CHASM_TOOL_SELECT;
    bool f_click = false;
    if (f_select && f_clicked && f_over_scene){
        if (f_scripted){
            f_click = true;
        }else{
            left_press_px = px;
            f_left_press = true;
            f_box_drag = false;
        }
    }
    if (f_left_press && !f_select){
        f_left_press = false;               //a tool taken up meanwhile: the press is the tool's
        f_box_drag = false;
    }else if (f_left_press){
        f_box_drag = f_box_drag || std::abs(px.x - left_press_px.x) > RIGHT_CLICK_SLOP
                                || std::abs(px.y - left_press_px.y) > RIGHT_CLICK_SLOP;
        if (!input->IsKeyDown(INPUT_CLICK_LEFT)){
            f_left_press = false;
            if (f_box_drag){
                TakeBox(PeopleInBox(vec2((float)left_press_px.x,(float)left_press_px.y),vec2((float)px.x,(float)px.y)),f_shift);
            }else{
                f_click = f_over_scene;
            }
            f_box_drag = false;
        }
    }
    std::vector<uint32_t> in_box;
    if (f_box_drag){
        in_box = PeopleInBox(vec2((float)left_press_px.x,(float)left_press_px.y),vec2((float)px.x,(float)px.y));
    }
    //Shift+click (selection_plan.md, "Several people selected"): a grown-up added to the selection or taken out of
    //it; on anything else, nothing - the selection stays as it is.
    bool f_shift_click = f_shift && f_click;
    if (f_shift_click && under.kind == PLAY_PICK_PERSON){
        ToggleSelected(under.id);
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    if (under != play_hover){
        play_hover = under;
        play_pick_version++;
    }
    //The box as drawn, and who it would take (UploadSelection outlines them as hovered) - unless a script holds one.
    if (!f_box_scripted){
        f_box_shown = f_box_drag;
        box_a = vec2((float)left_press_px.x,(float)left_press_px.y);
        box_b = vec2((float)px.x,(float)px.y);
        box_people.swap(in_box);
    }
    //A click on someone - picked, or picked again - and he answers (ApplicationChasmSound.cpp, Say).
    if (!f_shift_click && f_click && under.kind == PLAY_PICK_PERSON){
        Say(under.id,"picked",CHASM_SAY_PICKED);
    }
    //A click on nothing - bare ground - clears the selection, a group's too.
    if (!f_shift_click && f_click && under != play_selected){
        if (under.kind == PLAY_PICK_PERSON){
            play_selected = under;
            play_group.clear();
            play_people_before.clear();
            play_pick_version++;
        }else{
            SelectLocked(under);
        }
    }else if (!f_shift_click && f_click){
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
        world_scene->AtTickBoundary([&](){
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
                world_scene->AddObject(o);
                select_outline[i] = o;
            }
        });
    }
    PlayPick hover;
    PlayPick sel;
    std::vector<uint32_t> people;       //the people selected: a group, or the one
    std::vector<uint32_t> boxed;        //...and those a box being dragged would take, outlined as hovered
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        hover = play_hover;
        sel = play_selected;
        people = (sel.kind == PLAY_PICK_GROUP) ? play_group
               : (sel.kind == PLAY_PICK_PERSON) ? std::vector<uint32_t>{sel.id} : std::vector<uint32_t>();
        if (f_box_shown){
            boxed = box_people;
        }
    }
    bool f_play = PlayMode();
    bool f_ready = w && z && z->world == w;
    //What each slot outlines: the selected building, the hovered one; then what belongs with the selection.
    uint32_t want[SELECT_OUTLINES] = {0,0,0,0};
    vec4 colour[SELECT_OUTLINES] = {SELECT_COLOUR,HOVER_COLOUR,RELATED_COLOUR,RELATED_COLOUR};
    RelatedPlaces related_places;
    if (f_play && f_ready){
        //A garden or lot being built by its patch (BuildSelectionMesh).
        auto outlined = [](const PlayPick& p) -> uint32_t {
            return (p.kind == PLAY_PICK_BUILDING) ? p.id : (p.kind == PLAY_PICK_GROUND) ? (ECONOMY_SITE_GROUND | p.id) : 0;
        };
        want[0] = outlined(sel);
        if (hover != sel){
            want[1] = outlined(hover);
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
                //A selected person's post up on a deck (ramparts_plan.md) is marked there, not on the ground under it.
                const EconomyWorker* posted = (sel.kind == PLAY_PICK_PERSON && e && e->world == w) ? PersonById(*e,sel.id) : NULL;
                for (int v : related_places.plots){
                    float floor = -INFINITY;
                    if (posted && posted->post == v){
                        int ls[WALK_MOST_DECKS];
                        int n = WalkDeckLayers(*z,v,ls);
                        int layer = (posted->post_layer >= 0) ? posted->post_layer : (n > 0) ? ls[n - 1] : WALK_GROUND;
                        floor = (layer > 0) ? BuildingLayerHeight(*w,*z,v,layer) + FIELD_OUTLINE_LIFT : floor;
                    }
                    BuildPlotTile(*w,v,FIELD_OUTLINE_LIFT,verts,floor);
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
    //The people: the selected - one, or a group's - the one under the cursor, and those who belong with the selection.
    std::vector<uint32_t> related;
    if (f_play && f_ready && e && e->world == w){
        RelatedPeople(sel,*z,*e,related);
    }
    for (size_t i = 0; i < worker_objects.size(); i++){
        vec4 c(0.0f,0.0f,0.0f,0.0f);
        if (f_play && e && i < e->workers.size()){
            uint32_t id = e->workers[i].id;
            //The hover over the related: hovering one of a selected house's family, on the map or his line, picks him out.
            if (std::find(people.begin(),people.end(),id) != people.end()){
                c = SELECT_COLOUR;
            }else if ((hover.kind == PLAY_PICK_PERSON && hover.id == id) || std::find(boxed.begin(),boxed.end(),id) != boxed.end()){
                c = PERSON_HOVER_COLOUR;
            }else if (std::find(related.begin(),related.end(),id) != related.end()){
                c = RELATED_COLOUR;
            }
        }
        //On whichever figure he is drawn as: the static one, or a rigged figure's skeleton (the rest are hidden).
        SetOutlineOf(worker_objects[i],c);
        if (i < worker_skins.size()){
            for (WorkerSkin& ws : worker_skins[i]){
                if (ws.figure){
                    SetOutlineOf(ws.figure,c);
                }
            }
        }
    }
    /*
        A crate (crates_plan.md): an outline-only copy of chasm_props.glb's crate where the selected one lies, and
        another where the hovered one does - the crates themselves are one instance set, which outlines skip.
        Rubble (stone_plan.md) has its own pair, of its pile, at its scale, and so has a seam's crystal lying.
    */
    auto make_outlines = [&](Object* (&out)[2], const char* asset, const char* what){
        Mesh* mesh = (!out[0] && asset) ? assetmanager->GetMeshFromAsset(asset) : NULL;
        if (!mesh){
            return;
        }
        world_scene->AtTickBoundary([&](){
            for (int i = 0; i < 2; i++){
                Object* o = new Object();
                o->name = std::string((i == 0) ? "Selected " : "Hovered ") + what + " outline";
                mesh->Retain();     //the set's mesh too: held for good, or a SetMesh would free it under the set
                o->SetMesh(mesh);
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetCastsShadow(false);
                o->SetVisibility(false);
#if CHASM_OUTLINE_READY
                o->SetOutlineOnly(true);
#endif
                world_scene->AddObject(o);
                out[i] = o;
            }
        });
    };
    make_outlines(crate_outline,f_crate_loaded ? CHASM_CRATE_ASSET : NULL,"crate");
    make_outlines(rubble_outline,rubble_asset,"rubble");
    make_outlines(crystal_outline,crystal_asset,"floatstone lying");
    for (int i = 0; i < 2; i++){
        const PlayPick& p = (i == 0) ? sel : hover;
        const EconomyCrate* c = NULL;
        if (f_play && f_ready && e && e->world == w && p.kind == PLAY_PICK_CRATE && !(i == 1 && hover == sel)){
            for (const EconomyCrate& x : e->crates){
                c = (x.id == p.id) ? &x : c;
            }
        }
        //Drawn as it is drawn (UploadCrates): each look only while its heap is there.
        int look = c ? CrateLook(*c) : -1;
        Object* outlines[3] = {crate_outline[i],rubble_outline[i],crystal_outline[i]};
        for (int k = 0; k < 3; k++){
            if (outlines[k] && k != look){
                outlines[k]->SetVisibility(false);
            }
        }
        Object* o = (look >= 0) ? outlines[look] : NULL;
        if (!c || !o){
            continue;
        }
        float scale = (look == CRATE_LOOK_CRYSTAL) ? CHASM_CRYSTAL_SCALE : (look == CRATE_LOOK_RUBBLE) ? rubble_scale : 1.0f;
        o->SetPosition(vec3(c->pos.x,w->terrain->GroundHeight(c->pos,w->terrain->Height(c->plot)),c->pos.y),false);
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),CrateYaw(c->id)),false);
        o->SetScale(vec3(scale,scale,scale));
        SetOutlineOf(o,(i == 0) ? SELECT_COLOUR : HOVER_COLOUR);
        o->SetVisibility(CHASM_OUTLINE_READY != 0);
    }
    //Where the selected person is going (BuildPathMarks), out of doors and on his way - or a group's, each his own way,
    //all in the one mesh.
    if (!path_marks){
        world_scene->AtTickBoundary([&](){
            Object* o = new Object();
            o->name = "Selected person's way";
            o->SetMesh(new Mesh());
            o->SetMaterialSlot(0,palette_material);
            o->SetPickability(false);
            o->SetVisualOnly(true);
            o->SetCastsShadow(false);
            o->SetVisibility(false);
            world_scene->AddObject(o);
            path_marks = o;
        });
    }
    std::vector<const EconomyWorker*> walking;
    uint64_t key = 0;
    if (f_play && f_ready && e && e->world == w){
        for (uint32_t id : people){
            const EconomyWorker* k = PersonById(*e,id);
            if (k && !EconomyIndoors(*k) && k->seg + 1 < (int)k->route.size()){
                walking.push_back(k);
                key = key * 1099511628211ULL ^ PathMarksKey(*k);
            }
        }
    }
    if (walking.empty()){
        path_marks->SetVisibility(false);
        path_marks_key = 0;
    }else{
        key = key ? key : 1;
        if (key != path_marks_key){
            path_marks_key = key;
            verts.clear();
            std::vector<vertex> one;
            for (const EconomyWorker* k : walking){
                BuildPathMarks(*w,z.get(),*k,one);
                verts.insert(verts.end(),one.begin(),one.end());
            }
            if (!verts.empty()){
                path_marks->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            }
        }
        path_marks->SetVisibility(true);
    }
}

//--- The card and the pins (render thread) ---------------------------------------------------------

/*
    THE NEEDIEST (the user's, 2026-10-08): over the jobs in the bottom left corner, the NEEDIEST_SHOWN people
    with the least health - the dry first where it is even, as they lose it fastest - each with his badge,
    his name, what he is short of and how much health he has left, so whoever is in trouble can be found
    before it is too late. A click on one selects him and takes the view to him (OVERLAY_HIT_PERSON).
*/
std::vector<const EconomyWorker*> ChasmNeediest(const EconomyState& e, int count){
    std::vector<const EconomyWorker*> order;
    for (const EconomyWorker& k : e.workers){
        order.push_back(&k);
    }
    int shown = std::max(0,std::min(count,(int)order.size()));
    std::partial_sort(order.begin(),order.begin() + shown,order.end(),[](const EconomyWorker* a, const EconomyWorker* b){
        if (a->health != b->health){
            return a->health < b->health;
        }
        if (EconomyDry(*a) != EconomyDry(*b)){
            return EconomyDry(*a);
        }
        return a->id < b->id;
    });
    order.resize(shown);
    return order;
}

const char* ChasmNeedName(const EconomyWorker& k){
    return EconomyDry(k) ? "DRY" : (k.water < THIRST_MARK_WATER) ? "THIRSTY" : (k.health < NEEDS_PRESSING_HEALTH) ? "HUNGRY" : "";
}

void ApplicationChasm::DrawNeediest(float s, float bottom){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world || e->workers.empty()){
        return;
    }
    std::vector<const EconomyWorker*> order = ChasmNeediest(*e,NEEDIEST_SHOWN);
    int shown = (int)order.size();
    PlayPick hovered;
    std::vector<uint32_t> selected;     //the one person selected, or a group
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        hovered = play_hover;
        selected = (play_selected.kind == PLAY_PICK_GROUP) ? play_group
                 : (play_selected.kind == PLAY_PICK_PERSON) ? std::vector<uint32_t>{play_selected.id} : std::vector<uint32_t>();
    }
    //Each row's words: what he is short of, as his badge says it - dry, then thirsty, then hungry - and his
    //health in the badge ring's colour.
    struct Row{
        const EconomyWorker* k;
        std::string name;
        const char* need;
        uint32_t need_colour;
        char health[8];
        uint32_t colour;
    };
    std::vector<Row> rows;
    for (int i = 0; i < shown; i++){
        const EconomyWorker& k = *order[i];
        Row r;
        r.k = &k;
        r.name = EconomyPersonName(k);
        r.need = ChasmNeedName(k);
        r.need_colour = EconomyDry(k) ? HUD_DANGER : (k.water < THIRST_MARK_WATER) ? HUD_WATER : HUD_AMBER;
        snprintf(r.health,sizeof(r.health),"%i%%",(int)std::lround(100.0f * std::max(0,k.health) / NEEDS_HEALTH_FULL));
        r.colour = EconomyDry(k) ? HUD_DANGER : (k.health <= BADGE_LOW_HEALTH) ? HUD_AMBER : HUD_TEXT;
        rows.push_back(r);
    }
    const float size = 14.0f * s;
    const float title_size = 12.0f * s;
    const float title_h = 18.0f * s;
    const float line_h = 26.0f * s;
    const float pad = 12.0f * s;
    const float badge = BADGE_RADIUS * s;
    const float col_gap = 14.0f * s;
    float name_w = 0.0f;
    for (const Row& r : rows){
        name_w = std::max(name_w,overlay->MeasureText(r.name.c_str(),size).x);
    }
    float need_w = overlay->MeasureText("THIRSTY",size).x;
    float health_w = overlay->MeasureText("100%",size).x;
    float w = pad * 2.0f + badge * 2.0f + 8.0f * s + name_w + col_gap + need_w + col_gap + health_w;
    float h = pad * 0.8f + title_h + line_h * (float)rows.size() + pad * 0.4f;
    vec2 p0(16.0f * s,bottom - h);
    vec2 p1(p0.x + w,bottom);
    overlay->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    overlay->AddText("MOST IN NEED",vec2(p0.x + pad,p0.y + pad * 0.8f + 11.0f * s),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    float name_x = p0.x + pad + badge * 2.0f + 8.0f * s;
    float need_x = name_x + name_w + col_gap;
    for (size_t i = 0; i < rows.size(); i++){
        const Row& r = rows[i];
        float top = p0.y + pad * 0.8f + title_h + line_h * (float)i;
        float mid = top + line_h * 0.5f;
        bool f_selected = std::find(selected.begin(),selected.end(),r.k->id) != selected.end();
        //Lit orange while selected, light blue while he is the hover - here or out on the map.
        if (f_selected || (hovered.kind == PLAY_PICK_PERSON && hovered.id == r.k->id)){
            overlay->AddRect(vec2(p0.x + 4.0f * s,top + 1.0f * s),vec2(p1.x - 4.0f * s,top + line_h - 1.0f * s),6.0f * s,
                             f_selected ? HudFaded(HUD_SELECT,60) : HudFaded(HUD_PERSON_HOVER,55));
        }
        DrawPersonBadge(overlay,vec2(p0.x + pad + badge,mid),s,*r.k,f_selected ? HUD_SELECT : 0);
        overlay->AddText(r.name.c_str(),vec2(name_x,mid + 5.0f * s),size,HUD_TEXT,UI_ALIGN_LEFT);
        overlay->AddText(r.need,vec2(need_x,mid + 5.0f * s),size,r.need_colour,UI_ALIGN_LEFT);
        overlay->AddText(r.health,vec2(p1.x - pad,mid + 5.0f * s),size,r.colour,UI_ALIGN_RIGHT);
        overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_PERSON,(int)r.k->id});
    }
}

void ApplicationChasm::DrawSelectionOverlay(float s){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !z || z->world != w){
        return;
    }
    const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
    PlayPick sel;
    PlayPick hovered;
    std::vector<uint32_t> group;        //a group's people (selection_plan.md, "Several people selected")
    bool f_box = false;                 //...and a box being dragged, corner to corner
    vec2 box0, box1;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
        hovered = play_hover;
        if (sel.kind == PLAY_PICK_GROUP){
            group = play_group;
        }
        f_box = f_box_shown;
        box0 = vec2(std::min(box_a.x,box_b.x),std::min(box_a.y,box_b.y));
        box1 = vec2(std::max(box_a.x,box_b.x),std::max(box_a.y,box_b.y));
    }
    float width = (float)main_window->width;
    float height = (float)main_window->height;
    //THE COLONY the selection belongs to, at the top left (SelectionColony) - in the calendar's panel and lettering -
    //under the goals and REC (hud_left_y, DrawOverlay).
    if (ep){
        std::string colony = SelectionColony(sel,group,*w,*ep);
        if (!colony.empty()){
            float text = 20.0f * s;
            vec2 q0(16.0f * s,hud_left_y);
            vec2 q1(q0.x + overlay->MeasureText(colony.c_str(),text).x + 32.0f * s,q0.y + 44.0f * s);
            overlay->AddRect(q0,q1,12.0f * s,HUD_PANEL);
            overlay->AddText(colony.c_str(),vec2(q0.x + 16.0f * s,q0.y + 29.0f * s),text,HUD_TEXT,UI_ALIGN_LEFT);
            overlay_hits_drawing.push_back(OverlayHit{q0,q1,OVERLAY_HIT_BLOCK,0});
        }
    }
    /*
        PINNED: everyone who belongs with the selection, and the selected person himself - or a group, all of them -
        his badge on a stem in the selection's colour, kept on screen at its edge when he is off it, so he can be found.
    */
    std::vector<uint32_t> pinned;
    if (ep){
        RelatedPeople(sel,*z,*ep,pinned);
        pinned.insert(pinned.end(),group.begin(),group.end());
        if (sel.kind == PLAY_PICK_PERSON){
            pinned.push_back(sel.id);
        }
    }
    //Where a pin over a world point goes: kept on screen, off the bottom edge the panels stand on, with
    //whether it had to be (then it has no stem). False behind the camera.
    auto pin_spot = [&](const vec3& stem, const vec3& top, float rise, vec2& head, vec2& dot, bool& f_off){
        vec2 tip;
        if (!ScreenOf(world_scene->camera,width,height,top,tip)){
            return false;
        }
        ScreenOf(world_scene->camera,width,height,stem,head);
        float margin = 18.0f * s;
        float bottom = height - 64.0f * s;
        f_off = tip.x < margin || tip.y < margin || tip.x > width - margin || tip.y > bottom;
        dot = vec2(std::max(margin,std::min(width - margin,tip.x)),std::max(margin,std::min(bottom,tip.y - rise * s)));
        return true;
    };
    auto person_spot = [&](const EconomyWorker& k, vec2& head, vec2& dot, bool& f_off){
        vec3 feet = FeetOf(*w,z.get(),k);
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
                                                                          : FeetOf(*w,z.get(),k).y + PERSON_PIN;
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
            bool f_selected = (sel.kind == PLAY_PICK_PERSON && sel.id == id)
                              || std::find(group.begin(),group.end(),id) != group.end();
            uint32_t colour = f_selected ? HUD_SELECT : HUD_AMBER;
            if (!f_off){
                overlay->AddLine(head,dot,2.0f * s,HudFaded(colour,220));
            }
            DrawPersonBadge(overlay,dot,s,*k,colour);
        }
    }

    //THE BOX being dragged (step 2): faintly filled, edged in the hover's light blue, as are the people it would take.
    if (f_box){
        overlay->AddRect(box0,box1,0.0f,HudFaded(HUD_PERSON_HOVER,28));
        overlay->AddRectOutline(box0,box1,0.0f,1.5f * s,HudFaded(HUD_PERSON_HOVER,210));
    }

    //THE CARD, on the right under the date's level.
    Card card;
    if (!MakeCard(sel,*w,*z,ep,calendar_shown.load(),card,group)){
        return;
    }
    const float pad = 14.0f * s;
    const float title_size = 19.0f * s;
    const float text_size = 14.0f * s;
    const float line_h = 20.0f * s;
    const float bar_h = 26.0f * s;
    const float chips_h = 30.0f * s;
    const float row_h = 26.0f * s;             //a group's row (CARD_PERSON), as tall as the neediest's
    const float badge = BADGE_RADIUS * s;
    const float drop_w = 22.0f * s;            //...and its x, at the right
    float card_w = CARD_W * s;
    //The title, and at its right the subtitle, clear of the close mark.
    float head_w = overlay->MeasureText(card.title.c_str(),title_size).x + pad * 2.0f + 30.0f * s;
    if (!card.subtitle.empty()){
        head_w += overlay->MeasureText(card.subtitle.c_str(),text_size).x + 16.0f * s;
    }
    card_w = std::max(card_w,head_w);
    for (const CardLine& l : card.lines){
        //A long line widens the card rather than running off it - and so does a row of chips, and a group's row.
        float before = (l.type == CARD_PERSON) ? badge * 2.0f + 8.0f * s : 0.0f;
        float after = (l.type == CARD_PERSON) ? overlay->MeasureText(l.right.c_str(),text_size).x + 12.0f * s + drop_w : 0.0f;
        card_w = std::max(card_w,overlay->MeasureText(l.text.c_str(),text_size).x + before + after + pad * 2.0f);
        if (l.type == CARD_CHOICE){
            float row = pad * 2.0f;
            for (const auto& ch : l.choices){
                row += overlay->MeasureText(ch.first.c_str(),text_size).x + 20.0f * s;
            }
            card_w = std::max(card_w,row);
        }
        //A goods strip as wide as its header, or its key on one line - past the limit below, the key wraps.
        if (l.type == CARD_STOCK){
            card_w = std::max(card_w,HudStockWidth(overlay,l.stock,s) + pad * 2.0f);
        }
    }
    card_w = std::min(card_w,width * 0.45f);
    /*
        A store's goods chips go a row a GROUP of goods (HudGoodsGroup, goods_plan.md): the group's own chip, then
        its goods' from one indent, past the widest group chip, so they line up - and WRAP onto another row, at that
        indent, where the next would run off the card: with provisions (defence_plan.md) six in a row ran off its
        edge, and more goods are coming. The card widens to fit three goods a row past the indent, and no more.
        Where each chip goes, for the height here and the drawing below.
    */
    const float chip_gap = 6.0f * s;
    const float swatch_w = 13.0f * s;      //a good's chip: its colour's swatch before its name (HudGoodColour)
    auto chip_w = [&](const char* name){
        return overlay->MeasureText(name,text_size).x + 14.0f * s;
    };
    auto good_chip_w = [&](int g){
        return chip_w(GoodName(g)) + swatch_w;
    };
    float indent = 0.0f;
    float three = 0.0f;     //the widest group's first three goods, a row of them
    for (int k = 0; k < HUD_GROUP_COUNT; k++){
        indent = std::max(indent,chip_w(HudGoodsGroupName(k)) + chip_gap);
        float row = 0.0f;
        int n = 0;
        for (int g : HudGoodsOrder()){
            if (HudGoodsGroup(g) == k && n++ < 3){
                row += good_chip_w(g) + chip_gap;
            }
        }
        three = std::max(three,row);
    }
    for (const CardLine& l : card.lines){
        if (l.type == CARD_CHIPS){
            card_w = std::max(card_w,std::min(pad * 2.0f + indent + three,width * 0.45f));
        }
    }
    //Calls place(group, good, x, row) for each chip at `width` - good -1 for the group's own - and returns the rows.
    auto chip_rows = [&](float width, auto&& place) -> int {
        int row = 0;
        for (int k = 0; k < HUD_GROUP_COUNT; k++){
            place(k,-1,0.0f,row);
            float x = indent;
            for (int g : HudGoodsOrder()){
                if (HudGoodsGroup(g) != k){
                    continue;
                }
                float w = good_chip_w(g);
                if (x > indent && x + w > width){
                    row++;
                    x = indent;
                }
                place(k,g,x,row);
                x += w + chip_gap;
            }
            row++;
        }
        return row;
    };
    auto goods_rows = [&](){
        return chip_rows(card_w - pad * 2.0f,[](int,int,float,int){});
    };
    /*
        Toggles one after another share a row while they fit (the user's, 2026-10-19): a posted person's Release, Grab
        Supplies and Pack a Tent side by side. Each one's x along its row, and whether it starts one.
    */
    std::vector<float> toggle_x(card.lines.size(),0.0f);
    std::vector<char> f_toggle_row(card.lines.size(),1);
    float run = 0.0f;
    for (size_t i = 0; i < card.lines.size(); i++){
        if (card.lines[i].type != CARD_TOGGLE){
            continue;
        }
        float w = chip_w(card.lines[i].text.c_str());
        if (i > 0 && card.lines[i - 1].type == CARD_TOGGLE && run + w <= card_w - pad * 2.0f){
            toggle_x[i] = run;
            f_toggle_row[i] = 0;
        }else{
            run = 0.0f;
        }
        run += w + chip_gap;
    }
    float h = pad + 30.0f * s;
    for (size_t i = 0; i < card.lines.size(); i++){
        const CardLine& l = card.lines[i];
        h += (l.type == CARD_BAR) ? bar_h : (l.type == CARD_CHIPS) ? chips_h * goods_rows()
           : (l.type == CARD_TOGGLE) ? (f_toggle_row[i] ? chips_h : 0.0f)
           : (l.type == CARD_CHOICE) ? chips_h
           : (l.type == CARD_STOCK) ? HudStockHeight(overlay,l.stock,card_w - pad * 2.0f,s)
           : (l.type == CARD_PERSON) ? row_h : line_h;
    }
    h += pad * 0.5f;
    /*
        A card too long to end above the tools' grid and what stacks over it (DrawIssues) goes to their left,
        rather than over them - as the jobs overview rises over them (DrawJobsOverview). A camp full of
        settlers, with its pantry, woodshed and supplies, runs that long.
    */
    float right = width - 16.0f * s;
    if (128.0f * s + h > build_stack_top - 8.0f * s){
        right = std::min(right,build_stack_left - 10.0f * s);
    }
    vec2 c0(right - card_w,128.0f * s);
    vec2 c1(right,c0.y + h);
    overlay->AddRect(c0,c1,10.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{c0,c1,OVERLAY_HIT_BLOCK,0});
    float y = c0.y + pad + 14.0f * s;
    overlay->AddText(card.title.c_str(),vec2(c0.x + pad,y),title_size,HUD_TEXT,UI_ALIGN_LEFT);
    //The close mark, top right, and the subtitle before it.
    vec2 x0(c1.x - pad - 14.0f * s,c0.y + pad - 4.0f * s);
    vec2 x1(c1.x - pad + 2.0f * s,x0.y + 16.0f * s);
    overlay->AddLine(x0 + vec2(3.0f * s,3.0f * s),x1 - vec2(3.0f * s,3.0f * s),2.0f * s,HUD_TEXT_DIM);
    overlay->AddLine(vec2(x1.x - 3.0f * s,x0.y + 3.0f * s),vec2(x0.x + 3.0f * s,x1.y - 3.0f * s),2.0f * s,HUD_TEXT_DIM);
    overlay_hits_drawing.push_back(OverlayHit{x0 - vec2(4.0f * s,4.0f * s),x1 + vec2(4.0f * s,4.0f * s),OVERLAY_HIT_CLOSE,0});
    if (!card.subtitle.empty()){
        overlay->AddText(card.subtitle.c_str(),vec2(x0.x - 10.0f * s,y),text_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }
    //The selection's orange as a rule under the title (the user's, 2026-10-19), where it was the card's left edge.
    overlay->AddRect(vec2(c0.x + pad,y + 8.0f * s),vec2(c1.x - pad,y + 10.0f * s),1.0f * s,HUD_SELECT);
    y += 16.0f * s;
    /*
        A chip: lit while on; a click turns it over (not while it is fixed). Returns where the next one goes.
        A GOOD's chip is its colour (HudGoodColour): lit, filled with it and ticked; off, its swatch before
        its name - the same width either way, so a click moves nothing.
    */
    auto chip = [&](float x, const char* name, bool f_on, bool f_locked, int action, int value, uint32_t good_colour = 0) -> float {
        float tw = overlay->MeasureText(name,text_size).x + (good_colour ? swatch_w : 0.0f);
        vec2 k0(x,y - 24.0f * s);
        vec2 k1(x + tw + 14.0f * s,y - 4.0f * s);
        uint8_t alpha = f_locked ? 110 : 235;
        if (f_on){
            overlay->AddRect(k0,k1,8.0f * s,HudFaded(good_colour ? good_colour : HUD_AMBER,alpha));
        }else{
            overlay->AddRectOutline(k0,k1,8.0f * s,1.5f * s,HudFaded(HUD_TEXT,alpha / 2));
        }
        float tx = x + 7.0f * s;
        if (good_colour){
            vec2 q0(tx,y - 18.5f * s);
            vec2 q1(tx + 9.0f * s,y - 9.5f * s);
            if (f_on){
                uint32_t ink = HudFaded(HUD_PANEL,255);
                overlay->AddLine(vec2(q0.x + 1.0f * s,q0.y + 4.5f * s),vec2(q0.x + 3.5f * s,q1.y - 1.0f * s),1.8f * s,ink);
                overlay->AddLine(vec2(q0.x + 3.5f * s,q1.y - 1.0f * s),vec2(q1.x,q0.y + 0.5f * s),1.8f * s,ink);
            }else{
                overlay->AddRect(q0,q1,2.0f * s,HudFaded(good_colour,alpha));
            }
            tx += swatch_w;
        }
        overlay->AddText(name,vec2(tx,y - 9.0f * s),text_size,f_on ? HudFaded(HUD_PANEL,255) : HudFaded(HUD_TEXT,alpha),
                         UI_ALIGN_LEFT);
        if (!f_locked){
            overlay_hits_drawing.push_back(OverlayHit{k0,k1,action,value});
        }
        return k1.x + 6.0f * s;
    };
    for (size_t i = 0; i < card.lines.size(); i++){
        const CardLine& l = card.lines[i];
        if (l.type == CARD_TEXT){
            y += line_h;
            if (l.person){
                //A line about a person: lit light blue while he is the hover - this line, or him on the map - and
                //a click on it selects him.
                vec2 r0(c0.x + 6.0f * s,y - line_h + 1.0f * s);
                vec2 r1(c1.x - 6.0f * s,y + 1.0f * s);
                if (hovered.kind == PLAY_PICK_PERSON && hovered.id == l.person){
                    overlay->AddRect(r0,r1,5.0f * s,HudFaded(HUD_PERSON_HOVER,55));
                }
                overlay_hits_drawing.push_back(OverlayHit{r0,r1,OVERLAY_HIT_FAMILY,(int)l.person});
            }
            overlay->AddText(l.text.c_str(),vec2(c0.x + pad,y - 5.0f * s),text_size,l.colour,UI_ALIGN_LEFT);
        }else if (l.type == CARD_PERSON){
            //A group's row: lit light blue while he is the hover, a click selects him alone, its x takes him out.
            y += row_h;
            float mid = y - row_h * 0.5f;
            vec2 r0(c0.x + 6.0f * s,y - row_h + 1.0f * s);
            vec2 r1(c1.x - 6.0f * s,y - 1.0f * s);
            if (hovered.kind == PLAY_PICK_PERSON && hovered.id == l.person){
                overlay->AddRect(r0,r1,5.0f * s,HudFaded(HUD_PERSON_HOVER,55));
            }
            overlay_hits_drawing.push_back(OverlayHit{r0,r1,OVERLAY_HIT_FAMILY,(int)l.person});
            const EconomyWorker* k = ep ? PersonById(*ep,l.person) : NULL;
            if (k){
                DrawPersonBadge(overlay,vec2(c0.x + pad + badge,mid),s,*k,HUD_SELECT);
            }
            overlay->AddText(l.text.c_str(),vec2(c0.x + pad + badge * 2.0f + 8.0f * s,mid + 5.0f * s),text_size,l.colour,
                             UI_ALIGN_LEFT);
            vec2 x_mid(c1.x - pad - drop_w * 0.5f + 4.0f * s,mid);
            overlay->AddText(l.right.c_str(),vec2(x_mid.x - drop_w * 0.5f - 4.0f * s,mid + 5.0f * s),text_size,HUD_TEXT_DIM,
                             UI_ALIGN_RIGHT);
            float a = 4.5f * s;
            overlay->AddLine(x_mid - vec2(a,a),x_mid + vec2(a,a),2.0f * s,HUD_TEXT_DIM);
            overlay->AddLine(vec2(x_mid.x + a,x_mid.y - a),vec2(x_mid.x - a,x_mid.y + a),2.0f * s,HUD_TEXT_DIM);
            //After the row's, so it wins where they overlap (OverlayHitAt takes the last drawn).
            overlay_hits_drawing.push_back(OverlayHit{x_mid - vec2(drop_w * 0.5f,row_h * 0.5f - 1.0f * s),
                                                      x_mid + vec2(drop_w * 0.5f,row_h * 0.5f - 1.0f * s),
                                                      OVERLAY_HIT_GROUP_DROP,(int)l.person});
        }else if (l.type == CARD_BAR){
            y += bar_h;
            overlay->AddText(l.text.c_str(),vec2(c0.x + pad,y - 11.0f * s),text_size,l.colour,UI_ALIGN_LEFT);
            vec2 b0(c0.x + pad,y - 6.0f * s);
            vec2 b1(c1.x - pad,y - 2.0f * s);
            overlay->AddRect(b0,b1,2.0f * s,HudFaded(HUD_TEXT,45));
            overlay->AddRect(b0,vec2(b0.x + (b1.x - b0.x) * l.bar,b1.y),2.0f * s,HudFaded(l.colour,210));
        }else if (l.type == CARD_TOGGLE){
            y += f_toggle_row[i] ? chips_h : 0.0f;
            chip(c0.x + pad + toggle_x[i],l.text.c_str(),l.chips_on != 0,l.f_locked,
                 l.choice_action ? l.choice_action : OVERLAY_HIT_SITE_FIRST,0);
        }else if (l.type == CARD_CHOICE){
            //One chip per choice, the one in force lit; a click on another chooses it.
            y += chips_h;
            float x = c0.x + pad;
            for (const auto& ch : l.choices){
                x = chip(x,ch.first.c_str(),ch.second == l.choice_on,l.f_locked,l.choice_action,ch.second);
            }
        }else if (l.type == CARD_STOCK){
            y += DrawHudStock(overlay,l.stock,c0.x + pad,y,c1.x - c0.x - pad * 2.0f,s);
        }else{
            //A row a group: its chip - lit while the store takes all of the group, a click all or none of it - then a
            //chip per good, lit if the store takes it.
            float top = y;
            int rows = chip_rows(c1.x - c0.x - pad * 2.0f,[&](int k, int g, float x, int row){
                y = top + chips_h * (row + 1);
                if (g < 0){
                    GoodsMask group = HudGoodsGroupMask(k);
                    chip(c0.x + pad + x,HudGoodsGroupName(k),(l.chips_on & group) == group,l.f_locked,
                         OVERLAY_HIT_STORE_ALLOW_GROUP,k);
                }else{
                    chip(c0.x + pad + x,GoodName(g),(l.chips_on & GOOD_BIT(g)) != 0,l.f_locked,OVERLAY_HIT_STORE_ALLOW,g,
                         HudGoodColour(g));
                }
            });
            y = top + chips_h * rows;
        }
    }
}

//--- The tool --------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterSelectTools(){
    MCPServer::Get()->RegisterTool("chasm_select",
        "Play mode's selection (docs/selection_plan.md) - what a click selects there, without the mouse. "
        "op select: the building at x,z (or by `building` id) - or the gate there, for its door (docs/defence_plan.md), or the "
        "garden or lot being built there (kind ground, by its patch's lowest plot: docs/yards_plan.md) - or a person by `person` id, "
        "or a crate by `crate` id (docs/crates_plan.md) - or several grown-ups by `people` (ids), a GROUP; op add / remove `person`: "
        "into the selection or out of it, as a Shift+click does; op box px0,py0,px1,py1: a box dragged and let go there - every "
        "grown-up out of doors whose figure's middle is inside, selected or with `shift` added, `in_box` who; `hold` true draws it as "
        "still being dragged, for a screenshot, until an op box without it; op clear; op hover px,py: the hover pinned at that "
        "pixel in place of the mouse's - the overlay's and the scene's - so what it shows is in the next screenshot (the top bar's "
        "FOOD lists its foods, a person's light blue outline, his line on a card lit); it returns the hover - "
        "op hover with no pixel gives it back; op under "
        "px,py: what the cursor would pick at that pixel (and the plot and building the ground pick alone gives, `figure` the person "
        "whose figure is drawn at the pixel itself, `read` whether the engine's readback came, and the overlay's chip there with its value); "
        "under, press and hover have the engine read the pixel as the mouse's, a few frames; "
        "op hits: every chip and button on the overlay, its action, value and centre x,y - a card's line naming a person is `family`; op "
        "press px,py: a left click there, as the player's (`shift` true: with Shift held) - on the HUD it presses what is drawn there (a tool of the "
        "grid, a card's chip, pause, speed, a row of the neediest list or of NEEDS ATTENTION), on the scene it selects; `pressed` says what "
        "the HUD had there and the tool in hand after. With `button` right it is a right click: a tool put down, a "
        "selected person sent to the plot there (an order, docs/defence_plan.md) - on a guard tower, posted as its guard by hand (step 8) - or the selection cleared; op "
        "status (default): the selection and hover, `group` (its people, for a group), the card as text lines, and the people pinned with it. "
        "View state only - nothing recorded but what a press sends: a chip's setting, an order, a Release. The card "
        "and outlines show in play mode (chasm_view play). "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"building",{{"type","integer"}}},
                {"person",{{"type","integer"}}},
                {"crate",{{"type","integer"}}},
                {"px",{{"type","integer"}}},
                {"py",{{"type","integer"}}},
                {"button",{{"type","string"},{"description","press: left (default) or right"}}},
                {"shift",{{"type","boolean"},{"description","press: with Shift held - a person into the selection or out"}}},
                {"people",{{"type","array"},{"items",{{"type","integer"}}},{"description","select: several people, a group"}}},
                {"px0",{{"type","number"}}},
                {"py0",{{"type","number"}}},
                {"px1",{{"type","number"}}},
                {"py1",{{"type","number"}}},
                {"hold",{{"type","boolean"},{"description","box: drawn as still being dragged"}}},
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
            static const char* const actions[] = {"panel","tool","store_allow","close","site_first","field_crop",
                                                  "field_next","pause","speed","field_land","person","release",
                                                  "take_tool","building","gate","ladder","tower_guards","guard_release",
                                                  "store_allow_group","keep","make_more","gather","family",
                                                  "group_drop","tent","pitch","settle","work_stop","forage","pick_up","stats",
                                                  "replay_stop"};
            const int last = (int)(sizeof(actions) / sizeof(actions[0])) - 1;
            /*
                The hover pinned at a pixel in place of the mouse's, until op hover with none - the overlay's (goods_plan.md:
                the top bar's FOOD lists its foods while hovered) and the scene's, read by the engine there as the mouse's
                would be - for a screenshot of what is drawn for it: a person's light blue outline, his line on a card lit.
            */
            if (op == "hover"){
                bool f_pin = args.contains("px") && args.contains("py") && args["px"].is_number() && args["py"].is_number();
                if (f_pin){
                    overlay_mouse_x = args["px"].get<int>();
                    overlay_mouse_y = args["py"].get<int>();
                    f_overlay_mouse_pinned = true;
                    bool f_read = WaitForPickAt(int2(overlay_mouse_x.load(),overlay_mouse_y.load()));
                    //...and a few frames more, for a pass to take the hover up and a frame to draw it.
                    InputController* input = main_scene->inputcontroller;
                    uint32_t serial = input->GetPickSerial();
                    for (int i = 0; i < 100 && input->GetPickSerial() - serial < 3; i++){
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    PlayPick h;
                    {
                        std::lock_guard<std::mutex> lock(pick_mutex);
                        h = play_hover;
                    }
                    const char* kinds[] = {"none","building","person","gate","crate","ground"};
                    return json{{"hover_pinned",true},{"read",f_read},
                                {"hover",json{{"kind",kinds[std::max(0,std::min(PLAY_PICK_GROUND,h.kind))]},{"id",h.id}}}};
                }
                f_overlay_mouse_pinned = false;
                RestorePickPin();
                return json{{"hover_pinned",false}};
            }
            if (op == "under" && args.contains("px") && args.contains("py") && args["px"].is_number() && args["py"].is_number()){
                int2 px(args["px"].get<int>(),args["py"].get<int>());
                //The engine's own answer at the pixel first: which person's figure is there (PlayPickUnder).
                bool f_read = WaitForPickAt(px);
                json out;
                main_scene->AtTickBoundary([&](){
                    GridPick ground = PickUnderPixel(px);
                    PlayPick p = PlayPickUnder(px,ground);
                    const char* kinds[] = {"none","building","person","gate","crate","ground"};
                    out = json{{"kind",kinds[std::max(0,std::min(PLAY_PICK_GROUND,p.kind))]},{"id",p.id},{"plot",p.plot},
                               {"ground_plot",ground.f_hit ? ground.plot : -1},
                               {"ground_building",(ground.f_hit && ground.plot < (int)z->building.size()) ? z->building[ground.plot] : 0},
                               {"read",f_read}};
                    //A person's figure under the pixel itself, not one picked near it.
                    objectid_t id = main_scene->inputcontroller->GetHoveredObjectID();
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    for (const auto& f : person_objects){
                        if (f.first == id){
                            out["figure"] = f.second;
                        }
                    }
                });
                RestorePickPin();
                //...and what of the overlay is drawn there, which a press there would press - nothing pressed.
                OverlayHit hit;
                bool f_hit = OverlayHitAt(px,&hit);
                out["overlay"] = f_hit ? json(actions[std::max(0,std::min(last,hit.action))]) : json(nullptr);
                if (f_hit){
                    out["overlay_value"] = hit.value;      //a chip's: a good, a crop, a count, a person
                }
                return json{{"under",out}};
            }
            //Every place on the overlay a press would press, as drawn last frame - so a chip is found without
            //scanning pixels with `under`. Panels left out.
            if (op == "hits"){
                json list = json::array();
                std::lock_guard<std::mutex> lock(pick_mutex);
                for (const OverlayHit& h : overlay_hits){
                    if (h.action != OVERLAY_HIT_BLOCK){
                        list.push_back(json{{"action",actions[std::max(0,std::min(last,h.action))]},{"value",h.value},
                                            {"x",(h.a.x + h.b.x) * 0.5f},{"y",(h.a.y + h.b.y) * 0.5f},
                                            {"w",h.b.x - h.a.x},{"h",h.b.y - h.a.y}});
                    }
                }
                return json{{"hits",list}};
            }
            /*
                THE BOX (selection_plan.md, "Several people selected", step 2), px0,py0 to px1,py1: who it takes, and
                let go as a drag would be - the selection, or added to it with `shift`; an empty one changes nothing.
                With `hold` it is drawn as though still being dragged, its people outlined, until an op box without it.
            */
            json boxed;
            if (op == "box"){
                bool f_pixels = args.contains("px0") && args.contains("py0") && args.contains("px1") && args.contains("py1")
                                && args["px0"].is_number() && args["py0"].is_number() && args["px1"].is_number() && args["py1"].is_number();
                vec2 a, b;
                std::vector<uint32_t> ids;
                if (f_pixels){
                    a = vec2(args["px0"].get<float>(),args["py0"].get<float>());
                    b = vec2(args["px1"].get<float>(),args["py1"].get<float>());
                    main_scene->AtTickBoundary([&](){
                        ids = PeopleInBox(a,b);
                    });
                    boxed = ids;
                }
                bool f_hold = f_pixels && args.value("hold",false);
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    f_box_scripted = f_hold;
                    f_box_shown = f_hold;
                    box_a = a;
                    box_b = b;
                    box_people = f_hold ? ids : std::vector<uint32_t>();
                }
                if (f_pixels && !f_hold){
                    TakeBox(ids,args.value("shift",false));
                }
            }
            /*
                A left click at a pixel, as the player's (UpdatePlayPick): on the overlay it presses what is
                drawn there - a tool, a chip, the calendar's buttons, a row of the neediest - and on the
                scene it selects. Taken on the next pass, which runs paused too; the reply waits for it.
            */
            json pressed;
            if (op == "press"){
                if (!PlayMode()){
                    return json{{"error","press is play mode's click - chasm_view play first"}};
                }
                if (!(args.contains("px") && args.contains("py") && args["px"].is_number() && args["py"].is_number())){
                    return json{{"error","press needs px and py"}};
                }
                int2 px(args["px"].get<int>(),args["py"].get<int>());
                OverlayHit hit;
                if (OverlayHitAt(px,&hit)){
                    pressed = json{{"overlay",actions[std::max(0,std::min(last,hit.action))]},{"value",hit.value}};
                }else{
                    pressed = json{{"overlay",nullptr}};
                }
                //What the engine reads there - a person's figure, a deck's surface - as under the mouse.
                pressed["read"] = WaitForPickAt(px);
                {
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    script_press_px = px;
                }
                bool f_right = args.value("button",std::string("left")) == "right";
                pressed["button"] = f_right ? "right" : "left";
                f_script_press_right = f_right;
                f_script_press_shift = args.value("shift",false);
                f_script_press = true;
                for (int i = 0; i < 400 && f_script_press.load(); i++){
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                RestorePickPin();
                pressed["taken"] = !f_script_press.load();
                pressed["tool"] = (int)paint_tool.load();
            }
            //Several people (selection_plan.md, "Several people selected"): a list, as Shift+clicks would build it - grown-ups
            //only, as ever - or one added or taken out, as one Shift+click does.
            bool f_people = op == "select" && args.contains("people") && args["people"].is_array();
            if (f_people){
                std::vector<uint32_t> ids;
                for (const json& v : args["people"]){
                    if (v.is_number_unsigned()){
                        ids.push_back(v.get<uint32_t>());
                    }
                }
                SelectPeople(ids);
                SayPicked(ids);
            }
            if ((op == "add" || op == "remove") && args.contains("person") && args["person"].is_number_unsigned()){
                uint32_t id = args["person"].get<uint32_t>();
                std::vector<uint32_t> ids = SelectedPeople();
                bool f_in = std::find(ids.begin(),ids.end(),id) != ids.end();
                if (f_in != (op == "add")){
                    ToggleSelected(id);
                }
            }
            if (op == "clear" || (op == "select" && !f_people)){
                PlayPick p;
                if (op == "select"){
                    //Checked as numbers first: this build has no exceptions, so a get<> on a null aborts the app.
                    if (args.contains("person") && args["person"].is_number_unsigned()){
                        p.kind = PLAY_PICK_PERSON;
                        p.id = args["person"].get<uint32_t>();
                    }else if (args.contains("crate") && args["crate"].is_number_unsigned()){
                        //A crate (crates_plan.md), by its id - the plot it lies on with it.
                        p.kind = PLAY_PICK_CRATE;
                        p.id = args["crate"].get<uint32_t>();
                        const EconomyCrate* c = NULL;
                        if (e && e->world == w){
                            for (const EconomyCrate& x : e->crates){
                                c = (x.id == p.id) ? &x : c;
                            }
                        }
                        if (!c){
                            return json{{"error","no such crate"}};
                        }
                        p.plot = c->plot;
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
                            }else if (z->gate[g.plot]){
                                p.kind = PLAY_PICK_GATE;        //a wall's gate (defence_plan.md)
                                p.id = (uint32_t)g.plot;
                                p.plot = g.plot;
                            }else if (const EconomyCrate* c = (e && e->world == w) ? EconomyCrateOn(*e,g.plot) : NULL){
                                p.kind = PLAY_PICK_CRATE;       //a crate lying there (crates_plan.md)
                                p.id = c->id;
                                p.plot = g.plot;
                            }else{
                                std::vector<int> patch;         //a garden or lot being built (yards_plan.md)
                                GroundPatch(*z,g.plot,patch);
                                if (!patch.empty()){
                                    p.kind = PLAY_PICK_GROUND;
                                    p.id = (uint32_t)patch[0];
                                    p.plot = g.plot;
                                }
                            }
                        }
                    }
                    if (p.kind == PLAY_PICK_NONE){
                        return json{{"error","nothing to select there"}};
                    }
                }
                if (p.kind == PLAY_PICK_PERSON){
                    SelectPeople({p.id});
                    Say(p.id,"picked",CHASM_SAY_PICKED);
                }else{
                    std::lock_guard<std::mutex> lock(pick_mutex);
                    SelectLocked(p);
                }
            }
            PlayPick sel, hover;
            std::vector<uint32_t> group;
            {
                std::lock_guard<std::mutex> lock(pick_mutex);
                sel = play_selected;
                hover = play_hover;
                if (sel.kind == PLAY_PICK_GROUP){
                    group = play_group;
                }
            }
            auto pick_json = [](const PlayPick& p) -> json {
                const char* kinds[] = {"none","building","person","gate","crate","ground","group"};
                return json{{"kind",kinds[std::max(0,std::min(PLAY_PICK_GROUP,p.kind))]},{"id",p.id},{"plot",p.plot}};
            };
            json result{{"selected",pick_json(sel)},{"hover",pick_json(hover)},{"play_mode",PlayMode()}};
            if (sel.kind == PLAY_PICK_GROUP){
                result["group"] = group;
            }
            if (!pressed.is_null()){
                result["pressed"] = pressed;
            }
            if (!boxed.is_null()){
                result["in_box"] = boxed;
            }
            const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
            Card card;
            if (MakeCard(sel,*w,*z,ep,calendar_shown.load(),card,group)){
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
                    }else if (l.type == CARD_CHOICE){
                        json labels = json::array();
                        std::string on;
                        for (const auto& ch : l.choices){
                            labels.push_back(ch.first);
                            on = (ch.second == l.choice_on) ? ch.first : on;
                        }
                        lines.push_back(json{{"choice",labels},{"on",on}});
                    }else if (l.type == CARD_STOCK){
                        //A goods strip: by name, what it holds and what of that is reserved, its room (0 none), kept or anyone's.
                        json goods = json::object();
                        json reserved = json::object();
                        for (int g = 0; g < GOOD_COUNT; g++){
                            if (l.stock.goods[g] > 0){
                                goods[GoodName(g)] = l.stock.goods[g];
                            }
                            int r = std::min(l.stock.reserved[g],l.stock.goods[g]);
                            if (r > 0){
                                reserved[GoodName(g)] = r;
                            }
                        }
                        lines.push_back(json{{"stock",l.stock.label},{"whose",l.stock.whose},{"kept",l.stock.f_kept},
                                             {"room",l.stock.room},{"goods",goods},{"reserved",reserved}});
                        if (l.stock.f_load){
                            lines.back()["load"] = true;    //a person's load: striped through
                        }
                    }else if (l.type == CARD_PERSON){
                        lines.push_back(json{{"person",l.person},{"text",l.text},{"pack",l.right}});
                    }else{
                        lines.push_back(l.type == CARD_BAR ? json{{"text",l.text},{"bar",l.bar}} : json(l.text));
                    }
                }
                result["card"] = json{{"title",card.title},{"subtitle",card.subtitle},{"lines",lines}};
            }
            if (ep){
                std::vector<uint32_t> related;
                RelatedPeople(sel,*z,*ep,related);
                result["pinned"] = related;
                result["colony"] = SelectionColony(sel,group,*w,*ep);     //as the top left shows it
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
