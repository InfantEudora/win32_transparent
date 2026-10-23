#version 450 core
#include "texture_units.glsl"

/*
    THE RAINBOW IN A FALL'S SPRAY (docs/rainbow_plan.md). A box over each sheet's spray, one instance each
    (ApplicationChasmRainbow.cpp), and what it draws is LIGHT, added to the frame: the sun's, sent back by the
    spray's drops at about 42 degrees from the point opposite the sun. So the colour is the view ray's angle from
    that point alone - the same all along the ray - and the spray only says how much of it: a soft cloud in the
    box, marched in a few steps and cut off by the solid scene in the G-buffer, the mist's puffs included.

    The box is the unit cube, -0.5..0.5: x across the fall, y up, z out from the sheet's face. Its transform turns
    and stretches it, so the march runs in the box's own space with t still a world distance - bomber_explosion.frag's
    ray_box_dst says why that holds.
*/

layout (location = 0) out vec4 color;

//From default.vert.
layout (location = 0) in vec3 vposition;        //the box's face, in the world
layout (location = 9) flat in int vmatselect;   //= gl_InstanceID

#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS   4
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

layout (binding = TEXUNIT_GBUFFER_DEPTH) uniform sampler2D gbuffer_depth;          //1.0 where nothing solid was drawn
layout (binding = TEXUNIT_GBUFFER_POSITION) uniform sampler2D gbuffer_position;

uniform vec3 eye_position;
uniform vec2 render_target_size = vec2(1.0);

uniform vec3 rainbow_sun = vec3(0.0,1.0,0.0);  //unit, toward the sun
uniform vec3 rainbow_light = vec3(0.0);        //the sun's colour x its brightness x the bow's strength
uniform float rainbow_width = 1.0;             //the bands' spread: 1 is a real bow's, about 2 degrees across
uniform float rainbow_secondary = 0.4;         //the secondary bow against the primary
uniform float rainbow_inner = 0.12;            //the white glow inside the primary
uniform float rainbow_density = 0.3;           //optical depth a world unit at the cloud's heart
uniform int rainbow_steps = 12;
uniform int rainbow_debug = 0;                 //1 the cloud in white, 2 the bow as if the box were all spray

#define PRIMARY     41.5    //degrees from the point opposite the sun: violet 40.6 inside, red 42.4 outside
#define SECONDARY   51.9    //red 50.4 inside, violet 53.6 outside - the colours the other way round

float Band(float deg, float centre, float sigma){
    float x = (deg - centre) / sigma;
    return exp(-0.5 * x * x);
}

/*
    The light the drops send back at `deg`, before the sun's colour: a few overlapping bands per bow, red outermost
    in the primary and innermost in the secondary, a little red again at the violet end. Between the two bows
    nothing - Alexander's dark band - and inside the primary a faint white glow, brightest near it.
*/
vec3 Bow(float deg){
    float w = rainbow_width;
    float s = 0.45 * w;
    vec3 bow = vec3(Band(deg,PRIMARY + 0.7 * w,s) + 0.35 * Band(deg,PRIMARY - 1.1 * w,0.8 * s),
                    Band(deg,PRIMARY,s),
                    Band(deg,PRIMARY - 0.7 * w,s));
    float s2 = 0.6 * w;
    vec3 second = vec3(Band(deg,SECONDARY - 1.1 * w,s2) + 0.35 * Band(deg,SECONDARY + 1.5 * w,0.8 * s2),
                       Band(deg,SECONDARY,s2),
                       Band(deg,SECONDARY + 1.0 * w,s2));
    float inside = (1.0 - smoothstep(PRIMARY - 2.0 * w,PRIMARY - 0.7 * w,deg)) * exp(-max(PRIMARY - deg,0.0) / 12.0);
    return bow + second * rainbow_secondary + vec3(inside * rainbow_inner);
}

//The spray in the box, 0..1: thickest in the middle across, just off the sheet's face and low, thinning outward
//and upward - where SprayBall's puffs mostly are (Mist.cpp).
float Cloud(vec3 q){
    vec3 u = q + 0.5;
    float across = max(1.0 - 4.0 * q.x * q.x,0.0);
    float out_from = smoothstep(0.0,0.15,u.z) * (1.0 - u.z);
    float up = 1.0 - u.y;
    return across * out_from * up;
}

//The unit cube against a ray in its own space: (distance to it, distance inside it), as world distances.
vec2 RayBox(vec3 origin, vec3 inv_dir){
    vec3 t0 = (vec3(-0.5) - origin) * inv_dir;
    vec3 t1 = (vec3(0.5) - origin) * inv_dir;
    vec3 near = min(t0,t1);
    vec3 far = max(t0,t1);
    float enter = max(max(near.x,near.y),near.z);
    float leave = min(min(far.x,far.y),far.z);
    float to_box = max(0.0,enter);
    return vec2(to_box,max(0.0,leave - to_box));
}

void main(){
    vec3 rd = normalize(vposition - eye_position);
    float deg = degrees(acos(clamp(dot(rd,-rainbow_sun),-1.0,1.0)));
    vec3 bow = Bow(deg);
    //Most of the box is at an angle with no bow in it: nothing to march for.
    if (rainbow_debug != 1 && max(bow.r,max(bow.g,bow.b)) < 0.004){
        discard;
    }

    mat4 to_local = inverse(instance_data[vmatselect].mat_transformscale);
    vec3 local_origin = (to_local * vec4(eye_position,1.0)).xyz;
    vec3 local_dir = (to_local * vec4(rd,0.0)).xyz;
    vec2 hit = RayBox(local_origin,1.0 / local_dir);
    /*
        ONE FRAGMENT A PIXEL, whatever the cull state: the custom pass may come here with culling off (the falls'
        sheets turn it off and nothing turns it back on), and light that adds must not be added twice. From outside,
        only the face the ray comes in by; from inside the box there is only the face it goes out by.
    */
    float t_face = dot(vposition - eye_position,rd);
    if (hit.x > 0.0 && abs(t_face - hit.x) > abs(t_face - (hit.x + hit.y))){
        discard;
    }

    //Cut off by the solid scene in front of the spray's far side, as a blast's march is.
    vec2 screen_uv = gl_FragCoord.xy / render_target_size;
    float span = hit.y;
    if (texture(gbuffer_depth,screen_uv).r < 1.0){
        float to_scene = dot(texture(gbuffer_position,screen_uv).xyz - eye_position,rd);
        span = clamp(to_scene - hit.x,0.0,span);
    }
    if (span <= 0.0){
        discard;
    }

    float dt = span / float(rainbow_steps);
    float depth = 0.0;
    for (int i = 0; i < rainbow_steps; i++){
        vec3 q = local_origin + local_dir * (hit.x + (float(i) + 0.5) * dt);
        depth += Cloud(q) * dt;
    }
    float cover = 1.0 - exp(-depth * rainbow_density);

    if (rainbow_debug == 1){
        color = vec4(vec3(cover * 0.5),0.0);
        return;
    }
    if (rainbow_debug == 2){
        cover = 1.0;
    }
    color = vec4(bow * rainbow_light * cover,0.0);
}
