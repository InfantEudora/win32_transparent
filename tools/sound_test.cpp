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
        ./sound_test.exe shared_assets --offline-only

    It makes noise for about four seconds. Exit code 0 = all checks passed.

    THE OFFLINE CHECKS run first and make no noise at all: SoundSystem::InitialiseOffline mixes
    with no device, and Render pulls the mix out as numbers. That is what pan, pitch, buses and
    gain smoothing are checked against - a channel that should be empty either is or is not, where
    by ear "a bit quieter on the right" is all anyone could say. They play a sine the test writes
    itself, so a quiet stretch in some asset cannot pass for a muted bus. `--offline-only` skips
    the device checks, for a run at a desk where someone is working.
*/
#include "SoundSystem.h"
#include "File.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
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

// --- offline ----------------------------------------------------------------------------------

static const uint32_t RATE = 48000;

// A mono 16-bit sine, in the plain 44-byte layout WaveFile reads.
static bool write_sine_wav(const std::string& path, float seconds, float freq, float amp) {
    uint32_t frames = (uint32_t)(seconds * RATE);
    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i] = (int16_t)lroundf(amp * 32767.0f * sinf(6.2831853f * freq * (float)i / (float)RATE));
    }
    uint32_t data_bytes = frames * 2;
    uint32_t riff = 36 + data_bytes, fmt_len = 16, rate = RATE, byte_rate = RATE * 2;
    uint16_t format = 1, channels = 1, align = 2, bits = 16;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_len, 4, 1, f); fwrite(&format, 2, 1, f);
    fwrite(&channels, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
    fwrite(pcm.data(), 2, frames, f);
    fclose(f);
    return true;
}

// Mixes `frames` more frames and returns them, stereo interleaved.
static std::vector<float> render(SoundSystem& ss, uint64_t frames) {
    std::vector<float> out(frames * 2, 0.0f);
    ss.Render(out.data(), frames);
    return out;
}

// RMS of each channel over [from, from + n) of a rendered block.
static void rms(const std::vector<float>& b, size_t from, size_t n, double& l, double& r) {
    double sl = 0.0, sr = 0.0;
    for (size_t i = from; i < from + n; i++) {
        sl += (double)b[i * 2] * b[i * 2];
        sr += (double)b[i * 2 + 1] * b[i * 2 + 1];
    }
    l = sqrt(sl / (double)n);
    r = sqrt(sr / (double)n);
}

static double peak(const std::vector<float>& b, size_t from, size_t n) {
    double p = 0.0;
    for (size_t i = from * 2; i < (from + n) * 2; i++) p = fmax(p, fabs((double)b[i]));
    return p;
}

