#version 430 core

/*
    Raymarched volume material.

    Drawn as an ordinary instanced mesh through the MESH_MODE_SHADER pass (see
    Renderer::DrawFrame), so only the pixels the box covers ever run this code. The box the
    march happens in is NOT uploaded as a uniform: default.vert already hands us
    vmatselect (= gl_InstanceID), so we can read this instance's own transform out of the
    instance SSBO and march the unit cube in the object's LOCAL space. That means the
    volume is positioned, scaled and rotated by moving the Object, it costs no per-draw
    uniform upload, and several volumes still batch into one draw call.

    Density comes from the tileable 3D worley in shaders/noise3d.comp - see density_at() for
    the FBM, threshold and edge falloff that turn it into cloud rather than even haze.
    Lighting is the sun plus every point and cone light, each with its own march towards it
    so the cloud shadows itself.

    The march stops at solid geometry, which is what lets something sit half-buried in the
    volume: Renderer::CustomShaderPass binds the deferred G-buffer (filled before the color
    pass) and we clamp the march to whatever the scene depth says is in front of us.
*/

layout (location = 0) out vec4 color;

//From default.vert.
layout (location = 0) in vec3 vposition;        //Fragment position in world space
layout (location = 9) flat in int vmatselect;   //= gl_InstanceID, our index into instance_data

//All the light types fall together into a single light struct - matches light_t in Light.h
struct Light{
    vec3    position;
    int     shadow;     // Set if the light produces a shadow
    vec3    direction;	// Direction of 0 means its a point light
    float   brightness;
    vec3    color;
    float   cos_angle; 	// 0 means its a point light, else it becomes a cone light
};

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

layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 2) buffer LightBuffer{
	Light lights[];
};

//The deferred G-buffer of the solid scene, bound by Renderer::CustomShaderPass. The binding
//numbers are TEXUNIT_GBUFFER_* in Renderer.h and have to stay in step with them.
layout (binding = 1) uniform sampler2D gbuffer_depth;      //Window-space depth, 1.0 where nothing was drawn
layout (binding = 2) uniform sampler2D gbuffer_position;   //World-space position of the solid scene

//The tileable 3D worley written by shaders/noise3d.comp, bound by the app at
//TEXUNIT_APP_RESERVED (Renderer.h). All four channels are the same noise at doubling
//frequencies, so density_at builds its FBM from ONE fetch.
layout (binding = 25) uniform sampler3D noise_texture;

uniform vec3 eye_position;

uniform float noise_scale = 1.0;        // Noise tiles across the box this many times
uniform vec3 noise_offset = vec3(0);    // Scrolls the noise through the box - the wind
uniform float density_threshold = 0.45; // Noise below this is empty air. What makes clumps
uniform float edge_falloff = 0.15;      // Object-space distance over which the cloud fades out
                                        //  before the box face, so it has no flat sides

uniform float volume_density = 1.0;     // Density per world unit inside the box
uniform float light_absorption = 1.0;   // Multiplier on how fast light is extinguished
//How much cloud brightness one unit of the sun's own `brightness` is worth. A scale is needed
//because a surface turns radiance into pixels through a BRDF and an NdotL while the volume
//integrates it over density and step length, so the same number does not land in the same
//place. It is a SCALE on the sun's brightness though, not a replacement for it - the sun's
//brightness used to be ignored here entirely, which meant dragging it in the light inspector
//did nothing to the clouds.
uniform float sun_intensity = 0.15;
uniform int num_view_steps = 32;        // Steps along the view ray
uniform int num_light_steps = 6;        // Steps along the ray towards the sun, per view step
uniform int num_point_light_steps = 3;  // Same for a point/cone light. Fewer, because their
                                        //  reach is short and the cost is per light
//Attenuation exponent: brightness/pow(distance,this). 2 is the inverse square the reference
//shader uses. NOTE that default.frag lights SURFACES with an exponent of 1, so at 2 a light
//reads dimmer at range in the fog than it does on a hull next to it - set this to 1 to match.
uniform float light_falloff = 2.0;
//Width of a cone light's soft edge in cosine space - the reference's `epsilon`. Same name and
//meaning in default.frag, so a cone looks the same on a surface as in fog.
uniform float cone_softness = 0.15;
//Ceiling on the in-scattered radiance at one sample, as the reference clamps its Lo. With a
//square falloff the radiance next to a light inside the cloud runs away and a single sample
//would swamp the whole march.
uniform float max_radiance = 10.0;

