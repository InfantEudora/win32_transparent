/*
    core/Settings and core/UIMenu's logic, with no window, no GL and no engine - docs/menu_plan.md.
    Built and run by apps/archer's `make rules`:

        g++ -std=c++17 -I core -I 3rdparty tools/menu_test.cpp core/Settings.cpp core/UIMenu.cpp

    SETTINGS: defaults, ranges, a round trip through the file, a forgiving load of a bad one, keys
    it does not know kept, and the revision moving only on a real change.
    MENU: focus and wrap, disabled items skipped, values stepped and clamped, confirm per kind,
    Back reported, and the pointer - hover only on a move, press-and-release on the same item to
    activate, a press dragged off cancelling, a slider following a drag, a choice clicked on
    either half - all against the layout's own rects, which is what the drawing uses.
*/
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Settings.h"
#include "UIMenu.h"

static int g_checks = 0, g_failures = 0;
static void Check(bool f_ok, const char* what, const char* detail = NULL){
    g_checks++;
    if (f_ok){
        printf("  ok    %s\n",what);
        return;
    }
    g_failures++;
    printf("  FAIL  %s%s%s\n",what,detail ? " - " : "",detail ? detail : "");
}

static std::string TempFile(const char* name){
    const char* t = getenv("TEMP");
    return std::string(t ? t : ".") + "/" + name;
}

static void WriteText(const std::string& path, const char* text){
    FILE* f = fopen(path.c_str(),"wb");
    if (f){
        fputs(text,f);
        fclose(f);
    }
}

static void Declare(Settings& s){
    s.DeclareBool("fullscreen",false);
    s.DeclareInt("msaa",4,1,16);
    s.DeclareFloat("volume_master",0.8f,0.0f,1.0f);
}

static void TestSettings(){
    printf("settings\n");
    char d[200];
    const std::string path = TempFile("menu_test_settings.json");
    remove(path.c_str());
    Settings s;
    Declare(s);
    std::string report;
    Check(!s.Load(path,&report) && report.empty(),"a missing file is a first run: no file, nothing to report");
    Check(!s.GetBool("fullscreen") && s.GetInt("msaa") == 4 && s.GetFloat("volume_master") == 0.8f,"and the defaults");
    uint32_t r0 = s.Revision();
    Check(!s.SetInt("msaa",4) && s.Revision() == r0,"setting the same value changes nothing, revision included");
    Check(s.SetInt("msaa",99) && s.GetInt("msaa") == 16,"a value past the range is clamped to it");
    Check(s.Revision() != r0,"and a real change moves the revision");
    Check(!s.SetBool("msaa",true) && !s.SetFloat("nope",1.0f),"a wrong type or an undeclared key is refused");
    s.SetBool("fullscreen",true);
    s.SetFloat("volume_master",0.3f);
    Check(s.Save(),"it saves beside where it loaded from");

    Settings t;
    Declare(t);
    report.clear();
    bool f_loaded = t.Load(path,&report);
    snprintf(d,sizeof(d),"fullscreen %d msaa %d master %.3f, report '%s'",(int)t.GetBool("fullscreen"),t.GetInt("msaa"),
             t.GetFloat("volume_master"),report.c_str());
    Check(f_loaded && report.empty() && t.GetBool("fullscreen") && t.GetInt("msaa") == 16 &&
          std::fabs(t.GetFloat("volume_master") - 0.3f) < 1e-6f,"and loads back what it saved",d);

    //A player's edits, some of them wrong.
    WriteText(path,"{ \"fullscreen\": \"yes\", \"msaa\": 1000, \"volume_master\": 0.5, \"from_a_newer_build\": [1,2] }");
    Settings u;
    Declare(u);
    report.clear();
    u.Load(path,&report);
    snprintf(d,sizeof(d),"fullscreen %d msaa %d master %.2f; report:\n%s",(int)u.GetBool("fullscreen"),u.GetInt("msaa"),
             u.GetFloat("volume_master"),report.c_str());
    Check(!u.GetBool("fullscreen") && u.GetInt("msaa") == 16 && u.GetFloat("volume_master") == 0.5f,
          "a wrong type takes its default, an out-of-range number is clamped, the rest load",d);
    Check(report.find("fullscreen") != std::string::npos && report.find("msaa") != std::string::npos,
          "and both are named in the report",d);
    u.Save();
    std::string text = u.ToText();
    Check(text.find("\"from_a_newer_build\": [1,2]") != std::string::npos,"a key this build does not know is kept",text.c_str());

    WriteText(path,"not json {");
    Settings v;
    Declare(v);
    report.clear();
    Check(!v.Load(path,&report) && !report.empty() && v.GetInt("msaa") == 4,"an unparsable file is the defaults, and said so");
    remove(path.c_str());
}

