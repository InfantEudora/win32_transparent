#include "ApplicationUI.h"
#ifdef USE_IMGUI
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

//snprintf, for the hit-test readout. The overlay draws a const char*, so the numbers have to be
//turned into one somewhere and this is the only place in the app that formats anything.
#include <cstdio>

#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationUI", DEBUG_ALL);

/*
    The two ranges the sliders cover, named once.

    BOTH FRONT ENDS READ THESE. The overlay slider maps its 0..1 through them and the ImGui
    slider passes them straight to SliderFloat, so the two controls cannot end up meaning
    different things - which is the failure a second hard-coded 8.0f somewhere would produce, and
    it would look like the widget being wrong rather than like a typo.
*/
static const float UI_TEXT_SIZE_MIN = 8.0f;
static const float UI_TEXT_SIZE_MAX = 64.0f;
static const float UI_RADIUS_MIN    = 0.0f;
static const float UI_RADIUS_MAX    = 40.0f;

ApplicationUI::ApplicationUI():Application(){
    for (int i = 0;i < UI_WIDGET_COUNT;i++){
        widget_button[i] = -1;
    }
    debug->Info("Created new application.\n");
};

void ApplicationUI::Init(void){

    int2 dimension = GetDisplaySettings();
    debug->Info("Display Dimension: %i x %i\n",dimension.x,dimension.y);

    //Renderer needs to be set up.
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1400,900);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics(GetPhysicsTimestep());

    /*
        The widgets, bound as InputController rects.

        THE RECT IS ZERO HERE ON PURPOSE. AddTouchButton allocates the synthetic keycode and the
        KeyMap once and for good; the geometry belongs in LayoutTouchButtons, which runs again on
        every resize while this never does. The split is the whole reason a window resize is safe
        - see the note on SetTouchButtonRect.

        Declaration order is hit-test order: SubmitPointer takes the FIRST rect that contains the
        point. Nothing here overlaps, but that is a property of the layout below rather than
        something the input system enforces, and the readout exists partly to make it checkable.

        The labels are InputController's, not this file's: a legend short enough for the twelve
        bytes TouchButton carries, so the hit-test readout can name a rect from the input side
        rather than from a parallel table here that could drift out of step.
    */
    InputController* input = main_scene->inputcontroller;
    InputController::TouchRect nowhere;

    widget_button[UI_WIDGET_CHK_CAPTIONS]  = input->AddTouchButton(nowhere,INPUT_UI_CHK_CAPTIONS,"captions");
    widget_button[UI_WIDGET_CHK_MEASURE]   = input->AddTouchButton(nowhere,INPUT_UI_CHK_MEASURE,"measure");
    widget_button[UI_WIDGET_RADIO_LEFT]    = input->AddTouchButton(nowhere,INPUT_UI_RADIO_LEFT,"align L");
    widget_button[UI_WIDGET_RADIO_CENTER]  = input->AddTouchButton(nowhere,INPUT_UI_RADIO_CENTER,"align C");
    widget_button[UI_WIDGET_RADIO_RIGHT]   = input->AddTouchButton(nowhere,INPUT_UI_RADIO_RIGHT,"align R");
    widget_button[UI_WIDGET_SLIDER_TEXT]   = input->AddTouchButton(nowhere,INPUT_UI_SLIDER_TEXT,"textsize");
    widget_button[UI_WIDGET_SLIDER_RADIUS] = input->AddTouchButton(nowhere,INPUT_UI_SLIDER_RADIUS,"radius");

    /*
        Core draws every touch rect as a generic rounded button with its label on it - see
        Application::DrawTouchButtons - which is the right default for an app that has bound a
        d-pad and wants something on screen without writing a renderer for it. This app draws its
        own artwork for the same rects, so the generic pass would paint a second button on top of
        every widget. Turning it off is how an app says "these are mine".
    */
    f_draw_touch_buttons = false;
}

//----------------------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------------------

