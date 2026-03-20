#version 430 core

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




//It's set with glBindTextureUnit

layout (binding = 0) uniform sampler2D material_texture[24];   //Input texture
//layout (binding = 1) uniform sampler2D shadow_texture;

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    float brightness;
    float metallic;
    float roughness;
    int pad2;
    int pad3;
    int pad4;
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
};

//All the light types fall together into a single light struct
struct Light{
    vec3    position;
    int     shadow;     // Set if the the light produces a shadow
    vec3    direction;	// Direction of 0 means its a point light
    float   brightness;
    vec3    color;
    float   cos_angle; 	// 0 means its a point light, else it becomes a cone light
};
//Multiple of 4 for padding
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS	4
struct InstanceData{
	mat4 mat_transformscale;
	int material_slot[NUM_MATERIAL_SLOTS];
	float morph_factors[MAX_MORPH_TARGETS];
	int objectid;
	int num_bones;
	int vertex_count;
	int num_morph_targets;
};

#define PI 	3.14159265359

uniform vec3 eye_position;
uniform int f_normal_mapping = 1;
uniform int f_materialindex_is_color = 0;
uniform float alpha_clip = 1.0f;

//This gets set when lighting calculation is done, and is this fragments resulting normal.
vec3 sampled_normal = vec3(0,0,0);

//A material index comes in from a vertex, which matches a material specified in the OBJ file.
//This matches our material slot, which looks up the global index.
layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 2) buffer LightBuffer{
	Light lights[];
};

//The material we pick from buffer, or set our selves
Material m;

layout (std430, binding = 3) buffer ReadbackBuffer{
	int data_in[4];
    int data_out[4];
    float fdata_out[4];
};

