#include "ApplicationChasm.h"
#include "Debug.h"
#include "File.h"
#include "Directory.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "ChasmStats.h"
#include "ChasmVersion.h"
#include "TitleTown.h"
#include "Window.h"
#include "Light.h"
#include "ObjectAnimation.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#ifdef USE_SOUND
#include "SoundSystem.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdio.h>
#include <thread>

/*
    THE TITLE (docs/demo_plan.md, the user's 2026-10-15): a scene of its own, as archer's is - its camera, its light
    and its menu - with the game left loaded under it, so Continue goes straight back and nothing is lost on the way.
    While it shows, the world is neither ticked nor drawn (core simulates and draws the active scene only), and
    Chasm's own entry points - UpdateView, RunSimulationTick, PreRender, DrawOverlay, the panels - leave the world
    alone (TitleShowing). That is what keeps every `main_scene` in the world's code meaning the world: that code only
    runs while the world is the active scene.

    THE POP-UP (the user's: every way out asks first): one question over everything, the game paused under it - the
    quit, leaving a colony for another, saving over a save; a death and a goal met later. UIMenu's buttons in a
    panel, Cancel focused and Escape its answer, so a key pressed out of habit never quits. The window's X and Alt+F4
    come here too (Window::f_close_asks_app). Leaving a colony - quitting, or loading or starting another - autosaves
    it first, so the question loses nothing either way and only asks whether to go.
*/

static Debugger* debug = new Debugger("ChasmTitle",DEBUG_ALL);

#define TITLE_MENU_TOP      0.45f       //the menu's first button, in window heights
#define TITLE_SHADE         UIColor(0,0,0,110)      //over the world while a question is up
#define TITLE_DIORAMA       "menu_town"             //chasm_diorama's file by default: assets/dioramas/<this>.scap
#define TITLE_TURN          0.035f      //the camera's turn round the diorama, radians a second: three minutes a round
#define TITLE_PITCH         0.50f       //...from this far above the horizon, about 29 degrees
#define TITLE_DISTANCE      1.30f       //...and this many of the diorama's radius out
#define ASK_BOX             UIColor(22,28,32,235)   //the question's panel

namespace {

enum{
    TITLE_ITEM_CONTINUE = 1,
    TITLE_ITEM_NEW,
    TITLE_ITEM_LOAD,
    TITLE_ITEM_REPLAY,
    TITLE_ITEM_SETTINGS,
    TITLE_ITEM_QUIT,
    ASK_ITEM_YES,
    ASK_ITEM_NO,
};

}

UIMenuStyle ApplicationChasm::MenuStyle(){
    UIMenuStyle style;
    style.panel = HUD_PANEL;
    style.panel_focus = UIColor(44,54,60,225);
    style.panel_pressed = UIColor(30,38,42,240);
    style.outline_focus = HUD_SELECT;
    style.text = HUD_TEXT;
    style.text_dim = HUD_TEXT_DIM;
    style.track = UIColor(255,255,255,40);
    style.fill = HUD_SELECT;
    return style;
}

//The build, as saves, recordings and bug reports name it (ChasmVersion.h): "0.0.1 (210d9e4+, 2026-10-16, debug)".
std::string ApplicationChasm::BuildName(){
    std::string name = std::string(ChasmVersion()) + " (" + ChasmGitHash() + ", " + ChasmBuildDate();
#ifdef DEBUG
    name += ", debug";
#endif
    return name + ")";
}

bool ApplicationChasm::TitleShowing(){
    return title_scene && main_scene == title_scene;
}

//INIT, after the world's scene is built: the title's scene and its menu. Nothing in it yet but the camera and the light.
void ApplicationChasm::CreateTitleScene(){
    //The window and the taskbar name the version too (ChasmVersion.h); app_name stays "Chasm", which recordings match.
    main_window->SetTitle(std::string("Chasm ") + ChasmVersion());
    title_scene = CreateNewScene("Title");
    Camera* camera = title_scene->camera;
    camera->name = "Title Camera";
    camera->SetupPerspective((float)renderer->width,(float)renderer->height,40.0f,0.5f,4000.0f);
    camera->SetPosition(vec3(0.0f,30.0f,40.0f));
    camera->SetLookAt(vec3(0.0f,0.0f,0.0f));
    //The world's sun, as BuildScene makes it: the same direction and light, so what stands here is lit alike.
    title_sun = new DirectionalLight();
    title_sun->name = "Title Sun";
    title_sun->color = sun->color;
    title_sun->brightness = sun->brightness;
    title_sun->SetupOrthographic(4096,4096,60.0f,1.0f,sun_offset.length() * 2.0f);
    title_sun->SetPosition(sun_offset);
    title_sun->SetLookAt(vec3());
    title_scene->AddObject(title_sun);

    title_menu.layout.top = TITLE_MENU_TOP;
    title_menu.layout.width = 0.34f;
    title_menu.layout.item_h = 0.058f;
    title_menu.AddButton("Continue",TITLE_ITEM_CONTINUE);
    title_menu.AddButton("New game",TITLE_ITEM_NEW);
    title_menu.AddButton("Load",TITLE_ITEM_LOAD);
    title_menu.AddButton("Replay a recording",TITLE_ITEM_REPLAY);
    title_menu.AddButton("Settings",TITLE_ITEM_SETTINGS);
    title_menu.AddButton("Quit",TITLE_ITEM_QUIT);
    RefreshTitleMenu();

    ask_menu.layout.top = 0.52f;
    ask_menu.layout.width = 0.34f;
    ask_menu.layout.item_h = 0.058f;

    //THE TITLE'S TOWN (test_maps_plan.md, step 6): made by code and captured at start (BuildTitleTown) - no file.
    std::string error = "none was made";
    if (title_capture.empty() || !LoadDioramaBytes(title_capture.data(),title_capture.size(),error)){
        debug->Warn("The title has no town (%s): its menu over the sky\n",error.c_str());
    }
    UpdateTitleView(0.0f);
}

/*
    INIT, before the game's map: THE TITLE'S TOWN (TitleTown.h) made and captured. Its map is generated as the world, its
    town put up by its kind's start (EnsureZonesWorld, Zones::PlaceTitleTown), the uploads a frame would run done once -
    the ground, the buildings and yards, the forest, the range - and the capture taken as chasm_diorama takes one, with
    the town's people as its markers. Init is the render thread before the physics thread exists, so all of it is this
    thread's; nothing is ticked, saved or recorded, and the game's world replaces this one straight after.
*/
#define TITLE_TOWN_DATE     ((uint64_t)(CALENDAR_DAYS_PER_SEASON + 5) * CALENDAR_DAY_TICKS)   //midsummer: trees in full leaf

