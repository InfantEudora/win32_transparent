/*
    Headless checks for core/SoundSystem - backlog item 85, the miniaudio port.

    SoundSystem is otherwise only testable by playing a game and listening, which is exactly the
    kind of verification that misses a wrong sample rate. Everything below runs in a console
    program that links the ordinary core objects and opens the real default device.

    THE CHECK THAT MATTERS IS A STOPWATCH. shared_assets/sound/hax.wav is 12,370 frames at
    6000 Hz - 2.06 seconds - and the device runs at 48000. ma_audio_buffer_ref_init does not take
    a sample rate and leaves the field at zero, and a zero there means "same as the engine", which
    would play that file eight times too fast and finish in about a quarter of a second. One line
    in SoundSystem::Play sets it. Timing the playback is the only way to catch it going missing.

    Build (needs no core objects - it compiles the handful of sources it uses):

        export PATH="/c/msys64/mingw64/bin:$PATH"
        g++ -std=c++17 -O2 -fno-exceptions -DUSE_SOUND -D_WIN32             -Icore -Icore/physics -Icore/skeleton -I3rdparty -I3rdparty/imgui             -I3rdparty/stb_image -I3rdparty/miniz -I3rdparty/reactphysics3d             tools/sound_test.cpp core/SoundSystem.cpp core/WaveFile.cpp core/File.cpp             core/Debug.cpp core/Debug_win32.cpp core/BinaryAsset.cpp BinaryAssetMemoryEmpty.cpp             -Llibs -lthirdparty -lole32 -luser32             -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++             -o sound_test.exe

    Run it with the asset root as its one argument, from the repo root:

        ./sound_test.exe shared_assets

    It makes noise for about four seconds. Exit code 0 = all checks passed.
*/
#include "SoundSystem.h"
#include "File.h"
#include <cstdio>
#include <windows.h>

static int failures = 0;

static void check(bool ok, const char* what, const char* detail = "") {
    printf("  %-46s %s %s\n", what, ok ? "PASS" : "FAIL", detail);
    if (!ok) failures++;
}

static double seconds_until_finished(SoundSystem& ss, soundhandle_t h, double timeout) {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (;;) {
        if (ss.FinishedPlaying(h)) break;
        QueryPerformanceCounter(&t1);
        double el = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        if (el > timeout) return el;
        Sleep(5);
    }
    QueryPerformanceCounter(&t1);
    return (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
}

int main(int argc, char** argv) {
    AddAssetSearchRoot(argv[1]);

    SoundSystem ss;
    ss.Initialise();
    check(true, "engine initialised");

    ss.AppendFile("sound/hax.wav", "gameover");    // 6000 Hz mono, 12370 frames = 2.06 s
    ss.AppendFile("sound/click.wav", "move");      // 44100 Hz stereo
    ss.AppendFile("sound/click.wav", "lock");      // same file, second name

    // --- the resampling check -------------------------------------------------------------
    soundhandle_t g = ss.Play("gameover");
    check(g != SOUND_INVALID_HANDLE, "Play returns a handle");
    Sleep(120);
    check(ss.GetNumPlaying() == 1, "GetNumPlaying == 1 while playing");
    check(!ss.FinishedPlaying(g), "FinishedPlaying false mid-sound");

    double dur = seconds_until_finished(ss, g, 5.0);
    char buf[128];
    snprintf(buf, sizeof(buf), "(%.2fs, expected ~2.06s)", dur);
    check(dur > 1.8 && dur < 2.4, "6000 Hz file plays at the right speed", buf);

    check(ss.FinishedPlaying(g), "FinishedPlaying true after the end");
    check(ss.GetNumPlaying() == 0, "GetNumPlaying back to 0");

    // --- overlap: one buffer, several voices ----------------------------------------------
    soundhandle_t a = ss.Play("move");
    soundhandle_t b = ss.Play("move");
    soundhandle_t c = ss.Play("lock");       // same buffer under another name
    Sleep(80);
    snprintf(buf, sizeof(buf), "(%d playing)", ss.GetNumPlaying());
    check(ss.GetNumPlaying() == 3, "three voices on one buffer overlap", buf);
    check(a != b && b != c, "each playing gets its own handle");

    // --- stop / pause / resume ------------------------------------------------------------
    ss.Stop(a);
    Sleep(30);
    check(ss.FinishedPlaying(a), "Stop ends that voice");
    check(!ss.FinishedPlaying(b), "and leaves the others alone");

    ss.Pause(b);
    Sleep(30);
    check(ss.FinishedPlaying(b), "Pause reads as not playing");
    ss.Resume(b);
    Sleep(30);
    check(!ss.FinishedPlaying(b), "Resume carries on");

    // --- a handle for a finished sound is inert -------------------------------------------
    ss.Stop(b);
    ss.Stop(c);
    soundhandle_t stale = g;
    ss.Pause(stale);
    ss.Resume(stale);
    ss.Rewind(stale);
    ss.Stop(stale);
    check(ss.FinishedPlaying(stale), "stale handle is inert, not dangerous");

    // --- looping holds a voice open --------------------------------------------------------
    soundhandle_t loop = ss.Play("move", true, 0.5f, SOUND_KEEP);
    Sleep(700);   // click.wav is 0.46 s, so a non-looping voice would be done by now
    check(!ss.FinishedPlaying(loop), "looping voice still playing past its length");
    ss.Stop(loop);

    check(ss.GetNumPlaying() == 0, "everything stopped at the end");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