float DistributionGGX(vec3 N, vec3 H, float a){
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom    = a2;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom        = PI * denom * denom;
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float k){
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float k){
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0){
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 GetNormalMapNormal(){
    //Bindless
    //vec3 normal = texture(m.handle_normal,vuv).rgb;
    //Default
    vec3 normal = texture(material_texture[m.normal_texture],vuv).rgb;

    normal = (2.0 * normal) - 1.0;
    return normalize(normal);
}

//Returns the light intensity from a single directional light such as the sun
vec3 CalcDirectionalPBRLight(vec3 albedo, vec3 lightdirection, vec3 color, float brightness){

    vec3 N;
    vec3 V;
    vec3 L;
    if ((f_normal_mapping == 1) && (m.normal_texture >= 0)){
        N = GetNormalMapNormal();
        mat3 iTBN = transpose(TBN);
        V = normalize(iTBN * (eye_position- vposition));
        L = normalize(iTBN * (lightdirection));
    }else{
        N = normalize(vnormal);
        V = normalize(eye_position - vposition);
        L = normalize(lightdirection);
    }

    sampled_normal = normalize(vnormal);

    vec3 F0 = vec3(0.04); //Fresnell factor
    F0 = mix(F0, albedo, m.metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);

    // calculate per-light radiance
    vec3 H = normalize(V + L);

    vec3 radiance     = color * brightness;

    // cook-torrance brdf
    float NDF = DistributionGGX(N, H, m.roughness + 0.0001); //Some base roughness to prevent /0
    float G   = GeometrySmith(N, V, L, m.roughness + 0.0001);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - m.metallic;

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    // add to outgoing radiance Lo
    float NdotL = max(dot(N, L), 0.0);
    //Use the original object normal to completely shadow back faces
    //NdotL = min(dot(vnormal, normalize(lightpos - vposition)),NdotL );

    Lo += (kD * albedo / PI + specular) * radiance * NdotL * m.brightness;
    return Lo;
}

float GetTransparency(){
    if (m.diffuse_texture >= 0){
        //Bindless
        //return texture(m.handle_diffuse,vuv).w;
        //Default
        return texture(material_texture[m.diffuse_texture], vuv).w;
    }
    return m.color.w;
}


//Compute shadow from sun shadowmap
float CalcShadow(vec4 vposinshadow){
    //Linearise
    vec3 pos_proj = vposinshadow.xyz / vposinshadow.w;

    //Map to UV coordinates
    vec2 uvshadow;
	uvshadow.x 		= (0.5 * pos_proj.x) + (0.5);
    uvshadow.y 		= (0.5 * pos_proj.y) + (0.5);

    //Lookup this fragment's associated depth value from the lights point of view.
    float closest_depth = texture(material_texture[0], uvshadow).r;
    float current_depth = (0.5 * pos_proj.z) + (0.5);
    float bias = 0.00025;

    float shadow = (current_depth - bias) > closest_depth  ? 0.2 : 1.0;
    return shadow;
/*

    vec3 sunpos = sun.position;
    //vec3 lightvec = sunpos - vposition; //This would be the light vector if the sun had a perspective camera.
    vec3 lightvec = sunpos;
    vec3 nlightvec = normalize(lightvec);

    //float bias = max(0.05 * (1.0 - dot(vnormal, nlightvec)), shadow_bias);
    float bias = max(shadow_bias * (1.0 - dot(vnormal, nlightvec)), shadow_bias/32.0);
    //bias = shadow_bias;
    // check whether current frag pos is in shadow
    //float shadow = (current_depth - bias) - closest_depth;

    //if (shadow < 0){
    //    return 1;
    //}
    //shadow = clamp(shadow,0,1);
    float shadow = (current_depth - bias) > closest_depth  ? 0.0 : 1.0;
    //float shadow =
    return shadow;
*/
}


vec4 CalcPBRLighting(){
    vec4 final;

    vec3 total_light = vec3(0,0,0);
    vec3 albedo;
    if (m.diffuse_texture >= 0){
        //Bindless
        //albedo = texture(m.handle_diffuse,vuv).xyz;// * m.color.xyz;
        //albedo = texture(m.handle_normal,vuv).xyz;// * m.color.xyz;
        //Default
        albedo = texture(material_texture[m.diffuse_texture], vuv).rgb;
    }else{
        albedo = m.color.xyz;
    }

    for (int i = 0; i < lights.length(); i++){
        vec3 lightdirection = lights[i].position;
        vec3 light = lightdirection;
        float direction_len = dot(lights[i].direction,lights[i].direction);
        float falloff = 1.0f;
        float light_value = 1.0;

        if (direction_len < 0.1){
            //Makes it a point light instead of direction
            lightdirection = lights[i].position - vposition;

            float dist  = length(lights[i].position - vposition);
            float brightness = lights[i].brightness / pow(dist,falloff);

            if (brightness < 0.01){
                continue;
            }
            light_value = brightness;
        }else{
            float shadow = CalcShadow(vshadow);
            light_value = shadow;
        }

        light = light_value * CalcDirectionalPBRLight(albedo,lightdirection,lights[i].color,lights[i].brightness);
        total_light += light;
    }

    //Add some ambient
    total_light += 0.1f * albedo;


    //total_light *= 0.51f;
    //total_light += vshadow.xyz;


    float alpha = 1 - step(GetTransparency(),alpha_clip);
    //if (alpha < alpha_clip){
    //    discard;
    //}
    final = vec4(total_light ,alpha);

    return final;
}

void main(){
    //Select/Set the current material
    if (f_materialindex_is_color > 0){
       color = vec4(1,1,1,1);
       return;
    }else{
        //We have to do this again here. We could use vmatindex... but intel.
        //On intel, use matindex_out.
        //On nvidia, use vmatindex.

        if (vmatindex > -1){

            m = materials[vmatindex];
        }else{
            //Default invalid material
            m.diffuse_texture = -1;
            m.normal_texture = -1;
            m.color = vec4(0.9,0.0,0.5,1.0);
        }
    }

    vec4 final = CalcPBRLighting();



    //ivec2 mouse_coord = ivec2(data_in[0],data_in[1]);
    //ivec2 frag_coord = ivec2(gl_FragCoord.xy);
    color = final;

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