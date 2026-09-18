#include "InputController.h"
#if defined(__ANDROID__)
#include "Debug.h"

#include <android/input.h>
#include <android/keycodes.h>
#include <android/sensor.h>
#include <android_native_app_glue.h>
#include <stdio.h>
#include <time.h>


static Debugger* debug = new Debugger("InputController", DEBUG_INFO);

// Radians of rotation per pixel dragged; ~1 full turn per screen width.
#define DRAG_SENSITIVITY 0.006f

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char* motion_action_name(int32_t action) {
    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:         return "DOWN";
        case AMOTION_EVENT_ACTION_UP:            return "UP";
        case AMOTION_EVENT_ACTION_MOVE:          return "MOVE";
        case AMOTION_EVENT_ACTION_CANCEL:        return "CANCEL";
        case AMOTION_EVENT_ACTION_POINTER_DOWN:  return "POINTER_DOWN";
        case AMOTION_EVENT_ACTION_POINTER_UP:    return "POINTER_UP";
        default:                                 return "OTHER";
    }
}

static const char* tool_type_name(int32_t tool) {
    switch (tool) {
        case AMOTION_EVENT_TOOL_TYPE_FINGER: return "finger";
        case AMOTION_EVENT_TOOL_TYPE_STYLUS: return "stylus";
        case AMOTION_EVENT_TOOL_TYPE_MOUSE:  return "mouse";
        case AMOTION_EVENT_TOOL_TYPE_ERASER: return "eraser";
        default:                             return "unknown";
    }
}

// The device's 5 touch-panel virtual keys -- confirmed via a raw `getevent`
// capture to be synthesized by the touch controller itself (device "mtk-tpd"
// reports both ABS touch coordinates and these KEY codes), not separate
// hardware buttons. This device's own key layout (/system/usr/keylayout/
// mtk-tpd.kl) remaps raw Linux KEY_MENU (139) to Android's APP_SWITCH
// instead of the generic MENU -- confirmed via `dumpsys window`/WindowManager
// logs showing it (like HOME) jump window focus straight to
// com.android.systemui's RecentsActivity, with no KeyEvent ever reaching a
// regular app's input queue.
static const char* keycode_name(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_BACK:        return "BACK";
        case AKEYCODE_APP_SWITCH:  return "APP_SWITCH";
        case AKEYCODE_HOME:        return "HOME";
        case AKEYCODE_VOLUME_UP:   return "VOLUME_UP";
        case AKEYCODE_VOLUME_DOWN: return "VOLUME_DOWN";
        default:                   return "OTHER";
    }
}

void InputController::InitSensors(struct android_app* app) {
    sensor_manager = ASensorManager_getInstance();
    if (sensor_manager == NULL) {
        debug->Info("Sensors: no ASensorManager available");
        return;
    }
    gyroscope = ASensorManager_getDefaultSensor(sensor_manager, ASENSOR_TYPE_GYROSCOPE);
    accelerometer = ASensorManager_getDefaultSensor(sensor_manager, ASENSOR_TYPE_ACCELEROMETER);
    if (gyroscope == NULL && accelerometer == NULL) {
        debug->Info("Sensors: device has neither a gyroscope nor an accelerometer");
        return;
    }

    // One queue for both sensors -- DrainSensorEvents() tells them apart via
    // each ASensorEvent's own `type` field, same queue/looper setup the
    // gyro alone used to use.
    sensor_queue = ASensorManager_createEventQueue(sensor_manager, app->looper, LOOPER_ID_USER, NULL, NULL);
    if (gyroscope != NULL) {
        ASensorEventQueue_enableSensor(sensor_queue, gyroscope);
        ASensorEventQueue_setEventRate(sensor_queue, gyroscope, 1000000 / 60); // ~60 Hz
        debug->Info("Sensors: gyroscope enabled (%s)", ASensor_getName(gyroscope));
    }
    if (accelerometer != NULL) {
        ASensorEventQueue_enableSensor(sensor_queue, accelerometer);
        ASensorEventQueue_setEventRate(sensor_queue, accelerometer, 1000000 / 60); // ~60 Hz
        debug->Info("Sensors: accelerometer enabled (%s)", ASensor_getName(accelerometer));
    }
}