void ApplicationChasm::BuildTitleTown(){
    auto t0 = std::chrono::steady_clock::now();
    GridSettings s;
    s.kind = GRID_KIND_TITLE;
    s.seed = TITLE_TOWN_SEED;
    s.target_fine_cells = TITLE_TOWN_CELLS;
    RegenerateGrid(s);
    EnsureZonesWorld();
    SetCalendar(TITLE_TOWN_DATE);
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !z || z->world != w){
        return;
    }
    UploadTerrain();
    UploadZones();
    UploadForest();
    UploadRange();
    BuildBirdTrees(*w);         //the trees' perches, for the markers (BirdDioramaMarkers)
    vec2 c = TitleTownCentre(*w);
    vec3 centre(c.x,ViewGroundHeight(*w,c,0.0f),c.y);
    SceneCaptureOptions o = DioramaOptions(centre,TITLE_TOWN_RADIUS);
    for (const TitleTownPerson& p : TitleTownPeople(*w,*z)){
        nlohmann::json d{{"figure",p.figure},{"forward",{p.forward.x,p.forward.y}},{"rate",1.0f},{"clip",p.clip},
                         {"phase",p.phase},{"load",p.load},{"archer",p.f_archer}};
        if (p.path.size() >= 2 && p.speed > 0.0f){
            nlohmann::json path = nlohmann::json::array();
            for (const vec3& q : p.path){
                path.push_back({q.x,q.y,q.z});
            }
            d["path"] = path;
            d["speed"] = p.speed;
        }
        o.markers.push_back(SceneCaptureMarker{"person",p.at,d.dump()});
    }
    SceneCaptureReport r;
    title_capture.clear();
    SceneCaptureWrite(*world_scene,*renderer,o,title_capture,&r);
    debug->Info("The title's town: %i objects, %i triangles, %i instances, %i markers, %zu bytes, in %.1f ms\n",
                r.objects,r.triangles,r.instances,r.markers,r.bytes,
                std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count());
}

SceneCaptureOptions ApplicationChasm::DioramaOptions(const vec3& centre, float radius){
    //The world as the player sees it: not the cursor's marks, the selection's outlines or the debug views.
    SceneCaptureOptions o;
    o.centre = centre;
    o.radius = radius;
    //Nor the people, their loads, bows and arrows, which the diorama brings back alive from its markers, the
    //range's arrows, frozen mid-flight, or the zombies.
    o.markers = DioramaMarkers(centre,radius);
    o.keep = [this](Object* obj){
        static const char* const helpers[] = {"Pick View","Grid View","Selected","Outline","outline","Ghost","Cursor",
                                              "Preview","Hover","Path","Worker ","Crowd","Range arrow","zombies"};
        if (obj == pick_view){
            return false;
        }
        for (const char* h : helpers){
            if (obj->name.find(h) != std::string::npos){
                return false;
            }
        }
        return true;
    };
    return o;
}

//RENDER THREAD (ServiceDiorama with the game showing): assets/dioramas/<name>.scap into the title's scene.
bool ApplicationChasm::LoadDiorama(const std::string& name, std::string& error){
    std::string asset = "dioramas/" + name + ".scap";
    size_t size = 0;
    uint8_t* data = LoadFile(asset.c_str(),&size);
    if (!data){
        error = "no " + asset;
        return false;
    }
    return LoadDioramaBytes(data,size,error);
}

//RENDER THREAD: a capture's bytes into the title's scene - the title's town (CreateTitleScene), or a file's.
bool ApplicationChasm::LoadDioramaBytes(const uint8_t* data, size_t size, std::string& error){
    std::vector<Object*> made;
    SceneCaptureInfo info;
    if (!SceneCaptureLoad(data,size,*title_scene,*renderer,&made,&info,error)){
        for (Object* o : made){
            o->Destroy();
        }
        title_scene->DeleteDestroyedObjects();
        return false;
    }
    diorama_objects = made;
    diorama = info;
    DioramaLife(info.markers);
    //The clouds round it: not captured but the world's own puffs, made from its ground (ApplicationChasmExplore.cpp).
    BuildDioramaClouds();
    //Its sun's shadow box round it, as FollowSun keeps the world's round the view - the whole of it, and no more.
    title_sun->viewport.zoom = std::max(20.0f,info.radius * 1.15f);
    title_sun->SetPosition(info.centre + sun_offset);
    title_sun->SetLookAt(info.centre);
    return true;
}

/*
    RENDER THREAD, from PreRender: chasm_diorama's request. A capture reads the game's scene, so it waits for the
    game to show and its meshes to be uploaded; a reload changes the title's objects, so it waits for the title
    not to show - one rule for both: the game showing. Written into the app's own assets/dioramas/, which a ship
    build bakes in.
*/
void ApplicationChasm::ServiceDiorama(){
    int request = 0;
    vec3 centre;
    float radius = 0.0f;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(diorama_mutex);
        request = diorama_request;
        if (!request){
            return;
        }
        diorama_request = 0;
        centre = diorama_ask_centre;
        radius = diorama_ask_radius;
        name = diorama_ask_name;
    }
    std::string result;
    char buf[512];
    if (TitleShowing()){
        result = "the title is showing: continue to the game first";
    }else if (request == 1){
        SceneCaptureOptions o = DioramaOptions(centre,radius);
        std::vector<uint8_t> bytes;
        SceneCaptureReport r;
        SceneCaptureWrite(*world_scene,*renderer,o,bytes,&r);
        std::string dir = GetExecutableDirectory() + "/../assets/dioramas";
        Directory::Create(dir);
        std::string path = dir + "/" + name + ".scap";
        FILE* f = fopen(path.c_str(),"wb");
        bool f_ok = f && fwrite(bytes.data(),1,bytes.size(),f) == bytes.size();
        if (f){
            fclose(f);
        }
        if (f_ok){
            snprintf(buf,sizeof(buf),"captured %i objects - %i triangles, %i instances, %i skipped - and %i markers, %zu bytes to %s",
                     r.objects,r.triangles,r.instances,r.skipped,r.markers,r.bytes,path.c_str());
        }else{
            snprintf(buf,sizeof(buf),"could not write %s",path.c_str());
        }
        result = buf;
    }else{
        for (Object* o : diorama_objects){
            o->Destroy();
        }
        diorama_objects.clear();
        diorama_people.clear();
        title_bird_objects.clear();     //destroyed with the rest: none left to pose if the load below fails
        title_scene->DeleteDestroyedObjects();
        std::string error;
        bool f_loaded = false;
        //The title's own town, made at start (BuildTitleTown) - or a file's capture.
        if (name == TITLE_TOWN_NAME){
            f_loaded = !title_capture.empty() && LoadDioramaBytes(title_capture.data(),title_capture.size(),error);
        }else{
            ReleaseFile(("dioramas/" + name + ".scap").c_str());
            f_loaded = LoadDiorama(name,error);
        }
        if (f_loaded){
            snprintf(buf,sizeof(buf),"loaded %s: %i objects round (%.1f, %.1f), radius %.1f",name.c_str(),
                     (int)diorama_objects.size(),diorama.centre.x,diorama.centre.z,diorama.radius);
            result = buf;
        }else{
            result = error;
        }
    }
    std::lock_guard<std::mutex> lock(diorama_mutex);
    diorama_result = result;
    diorama_done++;
}

