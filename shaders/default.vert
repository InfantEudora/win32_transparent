#version 460 core
//In version 330 core we only have textures or uniform arrays as an arbitrary data input.

//Multiple of 4 for padding
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS	4

//Input variables
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 4) in int matindex;


struct InstanceData{
	mat4 mat_transformscale;
	int material_slot[NUM_MATERIAL_SLOTS];
	float morph_factors[MAX_MORPH_TARGETS];
	int objectid;
	int num_bones;
	int vertex_count;
	int num_morph_targets;
};

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

struct morph_vertex{
	vec3 position;
	float pad1;
	vec3 normal;
	float pad2;
};

//If would be nice, if we could fetch all data per instance for different mesh data layouts.
//Ie, one mesh would have 1 morph target, another maybe 8.
//One can be a skinned mesh,

//A material index comes in from a vertex, which matches a material specified in the OBJ file.
//This matches our material slot, which looks up the global index.
layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 5) buffer MorphBuffer{
	morph_vertex morph_vertices[];
};

//Output
layout (location = 0) out vec3 vposition; 	//Vertex position in world space, used for lighting
layout (location = 1) out vec3 vnormal;		//Normals
layout (location = 2) out vec2 vuv;			//Texture UV coordinates
layout (location = 3) out mat3 TBN;			//Normal mapping matrix

layout (location = 6) flat out int vmatindex;	//Material index
layout (location = 7) flat out int vobjid;	// based on gl_InstanceID

//We reserve n locations for shadow space
layout (location = 8) out vec4 vshadow;		//Vertex position in shadow coordinates for first shadowcaster

layout (location = 9) flat out int vmatselect;	// gl_InstanceID


//Settings
uniform int f_normal_mapping = 1;

//Matrix for world camera.
layout(location = 0) uniform mat4 mat_worldcam = mat4(
	1		,0		,0		,0,
	0		,1		,0		,0,
	0		,0		,1		,0,
	0		,0		,0		,1
);

 //Matrix for single shadow caster
layout(location = 1) uniform mat4 mat_shadow = mat4(
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

	//Calculate position when using morph targets
	vec3 pos = position;
	int voffset = instance_data[gl_InstanceID].vertex_count;
	for (int i=0;i<instance_data[gl_InstanceID].num_morph_targets;i++){
		pos += (morph_vertices[(i*voffset) + gl_VertexID].position * instance_data[gl_InstanceID].morph_factors[i]);
	}

	vec4 transpos = instance_data[gl_InstanceID].mat_transformscale * vec4(pos,1); //In world space
	vposition = transpos.xyz;

	vnormal = (mat_rotate * normal);
	vnormal = normalize(vnormal);
	vshadow = mat_shadow * transpos; //Vertex postition in shadow coordinates

	int matindex_out = instance_data[gl_InstanceID].material_slot[matindex];

	Material m = materials[matindex_out];
	if ((f_normal_mapping == 1) && (m.normal_texture >= 0)){
		vec3 T = tangent;
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
	//vtangent = mat_rotate * tangent;

	vobjid = instance_data[gl_InstanceID].objectid;
	vmatselect = m.diffuse_texture;

	gl_Position = (mat_worldcam * transpos);
}