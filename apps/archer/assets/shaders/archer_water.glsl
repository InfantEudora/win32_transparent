#include "texture_units.glsl"
/*
    The archer's water - apps/archer/water_plan.md. Flat shades that move, and nothing else: no
    reflection, no refraction, no transparency. Its colour is worked out here and then LIT like any
    other surface (lighting.glsl), so the fall in the bay's shade is shaded and the fireflies light
    it - the bay is dim green, and unlit water there glowed like a sign.

    TWO PROGRAMS FROM THIS ONE BODY, each its own two-line entry file that defines which it is
    and includes this, so the two looks are tuned apart without two sources to keep in step:

      archer_water_sheet.frag  the SHEETS - uv.x across (-1..1), uv.y down (0 at the top of a
         sheet, 1 at its foot). Streaks down the fall, in three shades, running at flow_speed; a
         light lip where it goes over the edge; white where it comes down; ragged edges.
      archer_water_flat.frag   the FLAT water - uv.x world x, uv.y distance from where the water
         came from. Rings spreading from there, bent by a slow noise so they are not circles; foam
         near the source; and a SHORE, from the G-buffer - below.

    --- WHY TWO FILES AND NOT A UNIFORM ------------------------------------------------------------
    The sheets' ragged edges are a `discard`, and a program that can discard loses the early depth
    test - for every fragment, whether or not that one discards. Most of the stream is under the
    grass, behind the ground's slab, so with the edge cut compiled into it too every one of those
    hidden fragments ran the whole light loop before the depth test threw it away: the water cost
    1.1 ms of GPU a frame against 0.09 without it in view. Compiled out of the flat program, the
    hidden ones are rejected before they are shaded.

    --- THE SHORE, AND WHY THIS IS NOT IN THE G-BUFFER --------------------------------------------
    The water is left out of the deferred pass (Shader::f_writes_gbuffer false), so at every water
    fragment the G-buffer still holds whatever is BELOW it: the rock of the pool, the wall's face
    under the stream. Its height under the surface is then a subtraction, and a line of foam where
    that is small is a shoreline wherever rock meets water - round every rim, every bump the noise
    left - with nothing measured on the CPU and nothing baked into the mesh. Deep water, where the
    ray found nothing close, is darkened a little.

    --- THE CLOCK --------------------------------------------------------------------------------
    water_seconds is simulation time (ticks * ARCHER_DT), so a paused game holds the water still
    and sim_step moves it by exact ticks, like the grass's wind.
*/

layout (location = 0) out vec4 color;

//default.vert's varyings, at its locations - lighting.glsl reads them.
layout (location = 0)  in vec3 vposition;
layout (location = 1)  in vec3 vnormal;
layout (location = 2)  in vec2 vuv;
layout (location = 3)  in mat3 TBN;
layout (location = 6)  flat in int vmatindex;
layout (location = 7)  flat in int vobjid;
layout (location = 8)  in vec4 vshadow;

#include "lighting.glsl"

layout (binding = TEXUNIT_GBUFFER_DEPTH) uniform sampler2D gbuffer_depth;
layout (binding = TEXUNIT_GBUFFER_POSITION) uniform sampler2D gbuffer_position;
uniform vec2 render_target_size = vec2(1.0);

uniform float water_seconds = 0.0;
//The three shades and the foam. The light one is the reference's lit blue, the deep one where it
//is darkest; mid is most of the surface.
//A step darker and greener than the reference's, which is lit by an open sky: in the bay's green
//shade the reference's own blues read as a lit sign.
uniform vec3  water_light = vec3(0.50,0.76,0.86);
uniform vec3  water_mid   = vec3(0.26,0.54,0.72);
uniform vec3  water_deep  = vec3(0.14,0.36,0.54);
uniform vec3  water_foam  = vec3(0.86,0.94,0.96);
//How fast the pattern moves with the water - down a sheet in sheet lengths a second, out across
//the flat in world units a second.
uniform float flow_speed = 0.8;
//Self-lit share of its own colour, so it still reads as water in the bay's shade.
uniform float water_fill = 0.12;

