/*
    Headless checks for core/CueSystem - step 2 of apps/archer/cue_plan.md.

    Two kinds. Most run against a FAKE output that records what it was asked to do, so the
    table's logic - triggers, scopes, delays, draws, groups, history - is checked by reading the
    log, with no audio anywhere. The PARITY checks then replay the archer's hand-wired kick shout,
    kick swing and arrow swoosh exactly as ApplicationArcher::UpdateSound and StartArrowSwooshes
    wrote them, beside the cue rows that are meant to replace them, over thousands of kicks and
    flights - so step 3 starts from rows already shown to decide what the old code decided. Last,
    one check drives the real SoundSystem offline through CueSoundOutput, to see a duck in the mix.

    Build (from the repo root; it compiles what it uses and needs no core objects):

        export PATH="/c/msys64/mingw64/bin:$PATH"
        g++ -std=c++17 -O2 -fno-exceptions -DJSON_NOEXCEPTION -DUSE_SOUND -D_WIN32 \
            -Icore -Icore/physics -Icore/skeleton -I3rdparty -I3rdparty/imgui \
            -I3rdparty/stb_image -I3rdparty/miniz -I3rdparty/reactphysics3d -Iapps/archer \
            tools/cue_test.cpp core/CueSystem.cpp core/SoundSystem.cpp core/WaveFile.cpp \
            core/File.cpp core/Debug.cpp core/Debug_win32.cpp core/BinaryAsset.cpp \
            BinaryAssetMemoryEmpty.cpp -Llibs -lthirdparty -lole32 -luser32 \
            -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -o cue_test.exe

        ./cue_test.exe

    Silent: the one SoundSystem check is offline. Exit code 0 = all checks passed.
*/
#include "CueSystem.h"
#include "CueSoundOutput.h"
#include "File.h"
#include "PlaceHash.h"      //the archer's Hash01, which the parity checks reproduce

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

static int failures = 0;

static void check(bool ok, const char* what, const std::string& detail = "") {
    printf("  %-58s %s %s\n", what, ok ? "PASS" : "FAIL", detail.c_str());
    if (!ok) failures++;
}

// Records every call; sounds have made-up lengths and peaks.
struct FakeOutput : public CueOutput {
    std::map<std::string, float> length, peak;
    std::vector<std::string> calls;
    std::map<uint32_t, float> gain_of;
    std::map<uint32_t, float> pan_of;
    std::map<int, float> bus_gain;
    std::map<std::string, int> bus_id;
    uint32_t next = 1;
    // A file with "missing" in its name fails to load, the way a wav that is not there does.
    void RegisterSound(const char* name, const char* file) override {
        if (strstr(file, "missing")) return;
        if (!length.count(name)) { length[name] = 0.5f; peak[name] = 0.1f; }
    }
    float LoudestAt(const char* n) override { return peak.count(n) ? peak[n] : -1.0f; }
    float LengthOf(const char* n) override { return length.count(n) ? length[n] : -1.0f; }
    uint32_t Play(const char* n, const CuePlay& p) override {
        char b[160];
        snprintf(b, sizeof(b), "play %s bus %d gain %.3f", n, p.bus, p.gain);
        calls.push_back(b);
        gain_of[next] = p.gain;
        pan_of[next] = p.pan;
        return next++;
    }
    void Stop(uint32_t h) override { calls.push_back("stop " + std::to_string(h)); }
    void SetGain(uint32_t h, float g) override { gain_of[h] = g; }
    void SetPitch(uint32_t, float) override {}
    void SetPan(uint32_t h, float p) override { pan_of[h] = p; }
    int AddBus(const char* name, const char*) override {
        if (!bus_id.count(name)) bus_id[name] = (int)bus_id.size() + 1;
        return bus_id[name];
    }
    void SetBusGain(int bus, float g) override { bus_gain[bus] = g; }
};

static std::string joined(const std::vector<std::string>& lines) {
    std::string s;
    for (const std::string& l : lines) s += l + "\n";
    return s;
}

// Lines of the log that are plays, as "tick cue sound".
static std::vector<std::string> plays(CueSystem& cs, const char* what = "play") {
    std::vector<std::string> out;
    for (const std::string& l : cs.log.Lines()) {
        char tick[32], cue[64], w[16], snd[64] = "";
        //Three columns is a line with no sound, a `reset`; four and more everything else.
        if (sscanf(l.c_str(), "%31s %63s %15s %63s", tick, cue, w, snd) >= 3 && std::string(w) == what)
            out.push_back(std::string(tick) + " " + cue + " " + snd);
    }
    return out;
}