void ApplicationUI::LayoutTouchButtons(int w, int h){
    (void)h;
    if (!main_window || !main_window->inputcontroller){
        return;
    }
    InputController* input = main_window->inputcontroller;

    /*
        The widget column sits to the right of the primitive showcase. Its x is fixed because the
        column to its left is - the showcase is a fixed-size catalogue, not a layout that reflows -
        and only the WIDTH follows the window, clamped so a narrow window squeezes the sliders
        rather than running them off the edge or under the ImGui panel.

        A demo is allowed a layout this simple. An app with a real menu wants its geometry in
        fractions of the surface; bomber's BOMBER_MENU_PANEL_* are what that looks like.
    */
    const float col_x = 640.0f;
    float col_w = (float)w - col_x - 380.0f;
    if (col_w > 360.0f){
        col_w = 360.0f;
    }
    if (col_w < 170.0f){
        col_w = 170.0f;
    }

    const float box        = 26.0f;   //checkbox side
    const float radio      = 24.0f;   //radio diameter
    const float track_h    = 30.0f;   //slider HIT height - taller than the track it draws, so the
                                      //  thing you have to hit is not the 8-pixel bar
    const float row_pitch  = 40.0f;
    const float group_gap  = 34.0f;   //before a heading

    float y = 78.0f;

    InputController::TouchRect r;

    r.x = col_x; r.y = y; r.w = box; r.h = box;
    input->SetTouchButtonRect(widget_button[UI_WIDGET_CHK_CAPTIONS],r);
    y += row_pitch;

    r.y = y;
    input->SetTouchButtonRect(widget_button[UI_WIDGET_CHK_MEASURE],r);
    y += row_pitch + group_gap;

    //Three radios across the column, each in its own third, so the label after one cannot reach
    //the next one's rect.
    for (int i = 0;i < 3;i++){
        r.x = col_x + (col_w / 3.0f) * (float)i;
        r.y = y;
        r.w = radio;
        r.h = radio;
        input->SetTouchButtonRect(widget_button[UI_WIDGET_RADIO_LEFT + i],r);
    }
    y += radio + group_gap;

    r.x = col_x; r.w = col_w; r.h = track_h;
    r.y = y;
    input->SetTouchButtonRect(widget_button[UI_WIDGET_SLIDER_TEXT],r);
    y += track_h + group_gap;

    r.y = y;
    input->SetTouchButtonRect(widget_button[UI_WIDGET_SLIDER_RADIUS],r);
}

float ApplicationUI::SliderKnobRadius(const InputController::TouchRect& rect){
    return rect.h * 0.45f;
}

float ApplicationUI::SliderTrackT(const InputController::TouchRect& rect, float pointer_x){
    /*
        The knob's CENTRE travels between rect.x + r and rect.x + w - r, not between the rect's
        own edges - otherwise half the knob hangs off each end at the extremes. So the usable
        span is the width less one whole knob, and both this and the drawing below go through
        here rather than each doing the inset themselves.
    */
    float r    = SliderKnobRadius(rect);
    float span = rect.w - r * 2.0f;
    if (span <= 0.0f){
        return 0.0f;
    }
    float t = (pointer_x - (rect.x + r)) / span;
    if (t < 0.0f){
        t = 0.0f;
    }
    if (t > 1.0f){
        t = 1.0f;
    }
    return t;
}

//----------------------------------------------------------------------------------------------
// The widget logic - PHYSICS THREAD
//----------------------------------------------------------------------------------------------

void ApplicationUI::UpdateTickInput(void){
    //The base walks the scene's own input. Nothing in this app's scene reads it today, but
    //skipping the base call is how an override quietly disables something later.
    Application::UpdateTickInput();

    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //--- the edge-shaped widgets ----------------------------------------------------------------
    //WasKeyPressed, not IsKeyDown: a checkbox responds to the press, once, and holding the mouse
    //down on it must not toggle it sixty times a second.
    if (input->WasKeyPressed(INPUT_UI_CHK_CAPTIONS)){
        f_demo_labels.store(!f_demo_labels.load());
    }
    if (input->WasKeyPressed(INPUT_UI_CHK_MEASURE)){
        f_demo_measure.store(!f_demo_measure.load());
    }
    //A radio group is three buttons and one value; "only one at a time" is a property of the
    //value, not something the group has to police.
    if (input->WasKeyPressed(INPUT_UI_RADIO_LEFT)){
        demo_align.store(UI_ALIGN_LEFT);
    }
    if (input->WasKeyPressed(INPUT_UI_RADIO_CENTER)){
        demo_align.store(UI_ALIGN_CENTER);
    }
    if (input->WasKeyPressed(INPUT_UI_RADIO_RIGHT)){
        demo_align.store(UI_ALIGN_RIGHT);
    }

    //--- the sliders ----------------------------------------------------------------------------
    /*
        A LEVEL, NOT AN EDGE, and this is the interesting half of the widget set.

        INPUT_UI_SLIDER_TEXT and INPUT_UI_SLIDER_RADIUS are allocated like any other touch rect
        and then deliberately never read. The keycode carries "pressed" and "released", which is
        the right shape for a button and the wrong one for a control whose entire value is WHERE
        along itself you are holding it. That position rides on the TouchButton instead - see
        TouchButton::pointer_x - so what a slider wants is the button's own f_down and pointer_x,
        read as a pair.

        AS A PAIR is the reason IsKeyDown is not used here even though it would work for the
        "held" half. IsKeyDown is latched at the tick and pointer_x is written the instant the
        mouse moves, so mixing them can pair a held state with a position from after the release.
        Both fields come off the same struct, written by the same thread in the same call.

        Reading them without a lock is the same benign race Application::DrawTouchButtons already
        documents for f_down: worst case a slider lands one mouse-move behind, for one tick.
    */
    const std::vector<InputController::TouchButton>& buttons = input->GetTouchButtons();

    int idx = widget_button[UI_WIDGET_SLIDER_TEXT];
    if ((idx >= 0) && (idx < (int)buttons.size()) && buttons[idx].f_down){
        float t = SliderTrackT(buttons[idx].rect,buttons[idx].pointer_x);
        demo_text_size.store(UI_TEXT_SIZE_MIN + t * (UI_TEXT_SIZE_MAX - UI_TEXT_SIZE_MIN));
    }

    idx = widget_button[UI_WIDGET_SLIDER_RADIUS];
    if ((idx >= 0) && (idx < (int)buttons.size()) && buttons[idx].f_down){
        float t = SliderTrackT(buttons[idx].rect,buttons[idx].pointer_x);
        demo_radius.store(UI_RADIUS_MIN + t * (UI_RADIUS_MAX - UI_RADIUS_MIN));
    }
}

