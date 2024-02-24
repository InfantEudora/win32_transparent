#version 430 core
//In version 330 core we only have textures or uniform arrays as an arbitrary data input.

//Input variables
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;

struct InstanceData{
	mat4 mat_transformscale;
};

layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

//Output
layout (location = 0) out vec3 vposition; //Vertex position in world space, used for lighting
layout (location = 1) out vec3 vnormal;	//Normals
layout (location = 2) out vec2 vuv;		//Texture UV coordinates

//Matrix for world camera.
uniform mat4 mat_worldcam = mat4(
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

	vuv = uv;

	gl_Position = (mat_worldcam * transpos);
}