static bool load(CueSystem& cs, const char* text) {
    std::string error;
    bool ok = cs.LoadTableText(text, error, "test");
    if (!ok) printf("    load error: %s\n", error.c_str());
    return ok;
}

// --- the table ------------------------------------------------------------------------------

static void table_checks() {
    printf("the table\n");
    FakeOutput out;
    CueSystem cs;
    cs.Init(60.0f, &out);
    std::string error;
    check(!cs.LoadTableText("{ nope", error, "t"), "malformed JSON is refused", error);
    check(!cs.LoadTableText(R"({"cues":{"a":{"signal":"x","sounds":"s","jiter":3}}})", error, "t") &&
          error.find("jiter") != std::string::npos, "a misspelt field is an error, named", error);
    check(!cs.LoadTableText(R"({"cues":{"a":{"sounds":"s"}}})", error, "t"), "a cue with no trigger is refused", error);
    check(!cs.LoadTableText(R"({"cues":{"a":{"signal":"x","sounds":"nothere"}}})", error, "t") &&
          error.find("nothere") != std::string::npos, "an unknown sound is an error", error);
    check(!cs.LoadTableText(R"({"sounds":{"s":"f.wav"},"cues":{"a":{"signal":"x","sounds":"s","group":"g"}}})", error, "t"),
          "an undeclared group is an error", error);
    {
        CueSystem c2;
        c2.Init(60.0f, &out);
        bool ok = load(c2, R"({"sounds":{"gone":"missing.wav","here":"f.wav"},
                               "cues":{"a":{"signal":"x","sounds":"gone"},"b":{"signal":"y","sounds":"here"}}})");
        c2.Signal("y");
        c2.Tick(1);
        check(ok && plays(c2).size() == 1, "a declared sound whose file will not load is silent, not fatal");
    }
    check(load(cs, R"({"sounds":{"s":"f.wav"},"cues":{"a":{"signal":"x","sounds":"s"}}})"), "a minimal table loads");
    std::string before = error;
    check(!cs.LoadTableText("{}", error, "t") && cs.CueNames().size() == 1, "a failed reload keeps the table in use", error);
}

// --- triggers, gains, scopes ---------------------------------------------------------------

