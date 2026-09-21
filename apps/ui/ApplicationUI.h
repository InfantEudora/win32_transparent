#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include <atomic>

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.

    It is also where UIOverlay gets exercised. The engine's 2D overlay is drawn by the apps that
    have a HUD to draw - tetris and bomber - and in both of them it is tangled up with gameplay
    state, which makes them a poor place to answer "what does AddRectOutline actually look like at
    thickness 6?". Here there is no gameplay to see past: the left column lays out every
    untextured primitive the overlay has, the right column builds real widgets out of them, and
    the ImGui panel drives the same parameters from the other side.

    UNTEXTURED ONLY, and that is a property of this app rather than of the demo. AddSprite and
    AddNineSliceSprite need a theme atlas, and this app deliberately owns no assets - see the
    ASSET_ROOTS note in its makefile, and the one root in main.cpp. AddNineSliceDebug stands in
    for the geometry half of nine-slicing, which is the half that has no art in it.

    --- THE WIDGETS ------------------------------------------------------------------------------
    A checkbox, a radio group and two sliders, cut from bomber's OPTIONS mockup
    (art_source/bomber_ui_example_2.png) and rebuilt out of nothing but AddRect, AddRectOutline
    and AddLine. They are cheap: 2 quads for a checkbox and 4 for a checked one, 2 and 3 for a
    radio, 4 for a slider, plus one quad per character of label. The whole right-hand column is
    well under 200 quads and still one draw call.

    THEY DRIVE THE DEMO ITSELF rather than some throwaway state - the checkboxes are the same two
    booleans the ImGui panel has, the radio group is the same alignment, the sliders are the same
    two floats. Two front ends onto one set of values is the honest way to show that the overlay
    widgets work: if they disagree with the ImGui controls beside them, it is visible immediately
    rather than being a thing to take on trust.

    --- WHAT IS BEING DEMONSTRATED ABOUT INPUT ---------------------------------------------------
    NOTHING IN THIS FILE HIT-TESTS ANYTHING. Every widget is an InputController touch rect, bound
    in Init and positioned in LayoutTouchButtons; InputController::SubmitPointer hit-tests them
    from the window thread, and the drawing below reads the rects back out of GetTouchButtons()
    so there is exactly one set of numbers. That separation is the overlay's oldest rule - see the
    header of core/UIOverlay.h - and a widget set is the case that would most tempt you to break
    it.

    The hit-test readout at the bottom of the column is the other half: the cursor position, which
    rect is under it, and which rect a pointer is holding. It is there to be watched while
    clicking around, because the two behaviours worth knowing about are invisible otherwise -
    overlapping rects resolve to the FIRST one declared, and a press CAPTURES its rect until the
    release, so a drag that wanders off a slider keeps driving that slider.
*/

/*
    This app's actions. INPUT_LAST is the last core action (see core/InputController.h) and app
    codes count up from it, the same convention apps/tetris uses.

    ONE PER WIDGET, not one per kind. Two checkboxes need two actions or a press on either would
    look the same to the tick below - AddTouchButton allocates the synthetic keycodes and keeps
    them apart, but only if the actions they map to are apart too.
*/
#define INPUT_UI_CHK_CAPTIONS   INPUT_LAST+1
#define INPUT_UI_CHK_MEASURE    INPUT_LAST+2
#define INPUT_UI_RADIO_LEFT     INPUT_LAST+3
#define INPUT_UI_RADIO_CENTER   INPUT_LAST+4
#define INPUT_UI_RADIO_RIGHT    INPUT_LAST+5
#define INPUT_UI_SLIDER_TEXT    INPUT_LAST+6
#define INPUT_UI_SLIDER_RADIUS  INPUT_LAST+7

//The widgets, in the order they are bound and laid out. Declaration order is also hit-test
//order - SubmitPointer takes the first rect that contains the point - so it is worth it being
//the order they are read in as well.
enum ui_widget_id{
    UI_WIDGET_CHK_CAPTIONS = 0,
    UI_WIDGET_CHK_MEASURE,
    UI_WIDGET_RADIO_LEFT,
    UI_WIDGET_RADIO_CENTER,
    UI_WIDGET_RADIO_RIGHT,
    UI_WIDGET_SLIDER_TEXT,
    UI_WIDGET_SLIDER_RADIUS,
    UI_WIDGET_COUNT
};

