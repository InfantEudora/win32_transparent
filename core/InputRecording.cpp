#include "InputRecording.h"
#include "Debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sstream>
#include <fstream>
#include <algorithm>

static Debugger* debug = new Debugger("InputRecording",DEBUG_INFO);

//--- Key names ----------------------------------------------------------------------------------

struct KeyName{
    uint32_t    code;
    const char* name;
};

//Pad buttons by their Xbox names. Portable: GAMEPAD_KEY_* are the same numbers on every platform,
//which is the point of the XINPUT_GAMEPAD_* fallbacks in InputController.h.
static const KeyName pad_names[] = {
    {GAMEPAD_KEY_DPAD_UP,       "PAD_UP"},
    {GAMEPAD_KEY_DPAD_DOWN,     "PAD_DOWN"},
    {GAMEPAD_KEY_DPAD_LEFT,     "PAD_LEFT"},
    {GAMEPAD_KEY_DPAD_RIGHT,    "PAD_RIGHT"},
    {GAMEPAD_KEY_START,         "PAD_START"},
    {GAMEPAD_KEY_BACK,          "PAD_BACK"},
    {GAMEPAD_KEY_LEFT_THUMB,    "PAD_L3"},
    {GAMEPAD_KEY_RIGHT_THUMB,   "PAD_R3"},
    {GAMEPAD_KEY_LEFT_SHOULDER, "PAD_L1"},
    {GAMEPAD_KEY_RIGHT_SHOULDER,"PAD_R1"},
    {GAMEPAD_KEY_A,             "PAD_A"},
    {GAMEPAD_KEY_B,             "PAD_B"},
    {GAMEPAD_KEY_X,             "PAD_X"},
    {GAMEPAD_KEY_Y,             "PAD_Y"},
};

//The keys with no printable character. Win32 only because VK_ codes are; elsewhere these come out
//as hex, which still replays - the names are for reading, not for correctness.
#if defined(_WIN32)
static const KeyName vk_names[] = {
    {VK_SPACE,"SPACE"}, {VK_RETURN,"RETURN"}, {VK_ESCAPE,"ESCAPE"}, {VK_TAB,"TAB"},
    {VK_BACK,"BACKSPACE"},
    {VK_LEFT,"LEFT"}, {VK_RIGHT,"RIGHT"}, {VK_UP,"UP"}, {VK_DOWN,"DOWN"},
    {VK_HOME,"HOME"}, {VK_END,"END"}, {VK_PRIOR,"PAGEUP"}, {VK_NEXT,"PAGEDOWN"},
    {VK_INSERT,"INSERT"}, {VK_DELETE,"DELETE"}, {VK_PAUSE,"PAUSE"},
    {VK_LSHIFT,"LSHIFT"}, {VK_RSHIFT,"RSHIFT"}, {VK_LCONTROL,"LCTRL"}, {VK_RCONTROL,"RCTRL"},
    {VK_LBUTTON,"MOUSE_LEFT"}, {VK_MBUTTON,"MOUSE_MIDDLE"}, {VK_RBUTTON,"MOUSE_RIGHT"},
    {VK_F1,"F1"}, {VK_F2,"F2"}, {VK_F3,"F3"}, {VK_F4,"F4"}, {VK_F5,"F5"}, {VK_F6,"F6"},
    {VK_F7,"F7"}, {VK_F8,"F8"}, {VK_F9,"F9"}, {VK_F10,"F10"}, {VK_F11,"F11"}, {VK_F12,"F12"},
};
#endif

std::string FormatSystemKey(uint32_t code){
    for (const KeyName& k: pad_names){
        if (k.code == code){
            return k.name;
        }
    }
#if defined(_WIN32)
    for (const KeyName& k: vk_names){
        if (k.code == code){
            return k.name;
        }
    }
    //Letters and digits are their own VK_ codes, so they can be written as themselves.
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')){
        return std::string(1,(char)code);
    }
#endif
    char buf[16];
    snprintf(buf,sizeof(buf),"0x%X",code);
    return buf;
}

bool ParseSystemKey(const std::string& text, uint32_t& out){
    if (text.empty()){
        return false;
    }
    for (const KeyName& k: pad_names){
        if (text == k.name){
            out = k.code;
            return true;
        }
    }
#if defined(_WIN32)
    for (const KeyName& k: vk_names){
        if (text == k.name){
            out = k.code;
            return true;
        }
    }
    if (text.size() == 1){
        char c = text[0];
        if (c >= 'a' && c <= 'z'){
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')){
            out = (uint32_t)c;
            return true;
        }
    }
#endif
    char* end = NULL;
    unsigned long v = strtoul(text.c_str(),&end,0);   //base 0: 0x.. hex or plain decimal
    if (end && *end == '\0'){
        out = (uint32_t)v;
        return true;
    }
    return false;
}