//----------------------------------------------------------------------------------------------
// The overlay showcase
//----------------------------------------------------------------------------------------------

//The palette. Named rather than written at each call site, because half of what this file is for
//is being read - a literal 0xFF203040 in the middle of a layout says nothing about what it is.
static const uint32_t UI_DEMO_PANEL   = UIColor( 16, 20, 28,200);
static const uint32_t UI_DEMO_LABEL   = UIColor(170,185,210,230);
static const uint32_t UI_DEMO_TEXT    = UIColor(240,245,255,255);
static const uint32_t UI_DEMO_GUIDE   = UIColor(255,110,110,170);
static const uint32_t UI_DEMO_MEASURE = UIColor(120,255,170,200);
//The widget palette, read off bomber's OPTIONS mockup: a near-black well, a pale rim, and the
//accent doing the work of saying "on".
static const uint32_t UI_DEMO_WELL    = UIColor( 26, 30, 38,255);
static const uint32_t UI_DEMO_RIM     = UIColor(150,165,190,190);
static const uint32_t UI_DEMO_RIM_HOT = UIColor(225,238,255,255);
static const uint32_t UI_DEMO_TICK    = UIColor(255,255,255,255);
static const uint32_t UI_DEMO_KNOB    = UIColor(196,166,110,255);

/*
    One captioned row of the showcase.

    `y` is the row's TOP and the return is the next row's top, so the layout below is a straight
    run of assignments and no row knows where any other one is. Adding or removing a row is then
    one line, which matters for a file whose whole job is to be edited while looking at the result.
*/
static float DemoRowTop(UIOverlay* overlay, const char* caption, float x, float y,
                        float caption_size, bool f_labels){
    if (f_labels && caption){
        //y is the row top and AddText wants a BASELINE, so the caption is dropped by its own size
        //to sit inside the row rather than above it.
        overlay->AddText(caption,vec2(x,y + caption_size),caption_size,UI_DEMO_LABEL);
        return y + caption_size * 1.6f;
    }
    return y;
}

void ApplicationUI::DrawOverlay(void){
    /*
        The overlay is never NULL once the frame thread is running, but it is INERT rather than
        absent when the font or the shader failed to load - see UIOverlay::Init. Both are checked
        because IsReady() false means every Add* below is a no-op, and drawing a screen of
        nothing is a worse way to find out than not drawing at all.
    */
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }

    const uint32_t accent = UIColor((uint8_t)(demo_color[0] * 255.0f),
                                    (uint8_t)(demo_color[1] * 255.0f),
                                    (uint8_t)(demo_color[2] * 255.0f),
                                    (uint8_t)(demo_color[3] * 255.0f));
    const float margin = 28.0f;

    //--- the backdrop ---------------------------------------------------------------------------
    //Not decoration: the scene behind this is an empty deferred pass, and a dark panel under the
    //whole showcase is what makes a light swatch and a dark one both readable against it.
    overlay->AddRect(vec2(margin - 12.0f,margin - 12.0f),
                     vec2((float)main_window->width - margin + 12.0f,
                          (float)main_window->height - margin + 12.0f),
                     10.0f,UI_DEMO_PANEL);

    DrawPrimitiveShowcase(margin,margin,accent);
    //x matches LayoutTouchButtons' col_x. The WIDGETS do not read it - they take their geometry
    //from the rects the hit test uses - but the headings between them are text, which has no
    //rect of its own.
    DrawWidgetColumn(640.0f,margin,accent);
}