class ApplicationUI : public Application{
public:
    ApplicationUI();

    void Init(void) override;

    /*
        Positions the widget rects. RENDER THREAD, before the first frame and again on every
        resize - which is why the layout is here and not in Init, where the window size is not
        final yet. See Application::LayoutTouchButtons.
    */
    void LayoutTouchButtons(int w, int h) override;

    /*
        The widget logic. PHYSICS THREAD, once per tick that actually runs.

        Here rather than in DrawOverlay because this is where an edge is reliable: an action that
        went down and up between two ticks is still seen by WasKeyPressed on the next ticking pass
        (backlog item 88), where a frame-rate poll of f_down would drop it. A click is worth not
        dropping.
    */
    void UpdateTickInput(void) override;

    /*
        The showcase and the widgets. RENDER THREAD, called by Application::DrawFrame with
        `overlay` already Begun at the window size - so this only adds quads and returns.
    */
    void DrawOverlay(void) override;

#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif

private:
    //--- drawing helpers, all RENDER THREAD ------------------------------------------------------
    void DrawPrimitiveShowcase(float x, float y, uint32_t accent);
    void DrawWidgetColumn(float x, float y, uint32_t accent);
    void DrawHitTestReadout(float x, float y);

    //`rect` comes from the InputController button, never from a second copy of the arithmetic.
    void DrawCheckbox(const InputController::TouchButton& b, bool f_on, uint32_t accent);
    void DrawRadio(const InputController::TouchButton& b, bool f_on, uint32_t accent);
    void DrawSlider(const InputController::TouchButton& b, float t, uint32_t accent);

    //The button index AddTouchButton handed back, per widget, or -1 if binding never happened.
    //An INDEX and not a pointer, because the next AddTouchButton invalidates pointers into the
    //vector - see the note on AddTouchButton.
    int widget_button[UI_WIDGET_COUNT];

    //Turns a slider's rect and the pointer position on it into 0..1, and back again for drawing.
    //One function so the handle cannot end up somewhere the click that put it there did not mean.
    static float SliderTrackT(const InputController::TouchRect& rect, float pointer_x);
    static float SliderKnobRadius(const InputController::TouchRect& rect);

    /*
        What the panel and the widgets both set, and what DrawOverlay reads.

        ATOMIC, unlike the rest of this app, and the reason is the thread each end runs on.
        DrawOverlay and DrawImGuiUI are both the render thread, so between those two a plain
        member would do - but UpdateTickInput is the PHYSICS thread, and a slider being dragged
        writes these from there while the render thread is reading them. std::atomic is the
        cheapest thing that makes that defined; relaxed ordering would do, and the default is
        kept only because nothing here is hot enough to be worth the extra word of explanation
        at every use.

        The cost is that ImGui's controls, which want a float*, need a load into a local and a
        store back on change. That is the four-line shape repeated through DrawImGuiUI.
    */
    std::atomic<float> demo_radius{14.0f};      //corner radius of the filled row, pixels
    std::atomic<float> demo_thickness{3.0f};    //outline width of the outlined row, pixels
    std::atomic<float> demo_text_size{22.0f};   //em size for the text row, pixels
    std::atomic<float> demo_nine_inset{24.0f};  //nine-slice inset, all four sides, pixels
    std::atomic<float> demo_panel_w{320.0f};    //the nine-slice panel's size, so the cuts can be
    std::atomic<float> demo_panel_h{150.0f};    //  watched as it is resized
    std::atomic<int>   demo_align{UI_ALIGN_LEFT};
    std::atomic<bool>  f_demo_measure{true};    //draw the MeasureText box around the sample string
    std::atomic<bool>  f_demo_labels{true};     //the small captions naming each row

    //Accent colour, as the 0..1 floats ImGui's colour picker wants. Only the ImGui panel writes
    //it, so unlike the rest of this block it is single-threaded and needs nothing. Converted to
    //a UIColor at use rather than stored twice, so there is one number and no way to disagree.
    float demo_color[4] = {0.35f,0.75f,1.0f,1.0f};
};

#endif
