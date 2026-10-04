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
    json features = json::array();
    for (const GridLine& line : g.features){
        json pts = json::array();
        for (const vec2& p : line.points){
            pts.push_back(json::array({p.x,p.y}));
        }
        features.push_back(pts);
    }
    json houses = json::array();
    for (const auto& h : s.houses){
        houses.push_back(json::array({h.first,h.second}));
    }
    json grounds = json::array();
    for (const auto& gr : s.grounds){
        grounds.push_back(json::array({gr.first,gr.second}));
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
            {"relax_strength",g.relax_strength},
            {"features",features}
        }},
        {"world_hash",s.world_hash},
        {"houses",houses},
        {"fields",s.fields},
        {"grounds",grounds}
    };
}

bool ChasmSaveFromJson(const json& j, ChasmSave& out, std::string& error){
    out = ChasmSave();
    if (!j.is_object() || !j.contains("chasm_save")){
        error = "not a chasm save";
        return false;
    }
    if (j["chasm_save"].get<int>() != CHASM_SAVE_VERSION){
        error = "save version " + std::to_string(j["chasm_save"].get<int>()) + ", expected " +
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
    //Features are stored rather than assumed default, so a save of a hand-drawn map loads as that
    //map even after the defaults change.
    if (g.contains("features")){
        s.features.clear();
        for (const json& line : g["features"]){
            GridLine gl;
            for (const json& p : line){
                gl.points.push_back(vec2(p[0].get<float>(),p[1].get<float>()));
            }
            s.features.push_back(gl);
        }
    }
    out.world_hash = j.value("world_hash",std::string());
    for (const json& h : j.value("houses",json::array())){
        out.houses.push_back(std::make_pair(h[0].get<int>(),h[1].get<int>()));
    }
    for (const json& f : j.value("fields",json::array())){
        out.fields.push_back(f.get<int>());
    }
    for (const json& gr : j.value("grounds",json::array())){
        out.grounds.push_back(std::make_pair(gr[0].get<int>(),gr[1].get<int>()));
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