static void trigger_checks() {
    printf("triggers and scopes\n");
    FakeOutput out;
    CueSystem cs;
    cs.Init(60.0f, &out);
    load(cs, R"({
      "sounds": { "leave": "a.wav", "hit": "b.wav", "creak": "c.wav", "swing": "d.wav", "land": "e.wav",
                  "ten": "f.wav" },
      "cues": {
        "arrow_leave": { "signal": "shot", "sounds": "leave",
                         "gain_by": [{ "value": "power", "in": [0, 1], "out": [0.55, 1.0] }] },
        "arrow_hit":   { "signal": "hit", "sounds": "hit",
                         "gain_by": [{ "value": "speed", "in": [0, 46], "out": [0.35, 1.0] },
                                     { "value": "distance", "in": [12, 40], "out": [1, 0], "min": 0.15 }] },
        "bow_tension": { "begin": "nocked", "sounds": "creak", "gain": 0.7, "on_end": "stop" },
        "kick_swing":  { "begin": "kick", "sounds": "swing", "delay": 10, "delay_from": "strike_shift", "gain": 0.8 },
        "kick_land":   { "signal": "kick_connected", "scope": "kick", "sounds": "land", "gain": 0.8 },
        "nice_shot":   { "signal": "stand_hit", "when": [["points", "==", 10]], "sounds": "ten", "gain": 0.8 }
      }
    })");

    cs.Signal("shot", CuePayload().Set("power", 0.5f));
    cs.Tick(100);
    std::vector<std::string> l = cs.log.Lines();
    check(l.size() == 1 && l[0].find("gain 0.775") != std::string::npos, "a signal plays on its tick, gain by curve", l.empty() ? "" : l[0]);

    // 0.35 + 0.65 * 23/46 = 0.675 by speed; 30 units away: 1 - 18/28 = 0.357 by distance
    cs.log.Clear();
    cs.SetListener(0.0f);
    cs.Signal("hit", CuePayload().Set("speed", 23.0f).Set("x", 30.0f));
    cs.Signal("hit", CuePayload().Set("speed", 23.0f).Set("x", 80.0f));
    cs.Tick(101);
    l = cs.log.Lines();
    check(l.size() == 2 && l[0].find("gain 0.241") != std::string::npos && l[1].find("gain 0.101") != std::string::npos,
          "gain factors multiply; the distance floor holds", joined(l));

    cs.log.Clear();
    cs.Signal("stand_hit", CuePayload().Set("points", 8));
    cs.Signal("stand_hit", CuePayload().Set("points", 10));
    cs.Tick(102);
    check(plays(cs).size() == 1, "a condition filters, silently");

    // A kick: the swing waits 10 ticks; a kick cut at tick 5 drops it.
    cs.log.Clear();
    cs.BeginScope("kick", 0, CuePayload().Set("strike_shift", 0));
    cs.Tick(200);
    for (uint64_t t = 201; t <= 205; t++) cs.Tick(t);
    cs.EndScope("kick");
    cs.Tick(206);
    for (uint64_t t = 207; t <= 215; t++) cs.Tick(t);
    l = cs.log.Lines();
    check(plays(cs).empty() && l.size() == 1 && l[0].find("skip") != std::string::npos &&
          l[0].find("scope ended") != std::string::npos, "a waiting cue is dropped when its scope ends", joined(l));

    // Ending on the very tick the swing is due: the kick is over by then, so no swing.
    cs.log.Clear();
    cs.BeginScope("kick", 0, CuePayload().Set("strike_shift", 0));
    cs.Tick(250);
    for (uint64_t t = 251; t < 260; t++) cs.Tick(t);
    cs.EndScope("kick");
    cs.Tick(260);
    l = cs.log.Lines();
    check(plays(cs).empty() && l.size() == 1 && l[0].find("260") != std::string::npos && l[0].find("scope ended") != std::string::npos,
          "a scope ending on the tick its cue is due drops it", joined(l));

    cs.log.Clear();
    cs.BeginScope("kick", 0, CuePayload().Set("strike_shift", 2));
    cs.Tick(300);
    cs.Signal("kick_connected");
    for (uint64_t t = 301; t <= 320; t++) {
        if (t == 309) cs.Signal("kick_connected");
        cs.Tick(t);
    }
    std::vector<std::string> p = plays(cs);
    // Signalled after Tick(300) returned, so it is acted on by the next one.
    check(p.size() == 3 && p[0] == "301 kick_land land" && p[1] == "309 kick_land land" && p[2] == "312 kick_swing swing",
          "delay_from shifts the swing; a scoped signal fires in its scope", joined(p));
    cs.EndScope("kick");
    cs.Tick(321);
    cs.log.Clear();
    cs.Signal("kick_connected");
    cs.Tick(322);
    check(plays(cs).empty(), "...and not once its scope is closed");

    // The creak: stopped when the nock ends - even when it has already run out, as the old code did.
    cs.log.Clear();
    cs.BeginScope("nocked");
    cs.Tick(400);
    for (uint64_t t = 401; t < 600; t++) cs.Tick(t);
    cs.EndScope("nocked");
    cs.Signal("shot", CuePayload().Set("power", 1.0f));
    cs.Tick(600);
    l = cs.log.Lines();
    check(l.size() == 3 && l[1].find("600") != std::string::npos && l[1].find("stop") != std::string::npos &&
          l[2].find("arrow_leave") != std::string::npos, "a stop cue logs its stop at the scope's end, before later signals", joined(l));
    check(cs.NumPlaying() >= 1, "an ended creak is off the books once stopped");
}

// --- parity with the archer's hand-wired sounds --------------------------------------------