static void offline_checks() {
    printf("offline\n");
    char buf[160];

    // The sine goes in a folder of its own that is then an asset root, so it resolves as
    // sound/sine.wav exactly as a game's sounds do.
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string root = std::string(tmp) + "sound_test_assets";
    CreateDirectoryA(root.c_str(), NULL);
    CreateDirectoryA((root + "/sound").c_str(), NULL);
    const float AMP = 0.5f;
    const double SINE_RMS = AMP / sqrt(2.0);
    check(write_sine_wav(root + "/sound/sine.wav", 1.0f, 440.0f, AMP), "test sine written");
    AddAssetSearchRoot(root.c_str());

    SoundSystem ss;
    check(ss.InitialiseOffline(RATE, 2), "offline engine initialised");
    check(ss.FindBus("master") == SOUND_BUS_MASTER, "the master bus exists from the start");
    ss.AppendFile("sound/sine.wav", "sine");
    float len = ss.LengthOf("sine");
    snprintf(buf, sizeof(buf), "(%.3fs)", len);
    check(fabsf(len - 1.0f) < 0.001f, "LengthOf the 1 s sine", buf);

    double l, r;

    // Centre: a mono sound comes out equally on both sides, at its own level.
    SoundParams p;
    soundhandle_t h = ss.Play("sine", p);
    std::vector<float> b = render(ss, 4800);
    rms(b, 480, 4320, l, r);
    snprintf(buf, sizeof(buf), "(L %.3f R %.3f, expected %.3f)", l, r, SINE_RMS);
    check(fabs(l - SINE_RMS) < 0.02 && fabs(r - SINE_RMS) < 0.02, "centred mono is equal on both sides", buf);
    ss.Stop(h);

    // The FIRST gain is not ramped to: a quiet sound must not start with a loud blip while the
    // smoothing climbs down from 1. Its first millisecond may be no louder than its gain allows.
    p.gain = 0.25f;
    h = ss.Play("sine", p);
    b = render(ss, 480);
    snprintf(buf, sizeof(buf), "(peak %.3f, ceiling %.3f)", peak(b, 0, 48), 0.25 * AMP * 1.02);
    check(peak(b, 0, 48) <= 0.25 * AMP * 1.02, "a new voice starts AT its gain, not ramping to it", buf);
    ss.Stop(h);
    p.gain = 1.0f;

    // Pan, both ways, at the start.
    p.pan = -1.0f;
    h = ss.Play("sine", p);
    b = render(ss, 4800);
    rms(b, 480, 4320, l, r);
    snprintf(buf, sizeof(buf), "(L %.3f R %.5f)", l, r);
    check(l > 0.2 && r < 0.001, "pan -1 empties the right channel", buf);
    ss.Stop(h);
    p.pan = 1.0f;
    h = ss.Play("sine", p);
    b = render(ss, 4800);
    rms(b, 480, 4320, l, r);
    snprintf(buf, sizeof(buf), "(L %.5f R %.3f)", l, r);
    check(r > 0.2 && l < 0.001, "pan +1 empties the left channel", buf);
    ss.Stop(h);
    p.pan = 0.0f;

    // Pan while playing.
    h = ss.Play("sine", p);
    render(ss, 2400);
    ss.SetPan(h, -1.0f);
    b = render(ss, 4800);
    rms(b, 960, 3840, l, r);
    snprintf(buf, sizeof(buf), "(L %.3f R %.5f)", l, r);
    check(l > 0.2 && r < 0.001, "SetPan moves a playing sound", buf);
    ss.Stop(h);

    // Gain while playing is SMOOTHED: not gone a millisecond later, gone well inside a tick.
    h = ss.Play("sine", p);
    render(ss, 2400);
    ss.SetGain(h, 0.0f);
    b = render(ss, 1440);
    rms(b, 0, 48, l, r);
    double first_ms = l;
    rms(b, 960, 480, l, r);
    snprintf(buf, sizeof(buf), "(first ms %.3f, after 20 ms %.5f)", first_ms, l);
    check(first_ms > 0.1 && l < 0.001, "SetGain ramps over ~10 ms instead of stepping", buf);
    ss.Stop(h);

    // Buses: a bus at 0 is silent, a bus at 0.5 halves, and a parent multiplies its children.
    int fx = ss.AddBus("fx");
    check(fx > SOUND_BUS_MASTER, "AddBus makes a second bus");
    check(ss.AddBus("fx") == fx, "adding the same name again returns the same bus");
    ss.SetBusGain(fx, 0.0f);
    p.bus = fx;
    h = ss.Play("sine", p);
    b = render(ss, 4800);
    rms(b, 960, 3840, l, r);
    snprintf(buf, sizeof(buf), "(L %.5f)", l);
    check(l < 0.001, "a sound on a bus at 0 is silent", buf);
    ss.SetBusGain(fx, 0.5f);
    b = render(ss, 4800);
    rms(b, 960, 3840, l, r);
    snprintf(buf, sizeof(buf), "(L %.3f, expected %.3f)", l, 0.5 * SINE_RMS);
    check(fabs(l - 0.5 * SINE_RMS) < 0.02, "a bus at 0.5 halves what is on it", buf);
    ss.SetBusGain(SOUND_BUS_MASTER, 0.5f);
    b = render(ss, 4800);
    rms(b, 960, 3840, l, r);
    snprintf(buf, sizeof(buf), "(L %.3f, expected %.3f)", l, 0.25 * SINE_RMS);
    check(fabs(l - 0.25 * SINE_RMS) < 0.02, "the master multiplies the bus under it", buf);
    ss.Stop(h);
    ss.SetBusGain(SOUND_BUS_MASTER, 1.0f);
    ss.SetBusGain(fx, 1.0f);
    p.bus = SOUND_BUS_MASTER;

    // A bus that does not exist falls back to the master rather than failing the sound.
    p.bus = NUM_SOUND_BUSES - 1;
    h = ss.Play("sine", p);
    b = render(ss, 4800);
    rms(b, 960, 3840, l, r);
    check(h != SOUND_INVALID_HANDLE && l > 0.2, "a missing bus plays on the master instead");
    ss.Stop(h);
    p.bus = SOUND_BUS_MASTER;

    // Pitch 2 plays the 1 s sine in half a second.
    p.pitch = 2.0f;
    h = ss.Play("sine", p);
    uint64_t frames = 0;
    while (!ss.FinishedPlaying(h) && frames < RATE * 3) {
        render(ss, 480);
        frames += 480;
    }
    snprintf(buf, sizeof(buf), "(%.3fs, expected 0.500s)", (double)frames / RATE);
    check(fabs((double)frames / RATE - 0.5) < 0.02, "pitch 2 plays in half the time", buf);
    p.pitch = 1.0f;

    // ListVoices says what is playing, where and how.
    p.bus = fx;
    p.pan = 0.3f;
    p.f_looping = true;
    h = ss.Play("sine", p);
    render(ss, 4800);
    std::vector<SoundVoiceInfo> voices;
    ss.ListVoices(voices);
    bool ok = voices.size() == 1 && voices[0].handle == h && voices[0].name == "sine" &&
              voices[0].bus == fx && fabsf(voices[0].pan - 0.3f) < 1e-6f && voices[0].f_looping &&
              fabsf(voices[0].position - 0.1f) < 0.01f && fabsf(voices[0].length - 1.0f) < 0.001f;
    snprintf(buf, sizeof(buf), "(%d voices, %s at %.3fs)", (int)voices.size(),
             voices.empty() ? "-" : voices[0].name.c_str(), voices.empty() ? 0.0f : voices[0].position);
    check(ok, "ListVoices reports name, bus, pan, loop and position", buf);
    ss.Stop(h);
    ss.ListVoices(voices);
    check(voices.empty(), "and nothing once it is stopped");
    printf("\n");
}

int main(int argc, char** argv) {
    AddAssetSearchRoot(argv[1]);

    offline_checks();
    if (argc > 2 && std::string(argv[2]) == "--offline-only") {
        printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
               failures, failures == 1 ? "" : "s");
        return failures ? 1 : 0;
    }
    printf("device\n");

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