void ApplicationUI::DrawPrimitiveShowcase(float x, float y, uint32_t accent){
    /*
        Loaded ONCE for the whole column rather than at each use. The physics thread can store a
        new value between two of these rows while a slider is being dragged, and a frame where
        the text row and the MeasureText row below it disagreed about the size would make the
        measure box look wrong for exactly one frame - which is the kind of flicker that gets
        chased as a bug in MeasureText.
    */
    const float radius     = demo_radius.load();
    const float thickness  = demo_thickness.load();
    const float text_size  = demo_text_size.load();
    const float nine_inset = demo_nine_inset.load();
    const float panel_w    = demo_panel_w.load();
    const float panel_h    = demo_panel_h.load();
    const int   align      = demo_align.load();
    const bool  f_measure  = f_demo_measure.load();
    const bool  f_labels   = f_demo_labels.load();

    const float caption_size = 15.0f;
    const float swatch_w     = 96.0f;
    const float swatch_h     = 56.0f;
    const float gap          = 16.0f;

    //--- AddRect, at four radii -----------------------------------------------------------------
    /*
        The last one asks for a radius far larger than the box, and that is the case worth seeing:
        AddRect clamps to half the shorter side, so it gives a capsule rather than turning itself
        inside out. A demo that only showed sensible values would not say that.
    */
    y = DemoRowTop(overlay,"AddRect - radius 0, 6, slider, 999 (clamped to a capsule)",
                   x,y,caption_size,f_labels);
    {
        const float radii[4] = {0.0f,6.0f,radius,999.0f};
        for (int i = 0;i < 4;i++){
            vec2 min = vec2(x + (swatch_w + gap) * i,y);
            overlay->AddRect(min,min + vec2(swatch_w,swatch_h),radii[i],accent);
        }
        y += swatch_h + gap * 1.6f;
    }

    //--- AddRectOutline, at four thicknesses ----------------------------------------------------
    //Thickness is centred on the edge, so a thick outline grows both ways and the filled swatch
    //above is the reference for where that edge actually is.
    y = DemoRowTop(overlay,"AddRectOutline - thickness 1, 2, slider, 8",
                   x,y,caption_size,f_labels);
    {
        const float thicknesses[4] = {1.0f,2.0f,thickness,8.0f};
        for (int i = 0;i < 4;i++){
            vec2 min = vec2(x + (swatch_w + gap) * i,y);
            overlay->AddRectOutline(min,min + vec2(swatch_w,swatch_h),radius,
                                    thicknesses[i],accent);
        }
        y += swatch_h + gap * 1.6f;
    }

    //--- AddLine --------------------------------------------------------------------------------
    /*
        The only primitive here that is not axis-aligned, and one quad each - see the header of
        UIOverlay::AddLine for why that costs no shader work. A fan is the honest way to show it:
        if the rotation were being faked by something axis-aligned, the diagonals would be the
        ones that gave it away.
    */
    y = DemoRowTop(overlay,"AddLine - one quad per stroke, round caps on the endpoints",
                   x,y,caption_size,f_labels);
    {
        const float fan_r = 46.0f;
        vec2 hub = vec2(x + fan_r,y + fan_r);
        //Eight spokes without a sin/cos table: the eight unit-ish directions written out. Exact
        //angles would say nothing more than "these are not all axis-aligned", which is the point.
        const vec2 dir[8] = {
            vec2( 1.00f, 0.00f), vec2( 0.92f, 0.38f), vec2( 0.71f, 0.71f), vec2( 0.38f, 0.92f),
            vec2( 0.00f, 1.00f), vec2(-0.38f, 0.92f), vec2(-0.71f, 0.71f), vec2(-0.92f, 0.38f)
        };
        for (int i = 0;i < 8;i++){
            overlay->AddLine(hub,vec2(hub.x + dir[i].x * fan_r,hub.y + dir[i].y * fan_r),
                             thickness,accent);
        }
        //A degenerate stroke beside it, which is a dot rather than nothing - the case a caller
        //animating two endpoints together would otherwise have to guard.
        vec2 dot = vec2(x + fan_r * 3.0f,hub.y);
        overlay->AddLine(dot,dot,thickness * 3.0f,accent);
        overlay->AddText("zero length",vec2(dot.x,hub.y + fan_r * 0.7f),
                         caption_size * 0.85f,UI_DEMO_LABEL,UI_ALIGN_CENTER);

        y += fan_r * 2.0f + gap * 1.6f;
    }

    //--- AddText, and what alignment does to it -------------------------------------------------
    /*
        THE GUIDE LINE IS THE POINT. Alignment moves the line relative to `pos`, which is invisible
        unless something marks where pos was - so all three strings are drawn at the same x, with
        a hairline down it. Left starts at the line, centre straddles it, right ends on it.
    */
    y = DemoRowTop(overlay,"AddText - red line is the position; align moves the text around it",
                   x,y,caption_size,f_labels);
    {
        const float guide_x   = x + 280.0f;
        const float line_step = text_size * 1.5f;
        const float block_h   = line_step * 3.0f;

        //Baselines, so the first line is stepped down by one before anything is drawn.
        overlay->AddText("UI_ALIGN_LEFT",  vec2(guide_x,y + line_step * 1.0f),
                         text_size,UI_DEMO_TEXT,UI_ALIGN_LEFT);
        overlay->AddText("UI_ALIGN_CENTER",vec2(guide_x,y + line_step * 2.0f),
                         text_size,UI_DEMO_TEXT,UI_ALIGN_CENTER);
        overlay->AddText("UI_ALIGN_RIGHT", vec2(guide_x,y + line_step * 3.0f),
                         text_size,UI_DEMO_TEXT,UI_ALIGN_RIGHT);

        //AFTER the text, not before. The batch has no depth and draws in submission order, so a
        //hairline added first is simply painted over by the centred string that crosses it - and
        //a guide you cannot see where it matters most is worse than none.
        overlay->AddLine(vec2(guide_x,y),vec2(guide_x,y + block_h),2.0f,UI_DEMO_GUIDE);

        y += block_h + gap * 1.6f;
    }

    //--- MeasureText, drawn against the text it measured ----------------------------------------
    /*
        The only honest way to demo MeasureText is to draw its result AROUND the real string: if
        the two ever disagree the box stops fitting, which is a bug you can see from across the
        room. It is also the check to repeat after any change to the font metrics.

        THE TWO BITS OF ARITHMETIC HERE ARE THE WHOLE LESSON, because they are exactly what a
        caller wanting a background behind a label has to do and neither is obvious:

          - AddText takes a BASELINE, and MeasureText returns a LINE BOX. `origin_y` is the pen's
            offset down from the top of a glyph cell, so scaling it by size/em is what puts the
            baseline inside the box. Skipping it - putting the baseline at the box's bottom edge -
            leaves every descender hanging outside, which is the bug this row exists to not have.
          - the left edge follows the SAME alignment the text is drawn with, because `pos` is a
            position the line is placed around rather than its left end.
    */
    y = DemoRowTop(overlay,"MeasureText - green box is the measured line box of the string",
                   x,y,caption_size,f_labels);
    {
        const char* sample = "Sphinx of black quartz, judge my vow";
        vec2 size = overlay->MeasureText(sample,text_size);

        const ui_font_header& font = overlay->GetFontHeader();
        float scale = (font.em_px > 0.0f) ? (text_size / font.em_px) : 1.0f;
        vec2 pen = vec2(x,y + font.origin_y * scale);

        float left = pen.x;
        if (align == UI_ALIGN_CENTER){
            left = pen.x - size.x * 0.5f;
        }else if (align == UI_ALIGN_RIGHT){
            left = pen.x - size.x;
        }

        if (f_measure){
            overlay->AddRectOutline(vec2(left,y),vec2(left + size.x,y + size.y),
                                    0.0f,1.0f,UI_DEMO_MEASURE);
        }
        overlay->AddText(sample,pen,text_size,UI_DEMO_TEXT,align);

        y += size.y + gap * 1.6f;
    }

    //--- AddNineSliceDebug ----------------------------------------------------------------------
    /*
        The geometry half of nine-slicing, with no art in it - see the note on AddNineSliceDebug.
        The panel is resizable from the ImGui sliders because that is the only thing worth
        watching here: the four corners must stay the inset's size whatever the panel does, and
        the colours name the ROLE so it is obvious at a glance when one of them does not.
    */
    y = DemoRowTop(overlay,"AddNineSliceDebug - resize it; the corners must not scale",
                   x,y,caption_size,f_labels);
    {
        ui_nine_inset inset;
        inset.left = inset.top = inset.right = inset.bottom = nine_inset;
        overlay->AddNineSliceDebug(vec2(x,y),vec2(x + panel_w,y + panel_h),inset);
    }
}