void InputController::DrainSensorEvents() {
    if (sensor_queue == NULL) {
        return;
    }
    static double last_log_time = 0.0;
    ASensorEvent event;
    while (ASensorEventQueue_getEvents(sensor_queue, &event, 1) > 0) {
        if (event.type == ASENSOR_TYPE_GYROSCOPE) {
            latest_gyro[0] = event.vector.x;
            latest_gyro[1] = event.vector.y;
            latest_gyro[2] = event.vector.z;
        } else if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            latest_accel[0] = event.acceleration.x;
            latest_accel[1] = event.acceleration.y;
            latest_accel[2] = event.acceleration.z;
        }
    }

    double t = now_seconds();
    static int sensor_logs = 5; //Only log 5 sensor events to console.
    if ((t - last_log_time > 1.0) && (sensor_logs-- > -1)) {
        last_log_time = t;
        debug->Info("Gyro: x=%.3f y=%.3f z=%.3f rad/s, Accel: x=%.3f y=%.3f z=%.3f m/s^2",
            latest_gyro[0], latest_gyro[1], latest_gyro[2],
            latest_accel[0], latest_accel[1], latest_accel[2]);
    }
}

// DOWN/UP/POINTER_* transitions always log; MOVE is throttled since a drag
// can fire well over 60 events/sec and would otherwise flood logcat.
int32_t InputController::HandleInputEvent(AInputEvent* event, bool suppress_drag) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t keycode = AKeyEvent_getKeyCode(event);
        int32_t key_action = AKeyEvent_getAction(event);

        snprintf(last_key.name, sizeof(last_key.name), "%s", keycode_name(keycode));
        snprintf(last_key.str_action, sizeof(last_key.str_action), "%s",
                 key_action == AKEY_EVENT_ACTION_DOWN ? "DOWN" :
                 key_action == AKEY_EVENT_ACTION_UP ? "UP" : "OTHER");
        last_key.keycode = keycode;
        last_key.action = key_action;

        // Feed the mapping machinery. This is the Android counterpart of the Win32
        // SubmitSystemKey() call in PollDevices(): from here on a device button is
        // indistinguishable from a PC key or a gamepad face button -- same KeyState,
        // same edges, same recording, same scripted override.
        if (key_action == AKEY_EVENT_ACTION_DOWN || key_action == AKEY_EVENT_ACTION_UP) {
            SubmitSystemKey((uint32_t)keycode, key_action == AKEY_EVENT_ACTION_DOWN);
        }

        // BACK/VOLUME_UP/VOLUME_DOWN arrive here as virtual keys the touch
        // panel driver synthesizes mid-drag whenever a finger's raw position
        // crosses into one of the 5 button hotspots at the bottom of the
        // panel -- consuming them (returning 1) only stops their default
        // system action (BACK finishing the activity, etc); it can't stop
        // the touch controller from generating the key event in the first
        // place, and it doesn't interrupt the simultaneous MotionEvent drag
        // stream below, which is a fully independent event.
        //
        // HOME and APP_SWITCH are NOT handled here, and checking for them
        // would be dead code: both are intercepted by the system before a
        // regular app's input queue ever sees them (confirmed via
        // WindowManager logs jumping straight to Launcher3/RecentsActivity),
        // same as a normal Android nav-bar Home/Recents button.
        if (keycode == AKEYCODE_BACK ||
            keycode == AKEYCODE_VOLUME_UP || keycode == AKEYCODE_VOLUME_DOWN) {
            if (key_action == AKEY_EVENT_ACTION_DOWN) {
                debug->Info("Key: %s DOWN (suppressed)", keycode_name(keycode));
            }
            return 1;
        }
        return 0;
    }

    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
        return 0;
    }
    if ((AInputEvent_getSource(event) & AINPUT_SOURCE_TOUCHSCREEN) == 0) {
        return 0;
    }

    const int32_t raw_action = AMotionEvent_getAction(event);
    int32_t action = raw_action & AMOTION_EVENT_ACTION_MASK;
    //WHICH pointer this event is about, for POINTER_DOWN/POINTER_UP. The action word packs it
    //into its high bits; masking it off (as the line above does, and as this file used to do
    //exclusively) answers "what happened" but throws away "to whom". That is fine for counting
    //fingers and useless for tracking them.
    const size_t action_index = (size_t)((raw_action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                         >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    //int is_discrete = action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_UP ||
    //                   action == AMOTION_EVENT_ACTION_POINTER_DOWN || action == AMOTION_EVENT_ACTION_POINTER_UP ||
    //                   action == AMOTION_EVENT_ACTION_CANCEL;

    size_t raw_pointer_count = AMotionEvent_getPointerCount(event);
    if (raw_pointer_count > INPUT_CONTROLLER_MAX_TOUCHES) {
        raw_pointer_count = INPUT_CONTROLLER_MAX_TOUCHES; // this device reports at most 5, but clamp defensively
    }

    pointer_count = (int)raw_pointer_count;
    // AMotionEvent_getPointerCount() for ACTION_UP/CANCEL still reports the
    // count *including* the pointer that's leaving (1 for an ordinary
    // single-finger tap) -- without this override, pointer_count would
    // never actually return to 0 after the very first touch ever seen,
    // permanently reading "1 finger down" even with nothing touching the
    // screen (confirmed: this silently broke both the "Touch:" ImGui
    // display and Scene::DrawFrame()'s new picking-readback gate, which
    // relies on this field to detect "no finger down"). ACTION_POINTER_UP
    // (one of *several* fingers lifting, not the last one) deliberately
    // isn't included here -- raw_pointer_count already reflects however
    // many fingers remain down in that case.
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        pointer_count = 0;
    }

    // Fill touch[0..raw_pointer_count-1] regardless of the UP/CANCEL
    // override above -- same reasoning as the old single-touch code this
    // replaced: position/tool/etc for the departing pointer(s) should still
    // reflect where they last were, only pointer_count itself needs to say
    // "nothing's down anymore".

    touch_midpoint = vec2();
    for (size_t i = 0; i < raw_pointer_count; i++) {
        TouchState& t = touch[i];
        snprintf(t.str_action, sizeof(t.str_action), "%s", motion_action_name(action));
        t.action = action;
        t.id = AMotionEvent_getPointerId(event, i);
        t.x = AMotionEvent_getX(event, i);
        t.y = AMotionEvent_getY(event, i);
        touch_midpoint += vec2(t.x, t.y);
        t.pressure = AMotionEvent_getPressure(event, i);
        t.size = AMotionEvent_getSize(event, i);
        snprintf(t.tool, sizeof(t.tool), "%s", tool_type_name(AMotionEvent_getToolType(event, i)));

        // Skip drag rotation while the caller says to suppress it (e.g. ImGui
        // is capturing this touch), so dragging a slider doesn't also spin the
        // scene underneath it.
        if (suppress_drag) {
            t.drag_active = 0;
        }else if (action == AMOTION_EVENT_ACTION_DOWN) {
            t.drag_active = 1;
            t.drag_last_x = AMotionEvent_getX(event, 0);
            t.drag_last_y = AMotionEvent_getY(event, 0);
        }else if (action == AMOTION_EVENT_ACTION_MOVE && t.drag_active) {
            //float x = AMotionEvent_getX(event, i);
            //float y = AMotionEvent_getY(event, i);
            t.drag_rot_x = (t.x - t.drag_last_x) * DRAG_SENSITIVITY;
            t.drag_rot_y = (t.y - t.drag_last_y) * DRAG_SENSITIVITY;
            t.drag_last_x = t.x;
            t.drag_last_y = t.y;
        }else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            t.drag_active = 0;
        }
    }
    touch_midpoint /= (float)raw_pointer_count;

    /*
        Feed the on-screen button panel (see InputController::AddTouchButton).

        BY POINTER ID, NEVER BY INDEX. The touch[] array above is indexed by the pointer's index
        in the event, which is what it wants -- it is a snapshot of "the fingers currently down".
        Ownership is a different question: indices are compacted when a finger lifts, so the
        finger that was index 1 BECOMES index 0, and a panel tracking by index would hand button
        ownership to the wrong finger at exactly the moment two fingers are down. Which is the
        only moment any of this matters.

        Edges only. MOVE is forwarded so the panel could act on it later (it deliberately does
        not today -- a captured button is held until its own pointer lifts), and CANCEL releases
        everything, because it is the one ending that delivers no ordinary UP.
    */
    switch (action){
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            //action_index is 0 for DOWN and the arriving finger for POINTER_DOWN. The bound check
            //matters for a device reporting more simultaneous pointers than we clamp to.
            if (action_index < raw_pointer_count){
                SubmitPointer(AMotionEvent_getPointerId(event,action_index),
                              AMotionEvent_getX(event,action_index),
                              AMotionEvent_getY(event,action_index),true);
            }
        break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            if (action_index < raw_pointer_count){
                SubmitPointer(AMotionEvent_getPointerId(event,action_index),
                              AMotionEvent_getX(event,action_index),
                              AMotionEvent_getY(event,action_index),false);
            }
        break;
        case AMOTION_EVENT_ACTION_MOVE:
            //One event carries a new position for every pointer still down, not just one.
            for (size_t i = 0; i < raw_pointer_count; i++){
                SubmitPointer(AMotionEvent_getPointerId(event,i),
                              AMotionEvent_getX(event,i),
                              AMotionEvent_getY(event,i),true);
            }
        break;
        case AMOTION_EVENT_ACTION_CANCEL:
            ReleaseAllTouchPointers();
        break;
        default:
        break;
    }

    //We'll comupte the direction the two fingers are around the midpoint. This is used to rotate the camera around the midpoint.
    if (pointer_count == 2) {
        TouchState& t0 = touch[0];
        TouchState& t1 = touch[1];
        vec2 v0 = vec2(t0.x, t0.y) - touch_midpoint;
        v0.normalize();
        vec2 v1 = vec2(t1.x, t1.y) - touch_midpoint;
        v1.normalize();
        vec2 diff = vec2(t0.x, t0.y) - vec2(t1.x, t1.y);
        float distance = diff.length();

        //Distance is zero the first time we see two fingers, so we don't want to use that as a delta.
        if (two_finger_distance == 0.0f) {
            two_finger_distance_delta = 0.0f;
        } else {
            two_finger_distance_delta = distance - two_finger_distance;
        }
        two_finger_distance = distance;
        two_finger_vector = vec2(t0.x, t0.y) - touch_midpoint;
        two_finger_vector.normalize();
    }else {
        two_finger_vector = vec2();
        two_finger_distance = 0.0f;
        two_finger_distance_delta = 0.0f;
    }
    return 1;
}


