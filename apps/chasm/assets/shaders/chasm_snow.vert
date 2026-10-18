#version 450 core
/*
    THE SNOW (ApplicationChasmWeather.cpp; docs/weather_plan.md): one mesh of flakes drawn once per lattice cell round
    the camera. A flake falls and drifts inside its cell and wraps round it, so it stays put in the world while the
    camera moves; the cell's corner comes in as the instance's translation.

    A vertex is one corner of its flake's quad, and carries the whole flake: where it starts in the cell (0..1), its
    RANK in normal.x - drawn only below snow_density, so a thin snow is the same flakes thinned - its size and speed in
    normal.y, phases in normal.z and the tangent.

    The gusts come from the trees' wind (core's wind field, on the ground): what blows here past the wind at the view
    carries a flake on, so a gust rolling through drives the snow with it. Each flake faces the eye and is drawn out
    along its motion - a little for snow; rain would be drawn out far.

    A cell's size is its instance's scale, so a cell need not be a cube. THE BAND (snow_band 1) is the spindrift: flat
    cells laid on the ground along the snow front's line, the flakes racing across them low and drawn out, thickest
    at the ground and faded at the band's two long edges.
*/
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS   4

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;

struct InstanceData{
    mat4 mat_transformscale;
    int material_slot[NUM_MATERIAL_SLOTS];
    float morph_factors[MAX_MORPH_TARGETS];
    int objectid;
    int num_bones;
    int vertex_count;
    int num_morph_targets;
};
layout (std430, binding = 0) buffer InstanceDataBuffer{
    InstanceData instance_data[];
};

//core's wind field (Renderer::SetWindField) - default.vert's declaration.
layout (std430, binding = 7) readonly buffer WindBuffer{
    vec4  wind_rect;
    ivec4 wind_size;
    vec4  wind_params;
    vec2  wind_grid[];
};

layout(location = 0) uniform mat4 mat_worldcam;
uniform vec3  eye_position;
uniform float snow_seconds;
uniform float snow_density;
uniform vec3  snow_centre;     //the point the snow is seen round...
uniform float snow_reach;      //...out to this far
uniform float snow_fall;       //units a second
uniform float snow_size;       //a flake's half width
uniform vec2  snow_drift;      //how far the wind has carried the flakes, wrapped to the cell
uniform vec2  snow_wind;       //the wind at the view
uniform float snow_carry;      //...the share of it a flake goes with
uniform float snow_gust_lag;
uniform float snow_streak;
uniform float snow_near;
uniform int   snow_band = 0;

layout (location = 0) out vec2 vcorner;
layout (location = 1) out float valpha;

//The field's wind at a point on the ground - and off the grid, the view's own, so no gust.
vec2 SampleWind(vec2 p){
    vec2 g = (p - wind_rect.xy) * wind_rect.zw;
    int w = wind_size.x;
    if ((g.x < 0.0) || (g.y < 0.0) || (g.x >= float(w - 1)) || (g.y >= float(wind_size.y - 1))){
        return snow_wind;
    }
    ivec2 i = ivec2(g);
    vec2 t = g - vec2(i);
    int k = i.y * w + i.x;
    return mix(mix(wind_grid[k],wind_grid[k + 1],t.x),mix(wind_grid[k + w],wind_grid[k + w + 1],t.x),t.y);
}

float CellHash(vec3 p){
    return fract(sin(dot(p,vec3(12.9898,78.233,37.719))) * 43758.5453);
}

void main(){
    mat4 placed = instance_data[gl_InstanceID].mat_transformscale;
    vec3 cell = placed[3].xyz;
    vec3 size = vec3(length(placed[0].xyz),length(placed[1].xyz),length(placed[2].xyz));
    //Each cell its own: which flakes a thin snow keeps, and a head start on the clock - one cell is not the next's copy.
    float h = CellHash(cell);
    float rank = fract(normal.x + h);
    vcorner = uv;
    valpha = 0.0;
    if (rank >= snow_density){
        gl_Position = vec4(2.0,2.0,2.0,1.0);
        return;
    }
    float t = snow_seconds + h * 97.0;
    float speed = mix(0.8,1.25,normal.y) * snow_fall;
    vec3 p = position * size;
    p.y -= speed * t;
    p.xz += snow_drift * mix(0.85,1.15,normal.z);
    p.x += 0.35 * sin(t * (0.6 + 0.5 * tangent.x) + 6.2832 * normal.z);
    p.z += 0.35 * sin(t * (0.5 + 0.5 * tangent.y) + 6.2832 * tangent.x);
    vec3 local = mod(p,size);
    vec3 w = cell + local;

    //What blows here past the view's wind: a gust passing carries the flake on, and then lets it go.
    vec2 gust = vec2(0.0);
    if ((wind_size.z != 0) && (wind_size.w == 1)){
        gust = clamp(SampleWind(w.xz) - snow_wind,vec2(-10.0),vec2(10.0));
    }
    w.xz += gust * (snow_carry * snow_gust_lag);
    vec2 carried = (snow_wind + gust) * snow_carry;
    vec3 velocity = vec3(carried.x,-speed,carried.y);

    float to_eye = length(w - eye_position);
    float fade;
    if (snow_band == 1){
        vec3 f = local / size;
        float up = 1.0 - f.y;
        fade = smoothstep(0.0,0.3,f.z) * smoothstep(0.0,0.3,1.0 - f.z) * smoothstep(0.0,0.1,f.y) * up * up;
    }else{
        fade = 1.0 - smoothstep(0.7 * snow_reach,snow_reach,length(w - snow_centre));
    }
    fade *= smoothstep(snow_near,snow_near * 2.5,to_eye);

    //Facing the eye, drawn out along the motion as the eye sees it.
    vec3 view = (w - eye_position) / max(to_eye,1e-3);
    vec3 axis = velocity - view * dot(velocity,view);
    float across = length(axis);
    axis = (across > 1e-4) ? axis / across : normalize(cross(view,vec3(1.0,0.0,0.0)));
    vec3 side = normalize(cross(view,axis));
    float half_width = mix(0.6,1.4,normal.y) * snow_size;
    vec3 at = w + side * (uv.x * half_width) + axis * (uv.y * half_width * (1.0 + across * snow_streak));
    gl_Position = mat_worldcam * vec4(at,1.0);
    valpha = fade * mix(0.55,1.0,tangent.x);
}
