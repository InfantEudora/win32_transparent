/*
    THE RAINBOW (docs/rainbow_plan.md) - light in the falls' spray. A box over each sheet's spray, one instance
    set posed every frame with the foam, drawn by assets/shaders/chasm_rainbow.frag: the colour from the view
    ray's angle to the point opposite the sun, the amount from a soft cloud in the box. So the bow moves with the
    camera and the sun as a real one does, and is seen with the sun at your back. View only: nothing here is read
    by a tick, saved or hashed.
*/
#include "ApplicationChasm.h"

#include "Debug.h"
#include "Primitives.h"
#include "glad.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <algorithm>
#include <cmath>

static Debugger* debug = new Debugger("ChasmRainbow",DEBUG_ALL);

//The spray's cloud round a sheet's foot, in world units - where Mist.cpp's SprayBall and FoamBall drift.
#define RAINBOW_ACROSS      1.8f    //the fall's widths across
#define RAINBOW_GAP         0.3f    //off the sheet's face, so the sheet hides it from behind
#define RAINBOW_OUT         9.0f    //and out from there
#define RAINBOW_BELOW       3.0f    //under the mist's top - its puffs cut it off
#define RAINBOW_ABOVE       20.0f   //and over it
//The look (chasm_rainbow.frag).
#define RAINBOW_SECONDARY   0.4f    //the secondary bow against the primary - a real one's is about 0.43
#define RAINBOW_INNER       0.12f   //the white glow inside the primary
#define RAINBOW_DENSITY     0.3f    //optical depth a world unit at the cloud's heart
#define RAINBOW_STEPS       12

//RENDER THREAD, from BuildWaterScene: after the water's programs, so the falls' sheets are in the depth buffer
//before the bow is tested against them.
void ApplicationChasm::BuildRainbowScene(){
    rainbow_shader = new Shader("shaders/default.vert","shaders/chasm_rainbow.frag");
    rainbow_shader->uniform_callback = std::bind(&ApplicationChasm::SetRainbowUniforms,this);
    rainbow_shader_index = renderer->AddCustomShader(rainbow_shader);

    //Its own box, never an asset's: the tag below would make every other user of a shared mesh a rainbow.
    Mesh* box = MakeBox(vec3(1.0f,1.0f,1.0f));
    if (!box){
        debug->Err("No box for the rainbow\n");
        return;
    }
    box->Retain();
    box->mesh_mode = MESH_MODE_SHADER;
    box->custom_shader_index = rainbow_shader_index;

    rainbow_set = new Object();
    rainbow_set->name = "Rainbow set";
    rainbow_set->SetMesh(box);
    //No material: the shader works out its own colour.
    rainbow_set->SetMaterialSlot(0,-1);
    rainbow_set->SetVisualOnly(true);
    rainbow_set->SetPickability(false);
    rainbow_set->SetCastsShadow(false);
    rainbow_set->SetInstances(std::vector<fmat4>());
    rainbow_set->SetVisibility(false);
    world_scene->AddObject(rainbow_set);
}

/*
    RENDER THREAD, every frame from UpdateMist: a box over each sheet's spray, as the foam is posed - none for a
    fall frozen at its lip, none with the water or the mist out of the view, none with no sun to make one.
*/
void ApplicationChasm::UpdateRainbow(){
    if (!rainbow_set){
        return;
    }
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::vector<fmat4> boxes;
    std::vector<vec4> seen;
    if (w && w->water && f_view_water && f_view_mist && sun && sun->brightness > 0.0f && rainbow_strength.load() > 0.0f){
        float front = SnowFrontNow();
        vec3 toward = sun_offset;
        toward.normalize();
        vec3 eye = world_scene->camera->GetPosition();
        const vec3 up(0.0f,1.0f,0.0f);
        for (const WaterSheetShape& s : w->water->shapes){
            if (CalendarSnowAt(s.origin,front)){
                continue;
            }
            //x across, y up, z out from the sheet's face. Across is up x out, so the box keeps its winding.
            const TerrainFall& f = s.foot;
            vec3 out(f.out.x,0.0f,f.out.y);
            vec3 across = up.cross(out);
            vec2 at = f.lip + f.out * (FOAM_SHEET_OUT + RAINBOW_GAP + RAINBOW_OUT * 0.5f);
            vec3 middle(at.x,CHASM_MIST_TOP + (RAINBOW_ABOVE - RAINBOW_BELOW) * 0.5f,at.y);
            fmat4 m;
            m.identity();
            m.vertex[0] = vec4(across * (f.width * RAINBOW_ACROSS),0.0f);
            m.vertex[1] = vec4(up * (RAINBOW_ABOVE + RAINBOW_BELOW),0.0f);
            m.vertex[2] = vec4(out * RAINBOW_OUT,0.0f);
            m.set_position(middle);
            boxes.push_back(m);
            vec3 ray = middle - eye;
            ray.normalize();
            float deg = acosf(std::max(-1.0f,std::min(1.0f,-ray.dot(toward)))) * 57.29578f;
            seen.push_back(vec4(middle,deg));
        }
    }
    {
        std::lock_guard<std::mutex> lock(rainbow_mutex);
        rainbow_seen = std::move(seen);
    }
    bool f_any = !boxes.empty();
    rainbow_set->SetInstances(std::move(boxes));
    rainbow_set->SetVisibility(f_any);
}