/*
    RENDER THREAD, at a capture: what the diorama brings back alive, as MARKERS (core/SceneCapture.h) - a CHIMNEY for
    every home's smoke, warm or not, since the title smokes in any season (HearthFixtures without heat); a PERCH for
    each place a bird can sit in its standing trees (BirdDioramaMarkers); a PERSON for
    each one drawn now (UploadWorkers' figures), where he stands and faces, his clip and how far through it, what he
    carries, his bow - and a walker's way on, while it stays in the diorama, sampled on the ground.
*/
std::vector<SceneCaptureMarker> ApplicationChasm::DioramaMarkers(const vec3& centre, float radius){
    std::vector<SceneCaptureMarker> out;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !z || z->world != w){
        return out;
    }
    auto inside = [&](const vec3& p){
        float dx = p.x - centre.x;
        float dz = p.z - centre.z;
        return dx * dx + dz * dz <= radius * radius;
    };
    std::vector<vec3> tops;
    HearthFixtures(*w,*z,NULL,&tops,NULL);
    for (const vec3& t : tops){
        if (inside(t)){
            out.push_back(SceneCaptureMarker{"chimney",t,""});
        }
    }
    //A PERCH for each place a bird can sit in the trees, for the title's flock (ApplicationChasmBirds.cpp).
    BirdDioramaMarkers(centre,radius,out);
    if (!e || e->world != w){
        return out;
    }
    auto ground = [&](const vec2& p, float y){
        GridPick k = w->picker->Pick(p);
        return (k.f_hit && k.plot >= 0) ? w->terrain->GroundHeight(p,w->terrain->Height(k.plot)) : y;
    };
    for (size_t i = 0; i < worker_skins.size() && i < e->workers.size(); i++){
        for (int f = 0; f < PERSON_FIGURES; f++){
            WorkerSkin& ws = worker_skins[i][f];
            if (!ws.figure || !ws.figure->IsVisible()){
                continue;
            }
            vec3 at = ws.figure->GetPosition();
            if (!inside(at)){
                continue;
            }
            const fmat4& m = ws.figure->GetWorldTransformScaleMatrix();
            vec2 facing(m.vertex[2].x,m.vertex[2].z);
            float fl = facing.length();
            facing = (fl > 1e-6f) ? facing * (1.0f / fl) : vec2(0.0f,1.0f);
            nlohmann::json d{{"figure",f},{"forward",{facing.x,facing.y}},{"rate",ws.rate}};
            Animation* clip = ws.figure->current_animation;
            if (clip){
                d["clip"] = clip->name;
                d["phase"] = (clip->duration > 0.0f) ? clip->time_index / clip->duration : 0.0f;
            }
            int load = 0;
            for (int l = 1; l < WORKER_LOADS; l++){
                load = (ws.load[l] && ws.load[l]->IsVisible()) ? l : load;
            }
            d["load"] = load;
            d["archer"] = ws.bow && ws.bow->IsVisible();
            //Walking while his route has a leg left (UploadWorkers' rule): the rest of it, a point every WALK_SAMPLE.
            const EconomyWorker& k = e->workers[i];
            if (k.seg + 1 < (int)k.route.size() && k.seg >= 0 && k.seg < (int)k.speed.size()){
                const float WALK_SAMPLE = 0.75f;
                nlohmann::json path = nlohmann::json::array();
                path.push_back({at.x,at.y,at.z});
                vec2 from(at.x,at.z);
                float y = at.y;
                bool f_out = false;
                for (size_t j = (size_t)k.seg + 1; j < k.route.size() && !f_out; j++){
                    vec2 to = k.route[j];
                    float len = (to - from).length();
                    int steps = std::max(1,(int)std::ceil(len / WALK_SAMPLE));
                    for (int s = 1; s <= steps; s++){
                        vec2 p = from + (to - from) * ((float)s / (float)steps);
                        y = ground(p,y);
                        if (!inside(vec3(p.x,y,p.y))){
                            f_out = true;
                            break;
                        }
                        path.push_back({p.x,y,p.y});
                    }
                    from = to;
                }
                d["speed"] = k.speed[k.seg];
                d["path"] = path;
            }
            out.push_back(SceneCaptureMarker{"person",at,d.dump()});
        }
    }
    return out;
}

namespace {

//A number from a JSON value, or `or_else` - with JSON_NOEXCEPTION a wrong type would abort the app.
float JsonFloat(const nlohmann::json& j, float or_else){
    return j.is_number() ? j.get<float>() : or_else;
}

}

/*
    RENDER THREAD, from LoadDiorama: its markers made alive - one set of smoke over every chimney (SetSmoke, as the
    hearths'), and each person a figure as the game builds him (BuildPersonSkin), placed, carrying what he carried,
    his bow if he had one, and in his clip where he was in it. Walkers go on walking in UpdateTitleView.
*/
void ApplicationChasm::DioramaLife(const std::vector<SceneCaptureMarker>& markers){
    diorama_people.clear();
    std::vector<vec3> tops;
    size_t people = 0;
    for (const SceneCaptureMarker& m : markers){
        if (m.kind == "chimney"){
            tops.push_back(m.at);
        }else if (m.kind == "person"){
            people++;
        }
    }
    if (!tops.empty() && hearth_puff_mesh && smoke_material >= 0){
        Object* smoke = new Object();
        smoke->name = "Diorama smoke";
        smoke->SetMesh(hearth_puff_mesh);
        smoke->SetMaterialSlot(0,smoke_material);
        smoke->SetVisualOnly(true);
        smoke->SetPickability(false);
        smoke->SetCastsShadow(false);
        SetSmoke(smoke,tops);
        title_scene->AddObject(smoke);
        diorama_objects.push_back(smoke);
    }
    diorama_people.reserve(people);
    for (const SceneCaptureMarker& m : markers){
        if (m.kind != "person"){
            continue;
        }
        nlohmann::json d = nlohmann::json::parse(m.data,nullptr,false);
        if (d.is_discarded() || !d.is_object()){
            continue;
        }
        int f = (int)JsonFloat(d.value("figure",nlohmann::json()),-1.0f);
        if (f < 0 || f >= PERSON_FIGURES || !worker_skin_mesh[f]){
            continue;
        }
        DioramaPerson p;
        p.figure = f;
        Object* s = BuildPersonSkin(p.ws,"Diorama person " + std::to_string(diorama_people.size()),f);
        if (!s){
            continue;
        }
        p.ws.figure = s;
        const nlohmann::json fw = d.value("forward",nlohmann::json());
        if (fw.is_array() && fw.size() == 2){
            vec3 v(JsonFloat(fw[0],0.0f),0.0f,JsonFloat(fw[1],1.0f));
            float l = v.length();
            p.forward = (l > 1e-6f) ? v * (1.0f / l) : vec3(0.0f,0.0f,1.0f);
        }
        p.f_archer = d.value("archer",nlohmann::json()).is_boolean() && d["archer"].get<bool>();
        int load = (int)JsonFloat(d.value("load",nlohmann::json()),0.0f);
        for (int l = 1; l < WORKER_LOADS; l++){
            if (p.ws.load[l]){
                p.ws.load[l]->SetVisibility(l == load);
            }
        }
        for (Object* kit : {p.ws.bow,p.ws.quiver}){
            if (kit){
                kit->SetVisibility(p.f_archer);
            }
        }
        const nlohmann::json path = d.value("path",nlohmann::json());
        for (size_t i = 0; path.is_array() && i < path.size(); i++){
            const nlohmann::json& pt = path[i];
            if (pt.is_array() && pt.size() == 3){
                p.path.push_back(vec3(JsonFloat(pt[0],0.0f),JsonFloat(pt[1],0.0f),JsonFloat(pt[2],0.0f)));
            }
        }
        p.speed = JsonFloat(d.value("speed",nlohmann::json()),0.0f);
        if (p.path.size() < 2 || p.speed <= 0.0f){
            p.path.clear();
        }
        s->SetPosition(m.at,false);
        s->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(p.forward.x,p.forward.z)),false);
        s->SetVisibility(true);
        const nlohmann::json clip_name = d.value("clip",nlohmann::json());
        Animation* clip = clip_name.is_string() ? s->FindAnimation(clip_name.get<std::string>()) : NULL;
        s->SwitchToAnimation(clip);
        if (clip){
            clip->time_index = JsonFloat(d.value("phase",nlohmann::json()),0.0f) * clip->duration;
        }
        s->SetAnimationRate(JsonFloat(d.value("rate",nlohmann::json()),1.0f));
        title_scene->AddObject(s);
        diorama_objects.push_back(s);
        if (p.ws.bow){
            title_scene->AddObject(p.ws.bow);
            diorama_objects.push_back(p.ws.bow);
        }
        diorama_people.push_back(p);
    }
    debug->Info("Diorama: smoke over %zu chimneys, %zu people\n",tops.size(),diorama_people.size());
    //And a flock over it, in its trees if the capture marked their perches (ApplicationChasmBirds.cpp).
    BuildTitleBirds(markers);
}