//--- Acquisition: the Android half of what InputController_win32 does with XInput/RawInput ------
//
//The portable core (win32_core/InputController.cpp) calls PollDevices() once per physics-thread
//pass, inside Renderer::physics_mutex, before the tick runs -- see Scene::StartPhysicsThread. That
//ordering is the whole point: every source of input lands in KeyState at the same instant relative
//to the simulation, so a run driven by a finger, by a sensor, or by a scripted HoldKey is the same
//run as far as the game is concerned.
//
//Key events do NOT come through here -- Android pushes those at us via HandleInputEvent (called
//from Application::HandleInput), which submits them directly. This function covers the things that
//must be POLLED rather than delivered: the sensors.
void InputController::PollDevices(){
    //NOTE: this deliberately does NOT call DrainSensorEvents(). The sensor queue is owned by the
    //ALooper the activity created on the main/render thread, and Application::Run drains it there
    //when the looper reports LOOPER_ID_USER -- which is how Android intends sensor delivery to
    //work. Draining the same queue from this thread as well would be two threads racing on it.
    //What crosses the thread boundary is only the plain floats below (latest_gyro/latest_accel),
    //which is already how HelloScene reads them today.

    //Sensors as analog axes. analog_values[] is exactly where PollGamepad puts XInput thumbstick
    //values on Windows, so a sensor mapped with AddGamePadMap gets the same dead zone, the same
    //-1..1 normalisation, the same GetAxis, and can be overridden by a scripted HoldAxis -- with
    //nothing downstream aware that there is no thumbstick. Scaled to XInput's +/-32767 range for
    //that reason, rather than inventing a second convention.
    //
    //Gyro is rad/s; +/-8 rad/s is a brisk deliberate twist, so that is full deflection.
    const float GYRO_FULL_SCALE = 8.0f;
    //Accelerometer is m/s^2 including gravity; 1g full scale means "tilted 90 degrees".
    const float ACCEL_FULL_SCALE = 9.81f;

    auto to_axis = [](float v, float full_scale) -> int {
        float n = v / full_scale;
        if (n > 1.0f)  n = 1.0f;
        if (n < -1.0f) n = -1.0f;
        return (int)(n * 32767.0f);
    };

    if (GAMEPAD_MAX_ANALOG_VALUES >= 6){
        analog_values[0] = to_axis(latest_gyro[0],  GYRO_FULL_SCALE);
        analog_values[1] = to_axis(latest_gyro[1],  GYRO_FULL_SCALE);
        analog_values[2] = to_axis(latest_gyro[2],  GYRO_FULL_SCALE);
        analog_values[3] = to_axis(latest_accel[0], ACCEL_FULL_SCALE);
        analog_values[4] = to_axis(latest_accel[1], ACCEL_FULL_SCALE);
        analog_values[5] = to_axis(latest_accel[2], ACCEL_FULL_SCALE);
    }

    //Turn those into INPUT_EVENT_AXIS_SCALAR events for whatever has been mapped. Shared with the
    //Windows path -- it is the same function PollGamepad calls after reading XInput.
    SubmitAnalogAxes();
}

//No hot-pluggable input device list on this platform yet: the sensors are fixed hardware and are
//reported by AndroidSysInfo's Platform Sensors tab already. Kept so the portable core and any
//debug UI can call it unconditionally.
void InputController::ListDevices(){
    debug->Info("ListDevices: android -- gyroscope %s, accelerometer %s\n",
                gyroscope ? "present" : "absent",
                accelerometer ? "present" : "absent");
}

//The MDT740 has no rumble motors. Accepted and ignored so app code that sets rumble is portable.
void InputController::SendMotorData(int l, int r){
    lmotor = l;
    rmotor = r;
}

//Windows-only concept (RawInput vs GetAsyncKeyState fallback). Android delivers key events through
//the activity's input queue with no such choice to make.
void InputController::SetRawInputActive(bool active){
    (void)active;
}

#endif // __ANDROID__
