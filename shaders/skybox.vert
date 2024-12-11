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

//Output
layout (location = 0) out vec3 vposition; 	//Vertex position in world space, used for lighting
layout (location = 1) out vec3 vnormal;		//Normals
layout (location = 2) out vec2 vuv;			//Texture UV coordinates
layout (location = 3) out mat3 TBN;			//Normal mapping matrix
layout (location = 6) flat out int vmatindex;	//Material index
layout (location = 7) flat out int vobjid;	//gl_InstanceID

//Matrix for world camera.
layout(location = 0) uniform mat4 mat_worldcam = mat4(
	1		,0		,0		,0,
	0		,1		,0		,0,
	0		,0		,1		,0,
	0		,0		,0		,1
);

void main(){
	vposition = position;
	vuv = uv;
	gl_Position = (mat_worldcam * vec4(position,1));
}