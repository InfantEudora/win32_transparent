#include "AndroidSysInfo.h"
#if defined(__ANDROID__)
#include "Debug.h"
#include <android/sensor.h>
#include <android/configuration.h>
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h> // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT (a constant, not a proc address -- no eglGetProcAddress needed)

static Debugger dbg("SysInfo", DEBUG_INFO);

// --- Display ---------------------------------------------------------

DisplayInfo queryDisplayInfo(struct android_app* app) {
    DisplayInfo info;
    if (app->config == NULL) {
        return info;
    }
    info.available = true;
    info.widthDp = AConfiguration_getScreenWidthDp(app->config);
    info.heightDp = AConfiguration_getScreenHeightDp(app->config);
    info.density = AConfiguration_getDensity(app->config);
    info.orientation = AConfiguration_getOrientation(app->config);
    return info;
}

void logDisplayInfo(const DisplayInfo& info) {
    if (!info.available) {
        return;
    }
    dbg.Info("Display: %dx%d dp, density=%d dpi, orientation=%d",
         info.widthDp, info.heightDp, info.density, info.orientation);
}

// activity.getWindow().getDecorView().getRootWindowInsets(), then the four getSystemWindowInset*
// getters. Those are deprecated in favour of getInsets(WindowInsets.Type.systemBars()) from API
// 30, but they still return the right numbers there and they exist all the way back to API 20 --
// one code path for both test devices beats two plus a version check for a value this small.
//
// Every step is null-checked rather than assumed. getRootWindowInsets() returning null is not an
// error, it is what a not-yet-attached decor view legitimately reports, and the caller's fallback
// (use the whole window) is exactly right in that case. ExceptionCheck after each call because a
// pending JNI exception makes every subsequent call undefined behaviour, and an OEM that has
// removed one of these methods should degrade to available=false, not abort the process.
SafeInsets querySafeInsets(struct android_app* app) {
    SafeInsets in;
    if ((app == NULL) || (app->activity == NULL) || (app->activity->vm == NULL)) {
        return in;
    }

    JNIEnv* env = NULL;
    if (app->activity->vm->AttachCurrentThread(&env, NULL) != JNI_OK) {
        dbg.Err("querySafeInsets: AttachCurrentThread failed");
        return in;
    }

    jobject activity     = app->activity->clazz;
    jclass activity_cls  = env->GetObjectClass(activity);
    jmethodID get_window = env->GetMethodID(activity_cls, "getWindow", "()Landroid/view/Window;");
    jobject window       = get_window ? env->CallObjectMethod(activity, get_window) : NULL;

    if (window) {
        jclass window_cls   = env->GetObjectClass(window);
        jmethodID get_decor = env->GetMethodID(window_cls, "getDecorView", "()Landroid/view/View;");
        jobject decor       = get_decor ? env->CallObjectMethod(window, get_decor) : NULL;

        if (decor) {
            jclass view_cls      = env->GetObjectClass(decor);
            jmethodID get_insets = env->GetMethodID(view_cls, "getRootWindowInsets",
                                                    "()Landroid/view/WindowInsets;");
            jobject insets       = get_insets ? env->CallObjectMethod(decor, get_insets) : NULL;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                insets = NULL;
            }

            if (insets) {
                jclass ins_cls = env->GetObjectClass(insets);
                jmethodID m_l  = env->GetMethodID(ins_cls, "getSystemWindowInsetLeft",   "()I");
                jmethodID m_t  = env->GetMethodID(ins_cls, "getSystemWindowInsetTop",    "()I");
                jmethodID m_r  = env->GetMethodID(ins_cls, "getSystemWindowInsetRight",  "()I");
                jmethodID m_b  = env->GetMethodID(ins_cls, "getSystemWindowInsetBottom", "()I");

                if (m_l && m_t && m_r && m_b) {
                    in.left   = (int32_t)env->CallIntMethod(insets, m_l);
                    in.top    = (int32_t)env->CallIntMethod(insets, m_t);
                    in.right  = (int32_t)env->CallIntMethod(insets, m_r);
                    in.bottom = (int32_t)env->CallIntMethod(insets, m_b);
                    in.available = !env->ExceptionCheck();
                    if (!in.available) {
                        env->ExceptionClear();
                    }
                }
                env->DeleteLocalRef(ins_cls);
                env->DeleteLocalRef(insets);
            } else {
                dbg.Info("querySafeInsets: getRootWindowInsets() returned null (view not attached yet)");
            }
            env->DeleteLocalRef(view_cls);
            env->DeleteLocalRef(decor);
        }
        env->DeleteLocalRef(window_cls);
        env->DeleteLocalRef(window);
    }
    env->DeleteLocalRef(activity_cls);

    app->activity->vm->DetachCurrentThread();
    return in;
}

