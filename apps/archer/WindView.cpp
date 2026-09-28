#include "WindView.h"
#include "Mesh.h"

#include <math.h>

//Dark blue at rest, green at the mean wind, yellow, then red from 2.5x the mean wind up.
static uint32_t SpeedColor(float ratio){
    static const float stop[5]    = {0.0f,0.5f,1.0f,1.6f,2.5f};
    static const float rgb[5][3]  = {{0.10f,0.20f,0.65f},{0.10f,0.75f,0.85f},{0.20f,0.90f,0.25f},
                                     {0.95f,0.90f,0.15f},{0.95f,0.20f,0.10f}};
    int k = 0;
    while ((k < 3) && (ratio > stop[k + 1])){
        k++;
    }
    float t = fminf(fmaxf((ratio - stop[k]) / (stop[k + 1] - stop[k]),0.0f),1.0f);
    uint32_t c = 0xFF000000u;
    for (int i = 0; i < 3; i++){
        float v = rgb[k][i] + (rgb[k + 1][i] - rgb[k][i]) * t;
        c |= (uint32_t)(v * 255.0f + 0.5f) << (16 - 8 * i);
    }
    return c;
}

bool WindViewRect(Camera* camera, float pad, float& x0, float& y0, float& x1, float& y1){
    if (!camera){
        return false;
    }
    vec3 eye = camera->GetPosition();
    if (eye.z <= 0.5f){
        return false;
    }
    float half_h = eye.z * tanf(camera->viewport.fov * 0.5f * 3.14159265f / 180.0f) * (1.0f + pad);
    float half_w = half_h * fmaxf(camera->viewport.aspect,0.1f);
    x0 = eye.x - half_w;
    x1 = eye.x + half_w;
    y0 = eye.y - half_h;
    y1 = eye.y + half_h;
    return true;
}

void WindView::Init(Scene* scene){
    object = new Object();
    object->name = "wind_debug";
    object->SetVisualOnly(true);
    mesh = new Mesh();
    object->SetMesh(mesh);
    object->SetPickability(false);
    object->SetVisibility(false);
    scene->AddObject(object);
    object->UpdatePhysicsState();
}

void WindView::SetVisible(bool f_on){
    f_visible = f_on;
    if (object && !f_on){
        object->SetVisibility(false);
    }
}

void WindView::Line(float ax, float ay, float bx, float by, uint32_t color){
    //Along the ray from the eye through (x, y, 0) to depth z_front: the same pixel, nearer.
    float s = (eye.z - options.z_front) / fmaxf(eye.z,1e-3f);
    line_vertex v;
    v.color = color;
    v.pos = vec3(eye.x + (ax - eye.x) * s,eye.y + (ay - eye.y) * s,options.z_front);
    verts.push_back(v);
    v.pos = vec3(eye.x + (bx - eye.x) * s,eye.y + (by - eye.y) * s,options.z_front);
    verts.push_back(v);
}

