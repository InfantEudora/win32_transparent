#include "UISheet.h"

#include "Debug.h"
#include "File.h"
#include "tinygltf/json.hpp"

using json = nlohmann::json;

static Debugger* debug = new Debugger("UISheet",DEBUG_INFO);

/*
    nlohmann is free here. It is NOT behind USE_MCP, which is easy to assume because the MCP server
    is the loudest user of it: core/Application.h includes tinygltf/json.hpp unconditionally, and
    GLTFLoader.cpp - which is in every build, never in CORE_SRCS_DROP - is built on tinygltf, which
    is built on this. Every binary this repo produces already links it, `make ship` included. So
    inventing a leaner format for this file would have been paying to avoid a dependency that is
    already paid for.
*/

//The packer writes "./button_blank"; the theme says "button_blank". Strip it on the way in so the
//two agree, rather than making every lookup remember.
static std::string StripPrefix(const std::string& key){
    if (key.size() > 2 && key[0] == '.' && (key[1] == '/' || key[1] == '\\')){
        return key.substr(2);
    }
    return key;
}

//Both exports spell these differently: "w"/"h" in TexturePacker's hash format, "width"/"height" in
//the flat one. Ask for both rather than picking a side.
static bool ReadSize(const json& o, const char* a, const char* b, int* out){
    if (o.contains(a) && o[a].is_number()){
        *out = o[a].get<int>();
        return true;
    }
    if (o.contains(b) && o[b].is_number()){
        *out = o[b].get<int>();
        return true;
    }
    return false;
}

bool UISheet::LoadSheet(const char* asset){
    sprites.clear();

    size_t sz = 0;
    uint8_t* bytes = LoadFile(asset,&sz);
    if (!bytes || !sz){
        debug->Err("UI sheet: could not read %s\n",asset);
        return false;
    }

    //accept=false: nlohmann is compiled with exceptions off in this tree (-fno-exceptions and
    //-DJSON_NOEXCEPTION), so a parse error must be reported by is_discarded rather than thrown.
    json j = json::parse(bytes,bytes + sz,nullptr,false);
    if (j.is_discarded() || !j.is_object()){
        debug->Err("UI sheet: %s is not valid JSON\n",asset);
        return false;
    }

    //The hash format wraps everything in "frames"; the flat one does not. One line to serve both.
    const json& frames = (j.contains("frames") && j["frames"].is_object()) ? j["frames"] : j;

    for (json::const_iterator it = frames.begin(); it != frames.end(); ++it){
        const json& e = it.value();
        if (!e.is_object() || !e.contains("frame")){
            //"meta" in the hash format sits beside the frames rather than inside them.
            continue;
        }
        const json& f = e["frame"];

        ui_sprite s;
        s.name = StripPrefix(it.key());
        if (!f.contains("x") || !f.contains("y") ||
            !ReadSize(f,"w","width",&s.w) || !ReadSize(f,"h","height",&s.h)){
            debug->Err("UI sheet: frame '%s' is missing x/y/w/h\n",s.name.c_str());
            sprites.clear();
            return false;
        }
        s.x = f["x"].get<int>();
        s.y = f["y"].get<int>();

        if (e.contains("trimmed") && e["trimmed"].is_boolean() && e["trimmed"].get<bool>()){
            //See the header. Refused rather than drawn in the wrong place.
            debug->Err("UI sheet: '%s' is TRIMMED, which nine-slicing cannot use - repack with "
                       "trimming off\n",s.name.c_str());
            sprites.clear();
            return false;
        }
        if (e.contains("rotated") && e["rotated"].is_boolean() && e["rotated"].get<bool>()){
            //A rotated frame is stored turned 90 degrees to pack tighter. Drawing it needs the UVs
            //transposed, which nothing here does - so refuse rather than draw it sideways.
            debug->Err("UI sheet: '%s' is ROTATED, which this loader does not undo - repack with "
                       "rotation off\n",s.name.c_str());
            sprites.clear();
            return false;
        }
        sprites.push_back(s);
    }

    if (sprites.empty()){
        debug->Err("UI sheet: %s has no usable frames\n",asset);
        return false;
    }
    debug->Ok("UI sheet: %s, %i sprites\n",asset,(int)sprites.size());
    return true;
}

bool UISheet::LoadTheme(const char* asset){
    elements.clear();

    size_t sz = 0;
    uint8_t* bytes = LoadFile(asset,&sz);
    if (!bytes || !sz){
        debug->Err("UI theme: could not read %s\n",asset);
        return false;
    }
    json j = json::parse(bytes,bytes + sz,nullptr,false);
    if (j.is_discarded() || !j.is_object()){
        debug->Err("UI theme: %s is not valid JSON\n",asset);
        return false;
    }

    for (json::const_iterator it = j.begin(); it != j.end(); ++it){
        const json& e = it.value();
        if (!e.is_object()){
            continue;
        }
        ui_element el;
        el.name = it.key();
        el.sprite = e.value("sprite",std::string());
        if (el.sprite.empty()){
            debug->Err("UI theme: element '%s' names no sprite\n",el.name.c_str());
            elements.clear();
            return false;
        }
        if (e.contains("slice") && e["slice"].is_object()){
            const json& s = e["slice"];
            el.slice_left   = s.value("left",0.0f);
            el.slice_top    = s.value("top",0.0f);
            el.slice_right  = s.value("right",0.0f);
            el.slice_bottom = s.value("bottom",0.0f);
        }
        elements.push_back(el);
    }

    if (elements.empty()){
        debug->Err("UI theme: %s defines nothing\n",asset);
        return false;
    }

    //Every role must resolve NOW rather than when something first tries to draw it. A theme that
    //names a sprite the sheet does not have is a typo or a stale repack, and finding out at load
    //costs one message where finding out at draw costs a silently missing widget.
    bool f_ok = true;
    for (size_t i = 0; i < elements.size(); i++){
        if (!FindSprite(elements[i].sprite.c_str())){
            debug->Err("UI theme: element '%s' wants sprite '%s', which is not in the sheet\n",
                       elements[i].name.c_str(),elements[i].sprite.c_str());
            f_ok = false;
        }
    }
    if (!f_ok){
        elements.clear();
        return false;
    }

    debug->Ok("UI theme: %s, %i elements\n",asset,(int)elements.size());
    return true;
}

bool UISheet::ValidateAgainstAtlas(int w, int h) const {
    bool f_ok = true;
    for (size_t i = 0; i < sprites.size(); i++){
        const ui_sprite& s = sprites[i];
        if (s.x < 0 || s.y < 0 || (s.x + s.w) > w || (s.y + s.h) > h){
            debug->Err("UI sheet: '%s' is at %i,%i %ix%i, outside a %ix%i atlas - the JSON and the "
                       "PNG are from different packs\n",
                       s.name.c_str(),s.x,s.y,s.w,s.h,w,h);
            f_ok = false;
        }
    }
    return f_ok;
}

const ui_sprite* UISheet::FindSprite(const char* name) const {
    for (size_t i = 0; i < sprites.size(); i++){
        if (sprites[i].name == name){
            return &sprites[i];
        }
    }
    return NULL;
}

const ui_element* UISheet::FindElement(const char* name) const {
    for (size_t i = 0; i < elements.size(); i++){
        if (elements[i].name == name){
            return &elements[i];
        }
    }
    return NULL;
}