static void parity_checks() {
    printf("parity with ApplicationArcher's hand-wired kick and swoosh\n");
    const float TPS = 60.0f;
    const float DT = (1.0f / 60.0f);    // as ARCHER_DT is written
    const char* SHOUTS[3] = { "kick_hyaa", "kick_hija", "kick_hoowa" };
    // Measured peaks as SetupSound logs them for the real files would do; any values will, as
    // long as both sides use the same ones. These put the three shouts 2, 0 and 5 ticks apart.
    const float PEAK[3] = { 0.117f, 0.083f, 0.200f };

    FakeOutput out;
    for (int i = 0; i < 3; i++) { out.length[SHOUTS[i]] = 0.4f; out.peak[SHOUTS[i]] = PEAK[i]; }
    out.length["kick_swing"] = 0.2f;
    out.length["arrow_swoosh"] = 0.3f; out.peak["arrow_swoosh"] = 0.160f;

    CueSystem cs;
    cs.Init(TPS, &out);
    bool ok = load(cs, R"({
      "cues": {
        "kick_swing": { "begin": "kick", "sounds": "kick_swing", "delay": 10, "delay_from": "strike_shift", "gain": 0.8 },
        "kick_shout": { "begin": "kick", "sounds": ["kick_hyaa", "kick_hija", "kick_hoowa"],
                        "chance": 0.4, "delay": 9, "jitter": 6, "delay_from": "strike_shift",
                        "align": "peak", "no_repeat": false, "seed": 0, "gain": 0.8 },
        "arrow_swoosh": { "signal": "arrow_impact", "forecast": "in", "scope": "arrow", "once": true,
                          "sounds": "arrow_swoosh" }
      }
    })");
    check(ok, "the parity table loads");

    // The old code, as UpdateSound has it (kick_swing_tick 11, shout chance 0.4 on 10..16).
    auto old_kick = [&](uint64_t start, int shift, std::vector<std::string>& lines) {
        int swing = std::max(11 + shift, 1);
        lines.push_back(std::to_string(start + swing - 1) + " kick_swing kick_swing");
        if (Hash01(0.0f, 0.0f, (int)start, 1) < 0.4f) {
            int span = std::max(16 - 10, 0) + 1;
            int pick = (int)(Hash01(0.0f, 0.0f, (int)start, 2) * (float)span);
            int k = std::min((int)(Hash01(0.0f, 0.0f, (int)start, 3) * 3.0f), 2);
            int peak_shift = (int)lroundf((PEAK[0] - PEAK[k]) * TPS);
            int t = std::max(10 + std::min(pick, span - 1) + shift + peak_shift, 2);
            lines.push_back(std::to_string(start + t - 1) + " kick_shout " + SHOUTS[k]);
        }
    };

    int kicks = 0, differ = 0, shouts = 0;
    int by_sound[3] = { 0, 0, 0 };
    std::string first_diff;
    const int SHIFTS[4] = { 0, -3, 2, 5 };
    uint64_t t = 1000;
    for (int n = 0; n < 4000; n++) {
        int shift = SHIFTS[n % 4];
        uint64_t start = t;
        cs.log.Clear();
        cs.BeginScope("kick", 0, CuePayload().Set("strike_shift", (float)shift));
        for (; t < start + 45; t++) cs.Tick(t);
        cs.EndScope("kick");
        cs.Tick(t++);
        t += (uint64_t)(n % 7);         // uneven gaps, so the hashed ticks are not a lattice
        std::vector<std::string> want;
        old_kick(start, shift, want);
        std::vector<std::string> got = plays(cs);
        std::sort(got.begin(), got.end());
        std::sort(want.begin(), want.end());
        kicks++;
        if (got != want) {
            if (!differ) first_diff = "kick at " + std::to_string(start) + ": old " + joined(want) + " new " + joined(got);
            differ++;
        }
        for (const std::string& w : want)
            for (int i = 0; i < 3; i++)
                if (w.find(SHOUTS[i]) != std::string::npos) { shouts++; by_sound[i]++; }
    }
    char buf[200];
    snprintf(buf, sizeof(buf), "(%d kicks, %d shouts: %d/%d/%d, %d differ)", kicks, shouts,
             by_sound[0], by_sound[1], by_sound[2], differ);
    check(differ == 0, "kick swing and shout: same tick and same file as the old code", differ ? first_diff : buf);
    check(shouts > kicks * 3 / 10 && shouts < kicks * 5 / 10 && by_sound[0] && by_sound[1] && by_sound[2],
          "and the draws are exercised: ~40% shout, every file picked", buf);

    // The swoosh: the old per-tick forecast against the cue's, for flights of every length.
    int flights = 0, sdiff = 0;
    std::string sfirst;
    for (int slot = 0; slot < 4; slot++) {
        for (int len = 1; len <= 40; len++) {
            uint64_t start = t;
            cs.log.Clear();
            cs.BeginScope("arrow", slot);
            std::string want;
            bool swooshed = false;
            for (int k = 0; k <= len; k++, t++) {
                int ticks = len - k;
                if (ticks > 0) cs.Signal("arrow_impact", CuePayload().Set("in", (float)ticks), slot);
                cs.Tick(t);
                float lead = (float)ticks * DT;
                if (!swooshed && ticks > 0 && !(lead > 0.160f)) {
                    snprintf(buf, sizeof(buf), "%llu from %.3f", (unsigned long long)t, 0.160f - lead);
                    want = buf;
                    swooshed = true;
                }
            }
            cs.EndScope("arrow", slot);
            cs.Tick(t++);
            std::string got;
            for (const std::string& l : cs.log.Lines()) {
                if (l.find("arrow_swoosh") == std::string::npos || l.find("play") == std::string::npos) continue;
                unsigned long long tk; float from = 0.0f;
                const char* f = strstr(l.c_str(), "from ");
                sscanf(l.c_str(), "%llu", &tk);
                if (f) sscanf(f + 5, "%f", &from);
                snprintf(buf, sizeof(buf), "%llu from %.3f", tk, from);
                got += buf;
            }
            flights++;
            if (got != want) {
                if (!sdiff) sfirst = "flight of " + std::to_string(len) + " from " + std::to_string(start) + ": old '" + want + "' new '" + got + "'";
                sdiff++;
            }
        }
    }
    snprintf(buf, sizeof(buf), "(%d flights, %d differ)", flights, sdiff);
    check(sdiff == 0, "arrow swoosh: same tick, same offset, once per flight", sdiff ? sfirst : buf);
}

