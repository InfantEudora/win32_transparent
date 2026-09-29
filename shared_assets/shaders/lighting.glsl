/*
    THE LIT SURFACE: materials, lights, the sun's shadow map, cloud and occluder-field shadows,
    and the light loop that combines them. Split out of default.frag unchanged, so that a custom
    shader can colour a surface itself and still be lit exactly like everything around it - the
    archer's water computes its own albedo and hands it to LightSurface.

    WHAT THE INCLUDING FILE MUST HAVE DONE FIRST, because this reads them rather than declaring
    them (a fragment stage's inputs belong to the file that pairs it with a vertex stage):
      - #include "texture_units.glsl"
      - the varyings default.vert writes: vposition, vnormal, vuv, TBN, vmatindex. Declare them
        at default.vert's locations and use default.vert as the vertex stage.

    WHAT A CUSTOM SHADER USING IT MUST ASK FOR: Shader::f_lit. The renderer hands the default and
    skinned shaders the sun's matrix, the filter radius and the two shadow volumes' uniforms every
    frame, and a custom shader only gets them if it says it lights with them - see
    Renderer::UploadLighting. Without it everything here still compiles and runs, lit as if the
    sun cast no shadow at all, which is the kind of wrong that looks nearly right.
*/

//It's set with glBindTextureUnit

//Materials take every unit above the engine's reserved run, so an INDEX HERE IS NOT A
//TEXTURE UNIT: unit = TEXUNIT_MATERIAL_FIRST + index. SampleMaterialTexture is the only
//thing that may bridge the two, and it does it with literals on both sides.
layout (binding = TEXUNIT_MATERIAL_FIRST) uniform sampler2D material_texture[NUM_MATERIAL_UNITS];
//The sun's depth map. A declaration of its own now rather than material_texture[0] - the
//material array no longer covers unit 0, and reading the shadow map through it was only ever
//working by the accident that the array happened to start there.
layout (binding = TEXUNIT_SHADOW) uniform sampler2D shadow_texture;
layout (binding = TEXUNIT_SKYBOX_CUBEMAP) uniform samplerCube environment_map;
//Setting for using reflections from environment map
uniform int f_environment_reflections = 1;

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    float brightness;
    float metallic;
    float roughness;
    int f_unlit;       //see material_t in core/Material.h; was pad2
    float wind_flex;   //see material_t in core/Material.h; was pad3. Only default.vert reads it
    int wind_mode;     //see material_t in core/Material.h; was pad4. Only default.vert reads it
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
    //Self-emitted light: xyz is glTF's emissiveFactor, w a strength multiplier. Must stay last
    //to match material_t in Material.h - see the comment there.
    vec4 emissive;
};

