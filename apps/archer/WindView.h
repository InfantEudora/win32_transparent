#ifndef _ARCHER_WIND_VIEW_H_
#define _ARCHER_WIND_VIEW_H_

#include "Wind.h"
#include "Scene.h"
#include "Camera.h"
#include "type_vertex.h"

#include <vector>

/*
    The wind's debug view: one line mesh drawn over the level, rebuilt every frame it is shown, from
    whatever part of the field the camera can see. See wind_plan.md section 3.

      ARROWS       on a grid, coloured by speed against the mean wind - dark blue still, green
                   at the mean wind, through yellow to red at two and a half times it
      STREAMLINES  from the upwind edge of the view, plus a pair from inside every eddy - the
                   field frozen at this tick, so an eddy draws as the closed loop it is
      EDDIES       a circle of each blob's radius, brighter the stronger it is, and a cross on
                   each shedding corner

    Drawn in front of the blocks (their front faces are at z 1.5 or so), and every point is slid
    along its ray to the eye rather than just pushed forward, so the lines sit exactly over the
    z = 0 plane they describe instead of spreading out from the middle of the screen.

    RENDER THREAD ONLY: Init adds an object to the scene (before the physics thread runs), Update
    uploads a mesh. The field is read through its const interface, which is safe while the
    physics thread ticks.
*/

/*
    What a perspective camera looking down -Z sees of the z = 0 plane, grown by `pad` (a fraction)
    on every side. False when the camera is not in front of the plane. Shared by the debug view and
    the grid the app bakes for the renderer, so both cover the same ground.
*/
bool WindViewRect(Camera* camera, float pad, float& x0, float& y0, float& x1, float& y1);

struct WindViewOptions{
    bool  f_arrows       = true;
    bool  f_streamlines  = true;
    bool  f_eddies       = true;
    float arrow_spacing  = 1.0f;
    float arrow_scale    = 0.3f;    //units of arrow per unit/s of wind, before the spacing cap
    float stream_spacing = 1.5f;    //between the seeds on the upwind edge
    float stream_step    = 0.2f;
    int   stream_points  = 240;
    float z_front        = 2.0f;
};

class WindView{
public:
    //Creates the (hidden) line object. Call from Init, on the render thread.
    void Init(Scene* scene);
    void SetVisible(bool f_on);
    bool IsVisible() const { return f_visible; }
    //Redraws the part of the field the camera can see. PreRender; does nothing while hidden.
    void Update(const WindField& wind, int64_t tick, Camera* camera);

    int VertexCount() const { return (int)verts.size(); }

    WindViewOptions options;

private:
    Object* object = NULL;
    Mesh*   mesh = NULL;
    bool    f_visible = false;
    std::vector<line_vertex> verts;
    vec3    eye;

    //A segment in the play plane, slid toward the eye to z_front.
    void Line(float ax, float ay, float bx, float by, uint32_t color);
};

#endif