void logSafeInsets(const SafeInsets& insets) {
    if (!insets.available) {
        dbg.Info("Safe insets: unavailable, using the whole window");
        return;
    }
    dbg.Info("Safe insets: left=%d top=%d right=%d bottom=%d px",
         insets.left, insets.top, insets.right, insets.bottom);
}

// --- Sensors -------------------------------------------------------------

SensorCapabilities querySensorCapabilities(void) {
    SensorCapabilities caps;

    ASensorManager* manager = ASensorManager_getInstance();
    if (manager == NULL) {
        dbg.Info("No ASensorManager available");
        return caps;
    }

    ASensorList list;
    int count = ASensorManager_getSensorList(manager, &list);
    caps.sensors.reserve(count > 0 ? count : 0);
    for (int i = 0; i < count; i++) {
        const ASensor* sensor = list[i];
        SensorInfo info;
        info.name = ASensor_getName(sensor);
        info.vendor = ASensor_getVendor(sensor);
        info.type = ASensor_getType(sensor);
        info.resolution = ASensor_getResolution(sensor);
        info.minDelayUs = ASensor_getMinDelay(sensor);
        caps.sensors.push_back(std::move(info));
    }
    return caps;
}

void logSensorCapabilities(const SensorCapabilities& caps) {
    dbg.Info("Found %zu sensors:", caps.sensors.size());
    for (size_t i = 0; i < caps.sensors.size(); i++) {
        const SensorInfo& s = caps.sensors[i];
        dbg.Info("  [%zu] %s (vendor=%s, type=%d, resolution=%.6f, minDelay=%dus)",
             i, s.name.c_str(), s.vendor.c_str(), s.type, s.resolution, s.minDelayUs);
    }
}

// --- CPU -------------------------------------------------------------

static bool readSysfsLong(const char* path, long* out) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = fscanf(f, "%ld", out) == 1;
    fclose(f);
    return ok;
}

static bool readSysfsLine(const char* path, char* buf, size_t bufSize) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = fgets(buf, (int)bufSize, f) != NULL;
    fclose(f);
    if (!ok) {
        return false;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return true;
}

// cpu0 on most devices has no "online" node at all (it can't be offlined),
// so treat a missing file as "online" rather than "unknown".
static bool readSysfsOnline(int coreIndex) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", coreIndex);
    long value = 1;
    readSysfsLong(path, &value);
    return value != 0;
}

// Reads /sys/devices/system/cpu/cpuN/cpufreq/{scaling_cur_freq,scaling_min_freq,
// scaling_max_freq,scaling_governor} for every configured core. These are
// the same nodes the framework's own CPU governor exposes; no root or
// special permission is needed to read them on this device. An offline
// core's cpufreq directory is typically gone entirely (big.LITTLE parts
// hotplug the big cluster off when idle rather than just clocking it
// down), so its freq entries come back as -1 independent of the explicit
// `online` read above.
void getCpuFreqInfo(CpuFreqInfo* info) {
    memset(info, 0, sizeof(*info));

    long coresConfigured = sysconf(_SC_NPROCESSORS_CONF);
    if (coresConfigured <= 0) {
        coresConfigured = 1;
    }
    if (coresConfigured > MAX_CPU_CORES) {
        coresConfigured = MAX_CPU_CORES;
    }
    info->coreCount = (int)coresConfigured;

    char path[128];
    for (int i = 0; i < info->coreCount; i++) {
        info->online[i] = readSysfsOnline(i);

        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        if (!readSysfsLong(path, &info->curKhz[i])) {
            info->curKhz[i] = -1;
        }
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", i);
        if (!readSysfsLong(path, &info->minKhz[i])) {
            info->minKhz[i] = -1;
        }
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
        if (!readSysfsLong(path, &info->maxKhz[i])) {
            info->maxKhz[i] = -1;
        }
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        if (!readSysfsLine(path, info->governor[i], sizeof(info->governor[i]))) {
            snprintf(info->governor[i], sizeof(info->governor[i]), "?");
        }
    }
}

