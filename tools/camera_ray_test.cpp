/*
    Headless checks for Camera::GetPixelRay - backlog item 31.

    GetPixelRay is pure maths on the camera's own state: it touches no GL, no window and no
    simulation, so it can be exercised in a console program that links the ordinary core objects.
    That matters because picking is otherwise only testable by moving a mouse and looking, which
    is exactly the kind of verification that misses a sign error.

    Build (after a normal `make APP=<anything>`, which produces the core objects this links):

        g++ -std=c++17 -fno-exceptions -DJSON_NOEXCEPTION -D_WIN32 \
            -Icore/ -Icore/physics -Icore/skeleton -I3rdparty/ -I3rdparty/imgui/ \
            -I3rdparty/stb_image/ -I3rdparty/miniz/ -I3rdparty/reactphysics3d/ \
            tools/camera_ray_test.cpp \
            $(ls core/*.o core/physics/*.o core/skeleton/*.o | grep -v SoundSystem | grep -v WaveFile) \
            -o camera_ray_test.exe -Llibs/ -lreactphysics3d -limgui -lthirdparty \
            -lsetupapi -lhid -luser32 -lopengl32 -lgdi32 -lws2_32 -lcrypt32 -lXinput9_1_0 \
            -static-libstdc++ -static-libgcc -static

    Run from the repo root. Exit code 0 = all checks passed.
*/

#include <stdio.h>
#include <math.h>
#include "Camera.h"

static int num_failed = 0;
static int num_passed = 0;

static bool NearlyEqual(float a, float b, float tolerance = 0.001f){
    return fabsf(a - b) <= tolerance;
}

static bool NearlyEqual(const vec3& a, const vec3& b, float tolerance = 0.001f){
    return NearlyEqual(a.x,b.x,tolerance) && NearlyEqual(a.y,b.y,tolerance) && NearlyEqual(a.z,b.z,tolerance);
}

static void Check(const char* what, const vec3& got, const vec3& expected, float tolerance = 0.001f){
    bool f_ok = NearlyEqual(got,expected,tolerance);
    printf("  [%s] %-52s got (%7.3f,%7.3f,%7.3f) expected (%7.3f,%7.3f,%7.3f)\n",
           f_ok ? "PASS" : "FAIL", what,
           got.x,got.y,got.z, expected.x,expected.y,expected.z);
    if (f_ok){ num_passed++; }else{ num_failed++; }
}

//Where a ray crosses a plane of constant z. Every test scene below is arranged so that is the
//interesting question - it is what a board, a terrain or a UI plane amounts to.
static vec3 IntersectZPlane(const ray& r, float plane_z){
    float t = (plane_z - r.origin.z) / r.direction.z;
    return r.origin + (r.direction * t);
}