//Lights past this are ignored. The engine has no per-volume light list, so this is just a bound
//on the worst case: the cost of the march is num_view_steps * (num_light_steps + lights *
//num_point_light_steps).
#define MAX_VOLUME_LIGHTS 8
//Debug views. 1 = ignore density and read out the marched interval, which is the quickest check
//that the box is being intersected as a slab. 2 = read out the G-buffer this shader is given:
//red where the depth buffer says there is geometry, blue where it says there is not, green
//scaled by how far away the recorded world position is. If 2 comes out all blue, the G-buffer is
//not reaching this shader and the depth clamp below cannot possibly be working.
uniform int f_show_box = 0;

//The volume fills the unit cube in object space. The cube mesh is built to match (see
//ApplicationShip::BuildVolumeCube), so an Object scale of (10,4,10) is a 10x4x10 box.
#define BOX_MIN vec3(-0.5)
#define BOX_MAX vec3( 0.5)

/*
    Slab test against the unit cube. Returns (distance to the box, distance travelled inside
    it), both as values of the ray parameter t.

    Called with a ray that has already been taken into object space, but NOT renormalised:
    because the transform is affine, inv*(ro + t*rd) == (inv*ro) + t*(inv*rd), so the same t
    describes both rays. rd is a unit vector in world space, so every t here - and therefore
    every step size below - is a real world-space distance, which is what the density and
    Beer's law need. Renormalising the local direction would silently break that on any
    non-uniformly scaled volume.
*/
vec2 ray_box_dst(vec3 local_origin, vec3 inv_local_dir){
    vec3 t0 = (BOX_MIN - local_origin) * inv_local_dir;
    vec3 t1 = (BOX_MAX - local_origin) * inv_local_dir;
    vec3 tsmall = min(t0,t1);
    vec3 tbig   = max(t0,t1);

    float dst_a = max(max(tsmall.x,tsmall.y),tsmall.z);   //Entry
    float dst_b = min(min(tbig.x,tbig.y),tbig.z);         //Exit

    float dst_to_box  = max(0.0,dst_a);                   //0 when the eye is inside the box
    float dst_inside  = max(0.0,dst_b - dst_to_box);      //0 when the box is missed or behind us
    return vec2(dst_to_box,dst_inside);
}

/*
    Cloud density at p, which is in object space - so inside the box p runs -0.5..0.5 on every
    axis regardless of the volume's world size or rotation, which is already a natural UVW for
    the noise texture.

    Three things turn raw noise into something cloud-shaped:

    - An FBM over the four channels. They are the same tileable worley at doubling frequencies
      (see noise3d.comp), weighted so the largest cells decide where the cloud IS and the finer
      ones only rough up its surface.

    - A threshold. Clouds are mostly empty air with dense clumps, not an even haze, so
      everything below `density_threshold` is cut to nothing and what remains is rescaled to
      keep its full range. Without this the whole box just fogs up uniformly.

    - An edge falloff, so the cloud fades out before the box and does not end in a flat plane.
      Taken per axis from the distance to the nearest face, which is cheap and, because it is in
      object space, follows the box when it is scaled or rotated.
*/
float density_at(vec3 p){
    vec3 uvw = (p + 0.5) * noise_scale + noise_offset;
    vec4 n = texture(noise_texture,uvw);

    float fbm = n.r * 0.55 + n.g * 0.25 + n.b * 0.13 + n.a * 0.07;

    //Rescaled rather than just clipped, so raising the threshold thins the cloud out instead of
    //dimming all of it towards zero.
    float shaped = (fbm - density_threshold) / max(1.0 - density_threshold,0.0001);
    shaped = clamp(shaped,0.0,1.0);

    vec3 to_face = 0.5 - abs(p);
    float edge = min(min(to_face.x,to_face.y),to_face.z);
    shaped *= smoothstep(0.0,max(edge_falloff,0.0001),edge);

    return shaped * max(volume_density,0.0);
}

