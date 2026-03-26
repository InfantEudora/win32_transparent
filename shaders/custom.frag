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

uniform float alpha_clip = 1.0f;

uniform float circle_start = 0.0f;
uniform float circle_end = 1.0f;

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

void main(){
    // Convert from range [0,1] to [-1,1]
    vec2 uv = 2.0 * vuv - 1.0;

    float softness = 0.01;
    float thickness = 0.2;
    float size = 0.8;

    // Donut shape
    float dist  = length(uv);
    float donut = smoothstep(size, size + softness, dist) * (1.0 - smoothstep(size + thickness - softness, size + thickness, dist));

    // Arc segment using atan2, range [-PI, PI].
    // Remap so circle_start sits at 0, then check if the fragment falls within the span.
    float angle  = atan(uv.y, uv.x);
    float offset = mod(angle - circle_start, 2.0 * PI);
    float span   = mod(circle_end - circle_start, 2.0 * PI);
    // span==0 means start==end (mod 2PI); treat as full circle.
    float arc    = (span < 0.0001)
                    ? 1.0
                    : smoothstep(0.0, softness, offset) * (1.0 - smoothstep(span - softness, span, offset));

    float circle = donut * arc;
    vec3 rgb = vec3(1.0, 0.4, 0.1) * circle;
    color = vec4(rgb, circle);
}