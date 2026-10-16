#ifndef _KEYCODES_H_
#define _KEYCODES_H_

/*
    THE DESKTOP KEYCODE SPACE IS WIN32'S VIRTUAL-KEY CODES, on every desktop platform.

    A system keycode is what AddKeyMap binds and what a recording stores (InputRecording.h), so it has
    to mean the same key wherever a binding is written or a recording replayed - one number per key,
    not one per platform. On Windows the VK_ codes already were that number, and every app binds by
    them, so the other desktop platforms adopt them rather than inventing a third set: a Linux window
    backend translates its own key events into these, as the Win32 one passes them straight on.

    The XINPUT_GAMEPAD_* fallbacks in InputController.h are the same idea for pad buttons. Android is
    the exception and stays one - its keys are AKEYCODE_ numbers, see InputController's constructor.

    ONE LIST, CHECKED BY THE COMPILER. On Windows <winuser.h> defines these names and every row below
    becomes a static_assert against it, so a value mistyped here fails the Windows build instead of
    quietly binding the wrong key on Linux. Elsewhere the same rows become enumerators. The values were
    read out of MinGW's winuser.h, not typed. Letters and digits are not listed: they are their own
    ASCII codes ('A', '7'), as on Win32.
*/
#include <stdint.h>

#define ENGINE_KEYCODES(X) \
    X(VK_LBUTTON,0x01) X(VK_RBUTTON,0x02) X(VK_MBUTTON,0x04) X(VK_XBUTTON1,0x05) \
    X(VK_XBUTTON2,0x06) X(VK_BACK,0x08) X(VK_TAB,0x09) X(VK_CLEAR,0x0C) X(VK_RETURN,0x0D) \
    X(VK_SHIFT,0x10) X(VK_CONTROL,0x11) X(VK_MENU,0x12) X(VK_PAUSE,0x13) X(VK_CAPITAL,0x14) \
    X(VK_ESCAPE,0x1B) X(VK_SPACE,0x20) X(VK_PRIOR,0x21) X(VK_NEXT,0x22) X(VK_END,0x23) \
    X(VK_HOME,0x24) X(VK_LEFT,0x25) X(VK_UP,0x26) X(VK_RIGHT,0x27) X(VK_DOWN,0x28) \
    X(VK_SNAPSHOT,0x2C) X(VK_INSERT,0x2D) X(VK_DELETE,0x2E) X(VK_LWIN,0x5B) X(VK_RWIN,0x5C) \
    X(VK_APPS,0x5D) X(VK_NUMPAD0,0x60) X(VK_NUMPAD1,0x61) X(VK_NUMPAD2,0x62) X(VK_NUMPAD3,0x63) \
    X(VK_NUMPAD4,0x64) X(VK_NUMPAD5,0x65) X(VK_NUMPAD6,0x66) X(VK_NUMPAD7,0x67) X(VK_NUMPAD8,0x68) \
    X(VK_NUMPAD9,0x69) X(VK_MULTIPLY,0x6A) X(VK_ADD,0x6B) X(VK_SEPARATOR,0x6C) X(VK_SUBTRACT,0x6D) \
    X(VK_DECIMAL,0x6E) X(VK_DIVIDE,0x6F) X(VK_F1,0x70) X(VK_F2,0x71) X(VK_F3,0x72) X(VK_F4,0x73) \
    X(VK_F5,0x74) X(VK_F6,0x75) X(VK_F7,0x76) X(VK_F8,0x77) X(VK_F9,0x78) X(VK_F10,0x79) \
    X(VK_F11,0x7A) X(VK_F12,0x7B) X(VK_F13,0x7C) X(VK_F14,0x7D) X(VK_F15,0x7E) X(VK_F16,0x7F) \
    X(VK_F17,0x80) X(VK_F18,0x81) X(VK_F19,0x82) X(VK_F20,0x83) X(VK_F21,0x84) X(VK_F22,0x85) \
    X(VK_F23,0x86) X(VK_F24,0x87) X(VK_NUMLOCK,0x90) X(VK_SCROLL,0x91) X(VK_LSHIFT,0xA0) \
    X(VK_RSHIFT,0xA1) X(VK_LCONTROL,0xA2) X(VK_RCONTROL,0xA3) X(VK_LMENU,0xA4) X(VK_RMENU,0xA5) \
    X(VK_OEM_1,0xBA) X(VK_OEM_PLUS,0xBB) X(VK_OEM_COMMA,0xBC) X(VK_OEM_MINUS,0xBD) \
    X(VK_OEM_PERIOD,0xBE) X(VK_OEM_2,0xBF) X(VK_OEM_3,0xC0) X(VK_OEM_4,0xDB) X(VK_OEM_5,0xDC) \
    X(VK_OEM_6,0xDD) X(VK_OEM_7,0xDE) X(VK_OEM_102,0xE2)

#if defined(_WIN32)
#include <windows.h>
#define ENGINE_KEYCODE_CHECK(name,value) static_assert(name == value, #name " differs from <winuser.h>");
ENGINE_KEYCODES(ENGINE_KEYCODE_CHECK)
#undef ENGINE_KEYCODE_CHECK
#elif !defined(__ANDROID__)
//Enumerators rather than #defines, since a list can only generate the one: they convert to the uint32_t
//every keycode parameter takes, and work as case labels, so a call site cannot tell the difference.
#define ENGINE_KEYCODE_ENUM(name,value) name = value,
enum EngineKeyCode : uint32_t { ENGINE_KEYCODES(ENGINE_KEYCODE_ENUM) };
#undef ENGINE_KEYCODE_ENUM
#endif

#endif