/*
    PHYSICS THREAD, from UpdateTitle: the camera turning slowly round the diorama - or round nothing, with none - and
    its people: a walker there and back along his way, an archer's bow in his hand.
*/
void ApplicationChasm::UpdateTitleView(float dt){
    title_yaw += TITLE_TURN * dt;
    float radius = (diorama.radius > 0.0f) ? diorama.radius : 30.0f;
    vec3 centre = diorama.centre;
    float d = radius * TITLE_DISTANCE;
    float pitch = TITLE_PITCH;
    {
        std::lock_guard<std::mutex> lock(menu_inject_mutex);
        if (f_title_view){
            centre = title_view_centre;
            d = title_view_distance;
            pitch = title_view_pitch;
        }
    }
    vec3 offset(sinf(title_yaw) * cosf(pitch),sinf(pitch),cosf(title_yaw) * cosf(pitch));
    title_scene->camera->SetPosition(centre + offset * d);
    title_scene->camera->SetLookAt(centre);
    for (DioramaPerson& p : diorama_people){
        Object* s = p.ws.figure;
        if (!s){
            continue;
        }
        if (!p.path.empty() && dt > 0.0f){
            float total = 0.0f;
            for (size_t i = 0; i + 1 < p.path.size(); i++){
                total += (p.path[i + 1] - p.path[i]).length();
            }
            p.along += (float)p.dir * p.speed * dt;
            if (p.along >= total){
                p.along = std::max(0.0f,2.0f * total - p.along);
                p.dir = -1;
            }else if (p.along <= 0.0f){
                p.along = std::min(total,-p.along);
                p.dir = 1;
            }
            float at = p.along;
            for (size_t i = 0; i + 1 < p.path.size(); i++){
                vec3 a = p.path[i];
                vec3 b = p.path[i + 1];
                float len = (b - a).length();
                if (at <= len || i + 2 == p.path.size()){
                    float t = (len > 1e-6f) ? std::min(1.0f,at / len) : 0.0f;
                    s->SetPosition(a + (b - a) * t,false);
                    vec3 way = (b - a) * (float)p.dir;
                    way.y = 0.0f;
                    if (way.length() > 1e-6f){
                        p.forward = way * (1.0f / way.length());
                        s->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(p.forward.x,p.forward.z)),false);
                    }
                    break;
                }
                at -= len;
            }
        }
        if (p.f_archer && p.ws.bow){
            PlaceBow(p.ws,p.figure,p.forward,NULL);
        }
    }
}

void ApplicationChasm::OpenTitle(){
    if (!title_scene || TitleShowing()){
        return;
    }
    menu_screen = CHASM_SCREEN_MAIN;
    RefreshTitleMenu();
    title_menu.SetFocus(0);
    RequestActiveScene(title_scene);
    debug->Info("Title screen\n");
}

void ApplicationChasm::CloseTitle(){
    if (!world_scene || !TitleShowing()){
        return;
    }
    menu_screen = CHASM_SCREEN_MAIN;
    RequestActiveScene(world_scene);
    //The switch to the title saved the recording that was running; the colony is recorded again from here.
    if (f_game_on.load()){
        RecordFromHere();
    }
    debug->Info("Title screen closed: back to the game\n");
}

//Continue: back to the colony in play, or the newest save with none - and greyed out with neither.
void ApplicationChasm::RefreshTitleMenu(){
    title_menu.SetEnabled(0,f_game_on.load() || !ChasmSaveList().empty());
    if (!title_menu.Item(title_menu.Focus()).f_enabled){
        title_menu.SetFocus(1);
    }
}