int main(){
    printf("Camera::GetPixelRay checks\n\n");

    /*
        1. Perspective, viewport filling the window. The centre pixel must look straight down the
        camera's forward axis, and the ray must start at the camera.
    */
    printf("1. Perspective, full-window viewport (1200x900)\n");
    {
        Camera camera;
        camera.SetPosition(vec3(0,0,10));
        camera.SetLookAt(vec3(0,0,0));
        camera.SetupPerspective(1200,900,45.0f,0.1f,100.0f);
        camera.viewport.px_offset_x = 0;
        camera.viewport.px_offset_y = 0;
        camera.CalculateLookatMatrix();

        int2 centre(600,450);
        ray r = camera.GetPixelRay(centre);
        Check("centre pixel origin",r.origin,vec3(0,0,10));
        Check("centre pixel direction",r.direction,camera.GetForward());
    }

    /*
        2. THE ITEM 31 CASE. Same camera, but the 3D is confined to a 900x900 square docked to the
        right of a 1200-wide window (renderer->viewport_x = 300), which is what ApplicationTank
        does to keep a panel strip clear. The mouse still reports WINDOW pixels, so the pixel at
        the centre of that square is x = 300 + 450 = 750, not 600.

        A ray through the centre of the viewport must be the same ray whether or not the viewport
        is offset. Before the fix this failed: GetPixelRay divided by the viewport's size but
        never subtracted its origin, so the whole picture was skewed by the offset.
    */
    printf("\n2. Perspective, 900x900 viewport offset 300px from the left (item 31)\n");
    {
        Camera camera;
        camera.SetPosition(vec3(0,0,10));
        camera.SetLookAt(vec3(0,0,0));
        camera.SetupPerspective(900,900,45.0f,0.1f,100.0f);
        camera.viewport.px_offset_x = 300;   //renderer->viewport_x
        camera.viewport.px_offset_y = 0;
        camera.CalculateLookatMatrix();

        int2 viewport_centre(300 + 450,450);
        ray r = camera.GetPixelRay(viewport_centre);
        Check("viewport-centre pixel direction",r.direction,camera.GetForward());

        //And a pixel on the viewport's left edge must sit on the left edge of the frustum, not
        //somewhere in the middle of it. Screen-right is +X for a camera looking down -Z with +Y
        //up, so the LEFT edge is negative - and the orthographic branch agrees, which is the
        //consistency check that matters (section 3 puts a right-of-centre pixel at +X).
        int2 left_edge(300,450);
        ray edge = camera.GetPixelRay(left_edge);
        vec3 at_far = IntersectZPlane(edge,-90.0f);          //100 units ahead of the camera
        float half_width = tanf(toradians(45.0f) / 2.0f) * 100.0f * camera.viewport.aspect;
        Check("viewport left-edge pixel at z=-90",at_far,vec3(-half_width,0,-90),0.5f);

        //What the bug looked like: the old code never subtracted the viewport's origin, which is
        //exactly this camera with px_offset_x left at zero while the caller still passes a window
        //pixel. Printed rather than asserted - the point is the size of the error, not a value
        //worth pinning down.
        camera.viewport.px_offset_x = 0;
        ray unfixed = camera.GetPixelRay(viewport_centre);
        vec3 wrong = IntersectZPlane(unfixed,-90.0f);
        printf("  [note] offset ignored (the old behaviour): centre pixel lands at x=%.2f "
               "instead of 0.00, i.e. %.0f%% of the way to the frustum edge\n",
               wrong.x,100.0f * fabsf(wrong.x) / half_width);
        camera.viewport.px_offset_x = 300;
    }

    /*
        3. Orthographic looking down -Z - the Tetris board's camera. An orthographic ray starts on
        the image plane and travels along the camera's forward axis, so the centre pixel must
        start exactly at the camera and a pixel a quarter of the way to the right edge must start
        half a zoom to the right.

        The old code hardcoded `vec3(-w*zoom*aspect, 0, h*zoom)` - screen-right mapped to world
        -X and screen-up to world +Z, with the camera's own orientation ignored entirely (there
        was a TODO saying so). That is only right for a camera looking straight down -Y.
    */
    printf("\n3. Orthographic looking down -Z, zoom 11.5, 900x900 (the Tetris camera)\n");
    {
        Camera camera;
        camera.SetPosition(vec3(6.0f,9.5f,40.0f));
        camera.SetLookAt(vec3(6.0f,9.5f,0.0f));
        camera.SetupOrthographic(900,900,11.5f,0.1f,120.0f);
        camera.viewport.px_offset_x = 0;
        camera.viewport.px_offset_y = 0;
        camera.CalculateLookatMatrix();

        int2 centre(450,450);
        ray r = camera.GetPixelRay(centre);
        Check("centre pixel origin",r.origin,vec3(6.0f,9.5f,40.0f));
        Check("centre pixel direction",r.direction,camera.GetForward());
        Check("centre pixel hits board at z=0",IntersectZPlane(r,0.0f),vec3(6.0f,9.5f,0.0f));

        //Three quarters across = +0.5 in normalised space = half a zoom to the right.
        int2 right(675,450);
        ray rr = camera.GetPixelRay(right);
        Check("3/4-right pixel hits board",IntersectZPlane(rr,0.0f),vec3(6.0f + 5.75f,9.5f,0.0f));

        //A quarter down the screen = +0.5 vertically, i.e. ABOVE the centre in world terms.
        int2 up(450,225);
        ray ru = camera.GetPixelRay(up);
        Check("1/4-down pixel hits board above centre",IntersectZPlane(ru,0.0f),vec3(6.0f,9.5f + 5.75f,0.0f));
    }

    /*
        4. Orthographic with an offset viewport - items 31 and the orthographic fix together,
        which is the combination the Tetris app would hit the moment it reserved a strip for the
        engine's debug panels.
    */
    printf("\n4. Orthographic, 900x900 viewport offset 300px from the left\n");
    {
        Camera camera;
        camera.SetPosition(vec3(6.0f,9.5f,40.0f));
        camera.SetLookAt(vec3(6.0f,9.5f,0.0f));
        camera.SetupOrthographic(900,900,11.5f,0.1f,120.0f);
        camera.viewport.px_offset_x = 300;
        camera.viewport.px_offset_y = 0;
        camera.CalculateLookatMatrix();

        int2 viewport_centre(300 + 450,450);
        ray r = camera.GetPixelRay(viewport_centre);
        Check("viewport-centre pixel hits board centre",IntersectZPlane(r,0.0f),vec3(6.0f,9.5f,0.0f));

        //Same "what it used to do" note as section 2, in world units this time: on the Tetris
        //board one unit is one cell, so this is the error measured in blocks.
        camera.viewport.px_offset_x = 0;
        ray unfixed = camera.GetPixelRay(viewport_centre);
        vec3 wrong = IntersectZPlane(unfixed,0.0f);
        printf("  [note] offset ignored (the old behaviour): centre pixel lands at x=%.2f instead "
               "of 6.00 - off by %.1f board cells\n",wrong.x,fabsf(wrong.x - 6.0f));
        camera.viewport.px_offset_x = 300;
    }

    /*
        5. A viewport that is a band along the BOTTOM of a taller window. renderer->viewport_y is
        measured from the bottom (GL's origin) while the mouse is measured from the top, so this
        is the case where the flip has to be right - see the comment on Camera::viewport.px_offset_y.

        Window 900 tall, viewport 600 tall sitting at viewport_y = 0 (along the bottom). In cursor
        space that band starts 300px down from the top, so its centre pixel is y = 300 + 300 = 600.
    */
    printf("\n5. Orthographic, 900x600 viewport as a band along the bottom of a 900-tall window\n");
    {
        Camera camera;
        camera.SetPosition(vec3(6.0f,9.5f,40.0f));
        camera.SetLookAt(vec3(6.0f,9.5f,0.0f));
        camera.SetupOrthographic(900,600,11.5f,0.1f,120.0f);
        camera.viewport.px_offset_x = 0;
        camera.viewport.px_offset_y = 300;   //window_height - (viewport_y + viewport_height)
        camera.CalculateLookatMatrix();

        int2 band_centre(450,600);
        ray r = camera.GetPixelRay(band_centre);
        Check("band-centre pixel hits board centre",IntersectZPlane(r,0.0f),vec3(6.0f,9.5f,0.0f));
    }

    printf("\n%i passed, %i failed\n",num_passed,num_failed);
    return num_failed == 0 ? 0 : 1;
}
