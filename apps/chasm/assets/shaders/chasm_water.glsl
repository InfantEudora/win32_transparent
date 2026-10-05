#include "texture_units.glsl"
/*
    Chasm's water - the rivers and the falls (WaterMesh.h, grid_plan.md step 9). Adapted from
    archer's (apps/archer/assets/shaders/archer_water.glsl), whose notes hold for this one: flat
    shades that move and nothing else, LIT like any other surface (lighting.glsl) so a river in a
    house's shadow is shaded; two programs from this one body because only the falls discard, and a
    program that can discard loses the early depth test for every fragment.

      chasm_water_sheet.frag  the FALLS - uv.x across (-1..1), uv.y down (0 at the lip, 1 where the
         sheet meets the mist). Long streaks running down, light where it goes over the edge, white
         where it reaches the mist, ragged edges.
      chasm_water_flat.frag   the RIVERS - uv.x across (-1..1 at the water's edges), uv.y distance
         from the source in world units. Streaks drifting downstream, and a SHORE from the G-buffer:
         the water is left out of the deferred pass, so under every water fragment the G-buffer
         still holds the river bed, and where that is close below the surface there is foam -
         wherever ground meets water, measured nowhere on the CPU.

    The clock is the simulation's (water_seconds = tick * step), so a paused game holds it still.
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
//A Little Age's blues: saturated, a step lighter than deep. The mid shade is the palette's water.
uniform vec3  water_light = vec3(0.40,0.62,0.92);
uniform vec3  water_mid   = vec3(0.22,0.41,0.81);
uniform vec3  water_deep  = vec3(0.15,0.31,0.66);
uniform vec3  water_foam  = vec3(0.90,0.95,0.98);
//Down a fall in sheet lengths a second; along a river in world units a second.
uniform float flow_speed = 0.3;
//Self-lit share of its own colour, so water in shade still reads as water.
uniform float water_fill = 0.10;
/*
    HAZE lying on the water (the swamp's pools; biomes_plan.md): soft patches of a pale colour drifting
    slowly across the surface, in two flat steps like the water's own shades. Mist on standing water
    has to be seen through, which opaque puffs over it never were - they read as stones and slabs. 0,
    the default, for the rivers.
*/
uniform float haze_amount = 0.0;
uniform vec3  haze_colour = vec3(0.77,0.80,0.75);

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

//Three flat shades out of one 0..1 value, with a narrow soft step between them.
vec3 Shades(float n){
    vec3 c = mix(water_deep,water_mid,smoothstep(0.30,0.36,n));
    return mix(c,water_light,smoothstep(0.64,0.70,n));
}

vec3 SheetColour(){
    float across = vuv.x;
    float down = vuv.y;
    float t = water_seconds * flow_speed;
    //A fall is some fifty units tall, so its streaks are many to a sheet.
    float n = 0.65 * vnoise(vec2(across * 3.0,(down - t) * 9.0))
            + 0.35 * vnoise(vec2(across * 8.0 + 3.1,(down - t * 1.3) * 22.0));
    vec3 c = Shades(n);
    c = mix(c,water_deep,smoothstep(0.75,1.0,abs(across)) * 0.6);
    //Light where it rolls over the edge.
    c = mix(c,water_light,smoothstep(0.035,0.005,down));
    //White as it nears the mist, broken by the streaks so the band's top is not ruled.
    float foam = smoothstep(0.86,0.92,down + 0.06 * (n - 0.5));
    return mix(c,water_foam,foam);
}

vec3 FlatColour(){
    float across = vuv.x;
    float along = vuv.y;
    float t = water_seconds * flow_speed;
    //Streaks long along the river, drifting down it; a slow warp so they do not run in rails.
    float warp = vnoise(vec2(along * 0.05,across * 0.7) + vec2(t * 0.02,0.0));
    float n = 0.6 * vnoise(vec2(across * 2.2 + warp * 1.5,(along - t) * 0.22))
            + 0.4 * vnoise(vec2(across * 5.0 + 7.0,(along - t * 1.25) * 0.6));
    vec3 c = Shades(0.18 + 0.75 * n);
    if (haze_amount > 0.0){
        vec2 xz = vposition.xz;
        float ht = water_seconds;
        float h = 0.65 * vnoise(xz * 0.11 + vec2(ht * 0.045,ht * 0.02))
                + 0.35 * vnoise(xz * 0.29 - vec2(ht * 0.03,-ht * 0.015));
        float haze = 0.55 * smoothstep(0.56,0.60,h) + 0.45 * smoothstep(0.70,0.74,h);
        c = mix(c,haze_colour,haze * haze_amount);
    }

    //The shore - see the note at the top.
    vec2 screen_uv = gl_FragCoord.xy / render_target_size;
    float below = 10.0;
    if (texture(gbuffer_depth,screen_uv).r < 1.0){
        below = vposition.y - texture(gbuffer_position,screen_uv).y;
    }
    if (below >= 0.0){
        float churn = vnoise(vposition.xz * 1.6 + vec2(t * 0.4,0.0));
        float shore = smoothstep(0.16,0.05,below + 0.06 * churn);
        c = mix(c,water_foam,shore * 0.9);
    }
    return c;
}

//A fall's two edges, ragged and moving with the water.
bool SheetEdgeCut(){
    float t = water_seconds * flow_speed;
    float side = (vuv.x > 0.0) ? 11.0 : 23.0;
    float n = 0.7 * vnoise(vec2(side,(vuv.y - t) * 40.0)) + 0.3 * vnoise(vec2(side + 5.0,(vuv.y - t) * 100.0));
    return abs(vuv.x) > 0.72 + 0.28 * n;
}

void main(){
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
#ifdef WATER_SHEET
    if (SheetEdgeCut()){
        discard;
    }
    vec3 albedo = SheetColour();
#else
    vec3 albedo = FlatColour();
#endif
    SelectMaterial();
    color = vec4(LightSurface(albedo) + albedo * water_fill,1.0);
}