// --- draws, gaps, instances, groups ---------------------------------------------------------

static void rule_checks() {
    printf("draws, gaps, instances, groups\n");
    FakeOutput out;
    out.length["a"] = 0.5f; out.length["b"] = 0.5f; out.length["c"] = 0.5f;
    out.length["line1"] = 1.0f; out.length["line2"] = 1.0f; out.length["big"] = 1.0f;
    CueSystem cs;
    cs.Init(60.0f, &out);
    load(cs, R"({
      "buses": { "effects": {}, "voice": {} },
      "groups": { "her": { "gap": 30 }, "narrator": { "duck": { "effects": 0.25 }, "duck_ticks": 10 } },
      "cues": {
        "varied":  { "signal": "v", "sounds": ["a", "b", "c"] },
        "gapped":  { "signal": "g", "sounds": "a", "gap": 20 },
        "capped":  { "signal": "m", "sounds": "a", "max_instances": 2 },
        "remark":  { "signal": "r", "sounds": "line1", "group": "her" },
        "patient": { "signal": "q", "sounds": "line2", "group": "her", "busy": "queue", "max_wait": 200 },
        "urgent":  { "signal": "u", "sounds": "big", "group": "her", "busy": "interrupt", "priority": 5 },
        "story":   { "signal": "n", "sounds": "line1", "group": "narrator", "bus": "voice" },
        "shaken":  { "signal": "s", "gain_by": [{ "value": "hard", "in": [0, 1], "out": [0, 1] }],
                     "actions": [{ "kind": "shake", "amount": 0.5 }, { "kind": "shake", "offset": 3 }] }
      }
    })");

    // Never the same twice running, over many draws.
    std::string last;
    bool repeat = false;
    int n = 0;
    for (uint64_t t = 1000; t < 1400; t++) {
        cs.log.Clear();
        cs.Signal("v");
        cs.Tick(t);
        std::vector<std::string> p = plays(cs);
        if (p.size() == 1) {
            std::string s = p[0].substr(p[0].rfind(' ') + 1);
            repeat = repeat || (s == last);
            last = s;
            n++;
        }
    }
    check(n == 400 && !repeat, "no_repeat: 400 draws, never the same file twice running");

    cs.log.Clear();
    for (uint64_t t = 2000; t < 2050; t++) { cs.Signal("g"); cs.Tick(t); }
    std::vector<std::string> p = plays(cs);
    check(p.size() == 3 && p[0].find("2000") == 0 && p[1].find("2020") == 0 && p[2].find("2040") == 0,
          "gap: 50 ticks of signals play on 0, 20 and 40", joined(p));

    cs.log.Clear();
    for (int i = 0; i < 4; i++) cs.Signal("m");
    cs.Tick(3000);
    check(plays(cs).size() == 2 && plays(cs, "skip").size() == 2, "max_instances: two of four, two skipped");
    cs.log.Clear();
    for (uint64_t t = 3001; t <= 3031; t++) cs.Tick(t);     // the 0.5 s sounds end on tick 3030
    cs.Signal("m");
    cs.Tick(3032);
    check(plays(cs).size() == 1, "...and room again once a sound's length has run");

    // Groups: one line at a time; skip, queue, interrupt; a gap after a line.
    cs.log.Clear();
    cs.Signal("r");
    cs.Tick(4000);
    cs.Signal("r");
    cs.Signal("q");
    cs.Tick(4010);
    for (uint64_t t = 4011; t < 4200; t++) cs.Tick(t);
    p = plays(cs);
    // remark 4000..4060, the group's gap to 4090, and the queued line plays on 4090
    check(p.size() == 2 && p[0] == "4000 remark line1" && p[1] == "4090 patient line2",
          "a busy group skips, and a queued line waits out the line and the gap", joined(p));
    cs.log.Clear();
    for (uint64_t t = 4200; t < 4400; t++) cs.Tick(t);
    cs.Signal("r");
    cs.Tick(4400);
    cs.Signal("u");
    cs.Tick(4405);
    std::vector<std::string> l = cs.log.Lines();
    check(l.size() == 3 && l[1].find("stop") != std::string::npos && l[2].find("urgent") != std::string::npos,
          "a higher priority interrupts: the line is stopped and the new one plays", joined(l));

    // The narrator ducks the effects bus while it speaks, easing over 10 ticks, and lets it back.
    for (uint64_t t = 4406; t < 4600; t++) cs.Tick(t);
    int fx = out.bus_id["effects"];
    cs.Signal("n");
    cs.Tick(4600);
    float d1 = out.bus_gain[fx];
    for (uint64_t t = 4601; t < 4615; t++) cs.Tick(t);
    float d2 = out.bus_gain[fx];
    for (uint64_t t = 4615; t < 4700; t++) cs.Tick(t);
    float d3 = out.bus_gain[fx];
    char buf[160];
    snprintf(buf, sizeof(buf), "(first tick %.3f, after 15 %.3f, after it ends %.3f)", d1, d2, d3);
    check(fabsf(d1 - 0.925f) < 1e-4f && fabsf(d2 - 0.25f) < 1e-4f && fabsf(d3 - 1.0f) < 1e-4f,
          "ducking eases the bus down while the narrator speaks, and back", buf);

    // Actions: the handler gets them, an offset one three ticks later.
    std::vector<std::string> acts;
    cs.SetActionHandler("shake", [&](const CueAction& a) {
        acts.push_back(std::to_string(a.tick) + " " + a.params.value("kind", std::string()) + " " +
                       std::to_string(a.params.value("amount", 0.0f)).substr(0, 4) + " x" +
                       std::to_string(a.gain).substr(0, 4));
    });
    cs.Signal("s", CuePayload().Set("hard", 0.25f));
    for (uint64_t t = 5000; t < 5010; t++) cs.Tick(t);
    check(acts.size() == 2 && acts[0] == "5000 shake 0.50 x0.25" && acts[1] == "5003 shake 0.00 x0.25",
          "actions reach their handler with the cue's gain, offset ones on their tick", joined(acts));

    // History: capture, change it, restore - the next no_repeat pick avoids the restored one.
    json h = cs.CaptureHistory();
    check(h["cues"].contains("varied") && h["cues"].contains("remark"), "history holds the cues that fired");
    CueSystem cs2;
    cs2.Init(60.0f, &out);
    load(cs2, R"({"cues":{"varied":{"signal":"v","sounds":["a","b","c"]}}})");
    bool same = true;
    for (int pick = 0; pick < 3; pick++) {
        cs.RestoreHistory(json{ {"cues", { {"varied", json::array({pick, 900})} }} });
        cs2.RestoreHistory(cs.CaptureHistory());
        for (uint64_t t = 6000; t < 6003; t++) {
            cs.log.Clear(); cs2.log.Clear();
            cs.Signal("v"); cs2.Signal("v");
            cs.Tick(t); cs2.Tick(t);
            same = same && plays(cs) == plays(cs2);
        }
    }
    check(same, "restored history makes two systems draw alike");

    // A clock that goes backwards (a restart) does not leave a gap blocking the cue.
    cs.log.Clear();
    cs.Reset();
    cs.Signal("g");
    cs.Tick(10);
    check(plays(cs).size() == 1, "after a restart to tick 10 a gapped cue plays at once");
    check(plays(cs, "reset").size() == 1, "a restart leaves a reset marker in the log");

    // Reload keeps history and the table's new numbers apply.
    std::string error;
    bool reloaded = cs.LoadTableText(R"({"cues":{"gapped":{"signal":"g","sounds":"a","gap":5}}})", error, "t2");
    cs.log.Clear();
    for (uint64_t t = 11; t < 30; t++) { cs.Signal("g"); cs.Tick(t); }
    p = plays(cs);
    // It last fired on 10, before the reload; with the new gap of 5 it plays on 15, 20 and 25.
    check(reloaded && p.size() == 3 && p[0].find("15") == 0 && p[2].find("25") == 0,
          "reload: history kept, the new gap applies", joined(p));
}

