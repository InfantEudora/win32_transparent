#include "ApplicationChasm.h"
#include "Calendar.h"
#include "Palette.h"
#include "Shader.h"
#include "Texture.h"
#include <algorithm>

/*
    Winter, drawn (gameplay_plan.md P1). The terrain, the zones and the forest are drawn by
    shaders/chasm_ground.frag, which moves what faces up north of the snow's front onto the
    palette's frozen row (the note at its top says how). View only: the rules read the front from
    Calendar.h and never from here.

    The program is an ordinary solid surface that only works out its own colour, so it asks core
    for everything the default shader got for these meshes:
      f_writes_gbuffer  depth, normals and ids in the G-buffer - the SSAO is measured from it
      f_lit             the sun's shadow matrix and the rest of lighting.glsl's uniforms
      f_casts_shadow    into the sun's shadow map, or every cliff, tree and house loses its shadow
      f_solid           drawn among the solid geometry, before the SSAO is multiplied in
    and the camera cull reaches custom-shader meshes too (Renderer::ComputeViewCull).

    With no snow on the map the shader takes default.frag's own path, and the frame is the frame
    the default shader drew - above the void, where it also darkens the chasm's walls into the dark.

    THE SEASONS (docs/seasons_plan.md, 2026-10-08): the palette texture's TEMPERATE ROW, on the GPU, is
    rewritten as a blend of two season rows - the season before's and this one's, faded over the
    season's first GROUND_SEASON_FADE_DAYS, everywhere at once (the user's: the snow's front is for
    something else). Everything drawn on the temperate row changes colour with it, at no cost per
    pixel; blending in the shader instead sampled the palette twice a fragment and cost about 15% of
    the colour pass. The leaf columns keep summer's, for the crops that borrow them (CropMesh); the
    broadleaves themselves are never on this row - every stage of theirs has a fixed row of its own
    (Seasons.h), so they turn tree by tree. The first spring fades from nothing - there was no winter
    before it, as there was no snow.
*/

#define GROUND_SNOW_EDGE        6.0f    //world units either side of the front the ragged edge reaches
#define GROUND_SNOW_UP          0.55f   //normal.y from which ground and roofs hold snow
#define GROUND_SNOW_UP_FOLIAGE  0.2f    //...and needles, leaves and bushes, which hold it steeper
#define GROUND_SEASON_FADE_DAYS 4.0f    //into each season's colours over its first this many days (tuning)
//Each season's palette row, by SEASON_*: summer is the temperate row itself.
static const int ground_season_rows[CALENDAR_SEASONS] = {PAL_SPRING,PAL_TEMPERATE,PAL_AUTUMN,PAL_WINTER};
/*
    The void (the frag's note): the walls darken from just under the balconies, which keep their
    colour (the islands' tops, at -24, lose about 5%), to black a little above the floor. The mist's
    top rolls about CHASM_MIST_TOP, half way down, so what shows above it is already dimmed.
*/
#define GROUND_VOID_TOP         -18.0f  //world y where the darkening starts
#define GROUND_VOID_ABOVE_FLOOR 10.0f   //black this far above the chasm floor

void ApplicationChasm::BuildGroundShader(){
    //default.vert for the varyings lighting.glsl reads, and for the instance sets' transforms.
    ground_shader = new Shader("shaders/default.vert","shaders/chasm_ground.frag");
    ground_shader->f_lit = true;
    ground_shader->f_writes_gbuffer = true;
    ground_shader->f_casts_shadow = true;
    ground_shader->f_solid = true;
    ground_shader->uniform_callback = std::bind(&ApplicationChasm::SetGroundUniforms,this);
    ground_shader_index = renderer->AddCustomShader(ground_shader);
}

//RENDER THREAD, from the renderer, before the ground's sub-pass.
void ApplicationChasm::SetGroundUniforms(){
    //What the default shader is handed for these meshes and a custom one is not (UploadLighting
    //covers the rest). Missed, alpha_clip stays at lighting.glsl's 1.0 and every pixel's alpha
    //comes out different.
    ground_shader->Setint("f_normal_mapping",(int)renderer->f_normal_mapping);
    ground_shader->Setfloat("alpha_clip",renderer->alpha_clip);
    ground_shader->Setfloat("void_top",GROUND_VOID_TOP);
    ground_shader->Setfloat("void_bottom",terrain_levels[TERRAIN_FLOOR].height + GROUND_VOID_ABOVE_FLOOR);

    //The rock behind every sheet that is drawn and running, wet (the frag's WET ROCK). A frozen fall's is
    //dry, as it raises no foam (UpdateMist).
    float front = SnowFrontNow();
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    vec4 wet_frame[WATER_MAX_SHEETS];
    vec4 wet_span[WATER_MAX_SHEETS];
    int wet = 0;
    if (w && w->water && f_view_water){
        for (const WaterSheetShape& s : w->water->shapes){
            if (wet < WATER_MAX_SHEETS && !CalendarSnowAt(s.origin,front)){
                wet_frame[wet] = vec4(s.origin.x,s.origin.y,s.out.x,s.out.y);
                wet_span[wet] = vec4(s.mid,s.half,s.top_y,s.front);
                wet++;
            }
        }
    }
    ground_shader->Setint("wet_count",wet);
    if (wet > 0){
        ground_shader->Setvec4v("wet_frame",wet_frame,wet);
        ground_shader->Setvec4v("wet_span",wet_span,wet);
    }

    //Snow anywhere on the map? Not while the front, at its furthest north bend and with the
    //ragged edge, is still off the north edge - then the shader is default.frag exactly.
    std::shared_ptr<const Grid> g = GetGrid();
    bool f_snow = g && (front + CALENDAR_SNOW_WOBBLE + GROUND_SNOW_EDGE > g->bounds_min.y);
    ground_shader->Setint("f_snow",f_snow ? 1 : 0);
    if (!f_snow){
        return;
    }
    ground_shader->Setint("season_rows_first",PAL_SPRING);
    ground_shader->Setint("season_rows_last",PAL_SUMMER);
    ground_shader->Setfloat("snow_front_z",front);
    ground_shader->Setfloat("snow_wobble",CALENDAR_SNOW_WOBBLE);
    ground_shader->Setfloat("snow_edge",GROUND_SNOW_EDGE);
    ground_shader->Setfloat("snow_up",GROUND_SNOW_UP);
    ground_shader->Setfloat("snow_up_foliage",GROUND_SNOW_UP_FOLIAGE);
    ground_shader->Setvec2("palette_size",vec2((float)PALETTE_COLS,(float)PALETTE_ROWS));
    ground_shader->Setint("frozen_row",PAL_FROZEN);
    ground_shader->Setint("biome_rows",PAL_SWAMP + 1);
    ground_shader->Setint("roof_column",PAL_ROOF);
    ground_shader->Setint("roof_snow_column",PAL_GRASS_0);
    ground_shader->Setint("foliage_first",PAL_PINE_DARK);
    ground_shader->Setint("foliage_last",PAL_LEAF_LIGHT);
    ground_shader->Setint("bush_column",PAL_BUSH);
}