void logCpuFreqInfo(const CpuFreqInfo& info) {
    long onlineCount = 0;
    for (int i = 0; i < info.coreCount; i++) {
        if (info.online[i]) onlineCount++;
    }
    dbg.Info("CPU: %ld online / %d configured cores", onlineCount, info.coreCount);
    for (int i = 0; i < info.coreCount; i++) {
        if (!info.online[i]) {
            dbg.Info("  cpu%d: offline", i);
            continue;
        }
        dbg.Info("  cpu%d: cur=%ld min=%ld max=%ld kHz governor=%s",
             i, info.curKhz[i], info.minKhz[i], info.maxKhz[i], info.governor[i]);
    }
}

// --- Battery ---------------------------------------------------------

BatteryInfo queryBatteryInfo(void) {
    BatteryInfo info;
    const char* base = "/sys/class/power_supply/battery";
    char path[256];

    snprintf(path, sizeof(path), "%s/capacity", base);
    bool hasCapacity = readSysfsLong(path, &info.capacityPercent);

    char statusBuf[64] = "unknown";
    snprintf(path, sizeof(path), "%s/status", base);
    bool hasStatus = readSysfsLine(path, statusBuf, sizeof(statusBuf));
    info.status = statusBuf;

    if (!hasCapacity && !hasStatus) {
        dbg.Info("Battery: sysfs info not available at %s", base);
        return info;
    }
    info.available = true;

    char technologyBuf[32] = "unknown";
    snprintf(path, sizeof(path), "%s/technology", base);
    readSysfsLine(path, technologyBuf, sizeof(technologyBuf));
    info.technology = technologyBuf;

    char healthBuf[32] = "unknown";
    snprintf(path, sizeof(path), "%s/health", base);
    readSysfsLine(path, healthBuf, sizeof(healthBuf));
    info.health = healthBuf;

    // Vendor names for voltage/temp aren't standardized: this device
    // (MediaTek) exposes batt_vol/batt_temp instead of the generic
    // voltage_now/temp, and batt_vol is millivolts rather than
    // voltage_now's microvolts, so each candidate carries its own
    // conversion factor to volts.
    static const struct { const char* name; double voltsPerUnit; } voltageCandidates[] = {
        { "voltage_now", 1.0 / 1000000.0 },
        { "batt_vol", 1.0 / 1000.0 },
    };
    for (const auto& candidate : voltageCandidates) {
        long raw;
        snprintf(path, sizeof(path), "%s/%s", base, candidate.name);
        if (readSysfsLong(path, &raw)) {
            info.voltageV = raw * candidate.voltsPerUnit;
            break;
        }
    }

    static const char* tempCandidates[] = { "temp", "batt_temp" };
    for (const char* candidate : tempCandidates) {
        long raw;
        snprintf(path, sizeof(path), "%s/%s", base, candidate);
        if (readSysfsLong(path, &raw)) {
            info.temperatureC = raw / 10.0;
            break;
        }
    }

    return info;
}

void logBatteryInfo(const BatteryInfo& info) {
    if (!info.available) {
        return;
    }
    dbg.Info("Battery: %ld%% status=%s health=%s tech=%s voltage=%.3fV temp=%.1fC",
         info.capacityPercent, info.status.c_str(), info.health.c_str(),
         info.technology.c_str(), info.voltageV, info.temperatureC);
}

// --- GL ----------------------------------------------------------------

bool GLCapabilities::hasExtension(const char* name) const {
    for (const std::string& ext : extensions) {
        if (ext == name) return true;
    }
    return false;
}

// Some GL implementations (and this device's older bionic libc, which
// doesn't guard %s against NULL the way glibc's "(null)" fallback does)
// will segfault formatting a NULL glGetString() result directly -- wrap it
// so a query the driver doesn't support just logs as "(unsupported)"
// instead of crashing the whole process.
static const char* safeGlString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? (const char*)s : "(unsupported)";
}

// Splits the space-separated GL_EXTENSIONS string.
//
// This used to be three lines of std::istringstream, and those three lines cost about a
// quarter of a megabyte. Constructing ANY stream instantiates std::locale, and std::locale
// builds every standard facet -- so an istringstream here is why money_get, money_put and
// time_get, for char AND wchar_t, were linked into a game. Measured across the whole .so the
// stream/locale cone was 248 KB; this and tinygltf's writer were the only two things asking
// for it.
//
// The replacement is deliberately dull: scan, skip runs of whitespace, emit what is between.
// Extension names are ASCII tokens with no quoting or escapes, so there is nothing a stream
// was doing for us here beyond the splitting.
static std::vector<std::string> splitExtensions(const char* extString) {
    std::vector<std::string> result;
    if (extString == nullptr) return result;
    const char* p = extString;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; }
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') { p++; }
        if (p > start) { result.push_back(std::string(start, (size_t)(p - start))); }
    }
    return result;
}

