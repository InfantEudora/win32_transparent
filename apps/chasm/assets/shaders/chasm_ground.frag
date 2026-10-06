#version 430 core
/*
    Chasm's ground - the terrain, the zones and the forest - under WINTER's snow (gameplay_plan.md
    P1, Calendar.h). ApplicationChasmSnow.cpp puts those meshes on this program.

    Everything Chasm draws takes its colour from one palette cell (Palette.h): a ROW per biome, a
    COLUMN per material, and the FROZEN row is a winter copy of every column. So snow is a row
    change: north of the front, on what faces up, the fragment samples its own column on the frozen
    row instead of its own row. Walls, trunks and anything steep keep their colour, which is what
    makes the snow read as lying on things. Lit through lighting.glsl exactly as default.frag lights
    a material.

    WITH NO SNOW THIS IS default.frag, line for line, above the void: f_snow is 0 and main() takes
    the same path, so the frame is the same frame (measured by pixel diff, ApplicationChasmSnow.cpp).

    THE VOID (2026-10-06): below void_top everything darkens toward black, black at void_bottom just
    above the chasm floor (which is drawn black, PAL_VOID). The walls run down into the dark instead
    of to a floor, and the mist, drawn by another shader, stays light against them.

    THE FRONT is the rules' (Calendar.cpp): snow where z < snow_front_z - wobble(x), north being -z.
    SnowWobble below is CalendarSnowWobble term for term - change one, change both. The picture
    adds a ragged edge of world-space noise either side of it; the rules use the plain front.
*/
#include "texture_units.glsl"

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

uniform int   f_snow = 0;                   //0: no snow anywhere on the map - default.frag's path
uniform float snow_front_z = -1.0e9;        //the front's mean line (CalendarSnowFrontZ)
uniform float snow_wobble = 14.0;           //CALENDAR_SNOW_WOBBLE
uniform float snow_edge = 6.0;              //how far either side of the front the ragged edge reaches
uniform float snow_up = 0.55;               //normal.y from which ground and roofs hold snow
uniform float snow_up_foliage = 0.2;        //...and needles and leaves, which hold it on steeper faces
//The palette's layout (Palette.h), handed over rather than copied.
uniform vec2  palette_size = vec2(32.0,16.0);
uniform int   frozen_row = 2;
uniform int   biome_rows = 4;               //rows below this are biomes; the rest are not frozen
uniform int   roof_column = 30;             //a frozen roof is slate; snow on one is...
uniform int   roof_snow_column = 0;         //...the frozen row's grass, white
uniform int   foliage_first = 16;           //pines and leaves, these columns inclusive
uniform int   foliage_last = 21;
uniform int   bush_column = 27;
//The void: full colour above void_top, black at void_bottom (world y).
uniform float void_top = -1.0e9;
uniform float void_bottom = -1.0e9;

float SnowWobble(float x){
    return snow_wobble * (0.7 * sin(x * 0.0245 + 1.3) + 0.3 * sin(x * 0.0610 + 0.4));
}

float SnowHash(vec2 p){
    return fract(sin(dot(p,vec2(127.1,311.7))) * 43758.5453);
}

float SnowNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(SnowHash(i),SnowHash(i + vec2(1.0,0.0)),u.x),
               mix(SnowHash(i + vec2(0.0,1.0)),SnowHash(i + vec2(1.0,1.0)),u.x),u.y);
}

//The palette cell this fragment's colour comes from, moved onto the frozen row if snow lies on it.
vec2 SnowUV(){
    ivec2 cell = ivec2(floor(vuv * palette_size));
    if (cell.y >= biome_rows){
        return vuv;
    }
    //North of the front, give or take the ragged edge: two octaves, the long one shaping bays
    //and tongues, the short one fraying them.
    float north = snow_front_z - SnowWobble(vposition.x) - vposition.z;
    float rag = (SnowNoise(vposition.xz * 0.08) - 0.5) * 1.6 + (SnowNoise(vposition.xz * 0.5) - 0.5) * 0.6;
    if (north + rag * snow_edge <= 0.0){
        return vuv;
    }
    bool f_foliage = (cell.x >= foliage_first && cell.x <= foliage_last) || cell.x == bush_column;
    if (normalize(vnormal).y <= (f_foliage ? snow_up_foliage : snow_up)){
        return vuv;
    }
    int column = (cell.x == roof_column) ? roof_snow_column : cell.x;
    return (vec2(float(column),float(frozen_row)) + 0.5) / palette_size;
}

void main(){
    //UV derivatives while control flow is still uniform - see SampleMaterialTexture.
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
    if (f_materialindex_is_color > 0){
       color = vec4(1,1,1,1);
       return;
    }
    SelectMaterial();
    //No snow on the map, or not a palette material: exactly default.frag.
    if (f_snow == 0 || m.diffuse_texture < 0){
        color = CalcPBRLighting();
    }else{
        //CalcPBRLighting with the cell moved. Nearest filtering and no mipmaps (Palette.h), so the
        //unmoved uv's derivatives serve for the moved one.
        vec3 albedo = SampleMaterialTexture(m.diffuse_texture,SnowUV()).rgb;
        float alpha = 1 - step(GetTransparency(),alpha_clip);
        if (m.f_unlit != 0){
            color = vec4(albedo + m.emissive.rgb * m.emissive.w,alpha);
        }else{
            color = vec4(LightSurface(albedo),alpha);
        }
    }
    //Into the void. Eased at both ends, so the walls neither start darkening at a line nor reach
    //black at one.
    if (vposition.y < void_top){
        color.rgb *= smoothstep(void_bottom,void_top,vposition.y);
    }
}