void WindView::Update(const WindField& wind, int64_t tick, Camera* camera){
    if (!object || !f_visible || !camera){
        return;
    }
    verts.clear();
    eye = camera->GetPosition();
    //Only while the camera is in front of the play plane - the orbit camera can swing behind it,
    //and then the slide toward the eye would put the lines behind the level.
    if (!wind.IsBuilt() || (eye.z <= options.z_front + 0.5f)){
        object->SetVisibility(false);
        return;
    }

    //What the camera sees of the z = 0 plane, padded a little, clipped to the field.
    float vx0, vy0, vx1, vy1;
    WindViewRect(camera,0.1f,vx0,vy0,vx1,vy1);
    float rx0 = fmaxf(vx0,wind.MinX()), rx1 = fminf(vx1,wind.MaxX());
    float ry0 = fmaxf(vy0,wind.MinY()), ry1 = fminf(vy1,wind.MaxY());
    float mean = fmaxf(fabsf(wind.Params().speed),0.05f);

    if (options.f_arrows){
        float sp = fmaxf(options.arrow_spacing,0.25f);
        for (float y = ceilf(ry0 / sp) * sp; y <= ry1; y += sp){
            for (float x = ceilf(rx0 / sp) * sp; x <= rx1; x += sp){
                if (wind.Distance(x,y) < 0.1f){
                    continue;
                }
                WindVec v = wind.Velocity(x,y,tick);
                float speed = sqrtf(v.x * v.x + v.y * v.y);
                if (speed < 1e-4f){
                    continue;
                }
                float len = fminf(speed * options.arrow_scale,0.9f * sp);
                float dx = v.x / speed, dy = v.y / speed;
                //Centred on the grid point, so a row of arrows reads as flow rather than as
                //a row of starting points.
                float ax = x - 0.5f * len * dx, ay = y - 0.5f * len * dy;
                float bx = x + 0.5f * len * dx, by = y + 0.5f * len * dy;
                uint32_t c = SpeedColor(speed / mean);
                Line(ax,ay,bx,by,c);
                float hl = 0.3f * len;
                Line(bx,by,bx - hl * (dx * 0.87f - dy * 0.5f),by - hl * (dy * 0.87f + dx * 0.5f),c);
                Line(bx,by,bx - hl * (dx * 0.87f + dy * 0.5f),by - hl * (dy * 0.87f - dx * 0.5f),c);
            }
        }
    }

    std::vector<WindEddy> eddies;
    wind.Eddies(tick,eddies);
    float strongest = 1e-4f;
    for (const WindEddy& e : eddies){
        strongest = fmaxf(strongest,fabsf(e.strength));
    }

    if (options.f_streamlines){
        std::vector<float> pts;
        auto trace = [&](float sx, float sy, uint32_t color){
            if (wind.Distance(sx,sy) < 0.05f){
                return;
            }
            pts.clear();
            wind.TraceStreamline(sx,sy,tick,options.stream_step,options.stream_points,pts);
            for (size_t k = 2; k + 1 < pts.size(); k += 2){
                Line(pts[k - 2],pts[k - 1],pts[k],pts[k + 1],color);
            }
        };
        float upwind = (wind.Params().speed >= 0.0f) ? rx0 : rx1;
        float sp = fmaxf(options.stream_spacing,0.25f);
        for (float y = ceilf(ry0 / sp) * sp; y <= ry1; y += sp){
            trace(upwind,y,0xFFD8DDE6u);
        }
        for (const WindEddy& e : eddies){
            if ((fabsf(e.strength) < 0.15f * strongest) || (e.x < rx0) || (e.x > rx1) || (e.y < ry0) || (e.y > ry1)){
                continue;
            }
            trace(e.x,e.y + 0.4f * e.radius,0xFFF0A0F0u);
            trace(e.x,e.y + 0.8f * e.radius,0xFFF0A0F0u);
        }
    }

    if (options.f_eddies){
        const int segments = 24;
        for (const WindEddy& e : eddies){
            float ex = e.radius * e.stretch;
            if ((e.x + ex < rx0) || (e.x - ex > rx1) || (e.y + e.radius < ry0) || (e.y - e.radius > ry1)){
                continue;
            }
            float t = fabsf(e.strength) / strongest;
            if (t < 0.02f){
                continue;
            }
            uint32_t b = (uint32_t)(80.0f + 175.0f * t);
            uint32_t c = 0xFF000000u | (b << 16) | ((b / 3) << 8) | b;
            for (int k = 0; k < segments; k++){
                float a0 = 6.2831853f * k / segments, a1 = 6.2831853f * (k + 1) / segments;
                Line(e.x + ex * cosf(a0),e.y + e.radius * sinf(a0),
                     e.x + ex * cosf(a1),e.y + e.radius * sinf(a1),c);
            }
        }
        for (const WindCorner& c : wind.Corners()){
            Line(c.x - 0.25f,c.y - 0.25f,c.x + 0.25f,c.y + 0.25f,0xFFFF9020u);
            Line(c.x - 0.25f,c.y + 0.25f,c.x + 0.25f,c.y - 0.25f,0xFFFF9020u);
        }
    }

    if (verts.empty()){
        object->SetVisibility(false);
        return;
    }
    mesh->SetLineMeshData(&verts[0],(int)verts.size());
    object->SetVisibility(true);
}