// --- the real mixer ------------------------------------------------------------------------

static void sound_checks() {
    printf("through SoundSystem, offline\n");
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string root = std::string(tmp) + "sound_test_assets";      // written by tools/sound_test
    AddAssetSearchRoot(root.c_str());
    SoundSystem ss;
    if (!ss.InitialiseOffline(48000, 2)) { check(false, "offline engine"); return; }
    CueSoundOutput cso(&ss);
    CueSystem cs;
    cs.Init(60.0f, &cso);
    std::string error;
    bool ok = cs.LoadTableText(R"({
      "sounds": { "tone": "sound/sine.wav" },
      "buses":  { "effects": {}, "voice": {} },
      "groups": { "narrator": { "duck": { "effects": 0.25 }, "duck_ticks": 4 } },
      "cues": {
        "hum":   { "signal": "hum", "sounds": "tone", "bus": "effects", "looping": true, "on_end": "stop" },
        "story": { "signal": "story", "sounds": "tone", "bus": "voice", "group": "narrator", "gain": 0.0 }
      }
    })", error, "sound");
    if (!ok) {
        check(false, "the table loads against SoundSystem", error + " (run tools/sound_test first: it writes the sine)");
        return;
    }
    auto rms = [&](int frames) {
        std::vector<float> b(frames * 2);
        ss.Render(b.data(), frames);
        double s = 0; for (int i = 0; i < frames; i++) s += (double)b[i * 2] * b[i * 2];
        return sqrt(s / frames);
    };
    cs.Signal("hum");
    cs.Tick(1);
    rms(1600);
    double before = rms(4800);
    cs.Signal("story");                  // silent itself, so what is measured is the duck alone
    for (uint64_t t = 2; t < 10; t++) { cs.Tick(t); rms(800); }
    double during = rms(4800);
    std::vector<SoundVoiceInfo> v;
    ss.ListVoices(v);
    char buf[160];
    snprintf(buf, sizeof(buf), "(hum %.3f, under the narrator %.3f, %d voices)", before, during, (int)v.size());
    check(before > 0.3 && fabs(during - before * 0.25) < 0.02 && v.size() == 2,
          "a narrator line ducks the effects bus in the real mix", buf);
}

