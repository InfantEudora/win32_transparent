#include "ChasmSave.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

#define CHASM_SAVE_DIR  "saves"

json ChasmSaveToJson(const ChasmSave& s){
    const GridSettings& g = s.settings;
    //A building by name of kind and crop, so a person can read a save, and an id that never changes.
    json buildings = json::array();
    for (const ZoneSavedBuilding& b : s.buildings){
        json jb{{"id",b.id},{"kind",ZoneKindName(b.kind)}};
        if (b.kind == ZONE_KIND_FIELD){
            jb["cells"] = b.cells;
            jb["crop"] = ZoneCropName(b.crop);
        }else{
            json plots = json::array();
            //[plot, storeys], and a third - what of it stands - on a construction site's plot.
            for (size_t i = 0; i < b.plots.size(); i++){
                int st = (i < b.standing.size()) ? b.standing[i] : b.plots[i].second;
                if (st < b.plots[i].second){
                    plots.push_back(json::array({b.plots[i].first,b.plots[i].second,st}));
                }else{
                    plots.push_back(json::array({b.plots[i].first,b.plots[i].second}));
                }
            }
            jb["plots"] = plots;
        }
        //What a store takes on its own, by name - written only when it is not everything.
        if (b.kind == ZONE_KIND_STORE && b.allow != GOODS_ALL){
            json allow = json::array();
            for (int g = 0; g < GOOD_COUNT; g++){
                if (b.allow & GOOD_BIT(g)){
                    allow.push_back(GoodName(g));
                }
            }
            jb["allow"] = allow;
        }
        buildings.push_back(jb);
    }
    json grounds = json::array();
    for (const auto& gr : s.grounds){
        grounds.push_back(json::array({gr.first,gr.second}));
    }
    /*
        A walker as it stands: where it walks between, which way, the path it is on and how far down
        it. `along` is a float; written as JSON's double it reads back to the same bits, which the
        replay's state hash depends on.
    */
    json walkers = json::array();
    for (const Walker& k : s.walkers){
        walkers.push_back(json{{"home",k.home},{"goal",k.goal},{"outward",k.f_outward},{"path",k.path},
                               {"seg",k.seg},{"along",k.along},{"stuck",k.f_stuck},{"legs",k.legs}});
    }
    /*
        The economy (Economy.h): stocks by building and good name, felled props, every worker on his
        route, every field's growth. Floats as JSON doubles, which read back to the same bits.
    */
    json economy = json::object();
    {
        const EconomySaved& e = s.economy;
        json stocks = json::array();
        for (const auto& st : e.stocks){
            json goods = json::object();
            for (int g = 0; g < GOOD_COUNT; g++){
                if (st.second[g]){
                    goods[GoodName(g)] = st.second[g];
                }
            }
            stocks.push_back(json{{"building",st.first},{"goods",goods}});
        }
        json props = json::array();
        for (const auto& pr : e.props){
            props.push_back(json::array({pr.first,pr.second}));
        }
        json workers = json::array();
        for (const EconomyWorker& k : e.workers){
            json route = json::array();
            for (const vec2& p : k.route){
                route.push_back(json::array({p.x,p.y}));
            }
            json skills = json::array();
            for (int sk = 0; sk < SKILL_COUNT; sk++){
                skills.push_back((int)k.skills[sk]);
            }
            workers.push_back(json{{"id",k.id},{"family",k.family},{"house",k.house},{"age",k.age},{"female",k.f_female},
                                   {"skills",skills},{"building",k.building},{"job",k.job},{"state",k.state},{"prop",k.prop},
                                   {"store",k.store},{"site",k.site},{"carry_good",k.carry_good},{"carry",k.carry},
                                   {"timer",k.timer},{"route",route},{"speed",k.speed},{"seg",k.seg},
                                   {"along",k.along},{"pos",json::array({k.pos.x,k.pos.y})},{"indoors",k.f_indoors}});
        }
        json fields = json::array();
        for (const EconomyField& f : e.fields){
            fields.push_back(json::array({f.building,f.crop,f.grown}));
        }
        json sites = json::array();
        for (const auto& st : e.sites){
            sites.push_back(json::array({st.first,st.second}));
        }
        //Gardens and lots being built: plot, wood brought.
        json ground_sites = json::array();
        for (const auto& st : e.ground_sites){
            ground_sites.push_back(json::array({st.first,st.second}));
        }
        economy = json{{"stocks",stocks},{"felled",props},{"people",workers},{"fields",fields},
                       {"sites",sites},{"ground_sites",ground_sites},{"next_person",e.next_person}};
    }
    return json{
        {"chasm_save",CHASM_SAVE_VERSION},
        {"grid",{
            {"seed",g.seed},
            {"target_fine_cells",g.target_fine_cells},
            {"aspect",g.aspect},
            {"triangle_side",g.triangle_side},
            {"relax_passes_coarse",g.relax_passes_coarse},
            {"relax_passes_fine",g.relax_passes_fine},
            {"relax_strength",g.relax_strength}
        }},
        {"world_hash",s.world_hash},
        {"buildings",buildings},
        {"next_building",s.next_building},
        {"stroke",{s.stroke,s.stroke_building}},
        {"grounds",grounds},
        {"walkers",walkers},
        {"calendar",s.calendar_tick},
        {"explored",s.explored},
        {"economy",economy}
    };
}

