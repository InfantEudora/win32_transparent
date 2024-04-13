#version 460 core
//In version 330 core we only have textures or uniform arrays as an arbitrary data input.

//Multiple of 4 for padding
#define NUM_MATERIAL_SLOTS  4

//Input variables
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 4) in int matindex;


struct InstanceData{
	mat4 mat_transformscale;
	int material_slot[NUM_MATERIAL_SLOTS];
	int objectid;
	int pad1;
	int pad2;
	int pad3;
};

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    int pad2;
    int pad3;
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
};

//A material index comes in from a vertex, which matches a material specified in the OBJ file.
//This matches our material slot, which looks up the global index.
layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

//Output
layout (location = 0) out vec3 vposition; 	//Vertex position in world space, used for lighting
layout (location = 1) out vec3 vnormal;		//Normals
layout (location = 2) out vec3 vtangent;	//For normalmapping
layout (location = 3) out vec2 vuv;			//Texture UV coordinates
layout (location = 4) out mat3 TBN;			//Normal mapping matrix

layout (location = 7) flat out int vmatindex;	//Material index
layout (location = 8) flat out int vobjid;	//gl_InstanceID

//Settings
uniform int f_normal_mapping = 1;

//Matrix for world camera.
layout(location = 0) uniform mat4 mat_worldcam = mat4(
	1		,0		,0		,0,
	0		,1		,0		,0,
	0		,0		,1		,0,
	0		,0		,0		,1
);

void main(){
	//Only object rotation. We want to decompose this from the mat_trans for light calculations
	mat3 mat_rotate;

	//We need to decompose the matrix into a rotation only, used to compute normals.
	//Zero out the translation and scale.
	mat_rotate[0] = instance_data[gl_InstanceID].mat_transformscale[0].xyz;
	mat_rotate[1] = instance_data[gl_InstanceID].mat_transformscale[1].xyz;
	mat_rotate[2] = instance_data[gl_InstanceID].mat_transformscale[2].xyz;

	vec3 objpos = instance_data[gl_InstanceID].mat_transformscale[3].xyz;

	vec4 transpos = instance_data[gl_InstanceID].mat_transformscale * vec4(position,1); //In world space
	vposition = transpos.xyz;

	vnormal = (mat_rotate * normal);

	int matindex_out = instance_data[gl_InstanceID].material_slot[matindex];

	Material m = materials[matindex_out];
	if ((f_normal_mapping == 1) && (m.normal_texture >= 0)){
		vec3 T = vtangent;
		vec3 B = normalize(cross(vnormal, T));
		TBN = mat3(T, B, vnormal);
	}else{
		TBN = mat3(
			1		,0		,0,
			0		,1		,0,
			0		,0		,1
		);
	}

	//Calculated the TBN matrix for normal mapping..
	//TODO: Maybe this can be done in a Geometry Shader.

	vmatindex = matindex_out;

	vuv = uv;
	vtangent = mat_rotate * tangent;

	vobjid = instance_data[gl_InstanceID].objectid;

	gl_Position = (mat_worldcam * transpos);
}