#version 430 core
//The engine-wide texture unit map - every layout(binding = ...) below names an entry in
//it rather than a number of its own. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"


//#version 460 core
//#extension GL_ARB_bindless_texture : require

//We get info from the deferred stage. When the fragment has full MSAA coverage, it's in the GBUFFEr.
//Else, it's in a sperate buffer and we have to run code here. The edges cannot be looked up in the gbuffer.

//When rendering to multiple color targets
layout (location = 0) out vec4 color;

//gl_Position = fragment position from camera view
//in vec4 gl_FragCoord;  contains the window relative coordinate (x, y, z, 1/w)
    //The z component is the depth value that would be used for the fragment's depth if no shader contained any writes to gl_FragDepth.
//gl_FragDepth

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;       //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;         //Vertex normals
layout (location = 2)  in vec2 vuv;             //Texture UV coordinates
layout (location = 3)  in mat3 TBN;			    //Normal mapping matrix

layout (location = 6)  flat in int vmatindex;   //Material index
layout (location = 7)  flat in int vobjid;      //ObjectID from vertex shader

layout (location = 8) in vec4 vshadow;    //This vertex' position as seen from sun light source

//The materials, the lights, the three shadow lookups and the light loop. A file of its own so a
//custom shader can light a surface of its own colour exactly the way this one lights a
//material's - see the note at the top of it. It reads the varyings above, so it comes after them.
#include "lighting.glsl"


void main(){
    //UV derivatives while control flow is still uniform - see SampleMaterialTexture.
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
    //Select/Set the current material
    if (f_materialindex_is_color > 0){
       color = vec4(1,1,1,1);
       return;
    }else{
        SelectMaterial();
    }

    vec4 final = CalcPBRLighting();



    //ivec2 mouse_coord = ivec2(data_in[0],data_in[1]);
    //ivec2 frag_coord = ivec2(gl_FragCoord.xy);
    color = final;
    //color = vec4(float(vmatindex) / 8.0, float(m.diffuse_texture) / 32.0, 1, 1);
    //return;

    //This is quite slow.
    /*
    float dist = length(frag_coord - mouse_coord);
    if (dist < 4){
        color = vec4(1,0,0,1);

        //We do another Z-Test
        //if ((mouse_coord.x == frag_coord.x) && (mouse_coord.y == frag_coord.y)){ // && (gl_SampleID == (gl_NumSamples-1))){
            //Z-Value 0 ... 1
            float z = gl_FragCoord.z;
            //if (fdata_out[0] > z){
                data_out[0] = vobjid;

                //atomicCounterIncrement(zcount);

                //data_out[1] += 1;//gl_NumSamples;

                fdata_out[0] = z;
                fdata_out[1] = sampled_normal.x;
                fdata_out[2] = sampled_normal.y;
                fdata_out[3] = sampled_normal.z;
            //}
        //}
    }*/


    //uint m = (1 << gl_SampleID);
    //m = gl_SampleMaskIn[0] & m;

    //gl_SampleMaskIn[0]
    //gl_NumSamples
    //gl_SampleID


}