bool ChasmSaveFromJson(const json& j, ChasmSave& out, std::string& error){
    out = ChasmSave();
    if (!j.is_object() || !j.contains("chasm_save")){
        error = "not a chasm save";
        return false;
    }
    int version = j["chasm_save"].get<int>();
    if (version < 1 || version > CHASM_SAVE_VERSION){
        error = "save version " + std::to_string(version) + ", expected 1 to " +
                std::to_string(CHASM_SAVE_VERSION);
        return false;
    }
    const json& g = j.value("grid",json::object());
    GridSettings& s = out.settings;
    s.seed = g.value("seed",s.seed);
    s.target_fine_cells = g.value("target_fine_cells",s.target_fine_cells);
    s.aspect = g.value("aspect",s.aspect);
    s.triangle_side = g.value("triangle_side",s.triangle_side);
    s.relax_passes_coarse = g.value("relax_passes_coarse",s.relax_passes_coarse);
    s.relax_passes_fine = g.value("relax_passes_fine",s.relax_passes_fine);
    s.relax_strength = g.value("relax_strength",s.relax_strength);
    out.world_hash = j.value("world_hash",std::string());
    for (const json& jb : j.value("buildings",json::array())){
        ZoneSavedBuilding b;
        b.id = jb.value("id",0u);
        b.kind = ZoneKindByName(jb.value("kind",std::string()));
        int crop = ZoneCropByName(jb.value("crop",std::string("wheat")));
        b.crop = (crop < 0) ? ZONE_CROP_WHEAT : crop;
        for (const json& pl : jb.value("plots",json::array())){
            b.plots.push_back(std::make_pair(pl[0].get<int>(),pl[1].get<int>()));
            b.standing.push_back((pl.size() >= 3) ? pl[2].get<int>() : pl[1].get<int>());
        }
        b.cells = jb.value("cells",std::vector<int>());
        if (jb.contains("allow") && jb["allow"].is_array()){
            b.allow = 0;
            for (const json& g : jb["allow"]){
                int good = GoodByName(g.get<std::string>());
                if (good >= 0){
                    b.allow |= GOOD_BIT(good);
                }
            }
        }
        out.buildings.push_back(b);
        out.next_building = std::max(out.next_building,b.id + 1);
    }
    out.next_building = std::max(out.next_building,j.value("next_building",1u));
    if (j.contains("stroke") && j["stroke"].is_array() && j["stroke"].size() == 2){
        out.stroke = j["stroke"][0].get<uint32_t>();
        out.stroke_building = j["stroke"][1].get<uint32_t>();
    }
    /*
        Version 1 had no buildings, only plots and cells: each house plot comes back a house of its own
        and each field cell a field of its own - nothing is lost, it is only not grouped.
    */
    for (const json& h : j.value("houses",json::array())){
        ZoneSavedBuilding b;
        b.id = out.next_building++;
        b.kind = ZONE_KIND_HOUSE;
        b.plots.push_back(std::make_pair(h[0].get<int>(),h[1].get<int>()));
        out.buildings.push_back(b);
    }
    for (const json& f : j.value("fields",json::array())){
        ZoneSavedBuilding b;
        b.id = out.next_building++;
        b.kind = ZONE_KIND_FIELD;
        b.cells.push_back(f.get<int>());
        out.buildings.push_back(b);
    }
    out.calendar_tick = j.value("calendar",(uint64_t)0);
    out.explored = j.value("explored",std::string());
    if (j.contains("economy") && j["economy"].is_object()){
        const json& e = j["economy"];
        for (const json& st : e.value("stocks",json::array())){
            std::array<int,GOOD_COUNT> goods{};
            for (auto it = st["goods"].begin(); it != st["goods"].end(); ++it){
                int g = GoodByName(it.key());
                if (g >= 0){
                    goods[g] = it.value().get<int>();
                }
            }
            out.economy.stocks.push_back(std::make_pair(st.value("building",0u),goods));
        }
        for (const json& pr : e.value("felled",json::array())){
            out.economy.props.push_back(std::make_pair(pr[0].get<int>(),pr[1].get<int>()));
        }
        //"people" since P4; "workers" before, the stand-ins - read, and dropped by Economy::Restore.
        json people = e.contains("people") ? e["people"] : e.value("workers",json::array());
        for (const json& wk : people){
            EconomyWorker k;
            k.id = wk.value("id",0u);
            k.family = wk.value("family",0u);
            k.house = wk.value("house",0u);
            k.age = wk.value("age",0);
            //A save from before people had a sex: one by the id, so a load always gives the same.
            k.f_female = wk.contains("female") ? wk["female"].get<bool>() : (k.id % 2) == 0;
            std::vector<int> skills = wk.value("skills",std::vector<int>());
            for (int sk = 0; sk < SKILL_COUNT && sk < (int)skills.size(); sk++){
                k.skills[sk] = (uint8_t)skills[sk];
            }
            k.building = wk.value("building",0u);
            k.job = wk.value("job",(int)WORKER_JOB_NONE);
            k.state = wk.value("state",0);
            k.prop = wk.value("prop",-1);
            k.store = wk.value("store",0u);
            k.site = wk.value("site",0u);
            k.carry_good = wk.value("carry_good",-1);
            k.carry = wk.value("carry",0);
            k.timer = wk.value("timer",0);
            for (const json& p : wk.value("route",json::array())){
                k.route.push_back(vec2(p[0].get<float>(),p[1].get<float>()));
            }
            k.speed = wk.value("speed",std::vector<float>());
            k.seg = wk.value("seg",0);
            k.along = wk.value("along",0.0f);
            if (wk.contains("pos")){
                k.pos = vec2(wk["pos"][0].get<float>(),wk["pos"][1].get<float>());
            }
            k.f_indoors = wk.value("indoors",false);
            if (k.route.empty()){
                k.route.push_back(k.pos);
            }
            out.economy.workers.push_back(k);
        }
        out.economy.next_person = e.value("next_person",1u);
        for (const json& st : e.value("sites",json::array())){
            out.economy.sites.push_back(std::make_pair(st[0].get<uint32_t>(),st[1].get<int>()));
        }
        for (const json& st : e.value("ground_sites",json::array())){
            out.economy.ground_sites.push_back(std::make_pair(st[0].get<int>(),st[1].get<int>()));
        }
        for (const json& f : e.value("fields",json::array())){
            EconomyField fd;
            fd.building = f[0].get<uint32_t>();
            fd.crop = f[1].get<int>();
            fd.grown = f[2].get<int>();
            out.economy.fields.push_back(fd);
        }
    }
    for (const json& gr : j.value("grounds",json::array())){
        out.grounds.push_back(std::make_pair(gr[0].get<int>(),gr[1].get<int>()));
    }
    for (const json& wj : j.value("walkers",json::array())){
        Walker k;
        k.home = wj.value("home",-1);
        k.goal = wj.value("goal",-1);
        k.f_outward = wj.value("outward",true);
        k.path = wj.value("path",std::vector<int>());
        k.seg = wj.value("seg",0);
        k.along = wj.value("along",0.0f);
        k.f_stuck = wj.value("stuck",false);
        k.legs = wj.value("legs",0u);
        out.walkers.push_back(k);
    }
    return true;
}

