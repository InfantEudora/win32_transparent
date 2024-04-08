#version 460 core

#extension GL_ARB_bindless_texture : require

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
layout (location = 3)  in vec3 vtangent;        //Tangent to normals
layout (location = 4)  flat in int vmatindex;   //Material index
layout (location = 5)  flat in int vobjid;      //ObjectID from vertex shader

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    int pad2;
    int pad3;
    sampler2D handle_diffuse;
    sampler2D handle_normal;
};

#define PI 	3.14159265359

float metallic = 0.5f;
float roughness = 0.5f;
uniform vec3 eye_position  = vec3(0.0,0.5,8.0);

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 2) buffer ReadbackBuffer{
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
    vec3 normal = texture(materials[vmatindex].handle_normal,vuv).rgb;

    normal = (2.0 * normal) - 1.0;
    return normal;
}

//Returns the light intensity from a single directional light
vec3 CalcDirectionalPBRLight(vec3 lightpos, vec3 color, float brightness){
    Material m = materials[vmatindex];

    vec3 albedo;
    if (m.diffuse_texture >= 0){
        //albedo = texture(material_texture[m.texture_unit], vuv).xyz * m.color.xyz;
        albedo = texture(m.handle_diffuse,vuv).xyz;// * m.color.xyz;
        //albedo = texture(m.handle_normal,vuv).xyz;// * m.color.xyz;
        //albedo = m.color.xyz;
    }else{
        albedo = m.color.xyz;
    }

    vec3 N;
    vec3 V;
    vec3 L;
    if (m.normal_texture >= 0){
        vec3 T = vtangent;
        vec3 B = normalize(cross(vnormal, T));
        mat3 TBN = mat3(T, B, vnormal);

        N = GetNormalMapNormal();
        N = normalize(TBN * N);

        mat3 iTBN = transpose(TBN);

        V = normalize(iTBN * (eye_position - vposition));
        L = normalize(iTBN * (lightpos - vposition));

    }else{
        N = vnormal;
        V = normalize(eye_position - vposition);
        L = normalize(lightpos - vposition);
    }

    vec3 F0 = vec3(0.04); //Fresnell factor
    F0 = mix(F0, albedo, metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);

    // calculate per-light radiance
    //vec3 L = normalize(lightpos - vposition);
    vec3 H = normalize(V + L);

    vec3 radiance     = color * brightness;

    // cook-torrance brdf
    float NDF = DistributionGGX(N, H, roughness + 0.001); //Some base roughness to prevent /0
    float G   = GeometrySmith(N, V, L, roughness + 0.001);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular     = numerator / denominator;

    // add to outgoing radiance Lo
    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    //Ambient
    Lo += 0.10 * albedo;
    return Lo;
}

float GetTransparency(){
    Material m = materials[vmatindex];
    if (m.diffuse_texture >= 0){
        return texture(m.handle_diffuse,vuv).w;
        //return texture(material_texture[m.texture_unit], vuv).w;
    }
    return m.color.w;
}

vec4 CalcPBRLighting(){
    vec4 final;

    vec3 light = CalcDirectionalPBRLight(vec3(-10,10,10),vec3(1,1,1),5.0);

    float alpha = GetTransparency();
    final = vec4(light,alpha);

    return final;
}

void main(){
    vec4 final = CalcPBRLighting();

    ivec2 mouse_coord = ivec2(data_in[0],data_in[1]);
    ivec2 frag_coord = ivec2(gl_FragCoord.xy);
    color = final;

    float dist = length(frag_coord - mouse_coord);
    if (dist < 5){
        color = vec4(1,0,0,1);
    }

    uint m = (1 << gl_SampleID);
    m = gl_SampleMaskIn[0] & m;

    //gl_SampleMaskIn[0]
    //gl_NumSamples
    //gl_SampleID

    //We do another Z-Test
    if ((mouse_coord.x == frag_coord.x) && (mouse_coord.y == frag_coord.y) && (gl_SampleID == (gl_NumSamples-1))){
        //Z-Value 0 ... 1
        float z = gl_FragCoord.z;
        if (fdata_out[0] > z){
            data_out[0] = vobjid;

            data_out[1] = gl_NumSamples;

            fdata_out[0] = z;
        }
    }
}