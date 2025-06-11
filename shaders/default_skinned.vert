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
layout (location = 5) in ivec3 bones;
layout (location = 6) in vec3 weights;



struct InstanceData{
	mat4 mat_transformscale;
	int material_slot[NUM_MATERIAL_SLOTS];
	int objectid;
	int num_bones; //Number of bones in this unique mesh, same data per instance
	int pad2;
	int pad3;
};

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    float brightness;
    int pad3;
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
};

struct Bone{
	mat4 mat_transformscale;
	mat4 inv_bindmatrix;
};

//A material index comes in from a vertex, which matches a material specified in the OBJ file.
//This matches our material slot, which looks up the global index.
layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 4) buffer BoneDataBuffer{
	Bone bone_data[];
};

//Output
layout (location = 0) out vec3 vposition; 	//Vertex position in world space, used for lighting
layout (location = 1) out vec3 vnormal;		//Normals
layout (location = 2) out vec2 vuv;			//Texture UV coordinates
layout (location = 3) out mat3 TBN;			//Normal mapping matrix

layout (location = 6) flat out int vmatindex;	//Material index
layout (location = 7) flat out int vobjid;	//gl_InstanceID

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

	//HERE
	/*
		An instance mesh with bones has maybe 10 bones. Each instance has the same amount.
		There will be bone_data of bone_count * bone_size * instances.
		So each instance may have different bone data, i.e. can play a different animation on the same mesh.
	*/

	//Compute the bone index in the data list for this instance.
	//A vertex is in the local space of the root node, the skeleton.
	//The bone weights are ...
	int bone_count = instance_data[gl_InstanceID].num_bones;
	mat4 skin_matrix =
        weights.x * bone_data[(gl_InstanceID * bone_count) + bones.x].mat_transformscale * bone_data[(gl_InstanceID * bone_count) + bones.x].inv_bindmatrix +
        weights.y * bone_data[(gl_InstanceID * bone_count) + bones.y].mat_transformscale * bone_data[(gl_InstanceID * bone_count) + bones.y].inv_bindmatrix +
        weights.z * bone_data[(gl_InstanceID * bone_count) + bones.z].mat_transformscale * bone_data[(gl_InstanceID * bone_count) + bones.z].inv_bindmatrix;

	//Global transform of the current vertex
	mat4 joint_matrix = skin_matrix;

	//mat4 global_transform_matrix = bone_data[(gl_InstanceID * bone_count) + bones.x].mat_transformscale;

	mat4 final_transform_matrix = /*global_transform_matrix * */joint_matrix;

	vec4 world_position = final_transform_matrix * vec4(position,1) ;

	//Calculated the TBN matrix for normal mapping..
	//TODO: Maybe this can be done in a Geometry Shader.
	vuv = uv;
	//vtangent = mat_rotate * tangent;

	vobjid = instance_data[gl_InstanceID].objectid;

	vposition = world_position.xyz;

	gl_Position = (mat_worldcam  * world_position);

	//We need to decompose the matrix into a rotation only, used to compute normals.
	//Zero out the translation and scale.
	mat_rotate[0] = final_transform_matrix[0].xyz;
	mat_rotate[1] = final_transform_matrix[1].xyz;
	mat_rotate[2] = final_transform_matrix[2].xyz;


	//vec3 objpos = final_transform_matrix[3].xyz;

	//This may need to happen only on bone matrix
	//vec4 transpos = instance_data[gl_InstanceID].mat_transformscale * vec4(position,1); //In world space
	//vposition = transpos.xyz;

	vnormal = (mat_rotate * normal);
	vnormal = normalize(vnormal);

	int matindex_out = instance_data[gl_InstanceID].material_slot[matindex];
	vmatindex = matindex_out;

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
}