std::string ChasmSavePath(const std::string& name){
    std::string n = name;
    if (n.size() < 5 || n.compare(n.size() - 5,5,".json") != 0){
        n += ".json";
    }
    if (n.find('/') != std::string::npos || n.find('\\') != std::string::npos){
        return n;
    }
    return std::string(CHASM_SAVE_DIR) + "/" + n;
}

bool ChasmSaveWrite(const std::string& name, const json& j, std::string& error){
#ifdef _WIN32
    CreateDirectoryA(CHASM_SAVE_DIR,NULL);      //fails harmlessly if it exists
#endif
    std::string path = ChasmSavePath(name);
    std::ofstream f(path.c_str(),std::ios::binary);
    if (!f){
        error = "cannot write '" + path + "'";
        return false;
    }
    //Indented: a person may want to read what a save holds, and it is small.
    f << j.dump(1) << "\n";
    if (!f){
        error = "write to '" + path + "' failed";
        return false;
    }
    return true;
}

bool ChasmSaveRead(const std::string& name, json& out, std::string& error){
    std::string path = ChasmSavePath(name);
    std::ifstream f(path.c_str(),std::ios::binary);
    if (!f){
        error = "cannot open '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out = json::parse(ss.str(),nullptr,false);
    if (out.is_discarded()){
        error = "'" + path + "' is not valid JSON";
        return false;
    }
    return true;
}

std::vector<std::string> ChasmSaveList(){
    std::vector<std::string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(CHASM_SAVE_DIR "/*.json",&fd);
    if (h != INVALID_HANDLE_VALUE){
        do{
            std::string n = fd.cFileName;
            names.push_back(n.substr(0,n.size() - 5));
        }while (FindNextFileA(h,&fd));
        FindClose(h);
    }
#endif
    std::sort(names.begin(),names.end());
    return names;
}