//RENDER THREAD, from the rainbow's sub-pass.
void ApplicationChasm::SetRainbowUniforms(){
    vec3 toward = sun_offset;
    toward.normalize();
    vec3 light = sun ? sun->color * (sun->brightness * rainbow_strength.load()) : vec3();
    rainbow_shader->Setvec3("rainbow_sun",toward);
    rainbow_shader->Setvec3("rainbow_light",light);
    rainbow_shader->Setfloat("rainbow_width",rainbow_width.load());
    rainbow_shader->Setfloat("rainbow_secondary",RAINBOW_SECONDARY);
    rainbow_shader->Setfloat("rainbow_inner",RAINBOW_INNER);
    rainbow_shader->Setfloat("rainbow_density",RAINBOW_DENSITY);
    rainbow_shader->Setint("rainbow_steps",RAINBOW_STEPS);
    rainbow_shader->Setint("rainbow_debug",rainbow_debug.load());
    //Light ADDED to the frame, its alpha left alone - Renderer::CustomShaderSubPass puts the blend function back
    //after. And no depth written: the snow drawn after it falls through the spray.
    glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);
    glDepthMask(GL_FALSE);
}

#ifdef USE_IMGUI
//RENDER THREAD with physics_mutex held, under the Time panel, between the sun's section and the weather's.
void ApplicationChasm::RenderRainbowPanel(){
    if (!ImGui::CollapsingHeader("Rainbow")){
        return;
    }
    int boxes = 0;
    float nearest = 180.0f;
    {
        std::lock_guard<std::mutex> lock(rainbow_mutex);
        boxes = (int)rainbow_seen.size();
        for (const vec4& b : rainbow_seen){
            nearest = std::min(nearest,std::fabs(b.w - 41.5f));
        }
    }
    if (boxes > 0){
        ImGui::Text("%i falls' spray, the nearest %.1f deg off the bow from here",boxes,nearest);
    }
    else{
        ImGui::TextDisabled("no spray to make one: no falls in this world, all frozen, or no sun");
    }
    float strength = rainbow_strength.load();
    if (ImGui::SliderFloat("strength##rainbow",&strength,0.0f,1.0f)){
        rainbow_strength = strength;
    }
    float width = rainbow_width.load();
    if (ImGui::SliderFloat("width##rainbow",&width,0.5f,4.0f,"x%.2f")){
        rainbow_width = width;
    }
    int view = rainbow_debug.load();
    ImGui::TextDisabled("show:");
    ImGui::SameLine();
    ImGui::RadioButton("the bow##rainbow",&view,0);
    ImGui::SameLine();
    ImGui::RadioButton("the spray##rainbow",&view,1);
    ImGui::SameLine();
    ImGui::RadioButton("all spray##rainbow",&view,2);
    rainbow_debug = view;
}
#endif

#ifdef USE_MCP
//chasm_time's `weather`, its `rainbow`: the knobs, and each box drawn with the view ray's angle at its middle.
json ApplicationChasm::RainbowStatusJson(){
    auto r = [](float value, float by){
        return std::round(value * by) / by;
    };
    json boxes = json::array();
    {
        std::lock_guard<std::mutex> lock(rainbow_mutex);
        for (const vec4& b : rainbow_seen){
            boxes.push_back(json{{"at",json::array({r(b.x,10.0f),r(b.y,10.0f),r(b.z,10.0f)})},{"angle",r(b.w,10.0f)}});
        }
    }
    return json{{"strength",r(rainbow_strength.load(),100.0f)},{"width",r(rainbow_width.load(),100.0f)},
                {"debug",rainbow_debug.load()},{"boxes",boxes}};
}

//chasm_time's `weather` {rainbow: {strength, width, debug}}, as the Rainbow panel sets them.
void ApplicationChasm::SteerRainbow(const json& s){
    if (!s.is_object()){
        return;
    }
    auto it = s.find("strength");
    if (it != s.end() && it->is_number()){
        rainbow_strength = std::max(0.0f,std::min(2.0f,it->get<float>()));
    }
    it = s.find("width");
    if (it != s.end() && it->is_number()){
        rainbow_width = std::max(0.25f,std::min(6.0f,it->get<float>()));
    }
    it = s.find("debug");
    if (it != s.end() && it->is_number_integer()){
        rainbow_debug = std::max(0,std::min(2,it->get<int>()));
    }
}
#endif