GLCapabilities queryGLCapabilities(void) {
    GLCapabilities caps;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        dbg.Info("EGL: no display available");
        return caps;
    }

    EGLint numConfigs = 0;
    EGLConfig config;
    caps.clientVersion = 3;
    const EGLint es3Attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig(display, es3Attribs, &config, 1, &numConfigs) || numConfigs == 0) {
        const EGLint es2Attribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_NONE
        };
        caps.clientVersion = 2;
        if (!eglChooseConfig(display, es2Attribs, &config, 1, &numConfigs) || numConfigs == 0) {
            dbg.Info("EGL: no suitable config found");
            eglTerminate(display);
            return caps;
        }
    }

    const EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);

    // SSBOs (needed for morph targets, see core/Mesh.cpp) require ES 3.1, not
    // just 3.0 -- EGL_OPENGL_ES3_BIT above only guarantees *at least* 3.0, so
    // ask for 3.1 explicitly via EGL_CONTEXT_MAJOR/MINOR_VERSION (EGL 1.5) and
    // see whether the driver actually grants it instead of assuming it does.
    EGLContext context = EGL_NO_CONTEXT;
    if (caps.clientVersion == 3) {
        caps.requested31 = true;
        const EGLint contextAttribs31[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 1,
            EGL_NONE
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs31);
    }
    if (context == EGL_NO_CONTEXT) {
        const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, caps.clientVersion, EGL_NONE };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    }

    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        dbg.Info("EGL: failed to create context (requested ES%d)", caps.clientVersion);
    } else {
        caps.valid = true;
        if (caps.requested31) {
            caps.granted31 = strstr(safeGlString(GL_VERSION), "3.1") != NULL;
        }

        caps.vendor = safeGlString(GL_VENDOR);
        caps.renderer = safeGlString(GL_RENDERER);
        caps.version = safeGlString(GL_VERSION);
        caps.shadingLanguageVersion = safeGlString(GL_SHADING_LANGUAGE_VERSION);

        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
        caps.extensionsRaw = extensions != nullptr ? extensions : "";
        caps.extensions = splitExtensions(extensions);

        auto addLimit = [&](const char* name, GLenum pname) {
            GLint value = 0;
            glGetIntegerv(pname, &value);
            caps.limits.push_back({name, value});
        };
        auto addFeature = [&](const char* name, bool supported) {
            caps.features.push_back({name, supported});
        };

        addLimit("Max texture size", GL_MAX_TEXTURE_SIZE);
        addLimit("Max texture image units", GL_MAX_TEXTURE_IMAGE_UNITS);
        addLimit("Max vertex attribs", GL_MAX_VERTEX_ATTRIBS);
        addLimit("Max renderbuffer size", GL_MAX_RENDERBUFFER_SIZE);

        // Extra texture-specific limits ahead of porting core/Texture.cpp to
        // GLES (see docs/gles31-texture-limits.md, written from this output).
        addLimit("Max combined texture image units", GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
        addLimit("Max vertex texture image units", GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS);
        addLimit("Max cube map texture size", GL_MAX_CUBE_MAP_TEXTURE_SIZE);
        addLimit("Max array texture layers", GL_MAX_ARRAY_TEXTURE_LAYERS);
        addLimit("Max 3D texture size", GL_MAX_3D_TEXTURE_SIZE);
        addLimit("Max samples", GL_MAX_SAMPLES);

        bool hasAniso = caps.hasExtension("GL_EXT_texture_filter_anisotropic");
        addFeature("Anisotropic filtering (EXT_texture_filter_anisotropic)", hasAniso);
        if (hasAniso) {
            GLfloat maxAniso = 0.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            caps.limits.push_back({"Max anisotropy", (GLint)maxAniso});
        }

        addFeature("ASTC compressed textures", caps.hasExtension("GL_KHR_texture_compression_astc_ldr") ||
                                                 caps.hasExtension("GL_OES_texture_compression_astc"));
        addFeature("S3TC/DXT compressed textures", caps.hasExtension("GL_EXT_texture_compression_dxt1") ||
                                                     caps.hasExtension("GL_EXT_texture_compression_s3tc"));
        addFeature("PVRTC compressed textures", caps.hasExtension("GL_IMG_texture_compression_pvrtc"));
        // ETC2/EAC is core since ES 3.0, unconditionally available whenever clientVersion == 3.
        addFeature("Sampling-only float textures (not framebuffer-attachable)", caps.hasExtension("GL_OES_texture_float"));
        addFeature("Sampling-only half-float textures (not framebuffer-attachable)", caps.hasExtension("GL_OES_texture_half_float"));
        addFeature("Multisampled render-to-texture", caps.hasExtension("GL_EXT_multisampled_render_to_texture") ||
                                                        caps.hasExtension("GL_IMG_multisampled_render_to_texture"));

        // UBOs (unlike SSBOs) are core since ES 3.0, so this isn't gated on
        // requested31 -- relevant now that materials/instance data use a
        // UBO because this device's SSBOs are compute-stage-only (see below).
        if (caps.clientVersion == 3) {
            addLimit("Max uniform block size (bytes)", GL_MAX_UNIFORM_BLOCK_SIZE);
            addLimit("Max vertex uniform blocks", GL_MAX_VERTEX_UNIFORM_BLOCKS);
            addLimit("Max fragment uniform blocks", GL_MAX_FRAGMENT_UNIFORM_BLOCKS);
            addLimit("Max uniform buffer bindings", GL_MAX_UNIFORM_BUFFER_BINDINGS);
        }

        if (caps.requested31) {
            GLint maxCombinedSsbo = 0, maxSsboBindings = 0;
            addLimit("Max vertex shader storage blocks", GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS);
            addLimit("Max fragment shader storage blocks", GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS);
            addLimit("Max compute shader storage blocks", GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS);
            glGetIntegerv(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS, &maxCombinedSsbo);
            glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxSsboBindings);
            caps.limits.push_back({"Max combined shader storage blocks", maxCombinedSsbo});
            caps.limits.push_back({"Max shader storage buffer bindings", maxSsboBindings});
            // A driver that doesn't actually support SSBOs leaves these at 0
            // rather than raising a GL error, so a positive binding count is
            // the signal to trust here, not just "did context creation succeed".
            addFeature("Shader storage buffers (SSBO)", maxSsboBindings > 0);
        }

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (context != EGL_NO_CONTEXT) {
        eglDestroyContext(display, context);
    }
    if (surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, surface);
    }
    eglTerminate(display);

    return caps;
}

