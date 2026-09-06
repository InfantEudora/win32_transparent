#ifndef _RAWINPUT_H_
#define _RAWINPUT_H_
class RawInputSource;

#include <windows.h>
#include <atomic>
#include "InputController.h"

/*
    Keyboard and mouse acquisition through the Win32 Raw Input API, on a thread of its own.

    Why not just use the main window's messages? Because that thread stalls. When the user grabs
    the title bar or a resize edge, DefWindowProc enters a NESTED modal message loop that does not
    return until the drag ends - the same reason rendering and physics already live on their own
    threads here. Input was the last thing still tied to the thread that freezes.

    A window's messages are delivered to the message queue of the thread that CREATED that window,
    so the fix is a message-only window (HWND_MESSAGE parent) created here, on this thread, with
    its own small pump. It is never dragged, never resized, and so never blocks.

    Registration uses RIDEV_INPUTSINK. That flag is required when naming an explicit hwndTarget,
    and it delivers input regardless of which window is in the foreground - which is what we need,
    because a message-only window can never BE the foreground window, so a foreground-mode
    registration pointed at it would deliver nothing at all. Filtering by focus is therefore ours
    to do, in InputController::HasFocus - which is already where it was being done.

    What this buys over polling GetAsyncKeyState: real key edges instead of a level re-sampled once
    per tick (so a tap shorter than a tick can no longer vanish, and it can be recorded and
    replayed faithfully), and unaccelerated, unclipped mouse deltas at device resolution.

    Deliberately NOT registered with RIDEV_NOLEGACY: ImGui's Win32 backend needs the ordinary
    WM_KEYDOWN/WM_CHAR/WM_MOUSE* messages on the main window, and NOLEGACY would suppress them.
    Raw Input therefore runs ALONGSIDE the normal message flow rather than replacing it, and
    InputController is told to stop polling keys and to ignore WM_MOUSEWHEEL so nothing is counted
    twice - see InputController::SetRawInputActive. If Start() fails, none of that happens and the
    old polling path stays in charge.
*/
class RawInputSource{
public:
    ~RawInputSource();

    //Spawns the input thread and blocks until its window exists and the devices are registered,
    //so a caller knows on return whether raw input is actually live. Everything read is submitted
    //to target as InputEvents.
    bool Start(InputController* target);
    void Stop();
    bool IsRunning(){ return f_running; }

private:
    static DWORD WINAPI ThreadFunction(LPVOID param);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateMessageWindow();
    void HandleRawInput(HRAWINPUT raw_handle);

    InputController* input = NULL;
    HWND hwnd = NULL;
    HANDLE thread_handle = NULL;
    DWORD thread_id = 0;
    std::atomic<bool> f_running{false};
    std::atomic<bool> f_setup_done{false};  //setup finished, successfully or not
    std::atomic<bool> f_setup_ok{false};
};

#endif
