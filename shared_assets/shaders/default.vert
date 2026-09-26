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
    int f_unlit;       //see material_t in core/Material.h; was pad2
    float wind_flex;   //see material_t; was pad3 (and still is, in the mirrors that do not read it)
    int wind_mode;     //see material_t in core/Material.h; was pad4. Only default.vert reads it
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
    //Self-emitted light: xyz is glTF's emissiveFactor, w a strength multiplier. Must stay last
    //to match material_t in Material.h - see the comment there.
    vec4 emissive;
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

//This mesh's own vertex buffer, bound by Mesh::RenderInstances. The integer fields of a vertex
//are read from here by gl_VertexID rather than as vertex attributes - see SSBO_VERTEX_PULL in
//core/Mesh.h for the Intel measurements behind that. One uint per 4-byte field; the stride in
//words is the struct size pinned by the static_asserts in core/Mesh.cpp.
layout (std430, binding = 6) readonly buffer VertexPull{
	uint vertex_words[];
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 5) buffer MorphBuffer{
	morph_vertex morph_vertices[];
};

/*
	THE WIND - Renderer::SetWindField fills it, material_t::wind_flex says who bends. A grid of
	world X/Y velocities with its own mapping in the header, so nothing needs a uniform for it.
	Always bound; enabled (wind_size.z) is 0 until an app sets a field. Mirrors wind_header_t.
*/
layout (std430, binding = 7) readonly buffer WindBuffer{
	vec4  wind_rect;	//x0, y0, 1/cell_x, 1/cell_y
	ivec4 wind_size;	//width, height, enabled, unused
	vec4  wind_params;	//time in seconds, unused x3
	vec2  wind_grid[];
};

//Bilinear, and NO wind outside the grid rather than the edge smeared outward - the app sizes the
//grid to what the camera sees, so outside it is also out of sight.
vec2 SampleWind(vec2 p){
	vec2 g = (p - wind_rect.xy) * wind_rect.zw;
	int w = wind_size.x;
	if ((g.x < 0.0) || (g.y < 0.0) || (g.x >= float(w - 1)) || (g.y >= float(wind_size.y - 1))){
		return vec2(0.0);
	}
	ivec2 i = ivec2(g);
	vec2 t = g - vec2(i);
	int k = i.y * w + i.x;
	return mix(mix(wind_grid[k],wind_grid[k + 1],t.x),mix(wind_grid[k + w],wind_grid[k + w + 1],t.x),t.y);
}

/*
	Bends one vertex of something attached at its origin - see material_t::wind_mode. LEAF is the
	first branch below; the rest of this note is STALK, a plant authored base-down.

	The push grows with the SQUARE of the height above the origin, so the root stays put and the
	tip moves most, which is how a stalk bends; the wind is read at the vertex, so a tall plant
	whose top is in faster air bends more at the top. On top of the bend, a flutter that scales
	with the wind speed and is phased by where the plant stands, so neighbours never sway in step,
	and a smaller sway through the slab so a row of grass is not a row of cut-outs.

	Both modes then SWING rather than shift - see WindSwing.
*/

/*
	Turns a push into a swing about the origin: the part of the push that runs ALONG the line from
	the origin to the vertex is dropped, and the vertex is put back at its old distance. The push
	is in world space - it comes from the wind - so without this, which way the part pointed
	decided what the wind did to it: a vine leaf pointing downwind was pushed along its own length
	and STRETCHED, one pointing upwind was squashed, and only one hanging across the wind swung. A
	fern frond lying along the wind did the same. Now everything rotates about where it is attached,
	and an upright blade's tip comes down on the exact arc.
*/
vec3 WindSwing(vec3 world, vec3 origin, vec3 push){
	vec3 r = world - origin;
	float d = length(r);
	if (d < 1e-5){
		return world;
	}
	vec3 dir = r / d;
	push -= dir * dot(push,dir);
	return origin + normalize(r + push) * d;
}

vec3 WindBend(vec3 world, vec3 origin, float flex, int mode){
	float t = wind_params.x;
	float phase = dot(origin,vec3(1.73,0.0,2.41));
	vec2 wind = SampleWind(world.xy);
	float speed = length(wind);
	/*
		LEAF (mode 1): by distance from the stem, whatever way the leaf hangs, and mostly flutter -
		a leaf does not lie down in the wind, it trembles and lifts. It swings with the wind, flaps
		up and down, and twists a little through the slab; faster and harder in a stronger wind.
	*/
	if (mode == 1){
		float d = length(world - origin);
		float bend = flex * d * d;
		float flap = sin(t * (7.0 + 0.3 * speed) + phase + d * 6.0);
		float dx = bend * (0.6 * wind.x + speed * 0.5 * flap);
		float dy = bend * (0.4 * wind.y + speed * 0.6 * sin(t * 9.7 + phase * 1.3 + d * 5.0));
		float dz = bend * speed * 0.3 * sin(t * 5.1 + phase * 2.3);
		return WindSwing(world,origin,clamp(vec3(dx,dy,dz),vec3(-0.6 * d),vec3(0.6 * d)));
	}

	float h = world.y - origin.y;
	if (h <= 0.0){
		return world;
	}
	float flutter = 0.25 * sin(t * 5.3 + phase + h * 4.0) + 0.1 * sin(t * 8.9 + phase * 1.7);
	float bend = flex * h * h;
	float dx = clamp(bend * (wind.x + speed * flutter),-0.9 * h,0.9 * h);
	float dz = clamp(bend * speed * 0.35 * sin(t * 3.7 + phase * 2.3),-0.5 * h,0.5 * h);
	return WindSwing(world,origin,vec3(dx,0.0,dz));
}

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
	//vec3 objpos = instance_data[gl_InstanceID].mat_transformscale[3].xyz;

	//Calculate position when using morph targets
	vec3 pos = position; //Position of this vertex
	int voffset = instance_data[gl_InstanceID].vertex_count;
	for (int i=0;i<instance_data[gl_InstanceID].num_morph_targets;i++){
		pos += (morph_vertices[(i*voffset) + gl_VertexID].position * instance_data[gl_InstanceID].morph_factors[i]);
	}

	vec4 world_position = instance_data[gl_InstanceID].mat_transformscale * vec4(pos,1);

	int matindex_out = instance_data[gl_InstanceID].material_slot[int(vertex_words[uint(gl_VertexID) * 12u + 11u])];

	Material m = materials[matindex_out];
	//Before anything reads world_position, so the shadow and the G-buffer bend with the colour.
	if ((wind_size.z != 0) && (m.wind_flex > 0.0)){
		world_position.xyz = WindBend(world_position.xyz,instance_data[gl_InstanceID].mat_transformscale[3].xyz,m.wind_flex,m.wind_mode);
	}
	vposition = world_position.xyz;

	vnormal = (mat_rotate * normal);
	vnormal = normalize(vnormal);
	vshadow = mat_shadow * world_position; //Vertex postition in shadow coordinates
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
	//Which instance this fragment belongs to, for a fragment shader that needs this
	//instance's own row of instance_data - the raymarched volume reads its box transform
	//out of it. Declared since forever but never actually written, which nothing noticed
	//while only one volume existed: every instance then read instance_data[0] and drew at
	//the first one's transform.
	vmatselect = gl_InstanceID;

	gl_Position = (mat_worldcam * world_position);
}