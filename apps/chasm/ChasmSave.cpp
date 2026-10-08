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
            jb["crop"] = ZoneCropName(b.crop);          //its starting crop - "none" until chosen
            if (b.next_crop != ZONE_CROP_AUTO){
                jb["next"] = ZoneCropName(b.next_crop);
            }
            //The house the player gave it to (land_plan.md) - written only when given.
            if (b.land_of){
                jb["land"] = b.land_of;
            }
            //Which of them are built - written only while some are still a site (fields_plan.md).
            if (std::find(b.cells_built.begin(),b.cells_built.end(),0) != b.cells_built.end()){
                jb["cells_built"] = b.cells_built;
            }
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
        //A site the player put first - written only when it is.
        if (b.f_priority){
            jb["priority"] = true;
        }
        buildings.push_back(jb);
    }
    //[plot, kind] - the kind with its site and gate flags - and a road's or wall's line as a third.
    json grounds = json::array();
    for (const ZoneSavedGround& gr : s.grounds){
        if (gr.line){
            grounds.push_back(json::array({gr.plot,gr.kind,gr.line}));
        }else{
            grounds.push_back(json::array({gr.plot,gr.kind}));
        }
    }
    /*
        The economy (Economy.h): stocks by building and good name, felled props, every worker on his
        route with his health and water, every field's growth, the dead. Floats as JSON doubles, which
        read back to the same bits.
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
                                   {"timer",k.timer},{"stint",k.stint},{"route",route},{"speed",k.speed},{"seg",k.seg},
                                   {"along",k.along},{"pos",json::array({k.pos.x,k.pos.y})},{"indoors",k.f_indoors},
                                   {"health",k.health},{"water",k.water}});
        }
        //The colony's dead (needs_plan.md): id, family, female, age, the tick they died on, of what.
        json dead = json::array();
        for (const EconomyDeath& d : e.dead){
            dead.push_back(json::array({d.id,d.family,d.f_female,d.age,d.tick,d.cause}));
        }
        json fields = json::array();
        //[building, crop growing, grown, sown, yield %, last crop harvested (fields_plan.md), owner (land_plan.md),
        //and the crop the frost took while it waits to be sown again].
        for (const EconomyField& f : e.fields){
            json one = json::array({f.building,f.crop,f.grown,f.f_sown,f.yield,f.last,f.owner});
            if (f.frost >= 0){
                one.push_back(f.frost);
            }
            fields.push_back(one);
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
        //Homes warm or cold since dawn (needs_plan.md, step 3): building (0 the homeless), heat.
        json heats = json::array();
        for (const auto& hh : e.heats){
            heats.push_back(json::array({hh.first,hh.second}));
        }
        economy = json{{"stocks",stocks},{"felled",props},{"people",workers},{"fields",fields},
                       {"sites",sites},{"ground_sites",ground_sites},{"heat",heats},{"next_person",e.next_person},{"dead",dead}};
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
        int next = ZoneCropByName(jb.value("next",std::string("auto")));
        b.next_crop = (next < 0 || next == ZONE_CROP_NONE) ? ZONE_CROP_AUTO : next;
        b.cells_built = jb.value("cells_built",std::vector<int>());
        b.land_of = jb.value("land",0u);
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
        b.f_priority = jb.value("priority",false);
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
            k.stint = wk.value("stint",0);      //road work (line_works_plan.md); none before it
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
            //A save from before needs (needs_plan.md): everyone fed and watered.
            k.health = wk.value("health",(int)NEEDS_HEALTH_FULL);
            k.water = wk.value("water",(int)NEEDS_WATER_FULL);
            if (k.route.empty()){
                k.route.push_back(k.pos);
            }
            out.economy.workers.push_back(k);
        }
        out.economy.next_person = e.value("next_person",1u);
        for (const json& dj : e.value("dead",json::array())){
            if (!dj.is_array() || dj.size() < 6){
                continue;
            }
            EconomyDeath d;
            d.id = dj[0].get<uint32_t>();
            d.family = dj[1].get<uint32_t>();
            d.f_female = dj[2].get<bool>();
            d.age = dj[3].get<int>();
            d.tick = dj[4].get<uint64_t>();
            d.cause = dj[5].get<int>();
            out.economy.dead.push_back(d);
        }
        for (const json& st : e.value("sites",json::array())){
            out.economy.sites.push_back(std::make_pair(st[0].get<uint32_t>(),st[1].get<int>()));
        }
        for (const json& st : e.value("ground_sites",json::array())){
            out.economy.ground_sites.push_back(std::make_pair(st[0].get<int>(),st[1].get<int>()));
        }
        //Absent before firewood: no home warm or cold until the next dawn.
        for (const json& hh : e.value("heat",json::array())){
            out.economy.heats.push_back(std::make_pair(hh[0].get<uint32_t>(),hh[1].get<int>()));
        }
        for (const json& f : e.value("fields",json::array())){
            EconomyField fd;
            fd.building = f[0].get<uint32_t>();
            fd.crop = f[1].get<int>();
            fd.grown = f[2].get<int>();
            //A save from before sowing (fields_plan.md) holds only the first three: its crop was growing.
            fd.f_sown = (f.size() > 3) ? f[3].get<bool>() : (fd.crop >= 0);
            fd.yield = (f.size() > 4) ? f[4].get<int>() : 100;
            fd.last = (f.size() > 5) ? f[5].get<int>() : -1;
            //...and one from before land (land_plan.md) no owner: it gets one on its first tick.
            fd.owner = (f.size() > 6) ? f[6].get<uint32_t>() : 0;
            fd.frost = (f.size() > 7) ? f[7].get<int>() : -1;
            out.economy.f_before_land = out.economy.f_before_land || f.size() <= 6;
            out.economy.fields.push_back(fd);
        }
    }
    for (const json& gr : j.value("grounds",json::array())){
        ZoneSavedGround g;
        g.plot = gr[0].get<int>();
        g.kind = gr[1].get<int>();
        g.line = (gr.size() >= 3) ? gr[2].get<uint32_t>() : 0;
        out.grounds.push_back(g);
    }
    //An older save's "walkers" - the debug walker, retired 2026-10-07 - are let go unread.
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