/*
    Transmittance from p towards a light: how much of that light survives the cloud between the
    two. This is what makes one part of the cloud shadow another.

    Stops at the box, or at `max_dst` if that is nearer. For the sun `max_dst` is effectively
    infinite, but a point light sits at a finite distance and often INSIDE the cloud, where
    marching all the way to the box face would shadow it with density that is behind it.

    `local_dir` must be an object-space direction whose world image is a unit vector, so that
    the step sizes here - and `max_dst` - are world distances. See the note at the call site.
*/
float light_march(vec3 p, vec3 local_dir, float max_dst, int steps){
    float dst_inside = min(ray_box_dst(p,1.0 / local_dir).y,max_dst);
    if (steps < 1 || dst_inside <= 0.0){
        return 1.0;
    }
    float step_size = dst_inside / float(steps);
    float total_density = 0.0;
    for (int i = 0;i < steps;i++){
        p += local_dir * step_size;
        total_density += density_at(p) * step_size;
    }
    return exp(-total_density * light_absorption);
}

/*
    Which kind of light lights[i] is. The engine packs all three into one light_t
    (Renderer::UploadLights): no direction means a point light, a direction with no cos_angle is
    the sun, and a direction WITH a cos_angle is a cone. The 0.1 threshold matches default.frag.

    Worth spelling out because the sun test here used to be "has a direction", which a cone
    light also satisfies - so a cone earlier in the buffer would have been used as the sun.
*/
bool light_is_directional(int i){
    return dot(lights[i].direction,lights[i].direction) > 0.1;
}
bool light_is_sun(int i){
    return light_is_directional(i) && (lights[i].cos_angle <= 0.0);
}
bool light_is_cone(int i){
    return light_is_directional(i) && (lights[i].cos_angle > 0.0);
}

