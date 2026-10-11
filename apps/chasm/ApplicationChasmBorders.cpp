#include "ApplicationChasm.h"
#include "ChasmHud.h"
#include "MeshBuild.h"

#include <cmath>

/*
    SETTLEMENTS, DRAWN (docs/settlements_plan.md): the border of every territory as The Settlers marks one - a slim
    post with a little flag at the middle of every grid edge whose two plots lie in different settlements, or
    one in none, the flag turned toward the territory. Not along a cliff's edge: the drop is border enough, and a
    rim lined with posts would hide it. View only: one mesh for the map, rebuilt when the territories are worked out
    again (Economy::Settle, on a change of the zones).

    And with a person selected, how far it is to walk to the plot under the cursor (DrawWalkTip).
*/

#define BORDER_POST_W       0.08f
#define BORDER_POST_HEIGHT  0.85f   //over most of what grows, so a line of them reads from play distance
#define BORDER_FLAG_LENGTH  0.32f   //along its post's side toward the territory
#define BORDER_FLAG_H       0.20f
#define BORDER_FLAG_THICK   0.03f
#define BORDER_FOOTING      0.10f   //below the ground's relief, so a post never floats

namespace {

//A beam from a to b, `thick` across and `height` up from y0, in palette cell (column, row): sides, top and ends.
void BorderBeam(std::vector<vertex>& out, const vec2& a, const vec2& b, float y0, float height, float thick,
                int column, int row){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-5f){
        return;
    }
    dir = dir / len;
    vec2 side(-dir.y * thick * 0.5f,dir.x * thick * 0.5f);
    vec3 al(a.x + side.x,y0,a.y + side.y), ar(a.x - side.x,y0,a.y - side.y);
    vec3 bl(b.x + side.x,y0,b.y + side.y), br(b.x - side.x,y0,b.y - side.y);
    vec3 up(0.0f,height,0.0f);
    vec3 n_side(side.x,0.0f,side.y);
    MeshQuad(out,al,bl,bl + up,al + up,n_side,column,row);
    MeshQuad(out,ar,br,br + up,ar + up,n_side * -1.0f,column,row);
    MeshQuad(out,al + up,bl + up,br + up,ar + up,vec3(0,1,0),column,row);
    MeshQuad(out,al,ar,ar + up,al + up,vec3(-dir.x,0.0f,-dir.y),column,row);
    MeshQuad(out,bl,br,br + up,bl + up,vec3(dir.x,0.0f,dir.y),column,row);
}

}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadBorders(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    if (!border_object){
        main_scene->AtTickBoundary([&](){
            Object* o = new Object();
            o->name = "settlement borders";
            o->SetMesh(new Mesh());
            o->SetMaterialSlot(0,palette_material);
            o->SetPickability(false);
            o->SetVisualOnly(true);
            o->SetVisibility(false);
            main_scene->AddObject(o);
            border_object = o;
        });
        if (!border_object){
            return;
        }
    }
    bool f_ours = e && e->world == w && e->settlement.size() == w->grid->fine.pos.size();
    uint32_t version = f_ours ? e->zones_version : 0xFFFFFFFEu;
    if (w == border_built_world && version == border_built_zones){
        return;
    }
    border_built_world = w;
    border_built_zones = version;
    std::vector<vertex> verts;
    if (f_ours){
        const std::vector<vec2>& pos = w->grid->fine.pos;
        const Terrain& t = *w->terrain;
        for (const auto& edge : w->grid->fine.edges){
            int a = edge.first;
            int b = edge.second;
            if (e->settlement[a] == e->settlement[b] || !t.SameGround(a,b)){
                continue;
            }
            int inside = e->settlement[a] ? a : b;      //the first's side where two meet
            vec2 mid = (pos[a] + pos[b]) * 0.5f;
            float y0 = t.GroundHeight(mid,t.Height(inside)) - BORDER_FOOTING;
            float top = y0 + BORDER_FOOTING + BORDER_POST_HEIGHT;
            vec2 half(BORDER_POST_W * 0.5f,0.0f);
            BorderBeam(verts,mid - half,mid + half,y0,top - y0,BORDER_POST_W,PAL_BARK_DARK,PAL_TEMPERATE);
            vec2 in = pos[inside] - mid;
            float l = in.length();
            if (l > 1e-4f){
                vec2 dir = in * (1.0f / l);
                BorderBeam(verts,mid,mid + dir * BORDER_FLAG_LENGTH,top - BORDER_FLAG_H,BORDER_FLAG_H,BORDER_FLAG_THICK,
                           PAL_ROOF,PAL_TEMPERATE);     //terracotta: the colour that stands out on grass
            }
        }
    }
    if (verts.empty()){
        border_object->SetVisibility(false);
        return;
    }
    border_object->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
    UseGroundShader(border_object->GetMesh());      //after: SetMeshData resets the mode
    border_object->SetVisibility(true);
}

