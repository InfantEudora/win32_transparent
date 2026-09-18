#ifndef __ANDROID_SYSINFO_H_
#define __ANDROID_SYSINFO_H_

/*
    ANDROID-ONLY, AND GUARDED RATHER THAN KEPT OUT OF THE SHARED FOLDER.

    Everything below reads sysfs, ASensorManager, EGL and JNI - there is no Windows equivalent
    and no attempt at one. The guard is what lets this file sit in the shared core alongside
    the engine, where the makefile picks up core/*.cpp with a wildcard: on Windows the whole
    header and its translation unit compile to nothing, and on Android they are what they were.

    Application.h already includes this inside the same guard, so nothing changes at the call
    site; the guard here is what makes the file safe to include, and to compile, from anywhere.
*/
#if defined(__ANDROID__)

#include <android_native_app_glue.h>
#include <GLES2/gl2.h>
#include <string>
#include <vector>

// --- Display ---------------------------------------------------------

struct DisplayInfo {
    bool available = false; // false if app->config was NULL when queried
    int32_t widthDp = 0;
    int32_t heightDp = 0;
    int32_t density = 0;
    int32_t orientation = 0;
};
DisplayInfo queryDisplayInfo(struct android_app* app);
void logDisplayInfo(const DisplayInfo& info);

// The part of our own window that the system bars (status bar, navigation bar, display cutout)
// sit on top of, in window pixels, as reported by android.view.WindowInsets.
//
// This is NOT derivable from the window size. On a device with on-screen navigation buttons the
// surface we are handed spans the bar as well -- we render under it perfectly happily -- but the
// window manager keeps that strip in its touch-exclude region, so every touch there goes to the
// bar and never reaches us. A control drawn in it is visible, looks live, and is simply dead.
// (Measured on the Redmi 2201116SG in landscape: 2310x1080 surface, rightmost 130 px owned by
// the navigation bar. The MDT740 has physical buttons and reports zeroes here, which is why the
// bug did not exist until a second device turned up.)
//
// There is no NDK equivalent -- android/window.h only defines the LAYOUT_INSET_DECOR *flag* --
// so this is JNI, the same route RequestSustainedPerformance takes for setSustainedPerformanceMode.
struct SafeInsets {
    bool available = false; // false if the view was not attached yet, or the JNI call failed
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};
// Must be called once the activity's window exists (APP_CMD_INIT_WINDOW or later): before the
// decor view is attached, getRootWindowInsets() legitimately returns null and this reports
// available=false rather than guessing.
SafeInsets querySafeInsets(struct android_app* app);
void logSafeInsets(const SafeInsets& insets);

// --- Sensors -----------------------------------------------------------

struct SensorInfo {
    std::string name;
    std::string vendor;
    int type = 0;
    float resolution = 0.0f; // smallest change the sensor can measure, in its native unit
    int minDelayUs = 0;      // minimum sampling period, microseconds (0 = on-change/unknown)
};
struct SensorCapabilities {
    std::vector<SensorInfo> sensors; // every sensor ASensorManager reports, not just accel/gyro
};
SensorCapabilities querySensorCapabilities(void);
void logSensorCapabilities(const SensorCapabilities& caps);

// --- CPU -----------------------------------------------------------------
// Fixed-size, zero-allocation by design: getCpuFreqInfo() is called every
// frame from the ImGui CPU tab (watching a core's clock ramp up/down in
// real time, e.g. this device's touch-input boost), not just once at
// startup, so it must not allocate. -1 in curKhz/minKhz/maxKhz means that
// sysfs node wasn't readable. `online` is read explicitly from each core's
// own sysfs node (rather than inferred from a missing cpufreq directory)
// so it can't be fooled by a kernel that leaves a stale cpufreq dir behind
// after hotplugging a big.LITTLE core off.
#define MAX_CPU_CORES 16
struct CpuFreqInfo {
    int coreCount = 0;
    bool online[MAX_CPU_CORES];
    long curKhz[MAX_CPU_CORES];
    long minKhz[MAX_CPU_CORES];
    long maxKhz[MAX_CPU_CORES];
    char governor[MAX_CPU_CORES][32];
};
void getCpuFreqInfo(CpuFreqInfo* info);
void logCpuFreqInfo(const CpuFreqInfo& info);

// --- Battery -------------------------------------------------------------
// No NDK API for battery state, and this app has no Java/BatteryManager, so
// this reads the same sysfs nodes the framework itself reads. Attribute
// names aren't standardized across vendors -- see queryBatteryInfo()'s
// MediaTek-specific fallback (this device exposes batt_vol/batt_temp
// instead of the generic voltage_now/temp, in different units).
struct BatteryInfo {
    bool available = false;
    long capacityPercent = -1;
    std::string status = "unknown";
    std::string health = "unknown";
    std::string technology = "unknown";
    double voltageV = 0.0;
    double temperatureC = 0.0;
};
BatteryInfo queryBatteryInfo(void);
void logBatteryInfo(const BatteryInfo& info);

// --- GL --------------------------------------------------------------
// Queried on a throwaway 1x1-pbuffer EGL context created and torn down
// entirely inside queryGLCapabilities(). At the point this is called (top
// of android_main, before the app's window/render surface exist), there is
// no live render context yet to query instead -- unlike an app that probes
// capabilities after its real context is already up, this can't just query
// glGetString() against "whatever's currently bound".
struct GLLimit {
    const char* name;
    GLint value;
};
struct GLFeature {
    const char* name;
    bool supported;
};
struct GLCapabilities {
    bool valid = false;
    int clientVersion = 0;   // 2 or 3 -- the probe context actually created
    bool requested31 = false;
    bool granted31 = false;  // only meaningful if requested31 is true

    std::string vendor;
    std::string renderer;
    std::string version;
    std::string shadingLanguageVersion;
    std::string extensionsRaw; // unsplit GL_EXTENSIONS, kept for the chunked logcat dump
    std::vector<std::string> extensions;

    std::vector<GLLimit> limits;
    std::vector<GLFeature> features;

    bool hasExtension(const char* name) const;
};
GLCapabilities queryGLCapabilities(void);
void logGLCapabilities(const GLCapabilities& caps);

#endif // __ANDROID__

#endif