//----------------------------------------------------------------------------------------------
// The widgets
//----------------------------------------------------------------------------------------------

void ApplicationUI::DrawCheckbox(const InputController::TouchButton& b, bool f_on, uint32_t accent){
    vec2 min = vec2(b.rect.x,b.rect.y);
    vec2 max = vec2(b.rect.x + b.rect.w,b.rect.y + b.rect.h);
    float s  = (b.rect.w < b.rect.h) ? b.rect.w : b.rect.h;

    //Two quads for the box, and the state is a fill colour rather than a second sprite - which is
    //what the themed path would also do, so the shape of the code survives the art arriving.
    overlay->AddRect(min,max,s * 0.22f,f_on ? accent : UI_DEMO_WELL);
    overlay->AddRectOutline(min,max,s * 0.22f,2.0f,b.f_down ? UI_DEMO_RIM_HOT : UI_DEMO_RIM);

    if (f_on){
        /*
            The tick: two strokes, two quads, and the only thing in this app that could not be
            drawn before AddLine existed. The font is printable ASCII, so there is no glyph for
            it - the alternative was an 'x', which is a different mark meaning a different thing.

            Fractions of the box rather than pixels, so the same tick is right at any size.
        */
        float t = s * 0.13f;
        vec2 p0 = vec2(min.x + s * 0.24f,min.y + s * 0.52f);
        vec2 p1 = vec2(min.x + s * 0.43f,min.y + s * 0.71f);
        vec2 p2 = vec2(min.x + s * 0.77f,min.y + s * 0.30f);
        overlay->AddLine(p0,p1,t,UI_DEMO_TICK);
        overlay->AddLine(p1,p2,t,UI_DEMO_TICK);
    }
}