/*
    RENDER THREAD, from DrawOverlay in play mode (settlements_plan.md, the user's): with a person selected, how far
    it is to walk to the plot under the cursor - his straight walk where he would take one, else the grid's route
    (Walkers::PlanRoute, the view's own search) - beside the cursor, and whether it is outside his settlement. Worked
    out again only when he moves to another plot, the cursor to another, or the zones change. Not with a tool in hand.
*/
void ApplicationChasm::DrawWalkTip(float s){
    int mx = overlay_mouse_x.load();
    int my = overlay_mouse_y.load();
    PlayPick sel;
    bool f_over_panel = false;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
        //Over a card, the bar or a panel (last frame's): the cursor is on the overlay, not on the ground.
        for (const OverlayHit& h : overlay_hits){
            f_over_panel = f_over_panel || (mx >= h.a.x && mx <= h.b.x && my >= h.a.y && my <= h.b.y);
        }
    }
    //With a tool in hand a right click puts it down rather than sending him, and the price tag has the spot.
    if (sel.kind != PLAY_PICK_PERSON || mx < 0 || my < 0 || f_over_panel || paint_tool.load() != CHASM_TOOL_SELECT){
        return;
    }
    GridPick hover = GetHoverPick();
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!hover.f_hit || hover.plot < 0 || !w || !z || !e || z->world != w || e->world != w){
        return;
    }
    const EconomyWorker* k = NULL;
    for (const EconomyWorker& o : e->workers){
        k = (o.id == sel.id) ? &o : k;
    }
    GridPick here = k ? w->picker->Pick(k->pos) : GridPick();
    if (!k || !here.f_hit || here.plot < 0){
        return;
    }
    std::array<int,4> key = {{(int)k->id,here.plot,hover.plot,(int)z->version}};
    if (key != walk_tip_key){
        walk_tip_key = key;
        const std::vector<vec2>& pos = w->grid->fine.pos;
        int layer = EconomyWorkerLayer(*k);
        if (layer == WALK_GROUND && EconomyStraightClear(*w,*z,k->pos,pos[hover.plot],here.plot,hover.plot)){
            walk_tip_length = (pos[hover.plot] - k->pos).length();
        }else{
            if (walk_tip_walkers.GetWorld() != w){
                walk_tip_walkers.Reset(w);
            }
            WalkPlace from;
            from.plot = here.plot;
            from.layer = (uint8_t)layer;
            std::vector<WalkPlace> places;
            std::vector<float> speeds;
            walk_tip_length = -1.0f;
            if (walk_tip_walkers.PlanRoute(*z,from,hover.plot,EconomyPosted(*k) ? WALK_WHO_POSTED : WALK_WHO_VILLAGER,
                                           places,speeds)){
                float len = 0.0f;
                vec2 at = k->pos;
                for (const WalkPlace& p : places){
                    len += (pos[p.plot] - at).length();
                    at = pos[p.plot];
                }
                walk_tip_length = len;
            }
        }
    }
    std::string text;
    uint32_t colour = HUD_TEXT;
    if (walk_tip_length < 0.0f){
        text = "No way there";
        colour = HUD_AMBER;
    }else{
        char buf[48];
        snprintf(buf,sizeof(buf),"%.0f units to walk",walk_tip_length);
        text = buf;
        uint32_t there = (hover.plot < (int)e->settlement.size()) ? e->settlement[hover.plot] : 0;
        if (there != EconomyPersonSettlement(*e,*k)){
            text += there ? " - another settlement" : " - outside the settlement";
            colour = HUD_AMBER;
        }
    }
    const float size = 14.0f * s;
    const float side = 9.0f * s;
    vec2 m = overlay->MeasureText(text.c_str(),size);
    vec2 t0((float)mx + 18.0f * s,(float)my + 14.0f * s);
    vec2 t1(t0.x + m.x + side * 2.0f,t0.y + 24.0f * s);
    overlay->AddRect(t0,t1,7.0f * s,HudFaded(HUD_PANEL,225));
    overlay->AddText(text.c_str(),vec2(t0.x + side,t0.y + 17.0f * s),size,colour,UI_ALIGN_LEFT);
}
