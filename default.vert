#version 430 core
//In version 330 core we only have textures or uniform arrays as an arbitrary data input.

//Input variables
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;

struct InstanceData{
	mat4 mat_transformscale;
	vec3 position;
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
	0		,0		,-1.004		,-1.0,
	0		,0		,1.2		,1.2
);

//Matrix rotating all incoming vertex data
uniform mat3 obj_rotate = mat3(
		1		,0		,0,
		0		,1		,0,
		0		,0		,1
);

void main() {
	//Rotation and position in 1
	mat3 mat_trans = mat3(
		1		,0		,0,
		0		,1		,0,
		0		,0		,1
	);

	//Only object rotation. We want to decompose this from the mat_trans for light calculations
	mat4 mat_rotate = mat4(
		1		,0		,0		,0,
		0		,1		,0		,0,
		0		,0		,1		,0,
		0		,0		,0		,1
	);

	mat_trans = obj_rotate;


	vec3 transpos = mat_trans * position; //In world space
	transpos +=  instance_data[gl_InstanceID].position;
	vposition = transpos.xyz;

	vec4 vn = vec4(normal,1);
	vnormal = (mat_rotate * vn).xyz;

	vuv = uv;

	gl_Position = (mat_worldcam * vec4(transpos,1)) ;

	//gl_Position = uProjection * uView * vec4(lPosition + instance_data[gl_InstanceID].position, 1.f);

}