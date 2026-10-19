/*
    THE MAP TOOLS (docs/water_tools_plan.md steps 4 and 5): rivers, ponds and height placed by hand on the debug panel.
    Each is part of the map's settings (GridWater.h), so placing one makes the map again - see the block in
    ApplicationChasm.h.
*/
#include "ApplicationChasm.h"

#include <cmath>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#define MAP_DRAG_STEP       3.0f    //a river's or a height stroke's drag takes a point each time the cursor has gone this far

/*
    PHYSICS THREAD, from UpdatePaint with a map tool in hand. A press starts the drag where it lands - a river's source,
    a stroke's first point, a pond's middle; held, a river or a stroke takes a point every MAP_DRAG_STEP and a pond's
    edge follows the cursor; let go, it is added (RemakeMap) - here, on the physics thread with the pass's lock held,
    as the N key makes a new map - or turned down, and the panel says why.
*/
void ApplicationChasm::UpdateMapDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held){
    const int tool = paint_tool;
    const bool f_line = tool == CHASM_TOOL_RIVER || tool == CHASM_TOOL_HEIGHT;
    const bool f_on = f_over_scene && hover.f_hit;
    std::vector<vec2> drag;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        drag = map_drag;
    }
    bool f_changed = false;
    if (f_clicked && f_on){
        drag.assign(f_line ? 1 : 2,hover.at);
        f_map_dragging = true;
        f_changed = true;
    }else if (f_held && f_map_dragging && f_on){
        if (f_line && (hover.at - drag.back()).length() >= MAP_DRAG_STEP){
            drag.push_back(hover.at);
            f_changed = true;
        }else if (!f_line && drag.size() == 2 && (hover.at - drag[1]).length() > 0.01f){
            drag[1] = hover.at;
            f_changed = true;
        }
    }else if (!f_held && f_map_dragging){
        std::shared_ptr<const Grid> g = GetGrid();
        GridWater water = g ? g->settings.water : GridWater();
        GridRelief relief = g ? g->settings.relief : GridRelief();
        std::string why;
        if (f_line && f_on && (hover.at - drag.back()).length() > 0.5f){
            drag.push_back(hover.at);
        }
        if (tool == CHASM_TOOL_RIVER){
            if (drag.size() >= 2){
                GridPlacedRiver r;
                r.points = drag;
                r.width = water_river_width.load();
                water.rivers.push_back(r);
            }else{
                why = "a river is dragged from its source on";
            }
        }else if (tool == CHASM_TOOL_HEIGHT){
            //A click is a hill, a drag a ridge; a stroke of no height would change nothing.
            GridReliefStroke s;
            s.points = drag;
            s.height = relief_height.load();
            s.radius = relief_radius.load();
            if (std::fabs(s.height) < 0.05f){
                why = "a stroke with no height changes nothing";
            }else{
                relief.strokes.push_back(s);
            }
        }else{
            float radius = std::min(GRID_PLACED_POND_LARGEST,(drag[1] - drag[0]).length());
            if (radius < GRID_PLACED_POND_SMALLEST){
                why = "a pond is dragged out from its middle to its water's edge";
            }else if (g && !ChasmPondFits(g->layout,drag[0],radius,g->bounds_min,g->bounds_max)){
                why = "a pond lies on the plateau, clear of the map's edge, a rim, a crack and the mountain";
            }else{
                GridPlacedPond k;
                k.centre = drag[0];
                k.radius = radius;
                water.ponds.push_back(k);
            }
        }
        if (why.empty() && RemakeMap(water,relief,why)){
            why = (tool == CHASM_TOOL_RIVER) ? "river placed: a new map" : (tool == CHASM_TOOL_HEIGHT) ? "stroke painted: a new map"
                                                                                                    : "pond placed: a new map";
        }
        drag.clear();
        f_map_dragging = false;
        f_changed = true;
        std::lock_guard<std::mutex> lock(pick_mutex);
        map_status = why;
    }
    if (f_changed){
        std::lock_guard<std::mutex> lock(pick_mutex);
        map_drag = drag;
        pick_version++;
    }
}

bool ApplicationChasm::RemakeMap(const GridWater& water, const GridRelief& relief, std::string& why){
    //A new map in the middle of a recording would leave it nothing to replay against.
    if (main_scene->inputcontroller->IsRecording()){
        why = "not while recording: it makes a new map";
        return false;
    }
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        why = "no map";
        return false;
    }
    GridSettings s = g->settings;
    s.water = water;
    s.relief = relief;
    RegenerateGrid(s);
    return true;
}