//Value noise, 0..1, smooth. The sin-fract hash is plenty: it only has to be stable per cell.
float hash21(vec2 p){
    return fract(sin(dot(p,vec2(127.1,311.7))) * 43758.5453);
}
float vnoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0,0.0));
    float c = hash21(i + vec2(0.0,1.0));
    float d = hash21(i + vec2(1.0,1.0));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}

//Three flat shades out of one 0..1 value, with a narrow soft step between them - flat bands with
//edges that do not crawl, rather than a gradient.
vec3 Shades(float n){
    vec3 c = mix(water_deep,water_mid,smoothstep(0.30,0.36,n));
    return mix(c,water_light,smoothstep(0.62,0.68,n));
}

vec3 SheetColour(){
    float across = vuv.x;
    float down = vuv.y;
    float t = water_seconds * flow_speed;
    //Streaks: long along the fall, narrow across it, moving down. Two octaves, the finer one a
    //little faster, so a streak changes as it goes rather than sliding down whole.
    float n = 0.65 * vnoise(vec2(across * 3.5,(down - t) * 2.0))
            + 0.35 * vnoise(vec2(across * 9.0 + 3.1,(down - t * 1.3) * 5.0));
    vec3 c = Shades(n);
    //Its edges a shade deeper - the sheet thins there and the rock shows through.
    c = mix(c,water_deep,smoothstep(0.75,1.0,abs(across)) * 0.6);
    //Light where it rolls over the edge, the one place the reference is brightest.
    c = mix(c,water_light,smoothstep(0.14,0.04,down));
    //White at the foot, a band rather than a fade, broken up by the same streaks so its top edge
    //is not ruled.
    float foam = smoothstep(0.88,0.91,down + 0.10 * (n - 0.5));
    return mix(c,water_foam,foam);
}

vec3 FlatColour(){
    float from_source = vuv.y;
    float t = water_seconds * flow_speed;
    vec2 xz = vposition.xz;
    //A slow noise bends the rings and moves them about, so they never read as circles.
    float warp = vnoise(xz * 0.9 + vec2(t * 0.15,-t * 0.1));
    float ring = fract((from_source + warp * 0.9) * 1.6 - t * 1.6);
    //A thin light line on the leading side of each ring, the swirl lines of the reference.
    float line = smoothstep(0.0,0.06,ring) * smoothstep(0.22,0.10,ring);
    float base = vnoise(xz * 1.7 - vec2(t * 0.3,0.0));
    vec3 c = Shades(0.4 + 0.25 * base + 0.35 * line);

    //Foam where the water came in, torn up by the noise.
    float churn = vnoise(xz * 4.0 + vec2(0.0,t * 2.0));
    c = mix(c,water_foam,smoothstep(1.0,0.2,from_source + 0.4 * churn) * 0.85);

    //The shore - see the note at the top.
    vec2 screen_uv = gl_FragCoord.xy / render_target_size;
    float below = 10.0;
    if (texture(gbuffer_depth,screen_uv).r < 1.0){
        below = vposition.y - texture(gbuffer_position,screen_uv).y;
    }
    if (below >= 0.0){
        float shore = smoothstep(0.14,0.04,below + 0.05 * churn);
        c = mix(c,water_foam,shore * 0.9);
        c = mix(c,water_deep,smoothstep(0.3,1.5,below) * 0.45);
    }
    return c;
}

/*
    A sheet's two edges, ragged and moving with the water: past a noisy line near |across| = 1
    the fragment is not drawn. The geometry is a clean rectangle and this is what stops it
    looking like one. Opaque and cut rather than faded, so nothing has to be sorted.
*/
bool SheetEdgeCut(){
    float t = water_seconds * flow_speed;
    float side = (vuv.x > 0.0) ? 11.0 : 23.0;       //the two edges wobble apart
    float n = 0.7 * vnoise(vec2(side,(vuv.y - t) * 16.0)) + 0.3 * vnoise(vec2(side + 5.0,(vuv.y - t) * 41.0));
    return abs(vuv.x) > 0.66 + 0.34 * n;
}

void main(){
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
#ifdef WATER_SHEET
    if (SheetEdgeCut()){
        discard;        //the one discard, and the reason there are two files - see the top
    }
    vec3 albedo = SheetColour();
#else
    vec3 albedo = FlatColour();
#endif
    SelectMaterial();
    color = vec4(LightSurface(albedo) + albedo * water_fill,1.0);
}