/*
    One pass of the menus' input from Chasm's keys and the mouse - or what chasm_menu injected. Up and down are the
    pan keys (arrows, WS), Enter confirms, Escape is `f_back`. False with nothing live to read. PHYSICS THREAD.
*/
bool ApplicationChasm::ReadMenuInput(InputController* input, UIMenuInput& in, bool& f_back){
    //The menus' size (ChasmHud.h, HudMenuScale), so a click lands where DrawOverlay drew them at this window's size.
    float scale = HudMenuScale((float)main_window->height,settings.GetFloat("ui_size"));
    for (UIMenu* m : {&title_menu,&ask_menu,&pause_menu,&screen_menu,&end_menu}){
        m->layout.scale = scale;
    }
    bool f_escape = input->WasKeyPressed(INPUT_CHASM_MENU);
    bool f_up = input->WasKeyPressed(INPUT_MOVE_UP) || input->WasKeyPressed(INPUT_TURN_UP);
    bool f_down = input->WasKeyPressed(INPUT_MOVE_DOWN) || input->WasKeyPressed(INPUT_TURN_DOWN);
    bool f_left = input->WasKeyPressed(INPUT_MOVE_LEFT) || input->WasKeyPressed(INPUT_TURN_LEFT);
    bool f_right = input->WasKeyPressed(INPUT_MOVE_RIGHT) || input->WasKeyPressed(INPUT_TURN_RIGHT);
    bool f_confirm = input->WasKeyPressed(INPUT_CHASM_CONFIRM);
    bool f_press = input->WasKeyPressed(INPUT_CLICK_LEFT);
    bool f_release = input->WasKeyReleased(INPUT_CLICK_LEFT);
    //The mouse's movement and wheel, spent here: the camera does not read them while a menu is up, and left to pile
    //up they would all land on its first pass after.
    input->GetDelta(INPUT_MOUSE_DELTA_X);
    input->GetDelta(INPUT_MOUSE_DELTA_Y);
    input->GetDelta(INPUT_MOUSE_WHEEL);
    //A click or a back from chasm_menu: pressed and released in this one pass.
    bool f_injected = false;
    bool f_injected_back = false;
    vec2 at;
    {
        std::lock_guard<std::mutex> lock(menu_inject_mutex);
        if (f_menu_click_pending){
            f_injected = true;
            at = menu_click_at;
            f_menu_click_pending = false;
        }
        f_injected_back = f_menu_back_pending;
        f_menu_back_pending = false;
    }
    bool f_live = KeysLive();
    in.screen_w = (float)main_window->width;
    in.screen_h = (float)main_window->height;
    in.up = f_live && f_up;
    in.down = f_live && f_down;
    in.left = f_live && f_left;
    in.right = f_live && f_right;
    in.confirm = f_live && f_confirm;
    f_back = (f_live && f_escape) || f_injected_back;
    if (f_injected){
        in.f_pointer = true;
        in.pointer_x = at.x;
        in.pointer_y = at.y;
        in.pointer_pressed = in.pointer_released = true;
    }else if (main_window->f_has_focus && !UIWantsMouse()){
        //The pointer only with focus: a cursor resting over another program is not ours to read. A release beside
        //the window counts as one too (release_edge_ignores_focus), so it is held to the same.
        int2 p = input->GetRelativeMousePosition();
        in.f_pointer = true;
        in.pointer_x = (float)p.x;
        in.pointer_y = (float)p.y;
        in.pointer_pressed = f_press;
        in.pointer_released = f_release;
        in.pointer_down = input->IsKeyDown(INPUT_CLICK_LEFT);
    }
    return true;
}

//PHYSICS THREAD, from UpdateView while the title shows: its question first, a load waiting, a list, then its menu.
void ApplicationChasm::UpdateTitle(InputController* input){
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    UpdateTitleView(std::min(0.1f,std::chrono::duration<float>(now - last).count()));
    last = now;
    if (main_window->f_close_asked.exchange(false)){
        OpenAsk(CHASM_ASK_QUIT);
    }
    if (UpdateAsk(input) || RunBusy() || UpdateScreen(input)){
        return;
    }
    UIMenuInput in;
    bool f_back = false;
    if (!ReadMenuInput(input,in,f_back)){
        return;
    }
    if (f_back){
        OpenAsk(CHASM_ASK_QUIT);
        return;
    }
    UIMenuResult r = title_menu.Update(in);
    if (r.event != UI_MENU_ACTIVATED){
        return;
    }
    switch (r.id){
        case TITLE_ITEM_CONTINUE:
            if (f_game_on.load()){
                CloseTitle();
            }else{
                //No colony yet: the newest save of every kind - of a colony, not a test map (test_maps_plan.md).
                for (const ChasmSaveEntry& e : ChasmSaveEntries()){
                    if (!e.f_test_map){
                        MenuAction a;
                        a.kind = CHASM_ACT_LOAD;
                        a.name = e.name;
                        Busy(a);
                        break;
                    }
                }
            }
            break;
        case TITLE_ITEM_NEW:        OpenScreen(CHASM_SCREEN_NEW); break;
        case TITLE_ITEM_LOAD:       OpenScreen(CHASM_SCREEN_LOAD); break;
        case TITLE_ITEM_REPLAY:     OpenScreen(CHASM_SCREEN_REPLAY); break;
        case TITLE_ITEM_SETTINGS:   OpenScreen(CHASM_SCREEN_SETTINGS); break;
        case TITLE_ITEM_QUIT:       OpenAsk(CHASM_ASK_QUIT); break;
    }
}

/*
    The question asked: its line, the one under it, its buttons with Cancel focused - and over the game, the game
    paused while it is up, as it was before afterwards. What a yes does is ask_action's (OVERWRITE, LEAVE). PHYSICS
    THREAD.
*/
void ApplicationChasm::OpenAsk(int kind, const std::string& text, const std::string& detail){
    if (ask_kind.load() != CHASM_ASK_NONE){
        return;
    }
    ask_menu.Clear();
    std::string line = text;
    std::string under = detail;
    switch (kind){
        case CHASM_ASK_QUIT:
            line = "Quit Chasm?";
            under = (f_game_on.load() && !ColonyLost()) ? "The colony is saved first, as the autosave." : "";
            ask_menu.AddButton("Quit",ASK_ITEM_YES);
            ask_menu.AddButton("Cancel",ASK_ITEM_NO);
            ask_menu.SetFocus(1);
            break;
        case CHASM_ASK_LEAVE:
            line = "Leave this colony?";
            under = "It is saved first, as the autosave.";
            ask_menu.AddButton("Leave",ASK_ITEM_YES);
            ask_menu.AddButton("Cancel",ASK_ITEM_NO);
            ask_menu.SetFocus(1);
            break;
        case CHASM_ASK_OVERWRITE:
            line = "Save over this one?";
            ask_menu.AddButton("Save over it",ASK_ITEM_YES);
            ask_menu.AddButton("Cancel",ASK_ITEM_NO);
            ask_menu.SetFocus(1);
            break;
        case CHASM_ASK_NOTE:
            ask_menu.AddButton("OK",ASK_ITEM_NO);
            break;
        case CHASM_ASK_REPORT:
            ask_menu.AddButton("Open the folder",ASK_ITEM_YES);
            ask_menu.AddButton("OK",ASK_ITEM_NO);
            ask_menu.SetFocus(1);
            break;
        case CHASM_ASK_DEATH:
            //A death (demo_plan.md step 7, ApplicationChasmGoals.cpp): who and of what, in `detail`. Continue plays on;
            //Main menu loses nothing, as the pause menu's doesn't.
            ask_menu.AddButton("Continue",ASK_ITEM_NO);
            ask_menu.AddButton("Main menu",ASK_ITEM_YES);
            ask_menu.SetFocus(0);
            break;
        default:
            return;
    }
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        ask_text = line;
        ask_detail = under;
    }
    f_ask_paused_game = false;
    if (!TitleShowing() && world_scene && !world_scene->IsPhysicsPaused()){
        world_scene->PausePhysics(true);
        f_ask_paused_game = true;
    }
    ask_kind = kind;
}