void ApplicationUI::DrawRadio(const InputController::TouchButton& b, bool f_on, uint32_t accent){
    vec2 min = vec2(b.rect.x,b.rect.y);
    vec2 max = vec2(b.rect.x + b.rect.w,b.rect.y + b.rect.h);

    //A circle is an AddRect whose radius exceeds half its shorter side - the clamp in AddQuad
    //does the rest. There is no circle primitive and there does not need to be one.
    overlay->AddRect(min,max,999.0f,UI_DEMO_WELL);
    overlay->AddRectOutline(min,max,999.0f,2.0f,b.f_down ? UI_DEMO_RIM_HOT : UI_DEMO_RIM);

    if (f_on){
        float inset = b.rect.w * 0.28f;
        overlay->AddRect(vec2(min.x + inset,min.y + inset),
                         vec2(max.x - inset,max.y - inset),999.0f,accent);
    }
}

void ApplicationUI::DrawSlider(const InputController::TouchButton& b, float t, uint32_t accent){
    /*
        Four quads: the dark track, the accent fill up to the handle, and the handle's disc and
        ring. The hit RECT is taller than the track it draws - see LayoutTouchButtons - because
        the thing you have to hit should not be the eight-pixel bar.
    */
    float cy      = b.rect.y + b.rect.h * 0.5f;
    float track_h = b.rect.h * 0.27f;
    float knob_r  = SliderKnobRadius(b.rect);
    float knob_cx = b.rect.x + knob_r + t * (b.rect.w - knob_r * 2.0f);

    overlay->AddRect(vec2(b.rect.x,cy - track_h * 0.5f),
                     vec2(b.rect.x + b.rect.w,cy + track_h * 0.5f),
                     track_h * 0.5f,UI_DEMO_WELL);
    //Stopping at the knob's centre rather than its leading edge: the disc covers the join, and
    //the fill reads as running under the handle the way the mockup's does.
    overlay->AddRect(vec2(b.rect.x,cy - track_h * 0.5f),
                     vec2(knob_cx,cy + track_h * 0.5f),
                     track_h * 0.5f,accent);

    overlay->AddRect(vec2(knob_cx - knob_r,cy - knob_r),
                     vec2(knob_cx + knob_r,cy + knob_r),999.0f,UI_DEMO_KNOB);
    overlay->AddRectOutline(vec2(knob_cx - knob_r,cy - knob_r),
                            vec2(knob_cx + knob_r,cy + knob_r),999.0f,2.0f,
                            b.f_down ? UI_DEMO_RIM_HOT : UI_DEMO_RIM);
}

void ApplicationUI::DrawWidgetColumn(float x, float y, uint32_t accent){
    if (!main_window || !main_window->inputcontroller){
        return;
    }
    const std::vector<InputController::TouchButton>& buttons =
        main_window->inputcontroller->GetTouchButtons();

    const float caption_size = 15.0f;
    const float label_size   = 17.0f;

    overlay->AddText("Widgets - drawn from the rects the hit test uses",
                     vec2(x,y + caption_size),caption_size,UI_DEMO_LABEL);

    /*
        EVERY POSITION BELOW COMES OUT OF THE RECT, never out of the layout constants.

        The heading above a group is placed relative to that group's first rect, and a label is
        placed relative to the rect it belongs to. So there is one set of numbers -
        LayoutTouchButtons' - and moving a widget moves its label and its heading with it. The
        alternative, a second copy of the layout arithmetic here, is how a control ends up drawn
        a few pixels off the rectangle that actually responds, which is the single most common way
        a menu feels broken. bomber's DrawMenu carries the same note for the same reason.
    */
    const char* chk_label[2] = {"row captions","MeasureText box"};
    const bool  chk_state[2] = {f_demo_labels.load(),f_demo_measure.load()};
    for (int i = 0;i < 2;i++){
        int idx = widget_button[UI_WIDGET_CHK_CAPTIONS + i];
        if ((idx < 0) || (idx >= (int)buttons.size())){
            continue;
        }
        const InputController::TouchButton& b = buttons[idx];
        DrawCheckbox(b,chk_state[i],accent);
        //Baseline nudged below the rect's centre by about a third of the text height, so the
        //label sits optically centred on the box rather than hanging above it.
        overlay->AddText(chk_label[i],
                         vec2(b.rect.x + b.rect.w + 12.0f,
                              b.rect.y + b.rect.h * 0.5f + label_size * 0.34f),
                         label_size,UI_DEMO_TEXT);
    }

    const char* radio_label[3] = {"left","center","right"};
    const int   align_now      = demo_align.load();
    for (int i = 0;i < 3;i++){
        int idx = widget_button[UI_WIDGET_RADIO_LEFT + i];
        if ((idx < 0) || (idx >= (int)buttons.size())){
            continue;
        }
        const InputController::TouchButton& b = buttons[idx];
        //UI_ALIGN_LEFT/CENTER/RIGHT are 0/1/2 and the three widgets are bound in that order, so
        //the group index IS the value. Worth saying out loud, because it is the kind of
        //coincidence that stops being one the day a fourth alignment is added.
        DrawRadio(b,align_now == i,accent);
        overlay->AddText(radio_label[i],
                         vec2(b.rect.x + b.rect.w + 8.0f,
                              b.rect.y + b.rect.h * 0.5f + label_size * 0.34f),
                         label_size,UI_DEMO_TEXT);
        if (i == 0){
            overlay->AddText("text alignment",vec2(b.rect.x,b.rect.y - 10.0f),
                             caption_size,UI_DEMO_LABEL);
        }
    }

    const char* slider_name[2]  = {"text size","corner radius"};
    const float slider_min[2]   = {UI_TEXT_SIZE_MIN,UI_RADIUS_MIN};
    const float slider_max[2]   = {UI_TEXT_SIZE_MAX,UI_RADIUS_MAX};
    const float slider_value[2] = {demo_text_size.load(),demo_radius.load()};
    for (int i = 0;i < 2;i++){
        int idx = widget_button[UI_WIDGET_SLIDER_TEXT + i];
        if ((idx < 0) || (idx >= (int)buttons.size())){
            continue;
        }
        const InputController::TouchButton& b = buttons[idx];
        float span = slider_max[i] - slider_min[i];
        float t    = (span > 0.0f) ? ((slider_value[i] - slider_min[i]) / span) : 0.0f;
        DrawSlider(b,t,accent);

        char text[48];
        snprintf(text,sizeof(text),"%s  %.1f px",slider_name[i],slider_value[i]);
        overlay->AddText(text,vec2(b.rect.x,b.rect.y - 10.0f),caption_size,UI_DEMO_LABEL);
    }

    //Below the last widget, wherever the layout left it.
    int last = widget_button[UI_WIDGET_SLIDER_RADIUS];
    if ((last >= 0) && (last < (int)buttons.size())){
        DrawHitTestReadout(x,buttons[last].rect.y + buttons[last].rect.h + 46.0f);
    }
}