//--- Events -------------------------------------------------------------------------------------

static const char* EventWord(uint16_t type){
    switch (type){
        case INPUT_EVENT_KEY_DOWN:      return "down";
        case INPUT_EVENT_KEY_UP:        return "up";
        case INPUT_EVENT_AXIS_SCALAR:   return "axis";
        case INPUT_EVENT_AXIS_ABSOLUTE: return "abs";
        case INPUT_EVENT_AXIS_RELATIVE: return "rel";
        default:                        return NULL;
    }
}

static uint16_t EventType(const std::string& word){
    if (word == "down"){ return INPUT_EVENT_KEY_DOWN; }
    if (word == "up"){   return INPUT_EVENT_KEY_UP; }
    if (word == "axis"){ return INPUT_EVENT_AXIS_SCALAR; }
    if (word == "abs"){  return INPUT_EVENT_AXIS_ABSOLUTE; }
    if (word == "rel"){  return INPUT_EVENT_AXIS_RELATIVE; }
    return INPUT_EVENT_NONE;
}

static std::string ActionText(uint32_t mapped, const InputController* names){
    const char* name = names ? names->GetActionName(mapped) : NULL;
    return name ? std::string(name) : std::to_string(mapped);
}

//--- Save ---------------------------------------------------------------------------------------

bool InputRecording::Save(const std::string& path, const InputController* names, std::string& error) const{
    FILE* f = fopen(path.c_str(),"wb");
    if (!f){
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    fprintf(f,"input_recording %d\n",version);
    fprintf(f,"# Recorded input. One event per line: tick, event, action, value. Ticks count from\n");
    fprintf(f,"# the start of the recording. Only ticks begin..end replay: raise begin to cut the\n");
    fprintf(f,"# lead-in, lower end to cut the tail. Keys held across a cut stay held; everything\n");
    fprintf(f,"# is released at end. See core/InputRecording.h.\n");
    fprintf(f,"app %s\n",app.c_str());
    fprintf(f,"scene %s\n",scene.c_str());
    fprintf(f,"tick_rate %g\n",tick_rate);
    fprintf(f,"recorded %s\n",recorded_at.c_str());
    fprintf(f,"begin %u\n",begin);
    fprintf(f,"end %u\n",end);
    //One line, so it can be edited like the rest - dump() with no indent never emits a newline.
    fprintf(f,"state %s\n",state.dump().c_str());
    if (!events.empty()){
        //The first and last events, as the person trimming wants them: the idle lead-in is
        //everything before the first, the idle tail everything after the last.
        fprintf(f,"# events at ticks %u..%u of %u\n",events.front().tick,events.back().tick,end);
    }else{
        fprintf(f,"# no events\n");
    }
    fprintf(f,"# tick  event  action  value\n");
    for (const RecordedInputEvent& r: events){
        const char* word = EventWord(r.event.type);
        if (!word){
            continue;
        }
        std::string action = ActionText(r.event.mapped_keycode,names);
        switch (r.event.type){
            case INPUT_EVENT_KEY_DOWN:
            case INPUT_EVENT_KEY_UP:
                if (r.event.value != 0){
                    fprintf(f,"%u %s %s key=%s\n",r.tick,word,action.c_str(),
                            FormatSystemKey((uint32_t)r.event.value).c_str());
                }else{
                    fprintf(f,"%u %s %s\n",r.tick,word,action.c_str());
                }
            break;
            case INPUT_EVENT_AXIS_SCALAR:
                //%.9g round-trips a float exactly, and exactness is the whole point: a stick value
                //one ulp off is a different run.
                fprintf(f,"%u %s %s %.9g\n",r.tick,word,action.c_str(),r.event.fvalue);
            break;
            default:
                fprintf(f,"%u %s %s %d\n",r.tick,word,action.c_str(),r.event.value);
            break;
        }
    }
    bool f_ok = (ferror(f) == 0);
    fclose(f);
    if (!f_ok){
        error = "write to '" + path + "' failed";
    }
    return f_ok;
}

//--- Load ---------------------------------------------------------------------------------------

static std::string Trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos){
        return "";
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a,b - a + 1);
}

