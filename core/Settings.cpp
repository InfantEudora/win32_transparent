#include "Settings.h"

#include <cmath>
#include <cstdio>
#include "tinygltf/json.hpp"

#ifdef _WIN32
#include <windows.h>     //MoveFileExA, for the save's rename over an existing file
#endif

using json = nlohmann::json;

//Settings.cpp stands alone (tools/menu_test.cpp links it with nothing else), so it reads and
//writes files itself rather than through File.cpp - which is ReadFileToString's disk-only rule
//anyway: a player's settings are never a baked asset.
static bool ReadAll(const std::string& path, std::string& out){
    FILE* f = fopen(path.c_str(),"rb");
    if (!f){
        return false;
    }
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf,1,sizeof(buf),f)) > 0){
        out.append(buf,n);
    }
    fclose(f);
    return true;
}

void Settings::Declare(const Entry& e){
    std::lock_guard<std::mutex> lock(mutex);
    for (Entry& x : entries){
        if (x.key == e.key){
            x = e;
            return;
        }
    }
    entries.push_back(e);
}

void Settings::DeclareBool(const char* key, bool def){
    Entry e;
    e.key = key;
    e.type = T_BOOL;
    e.b = e.b_def = def;
    Declare(e);
}

void Settings::DeclareInt(const char* key, int def, int lo, int hi){
    Entry e;
    e.key = key;
    e.type = T_INT;
    e.i_lo = lo;
    e.i_hi = hi;
    e.i = e.i_def = (def < lo) ? lo : ((def > hi) ? hi : def);
    Declare(e);
}

void Settings::DeclareFloat(const char* key, float def, float lo, float hi){
    Entry e;
    e.key = key;
    e.type = T_FLOAT;
    e.f_lo = lo;
    e.f_hi = hi;
    e.f = e.f_def = (def < lo) ? lo : ((def > hi) ? hi : def);
    Declare(e);
}

Settings::Entry* Settings::Find(const char* key){
    for (Entry& e : entries){
        if (e.key == key){
            return &e;
        }
    }
    return NULL;
}

const Settings::Entry* Settings::Find(const char* key) const{
    for (const Entry& e : entries){
        if (e.key == key){
            return &e;
        }
    }
    return NULL;
}

bool Settings::Load(const std::string& file, std::string* report){
    std::lock_guard<std::mutex> lock(mutex);
    path = file;
    unknown.clear();
    for (Entry& e : entries){
        e.b = e.b_def;
        e.i = e.i_def;
        e.f = e.f_def;
    }
    auto note = [report](const std::string& line){
        if (report){
            *report += line;
            *report += "\n";
        }
    };
    std::string text;
    if (!ReadAll(file,text)){
        return false;           //a first run: the defaults, and nothing to report
    }
    //The tree builds with -fno-exceptions: parse without throwing, and read every value by type.
    json j = json::parse(text,nullptr,false);
    if (j.is_discarded() || !j.is_object()){
        note(file + " is not a JSON object - using the defaults");
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it){
        Entry* e = Find(it.key().c_str());
        if (!e){
            unknown.push_back(std::make_pair(it.key(),it.value().dump()));
            continue;
        }
        const json& v = it.value();
        switch (e->type){
            case T_BOOL:
                if (v.is_boolean()){
                    e->b = v.get<bool>();
                }else{
                    note(e->key + " is not true/false - using the default");
                }
            break;
            case T_INT:
                if (v.is_number()){
                    double d = v.get<double>();
                    int n = (int)std::lround(d);
                    if (n < e->i_lo || n > e->i_hi){
                        note(e->key + " is out of range - clamped");
                        n = (n < e->i_lo) ? e->i_lo : e->i_hi;
                    }
                    e->i = n;
                }else{
                    note(e->key + " is not a number - using the default");
                }
            break;
            case T_FLOAT:
                if (v.is_number()){
                    float x = (float)v.get<double>();
                    if (!std::isfinite(x)){
                        note(e->key + " is not finite - using the default");
                        break;
                    }
                    if (x < e->f_lo || x > e->f_hi){
                        note(e->key + " is out of range - clamped");
                        x = (x < e->f_lo) ? e->f_lo : e->f_hi;
                    }
                    e->f = x;
                }else{
                    note(e->key + " is not a number - using the default");
                }
            break;
        }
    }
    revision++;
    return true;
}