void logGLCapabilities(const GLCapabilities& caps) {
    if (!caps.valid) {
        dbg.Info("EGL: failed to create context (requested ES%d)", caps.clientVersion);
        return;
    }

    if (caps.requested31) {
        dbg.Info("EGL: ES 3.1 context %s", caps.granted31 ? "granted" : "denied (fell back)");
    }
    dbg.Info("GL_VENDOR: %s", caps.vendor.c_str());
    dbg.Info("GL_RENDERER: %s", caps.renderer.c_str());
    dbg.Info("GL_VERSION: %s", caps.version.c_str());
    dbg.Info("GL_SHADING_LANGUAGE_VERSION: %s", caps.shadingLanguageVersion.c_str());

    for (const GLLimit& limit : caps.limits) {
        dbg.Info("  %s: %d", limit.name, limit.value);
    }
    for (const GLFeature& feature : caps.features) {
        dbg.Info("  %s: %s", feature.name, feature.supported ? "yes" : "no");
    }

    dbg.Info("GL_EXTENSIONS (%zu):", caps.extensions.size());
    // A single dbg.Info() call truncates well before the full string for a
    // device with ~70 extensions (Android's per-line log length limit),
    // which silently ate the tail of the list last time. Print in chunks
    // instead, breaking only at spaces so no extension name is split
    // across two lines.
    const char* extensions = caps.extensionsRaw.c_str();
    const size_t CHUNK = 200;
    const char* lineStart = extensions;
    while (*lineStart) {
        size_t len = strlen(lineStart);
        size_t take = len < CHUNK ? len : CHUNK;
        if (take < len) {
            while (take > 0 && lineStart[take] != ' ') {
                take--;
            }
            if (take == 0) { // one extension name longer than CHUNK; take it whole
                take = strcspn(lineStart, " ");
                if (take == 0 || lineStart[take] == '\0') {
                    take = len;
                }
            }
        }
        dbg.Info("  %.*s", (int)take, lineStart);
        lineStart += take;
        while (*lineStart == ' ') {
            lineStart++;
        }
    }
}
#endif // __ANDROID__