void ApplicationChasm::CloseAsk(){
    if (f_ask_paused_game && world_scene){
        world_scene->PausePhysics(false);
    }
    f_ask_paused_game = false;
    ask_kind = CHASM_ASK_NONE;
}

//PHYSICS THREAD: the question's input, if one is up - true then, and nothing under it is to move this pass.
bool ApplicationChasm::UpdateAsk(InputController* input){
    int kind = ask_kind.load();
    if (kind == CHASM_ASK_NONE){
        return false;
    }
    UIMenuInput in;
    bool f_back = false;
    if (!ReadMenuInput(input,in,f_back)){
        return true;
    }
    UIMenuResult r = ask_menu.Update(in);
    if (f_back || r.event == UI_MENU_BACK || (r.event == UI_MENU_ACTIVATED && r.id == ASK_ITEM_NO)){
        ask_action = MenuAction();
        CloseAsk();
        return true;
    }
    if (r.event == UI_MENU_ACTIVATED && r.id == ASK_ITEM_YES){
        MenuAction a = ask_action;
        ask_action = MenuAction();
        std::string detail;
        {
            std::lock_guard<std::mutex> lock(menu_text_mutex);
            detail = ask_detail;
        }
        CloseAsk();
        if (kind == CHASM_ASK_QUIT){
            debug->Info("Quit, asked and answered\n");
            Quit();
        }else if (kind == CHASM_ASK_LEAVE){
            //The colony left kept, in a slot other than the save about to be loaded - and its recording saved, not
            //started afresh.
            Autosave(a.kind == CHASM_ACT_LOAD ? a.name : "",false);
            Busy(a);
        }else if (kind == CHASM_ASK_REPORT){
            //The report's folder in the file browser, ready to zip.
            OpenInFileBrowser(detail);
        }else if (kind == CHASM_ASK_OVERWRITE){
            std::string error;
            ShowNote(WriteSave(a.name,"save",error) ? "Saved: " + SaveTitle() : error);
            menu_screen = CHASM_SCREEN_MAIN;
        }else if (kind == CHASM_ASK_DEATH){
            OpenTitle();
        }
    }
    return true;
}

/*
    ESCAPE in the game (demo_plan.md): one step back at a time - the tool in hand, then the selection, and with
    neither, the pause menu. PHYSICS THREAD, from UpdateView.
*/
void ApplicationChasm::EscapeStep(){
    if (paint_tool.load() != CHASM_TOOL_SELECT){
        paint_tool = CHASM_TOOL_SELECT;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        if (play_selected.kind != PLAY_PICK_NONE){
            play_selected = PlayPick();
            play_group.clear();
            play_pick_version++;
            return;
        }
    }
    OpenPause();
}

//RENDER THREAD, from DrawOverlay while the title shows: the name and the menu, or a list it opened; the build.
void ApplicationChasm::DrawTitle(float s){
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    if (menu_screen.load() == CHASM_SCREEN_MAIN){
        overlay->AddText("CHASM",vec2(w * 0.5f,h * 0.30f),78.0f * s,HUD_TEXT,UI_ALIGN_CENTER);
        std::string version = std::string("VERSION ") + ChasmVersion();
        overlay->AddText(version.c_str(),vec2(w * 0.5f,h * 0.30f + 36.0f * s),18.0f * s,HUD_TEXT,UI_ALIGN_CENTER);
        UIMenuDraw(overlay,title_menu,w,h,MenuStyle());
    }else{
        DrawScreen(s);
    }
    DrawHudBuild(overlay,w,h,s);
}

//RENDER THREAD, from DrawOverlay over the title or the game: the question, if one is up, over everything.
void ApplicationChasm::DrawAsk(float s){
    int kind = ask_kind.load();
    if (kind == CHASM_ASK_NONE){
        return;
    }
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    overlay->AddRect(vec2(0.0f,0.0f),vec2(w,h),0.0f,TITLE_SHADE);
    std::string text;
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(menu_text_mutex);
        text = ask_text;
        detail = ask_detail;
    }
    DrawMenuPanel(ask_menu,text,detail,s,ASK_BOX,true);
}

/*
    RENDER THREAD: a menu in a box, its heading over the buttons and a dimmer line under that - the question's, the
    pause menu's, a list's. `f_lighter_buttons` for a box as dark as the buttons, which would vanish into it.
*/
void ApplicationChasm::DrawMenuPanel(const UIMenu& menu, const std::string& heading, const std::string& detail, float s,
                                     uint32_t box, bool f_lighter_buttons){
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    int count = menu.Count();
    if (count <= 0){
        return;
    }
    UIMenuRect first = menu.ItemRect(0,w,h);
    UIMenuRect last = menu.ItemRect(count - 1,w,h);
    const float pad = 22.0f * s;
    //The line under the heading may be several: a death's names, one a line.
    std::vector<std::string> lines;
    for (size_t at = 0; !detail.empty() && at <= detail.size();){
        size_t end = detail.find('\n',at);
        end = (end == std::string::npos) ? detail.size() : end;
        lines.push_back(detail.substr(at,end - at));
        at = end + 1;
    }
    const float line_h = 22.0f * s;
    const float title_h = 54.0f * s + (lines.empty() ? 0.0f : 26.0f * s + line_h * (float)(lines.size() - 1));
    //As wide as the buttons, or as the words over them where those are wider.
    float half = (first.x1 - first.x0) * 0.5f;
    half = std::max(half,overlay->MeasureText(heading.c_str(),24.0f * s).x * 0.5f);
    for (const std::string& line : lines){
        half = std::max(half,overlay->MeasureText(line.c_str(),16.0f * s).x * 0.5f);
    }
    float mid = (first.x0 + first.x1) * 0.5f;
    float top = first.y0 - title_h - pad;
    overlay->AddRect(vec2(mid - half - pad,top),vec2(mid + half + pad,last.y1 + pad),12.0f * s,box);
    overlay->AddText(heading.c_str(),vec2(w * 0.5f,top + pad + 24.0f * s),24.0f * s,HUD_TEXT,UI_ALIGN_CENTER);
    for (size_t i = 0; i < lines.size(); i++){
        overlay->AddText(lines[i].c_str(),vec2(w * 0.5f,top + pad + 52.0f * s + line_h * (float)i),16.0f * s,HUD_TEXT_DIM,
                         UI_ALIGN_CENTER);
    }
    UIMenuStyle style = MenuStyle();
    if (f_lighter_buttons){
        style.panel = UIColor(54,64,70,255);
    }
    UIMenuDraw(overlay,menu,w,h,style);
}

#ifdef USE_IMGUI
//RENDER THREAD, the debug panels while the title shows: the world's are left alone, as everything of it is.
void ApplicationChasm::RenderTitlePanel(){
    ImGui::SetNextWindowPos(ImVec2((float)std::max(main_window->width,800) - 470.0f,30.0f),ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Chasm")){
        ImGui::TextUnformatted("The title is showing; the game waits under it.");
        if (ImGui::Button("Back to the game")){
            CloseTitle();
        }
    }
    ImGui::End();
}
#endif

