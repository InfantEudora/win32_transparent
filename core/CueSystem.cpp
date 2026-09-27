#include "CueSystem.h"
#include "File.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <set>

#include "Debug.h"
static Debugger *debug = new Debugger("CueSystem", DEBUG_INFO);

//--- Small pieces -------------------------------------------------------------------------------

CuePayload& CuePayload::Set(const std::string& name, float value){
    for (auto& v : values){
        if (v.first == name){
            v.second = value;
            return *this;
        }
    }
    values.push_back(std::make_pair(name,value));
    return *this;
}

bool CuePayload::Has(const std::string& name) const{
    for (const auto& v : values){
        if (v.first == name){
            return true;
        }
    }
    return false;
}

float CuePayload::Get(const std::string& name, float fallback) const{
    for (const auto& v : values){
        if (v.first == name){
            return v.second;
        }
    }
    return fallback;
}

//See the declaration: Hash01's constants and order, with key and instance where x and y were.
float CueHash01(uint32_t key, uint32_t instance, uint64_t tick, uint32_t salt){
    uint32_t h = key * 374761393u;
    h += instance * 668265263u;
    h += (uint32_t)(int32_t)(int)tick * 2246822519u;
    h += salt * 3266489917u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

//A cue's key when the table gives no seed: FNV-1a of its name, so it is stable across reloads and
//across machines, and two cues on one tick do not draw alike.
static uint32_t NameKey(const std::string& s){
    uint32_t h = 2166136261u;
    for (unsigned char c : s){
        h = (h ^ c) * 16777619u;
    }
    return h;
}

//--- Loading ------------------------------------------------------------------------------------

void CueSystem::Init(float ticks_per_second, CueOutput* out){
    tps = (ticks_per_second > 0.0f) ? ticks_per_second : 60.0f;
    output = out;
}

/*
    FRESH FROM DISK FIRST, the packed copy second. A table is reloaded while it is being tuned, so
    the first read must not come from LoadFile's cache, which would hand back the bytes of the
    first load forever. But a shipped build has no loose file at all - the table is baked into
    the executable, possibly compressed - and only LoadFile knows how to find it there. So a disk
    read that fails falls back to LoadFile; in a loose build where the file is simply missing,
    that fails too, logs the name, and the error says so.
*/
bool CueSystem::LoadTable(const char* asset_name, std::string& error){
    std::string text;
    if (!asset_name){
        error = "no cue table named";
        return false;
    }
    if (!ReadFileToString(asset_name,text)){
        size_t size = 0;
        uint8_t* bytes = ::LoadFile(asset_name,&size);
        if (!bytes){
            error = std::string("no cue table named ") + asset_name;
            return false;
        }
        text.assign((const char*)bytes,size);
    }
    if (!LoadTableText(text,error,asset_name)){
        return false;
    }
    file = asset_name;
    return true;
}

bool CueSystem::Reload(std::string& error){
    if (file.empty()){
        error = "no cue table has been loaded from a file";
        return false;
    }
    std::string keep = file;
    return LoadTable(keep.c_str(),error);
}

bool CueSystem::LoadTableText(const std::string& text, std::string& error, const std::string& where){
    //No exceptions in this build, so a malformed file must be caught as a discarded value - and
    //every read below checks its type first, because a wrong type is an abort, not an error.
    const json j = json::parse(text,nullptr,false);
    if (j.is_discarded() || !j.is_object()){
        error = where + " is not valid JSON";
        return false;
    }
    CueSystem staged;
    staged.tps = tps;
    staged.output = output;
    if (!staged.Parse(j,where,error)){
        return false;
    }
    /*
        Swap in the new table. What was WAITING is dropped - its row may no longer say what it
        said when it was decided - and what is playing rings on under its old rules, which the
        Playing records carry. History is kept by name, which is the reason it is keyed that way.
    */
    for (const Waiting& w : waiting){
        Skip(w.cue,"","reload");
    }
    waiting.clear();
    cues = std::move(staged.cues);
    cue_index = std::move(staged.cue_index);
    groups = std::move(staged.groups);
    //A bus keeps where its duck had got to, so a reload mid-narration does not jump the volume.
    for (Bus& b : staged.buses){
        for (const Bus& old : buses){
            if (old.name == b.name){
                b.duck = old.duck;
            }
        }
    }
    buses = std::move(staged.buses);
    peaks = std::move(staged.peaks);
    lengths = std::move(staged.lengths);
    debug->Info("Cue table %s: %i cues, %i groups, %i buses\n",where.c_str(),(int)cues.size(),
                (int)groups.size(),(int)buses.size());
    return true;
}

namespace {

bool Num(const json& o, const char* key, float fallback, float& out){
    auto it = o.find(key);
    if (it == o.end()){
        out = fallback;
        return true;
    }
    if (!it->is_number()){
        return false;
    }
    out = it->get<float>();
    return true;
}

bool Int(const json& o, const char* key, int fallback, int& out){
    float f = 0.0f;
    if (!Num(o,key,(float)fallback,f)){
        return false;
    }
    out = (int)lroundf(f);
    return true;
}

bool Str(const json& o, const char* key, const std::string& fallback, std::string& out){
    auto it = o.find(key);
    if (it == o.end()){
        out = fallback;
        return true;
    }
    if (!it->is_string()){
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool Bool(const json& o, const char* key, bool fallback, bool& out){
    auto it = o.find(key);
    if (it == o.end()){
        out = fallback;
        return true;
    }
    if (!it->is_boolean()){
        return false;
    }
    out = it->get<bool>();
    return true;
}

//A two-number array.
bool Pair(const json& o, const char* key, float& a, float& b){
    auto it = o.find(key);
    if (it == o.end()){
        return true;
    }
    if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()){
        return false;
    }
    a = (*it)[0].get<float>();
    b = (*it)[1].get<float>();
    return true;
}

//Unknown keys are an ERROR, not ignored: a misspelt "jiter" silently doing nothing is exactly
//the kind of thing that costs an evening of tuning by ear.
bool OnlyKeys(const json& o, const std::set<std::string>& known, const std::string& where, std::string& error){
    for (auto it = o.begin(); it != o.end(); ++it){
        if (!known.count(it.key())){
            error = where + ": unknown field '" + it.key() + "'";
            return false;
        }
    }
    return true;
}

} //namespace

bool CueSystem::Parse(const json& j, const std::string& where, std::string& error){
    if (!OnlyKeys(j,{"sounds","buses","groups","cues","comment"},where,error)){
        return false;
    }

    //Sounds first, so the cues below can be checked against what is actually registered.
    std::set<std::string> declared;
    auto js = j.find("sounds");
    if (js != j.end()){
        if (!js->is_object()){
            error = where + ": 'sounds' must be an object of name: file";
            return false;
        }
        for (auto it = js->begin(); it != js->end(); ++it){
            if (!it->is_string()){
                error = where + ": sound '" + it.key() + "' must name a file";
                return false;
            }
            if (output){
                output->RegisterSound(it.key().c_str(),it->get<std::string>().c_str());
            }
            declared.insert(it.key());
        }
    }

    auto jb = j.find("buses");
    if (jb != j.end()){
        if (!jb->is_object()){
            error = where + ": 'buses' must be an object";
            return false;
        }
        for (auto it = jb->begin(); it != jb->end(); ++it){
            std::string at = where + ": bus '" + it.key() + "'";
            if (!it->is_object() || !OnlyKeys(*it,{"gain","parent","comment"},at,error)){
                if (error.empty()) error = at + " must be an object";
                return false;
            }
            Bus b;
            b.name = it.key();
            if (!Num(*it,"gain",1.0f,b.gain) || !Str(*it,"parent","master",b.parent)){
                error = at + ": gain is a number, parent a bus name";
                return false;
            }
            buses.push_back(b);
        }
        //Made parents first, whatever order they are written in - json objects come back sorted.
        std::set<std::string> made;
        std::function<bool(Bus&,int)> make = [&](Bus& b, int depth) -> bool {
            if (made.count(b.name)){
                return true;
            }
            if (depth > (int)buses.size()){
                error = where + ": bus '" + b.name + "' is its own ancestor";
                return false;
            }
            if (b.parent != "master" && !b.parent.empty()){
                Bus* parent = NULL;
                for (Bus& p : buses){
                    if (p.name == b.parent) parent = &p;
                }
                if (!parent){
                    error = where + ": bus '" + b.name + "' feeds '" + b.parent + "', which is not declared";
                    return false;
                }
                if (!make(*parent,depth + 1)){
                    return false;
                }
            }
            if (output){
                b.id = output->AddBus(b.name.c_str(),b.parent.c_str());
                if (b.id >= 0){
                    output->SetBusGain(b.id,b.gain * b.duck);
                }
            }
            made.insert(b.name);
            return true;
        };
        for (Bus& b : buses){
            if (!make(b,0)){
                return false;
            }
        }
    }

    auto jg = j.find("groups");
    if (jg != j.end()){
        if (!jg->is_object()){
            error = where + ": 'groups' must be an object";
            return false;
        }
        for (auto it = jg->begin(); it != jg->end(); ++it){
            std::string at = where + ": group '" + it.key() + "'";
            if (!it->is_object() || !OnlyKeys(*it,{"gap","duck","duck_ticks","comment"},at,error)){
                if (error.empty()) error = at + " must be an object";
                return false;
            }
            Group g;
            g.name = it.key();
            if (!Int(*it,"gap",0,g.gap) || !Int(*it,"duck_ticks",1,g.duck_ticks)){
                error = at + ": gap and duck_ticks are tick counts";
                return false;
            }
            g.duck_ticks = std::max(g.duck_ticks,1);
            auto jd = it->find("duck");
            if (jd != it->end()){
                if (!jd->is_object()){
                    error = at + ": duck must be an object of bus: gain";
                    return false;
                }
                for (auto d = jd->begin(); d != jd->end(); ++d){
                    bool f_known = false;
                    for (const Bus& b : buses){
                        f_known = f_known || (b.name == d.key());
                    }
                    if (!d->is_number() || !f_known){
                        error = at + ": duck '" + d.key() + "' must be a declared bus and a gain";
                        return false;
                    }
                    g.duck[d.key()] = d->get<float>();
                }
            }
            groups.push_back(g);
        }
    }

    auto jc = j.find("cues");
    if (jc == j.end() || !jc->is_object()){
        error = where + ": 'cues' must be an object of name: cue";
        return false;
    }
    static const std::set<std::string> CUE_KEYS = {
        "signal","begin","end","scope","when","once","chance","delay","jitter","delay_from",
        "forecast","align","sounds","no_repeat","seed","gain","gain_by","pan_by","pitch","bus",
        "looping","follow","group","priority","busy","max_wait","gap","max_instances","on_end",
        "actions","comment"
    };
    auto curve = [&](const json& o, Curve& c, const std::string& at) -> bool {
        if (!o.is_object() || !OnlyKeys(o,{"value","in","out","min","max"},at,error)){
            if (error.empty()) error = at + " must be an object";
            return false;
        }
        if (!Str(o,"value","",c.value) || !Pair(o,"in",c.in0,c.in1) || !Pair(o,"out",c.out0,c.out1) ||
            !Num(o,"min",-1e30f,c.lo) || !Num(o,"max",1e30f,c.hi)){
            error = at + ": value is a name, in and out are [a, b], min and max numbers";
            return false;
        }
        c.f_set = true;
        return true;
    };
    for (auto it = jc->begin(); it != jc->end(); ++it){
        std::string at = where + ": cue '" + it.key() + "'";
        const json& o = *it;
        if (!o.is_object()){
            error = at + " must be an object";
            return false;
        }
        if (!OnlyKeys(o,CUE_KEYS,at,error)){
            return false;
        }
        Cue c;
        c.name = it.key();
        c.key = NameKey(c.name);

        int triggers = 0;
        std::string s;
        if (o.contains("signal")){ triggers++; c.trigger = TRIGGER_SIGNAL; if (!Str(o,"signal","",c.on)) triggers = 9; }
        if (o.contains("begin")){  triggers++; c.trigger = TRIGGER_BEGIN;  if (!Str(o,"begin","",c.on))  triggers = 9; }
        if (o.contains("end")){    triggers++; c.trigger = TRIGGER_END;    if (!Str(o,"end","",c.on))    triggers = 9; }
        if (triggers != 1 || c.on.empty()){
            error = at + ": needs exactly one of signal, begin or end, naming what fires it";
            return false;
        }
        //A begin cue belongs to the scope that began; an end cue to the level, since what it
        //would belong to is the very thing that has just ended.
        if (!Str(o,"scope",c.trigger == TRIGGER_BEGIN ? c.on : "",c.scope)){
            error = at + ": scope is a name";
            return false;
        }
        if (c.trigger == TRIGGER_BEGIN && c.scope != c.on){
            error = at + ": a begin cue belongs to the scope it begins with; leave scope out";
            return false;
        }
        if (c.trigger == TRIGGER_END){
            c.scope.clear();
        }

        auto jw = o.find("when");
        if (jw != o.end()){
            if (!jw->is_array()){
                error = at + ": when is a list of [value, op, number]";
                return false;
            }
            static const char* OPS[] = { "==","!=","<","<=",">",">=" };
            for (const json& w : *jw){
                Condition cond;
                cond.op = -1;
                if (w.is_array() && w.size() == 3 && w[0].is_string() && w[1].is_string() && w[2].is_number()){
                    cond.value = w[0].get<std::string>();
                    std::string op = w[1].get<std::string>();
                    for (int k = 0; k < 6; k++){
                        if (op == OPS[k]) cond.op = k;
                    }
                    cond.rhs = w[2].get<float>();
                }
                if (cond.op < 0){
                    error = at + ": each when is [value, op, number] with op one of == != < <= > >=";
                    return false;
                }
                c.when.push_back(cond);
            }
        }

        std::string align, on_end, busy;
        float seed = -1.0f;
        if (!Bool(o,"once",false,c.f_once) || !Num(o,"chance",1.0f,c.chance) ||
            !Int(o,"delay",0,c.delay) || !Int(o,"jitter",0,c.jitter) ||
            !Str(o,"delay_from","",c.delay_from) || !Str(o,"forecast","",c.forecast) ||
            !Str(o,"align","start",align) || !Bool(o,"no_repeat",true,c.f_no_repeat) ||
            !Num(o,"seed",-1.0f,seed) || !Num(o,"gain",1.0f,c.gain) || !Num(o,"pitch",1.0f,c.pitch) ||
            !Str(o,"bus","",c.bus) || !Bool(o,"looping",false,c.f_looping) ||
            !Str(o,"group","",c.group) || !Int(o,"priority",0,c.priority) ||
            !Str(o,"busy","skip",busy) || !Int(o,"max_wait",60,c.max_wait) || !Int(o,"gap",0,c.gap) ||
            !Int(o,"max_instances",0,c.max_instances) || !Str(o,"on_end","drop",on_end)){
            error = at + ": a field has the wrong type - see the table format in CueSystem.h";
            return false;
        }
        if (seed >= 0.0f){
            c.key = (uint32_t)lroundf(seed);
        }
        if (align != "start" && align != "peak"){
            error = at + ": align is start or peak";
            return false;
        }
        c.f_align_peak = (align == "peak");
        if (on_end == "drop"){ c.on_end = END_DROP; }
        else if (on_end == "stop"){ c.on_end = END_STOP; }
        else if (on_end == "keep"){ c.on_end = END_KEEP; }
        else { error = at + ": on_end is drop, stop or keep"; return false; }
        if (busy == "skip"){ c.busy = BUSY_SKIP; }
        else if (busy == "queue"){ c.busy = BUSY_QUEUE; }
        else if (busy == "interrupt"){ c.busy = BUSY_INTERRUPT; }
        else { error = at + ": busy is skip, queue or interrupt"; return false; }
        if (c.jitter < 0 || c.gap < 0 || c.max_instances < 0 || c.max_wait < 0){
            error = at + ": jitter, gap, max_instances and max_wait cannot be negative";
            return false;
        }
        if (c.f_once && c.scope.empty() && c.trigger != TRIGGER_BEGIN){
            error = at + ": once needs a scope to be once per";
            return false;
        }
        if (!c.group.empty() && !FindGroup(c.group)){
            error = at + ": group '" + c.group + "' is not declared";
            return false;
        }
        if (!c.bus.empty() && c.bus != "master"){
            const Bus* bus = NULL;
            for (const Bus& b : buses){
                if (b.name == c.bus) bus = &b;
            }
            if (!bus){
                error = at + ": bus '" + c.bus + "' is not declared";
                return false;
            }
            c.bus_id = std::max(bus->id,0);
        }

        auto jsnd = o.find("sounds");
        if (jsnd != o.end()){
            if (jsnd->is_string()){
                c.sounds.push_back(jsnd->get<std::string>());
            }else if (jsnd->is_array()){
                for (const json& n : *jsnd){
                    if (!n.is_string()){
                        error = at + ": sounds are names";
                        return false;
                    }
                    c.sounds.push_back(n.get<std::string>());
                }
            }else{
                error = at + ": sounds is a name or a list of names";
                return false;
            }
        }
        /*
            A NAME NOBODY DECLARED is a typo, and an error. A name the table declared whose FILE
            would not load is only that one sound missing: it is silent, the log says so, and the
            rest of the table works - which is how the game treated a missing wav before the cues,
            and a missing file should not silence everything else.
        */
        for (const std::string& n : c.sounds){
            if (output && !peaks.count(n)){
                float len = output->LengthOf(n.c_str());
                if (len < 0.0f && declared.count(n)){
                    debug->Warn("%s: sound '%s' did not load - it will be silent\n",at.c_str(),n.c_str());
                    lengths[n] = 0.0f;
                    peaks[n] = 0.0f;
                    continue;
                }
                if (len < 0.0f){
                    error = at + ": no sound named '" + n + "' - declare it under \"sounds\" or register it first";
                    return false;
                }
                lengths[n] = len;
                peaks[n] = std::max(output->LoudestAt(n.c_str()),0.0f);
            }
        }
        if (!c.forecast.empty() && c.sounds.empty()){
            error = at + ": a forecast needs a sound to lead in with";
            return false;
        }

        auto jgb = o.find("gain_by");
        if (jgb != o.end()){
            if (!jgb->is_array()){
                error = at + ": gain_by is a list of curves";
                return false;
            }
            for (const json& g : *jgb){
                Curve cv;
                if (!curve(g,cv,at + ": gain_by")){
                    return false;
                }
                c.gain_by.push_back(cv);
            }
        }
        auto jpb = o.find("pan_by");
        if (jpb != o.end() && !curve(*jpb,c.pan_by,at + ": pan_by")){
            return false;
        }
        auto jf = o.find("follow");
        if (jf != o.end()){
            std::string fat = at + ": follow";
            if (!jf->is_object() || !OnlyKeys(*jf,{"value","gain","pitch"},fat,error)){
                if (error.empty()) error = fat + " must be an object";
                return false;
            }
            std::string value;
            if (!Str(*jf,"value","",value) || value.empty()){
                error = fat + " needs the value it follows";
                return false;
            }
            //Each of gain and pitch is a curve without its own value - the follow names that once.
            for (int k = 0; k < 2; k++){
                const char* part = k ? "pitch" : "gain";
                auto jp = jf->find(part);
                if (jp == jf->end()){
                    continue;
                }
                if (!jp->is_object() || jp->contains("value")){
                    error = fat + "." + part + " is { in, out, min, max } - the value is the follow's";
                    return false;
                }
                json with_value = *jp;
                with_value["value"] = value;
                if (!curve(with_value,k ? c.follow_pitch : c.follow_gain,fat + "." + part)) return false;
            }
        }
        auto ja = o.find("actions");
        if (ja != o.end()){
            if (!ja->is_array()){
                error = at + ": actions is a list";
                return false;
            }
            for (const json& a : *ja){
                std::string kind;
                int offset = 0;
                if (!a.is_object() || !Str(a,"kind","",kind) || kind.empty() || !Int(a,"offset",0,offset)){
                    error = at + ": each action is an object with a kind and an optional tick offset";
                    return false;
                }
            }
            c.actions = *ja;
        }
        if (c.sounds.empty() && c.actions.empty()){
            error = at + ": does nothing - give it sounds or actions";
            return false;
        }
        cue_index[c.name] = (int)cues.size();
        cues.push_back(c);
    }
    return true;
}

//--- Events -------------------------------------------------------------------------------------

void CueSystem::Signal(const std::string& name, const CuePayload& payload, int instance){
    Pending p;
    p.kind = 0;
    p.name = name;
    p.instance = instance;
    p.payload = payload;
    pending.push_back(p);
}

void CueSystem::BeginScope(const std::string& name, int instance, const CuePayload& payload){
    Pending p;
    p.kind = 1;
    p.name = name;
    p.instance = instance;
    p.payload = payload;
    pending.push_back(p);
}

void CueSystem::EndScope(const std::string& name, int instance){
    Pending p;
    p.kind = 2;
    p.name = name;
    p.instance = instance;
    pending.push_back(p);
}

bool CueSystem::IsScopeOpen(const std::string& name, int instance) const{
    for (const Scope& s : scopes){
        if (s.name == name && s.instance == instance){
            return true;
        }
    }
    return false;
}

void CueSystem::SetParameter(const std::string& name, float value){
    parameters[name] = value;
}

void CueSystem::SetListener(float x){
    listener_x = x;
}

void CueSystem::SetActionHandler(const std::string& kind, std::function<void(const CueAction&)> handler){
    handlers[kind] = handler;
}

std::vector<std::string> CueSystem::CueNames() const{
    std::vector<std::string> out;
    for (const Cue& c : cues){
        out.push_back(c.name);
    }
    return out;
}

float CueSystem::PeakOf(const std::string& sound) const{
    auto it = peaks.find(sound);
    return (it == peaks.end()) ? -1.0f : it->second;
}

const CueSystem::Cue* CueSystem::FindCue(const std::string& name) const{
    auto it = cue_index.find(name);
    return (it == cue_index.end()) ? NULL : &cues[it->second];
}

const CueSystem::Group* CueSystem::FindGroup(const std::string& name) const{
    for (const Group& g : groups){
        if (g.name == name){
            return &g;
        }
    }
    return NULL;
}

CueSystem::Scope* CueSystem::FindScope(const std::string& name, int instance){
    for (Scope& s : scopes){
        if (s.name == name && s.instance == instance){
            return &s;
        }
    }
    return NULL;
}

const CueSystem::Scope* CueSystem::FindScopeBySerial(uint64_t serial) const{
    for (const Scope& s : scopes){
        if (s.serial == serial){
            return &s;
        }
    }
    return NULL;
}

//--- The tick -----------------------------------------------------------------------------------

/*
    In a fixed order, and the order is part of what makes a replay print the same log:

      1. sounds whose length has run out are retired, and following sounds read their parameter;
      2. what was decided EARLIER and is due now fires, oldest decision first - EXCEPT what belongs
         to a scope that ends this tick. A scope ending on tick T means the thing it stands for is
         already over on T: the kick's last counted tick was T-1, so a swing planned for T is a
         swing after the kick, and the old hand-wired code (which tested "kick tick == N") never
         played one. Those stay waiting, and step 3 drops them;
      3. this tick's events are acted on in the order they were signalled - so a cue with no delay
         fires now, after anything that was already waiting for this tick;
      4. the ducks ease one tick toward where the speaking groups want them.
*/
void CueSystem::Tick(uint64_t tick){
    now = tick;
    Retire();

    std::vector<uint64_t> ending;
    for (const Pending& e : pending){
        const Scope* s = (e.kind == 2) ? FindScope(e.name,e.instance) : NULL;
        if (s){
            ending.push_back(s->serial);
        }
    }
    std::vector<Waiting> due;
    for (size_t i = 0; i < waiting.size();){
        bool f_ending = waiting[i].scope_serial != 0 &&
                        std::find(ending.begin(),ending.end(),waiting[i].scope_serial) != ending.end();
        if (waiting[i].due <= now && !f_ending){
            due.push_back(waiting[i]);
            waiting.erase(waiting.begin() + (long)i);
        }else{
            i++;
        }
    }
    for (Waiting& w : due){
        Fire(w);
    }

    std::vector<Pending> events;
    events.swap(pending);
    for (const Pending& e : events){
        if (e.kind == 0){
            for (const Cue& c : cues){
                if (c.trigger != TRIGGER_SIGNAL || c.on != e.name){
                    continue;
                }
                uint64_t serial = 0;
                if (!c.scope.empty()){
                    Scope* s = FindScope(c.scope,e.instance);
                    if (!s){
                        continue;   //belongs to something that is not happening
                    }
                    serial = s->serial;
                }
                Trigger(c,e.payload,e.instance,serial);
            }
        }else if (e.kind == 1){
            //A scope begun again while open is the old one ending first - a second kick pressed
            //before the first one has let go of its ticks.
            if (FindScope(e.name,e.instance)){
                EndScopeNow(e.name,e.instance);
            }
            Scope s;
            s.name = e.name;
            s.instance = e.instance;
            s.begun = now;
            s.payload = e.payload;
            s.serial = next_serial++;
            scopes.push_back(s);
            for (const Cue& c : cues){
                if (c.trigger == TRIGGER_BEGIN && c.on == e.name){
                    Trigger(c,e.payload,e.instance,s.serial);
                }
            }
        }else{
            EndScopeNow(e.name,e.instance);
        }
    }

    UpdateDucks();
}

void CueSystem::Retire(){
    for (size_t i = 0; i < playing.size();){
        Playing& p = playing[i];
        if (p.ends == UINT64_MAX || p.ends > now){
            //Still going: a following sound reads its parameter.
            if (output && p.handle){
                if (p.follow_gain.f_set){
                    auto it = parameters.find(p.follow_gain.value);
                    float v = (it == parameters.end()) ? 0.0f : it->second;
                    output->SetGain(p.handle,p.base_gain * Eval(p.follow_gain,v));
                }
                if (p.follow_pitch.f_set){
                    auto it = parameters.find(p.follow_pitch.value);
                    float v = (it == parameters.end()) ? 0.0f : it->second;
                    output->SetPitch(p.handle,p.base_pitch * Eval(p.follow_pitch,v));
                }
            }
            i++;
            continue;
        }
        if (!p.group.empty()){
            GroupHistory& gh = group_history[p.group];
            gh.last_end = std::max(gh.last_end,(int64_t)p.ends);
            p.group.clear();
        }
        /*
            A sound its scope will STOP is kept on the books after it has run out, until the scope
            ends and stops it. The log records what the game decided - the string went, so the
            creak is cut - and whether the creak had already faded by then is the sound's
            business. It no longer counts as playing for anything else.
        */
        if (p.on_end == END_STOP && p.scope_serial != 0 && FindScopeBySerial(p.scope_serial)){
            p.ends = 0;
            i++;
            continue;
        }
        playing.erase(playing.begin() + (long)i);
    }
}

float CueSystem::Value(const std::string& name, const CuePayload& payload) const{
    if (name == "distance" || name == "dx"){
        if (!payload.Has("x")){
            return 0.0f;
        }
        float dx = payload.Get("x") - listener_x;
        return (name == "dx") ? dx : fabsf(dx);
    }
    if (payload.Has(name)){
        return payload.Get(name);
    }
    auto it = parameters.find(name);
    return (it == parameters.end()) ? 0.0f : it->second;
}

float CueSystem::Eval(const Curve& c, float v) const{
    float t;
    if (c.in1 == c.in0){
        t = (v >= c.in1) ? 1.0f : 0.0f;
    }else{
        t = (v - c.in0) / (c.in1 - c.in0);
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
    }
    float out = c.out0 + (c.out1 - c.out0) * t;
    return (out < c.lo) ? c.lo : ((out > c.hi) ? c.hi : out);
}

uint64_t CueSystem::LengthTicks(const std::string& sound, float from, float pitch) const{
    auto it = lengths.find(sound);
    float len = (it == lengths.end()) ? 0.0f : it->second;
    float seconds = std::max(len - from,0.0f) / std::max(pitch,0.01f);
    return (uint64_t)ceilf(seconds * tps);
}

void CueSystem::Skip(const std::string& cue, const std::string& sound, const char* why){
    CueLogEntry e;
    e.tick = now;
    e.cue = cue;
    e.what = "skip";
    e.sound = sound;
    e.note = why;
    log.Add(e);
}

/*
    A trigger has arrived for `cue`: decide whether, which and when. Silent when the cue simply
    does not apply (a condition, a forecast not yet in reach, a once already spent); logged as a
    skip when it applied and a DRAW said no, since that is a decision someone tuning it wants
    to see.
*/
void CueSystem::Trigger(const Cue& cue, const CuePayload& payload, int instance, uint64_t scope_serial){
    for (const Condition& c : cue.when){
        float v = Value(c.value,payload);
        bool f_ok = false;
        switch (c.op){
            case 0: f_ok = (v == c.rhs); break;
            case 1: f_ok = (v != c.rhs); break;
            case 2: f_ok = (v <  c.rhs); break;
            case 3: f_ok = (v <= c.rhs); break;
            case 4: f_ok = (v >  c.rhs); break;
            case 5: f_ok = (v >= c.rhs); break;
        }
        if (!f_ok){
            return;
        }
    }
    Scope* scope = NULL;
    if (scope_serial != 0){
        for (Scope& s : scopes){
            if (s.serial == scope_serial) scope = &s;
        }
    }
    if (cue.f_once && scope &&
        std::find(scope->fired_once.begin(),scope->fired_once.end(),cue.name) != scope->fired_once.end()){
        return;
    }

    //Which sound, first: a forecast's lead and an alignment both depend on it.
    int n = (int)cue.sounds.size();
    int pick = 0;
    if (n > 1){
        float h = CueHash01(cue.key,(uint32_t)instance,now,3);
        const CueHistory& ch = history[cue.name];
        if (cue.f_no_repeat && ch.last_pick >= 0 && ch.last_pick < n){
            pick = std::min((int)(h * (float)(n - 1)),n - 2);
            if (pick >= ch.last_pick){
                pick++;
            }
        }else{
            pick = std::min((int)(h * (float)n),n - 1);
        }
    }
    auto peak_of = [&](int k) -> float {
        if (k < 0 || k >= n) return 0.0f;
        auto it = peaks.find(cue.sounds[k]);
        return (it == peaks.end()) ? 0.0f : it->second;
    };

    /*
        A FORECAST fires only once its moment is within the sound's lead: `in` ticks away is
        `in / tps` seconds, and a sound that is loudest `peak` seconds in has to start that long
        before. Inside the lead it starts now, `peak - lead` seconds in, so the peak still lands on
        the moment. Written as the archer's swoosh wrote it, product and comparison alike, so that
        the move onto a cue can be checked against a recording tick for tick.
    */
    float from = 0.0f;
    if (!cue.forecast.empty()){
        float peak = peak_of(pick);
        float lead = payload.Get(cue.forecast,1e30f) * (1.0f / tps);
        if (peak <= 0.0f || lead > peak){
            return;
        }
        from = peak - lead;
    }

    if (cue.chance < 1.0f && !(CueHash01(cue.key,(uint32_t)instance,now,1) < cue.chance)){
        Skip(cue.name,"","chance");
        return;
    }
    if (cue.f_once && scope){
        scope->fired_once.push_back(cue.name);
    }

    int delay = cue.delay;
    if (!cue.delay_from.empty()){
        delay += (int)lroundf(Value(cue.delay_from,payload));
    }
    if (cue.jitter > 0){
        int span = cue.jitter + 1;
        int draw = (int)(CueHash01(cue.key,(uint32_t)instance,now,2) * (float)span);
        delay += std::min(draw,span - 1);
    }
    //Every variant's loud part where the first one's is: authored on the first, moved for the rest.
    if (cue.f_align_peak && pick > 0){
        delay += (int)lroundf((peak_of(0) - peak_of(pick)) * tps);
    }

    Waiting w;
    w.cue = cue.name;
    w.decided = now;
    w.due = now + (uint64_t)std::max(delay,0);
    w.pick = pick;
    w.from = from;
    w.instance = instance;
    w.payload = payload;
    w.scope_serial = scope_serial;
    if (delay <= 0){
        Fire(w);
    }else{
        waiting.push_back(w);
    }
}

bool CueSystem::GroupBusy(const std::string& group, uint64_t at, const Playing** speaking) const{
    *speaking = NULL;
    for (const Playing& p : playing){
        if (p.group == group && (p.ends == UINT64_MAX || p.ends > at)){
            *speaking = &p;
            return true;
        }
    }
    const Group* g = FindGroup(group);
    auto it = group_history.find(group);
    if (g && it != group_history.end() && it->second.last_end >= 0 && (int64_t)at >= it->second.last_end &&
        (int64_t)at < it->second.last_end + g->gap){
        return true;
    }
    return false;
}

void CueSystem::Fire(Waiting& w){
    const Cue* found = FindCue(w.cue);
    if (!found){
        return;         //reloaded away
    }
    const Cue& cue = *found;
    if (w.action >= 0){
        FireAction(cue,w.action,w);
        return;
    }
    const std::string sound = cue.sounds.empty() ? std::string() : cue.sounds[w.pick];
    CueHistory& ch = history[cue.name];
    //A clock that has gone BACKWARDS - a restart, a restored replay - leaves no gap to honour.
    if (ch.last_fired > (int64_t)now){
        ch.last_fired = -1;
    }
    if (cue.gap > 0 && ch.last_fired >= 0 && (int64_t)now - ch.last_fired < cue.gap){
        Skip(cue.name,sound,"gap");
        return;
    }
    if (cue.max_instances > 0){
        int count = 0;
        for (const Playing& p : playing){
            if (p.cue == cue.name && (p.ends == UINT64_MAX || p.ends > now)){
                count++;
            }
        }
        if (count >= cue.max_instances){
            Skip(cue.name,sound,"max instances");
            return;
        }
    }
    if (!cue.group.empty()){
        const Playing* speaking = NULL;
        if (GroupBusy(cue.group,now,&speaking)){
            if (cue.busy == BUSY_QUEUE){
                if (w.queued_in_group == 0){
                    w.queued_in_group = now + (uint64_t)cue.max_wait;
                }
                if (now >= w.queued_in_group){
                    Skip(cue.name,sound,"waited too long");
                    return;
                }
                w.due = now + 1;
                waiting.push_back(w);
                return;
            }
            if (cue.busy == BUSY_INTERRUPT && speaking && cue.priority > speaking->priority){
                for (Playing& p : playing){
                    if (&p == speaking){
                        StopPlaying(p);
                        //Cut short, so its group's gap runs from now rather than from its end.
                        group_history[cue.group].last_end = (int64_t)now;
                        p.group.clear();
                        p.ends = 0;
                        p.on_end = END_DROP;
                        p.scope_serial = 0;
                    }
                }
            }else{
                Skip(cue.name,sound,"group busy");
                return;
            }
        }
    }

    //Worked out whether or not there is a sound: the cue's actions are scaled by it too.
    float cue_gain = cue.gain;
    for (const Curve& c : cue.gain_by){
        cue_gain *= Eval(c,Value(c.value,w.payload));
    }
    if (!sound.empty()){
        float gain = cue_gain;
        float pan = cue.pan_by.f_set ? Eval(cue.pan_by,Value(cue.pan_by.value,w.payload)) : 0.0f;
        pan = (pan < -1.0f) ? -1.0f : ((pan > 1.0f) ? 1.0f : pan);
        //A following sound STARTS where its parameter says, rather than a tick late.
        float base_gain = gain;
        float pitch = cue.pitch;
        if (cue.follow_gain.f_set){
            gain *= Eval(cue.follow_gain,Value(cue.follow_gain.value,CuePayload()));
        }
        if (cue.follow_pitch.f_set){
            pitch *= Eval(cue.follow_pitch,Value(cue.follow_pitch.value,CuePayload()));
        }

        CueLogEntry e;
        e.tick = now;
        e.cue = cue.name;
        e.what = "play";
        e.sound = sound;
        e.gain = gain;
        e.pitch = pitch;
        e.pan = pan;
        e.from = w.from;
        log.Add(e);

        Playing p;
        p.cue = cue.name;
        p.sound = sound;
        p.started = now;
        p.ends = cue.f_looping ? UINT64_MAX : now + LengthTicks(sound,w.from,pitch);
        p.scope_serial = w.scope_serial;
        p.on_end = cue.on_end;
        p.group = cue.group;
        p.priority = cue.priority;
        p.follow_gain = cue.follow_gain;
        p.follow_pitch = cue.follow_pitch;
        p.base_gain = base_gain;
        p.base_pitch = cue.pitch;
        if (output){
            CuePlay play;
            play.gain = gain;
            play.pitch = pitch;
            play.pan = pan;
            play.bus = cue.bus_id;
            play.f_looping = cue.f_looping;
            play.from = w.from;
            p.handle = output->Play(sound.c_str(),play);
        }
        playing.push_back(p);
    }
    ch.last_fired = (int64_t)now;
    ch.last_pick = w.pick;

    w.gain = cue_gain;
    for (int i = 0; i < (int)cue.actions.size(); i++){
        int offset = 0;
        const json& a = cue.actions[i];
        auto it = a.find("offset");
        if (it != a.end() && it->is_number()){
            offset = (int)lroundf(it->get<float>());
        }
        if (offset <= 0){
            FireAction(cue,i,w);
        }else{
            Waiting later = w;
            later.action = i;
            later.due = now + (uint64_t)offset;
            waiting.push_back(later);
        }
    }
}

void CueSystem::FireAction(const Cue& cue, int action, const Waiting& w){
    if (action < 0 || action >= (int)cue.actions.size()){
        return;
    }
    CueAction a;
    a.params = cue.actions[action];
    a.kind = a.params.value("kind",std::string());
    a.cue = cue.name;
    a.tick = now;
    a.gain = w.gain;
    a.payload = w.payload;

    CueLogEntry e;
    e.tick = now;
    e.cue = cue.name;
    e.what = "act";
    e.sound = a.kind;
    e.gain = a.gain;
    auto h = handlers.find(a.kind);
    if (h == handlers.end()){
        e.note = "no handler";
    }
    log.Add(e);
    if (h != handlers.end() && h->second){
        h->second(a);
    }
}

void CueSystem::StopPlaying(Playing& p){
    CueLogEntry e;
    e.tick = now;
    e.cue = p.cue;
    e.what = "stop";
    e.sound = p.sound;
    log.Add(e);
    if (output && p.handle){
        output->Stop(p.handle);
    }
    p.handle = 0;
}

/*
    A scope ending, by whatever rule each of its cues has. Its own cues first - what it started
    is stopped or let ring, what it planned is dropped or kept - and only then the cues that
    fire ON its end, so a release sound is never cut by the release.
*/
void CueSystem::EndScopeNow(const std::string& name, int instance){
    Scope* s = FindScope(name,instance);
    if (!s){
        return;
    }
    uint64_t serial = s->serial;
    CuePayload payload = s->payload;

    for (size_t i = 0; i < waiting.size();){
        Waiting& w = waiting[i];
        if (w.scope_serial != serial){
            i++;
            continue;
        }
        const Cue* c = FindCue(w.cue);
        if (c && c->on_end == END_KEEP){
            w.scope_serial = 0;
            i++;
            continue;
        }
        std::string what = (c && w.action < 0 && !c->sounds.empty()) ? c->sounds[w.pick] : std::string();
        if (c && w.action >= 0){
            what = c->actions[w.action].value("kind",std::string());
        }
        Skip(w.cue,what,"scope ended");
        waiting.erase(waiting.begin() + (long)i);
    }
    for (size_t i = 0; i < playing.size();){
        Playing& p = playing[i];
        if (p.scope_serial != serial){
            i++;
            continue;
        }
        if (p.on_end == END_STOP){
            StopPlaying(p);
            if (!p.group.empty()){
                group_history[p.group].last_end = (int64_t)now;
            }
            playing.erase(playing.begin() + (long)i);
            continue;
        }
        p.scope_serial = 0;         //rings on, belonging to nothing
        i++;
    }
    for (size_t i = 0; i < scopes.size(); i++){
        if (scopes[i].serial == serial){
            scopes.erase(scopes.begin() + (long)i);
            break;
        }
    }
    for (const Cue& c : cues){
        if (c.trigger == TRIGGER_END && c.on == name){
            Trigger(c,payload,instance,0);
        }
    }
}

void CueSystem::Reset(){
    pending.clear();
    //Newest first, so a scope opened inside another ends before it.
    while (!scopes.empty()){
        Scope last = scopes.back();
        EndScopeNow(last.name,last.instance);
    }
    for (const Waiting& w : waiting){
        Skip(w.cue,"","reset");
    }
    waiting.clear();
    for (Playing& p : playing){
        p.group.clear();
    }
    for (Bus& b : buses){
        b.duck = 1.0f;
        if (output && b.id >= 0){
            output->SetBusGain(b.id,b.gain);
        }
    }
}

/*
    Each bus eases toward the product of what every SPEAKING group asks of it. `duck_ticks` is
    how long the WHOLE duck takes, down or back up - so a group ducking to 0.25 over 10 ticks
    moves 0.075 a tick, not a tenth of full scale, which would arrive early and read as a lurch.
    With several groups on one bus, the slowest rate among them wins, so a narrator's slow duck is
    not rushed by a quick one's. Set on the bus only when it moved; SoundSystem smooths each step,
    so a ramp set a tick at a time is continuous.
*/
void CueSystem::UpdateDucks(){
    for (Bus& b : buses){
        float target = 1.0f;
        float step = 1.0f;
        for (const Group& g : groups){
            auto d = g.duck.find(b.name);
            if (d == g.duck.end()){
                continue;
            }
            bool f_speaking = false;
            for (const Playing& p : playing){
                f_speaking = f_speaking || (p.group == g.name && (p.ends == UINT64_MAX || p.ends > now));
            }
            if (f_speaking){
                target *= d->second;
            }
            float rate = fabsf(1.0f - d->second) / (float)g.duck_ticks;
            if (rate > 0.0f){
                step = std::min(step,rate);
            }
        }
        float before = b.duck;
        if (b.duck < target){
            b.duck = std::min(b.duck + step,target);
        }else if (b.duck > target){
            b.duck = std::max(b.duck - step,target);
        }
        if (b.duck != before && output && b.id >= 0){
            output->SetBusGain(b.id,b.gain * b.duck);
        }
    }
}

//--- History ------------------------------------------------------------------------------------

json CueSystem::CaptureHistory() const{
    json cj = json::object();
    for (const auto& h : history){
        if (h.second.last_pick >= 0 || h.second.last_fired >= 0){
            cj[h.first] = json::array({h.second.last_pick,h.second.last_fired});
        }
    }
    json gj = json::object();
    for (const auto& g : group_history){
        if (g.second.last_end >= 0){
            gj[g.first] = g.second.last_end;
        }
    }
    return json{ {"cues",cj}, {"groups",gj} };
}

void CueSystem::RestoreHistory(const json& h){
    history.clear();
    group_history.clear();
    if (!h.is_object()){
        return;
    }
    auto cj = h.find("cues");
    if (cj != h.end() && cj->is_object()){
        for (auto it = cj->begin(); it != cj->end(); ++it){
            if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number()){
                CueHistory ch;
                ch.last_pick = (*it)[0].get<int>();
                ch.last_fired = (*it)[1].get<int64_t>();
                history[it.key()] = ch;
            }
        }
    }
    auto gj = h.find("groups");
    if (gj != h.end() && gj->is_object()){
        for (auto it = gj->begin(); it != gj->end(); ++it){
            if (it->is_number()){
                group_history[it.key()].last_end = it->get<int64_t>();
            }
        }
    }
}
