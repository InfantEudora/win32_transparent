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
/*
    ICE (the user's, 2026-10-08): north of winter's snow front the water FREEZES - the rivers, and their
    falls with them, so no frozen river pours over the rim. The front and its ragged edge are the
    ground's snow's, formula for formula (chasm_ground.frag's SnowLies), so the ice ends where the snow
    on its banks does. Ice is still: flat pale shades of a pattern that does not move, a few dark cracks,
    and snow drifted onto it along the banks. A collector under the snow gives no water (Economy.cpp) -
    this is that rule, seen.
*/
uniform int   f_ice = 0;                    //0: no snow on the map - the water's own path
uniform float snow_front_z = -1.0e9;        //the front's mean line (CalendarSnowFrontZ)
uniform float snow_wobble = 14.0;           //CALENDAR_SNOW_WOBBLE
uniform float snow_edge = 6.0;              //how far either side of the front the ragged edge reaches
uniform vec3  ice_light = vec3(0.84,0.92,0.97);
uniform vec3  ice_mid   = vec3(0.68,0.83,0.93);
uniform vec3  ice_deep  = vec3(0.54,0.71,0.86);
uniform vec3  ice_crack = vec3(0.42,0.58,0.74);
uniform vec3  ice_snow  = vec3(0.94,0.96,0.99);

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
vec3 IceShades(float n){
    vec3 c = mix(ice_deep,ice_mid,smoothstep(0.30,0.36,n));
    return mix(c,ice_light,smoothstep(0.64,0.70,n));
}

//North of the snow's front, give or take its ragged edge - the ground's test (hash21 and vnoise are its
//SnowHash and SnowNoise).
bool Frozen(){
    if (f_ice == 0){
        return false;
    }
    float wobble = snow_wobble * (0.7 * sin(vposition.x * 0.0245 + 1.3) + 0.3 * sin(vposition.x * 0.0610 + 0.4));
    float north = snow_front_z - wobble - vposition.z;
    float rag = (vnoise(vposition.xz * 0.08) - 0.5) * 1.6 + (vnoise(vposition.xz * 0.5) - 0.5) * 0.6;
    return north + rag * snow_edge > 0.0;
}

//How far the river bed lies under this water fragment, from the G-buffer (the note at the top); 10 where
//nothing was drawn there.
float BedBelow(){
    vec2 screen_uv = gl_FragCoord.xy / render_target_size;
    if (texture(gbuffer_depth,screen_uv).r < 1.0){
        return vposition.y - texture(gbuffer_position,screen_uv).y;
    }
    return 10.0;
}

vec2 hash22(vec2 p){
    return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3)))) * 43758.5453);
}

/*
    The ice's PLATES: the nearest of a jittered point per cell (a Voronoi), so the sheet breaks into flat
    polygons - the terrain's own low-poly look, where smooth noise read as a blur. `id` picks the plate's
    shade, `seam` is how far it is to the next plate's edge (cell units, roughly), for the cracks between.
*/
void IcePlates(vec2 p, out float id, out float seam){
    vec2 i = floor(p);
    vec2 f = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    vec2 best = i;
    for (int y = -1; y <= 1; y++){
        for (int x = -1; x <= 1; x++){
            vec2 g = vec2(float(x),float(y));
            vec2 r = g + 0.1 + 0.8 * hash22(i + g) - f;
            float d = dot(r,r);
            if (d < d1){
                d2 = d1;
                d1 = d;
                best = i + g;
            }else if (d < d2){
                d2 = d;
            }
        }
    }
    id = hash21(best);
    seam = sqrt(d2) - sqrt(d1);
}

#define ICE_PLATE_SCALE     0.32    //plates a world unit: about three units across, a house and a half
#define ICE_SEAM            0.07    //crack width, in plate units
//A frozen river or pool: still plates in three pale shades, dark seams between, snow at the banks.
vec3 IceColour(){
    vec2 xz = vposition.xz;
    float id, seam;
    IcePlates(xz * ICE_PLATE_SCALE,id,seam);
    vec3 c = (id < 0.35) ? ice_deep : (id < 0.75) ? ice_mid : ice_light;
    //A few plates with snow blown over them.
    c = mix(c,ice_snow,step(0.92,id) * 0.7);
    float aa = fwidth(seam);
    c = mix(c,ice_crack,(1.0 - smoothstep(ICE_SEAM - aa,ICE_SEAM + aa,seam)) * 0.85);
    //Snow drifted onto the ice where it meets the bank, ragged like the front.
    float below = BedBelow();
    if (below >= 0.0){
        float drift = vnoise(xz * 0.7);
        c = mix(c,ice_snow,smoothstep(0.16,0.10,below + 0.08 * (drift - 0.5)));
    }
    return c;
}

//A fall frozen still: the same long streaks, as icicles down the wall.
vec3 SheetColour(bool f_frozen){
    float across = vuv.x;
    float down = vuv.y;
    float t = f_frozen ? 0.0 : water_seconds * flow_speed;
    //A fall is some fifty units tall, so its streaks are many to a sheet.
    float n = 0.65 * vnoise(vec2(across * 3.0,(down - t) * 9.0))
            + 0.35 * vnoise(vec2(across * 8.0 + 3.1,(down - t * 1.3) * 22.0));
    vec3 deep = f_frozen ? ice_deep : water_deep;
    vec3 light = f_frozen ? ice_light : water_light;
    vec3 c = f_frozen ? IceShades(n) : Shades(n);
    c = mix(c,deep,smoothstep(0.75,1.0,abs(across)) * 0.6);
    //Light where it rolls over the edge.
    c = mix(c,light,smoothstep(0.035,0.005,down));
    //White as it nears the mist, broken by the streaks so the band's top is not ruled.
    float foam = smoothstep(0.86,0.92,down + 0.06 * (n - 0.5));
    return mix(c,f_frozen ? ice_snow : water_foam,foam);
}

vec3 FlatColour(){
    if (Frozen()){
        return IceColour();
    }
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
    float below = BedBelow();
    if (below >= 0.0){
        float churn = vnoise(vposition.xz * 1.6 + vec2(t * 0.4,0.0));
        float shore = smoothstep(0.16,0.05,below + 0.06 * churn);
        c = mix(c,water_foam,shore * 0.9);
    }
    return c;
}

//A fall's two edges, ragged and moving with the water - still, frozen.
bool SheetEdgeCut(bool f_frozen){
    float t = f_frozen ? 0.0 : water_seconds * flow_speed;
    float side = (vuv.x > 0.0) ? 11.0 : 23.0;
    float n = 0.7 * vnoise(vec2(side,(vuv.y - t) * 40.0)) + 0.3 * vnoise(vec2(side + 5.0,(vuv.y - t) * 100.0));
    return abs(vuv.x) > 0.72 + 0.28 * n;
}

void main(){
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
#ifdef WATER_SHEET
    bool f_frozen = Frozen();
    if (SheetEdgeCut(f_frozen)){
        discard;
    }
    vec3 albedo = SheetColour(f_frozen);
#else
    vec3 albedo = FlatColour();
#endif
    SelectMaterial();
    color = vec4(LightSurface(albedo) + albedo * water_fill,1.0);
}