void ApplicationUI::DrawHitTestReadout(float x, float y){
    if (!main_window || !main_window->inputcontroller){
        return;
    }
    InputController* input = main_window->inputcontroller;

    /*
        THE CURSOR, IN THE OVERLAY'S OWN SPACE, with no conversion here.

        GetRelativeMousePosition subtracts the window position the InputController got from
        WM_MOVE, and for a top-level window that message carries the CLIENT area's origin - so
        what comes back is already client pixels, top-left origin, which is exactly the space
        TouchRect and UIOverlay both use. The three agreeing is not a coincidence; it is the
        correspondence the overlay's header calls the one thing worth being careful about.

        IT DEPENDS ON THAT MESSAGE HAVING ARRIVED, which is worth knowing before copying this
        into an app that behaves differently. Until the first WM_MOVE the offset is whatever it
        was initialised to and this reads as screen coordinates. Here Init calls
        main_window->Resize, whose MoveWindow produces one before the first frame, so by the time
        anything below runs it is correct - verified against ClientToScreen 2026-09-21. An app
        that never moves or resizes its window would want to seed it rather than assume.

        It is sampled on the physics thread (PollDevices) and read here on the render thread
        through the accessor, which takes the lock. So it can be one pass old, which for a number
        printed on screen is not something an eye can find.
    */
    int2 cursor = input->GetRelativeMousePosition();

    /*
        THE HIT TEST IS DONE AGAIN HERE, and that needs saying because it looks like duplication.

        InputController tracks which rect a pointer is HOLDING - TouchButton::f_down - but nothing
        tracks hover, because nothing in the engine needs it: a touch screen has no hover, and the
        rect list exists to turn presses into actions. So a readout that wants to say "the cursor
        is over rect 3" has to walk the list itself. GetTouchButtons() is public for exactly this
        kind of consumer.

        FIRST MATCH WINS, deliberately the same rule SubmitPointer uses, so what this prints is
        what a press at that position would actually hit rather than a second opinion about it.
    */
    const std::vector<InputController::TouchButton>& buttons = input->GetTouchButtons();
    int hover = -1;
    int down  = -1;
    for (int i = 0;i < (int)buttons.size();i++){
        if ((hover < 0) && buttons[i].rect.Contains((float)cursor.x,(float)cursor.y)){
            hover = i;
        }
        if (buttons[i].f_down){
            down = i;
        }
    }

    const float caption_size = 15.0f;
    const float line_size    = 16.0f;
    const float line_step    = line_size * 1.45f;

    overlay->AddText("hit test - InputController's, not the overlay's",
                     vec2(x,y),caption_size,UI_DEMO_LABEL);
    y += line_step * 1.4f;

    char text[96];
    snprintf(text,sizeof(text),"cursor : %4d, %4d",cursor.x,cursor.y);
    overlay->AddText(text,vec2(x,y),line_size,UI_DEMO_TEXT);
    y += line_step;

    if (hover >= 0){
        snprintf(text,sizeof(text),"hover  : #%d %s",hover,buttons[hover].label);
    }else{
        snprintf(text,sizeof(text),"hover  : -");
    }
    overlay->AddText(text,vec2(x,y),line_size,(hover >= 0) ? UI_DEMO_TEXT : UI_DEMO_LABEL);
    y += line_step;

    if (down >= 0){
        //The held rect and where on it the pointer is. Drag off the slider and watch this keep
        //reporting the slider: a press CAPTURES its rect until the release, wherever the pointer
        //then goes, which is what lets a drag leave a ten-pixel track without dropping it.
        snprintf(text,sizeof(text),"down   : #%d %s at %.0f,%.0f",down,buttons[down].label,
                 buttons[down].pointer_x,buttons[down].pointer_y);
    }else{
        snprintf(text,sizeof(text),"down   : -");
    }
    overlay->AddText(text,vec2(x,y),line_size,(down >= 0) ? UI_DEMO_TEXT : UI_DEMO_LABEL);
    y += line_step * 1.4f;

    snprintf(text,sizeof(text),"quads  : %d",overlay->GetNumQuads());
    overlay->AddText(text,vec2(x,y),line_size,UI_DEMO_LABEL);
}

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationUI::DrawImGuiUI(){
    //Only when this window has no entry in imgui.ini yet, so a panel the user has dragged
    //somewhere stays where they put it. Clear of the widget column at the default window size.
    ImGui::SetNextWindowPos(ImVec2(1050.0f,40.0f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f,700.0f),ImGuiCond_FirstUseEver);
    ImGui::Begin("Overlay demo");

    ImGui::TextWrapped("The engine's 2D overlay - core/UIOverlay.h. Everything below the scene "
                       "and under these panels is one draw call.");
    ImGui::Separator();

    /*
        The readout first, because it is the thing worth watching while the sliders move. A quad
        count that jumps when a slider does not is the batch growing for a reason nobody asked
        for - AddText is per glyph, AddNineSliceDebug is nine at a time - and that is the number
        that says so.
    */
    if (overlay && overlay->IsReady()){
        const ui_font_header& font = overlay->GetFontHeader();
        ImGui::Text("ready   : yes, font em %.0f px, line height %.0f px",
                    font.em_px,font.line_height_px);
        ImGui::Text("quads   : %d this frame",overlay->GetNumQuads());
    }else{
        //Not an error state to fix here: Init logs why and leaves the overlay inert on purpose,
        //so the app runs without a HUD rather than not at all.
        ImGui::TextColored(ImVec4(1.0f,0.6f,0.4f,1.0f),
                           "overlay not ready - font or shader missing, see the log");
    }

    ImGui::Separator();
    ImGui::TextWrapped("These are the same values the overlay widgets drive. Move one and watch "
                       "the other follow.");

    /*
        LOAD, HAND ImGui A LOCAL, STORE BACK IF IT CHANGED. ImGui's controls take a raw pointer
        and std::atomic cannot supply one; the round trip is what that costs. Storing only on
        change matters as well as reading well - an unconditional store every frame would fight
        a drag in progress on the physics thread for the one frame they overlap.
    */
    float f = demo_radius.load();
    if (ImGui::SliderFloat("radius",&f,UI_RADIUS_MIN,UI_RADIUS_MAX,"%.1f px")){
        demo_radius.store(f);
    }
    f = demo_thickness.load();
    if (ImGui::SliderFloat("outline",&f,0.5f,16.0f,"%.1f px")){
        demo_thickness.store(f);
    }
    f = demo_text_size.load();
    if (ImGui::SliderFloat("text size",&f,UI_TEXT_SIZE_MIN,UI_TEXT_SIZE_MAX,"%.1f px")){
        demo_text_size.store(f);
    }
    ImGui::ColorEdit4("accent",demo_color);

    ImGui::Separator();
    ImGui::Text("text alignment");
    int align = demo_align.load();
    bool f_align_changed = false;
    f_align_changed |= ImGui::RadioButton("left",&align,UI_ALIGN_LEFT);
    ImGui::SameLine();
    f_align_changed |= ImGui::RadioButton("center",&align,UI_ALIGN_CENTER);
    ImGui::SameLine();
    f_align_changed |= ImGui::RadioButton("right",&align,UI_ALIGN_RIGHT);
    if (f_align_changed){
        demo_align.store(align);
    }

    bool b = f_demo_measure.load();
    if (ImGui::Checkbox("show the MeasureText box",&b)){
        f_demo_measure.store(b);
    }

    ImGui::Separator();
    ImGui::Text("nine-slice");
    f = demo_nine_inset.load();
    if (ImGui::SliderFloat("inset",&f,0.0f,60.0f,"%.1f px")){
        demo_nine_inset.store(f);
    }
    f = demo_panel_w.load();
    if (ImGui::SliderFloat("panel w",&f,60.0f,700.0f,"%.0f px")){
        demo_panel_w.store(f);
    }
    f = demo_panel_h.load();
    if (ImGui::SliderFloat("panel h",&f,60.0f,400.0f,"%.0f px")){
        demo_panel_h.store(f);
    }

    ImGui::Separator();
    b = f_demo_labels.load();
    if (ImGui::Checkbox("row captions",&b)){
        f_demo_labels.store(b);
    }

    ImGui::End();
}
#endif //USE_IMGUI