/*
    DEBUG PANEL, under the map tools: the river's width or the stroke's height and radius, and what this map has - for
    the river and pond tools its water (each river by how it came to end, each pond by where it lies), for the height
    tool its strokes - each with a button to take it away, which makes the map again.
*/
void ApplicationChasm::RenderMapTools(){
#ifdef USE_IMGUI
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        return;
    }
    const int tool = paint_tool;
    const GridWater& water = g->settings.water;
    const GridRelief& relief = g->settings.relief;
    GridWater water_after = water;
    GridRelief relief_after = relief;
    bool f_change = false;
    if (tool == CHASM_TOOL_HEIGHT){
        float height = relief_height.load();
        float radius = relief_radius.load();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("height##relief",&height,-GRID_RELIEF_HIGHEST,GRID_RELIEF_HIGHEST,"%.1f")){
            relief_height = height;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("radius##relief",&radius,GRID_RELIEF_NARROWEST,GRID_RELIEF_WIDEST,"%.0f")){
            relief_radius = radius;
        }
        ImGui::TextDisabled("click for a hill, drag for a ridge - below 0 a hollow or a valley; each makes the map again");
        for (size_t i = 0; i < relief.strokes.size(); i++){
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x##stroke") && !f_change){
                relief_after.strokes.erase(relief_after.strokes.begin() + i);
                f_change = true;
            }
            ImGui::SameLine();
            const GridReliefStroke& s = relief.strokes[i];
            ImGui::Text("stroke %zu: %s %+.1f, radius %.0f, from %.0f, %.0f",i + 1,s.points.size() == 1 ? "a hill" : "a ridge",
                        s.height,s.radius,s.points.front().x,s.points.front().y);
            ImGui::PopID();
        }
        if (!relief.Empty() && ImGui::Button("Undo the last stroke##relief") && !f_change){
            relief_after.strokes.pop_back();
            f_change = true;
        }
        if (!relief.Empty()){
            ImGui::SameLine();
        }
        if (!relief.Empty() && ImGui::Button("Take all the strokes away##relief") && !f_change){
            relief_after = GridRelief();
            f_change = true;
        }
    }else{
        float width = water_river_width.load();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("river width##water",&width,GRID_PLACED_RIVER_NARROWEST,GRID_PLACED_RIVER_WIDEST,"%.1f")){
            water_river_width = width;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("drag a river from its source, a pond out from its middle: each makes the map again");
        const ChasmLayout& l = g->layout;
        size_t placed_from = l.rivers.size() - std::min(l.rivers.size(),water.rivers.size());
        static const char* const ends[4] = {"falls over the rim","joins a river","sinks into a hole","runs off the edge"};
        for (size_t i = 0; i < water.rivers.size(); i++){
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x##river") && !f_change){
                water_after.rivers.erase(water_after.rivers.begin() + i);
                f_change = true;
            }
            ImGui::SameLine();
            const GridPlacedRiver& r = water.rivers[i];
            int end = (placed_from + i < l.rivers.size()) ? std::max(0,std::min(3,l.rivers[placed_from + i].end)) : 2;
            ImGui::Text("river %zu: %zu points from %.0f, %.0f, width %.1f - %s",i + 1,r.points.size(),r.points.front().x,
                        r.points.front().y,r.width,ends[end]);
            ImGui::PopID();
        }
        for (size_t i = 0; i < water.ponds.size(); i++){
            ImGui::PushID(1000 + (int)i);
            if (ImGui::SmallButton("x##pond") && !f_change){
                water_after.ponds.erase(water_after.ponds.begin() + i);
                f_change = true;
            }
            ImGui::SameLine();
            const GridPlacedPond& k = water.ponds[i];
            bool f_there = false;
            for (const ChasmLayout::Pond& p : l.ponds){
                f_there = f_there || (p.centre.x == k.centre.x && p.centre.y == k.centre.y);
            }
            ImGui::Text("pond %zu: at %.0f, %.0f, radius %.1f%s",i + 1,k.centre.x,k.centre.y,k.radius,
                        f_there ? "" : " - left out: not on the plateau");
            ImGui::PopID();
        }
        if (!water.Empty() && ImGui::Button("Take all the water away##water") && !f_change){
            water_after = GridWater();
            f_change = true;
        }
    }
    std::string status;
    if (f_change){
        bool f_done = RemakeMap(water_after,relief_after,status);
        std::lock_guard<std::mutex> lock(pick_mutex);
        map_status = f_done ? "taken away: a new map" : status;
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        status = map_status;
    }
    if (!status.empty()){
        ImGui::TextDisabled("%s",status.c_str());
    }
#endif
}
