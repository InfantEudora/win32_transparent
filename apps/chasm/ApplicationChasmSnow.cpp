#include "ApplicationChasm.h"
#include "Calendar.h"
#include "Palette.h"
#include "Shader.h"

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
*/

#define GROUND_SNOW_EDGE        6.0f    //world units either side of the front the ragged edge reaches
#define GROUND_SNOW_UP          0.55f   //normal.y from which ground and roofs hold snow
#define GROUND_SNOW_UP_FOLIAGE  0.2f    //...and needles, leaves and bushes, which hold it steeper
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

    //Snow anywhere on the map? Not while the front, at its furthest north bend and with the
    //ragged edge, is still off the north edge - then the shader is default.frag exactly.
    std::shared_ptr<const Grid> g = GetGrid();
    float front = SnowFrontNow();
    bool f_snow = g && (front + CALENDAR_SNOW_WOBBLE + GROUND_SNOW_EDGE > g->bounds_min.y);
    ground_shader->Setint("f_snow",f_snow ? 1 : 0);
    if (!f_snow){
        return;
    }
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

//RENDER THREAD. After every SetMeshData, which puts a mesh back to MESH_MODE_NORMAL.
void ApplicationChasm::UseGroundShader(Mesh* mesh){
    if (!mesh || ground_shader_index < 0){
        return;
    }
    mesh->mesh_mode = MESH_MODE_SHADER;
    mesh->custom_shader_index = ground_shader_index;
}