void main(){
    mat4 to_local = inverse(instance_data[vmatselect].mat_transformscale);

    //The ray. We are rasterising the box's back faces, so vposition is the far side of the
    //box, but the analytic intersection below does not care which face got rasterised - it
    //works identically with the camera outside or inside the volume.
    vec3 rd = normalize(vposition - eye_position);
    vec3 local_origin = (to_local * vec4(eye_position,1.0)).xyz;
    vec3 local_dir    = (to_local * vec4(rd,0.0)).xyz;

    vec2 hit = ray_box_dst(local_origin,1.0 / local_dir);
    float dst_to_box = hit.x;
    float dst_inside = hit.y;
    if (dst_inside <= 0.0){
        discard;
    }

    /*
        Clamp the march to the solid scene in front of us.

        The ordinary depth test on the box's own faces only settles whether this pixel is drawn
        at all; it says nothing about geometry sitting PART WAY through the volume, which
        without this would be buried under the full depth of fog instead of the right fraction
        of it. So: read the world position the G-buffer recorded for this pixel and shorten the
        interval to whatever is nearer.

        The world position is used rather than the depth value because it needs no inverse
        projection - the distance is just how far that point is along the ray. Depth is still
        read, but only as the "is there anything here at all" test: the position attachment
        clears to (0,0,0), which is a real place in the world, while depth clears to 1.0.
    */
    vec2 screen_uv = gl_FragCoord.xy / vec2(textureSize(gbuffer_depth,0));
    if (texture(gbuffer_depth,screen_uv).r < 1.0){
        vec3 scene_position = texture(gbuffer_position,screen_uv).xyz;
        float dst_to_scene = dot(scene_position - eye_position,rd);
        dst_inside = clamp(dst_to_scene - dst_to_box,0.0,dst_inside);
        if (dst_inside <= 0.0){
            discard;   //The scene is in front of the volume entirely.
        }
    }

    if (f_show_box == 1){
        //Straight readout of how far the ray travels through the box. Uniform grey means the
        //box is being intersected as a slab and not as its rasterised silhouette.
        float d = clamp(dst_inside * 0.2,0.0,1.0);
        color = vec4(vec3(1.0),d);
        return;
    }
    if (f_show_box == 2){
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(gbuffer_depth,0));
        float gdepth = texture(gbuffer_depth,uv).r;
        vec3 gpos = texture(gbuffer_position,uv).xyz;
        float has_geometry = gdepth < 1.0 ? 1.0 : 0.0;
        color = vec4(has_geometry,
                     clamp(length(gpos - eye_position) / 100.0,0.0,1.0),
                     1.0 - has_geometry,
                     1.0);
        return;
    }

    //The sun. Its direction is constant over the whole volume, so it is taken to object space
    //once, out here. `direction` is the way the light travels, so the direction TOWARDS it is
    //the negative of that.
    vec3 sun_dir = vec3(0,1,0);
    vec3 sun_color = vec3(1,1,1);
    float sun_brightness = 1.0;

    int num_lights = min(lights.length(),MAX_VOLUME_LIGHTS);
    //Point and cone light positions in object space, so the per-sample work below is a subtract
    //rather than a matrix multiply. Entries for the sun are written but never read.
    vec3 light_local[MAX_VOLUME_LIGHTS];
    for (int i = 0;i < num_lights;i++){
        light_local[i] = (to_local * vec4(lights[i].position,1.0)).xyz;
        if (light_is_sun(i)){
            sun_dir = normalize(-lights[i].direction);
            sun_color = lights[i].color;
            sun_brightness = lights[i].brightness;
        }
    }
    vec3 local_sun_dir = (to_local * vec4(sun_dir,0.0)).xyz;

    //The rotation+scale of the object transform, for turning an object-space offset back into a
    //world-space one. Needed because every distance that matters - the falloff, and the light
    //march's step length - has to be measured in world units.
    mat3 to_world_rot = mat3(instance_data[vmatselect].mat_transformscale);

    float step_size = dst_inside / float(max(num_view_steps,1));
    //Half a step in, so samples sit in the middle of the slabs they represent.
    vec3 p = local_origin + local_dir * (dst_to_box + step_size * 0.5);

    float transmittance = 1.0;
    //A vec3 now, not a float: with one light its colour could be applied once at the end, but
    //lights of different colours have to be accumulated already coloured.
    vec3 light_energy = vec3(0.0);
    for (int i = 0;i < num_view_steps;i++){
        float density = density_at(p);
        if (density > 0.0){
            //Sun first - one object-space direction for the whole volume, and no distance limit.
            vec3 scatter = sun_color * sun_brightness * sun_intensity
                         * light_march(p,local_sun_dir,1.0e9,num_light_steps);

            //Then every point and cone light. No culling: at most a handful are ever active
            //(the lasers and particles condense into one light), so a reach test would cost
            //more than it saves.
            for (int li = 0;li < num_lights;li++){
                if (light_is_sun(li)){
                    continue;
                }
                /*
                    The direction to a local light changes at every sample, and normalising it
                    in object space would be WRONG: under a non-uniform scale the normalised
                    local vector is not the local image of the normalised world one, and this
                    whole shader depends on t being a world distance.

                    Dividing the object-space offset by its WORLD length gives exactly the local
                    vector whose world image is a unit vector - so stepping by t still moves t
                    world units, and that same length is the falloff distance.
                */
                vec3 offset_local = light_local[li] - p;
                vec3 offset_world = to_world_rot * offset_local;
                float dst = length(offset_world);
                if (dst < 0.0001){
                    continue;
                }
                vec3 dir_local = offset_local / dst;

                float attenuation = lights[li].brightness / pow(dst,light_falloff);
                if (attenuation < 0.01){
                    continue;
                }

                if (light_is_cone(li)){
                    //-direction is the cone axis measured from the sample back towards the
                    //source, so theta is 1 on the axis and falls off outwards.
                    float theta = dot(offset_world / dst,normalize(-lights[li].direction));
                    if (theta < lights[li].cos_angle){
                        continue;
                    }
                    attenuation *= clamp((theta - lights[li].cos_angle)
                                         / max(cone_softness,0.0001),0.0,1.0);
                }

                //Self-shadowing towards this light, stopping AT it rather than at the box.
                attenuation *= light_march(p,dir_local,dst,num_point_light_steps);
                scatter += lights[li].color * attenuation;
            }

            scatter = min(scatter,vec3(max_radiance));

            light_energy += density * step_size * transmittance * scatter;
            transmittance *= exp(-density * step_size * light_absorption);
            if (transmittance < 0.01){
                break;
            }
        }
        p += local_dir * step_size;
    }

    float alpha = 1.0 - transmittance;
    if (alpha <= 0.001){
        discard;
    }

    //The pipeline blends with GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA (Renderer::SetOpenGLState),
    //so whatever we write here is multiplied by alpha again. light_energy is already an
    //absolute amount of scattered light, and already carries each light's colour, so divide
    //alpha back out to land on exactly light_energy over the background.
    color = vec4(light_energy / max(alpha,0.0001),alpha);
}