// --- the panel's play button ------------------------------------------------------------------

static void audition_checks() {
    printf("audition\n");
    FakeOutput out;
    CueSystem cs;
    cs.Init(60.0f, &out);
    std::vector<std::string> acted;
    cs.SetActionHandler("shake", [&](const CueAction& a) {
        char b[64];
        snprintf(b, sizeof(b), "shake %.3f", a.gain);
        acted.push_back(b);
    });
    load(cs, R"({"sounds":{"a":"a.wav","b":"b.wav","c":"c.wav"},"groups":{"her":{}},
                 "cues":{"shout":{"signal":"k","sounds":["a","b","c"],"chance":0.01,"delay":30,
                                  "when":[["x","==",99]],"group":"her","gain":0.8,
                                  "gain_by":[{"value":"power","in":[0,1],"out":[0.5,0.9]}]},
                         "thud":{"signal":"t","sounds":"a","gain":0.5,
                                 "actions":[{"kind":"shake","amount":1,"offset":20}]}}})");
    cs.Audition("shout");
    cs.Tick(1);
    std::vector<std::string> p = plays(cs);
    check(p.size() == 1 && p[0] == "1 shout a", "plays on the next tick, past its chance, delay and condition", joined(p));
    check(!out.calls.empty() && out.calls.back().find("gain 0.720") != std::string::npos,
          "at its gain times each curve at its loudest", out.calls.empty() ? "" : out.calls.back());
    cs.log.Clear();
    cs.Audition("shout");
    cs.Tick(2);
    cs.Audition("shout");
    cs.Tick(3);
    p = plays(cs);
    check(p.size() == 2 && p[0] == "2 shout b" && p[1] == "3 shout c",
          "each press takes the next variant, and a group does not hold it back", joined(p));
    check(cs.CaptureHistory()["cues"].empty() && cs.CaptureHistory()["groups"].empty(),
          "and it leaves no history a replay could read", cs.CaptureHistory().dump());
    cs.Audition("thud");
    cs.Tick(4);
    check(acted.size() == 1 && acted[0] == "shake 0.500", "its actions fire at once, at the cue's gain", joined(acted));
    cs.log.Clear();
    cs.Audition("nothing");
    cs.Tick(5);
    check(cs.log.Lines().empty(), "a name the table does not have does nothing");
}

