#version 430 core
//In version 330 core we only have textures or uniform arrays as an arbitrary data input.

//Input variables
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;

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

void main() {
	//Rotation and position in 1
	mat4 mat_trans = mat4(
		1		,0		,0		,0,
		0		,1		,0		,0,
		0		,0		,-1.004		,-1.0,
		0		,0		,5		,5
	);

	//Only object rotation. We want to decompose this from the mat_trans for light calculations
	mat4 mat_rotate = mat4(
		1		,0		,0		,0,
		0		,1		,0		,0,
		0		,0		,1		,0,
		0		,0		,0		,1
	);

	vec4 v = vec4(position,1); //Vertex in
	vec4 transpos = mat_trans * v; //In world space
	vposition = transpos.xyz;

	vec4 vn = vec4(normal,1);
	vnormal = (mat_rotate * vn).xyz;

	vuv = uv;

	gl_Position = (mat_worldcam * transpos) ;

}