#ifdef USE_MCP
void ApplicationChasm::RegisterTitleTools(){
    MCPServer::Get()->RegisterTool("chasm_menu",
        "THE MENUS AND THE POP-UP (docs/demo_plan.md). op state (default): which screen shows - title, game or "
        "pause (the pause menu over the game) - the list open (main, new, load, save), the question up (none, quit, "
        "leave, overwrite, note) with its text, `busy` while a load or a new map waits, the last note, whether a "
        "colony is in play and its name, and the menu in front with its items' labels, focus and window rects. op "
        "open: the title, over the game. op pause: the pause menu (as Escape with nothing in hand). op continue: back "
        "to the game. op click: a click at window pixel x,y, or on item `item` of the menu in front - the "
        "question's, else the list's, else the pause menu's or the title's. op back: Escape's back in a menu (a "
        "question's Cancel, a list's Back, the pause menu's Resume; on the title, the quit question). op close: as "
        "the window's X or Alt+F4 - the quit question. op view: the title camera by hand, still turning - round x,z "
        "(default the diorama's middle) at `distance` and `pitch` degrees; with none of those, back to its own. "
        "Clicks are menu input, not recorded commands; but Load, New game and Save do what they say - files are "
        "written and read in saves/ - and Quit really quits. THE DEMO (demo_plan.md step 7): question `death` is a "
        "death's pop-up (Continue, Main menu); `end_page` complete or lost is the end's statistics page, its buttons "
        "clicked by item as any menu's (Keep playing, Main menu; or Load, New game, Main menu); `goals` how each "
        "goal stands, as the panel shows it, with demo_complete and colony_lost once told. Both pop up in play mode only.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"distance",{{"type","number"}}},
                {"pitch",{{"type","number"}}},
                {"x",{{"type","number"}}},
                {"y",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"item",{{"type","integer"}}},
                {"triggers",{{"type","object"},{"description","debug builds, with any op: what play mode tells and stops for, "
                                                             "switched - winter, chasm, archer, swamp and wave (the goals' rows and "
                                                             "notices), death (its pop-up), "
                                                             "colony_lost and demo_complete (their pages), true on and false off. "
                                                             "The viewer's: never recorded or saved; the rules journal every goal "
                                                             "and end either way. `triggers` in the reply says how each stands"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string op = args.value("op",std::string("state"));
#ifdef DEBUG
            //THE SWITCHES (ApplicationChasmGoals.cpp), set before the op: {"death":false,"colony_lost":false}.
            if (args.contains("triggers") && args["triggers"].is_object()){
                for (auto it = args["triggers"].begin(); it != args["triggers"].end(); ++it){
                    int found = -1;
                    for (int t = 0; t < CHASM_TRIGGERS; t++){
                        found = (it.key() == TriggerName(t)) ? t : found;
                    }
                    if (found < 0 || !it.value().is_boolean()){
                        return json{{"error","triggers: winter, chasm, archer, swamp, wave, death, colony_lost or demo_complete, "
                                             "each true or false"}};
                    }
                    SetTrigger(found,it.value().get<bool>());
                }
            }
#endif
            const float w = (float)main_window->width;
            const float h = (float)main_window->height;
            auto front = [this]() -> UIMenu& {
                if (ask_kind.load() != CHASM_ASK_NONE){
                    return ask_menu;
                }
                if (f_stats_open.load() && stats_end.load() != CHASM_END_NONE){
                    return end_menu;
                }
                if (menu_screen.load() != CHASM_SCREEN_MAIN){
                    return screen_menu;
                }
                return (!TitleShowing() && f_pause_open.load()) ? pause_menu : title_menu;
            };
            auto wait_passes = [](){
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            };
            if (op == "open"){
                OpenTitle();
                wait_passes();
            }else if (op == "pause"){
                pause_request = 1;
                wait_passes();
            }else if (op == "continue"){
                CloseTitle();
                wait_passes();
            }else if (op == "click"){
                vec2 at(args.value("x",0.0f),args.value("y",0.0f));
                if (args.contains("item")){
                    //The end's buttons stand in a row of their own (EndButtonAt), not where a menu's column would.
                    bool f_end = ask_kind.load() == CHASM_ASK_NONE && f_stats_open.load() && stats_end.load() != CHASM_END_NONE;
                    UIMenuRect r = f_end ? EndButtonAt(args["item"].get<int>()) : front().ItemRect(args["item"].get<int>(),w,h);
                    at = vec2((r.x0 + r.x1) * 0.5f,(r.y0 + r.y1) * 0.5f);
                }
                {
                    std::lock_guard<std::mutex> lock(menu_inject_mutex);
                    menu_click_at = at;
                    f_menu_click_pending = true;
                }
                wait_passes();
            }else if (op == "back"){
                {
                    std::lock_guard<std::mutex> lock(menu_inject_mutex);
                    f_menu_back_pending = true;
                }
                wait_passes();
            }else if (op == "close"){
                main_window->f_close_asked = true;
                wait_passes();
            }else if (op == "view"){
                //The title camera by hand: round x,z (default the diorama's middle) at `distance` and `pitch` degrees,
                //still turning; no arguments puts it back.
                std::lock_guard<std::mutex> lock(menu_inject_mutex);
                f_title_view = args.contains("distance") || args.contains("x") || args.contains("pitch");
                vec2 at(args.value("x",diorama.centre.x),args.value("z",diorama.centre.z));
                std::shared_ptr<const ChasmWorld> w = GetWorld();
                float y = (w && w->terrain && w->picker) ? ViewGroundHeight(*w,at,diorama.centre.y) : diorama.centre.y;
                title_view_centre = vec3(at.x,y,at.y);
                title_view_distance = args.value("distance",(diorama.radius > 0.0f ? diorama.radius : 30.0f) * TITLE_DISTANCE);
                title_view_pitch = args.value("pitch",TITLE_PITCH * 57.29578f) / 57.29578f;
            }else if (op != "state"){
                return json{{"error","op: state, open, pause, continue, click, back, close or view"}};
            }
            auto items = [&](UIMenu& m){
                json list = json::array();
                for (int i = 0; i < m.Count(); i++){
                    UIMenuItem it = m.Item(i);
                    UIMenuRect r = m.ItemRect(i,w,h);
                    list.push_back(json{{"item",i},{"label",it.label},{"enabled",it.f_enabled},
                                        {"rect",json::array({r.x0,r.y0,r.x1,r.y1})}});
                }
                return json{{"items",list},{"focus",m.Focus()}};
            };
            int kind = ask_kind.load();
            static const char* const questions[] = {"none","quit","leave","overwrite","note","report","death"};
            static const char* const lists[] = {"main","new","load","save","replay","settings"};
            int list = menu_screen.load();
            std::string text, detail, busy, note, colony;
            {
                std::lock_guard<std::mutex> lock(menu_text_mutex);
                text = ask_text;
                detail = ask_detail;
                busy = menu_busy;
                note = menu_note;
                colony = colony_name;
            }
            json result{{"screen",TitleShowing() ? "title" : f_pause_open.load() ? "pause" : "game"},
                        {"list",(list >= 0 && list <= CHASM_SCREEN_SETTINGS) ? lists[list] : "?"},
                        {"diorama",json{{"objects",(int)diorama_objects.size()},{"x",diorama.centre.x},{"z",diorama.centre.z},
                                        {"radius",diorama.radius}}},
                        {"question",(kind >= 0 && kind <= CHASM_ASK_DEATH) ? questions[kind] : "?"},
                        {"game_on",f_game_on.load()},{"colony",colony},{"busy",busy},{"note",note},
                        {"menu",items(front())}};
            result["build"] = BuildName();      //as saves and reports name it (ChasmVersion.h)
            //THE DEMO (demo_plan.md step 7): the end's page up, and how each goal stands - as the panel shows them.
            int end = stats_end.load();
            result["end_page"] = !f_stats_open.load() ? "none" : (end == CHASM_END_COMPLETE) ? "complete"
                               : (end == CHASM_END_LOST) ? "lost" : "none";
            if (result["end_page"] != "none"){
                json rects = json::array();
                for (int i = 0; i < end_menu.Count(); i++){
                    UIMenuRect r = EndButtonAt(i);
                    rects.push_back(json::array({r.x0,r.y0,r.x1,r.y1}));
                }
                result["menu"]["rects"] = rects;    //the row's own: the items' "rect" are a column's
            }
            std::shared_ptr<const EconomyState> ge = GetEconomy();
            std::shared_ptr<const ChasmDiscoveries> gd = GetDiscoveries();
            if (ge && gd){
                static const char* const states[] = {"in_progress","met","failed"};
                json goals = json::array();
                for (int g = 0; g < CHASM_GOALS; g++){
                    int st = ChasmGoalState(g,*ge,*gd);
                    goals.push_back(json{{"goal",ChasmGoalTitle(g)},{"state",states[st]},
                                         {"next",(st == CHASM_GOAL_GOING) ? ChasmGoalNext(g,*ge,*gd) : std::string()},
                                         {"told",gd->goal_told[g] >= 0},{"shown",ChasmGoalShown(g,*gd)}});
                }
                result["goals"] = goals;
                result["demo_complete"] = gd->f_complete_told;
                result["colony_lost"] = gd->f_lost_told;
            }
            json triggers = json::object();
            for (int t = 0; t < CHASM_TRIGGERS; t++){
                triggers[TriggerName(t)] = TriggerOn(t);
            }
            result["triggers"] = triggers;
            if (kind != CHASM_ASK_NONE){
                result["question_text"] = text;
                result["question_detail"] = detail;
            }
            //The settings as saved, and as applied: the renderer's samples, the window, the three buses' gains.
            json settings_json{{"path",settings.Path()},{"msaa",settings.GetInt("msaa")},
                               {"msaa_in_use",renderer ? renderer->aa_samples : 0},
                               {"volume_master",settings.GetFloat("volume_master")},{"volume_music",settings.GetFloat("volume_music")},
                               {"volume_effects",settings.GetFloat("volume_effects")},{"fullscreen",settings.GetBool("fullscreen")},
                               {"window_fullscreen",main_window->f_fullscreen},{"master_applied",sound_volume.load()},
                               {"ui_size",settings.GetFloat("ui_size")},
                               {"ui_scale",HudUiScale((float)main_window->height,settings.GetFloat("ui_size"))},
                               {"music_applied",music_volume.load()},{"effects_applied",effects_volume.load()},
                               {"show_fps",settings.GetBool("show_fps")},{"fps_shown",f_show_fps.load()},
                               //Saved in every build, applied only in one without the panels (ApplicationChasm::Init).
                               {"always_record",settings.GetBool("always_record")},
                               {"always_recording",f_always_record.load()},
                               {"recording",main_window->inputcontroller->IsRecording()}};
#ifdef USE_SOUND
            //...and read back from the buses themselves.
            if (soundsystem && soundsystem->f_initialised){
                settings_json["bus_gains"] = json{{"master",soundsystem->GetBusGain(SOUND_BUS_MASTER)},
                                                  {"music",music_bus >= 0 ? soundsystem->GetBusGain(music_bus) : -1.0f},
                                                  {"sounds",sounds_bus >= 0 ? soundsystem->GetBusGain(sounds_bus) : -1.0f}};
            }
#endif
            result["settings"] = settings_json;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_diorama",
        "THE TITLE'S DIORAMA (docs/demo_plan.md; core/SceneCapture.h). op capture: what the game's scene draws "
        "within `radius` (default 40, horizontal) of x,z - its ground, buildings, trees, water, smoke; not the "
        "cursor's marks, outlines or debug views; nothing skinned - written to assets/dioramas/<name>.scap (name "
        "default menu_town), for looking at a real town in the title. The title itself shows ITS OWN TOWN, made by code "
        "at start (docs/test_maps_plan.md step 6, TitleTown.h) - no file. op load: the title's diorama replaced by "
        "<name>'s, without a restart; name \"town\" puts the title's own town back. Both need the game showing "
        "(chasm_menu continue). "
        "Returns what was done - objects, triangles, instances, bytes - or why not.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"radius",{{"type","number"}}},
                {"name",{{"type","string"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string op = args.value("op",std::string("capture"));
            std::string name = args.value("name",std::string(TITLE_DIORAMA));
            if (name.empty() || name.find_first_of("/\\.:") != std::string::npos){
                return json{{"error","name: a bare name, no path"}};
            }
            int request = (op == "capture") ? 1 : (op == "load") ? 2 : 0;
            if (!request){
                return json{{"error","op: capture or load"}};
            }
            vec3 centre;
            if (request == 1){
                if (!args.contains("x") || !args.contains("z")){
                    return json{{"error","capture: give x,z"}};
                }
                vec2 at(args["x"].get<float>(),args["z"].get<float>());
                std::shared_ptr<const ChasmWorld> w = GetWorld();
                float y = (w && w->terrain && w->picker) ? ViewGroundHeight(*w,at,0.0f) : 0.0f;
                centre = vec3(at.x,y,at.y);
            }
            int before = 0;
            {
                std::lock_guard<std::mutex> lock(diorama_mutex);
                before = diorama_done;
                diorama_request = request;
                diorama_ask_centre = centre;
                diorama_ask_radius = args.value("radius",40.0f);
                diorama_ask_name = name;
            }
            //Served on the render thread's next frame: wait for it, a few seconds at most.
            for (int i = 0; i < 100; i++){
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::lock_guard<std::mutex> lock(diorama_mutex);
                if (diorama_done != before){
                    return json{{"op",op},{"name",name},{"result",diorama_result}};
                }
            }
            return json{{"op",op},{"name",name},{"error","not served yet - is the app drawing?"}};
        });
}
#endif