/*
    A loop that stands in the level - the archer's waterfall: gain follows the distance from where
    it was begun to the listener, pan the signed offset, both re-read every tick as she moves, and
    a follow on a parameter still reads the parameter.
*/
static void follow_checks() {
    printf("follow\n");
    FakeOutput out;
    CueSystem cs;
    cs.Init(60.0f, &out);
    load(cs, R"({"sounds":{"a":"a.wav","b":"b.wav"},
                 "cues":{"fall":{"begin":"fall","sounds":"a","looping":true,"on_end":"stop",
                                 "follow":{"value":"distance","gain":{"in":[0,10],"out":[1,0]},
                                           "pan":{"value":"dx","in":[-10,10],"out":[-1,1]}}},
                         "swing":{"begin":"swing","sounds":"b","looping":true,
                                  "follow":{"value":"speed","gain":{"in":[0,4],"out":[0,1]}}}}})");
    cs.SetListener(0.0f);
    cs.BeginScope("fall", 0, CuePayload().Set("x", 5.0f));
    cs.Tick(1);
    uint32_t h = out.next - 1;
    char d[96];
    snprintf(d, sizeof(d), "gain %.3f pan %.3f", out.gain_of[h], out.pan_of[h]);
    check(fabsf(out.gain_of[h] - 0.5f) < 1e-4f && fabsf(out.pan_of[h] - 0.5f) < 1e-4f,
          "it starts at the distance and side it was begun at", d);
    cs.SetListener(5.0f);
    cs.Tick(2);
    snprintf(d, sizeof(d), "gain %.3f pan %.3f", out.gain_of[h], out.pan_of[h]);
    check(fabsf(out.gain_of[h] - 1.0f) < 1e-4f && fabsf(out.pan_of[h]) < 1e-4f,
          "loudest and centred with the listener on it", d);
    cs.SetListener(20.0f);
    cs.Tick(3);
    snprintf(d, sizeof(d), "gain %.3f pan %.3f", out.gain_of[h], out.pan_of[h]);
    check(out.gain_of[h] == 0.0f && out.pan_of[h] == -1.0f, "silent and hard left once she is past it", d);
    cs.EndScope("fall", 0);
    cs.Tick(4);
    check(!out.calls.empty() && out.calls.back() == "stop " + std::to_string(h), "and stopped with its scope",
          out.calls.empty() ? "" : out.calls.back());
    cs.SetParameter("speed", 2.0f);
    cs.BeginScope("swing");
    cs.Tick(5);
    uint32_t s = out.next - 1;
    cs.SetParameter("speed", 4.0f);
    cs.Tick(6);
    check(fabsf(out.gain_of[s] - 1.0f) < 1e-4f, "a follow on a parameter still reads the parameter");
    std::string error;
    CueSystem bad;
    bad.Init(60.0f, &out);
    bool loaded = bad.LoadTableText(R"({"sounds":{"a":"a.wav"},
        "cues":{"x":{"signal":"x","sounds":"a","follow":{"gain":{"in":[0,1],"out":[0,1]}}}}})", error, "test");
    check(!loaded && error.find("needs a value") != std::string::npos, "a part with no value anywhere is an error", error);
}

int main() {
    table_checks();
    trigger_checks();
    parity_checks();
    rule_checks();
    sound_checks();
    audition_checks();
    follow_checks();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
