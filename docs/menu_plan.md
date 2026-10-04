# Menu Plan: a title menu, a Controls screen and a Settings screen

For getting archer to testers. The user asked for:

- a title screen with real buttons - Start (or Continue), Controls, Settings - on the simple
  overlay, with no custom UI elements;
- a Controls screen that shows the input map and lights up whatever is pressed, live, on the
  keyboard, the mouse and the pad;
- a Settings screen for MSAA, the sound levels and whether the game starts full screen.

The menu, the settings store and the Controls screen are core, so another game can reuse them.
Only which buttons there are, what they do and the look are archer's.

## The choices, and what was picked

1. **No widget toolkit: three small pieces on UIOverlay.** ImGui is compiled out of the ship build
   that testers get (USE_IMGUI=0), and docs/ui_overlay_plan.md keeps the overlay itself free of
   state and hit-testing on purpose. So the state lives one level up, in `core/UIMenu` (a vertical
   list of items: button, choice, slider, toggle), and the overlay only draws it.

2. **Logic and drawing are split.**
   - `UIMenu::Update` takes one pass of navigation input (`UIMenuInput`: up/down/left/right,
     confirm, back, and the pointer) and returns what happened (`UIMenuResult`). It can run on any
     thread; archer runs it on the physics thread, in UpdateTitle, where the title's input already
     is.
   - `UIMenuDraw` draws it on the render thread. A mutex inside the menu guards the two.
   - The layout is a pure function of the screen size, which both sides know, so hit-testing and
     drawing cannot disagree.
   - The logic needs no GL, so `tools/menu_test.cpp` tests it with no window.

3. **The app gathers the input from its own actions; the menu reads no keys.**
   - Archer binds four new navigation actions (MENU_UP/DOWN/LEFT/RIGHT) to the arrows, W/S/A/D,
     the D-pad and the left stick. Confirm is the existing CONTINUE action (Space, pad A, pad
     Start - not Enter, because of Alt+Enter). Back is MENU (Escape, pad Back) and a new MENU_BACK
     (pad B).
   - None of these are recorded.
   - A key may drive several actions at once (SubmitSystemKey emits one event per mapping), so
     sharing the arrows with running costs nothing: the level is not ticking while the title is
     up.
   - The mouse is the existing full-window touch button, now bound to a pointer action instead of
     CONTINUE. The position comes from GetRelativeMousePosition.
   - A real click needs focus, and hover needs HasFocus(). Everything else goes through
     IsInputLive(), so `archer_hold` still drives the menu on a minimised window.
   - `UIMenuInput` is filled in by the app, so a pad-only, touch-only or MCP-only app is just a
     different filler.

4. **Hover moves the focus, but only when the pointer MOVES.** A cursor resting over a button does
   not fight the keyboard. Focus is always drawn: a brighter panel and an outline.