//A menu laid out on a 1000 x 800 screen, as archer's settings screen is shaped.
static const float W = 1000.0f, H = 800.0f;
static UIMenuInput Keys(){
    UIMenuInput in;
    in.screen_w = W;
    in.screen_h = H;
    return in;
}
static UIMenuInput Pointer(float x, float y, bool f_press, bool f_down, bool f_release){
    UIMenuInput in = Keys();
    in.f_pointer = true;
    in.pointer_x = x;
    in.pointer_y = y;
    in.pointer_pressed = f_press;
    in.pointer_down = f_down;
    in.pointer_released = f_release;
    return in;
}

static void TestMenu(){
    printf("menu\n");
    char d[200];
    UIMenu m;
    int start = m.AddButton("Start",1);
    int msaa = m.AddChoice("MSAA",{ "Off", "4x", "16x" },1,2);
    int vol = m.AddSlider("Master",0.8f,0.0f,1.0f,0.1f,3);
    int full = m.AddToggle("Full screen",false,4);
    int gone = m.AddButton("Disabled",5);
    int quit = m.AddButton("Quit",6);
    m.SetEnabled(gone,false);
    Check(m.Count() == 6 && m.Focus() == start,"items in order, focus on the first");

    //Up and down, wrapping, past the disabled one.
    UIMenuInput in = Keys();
    in.up = true;
    m.Update(in);
    Check(m.Focus() == quit,"up from the top wraps to the bottom");
    in = Keys();
    in.up = true;
    m.Update(in);
    Check(m.Focus() == full,"and up again skips the disabled item");
    in = Keys();
    in.down = true;
    m.Update(in);
    in = Keys();
    in.down = true;
    m.Update(in);
    Check(m.Focus() == start,"down wraps back to the top");

    //Values.
    m.SetFocus(msaa);
    in = Keys();
    in.right = true;
    UIMenuResult r = m.Update(in);
    Check(r.event == UI_MENU_CHANGED && r.id == 2 && m.Item(msaa).choice == 2,"right steps a choice, and says which item changed");
    r = m.Update(in);
    Check(r.event == UI_MENU_NONE && m.Item(msaa).choice == 2,"and clamps at its last value");
    in = Keys();
    in.confirm = true;
    r = m.Update(in);
    Check(r.event == UI_MENU_CHANGED && m.Item(msaa).choice == 0,"confirm steps it round, wrapping");
    m.SetFocus(vol);
    for (int i = 0; i < 5; i++){
        in = Keys();
        in.right = true;
        m.Update(in);
    }
    snprintf(d,sizeof(d),"%.6f",m.Item(vol).value);
    Check(m.Item(vol).value == 1.0f,"a slider steps on its grid and stops at the top, exactly",d);
    for (int i = 0; i < 12; i++){
        in = Keys();
        in.left = true;
        m.Update(in);
    }
    Check(m.Item(vol).value == 0.0f,"and at the bottom");
    m.SetFocus(full);
    in = Keys();
    in.confirm = true;
    r = m.Update(in);
    Check(r.event == UI_MENU_CHANGED && m.Item(full).f_on,"confirm flips a toggle");
    m.SetFocus(start);
    r = m.Update(in);
    Check(r.event == UI_MENU_ACTIVATED && r.id == 1,"and activates a button");
    in = Keys();
    in.back = true;
    r = m.Update(in);
    Check(r.event == UI_MENU_BACK,"back is reported, not acted on");
    in = Keys();
    in.left = true;
    r = m.Update(in);
    Check(r.event == UI_MENU_NONE && m.Focus() == start,"left on a button does nothing");

    //The pointer, against the layout's own rects.
    auto mid = [&m](int i){ UIMenuRect q = m.ItemRect(i,W,H); return std::make_pair((q.x0 + q.x1) * 0.5f,(q.y0 + q.y1) * 0.5f); };
    auto p = mid(quit);
    m.Update(Pointer(p.first,p.second,false,false,false));
    Check(m.Focus() == quit,"moving the pointer over an item focuses it");
    in = Keys();
    in.up = true;
    m.Update(in);
    m.Update(Pointer(p.first,p.second,false,false,false));
    Check(m.Focus() == full,"but a pointer that does not move does not take the focus back from the keys");
    auto g = mid(gone);
    m.Update(Pointer(g.first,g.second,false,false,false));
    Check(m.Focus() == full,"a disabled item takes no hover");
    r = m.Update(Pointer(p.first,p.second,true,true,false));
    Check(r.event == UI_MENU_NONE && m.Pressed() == quit,"a press holds a button down without activating it");
    r = m.Update(Pointer(p.first,p.second,false,false,true));
    Check(r.event == UI_MENU_ACTIVATED && r.id == 6 && m.Pressed() < 0,"and the release over it activates it");
    m.Update(Pointer(p.first,p.second,true,true,false));
    r = m.Update(Pointer(p.first,p.second + 200.0f,false,false,true));
    Check(r.event == UI_MENU_NONE && m.Pressed() < 0,"a press dragged off before the release cancels");
    r = m.Update(Pointer(-100.0f,-100.0f,true,true,false));
    Check(r.event == UI_MENU_NONE && m.Pressed() < 0,"a press on nothing is nothing");
    m.Update(Pointer(-100.0f,-100.0f,false,false,true));

    //The slider by the mouse: press 0.3 along the track, drag past its end, release.
    UIMenuRect track = m.TrackRect(vol,W,H);
    const float vy = (track.y0 + track.y1) * 0.5f;
    r = m.Update(Pointer(track.x0 + (track.x1 - track.x0) * 0.31f,vy,true,true,false));
    snprintf(d,sizeof(d),"%.3f",m.Item(vol).value);
    Check(r.event == UI_MENU_CHANGED && std::fabs(m.Item(vol).value - 0.3f) < 1e-5f,"a press on a slider's track sets it there, snapped to its step",d);
    r = m.Update(Pointer(track.x1 + 300.0f,vy + 150.0f,false,true,false));
    Check(r.event == UI_MENU_CHANGED && m.Item(vol).value == 1.0f,"and it follows a drag, even off its row, to the end");
    m.Update(Pointer(track.x1 + 300.0f,vy + 150.0f,false,false,true));
    Check(m.Pressed() < 0,"the release lets go");

    //A choice by the mouse: the left half of its value steps back, the right on.
    UIMenuRect v = m.ValueRect(msaa,W,H);
    const float cy = (v.y0 + v.y1) * 0.5f;
    m.SetChoice(msaa,1);
    m.Update(Pointer(v.x0 + 2.0f,cy,true,true,false));
    r = m.Update(Pointer(v.x0 + 2.0f,cy,false,false,true));
    Check(r.event == UI_MENU_CHANGED && m.Item(msaa).choice == 0,"a click on a choice's left half steps it back");
    m.Update(Pointer(v.x1 - 2.0f,cy,true,true,false));
    r = m.Update(Pointer(v.x1 - 2.0f,cy,false,false,true));
    Check(r.event == UI_MENU_CHANGED && m.Item(msaa).choice == 1,"its right half on");
    auto f = mid(full);
    m.Update(Pointer(f.first,f.second,true,true,false));
    r = m.Update(Pointer(f.first,f.second,false,false,true));
    Check(r.event == UI_MENU_CHANGED && !m.Item(full).f_on,"a click flips a toggle");

    //The pointer ignored when the app says it may not be used.
    UIMenuInput off = Pointer(p.first,p.second,true,true,false);
    off.f_pointer = false;
    r = m.Update(off);
    Check(r.event == UI_MENU_NONE && m.Pressed() < 0,"and with f_pointer false the pointer does nothing");

    //Rows do not overlap and stay in order, at any size.
    bool f_ordered = true;
    for (float sh : { 600.0f, 1080.0f, 2160.0f }){
        for (int i = 1; i < m.Count(); i++){
            f_ordered = f_ordered && m.ItemRect(i,sh * 1.78f,sh).y0 >= m.ItemRect(i - 1,sh * 1.78f,sh).y1;
        }
    }
    Check(f_ordered,"rows are stacked without overlapping at 600, 1080 and 2160 high");
}

int main(){
    printf("--- core settings + menu ---\n");
    TestSettings();
    TestMenu();
    printf("\n%d checks, %d failed\n",g_checks,g_failures);
    return g_failures ? 1 : 0;
}