//By hand rather than json::dump, for one key per line in declaration order - the file is the
//player's to read and edit, and dump() sorts keys and puts the floats' last bits on show.
std::string Settings::TextLocked() const{
    std::string out = "{\n";
    bool f_first = true;
    auto add = [&out,&f_first](const std::string& key, const std::string& value){
        if (!f_first){
            out += ",\n";
        }
        f_first = false;
        out += "  " + json(key).dump() + ": " + value;
    };
    for (const Entry& e : entries){
        char buf[64];
        switch (e.type){
            case T_BOOL:  add(e.key,e.b ? "true" : "false"); break;
            case T_INT:   snprintf(buf,sizeof(buf),"%d",e.i); add(e.key,buf); break;
            case T_FLOAT: snprintf(buf,sizeof(buf),"%.3g",(double)e.f); add(e.key,buf); break;
        }
    }
    for (const auto& u : unknown){
        add(u.first,u.second);
    }
    out += "\n}\n";
    return out;
}

std::string Settings::ToText() const{
    std::lock_guard<std::mutex> lock(mutex);
    return TextLocked();
}

bool Settings::Save(){
    std::string text, file;
    {
        std::lock_guard<std::mutex> lock(mutex);
        text = TextLocked();
        file = path;
    }
    if (file.empty()){
        return false;
    }
    std::string tmp = file + ".tmp";
    FILE* f = fopen(tmp.c_str(),"wb");
    if (!f){
        return false;
    }
    bool f_ok = fwrite(text.data(),1,text.size(),f) == text.size();
    f_ok = (fclose(f) == 0) && f_ok;
    if (!f_ok){
        remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    return MoveFileExA(tmp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(tmp.c_str(),file.c_str()) == 0;
#endif
}

bool Settings::GetBool(const char* key) const{
    std::lock_guard<std::mutex> lock(mutex);
    const Entry* e = Find(key);
    return (e && e->type == T_BOOL) ? e->b : false;
}

int Settings::GetInt(const char* key) const{
    std::lock_guard<std::mutex> lock(mutex);
    const Entry* e = Find(key);
    return (e && e->type == T_INT) ? e->i : 0;
}

float Settings::GetFloat(const char* key) const{
    std::lock_guard<std::mutex> lock(mutex);
    const Entry* e = Find(key);
    return (e && e->type == T_FLOAT) ? e->f : 0.0f;
}

bool Settings::SetBool(const char* key, bool value){
    std::lock_guard<std::mutex> lock(mutex);
    Entry* e = Find(key);
    if (!e || e->type != T_BOOL || e->b == value){
        return false;
    }
    e->b = value;
    revision++;
    return true;
}

bool Settings::SetInt(const char* key, int value){
    std::lock_guard<std::mutex> lock(mutex);
    Entry* e = Find(key);
    if (!e || e->type != T_INT){
        return false;
    }
    value = (value < e->i_lo) ? e->i_lo : ((value > e->i_hi) ? e->i_hi : value);
    if (e->i == value){
        return false;
    }
    e->i = value;
    revision++;
    return true;
}

bool Settings::SetFloat(const char* key, float value){
    std::lock_guard<std::mutex> lock(mutex);
    Entry* e = Find(key);
    if (!e || e->type != T_FLOAT || !std::isfinite(value)){
        return false;
    }
    value = (value < e->f_lo) ? e->f_lo : ((value > e->f_hi) ? e->f_hi : value);
    if (e->f == value){
        return false;
    }
    e->f = value;
    revision++;
    return true;
}

void Settings::Reset(const char* key){
    std::lock_guard<std::mutex> lock(mutex);
    Entry* e = Find(key);
    if (!e){
        return;
    }
    e->b = e->b_def;
    e->i = e->i_def;
    e->f = e->f_def;
    revision++;
}

uint32_t Settings::Revision() const{
    std::lock_guard<std::mutex> lock(mutex);
    return revision;
}

std::string Settings::Path() const{
    std::lock_guard<std::mutex> lock(mutex);
    return path;
}