//All the light types fall together into a single light struct
struct Light{
    vec3    position;
    int     shadow;     // Set if the the light produces a shadow
    vec3    direction;	// Direction of 0 means its a point light
    float   brightness;
    vec3    color;
    float   cos_angle; 	// 0 means its a point light, else it becomes a cone light
    //Size of the source in world units, for the penumbra estimate. Appended last, and the
    //next one must be too - see core/light_t, which this is a hand copy of.
    float   radius;
    float   pad0;
    float   pad1;
    float   pad2;
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

uniform vec3 eye_position;
uniform int f_normal_mapping = 1;
uniform int f_materialindex_is_color = 0;
//Cloud shadows: the transmittance map built by shaders/cloud_shadow.comp, bound by the renderer
//at TEXUNIT_CLOUD_SHADOW. Off unless an app has actually given the renderer a map, which only
//the ship app does - see Renderer::UploadCloudShadow.
layout (binding = TEXUNIT_CLOUD_SHADOW) uniform sampler3D cloud_shadow_texture;
uniform mat4 mat_cloud_shadow;
uniform int f_cloud_shadows = 0;
//Occluder field: the top-down min/max height map built by shaders/field.frag, bound by the
//renderer at TEXUNIT_FIELD_SHADOW. This is what shadows POINT lights - see CalcFieldShadow.
//Off unless an app called Renderer::EnableFieldShadows.
layout (binding = TEXUNIT_FIELD_SHADOW) uniform sampler2D field_texture;
uniform mat4 mat_field;
uniform vec3 field_axis = vec3(0,0,1);
uniform int f_field_shadows = 0;
uniform int field_shadow_steps = 64;
uniform float field_normal_bias = 0.15;
uniform float alpha_clip = 1.0f;
//Width of a cone light's soft edge, in cosine space - the `epsilon` of the reference
//shader. Shared with raymarch_volume.frag so a cone matches between surfaces and fog.
uniform float cone_softness = 0.15;
//The sun's shadow filter radius, in shadow-map TEXELS - see CalcShadow. From
//Renderer::shadow_pcf_radius; the default here is only what an app that never sets it gets.
uniform float shadow_pcf_radius = 1.0;
//The sun's matrix, the one the vertex stage already projects vshadow with. Declared here as
//well, with the same location and initialiser (both have to match for the program to link), so
//CalcShadow can project a point of its own choosing rather than only the interpolated vertex one.
layout(location = 1) uniform mat4 mat_shadow = mat4(
	1		,0		,0		,0,
	0		,1		,0		,0,
	0		,0		,1		,0,
	0		,0		,0		,1
);

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

layout (std430, binding = 3) buffer ReadbackBuffer{
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

/*
    THE ONLY WAY A MATERIAL'S TEXTURE MAY BE SAMPLED IN THIS SHADER.

    material_texture[] is an array of samplers, and GLSL only defines indexing one with a value
    that is the same for every fragment of the draw call. m.diffuse_texture is not that: one
    instanced draw covers several objects with different materials, and the per-vertex material
    id changes it from triangle to triangle inside one object besides. So
    `texture(material_texture[m.diffuse_texture], uv)` was undefined, and the undefined behaviour
    on the Intel Iris Xe (driver 32.0.101.7085, measured 2026-09-16) is that the FRAME dies once
    the index really diverges - see the longer note in deferred.frag, where it bit first. NVidia
    reads a sampler per lane and never minded.

    Every case indexes with a LITERAL, which is defined however the index diverges; the compiler
    emits a branch tree, and a flat index only diverges where two materials meet inside one pixel
    group. The derivatives are the globals below, taken at the top of main() where control flow
    is still uniform: implicit-derivative texture() inside a divergent branch is itself undefined,
    and every caller of this sits inside `if (m.diffuse_texture >= 0)`.

    THE CASE LABEL IS A TEXTURE UNIT, THE ARRAY INDEX IS NOT. m.diffuse_texture carries the
    absolute unit UploadMaterials bound to, and material_texture[] starts at
    TEXUNIT_MATERIAL_FIRST, so the two differ by exactly that. Both sides stay literal after
    the preprocessor has been at them, which is what keeps the divergent-index rule above
    satisfied. The shadow map has its own sampler; it is not in this array.
*/
vec2 g_uv_dx = vec2(0.0);
vec2 g_uv_dy = vec2(0.0);

vec4 SampleMaterialTexture(int unit, vec2 uv){
    vec2 dx = g_uv_dx;
    vec2 dy = g_uv_dy;
    switch (unit){
        case TEXUNIT_MATERIAL_FIRST +  0: return textureGrad(material_texture[ 0], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  1: return textureGrad(material_texture[ 1], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  2: return textureGrad(material_texture[ 2], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  3: return textureGrad(material_texture[ 3], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  4: return textureGrad(material_texture[ 4], uv, dx, dy);
#if NUM_MATERIAL_UNITS > 5
        case TEXUNIT_MATERIAL_FIRST +  5: return textureGrad(material_texture[ 5], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  6: return textureGrad(material_texture[ 6], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  7: return textureGrad(material_texture[ 7], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  8: return textureGrad(material_texture[ 8], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST +  9: return textureGrad(material_texture[ 9], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 10: return textureGrad(material_texture[10], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 11: return textureGrad(material_texture[11], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 12: return textureGrad(material_texture[12], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 13: return textureGrad(material_texture[13], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 14: return textureGrad(material_texture[14], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 15: return textureGrad(material_texture[15], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 16: return textureGrad(material_texture[16], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 17: return textureGrad(material_texture[17], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 18: return textureGrad(material_texture[18], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 19: return textureGrad(material_texture[19], uv, dx, dy);
        case TEXUNIT_MATERIAL_FIRST + 20: return textureGrad(material_texture[20], uv, dx, dy);
#endif
        //A unit nothing was bound to. The same magenta a missing material gets in main(), so a
        //bad index looks like a bad material rather than like a plausible surface.
        default: return vec4(0.9, 0.0, 0.5, 1.0);
    }
}

vec3 GetNormalMapNormal(){
    //Bindless
    //vec3 normal = texture(m.handle_normal,vuv).rgb;
    //Default
    vec3 normal = SampleMaterialTexture(m.normal_texture,vuv).rgb;

    normal = (2.0 * normal) - 1.0;
    return normalize(normal);
}

//Returns the light intensity from a single directional light such as the sun
vec3 CalcDirectionalPBRLight(vec3 albedo, vec3 lightdirection, vec3 color, float brightness){

    vec3 N;
    vec3 V;
    vec3 L;
    if ((f_normal_mapping == 1) && (m.normal_texture >= 0)){
        N = GetNormalMapNormal();
        mat3 iTBN = transpose(TBN);
        V = normalize(iTBN * (eye_position- vposition));
        L = normalize(iTBN * (lightdirection));
    }else{
        N = normalize(vnormal);
        V = normalize(eye_position - vposition);
        L = normalize(lightdirection);
    }

    sampled_normal = normalize(vnormal);

    vec3 F0 = vec3(0.04); //Fresnell factor
    F0 = mix(F0, albedo, m.metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);

    // calculate per-light radiance
    vec3 H = normalize(V + L);

    vec3 radiance     = color * brightness;

    // cook-torrance brdf
    //glTF stores *perceptual* roughness. GGX wants alpha = roughness^2, and Smith's
    //direct-lighting geometry term wants k = (roughness+1)^2 / 8. Feeding roughness straight
    //into both (as this used to) renders every material rougher than it was authored.
    //The floor is only there to keep a perfect mirror from dividing by zero.
    float rough = max(m.roughness, 0.03);
    float alpha = rough * rough;
    float k     = ((rough + 1.0) * (rough + 1.0)) / 8.0;

    float NDF = DistributionGGX(N, H, alpha);
    float G   = GeometrySmith(N, V, L, k);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - m.metallic;

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    // add to outgoing radiance Lo
    float NdotL = max(dot(N, L), 0.0);
    //Use the original object normal to completely shadow back faces
    //NdotL = min(dot(vnormal, normalize(lightpos - vposition)),NdotL );

    Lo += (kD * albedo / PI + specular) * radiance * NdotL * m.brightness;
    return Lo;
}

float GetTransparency(){
    if (m.diffuse_texture >= 0){
        //Bindless
        //return texture(m.handle_diffuse,vuv).w;
        //Default
        return SampleMaterialTexture(m.diffuse_texture, vuv).w;
    }
    return m.color.w;
}


/*
    One BILINEAR shadow compare: the four texels that bilinear filtering would blend at uv, each
    compared against depth on its own, and the four yes/no answers blended by where uv sits between
    them. Filtering the depths first and comparing after would be wrong - an edge texel's average
    depth is a surface nobody drew - so it is the answers that get filtered, never the depths.

    This alone turns the edge from a stair of whole texels into a one-texel ramp. The map itself is
    NEAREST; textureGather ignores the filter mode and always returns the linear-filter footprint.
*/
float ShadowCompare2x2(vec2 uv, float depth, vec2 size){
    vec4 lit = step(vec4(depth),textureGather(shadow_texture,uv,0));
    vec2 f = fract(uv * size - 0.5);
    //textureGather's component order runs anticlockwise from the top left: x (0,1), y (1,1),
    //z (1,0), w (0,0) - so w/z are the bottom row and x/y the top.
    return mix(mix(lit.w,lit.z,f.x),mix(lit.x,lit.y,f.x),f.y);
}

/*
    The sun's shadow: percentage-closer filtering over the depth map.

    A grid of ShadowCompare2x2 taps spread over shadow_pcf_radius texels either side, averaged. The
    grid is sized from the radius so the taps are never more than a texel apart - wider than that
    and the bilinear ramps stop overlapping and the edge bands. 0 is a single tap, 1 is 3x3, 2 is
    5x5; capped at 3 (49 taps), past which a bigger map is the better answer.

    THE RADIUS IS IN TEXELS, NOT WORLD UNITS, so the softness is a fraction of the map's own
    resolution: an app that fits its ortho to the view (apps/archer does) gets an edge of constant
    width on screen, and one with a fixed ortho gets one of constant width in the world.

    NORMAL OFFSET, because a wider filter is also a wider self-shadowing test. Every tap is a lookup
    up to (radius + 1) texels away across the receiver's OWN surface, and on a surface steep to the
    light that surface is deeper there than here by (distance * tan) - so a plain depth bias big
    enough for a 3x3 at a grazing angle would detach every shadow from its caster. Lifting the
    lookup off the surface along the normal by (reach * sin) keeps the receiver in front of its own
    surface for the whole kernel, scales to nothing on faces square to the sun, and costs one extra
    matrix multiply. The constant bias only has to cover depth quantisation after that.
*/
float CalcShadow(vec3 world_pos, vec3 normal, vec3 to_light){
    vec2 size = vec2(textureSize(shadow_texture,0));

    //World units per texel, read off the sun's own matrix: an orthographic projection scales x by
    //1/(half width), so the length of the matrix's first ROW is exactly that. Both axes, in case a
    //map is ever not square.
    vec3 row_x = vec3(mat_shadow[0][0],mat_shadow[1][0],mat_shadow[2][0]);
    vec3 row_y = vec3(mat_shadow[0][1],mat_shadow[1][1],mat_shadow[2][1]);
    float texel_world = max(2.0 / (length(row_x) * size.x),2.0 / (length(row_y) * size.y));

    float radius = clamp(shadow_pcf_radius,0.0,3.0);
    float cos_l = clamp(dot(normal,normalize(to_light)),0.0,1.0);
    float sin_l = sqrt(1.0 - cos_l * cos_l);
    //+1 for the bilinear tap's own reach, +0.5 of margin for where the rasteriser put the texel.
    vec3 lifted = world_pos + normal * (texel_world * (radius + 1.5) * sin_l);

    vec4 vposinshadow = mat_shadow * vec4(lifted,1.0);
    vec3 pos_proj = vposinshadow.xyz / vposinshadow.w;

    //Anything outside the sun's ortho box has no depth information, so it cannot be shadowed.
    //Without this the lookup below samples outside [0,1] and returns a meaningless depth, which
    //painted a bright patch onto the scene that tracked the light's position.
    if (any(greaterThan(abs(pos_proj.xy), vec2(1.0))) || (pos_proj.z > 1.0)){
        return 1.0;
    }

    vec2 uvshadow = (0.5 * pos_proj.xy) + 0.5;
    float current_depth = (0.5 * pos_proj.z) + 0.5;
    float bias = 0.00025;
    float depth = current_depth - bias;

    int n = int(ceil(radius));
    float spacing = (n > 0) ? (radius / float(n)) : 0.0;
    float lit = 0.0;
    for (int y = -n; y <= n; y++){
        for (int x = -n; x <= n; x++){
            lit += ShadowCompare2x2(uvshadow + (vec2(x,y) * spacing) / size,depth,size);
        }
    }
    lit /= float((2 * n + 1) * (2 * n + 1));

    //0.2 rather than 0: what the hard compare always left in shadow, so existing scenes keep the
    //darkness they were lit for.
    return mix(0.2,1.0,lit);
/*

    vec3 sunpos = sun.position;
    //vec3 lightvec = sunpos - vposition; //This would be the light vector if the sun had a perspective camera.
    vec3 lightvec = sunpos;
    vec3 nlightvec = normalize(lightvec);

    //float bias = max(0.05 * (1.0 - dot(vnormal, nlightvec)), shadow_bias);
    float bias = max(shadow_bias * (1.0 - dot(vnormal, nlightvec)), shadow_bias/32.0);
    //bias = shadow_bias;
    // check whether current frag pos is in shadow
    //float shadow = (current_depth - bias) - closest_depth;

    //if (shadow < 0){
    //    return 1;
    //}
    //shadow = clamp(shadow,0,1);
    float shadow = (current_depth - bias) > closest_depth  ? 0.0 : 1.0;
    //float shadow =
    return shadow;
*/
}

/*
    How much sunlight survives the raymarched clouds on its way to this point.

    Multiplies the sun term; it does not replace CalcShadow. That one asks "is an opaque surface
    in the way", this one asks "how much light got through the fog", and the answers compose.
    Being an integral rather than a compare, it is already soft - there is no penumbra to fake.

    The map is a 3D texture (shaders/cloud_shadow.comp): XY across the sun's frame, Z along the
    sun ray. Sampling it needs both, and one mat4 multiply produces both - the same projection
    the depth shadow map does, into a frustum fitted to the volumes rather than to the scene.

    Note what is NOT clamped. Outside the map in XY there is no cloud at all, so 1.0. In Z there
    is deliberately no test, because CLAMP_TO_EDGE already gives the right answer at both ends:
    a receiver in front of the layer clamps to the first slice, which nothing has shadowed yet,
    and one past the layer clamps to the last, which has the whole column's worth of cloud in
    front of it. Adding a bounds check there would break the second case.
*/
float CalcCloudShadow(vec3 world_position){
    if (f_cloud_shadows == 0){
        return 1.0;
    }
    vec4 clip = mat_cloud_shadow * vec4(world_position,1.0);
    vec3 proj = clip.xyz / clip.w;
    if (any(greaterThan(abs(proj.xy),vec2(1.0)))){
        return 1.0;
    }
    return texture(cloud_shadow_texture,proj * 0.5 + 0.5).r;
}

/*
    Is anything standing between this point and a point light?

    The third kind of occlusion test in this file, and the only one that is a march. CalcShadow
    compares against a depth written from the light's own viewpoint, which a point light cannot
    have without six of them; CalcCloudShadow integrates along a fixed direction, which a point
    light does not have either. So this one walks the actual segment from the surface to the
    light and asks a view-independent description of the world what it passes through.

    That description is the occluder field (shaders/field.frag): a top-down texture holding, per
    column, the height of the highest and lowest surface in it. A column is therefore treated as
    a SLAB - one solid block between those two heights - which is exact for anything extruded
    from the ground and conservative for anything else. A stack of blocks with a gap in it fills
    its own gap in, and a light shining through that gap is shadowed when it should not be. That
    is the price of a description that costs one texture rather than six per light, and in a
    fixed top-down view it is very hard to see.

    Deliberately view-independent: the field knows nothing about any light, so a scene with ten
    point lights builds it once and marches it ten times.

    This sphere-traces rather than stepping evenly, using the distance channel the jump flood
    filled (shaders/field_jfa.comp). That channel does two separate jobs here, and they must not
    be confused with each other - see the two quantities inside the loop.
*/
float CalcFieldShadow(vec3 world_position, vec3 normal, vec3 light_position, float light_radius){
    if ((f_field_shadows == 0) || (field_shadow_steps <= 0)){
        return 1.0;
    }
    //A receiver sits ON a surface, so at t=0 it is inside that surface's own slab and every
    //fragment shadows itself. Stepping off along the normal first is the fix, and is the same
    //trick - for the same reason - as the depth map's shadow_bias.
    vec3 origin = world_position + normal * field_normal_bias;

    vec3 to_light = light_position - origin;
    float dist = length(to_light);
    if (dist < 0.0001){
        return 1.0;
    }
    vec3 dir = to_light / dist;

    /*
        A floor under the step size, and the reason field_shadow_steps survived becoming a sphere
        trace. The distance field is zero everywhere directly above an occluder, so a ray running
        along the top of a wall would step by nothing and stall a few centimetres from where it
        started - and a march that stops early reports "unoccluded", which is the wrong answer in
        exactly the place that has the most geometry. With this floor, the worst case degrades to
        the even spacing the first version used and still reaches the light.
    */
    float min_step = dist / float(field_shadow_steps);

    /*
        Only the part of the ray over the field is worth marching: off its edge every step below
        is a `continue`. That used to cost nothing because the field covered everything the camera
        saw, and costs a great deal once it does not - archer's covers one bay of a long level,
        and every receiver outside it spent all field_shadow_steps finding nothing, 4.5 ms of
        colour pass at 1440x900. So the segment is clipped to the field's square first.

        The field camera is orthographic (EnableFieldShadows' contract), so clip space is linear
        along the segment and a fraction of it there is the same fraction of `dist`. Inside the
        field this changes nothing: the march starts at min_step exactly as before.
    */
    vec2 clip_a = (mat_field * vec4(origin,1.0)).xy;
    vec2 clip_d = (mat_field * vec4(light_position,1.0)).xy - clip_a;
    float s_in = 0.0, s_out = 1.0;
    for (int k = 0; k < 2; k++){
        if (abs(clip_d[k]) < 1.0e-6){
            if (abs(clip_a[k]) > 1.0){
                return 1.0;         //parallel to this edge and outside it: never over the field
            }
        }else{
            float s0 = (-1.0 - clip_a[k]) / clip_d[k];
            float s1 = ( 1.0 - clip_a[k]) / clip_d[k];
            s_in  = max(s_in,min(s0,s1));
            s_out = min(s_out,max(s0,s1));
        }
    }
    if (s_in >= s_out){
        return 1.0;
    }
    float t_end = s_out * dist;

    float visibility = 1.0;
    float t = max(min_step,s_in * dist);

    for (int i = 0; (i < field_shadow_steps) && (t < t_end); i++){
        vec3 p = origin + dir * t;

        vec4 clip = mat_field * vec4(p,1.0);
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        //Outside the field's box there is no information at all. Reporting "unoccluded" is the
        //only honest answer and matches what CalcShadow does off the edge of the depth map -
        //clamping would instead smear the border column across everything beyond it.
        if ((uv.x < 0.0) || (uv.x > 1.0) || (uv.y < 0.0) || (uv.y > 1.0)){
            t += min_step;
            continue;
        }

        //R is the top of this column's slab, A the bottom, G the 2D distance to the nearest
        //occupied column. An empty column kept the clear values - a very low top and a very high
        //bottom - so `clearance` below comes out enormous and the tests simply miss, which is
        //why the field needs no separate occupancy flag.
        vec4 field = texture(field_texture,uv);
        float h = dot(p,field_axis);

        //Distance above the top of this column's slab, or below its bottom. Negative inside it.
        float clearance = max(h - field.r,field.a - h);
        if (clearance < 0.0){
            return 0.0;
        }

        /*
            Two different distances, and the whole correctness of this loop is in keeping them
            apart.

            The STEP may only use field.g. A 2D distance to the nearest footprint is a genuine
            lower bound on the 3D distance to any occluder, because projecting onto the ground
            plane cannot lengthen anything - so jumping that far can never pass through something.
            The vertical clearance is NOT such a bound: it describes this column only, and a
            neighbouring column one texel away may rise to just under the ray. Stepping by it
            would tunnel straight through that neighbour.

            The PENUMBRA estimate wants the opposite reading, and may be a heuristic because it
            only shades an edge. Above an occupied column field.g is zero while the real occluder
            is `clearance` below; over an empty one clearance is the meaningless sentinel while
            field.g is the real answer. So each case takes the other's value.
        */
        float occupied = (field.r >= field.a) ? 1.0 : 0.0;
        float occluder_distance = mix(field.g,clearance,occupied);

        //The standard sphere-trace penumbra: a ray that passes close to an occluder while still
        //far from the surface it is shading is a soft edge, and how soft is set by how big the
        //light is. This is the term that makes these read as lit by a lamp rather than stencilled.
        visibility = min(visibility,occluder_distance / max(light_radius * t,0.0001));

        t += max(field.g,min_step);
    }
    return clamp(visibility,0.0,1.0);
}


/*
    Everything a lit surface of colour `albedo` receives, with `m` supplying the rest of the
    material (metallic, roughness, brightness, emission): every light, the sun's shadow and the
    two shadow volumes, the ambient term, the reflections and the emission. The surface normal and
    position are the fragment's own, vnormal and vposition.

    What CalcPBRLighting runs for an ordinary material, and what a custom shader that works out
    its own colour calls instead - which is the reason this is a function of its own.
*/
vec3 LightSurface(vec3 albedo){
    vec3 total_light = vec3(0,0,0);

    for (int i = 0; i < lights.length(); i++){
        //Vector from the surface towards the light. For a directional light that is the negated
        //light forward - constant everywhere, so the rays stay parallel and the light's position
        //does not affect the shading. The point light case below overwrites it per fragment.
        vec3 lightdirection = -lights[i].direction;
        vec3 light = lightdirection;
        float direction_len = dot(lights[i].direction,lights[i].direction);
        float falloff = 1.0f;
        float light_value = 1.0;
        //What gets handed to CalcDirectionalPBRLight as the light's brightness. For the
        //point and sun paths this stays lights[i].brightness, which means brightness is
        //applied TWICE for a point light - once here in light_value and again as radiance
        //inside that function. That is long-standing behaviour and every existing light is
        //tuned around it, so it is left alone. The cone path below sets this to 1 instead,
        //because its light_value already carries the full intensity - squaring a headlight
        //at brightness 30 blows a panel out to a flat colour.
        float shading_brightness = lights[i].brightness;

        if (direction_len < 0.1){
            //Makes it a point light instead of direction
            lightdirection = lights[i].position - vposition;

            float dist  = length(lights[i].position - vposition);
            float brightness = lights[i].brightness / pow(dist,falloff);

            if (brightness < 0.01){
                continue;
            }
            //The one light type the depth shadow map cannot serve, and the reason the occluder
            //field exists. Gated on the light's own flag so a fill light can stay cheap.
            if (lights[i].shadow != 0){
                brightness *= CalcFieldShadow(vposition,normalize(vnormal),lights[i].position,lights[i].radius);
                if (brightness < 0.01){
                    continue;
                }
            }
            light_value = brightness;
        }else if (lights[i].cos_angle > 0.0){
            /*
                Cone light. It has a direction like the sun, so without this branch it would be
                treated AS a sun - lighting everything from that direction with a shadow lookup
                into the one shadow map - which is what happened before cone lights were uploaded
                at all (see Renderer::UploadLights).

                `direction` is the way the light travels, so -direction is the cone axis measured
                from the lit point back towards the source, and theta is 1 on the axis falling
                off outwards. Same test and same cosine-space soft edge as the volume uses, so a
                cone reads the same on a surface as it does in fog.
            */
            vec3 to_light = lights[i].position - vposition;
            float dist = length(to_light);
            float theta = dot(to_light / max(dist,0.0001),normalize(-lights[i].direction));
            if (theta < lights[i].cos_angle){
                continue;
            }
            float edge = clamp((theta - lights[i].cos_angle) / max(cone_softness,0.0001),0.0,1.0);
            float brightness = lights[i].brightness / pow(dist,falloff);
            if (brightness * edge < 0.01){
                continue;
            }
            lightdirection = to_light;
            light_value = brightness * edge;
            shading_brightness = 1.0;
        }else{
            //Sun. Two independent occluders: opaque geometry through the depth shadow map, and
            //cloud through the transmittance map. They multiply - one is a compare, the other an
            //integral, and neither can express the other.
            float shadow = CalcShadow(vposition,normalize(vnormal),lightdirection);
            light_value = shadow * CalcCloudShadow(vposition);
        }

        light = light_value * CalcDirectionalPBRLight(albedo,lightdirection,lights[i].color,shading_brightness);
        total_light += light;
    }

    //Add some ambient
    total_light += 0.1f * albedo;

    // Environment reflections from cubemap
    if (f_environment_reflections > 0){
        vec3 N = normalize(vnormal);
        vec3 V = normalize(eye_position - vposition);
        vec3 R = reflect(-V, N);
        vec3 env = texture(environment_map, R).rgb;
        // Metallic surfaces reflect the environment fully; rough surfaces don't.
        float env_strength = m.metallic * (1.0 - m.roughness);
        total_light += env * env_strength;
    }


    //total_light *= 0.51f;
    //total_light += vshadow.xyz;

    //What the surface emits by itself. Added after every light, the ambient term and the
    //reflections, and deliberately not multiplied by any of them: an emissive surface glows in
    //full shadow, which is the whole difference between this and m.brightness.
    total_light += m.emissive.rgb * m.emissive.w;
    return total_light;
}

/*
    Sets `m` from this fragment's material index - or, with none, to the obvious magenta, so a
    missing material shows up as itself rather than as something plausible. Call it before either
    of the two above.
*/
void SelectMaterial(){
    //We have to do this again here. We could use vmatindex... but intel.
    //On intel, use matindex_out.
    //On nvidia, use vmatindex.
    if (vmatindex > -1){
        m = materials[vmatindex];
    }else{
        m.diffuse_texture = -1;
        m.normal_texture = -1;
        m.color = vec4(0.9,0.0,0.5,1.0);
        //The rest of the struct was left uninitialised here, which the compiler warns about
        //("m.emissive might be used before being initialized"). It mattered less when every
        //unset field only scaled the lit result; emission is ADDED, so garbage here would
        //show up as an arbitrary glow on anything with no material.
        //
        //EVERY FIELD ADDED TO material_t HAS TO BE SET HERE. f_unlit is the worst of them so
        //far: it is a BRANCH rather than a term, so uninitialised it decides whether this
        //fragment is lit at all - and if it came up non-zero it would return the magenta
        //flat, which is the one case where the magenta is trying to tell you something.
        m.brightness = 1.0;
        m.metallic = 0.0;
        m.roughness = 0.5;
        m.f_unlit = 0;
        m.emissive = vec4(0.0,0.0,0.0,1.0);
    }
}

//The material's own surface, lit: its texture or colour through LightSurface, or through nothing
//at all when it is unlit.
vec4 CalcPBRLighting(){
    vec3 albedo;
    if (m.diffuse_texture >= 0){
        //Bindless
        //albedo = texture(m.handle_diffuse,vuv).xyz;// * m.color.xyz;
        //albedo = texture(m.handle_normal,vuv).xyz;// * m.color.xyz;
        //Default
        albedo = SampleMaterialTexture(m.diffuse_texture, vuv).rgb;
    }else{
        albedo = m.color.xyz;
    }

    /*
        Unlit: the surface IS its albedo. Everything in LightSurface - the light loop, the ambient
        term, the reflections, the shadow lookups - is skipped, so the colour that reaches the
        screen is the one in the material.

        Emission is still added, because it is the one thing that was never lighting: it is what
        lets an unlit surface be BRIGHTER than its own colour rather than exactly it. Alpha is
        resolved the same way the lit path resolves it, so alpha clipping behaves identically.
    */
    if (m.f_unlit != 0){
        float unlit_alpha = 1 - step(GetTransparency(),alpha_clip);
        return vec4(albedo + m.emissive.rgb * m.emissive.w,unlit_alpha);
    }

    vec3 total_light = LightSurface(albedo);

    float alpha = 1 - step(GetTransparency(),alpha_clip);
    //if (alpha < alpha_clip){
    //    discard;
    //}
    return vec4(total_light ,alpha);
}