5. **Controls is generic over the app's map: `core/UIControls`.**
   - The app names its rows (a label and the actions it covers: "Run" is LEFT, RIGHT and the
     stick's MOVE).
   - The bindings themselves come from `InputController::keymap` - the actual map, never a typed
     list - sorted into keyboard, mouse and gamepad columns.
   - Each binding lights while held: `KeyMap::f_held` is per physical key, so it is the key that
     was pressed that lights, not only its action.
   - Analog bindings show their deflection as a bar (`GetAxis`).
   - A footer shows the raw pad: whether one is connected, all of its buttons (a new
     `GetGamepadButtons()` getter) and its six axes. A button nothing is bound to still answers
     "is my pad working".
   - Rebinding is not built. A row keeps the keymap indices it shows, so a later rebind is "pick a
     cell, wait for a key, write keymap[i].system_keycode, save it" - nothing here needs to change
     shape for that.

6. **Settings is a typed store: `core/Settings`.**
   - Keys are declared with a default and a range: bool, int, float.
   - It loads and saves a flat JSON object. A missing file means the defaults; a key of the wrong
     type, or out of range, is replaced by its default or clamped and named in the load's report.
   - Keys it does not know are kept on save, so an older build cannot drop a newer one's settings.
   - It saves through a temporary file and a rename, so a crash mid-write leaves the old file.
   - The file is `settings.json` BESIDE THE EXE (`GetExecutableDirectory`), read with
     ReadFileToString and an absolute path, so it is never a baked asset.
   - Note: imgui.ini and recordings are actually written to the working directory, not beside the
     exe. Settings goes beside the exe as asked, which is also where a tester's copy has to keep
     it.

7. **What each setting does, and when.**
   - **MSAA:** Off / 4x / 16x, applied live with `Renderer::RequestAASamples` (1 = off). 4x is the
     default.
   - **Master** and **music:** the existing `master` and `music` buses, through archer's
     `sound_volume` and `music_volume`, which UpdateView already applies every pass.
   - **Effects:** everything that is not music. The `<scene>/effects` buses are set by CueSystem
     itself, so they cannot be steered from outside. Effects therefore scales each scene's LAYER
     bus (`title`, `world`, each extra level), which carries the effects, voice, ambience and body
     buses beneath it and nothing else sets.
   - **Start full screen:** read in the app's constructor, before the window exists. Core applies
     it once the app's Init has returned (`Application::f_start_fullscreen`), so an app's own
     Resize in Init cannot undo it. `--minimized` wins over it.
   - **Saving:** each change is saved as it is made - the file is a few hundred bytes - so closing
     the window any way loses nothing.

8. **Determinism.**
   - The title is outside every level's ticks, and nothing here writes to Stage.
   - The settings are view-side (renderer, buses).
   - None of the new actions are recorded, so archer_test replays exactly as before.

## Archer's part

- **The title menu.**
  - It has four buttons: Start (Continue once a level has been started - the first press sets
    `f_game_started`), Controls, Settings and Quit.
  - Start/Continue is today's dismissal, unchanged: PlayStartHorn on the press tick, then the
    fade.
  - Escape on the title still quits, as before; Back (or Escape) on a sub-screen returns to the
    title menu, focused on what led there.
- **The look:** archer's parchment-on-dark band. The title menu draws over the title art, low
  centre, where "CLICK TO CONTINUE" was. Controls and Settings have their own picture,
  `images/menu_background.jpg` (MENU_ASSET), a second quad in the title scene that FitTitleQuad
  shows in place of the title art while either screen is up.
- **MCP:** `archer_menu` reads the screen, the focus and the items, and can move the pointer and
  click, which is how the mouse path is checked without taking the desk's real mouse.
  `archer_hold` takes the navigation actions by name.

## As built (2026-10-01)

### Core

| file | what |
|---|---|
| `core/Settings.h/.cpp` | `DeclareBool/Int/Float(key, default, lo, hi)`, `Load(path, &report)`, `Save()`, `Get*`/`Set*` (clamped; true when changed), `Revision()`, `ToText()` |
| `core/UIMenu.h/.cpp` | `UIMenu`: `AddButton/AddChoice/AddSlider/AddToggle(label, ..., id)`, `Update(UIMenuInput) -> UIMenuResult{event, item, id}`, `ItemRect/ValueRect/TrackRect` (the layout, pure), `SetFocus/SetLabel/SetEnabled/SetChoice/SetValue/SetOn`, `Item()/Items()` copies |
| `core/UIMenuDraw.cpp` | `UIMenuDraw(overlay, menu, w, h, UIMenuStyle)` - the only half with GL |
| `core/UIControls.h/.cpp` | `AddRow(label, {actions})`, `Draw(overlay, input, rect, UIControlsStyle)`; `Bindings`, `DeviceOf`, `KeyName` for a rebinding screen later |
| `core/Window` | `SetFullscreen(bool)`: Alt+Enter's code, now callable |
| `core/Application` | `f_start_fullscreen`, applied after the app's Init unless `--minimized` |
| `core/InputController.h` | `GetGamepadButtons()` |
| `tools/menu_test.cpp` | 40 checks of Settings and the menu's logic; part of archer's `make rules` |

### The settings file

`settings.json` beside the exe:

```json
{
  "fullscreen": false,
  "msaa": 4,
  "volume_master": 0.8,
  "volume_music": 0.7,
  "volume_effects": 1
}
```

- `msaa` is the sample count: 1 is off, and 4 or 16 are what the menu offers. Any value from 1
  to 16 is accepted from the file.
- The volumes are 0..1.

### Checked in game (debug, port 8768, minimised and driven over MCP)

- **Title:** four buttons where "click to continue" was, Start focused.
  - Keys: menu_down then continue opened Controls.
  - Mouse (`archer_menu`): a hover focused Quit, a click opened Settings, and a click on Back
    returned.
  - Pad B returned from Settings to the title, focused on Settings.
- **Start:** the horn plays on the press tick ("Start horn, on 'Title Screen' tick ..."), then the
  fade, then the level. The button then reads Continue. Escape comes back to the title focused on
  Continue, and a second Continue does not replay the horn.
- **Controls:**
  - The table comes from the keymap.
  - Scripted holds lit the binding they drive (a scripted hold lights the action's FIRST binding
    in keymap order - A for run, pad A for jump, LB for draw).
  - A scripted stick at 0.6 lit L stick X with its bar.
  - The pad strip shows a connected pad.
- **Settings:**
  - MSAA Off went into the renderer (`aa_samples` 1), and 16x the same (16). A crop of the world
    shows the bow and grass stair-stepped at Off and smooth at 16x.
  - Master 0.3 (set by a mouse click on the track), music 0.5 and effects 0.5 (by keys) reached
    their buses, the effects on both scene layer buses.
  - Full screen on was saved.
- **After a restart:** every value came back from the file; full screen was not applied, because
  `--minimized` wins.
- **Full screen at start** (with the desk locked, so it covered nobody's work): started without
  `--minimized`, the window came up 2560x1440, the whole monitor.
- **The ship build** (`make ship`, copied alone into an empty folder):
  - It starts with the defaults and draws the title menu (captured with PrintWindow), with its
    art and tables from the baked blob.
  - It binds no port.
  - A `settings.json` placed beside it is loaded.
  - Its menus were not driven: with no MCP and no focus there is nothing to drive them with.
- `cue_replay`: `state same`, `same` - also on the build merged with the apples work.

## The wardrobe (2026-10-01)

The Character scene for testers - before this it could only be reached from the debug Scene
panel.

- **Getting there:** the title menu's second button, Character, fades to the Character scene with
  the WARDROBE open.
  - No horn: that is the game starting, and this is not.
  - Escape, Back, B or the wardrobe's own Back return to the title, focused on Character.
  - Start/Continue then goes to the last level that is the GAME (`play_scene`), never back to the
    Character scene: each button names its destination (`title_destination`).
- **What it dresses:** Legs (Bare / Leggings - one or the other, `archer_legs` / `archer_leggings`),
  Armband, Cape and Pouch (`archer_sachet`), on and off.
- **Persistence:** it is `outfit`, a second core/Settings store, in `outfit.json` beside the exe:
  `{"leggings": true, "armband": true, "cape": true, "pouch": true}`.
  - It is a store of its own because it is the player's character, not the machine's setup, and
    gear will grow it.
  - ApplyOutfit puts it on her parts on a change. She is one model shared by every scene, so it
    holds in all of them.
- **Input:** while the wardrobe is open it has the input. GatherInput holds her still, so Space and
  the arrows do not also make her jump and aim.
- **Debug paths unchanged:**
  - Reached from the Scene panel, the Character scene is the animation bench it always was, with
    no wardrobe.
  - The Character panel's part checkboxes still override until the next outfit change.
- **Placement:** left of her - the Character camera frames her right of centre. With the debug
  panels up it moves in, between them and her.
- **Checked:**
  - Title -> Character (by click) opened it.
  - Keys and clicks changed the parts and wrote outfit.json; Space did not make her jump.
  - B returned to the title on Character, and Start then went to the world with the outfit on.
  - A restart kept it.
  - Two archer_test replays with different outfits gave identical reports.

## Not done

- Rebinding (see 5).
- A pause menu inside a level. Escape still goes back to the title, which already is one.
- Resolution or window-size settings.