bool InputRecording::Load(const std::string& path, const InputController* names, std::string& error){
    std::ifstream in(path.c_str(),std::ios::binary);
    if (!in){
        error = "cannot open '" + path + "'";
        return false;
    }
    *this = InputRecording();
    bool f_magic = false;
    bool f_have_end = false;
    std::string line;
    int line_no = 0;
    while (std::getline(in,line)){
        line_no++;
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#'){
            continue;
        }
        std::string where = path + ":" + std::to_string(line_no) + ": ";

        if (isdigit((unsigned char)t[0])){
            //An event. A trailing comment is allowed, so a person can annotate a line.
            size_t hash = t.find('#');
            if (hash != std::string::npos){
                t = Trim(t.substr(0,hash));
            }
            std::istringstream ss(t);
            std::string tick_s, word, action_s, value_s;
            ss >> tick_s >> word >> action_s >> value_s;
            RecordedInputEvent r;
            r.tick = (uint32_t)strtoul(tick_s.c_str(),NULL,10);
            r.event.type = EventType(word);
            if (r.event.type == INPUT_EVENT_NONE){
                error = where + "unknown event '" + word + "'";
                return false;
            }
            uint32_t mapped = names ? names->FindActionByName(action_s.c_str()) : (uint32_t)INPUT_NONE;
            if (mapped == INPUT_NONE){
                char* end = NULL;
                unsigned long v = strtoul(action_s.c_str(),&end,10);
                if (action_s.empty() || !end || *end != '\0'){
                    error = where + "unknown action '" + action_s + "'";
                    return false;
                }
                mapped = (uint32_t)v;
            }
            r.event.mapped_keycode = (uint16_t)mapped;
            if (r.event.type == INPUT_EVENT_KEY_DOWN || r.event.type == INPUT_EVENT_KEY_UP){
                if (!value_s.empty()){
                    if (value_s.compare(0,4,"key=") != 0){
                        error = where + "expected key=..., got '" + value_s + "'";
                        return false;
                    }
                    uint32_t code = 0;
                    if (!ParseSystemKey(value_s.substr(4),code)){
                        error = where + "unknown key '" + value_s.substr(4) + "'";
                        return false;
                    }
                    r.event.value = (int32_t)code;
                }
            }else if (r.event.type == INPUT_EVENT_AXIS_SCALAR){
                r.event.fvalue = strtof(value_s.c_str(),NULL);
            }else{
                r.event.value = (int32_t)strtol(value_s.c_str(),NULL,10);
            }
            events.push_back(r);
            continue;
        }

        //A header line: a key, then the rest of the line as its value.
        size_t space = t.find_first_of(" \t");
        std::string key = t.substr(0,space);
        std::string value = (space == std::string::npos) ? "" : Trim(t.substr(space));
        if (key == "input_recording"){
            f_magic = true;
            version = atoi(value.c_str());
        }else if (key == "app"){
            app = value;
        }else if (key == "scene"){
            scene = value;
        }else if (key == "tick_rate"){
            tick_rate = strtof(value.c_str(),NULL);
        }else if (key == "recorded"){
            recorded_at = value;
        }else if (key == "begin"){
            begin = (uint32_t)strtoul(value.c_str(),NULL,10);
        }else if (key == "end"){
            end = (uint32_t)strtoul(value.c_str(),NULL,10);
            f_have_end = true;
        }else if (key == "state"){
            state = nlohmann::json::parse(value,nullptr,false);
            if (state.is_discarded()){
                error = where + "state is not valid JSON";
                return false;
            }
        }else{
            //Unknown header keys are kept quiet rather than fatal, so a newer file with an extra
            //field still replays here.
            debug->Warn("%signoring header '%s'\n",where.c_str(),key.c_str());
        }
    }
    if (!f_magic){
        error = "'" + path + "' is not an input recording (no 'input_recording' line)";
        return false;
    }
    std::stable_sort(events.begin(),events.end(),
        [](const RecordedInputEvent& a, const RecordedInputEvent& b){ return a.tick < b.tick; });
    //A file with no end - written by hand, say - plays one tick past its last event.
    if (!f_have_end){
        end = events.empty() ? 0 : events.back().tick + 1;
    }
    if (begin > end){
        error = "begin (" + std::to_string(begin) + ") is after end (" + std::to_string(end) + ")";
        return false;
    }
    return true;
}