/*
    RENDER THREAD, from the water's uniform callbacks: the rivers, pools and falls freeze north of the same
    front, with the same ragged edge (shaders/chasm_water.glsl, ICE) - nothing at all while no snow is on
    the map.
*/
void ApplicationChasm::SetIceUniforms(Shader* s){
    std::shared_ptr<const Grid> g = GetGrid();
    float front = SnowFrontNow();
    bool f_ice = g && (front + CALENDAR_SNOW_WOBBLE + GROUND_SNOW_EDGE > g->bounds_min.y);
    s->Setint("f_ice",f_ice ? 1 : 0);
    if (!f_ice){
        return;
    }
    s->Setfloat("snow_front_z",front);
    s->Setfloat("snow_wobble",CALENDAR_SNOW_WOBBLE);
    s->Setfloat("snow_edge",GROUND_SNOW_EDGE);
}

//RENDER THREAD. After every SetMeshData, which puts a mesh back to MESH_MODE_NORMAL.
void ApplicationChasm::UseGroundShader(Mesh* mesh){
    if (!mesh || ground_shader_index < 0){
        return;
    }
    mesh->mesh_mode = MESH_MODE_SHADER;
    mesh->custom_shader_index = ground_shader_index;
}

/*
    RENDER THREAD, from PreRender. The temperate row of the palette texture for today's date: this
    season's row faded in from the one before's, eased at both ends, the leaf columns left at summer.
    Rewritten only when the blend moves by a 255th - a few times a second during a fade at normal
    speed, never between them - and in place (Texture::UploadTexture, as InvertRGB does), so the
    texture keeps its name and bindless handle and no material needs touching.
*/
void ApplicationChasm::UpdateSeasonPalette(){
    Texture* p = palette_texture;
    if (!p || !p->img_data || !p->IsResident() || palette_rows.empty()){
        return;
    }
    CalendarDate d = CalendarDateOf(calendar_shown.load());
    int to = ground_season_rows[d.season];
    int from = (d.year == 1 && d.season == SEASON_SPRING) ? to
             : ground_season_rows[(d.season + CALENDAR_SEASONS - 1) % CALENDAR_SEASONS];
    float t = std::min(1.0f,d.season_fraction * (float)CALENDAR_DAYS_PER_SEASON / GROUND_SEASON_FADE_DAYS);
    t = t * t * (3.0f - 2.0f * t);
    uint32_t steps = (uint32_t)(t * 255.0f + 0.5f);
    if (from == to){
        steps = 255;
    }
    uint32_t key = (uint32_t)from | ((uint32_t)to << 8) | (steps << 16);
    if (key == palette_season_key){
        return;
    }
    palette_season_key = key;
    int channels = (p->image_format == GL_RGBA) ? 4 : 3;
    size_t band = (size_t)p->width * (size_t)(p->height / PALETTE_ROWS) * (size_t)channels;
    int cell_w = p->width / PALETTE_COLS;
    const uint8_t* a = &palette_rows[(size_t)from * band];
    const uint8_t* b = &palette_rows[(size_t)to * band];
    const uint8_t* summer = &palette_rows[(size_t)PAL_TEMPERATE * band];
    uint8_t* out = p->img_data + (size_t)PAL_TEMPERATE * band;
    for (size_t i = 0; i < band; i++){
        int column = (int)((i / channels) % (size_t)p->width) / cell_w;
        if (column >= PAL_LEAF_DARK && column <= PAL_LEAF_LIGHT){
            out[i] = summer[i];
        }else{
            out[i] = (uint8_t)(((uint32_t)a[i] * (255 - steps) + (uint32_t)b[i] * steps + 127) / 255);
        }
    }
    p->UploadTexture(p->image_format,p->gl_target);
}
